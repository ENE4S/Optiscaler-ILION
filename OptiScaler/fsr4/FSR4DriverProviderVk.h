#pragma once

// ILION: FSR 4 of AMD's Vulkan driver (amdvkffx64.dll) as a native Vulkan upscaler, next to the FSR 3.1 of
// amd_fidelityfx_vk.dll. Adapted from OptiScaler upstream d8aebe45 (fsr4/FSR4DriverProviderVk, GPL-3.0).
//
//   where        ILION\Escaladores\FSR4\<folder>\amdvkffx64.dll first (the multi-folder layout), then the OptiScaler
//                folder, then the installed driver; the file is checked (UpdateFfxApiProvider) before it's loaded
//   listing      its provider is appended to the Vulkan FFX version list, after the SDK's own ones (their indexes and
//                the default don't move); the universal selector picks it by that index
//   context      the driver only takes the SDK 2.x backend type and wants the upscaler version; its shaders get the
//                half float image rewrite of upstream, its pipelines the pipeline layout fix (FSR4PipelineLayouts)
//   models       hooked by FSR4Models, chosen with the menu's FSR 4 model
// Frame generation of this dll (FP8 ML FG) isn't used: ILION has no Vulkan frame generation path.

#include "FSR4DriverProvider.h"

#include <vulkan/vulkan.h>

class FSR4DriverProviderVk
{
  public:
    // Finds and loads the dll and gets its upscaler provider, once; false when there's none
    static bool Init();
    static bool IsAvailable() { return _upscaler.functions.createContext != nullptr; }

    static HMODULE Module() { return _upscaler.module; }
    static uint64_t VersionId() { return _upscaler.functions.versionId; }
    static const char* VersionName() { return _upscaler.versionName.c_str(); }
    static const std::wstring& Path() { return _path; }

    // The provider's own check for the game's device (struct version 3); true when the driver has none
    static bool IsSupported(VkDevice device, VkPhysicalDevice physicalDevice, PFN_vkGetDeviceProcAddr gdpa);

    // The five FFX functions; createContext gives the driver its shaders through the half float image rewrite
    static PfnFfxCreateContext CreateContext();
    static PfnFfxDestroyContext DestroyContext() { return reinterpret_cast<PfnFfxDestroyContext>(_upscaler.functions.destroyContext); }
    static PfnFfxConfigure Configure() { return reinterpret_cast<PfnFfxConfigure>(_upscaler.functions.configure); }
    static PfnFfxQuery Query() { return reinterpret_cast<PfnFfxQuery>(_upscaler.functions.query); }
    static PfnFfxDispatch Dispatch() { return reinterpret_cast<PfnFfxDispatch>(_upscaler.functions.dispatch); }

    // Set by the Vulkan FFX feature while a context of this provider exists, for the menu's labels
    static void SetInUse(bool inUse) { _inUse = inUse; }
    static bool InUse() { return _inUse; }

  private:
    inline static FSR4DriverProvider::Upscaler _upscaler {};
    inline static std::wstring _path;
    inline static std::atomic<bool> _inUse { false };
};

// Vulkan FFX backend description type the driver providers take (SDK 2.x id), same layout as ffxCreateBackendVKDesc
#define FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_VK_V2 0x01000003u

// From SDK 2.x ffx_upscale.h: the driver complains without it and accepts versions 4.0.0 - 4.1.2
#define FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE_VERSION_V2 0x0001000Bu
#define FFX_UPSCALER_VERSION_V2 ((4u << 22) | (1u << 12) | 2u)

struct ffxCreateContextDescUpscaleVersionV2
{
    ffxCreateContextDescHeader header;
    uint32_t version;
};
