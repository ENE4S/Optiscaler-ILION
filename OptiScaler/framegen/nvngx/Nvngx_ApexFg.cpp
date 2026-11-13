#include "pch.h"
#include "Nvngx_ApexFg.h"

#include <proxies/Ntdll_Proxy.h>

namespace fs = std::filesystem;

// Where its add-on puts it: OptiScaler\custom\Apex Neural Rendering FG\dlssg (OptiScaler's folder is next to the game's
// .exe or next to this DLL), or the same folder under ILION's main DLL path
fs::path Nvngx_ApexFg::FindDll()
{
    const fs::path tail = fs::path(L"custom") / L"Apex Neural Rendering FG" / L"dlssg" / L"nvngx_dlssg.dll";
    std::vector<fs::path> candidates = { Util::DllPath().parent_path() / L"OptiScaler" / tail,
                                         Util::ExePath().parent_path() / L"OptiScaler" / tail };
    if (const auto main = Config::Instance()->MainDllPath.value_or(L""); !main.empty())
        candidates.push_back(fs::path(main) / tail);

    std::error_code ec;
    for (const auto& path : candidates)
    {
        if (fs::is_regular_file(path, ec))
            return path.lexically_normal();
    }
    return {};
}

void Nvngx_ApexFg::LoadLibraries()
{
    const auto path = FindDll();
    if (path.empty())
    {
        LOG_WARN("ANR FG selected but OptiScaler\\custom\\Apex Neural Rendering FG\\dlssg\\nvngx_dlssg.dll was not found");
        return;
    }

    // The loader's own LoadLibrary, as for XeMFG XMX: its file name is the one of NVIDIA's DLSSG, not one to redirect
    HMODULE module = NtdllProxy::LoadLibraryExW_Ldr(path.c_str(), NULL, 0);
    if (module == nullptr)
    {
        LOG_ERROR("ANR FG: {} can't be loaded (Win32 {})", wstring_to_string(path.wstring()), GetLastError());
        return;
    }

    dll = module;
    _DLSSG_D3D12_Init = (PFN_D3D12_Init) GetProcAddress(dll, "NVSDK_NGX_D3D12_Init");
    _DLSSG_D3D12_Init_Ext = (PFN_D3D12_Init_Ext) GetProcAddress(dll, "NVSDK_NGX_D3D12_Init_Ext");
    _DLSSG_D3D12_Shutdown = (PFN_D3D12_Shutdown) GetProcAddress(dll, "NVSDK_NGX_D3D12_Shutdown");
    _DLSSG_D3D12_Shutdown1 = (PFN_D3D12_Shutdown1) GetProcAddress(dll, "NVSDK_NGX_D3D12_Shutdown1");
    _DLSSG_D3D12_GetScratchBufferSize =
        (PFN_D3D12_GetScratchBufferSize) GetProcAddress(dll, "NVSDK_NGX_D3D12_GetScratchBufferSize");
    _DLSSG_D3D12_CreateFeature = (PFN_D3D12_CreateFeature) GetProcAddress(dll, "NVSDK_NGX_D3D12_CreateFeature");
    _DLSSG_D3D12_ReleaseFeature = (PFN_D3D12_ReleaseFeature) GetProcAddress(dll, "NVSDK_NGX_D3D12_ReleaseFeature");
    _DLSSG_D3D12_GetFeatureRequirements =
        (PFN_D3D12_GetFeatureRequirements) GetProcAddress(dll, "NVSDK_NGX_D3D12_GetFeatureRequirements");
    _DLSSG_D3D12_EvaluateFeature = (PFN_D3D12_EvaluateFeature) GetProcAddress(dll, "NVSDK_NGX_D3D12_EvaluateFeature");
    _DLSSG_D3D12_PopulateParameters_Impl =
        (PFN_D3D12_PopulateParameters_Impl) GetProcAddress(dll, "NVSDK_NGX_D3D12_PopulateParameters_Impl");

    if (_DLSSG_D3D12_Init == nullptr || _DLSSG_D3D12_CreateFeature == nullptr || _DLSSG_D3D12_EvaluateFeature == nullptr)
    {
        LOG_ERROR("ANR FG: {} doesn't export the NVSDK_NGX_D3D12 entry points of a DLSSG replacement",
                  wstring_to_string(path.wstring()));
        _DLSSG_D3D12_Init = nullptr;
        return;
    }

    LOG_INFO("ANR FG: loaded {}", wstring_to_string(path.wstring()));
}
