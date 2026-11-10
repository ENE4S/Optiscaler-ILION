#include "pch.h"
#include "Nvngx_XeMfgXmx.h"

#include <proxies/Ntdll_Proxy.h>
#include <proxies/XeFG_Proxy.h>
#include <plugins/PluginApi.h>

#include <fstream>
#include <mutex>
#include <vector>

#include <tlhelp32.h>

namespace fs = std::filesystem;

static constexpr const wchar_t* kDllName = L"dlssg_to_xefg_intel_is_better.dll";

// Where realdody's release puts it: OptiScaler\custom\XeMFG XMX\dlssg (OptiScaler's folder is next to the game's .exe
// or next to this DLL), or the same folder under ILION's main DLL path
static std::vector<fs::path> Candidates()
{
    const fs::path tail = fs::path(L"custom") / L"XeMFG XMX" / L"dlssg" / kDllName;
    std::vector<fs::path> list = { Util::DllPath().parent_path() / L"OptiScaler" / tail,
                                   Util::ExePath().parent_path() / L"OptiScaler" / tail };

    if (const auto main = Config::Instance()->MainDllPath.value_or(L""); !main.empty())
        list.push_back(fs::path(main) / tail);

    return list;
}

Nvngx_XeMfgXmx::Install Nvngx_XeMfgXmx::FindInstall()
{
    Install install;
    std::error_code ec;
    for (const auto& path : Candidates())
    {
        if (!fs::is_regular_file(path, ec))
            continue;

        install.dll = path.lexically_normal();
        install.programs = fs::is_directory(path.parent_path() / L"xefg_mfg_programs", ec);
        install.libxessFg = fs::is_regular_file(path.parent_path() / L"libxess_fg.dll", ec);
        break;
    }
    return install;
}

static std::mutex folderLock;
static fs::path loadedFolder;

fs::path Nvngx_XeMfgXmx::LoadedFolder()
{
    std::scoped_lock lock(folderLock);
    return loadedFolder;
}

