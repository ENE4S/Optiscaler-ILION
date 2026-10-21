#pragma once

// ILION: an AMD driver dll with FSR 4 (amdxcffx64 for DX12) used as an upscaler entry of the universal selector.
// Adapted from OptiScaler upstream d8aebe45 (fsr4/FSR4DriverProvider, GPL-3.0): the dll hands out FFX providers through
// UpdateFfxApiProvider; the upscaler provider's five functions are exactly the FFX table ILION switches between.
//
//   loading      from the path the selector chose, once; it stays loaded for the session (switching away doesn't
//                unload it, so switching back needs nothing else)
//   SDK check    the driver wants the FFX SDK dlls loaded and signed; when one isn't, its GetModuleHandleW import is
//                answered with the driver dll itself, signed with the same certificate (as upstream)
//   models       its FSR 4 networks are hooked by FSR4Models and chosen with the menu's FSR 4 model

#include "SysUtils.h"
#include <ffx_api.h>

namespace FSR4DriverProvider
{
// Functions of an FFX provider that the driver dlls hand out through UpdateFfxApiProvider
struct ProviderInterface
{
    uint64_t versionId;
    const char* versionName;
    bool (*canProvide)(uint64_t descType);
    ffxReturnCode_t (*createContext)(ffxContext* context, ffxCreateContextDescHeader* desc, const ffxAllocationCallbacks* memCb);
    ffxReturnCode_t (*destroyContext)(ffxContext* context, const ffxAllocationCallbacks* memCb);
    ffxReturnCode_t (*configure)(ffxContext* context, const ffxConfigureDescHeader* desc);
    ffxReturnCode_t (*query)(ffxContext* context, ffxQueryDescHeader* desc);
    ffxReturnCode_t (*dispatch)(ffxContext* context, const ffxDispatchDescHeader* desc);
};

// Data passed to UpdateFfxApiProvider, the driver fills in the provider for descType
struct ExternalProviderData
{
    uint32_t structVersion;
    uint64_t descType;
    ProviderInterface provider;
    // Version 3, whether the provider can run on a device
    bool (*isSupported)(const void* backend);
    void* reserved;

    // The driver dlls require all 0x60 bytes for version 3 and 0x50 for older versions
    static constexpr uint32_t SizeFor(uint32_t structVersion)
    {
        return structVersion >= 3 ? sizeof(ExternalProviderData) : offsetof(ExternalProviderData, isSupported);
    }
};

static_assert(offsetof(ExternalProviderData, isSupported) == 0x50);
static_assert(sizeof(ExternalProviderData) == 0x60);

struct Upscaler
{
    HMODULE module = nullptr;
    std::string versionName; // e.g. "4.1.2" as the driver names it
    ProviderInterface functions {};
    bool (*isSupported)(const void* backend) = nullptr; // struct version 3 only
};

// Loads the DX12 driver dll at path (or takes it when already loaded) and gets its upscaler provider.
// false with the reason in problem otherwise
bool LoadUpscaler(const std::wstring& path, Upscaler& out, std::string& problem);

// The same for any driver dll: the FFX SDK dlls it looks for (answered with itself when missing), the struct versions
// tried in order, and whether to ask through UpdateFfxApiProviderEx (Vulkan's amdvkffx64 answers struct version 3 there)
bool LoadUpscaler(const std::wstring& path, std::initializer_list<const wchar_t*> sdkModules,
                  std::initializer_list<uint32_t> structVersions, bool useEx, Upscaler& out, std::string& problem);
} // namespace FSR4DriverProvider
