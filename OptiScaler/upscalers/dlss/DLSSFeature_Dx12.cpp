#include <pch.h>
#include "DLSSFeature_Dx12.h"
#include <dxgi1_4.h>
#include <Config.h>
#include <misc/IlionD4r.h>
#include <imgui/ImGuiNotify.hpp>

PFN_D3D12_CreateFeature DLSSFeatureDx12::CreateFeature() const
{
    if (_custom != nullptr)
        return _custom->dx12Inited ? _custom->module.D3D12_CreateFeature : nullptr;

    return NVNGXProxy::D3D12_CreateFeature();
}

PFN_D3D12_EvaluateFeature DLSSFeatureDx12::EvaluateFeature() const
{
    if (_custom != nullptr)
        return _custom->dx12Inited ? _custom->module.D3D12_EvaluateFeature : nullptr;

    return NVNGXProxy::D3D12_EvaluateFeature();
}

PFN_D3D12_ReleaseFeature DLSSFeatureDx12::ReleaseFeature() const
{
    if (_custom != nullptr)
        return _custom->dx12Inited ? _custom->module.D3D12_ReleaseFeature : nullptr;

    return NVNGXProxy::D3D12_ReleaseFeature();
}

bool DLSSFeatureDx12::InitInternal(ID3D12GraphicsCommandList* InCommandList, NVSDK_NGX_Parameter* InParameters)
{
    if (IsInited())
        return true;

    return InitDLSS(InCommandList, InParameters);
}

