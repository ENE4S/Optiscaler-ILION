#include "pch.h"
#include "Nvngx_FakeMichau.h"

#include <NVNGX_Parameter.h>
#include <plugins/PluginApi.h>
#include <proxies/KernelBase_Proxy.h>

namespace
{
constexpr char ProviderName[] = "DLSSG-XeFG";
constexpr wchar_t ProviderPath[] = LR"(custom\DLSSG-XeFG\dlssg\dlssg_to_xefg_intel_is_better.dll)";
}

std::filesystem::path Nvngx_FakeMichau::FindDll()
{
    namespace fs = std::filesystem;
    const auto main = Config::Instance()->MainDllPath.value_or(L"");
    const auto proxyFolder = Util::DllPath().parent_path();
    const auto gameFolder = Util::ExePath().parent_path();

    // Match the official layout under MainDllPath and the ILION packaged layout when MainDllPath falls back.
    for (const auto& root : { fs::path(main), proxyFolder / L"OptiScaler", gameFolder / L"OptiScaler" })
    {
        if (root.empty())
            continue;

        std::error_code error;
        const auto candidate = (root / ProviderPath).lexically_normal();
        if (fs::is_regular_file(candidate, error))
            return candidate;
    }

    return {};
}

bool Nvngx_FakeMichau::InUse()
{
    const auto config = Config::Instance();
    return config->FGNvngxReplacement.value_or_default() == FGNvngxReplacement::FakeMichauXeFG &&
           (config->FGInput.value_or_default() == FGInput::NvngxFG || config->FGOutput.value_or_default() == FGOutput::DLSSG);
}

void Nvngx_FakeMichau::LoadLibraries()
{
    const auto path = FindDll();
    if (path.empty())
    {
        LOG_ERROR("DLSSG-XeFG: missing {}", wstring_to_string(ProviderPath));
        return;
    }

    dll = KernelBaseProxy::LoadLibraryExW_()(path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR |
                                                                  LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (dll == nullptr)
    {
        const auto error = GetLastError();
        LOG_ERROR("DLSSG-XeFG: could not load {} (Win32 {}); check its architecture and dependencies",
                  wstring_to_string(path.wstring()), error);
        return;
    }

    bool complete = true;
    const auto bind = [this, &complete](auto& function, const char* name)
    {
        function = reinterpret_cast<std::remove_reference_t<decltype(function)>>(KernelBaseProxy::GetProcAddress_()(dll, name));
        if (function == nullptr)
        {
            LOG_ERROR("DLSSG-XeFG: missing export {}", name);
            complete = false;
        }
    };

    bind(_DLSSG_D3D12_Init, "NVSDK_NGX_D3D12_Init");
    bind(_DLSSG_D3D12_Init_Ext, "NVSDK_NGX_D3D12_Init_Ext");
    bind(_DLSSG_D3D12_Shutdown, "NVSDK_NGX_D3D12_Shutdown");
    bind(_DLSSG_D3D12_Shutdown1, "NVSDK_NGX_D3D12_Shutdown1");
    bind(_DLSSG_D3D12_GetScratchBufferSize, "NVSDK_NGX_D3D12_GetScratchBufferSize");
    bind(_DLSSG_D3D12_CreateFeature, "NVSDK_NGX_D3D12_CreateFeature");
    bind(_DLSSG_D3D12_ReleaseFeature, "NVSDK_NGX_D3D12_ReleaseFeature");
    bind(_DLSSG_D3D12_GetFeatureRequirements, "NVSDK_NGX_D3D12_GetFeatureRequirements");
    bind(_DLSSG_D3D12_EvaluateFeature, "NVSDK_NGX_D3D12_EvaluateFeature");
    bind(_DLSSG_D3D12_PopulateParameters_Impl, "NVSDK_NGX_D3D12_PopulateParameters_Impl");

    if (!complete)
    {
        _DLSSG_D3D12_Init = nullptr;
        return;
    }

    NVNGX_Parameters params(API::DX12, false);
    int available = -1;
    int maxFakeFrames = 0;
    if (_DLSSG_D3D12_PopulateParameters_Impl(&params) == NVSDK_NGX_Result_Success)
    {
        params.Get("FrameGeneration.Available", &available);
        if (params.Get("DLSSG.MultiFrameCountMax", &maxFakeFrames) == NVSDK_NGX_Result_Success && maxFakeFrames > 0)
            _maxFakeFrames = maxFakeFrames;
    }
    if (available == 0)
    {
        LOG_ERROR("DLSSG-XeFG: provider reports FrameGeneration.Available=0; check libxess_fg.dll and the provider log");
        _DLSSG_D3D12_Init = nullptr;
        return;
    }

    const auto init = reinterpret_cast<PFN_OptiPluginInit>(KernelBaseProxy::GetProcAddress_()(dll, OPTI_PLUGIN_INIT_NAME));
    if (init != nullptr)
    {
        // PluginApi keeps callbacks for the process lifetime.
        HMODULE pinned = nullptr;
        if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                               reinterpret_cast<LPCWSTR>(init), &pinned))
            PluginApi::InitPlugin(dll, init, std::string(ProviderName));
        else
            LOG_WARN("DLSSG-XeFG: could not retain its module for the plugin tab (Win32 {})", GetLastError());
    }
    else
    {
        LOG_WARN("DLSSG-XeFG: no OptiPluginInit export; the provider has no plugin tab");
    }

    LOG_INFO("DLSSG-XeFG: loaded {}, maximum fake frames: {}", wstring_to_string(path.wstring()), _maxFakeFrames);
}
