#include "pch.h"
#include "FSR4DriverProvider.h"
#include "FSR4Models.h"

#include <State.h>
#include <Util.h>
#include <proxies/KernelBase_Proxy.h>
#include <proxies/Ntdll_Proxy.h>
#include <detours/detours.h>
#include <ffx_upscale.h>

namespace
{
typedef HRESULT(STDMETHODCALLTYPE* PFN_DriverUpdateProvider)(void* pData, uint32_t dataSizeInBytes);

typedef HRESULT(STDMETHODCALLTYPE* PFN_DriverUpdateProviderEx)(void* pData, uint32_t dataSizeInBytes, void* options);

// Options of UpdateFfxApiProviderEx, chained through next (upstream ProviderOption); only the upscaler is asked here
struct ProviderOption
{
    uint32_t type = 0;
    uint32_t effect = 0; // 0 upscale
    bool enable = true;
    ProviderOption* next = nullptr;
};
static_assert(sizeof(ProviderOption) == 0x18);

struct PatchedDll
{
    HMODULE module;
    decltype(&GetModuleHandleW) o_GetModuleHandleW;
    std::vector<std::wstring> sdkModules; // the FFX SDK dlls this driver dll looks for
};

std::vector<PatchedDll> patchedDlls;
std::mutex patchMutex;

// Only the driver dlls call it, through their patched imports: an SDK dll that isn't loaded is answered with the
// driver dll itself
HMODULE WINAPI hkGetModuleHandleW(LPCWSTR lpModuleName)
{
    auto caller = Util::GetCallerModule(_ReturnAddress());
    decltype(&GetModuleHandleW) original = nullptr;

    {
        std::scoped_lock guard(patchMutex);
        auto patched = std::ranges::find(patchedDlls, caller, &PatchedDll::module);
        if (patched != patchedDlls.end())
            original = patched->o_GetModuleHandleW;
    }

    std::vector<std::wstring> sdkModules;

    {
        std::scoped_lock guard(patchMutex);
        auto patched = std::ranges::find(patchedDlls, caller, &PatchedDll::module);
        if (patched != patchedDlls.end())
            sdkModules = patched->sdkModules;
    }

    HMODULE module = original != nullptr ? original(lpModuleName) : KernelBaseProxy::GetModuleHandleW_()(lpModuleName);

    if (module == nullptr && original != nullptr && lpModuleName != nullptr)
    {
        for (const auto& sdkModule : sdkModules)
        {
            if (_wcsicmp(lpModuleName, sdkModule.c_str()) == 0)
            {
                LOG_DEBUG(L"Giving the driver dll its own HMODULE for {}", lpModuleName);
                return caller;
            }
        }
    }

    return module;
}

BOOL CALLBACK PatchGetModuleHandleW(PVOID pContext, DWORD, LPCSTR pszFunc, PVOID* ppvFunc)
{
    if (pszFunc == nullptr || ppvFunc == nullptr || strcmp(pszFunc, "GetModuleHandleW") != 0)
        return TRUE;

    DWORD oldProtect = 0;
    if (!VirtualProtect(ppvFunc, sizeof(PVOID), PAGE_READWRITE, &oldProtect))
        return FALSE;

    static_cast<PatchedDll*>(pContext)->o_GetModuleHandleW = reinterpret_cast<decltype(&GetModuleHandleW)>(*ppvFunc);
    *ppvFunc = reinterpret_cast<PVOID>(&hkGetModuleHandleW);
    VirtualProtect(ppvFunc, sizeof(PVOID), oldProtect, &oldProtect);
    return FALSE;
}
} // namespace

bool FSR4DriverProvider::LoadUpscaler(const std::wstring& path, Upscaler& out, std::string& problem)
{
    // Struct version 1 asks for a signed amd_fidelityfx_dx12.dll, newer ones for the upscaler dll and a signed or
    // missing loader; FFX SDK 2.0+ sends version 2, the first drivers with FSR 4 only fill in version 1
    return LoadUpscaler(path, { L"amd_fidelityfx_dx12.dll", L"amd_fidelityfx_upscaler_dx12.dll" }, { 2u, 1u }, false, out,
                        problem);
}

bool FSR4DriverProvider::LoadUpscaler(const std::wstring& path, std::initializer_list<const wchar_t*> sdkModules,
                                      std::initializer_list<uint32_t> structVersions, bool useEx, Upscaler& out,
                                      std::string& problem)
{
    HMODULE module = NtdllProxy::LoadLibraryExW_Ldr(path.c_str(), NULL, 0);
    if (module == nullptr)
    {
        problem = "dll can't be loaded";
        return false;
    }

    auto update = reinterpret_cast<PFN_DriverUpdateProvider>(KernelBaseProxy::GetProcAddress_()(module, "UpdateFfxApiProvider"));
    auto updateEx =
        reinterpret_cast<PFN_DriverUpdateProviderEx>(KernelBaseProxy::GetProcAddress_()(module, "UpdateFfxApiProviderEx"));
    if (update == nullptr)
    {
        problem = "not an AMD driver FSR provider";
        return false;
    }

    // Once per dll: the SDK stand-in and its model checks
    {
        std::scoped_lock guard(patchMutex);

        if (std::ranges::find(patchedDlls, module, &PatchedDll::module) == patchedDlls.end())
        {
            PatchedDll patched { module, nullptr, {} };
            for (auto sdkModule : sdkModules)
                patched.sdkModules.emplace_back(sdkModule);
            DetourEnumerateImportsEx(module, &patched, nullptr, PatchGetModuleHandleW);

            if (patched.o_GetModuleHandleW == nullptr)
                LOG_WARN("Couldn't patch GetModuleHandleW of {}", wstring_to_string(path));

            patchedDlls.push_back(patched);
        }
    }

    FSR4Models::Hook(module);

    for (uint32_t structVersion : structVersions)
    {
        ExternalProviderData data {};
        data.structVersion = structVersion;
        data.descType = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;

        auto owner = State::GetOwner();
        State::DisableChecks(owner);
        ProviderOption upscale {};
        const HRESULT result = useEx && updateEx != nullptr ? updateEx(&data, ExternalProviderData::SizeFor(structVersion), &upscale)
                                                            : update(&data, ExternalProviderData::SizeFor(structVersion));
        State::EnableChecks(owner);

        if (SUCCEEDED(result) && data.provider.createContext != nullptr && data.provider.destroyContext != nullptr &&
            data.provider.configure != nullptr && data.provider.query != nullptr && data.provider.dispatch != nullptr)
        {
            out.module = module;
            out.versionName = data.provider.versionName != nullptr ? data.provider.versionName : "";
            out.functions = data.provider;
            out.functions.versionName = nullptr; // the driver reuses that buffer
            out.isSupported = structVersion >= 3 ? data.isSupported : nullptr;
            LOG_INFO("Driver upscaler provider {} (id {:X}, struct version {}) from {}", out.versionName,
                     out.functions.versionId, structVersion, wstring_to_string(path));
            return true;
        }

        LOG_DEBUG("UpdateFfxApiProvider struct version {}: 0x{:X}", structVersion, (UINT) result);
    }

    problem = "the driver gave no upscaler provider (GPU or driver without FSR 4)";
    return false;
}