void Nvngx_XeMfgXmx::LoadLibraries()
{
    const auto install = FindInstall();
    if (install.dll.empty())
    {
        LOG_WARN("XeMFG XMX selected but {} was not found (OptiScaler\\custom\\XeMFG XMX\\dlssg)",
                 wstring_to_string(kDllName));
        return;
    }

    if (!install.programs)
        LOG_WARN("XeMFG XMX: no xefg_mfg_programs folder next to {}: the XMX model can't be ported",
                 wstring_to_string(install.dll.wstring()));

    // Set before the load: its files under this folder load as they are (LibraryLoad_Hooks)
    {
        std::scoped_lock lock(folderLock);
        loadedFolder = install.dll.parent_path();
    }

    // The loader's own LoadLibrary, as for HelixSR: the DLL is a DLSSG replacement, not a name OptiScaler handles
    HMODULE module = NtdllProxy::LoadLibraryExW_Ldr(install.dll.c_str(), NULL, 0);
    if (module == nullptr)
    {
        LOG_ERROR("XeMFG XMX: {} can't be loaded (Win32 {})", wstring_to_string(install.dll.wstring()), GetLastError());
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
    // DirectX 12 only: no Vulkan entry points are taken (XeFG has no Vulkan API)

    if (_DLSSG_D3D12_Init == nullptr || _DLSSG_D3D12_CreateFeature == nullptr || _DLSSG_D3D12_EvaluateFeature == nullptr)
    {
        LOG_ERROR("XeMFG XMX: {} doesn't export the NVSDK_NGX_D3D12 entry points of a DLSSG replacement",
                  wstring_to_string(install.dll.wstring()));
        _DLSSG_D3D12_Init = nullptr;
        return;
    }

    LOG_INFO("XeMFG XMX: loaded {} (programs: {}, libxess_fg.dll next to it: {})",
             wstring_to_string(install.dll.wstring()), install.programs, install.libxessFg);

    // As OptiScaler v10's CustomProviders::LoadDll, right after the load: its OptiPluginInit reads [Plugin.XeMFG XMX]
    // (NetworkPath, HudlessUi) and registers its tab. Without it realdody stays on its default path and can't be switched
    const auto init = (PFN_OptiPluginInit) GetProcAddress(dll, OPTI_PLUGIN_INIT_NAME);
    if (init == nullptr)
        LOG_WARN("XeMFG XMX: this build has no OptiPluginInit (older than build-10): no network path choice");
    PluginApi::InitPlugin(dll, init, std::string(PluginName));
}

Nvngx_XeMfgXmx::NetworkPath Nvngx_XeMfgXmx::SavedNetworkPath()
{
    const std::string section = std::string("Plugin.") + PluginName;
    const auto value = Config::Instance()->GetRawValue(section.c_str(), "NetworkPath");
    if (value.has_value() && !value->empty() && isdigit((unsigned char) (*value)[0]))
    {
        const int path = atoi(value->c_str());
        if (path >= (int) NetworkPath::XmxPort && path <= (int) NetworkPath::XmxNative)
            return (NetworkPath) path;
    }
    return NetworkPath::XmxNative;
}

void Nvngx_XeMfgXmx::SaveNetworkPath(NetworkPath path)
{
    const std::string section = std::string("Plugin.") + PluginName;
    Config::Instance()->SetRawValue(section.c_str(), "NetworkPath", std::to_string((int) path).c_str());
    Config::Instance()->SaveRawValues();
    LOG_INFO("XeMFG XMX: network path {} saved, applied at the next start", NetworkPathName(path));
}

const char* Nvngx_XeMfgXmx::NetworkPathName(NetworkPath path)
{
    switch (path)
    {
    case NetworkPath::XmxPort:
        return "XMX Port";
    case NetworkPath::Cuda:
        return "CUDA";
    case NetworkPath::Fallback:
        return "Fallback (DP4a)";
    default:
        return "XMX (native)";
    }
}

bool Nvngx_XeMfgXmx::PluginApiAccepted()
{
    for (const auto& tab : PluginApi::GetTabs())
    {
        if (tab.plugin == PluginName)
            return true;
    }
    return false;
}

NVSDK_NGX_Result Nvngx_XeMfgXmx::D3D12_CreateFeature(ID3D12GraphicsCommandList* InCmdList, NVSDK_NGX_Feature InFeatureID,
                                                     NVSDK_NGX_Parameter* InParameters, NVSDK_NGX_Handle** OutHandle)
{
    const auto result = Nvngx_DllProxy::D3D12_CreateFeature(InCmdList, InFeatureID, InParameters, OutHandle);
    if (result != NVSDK_NGX_Result_Success)
        LOG_ERROR("XeMFG XMX: CreateFeature failed ({:X}); its log: dlssg_to_xefg_intel_is_better.log", (UINT) result);
    return result;
}

// ---- Its log: which XeFG path runs ------------------------------------------------------------------------------------
static FILETIME ProcessStartTime()
{
    FILETIME created {}, unused {};
    GetProcessTimes(GetCurrentProcess(), &created, &unused, &unused, &unused);
    return created;
}

static std::string VersionText(const fs::path& path)
{
    version_t version {};
    if (!Util::GetFileVersion(path.wstring(), &version))
        return {};
    return std::format("{}.{}.{}", version.major, version.minor, version.patch);
}

// Another libxess_fg.dll in the game's process, of another version than ILION's (OptiScaler\libxess_fg.dll, the one
// realdody uses): realdody documents that a different loaded copy can make XeFG's Init fail ("Failed to create heap").
// Its own copy (under its folder) and copies of the same version don't count
static std::string OtherLibxessFg(const fs::path& ownFolder)
{
    std::string ownVersion;
    if (const HMODULE own = XeFGProxy::Module(); own != nullptr)
    {
        wchar_t path[MAX_PATH] {};
        if (GetModuleFileNameW(own, path, MAX_PATH) > 0)
            ownVersion = VersionText(path);
    }

    std::string found;
    const HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
    if (snap == INVALID_HANDLE_VALUE)
        return found;
    MODULEENTRY32W entry { sizeof(entry) };
    for (BOOL more = Module32FirstW(snap, &entry); more; more = Module32NextW(snap, &entry))
    {
        if (_wcsicmp(entry.szModule, L"libxess_fg.dll") != 0)
            continue;
        const fs::path path(entry.szExePath);
        if (!ownFolder.empty() && Util::IsSubpath(path, ownFolder))
            continue;
        if (entry.hModule != nullptr && entry.hModule == XeFGProxy::Module())
            continue;
        const std::string version = VersionText(path);
        if (!ownVersion.empty() && version == ownVersion)
            continue;
        found = std::format("{} ({})", wstring_to_string(path.wstring()), version.empty() ? "?" : version);
        break;
    }
    CloseHandle(snap);
    return found;
}

Nvngx_XeMfgXmx::Backend Nvngx_XeMfgXmx::ReadBackend()
{
    static std::mutex lock;
    static Backend backend;
    static ULONGLONG checked = 0;
    static uintmax_t lastSize = 0;
    static fs::file_time_type lastWrite {};

    std::scoped_lock guard(lock);
    const ULONGLONG now = GetTickCount64();
    if (checked != 0 && now - checked < 2000)
        return backend;
    checked = now;

    const auto install = FindInstall();
    if (install.dll.empty())
    {
        backend = {};
        return backend;
    }

    backend.otherLibxessFg = OtherLibxessFg(install.dll.parent_path());

    const fs::path log = install.dll.parent_path() / L"dlssg_to_xefg_intel_is_better.log";
    std::error_code ec;
    const auto size = fs::file_size(log, ec);
    if (ec)
    {
        const auto other = backend.otherLibxessFg;
        backend = {};
        backend.otherLibxessFg = other;
        return backend;
    }
    const auto write = fs::last_write_time(log, ec);
    if (!ec && size == lastSize && write == lastWrite)
        return backend;
    lastSize = size;
    lastWrite = write;

    Backend b;
    b.otherLibxessFg = backend.otherLibxessFg;
    {
        WIN32_FILE_ATTRIBUTE_DATA data {};
        const FILETIME start = ProcessStartTime();
        b.logThisSession = GetFileAttributesExW(log.c_str(), GetFileExInfoStandard, &data) &&
                           CompareFileTime(&data.ftLastWriteTime, &start) >= 0;
    }

    // The last 64 KB at most: its log is short (one session, one line per event)
    std::ifstream file(log, std::ios::binary);
    if (size > 65536)
        file.seekg((std::streamoff) (size - 65536));
    std::string line;
    const auto after = [&line](const char* key) -> std::string
    {
        const auto at = line.find(key);
        if (at == std::string::npos)
            return {};
        std::string value = line.substr(at + strlen(key));
        while (!value.empty() && (value.back() == '\r' || value.back() == '\n' || value.back() == ' '))
            value.pop_back();
        return value;
    };
    while (std::getline(file, line))
    {
        if (auto v = after("XeFG version "); !v.empty())
            b.xefgVersion = v;
        else if (auto t = after("] Trying "); !t.empty())
            b.libxessFg = t;
        else if (auto n = after("Network path: "); !n.empty())
            b.networkPath = n;
        else if (line.find("OptiScaler plugin API version") != std::string::npos)
            b.pluginApi = true;
        else if (line.find("Ported XMX MFG model installed") != std::string::npos)
            b.xmxModel = true;
        else if (line.find("Creating XeFG context:") != std::string::npos)
        {
            b.lastError.clear(); // errors before this context belong to an earlier one
            b.contextError.clear();
            if (auto m = after("model: "); !m.empty())
                b.contextModel = atoi(m.c_str());
        }
        else if (auto e = after("[error] "); !e.empty())
        {
            if (b.contextModel >= 0 && b.contextError.empty())
                b.contextError = e;
            b.lastError = e;
        }
    }

    backend = b;
    return backend;
}
