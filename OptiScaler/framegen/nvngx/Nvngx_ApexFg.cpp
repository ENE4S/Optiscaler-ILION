#include "pch.h"
#include "Nvngx_ApexFg.h"

#include <proxies/Ntdll_Proxy.h>

#include <fstream>
#include <mutex>

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

Nvngx_ApexFg::Run Nvngx_ApexFg::ReadLog()
{
    static std::mutex lock;
    static Run run;
    static ULONGLONG checked = 0;

    std::scoped_lock guard(lock);
    if (checked != 0 && GetTickCount64() - checked < 2000)
        return run;
    checked = GetTickCount64();

    const auto dll = FindDll();
    if (dll.empty())
        return run = {};

    // The newest log of this process (written after it started)
    FILETIME created {}, unused {};
    GetProcessTimes(GetCurrentProcess(), &created, &unused, &unused, &unused);
    fs::path newest;
    FILETIME newestTime {};
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(dll.parent_path(), ec))
    {
        const auto name = entry.path().filename().wstring();
        if (!name.starts_with(L"nr_fg-") || entry.path().extension() != L".log")
            continue;
        WIN32_FILE_ATTRIBUTE_DATA data {};
        if (!GetFileAttributesExW(entry.path().c_str(), GetFileExInfoStandard, &data) ||
            CompareFileTime(&data.ftLastWriteTime, &created) < 0 || CompareFileTime(&data.ftLastWriteTime, &newestTime) <= 0)
            continue;
        newest = entry.path();
        newestTime = data.ftLastWriteTime;
    }
    if (newest.empty())
        return run = {};

    // Its lines: "... model size large: ..." and "... GPU path table: ..., order portable"
    Run found;
    std::ifstream file(newest, std::ios::binary);
    const auto size = fs::file_size(newest, ec);
    if (!ec && size > 65536)
        file.seekg((std::streamoff) (size - 65536));
    std::string line;
    const auto word = [&line](const char* key) -> std::string
    {
        const auto at = line.find(key);
        if (at == std::string::npos)
            return {};
        std::string value = line.substr(at + strlen(key));
        const auto end = value.find_first_of(" :,\r\n");
        return value.substr(0, end);
    };
    while (std::getline(file, line))
    {
        if (auto m = word("] model size "); !m.empty())
            found.model = m;
        else if (auto p = word(", order "); !p.empty())
            found.path = p;
    }
    return run = found;
}

void Nvngx_ApexFg::LoadLibraries()
{
    const auto path = FindDll();
    if (path.empty())
    {
        LOG_WARN("ANR FG selected but OptiScaler\\custom\\Apex Neural Rendering FG\\dlssg\\nvngx_dlssg.dll was not found");
        return;
    }

    // ANR FG runs its model on a Vulkan instance of its own, also in D3D12 games. ReShade's global Vulkan layer
    // (VK_LAYER_reshade) would join that instance while ReShade already runs in the game, and its vkCreateInstance
    // fails (ANR's log: "native D3D12 ... vkCreateInstance"). The variable ReShade's layer manifest names for it keeps
    // the layer out; the game's own ReShade is untouched. ANR FG is DirectX 12 only in ILION: no Vulkan game is affected
    SetEnvironmentVariableW(L"DISABLE_VK_LAYER_reshade_1", L"1");

    // The loader's own LoadLibrary, as for XeMFG XMX: its file name is the one of NVIDIA's DLSSG, not one to redirect
    HMODULE module = NtdllProxy::LoadLibraryExW_Ldr(path.c_str(), NULL, 0);
    if (module == nullptr)
    {
        LOG_ERROR("ANR FG: {} can't be loaded (Win32 {})", wstring_to_string(path.wstring()), GetLastError());
        return;
    }

    ngx.dll = module;
    LoadDx12Exports(); // ILION 1.5: shared with the other replacements (Nvngx_DllProxy)

    if (ngx.D3D12_Init == nullptr || ngx.D3D12_CreateFeature == nullptr || ngx.D3D12_EvaluateFeature == nullptr)
    {
        LOG_ERROR("ANR FG: {} doesn't export the NVSDK_NGX_D3D12 entry points of a DLSSG replacement",
                  wstring_to_string(path.wstring()));
        ngx.D3D12_Init = nullptr;
        return;
    }

    // ILION v1.4.1.2: the optional entry points it lacks (each call checks its own pointer, Nvngx_DllProxy)
    std::string missing;
    for (const auto& [function, name] : { std::pair { (void*) ngx.D3D12_Init_Ext, "Init_Ext" },
                                          { (void*) ngx.D3D12_Shutdown, "Shutdown" },
                                          { (void*) ngx.D3D12_Shutdown1, "Shutdown1" },
                                          { (void*) ngx.D3D12_GetScratchBufferSize, "GetScratchBufferSize" },
                                          { (void*) ngx.D3D12_ReleaseFeature, "ReleaseFeature" },
                                          { (void*) ngx.D3D12_GetFeatureRequirements, "GetFeatureRequirements" },
                                          { (void*) ngx.D3D12_PopulateParameters_Impl, "PopulateParameters_Impl" } })
    {
        if (function == nullptr)
            missing += missing.empty() ? name : std::string(", ") + name;
    }
    if (!missing.empty())
        LOG_WARN("ANR FG: optional NVSDK_NGX_D3D12 entry points not exported: {}", missing);

    LOG_INFO("ANR FG: loaded {}", wstring_to_string(path.wstring()));
}
