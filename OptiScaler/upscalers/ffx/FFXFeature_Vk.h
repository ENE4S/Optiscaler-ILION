#pragma once
#include "FFXFeature.h"
#include <upscalers/IFeature_Vk.h>

#include "vk/ffx_api_vk.h"
#include <proxies/FfxApi_Proxy.h>
#include <fsr4/FSR4DriverProviderVk.h>

class FFXFeatureVk : public FFXFeature, public IFeature_Vk
{
  private:
    // ILION: the context belongs to the AMD Vulkan driver's provider (FSR4DriverProviderVk) instead of
    // amd_fidelityfx_vk.dll; every call of it goes to the same provider
    bool _driverProvider = false;

  protected:
    bool InitFFX(const NVSDK_NGX_Parameter* InParameters);

    PfnFfxConfigure ConfigureFn() const
    {
        return _driverProvider ? FSR4DriverProviderVk::Configure() : FfxApiProxy::VULKAN_Configure();
    }
    PfnFfxDispatch DispatchFn() const
    {
        return _driverProvider ? FSR4DriverProviderVk::Dispatch() : FfxApiProxy::VULKAN_Dispatch();
    }

    // From IFeature_Vk
    bool InitInternal(VkCommandBuffer InCmdList, NVSDK_NGX_Parameter* InParameters) override;
    bool EvaluateInternal(VkCommandBuffer InCmdBuffer, NVSDK_NGX_Parameter* InParameters) override;

  public:
    FFXFeatureVk(unsigned int InHandleId, NVSDK_NGX_Parameter* InParameters);

    feature_version Version() override { return FFXFeature::Version(); }
    Upscaler GetUpscalerType() const final { return Upscaler::FFX; }
    API Api() const override { return IFeature_Vk::Api(); }
    std::optional<double> ReadUpscalerTime(void* unused) override { return IFeature_Vk::ReadUpscalerTime(unused); };
    void ReadDetailedGpuTimes(void* unused, std::vector<DetailedGpuTime>& detailedGpuTimes) override
    {
        return IFeature_Vk::ReadDetailedGpuTimes(unused, detailedGpuTimes);
    };

    bool IsWithDx12() final { return false; }

    ~FFXFeatureVk()
    {
        if (State::Instance().isShuttingDown)
            return;

        if (_context != nullptr)
        {
            if (_driverProvider)
            {
                FSR4DriverProviderVk::DestroyContext()(&_context, NULL);
                FSR4DriverProviderVk::SetInUse(false);
            }
            else
                FfxApiProxy::VULKAN_DestroyContext()(&_context, NULL);
        }
    }
};
