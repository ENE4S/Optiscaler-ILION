#pragma once

#include "SysUtils.h"

#include <ffx_api.h>
#include <string>

struct FfxModule
{
    HMODULE dll = nullptr;
    feature_version version { 0, 0, 0 };
    std::wstring filePath;

    // ILION: first upscaler provider the dll reports through ffxQuery ("4.1.1" for AMD's FSR or a third-party name),
    // whether it was asked with the game's device (AMD's dlls leave FSR 4 out without one), and whether any provider
    // id isn't AMD's (a third-party upscaler such as HelixSR, whose FSR 4 model hooks must not be applied)
    std::string providerName;
    bool providerFromDevice = false;
    bool thirdParty = false;

    // ILION (legacy, 1.4.x): an AMD driver dll used as the SR dll. ILION 1.5 reaches the driver through upstream's
    // FSR4DriverProviderDx12/Vk instead; these stay false/empty until that path is removed
    bool driverProvider = false;
    uint64_t providerVersionId = 0;
    PfnFfxQuery fallbackQuery = nullptr;

    bool skipCreateCalls = false;
    bool skipConfigureCalls = false;
    bool skipQueryCalls = false;
    bool skipDispatchCalls = false;

    bool isLoader = false;
    bool hooked = false;

    // Vulkan modules of FFX SDK 2.x and the driver take FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_VK_V2,
    // the legacy amd_fidelityfx_vk.dll takes FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_VK
    bool vkBackendV2 = false;

    PfnFfxCreateContext CreateContext = nullptr;
    PfnFfxDestroyContext DestroyContext = nullptr;
    PfnFfxConfigure Configure = nullptr;
    PfnFfxQuery Query = nullptr;
    PfnFfxDispatch Dispatch = nullptr;
};