bool DLSSFeatureDx12::InitDLSS(ID3D12GraphicsCommandList* InCommandList, NVSDK_NGX_Parameter* InParameters)
{
    if (_custom == nullptr && NVNGXProxy::NVNGXModule() == nullptr)
    {
        LOG_ERROR("nvngx.dll not loaded!");
        return false;
    }

    if (!_dlssInited)
    {
        _dlssInited = _custom != nullptr ? NVNGXProxy::InitCustomDx12(*_custom, Device) : NVNGXProxy::InitDx12(Device);

        if (!_dlssInited)
            return false;

        // LoadCustom already checked the functions of a custom runtime
        if (_custom == nullptr)
        {
            _moduleLoaded =
                (NVNGXProxy::D3D12_Init_ProjectID() != nullptr || NVNGXProxy::D3D12_Init_Ext() != nullptr) &&
                (NVNGXProxy::D3D12_Shutdown() != nullptr || NVNGXProxy::D3D12_Shutdown1() != nullptr) &&
                (NVNGXProxy::D3D12_GetParameters() != nullptr || NVNGXProxy::D3D12_AllocateParameters() != nullptr) &&
                NVNGXProxy::D3D12_DestroyParameters() != nullptr && NVNGXProxy::D3D12_CreateFeature() != nullptr &&
                NVNGXProxy::D3D12_ReleaseFeature() != nullptr && NVNGXProxy::D3D12_EvaluateFeature() != nullptr;
        }

        // delay between init and create feature
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    LOG_INFO("Creating DLSS feature");

    if (CreateFeature() != nullptr)
    {
        ProcessInitParams(InParameters);

        _p_dlssHandle = &_dlssHandle;

        NVSDK_NGX_Result nvResult;
        {
            ScopedSkipHeapCapture skipHeapCapture {};

            nvResult = CreateFeature()(InCommandList, NVSDK_NGX_Feature_SuperSampling, InParameters, &_p_dlssHandle);
        }

        if (nvResult != NVSDK_NGX_Result_Success)
        {
            LOG_ERROR("_CreateFeature result: {0:X}", (unsigned int) nvResult);
            _p_dlssHandle = nullptr; // NGX never issued it
            return false;
        }
        else
        {
            LOG_INFO("_CreateFeature result: NVSDK_NGX_Result_Success");
        }
    }
    else
    {
        LOG_ERROR("_CreateFeature is nullptr");
        return false;
    }

    ReadVersion(_custom != nullptr ? _custom->module.filePath : L"");

    IlionD4r::OnFeatureCreated();

    SetInit(true);
    return true;
}

bool DLSSFeatureDx12::EvaluateInternal(ID3D12GraphicsCommandList* InCommandList, NVSDK_NGX_Parameter* InParameters)
{
    if (!_moduleLoaded)
    {
        LOG_ERROR("nvngx.dll or _nvngx.dll is not loaded!");
        return false;
    }

    NVSDK_NGX_Result nvResult;

    if (EvaluateFeature() != nullptr)
    {
        ProcessEvaluateParams(InParameters);

        // ILION experiment: the render preset in the NGX parameters, before the call and once d4r has taken them
        const bool d4r = State::Instance().dlssExternalBackend;
        auto commonPreset = [InParameters]() -> uint32_t
        {
            static const char* names[] = { "DLSS.Hint.Render.Preset.DLAA",        "DLSS.Hint.Render.Preset.Quality",
                                           "DLSS.Hint.Render.Preset.Balanced",    "DLSS.Hint.Render.Preset.Performance",
                                           "DLSS.Hint.Render.Preset.UltraPerformance",
                                           "DLSS.Hint.Render.Preset.UltraQuality" };
            uint32_t common = IlionD4r::kPresetUnknown;
            for (const char* name : names)
            {
                unsigned int value = 0;
                if (InParameters->Get(name, &value) != NVSDK_NGX_Result_Success)
                    return IlionD4r::kPresetUnknown;

                if (common == IlionD4r::kPresetUnknown)
                    common = value;
                else if (common != value)
                    return IlionD4r::kPresetMixed;
            }
            return common;
        };
        const uint32_t presetBefore = d4r ? commonPreset() : IlionD4r::kPresetUnknown;

        nvResult = EvaluateFeature()(InCommandList, _p_dlssHandle, InParameters, NULL);
        IlionD4r::OnEvaluate((uint32_t) nvResult);
        if (d4r)
            IlionD4r::OnPresets(presetBefore, commonPreset());

        if (nvResult != NVSDK_NGX_Result_Success)
        {
            LOG_ERROR("_EvaluateFeature result: {0:X}", (unsigned int) nvResult);
            return false;
        }
    }
    else
    {
        LOG_ERROR("_EvaluateFeature is nullptr");
        return false;
    }

    _frameCount++;

    return true;
}

void DLSSFeatureDx12::Shutdown(ID3D12Device* InDevice)
{
    if (_dlssInited)
    {
        if (_custom != nullptr)
        {
            NVNGXProxy::ShutdownCustomDx12(*_custom, InDevice);
        }
        // Inputs share the same NGX instance, it is shut down once. Skipped on exit like the inputs do
        else if (NVNGXProxy::IsDx12Inited() && !State::Instance().isShuttingDown)
        {
            if (NVNGXProxy::D3D12_Shutdown() != nullptr)
                NVNGXProxy::D3D12_Shutdown()();
            else if (NVNGXProxy::D3D12_Shutdown1() != nullptr)
            {
                unsigned int remaining = 0;
                NVNGXProxy::D3D12_Shutdown1()(InDevice, &remaining);
            }

            NVNGXProxy::SetDx12Inited(false);
        }
    }

    DLSSFeature::Shutdown();
}

DLSSFeatureDx12::DLSSFeatureDx12(unsigned int InHandleId, NVSDK_NGX_Parameter* InParameters,
                                 const std::string& customUpscaler)
    : IFeature(InHandleId, InParameters), IFeature_Dx12(InHandleId, InParameters), DLSSFeature(InHandleId, InParameters)
{
    if (!customUpscaler.empty())
    {
        _custom = NVNGXProxy::LoadCustom(customUpscaler, API::DX12);
        _moduleLoaded = _custom != nullptr;

        if (!_moduleLoaded)
            ImGui::InsertNotification(
                { ImGuiToastType::Error, 10000, "Can't load custom upscaler %s", customUpscaler.c_str() });

        return;
    }

    if (NVNGXProxy::NVNGXModule() == nullptr)
    {
        LOG_INFO("nvngx.dll not loaded, now loading");
        NVNGXProxy::InitNVNGX();
    }

    LOG_INFO("binding complete!");
}

DLSSFeatureDx12::~DLSSFeatureDx12()
{
    if (State::Instance().isShuttingDown)
        return;

    if (ReleaseFeature() != nullptr && _p_dlssHandle != nullptr)
        ReleaseFeature()(_p_dlssHandle);
}
