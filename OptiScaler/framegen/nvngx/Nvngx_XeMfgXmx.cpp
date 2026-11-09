#include "pch.h"
#include "Nvngx_XeMfgXmx.h"

#include <proxies/Ntdll_Proxy.h>

#include <fstream>
#include <mutex>
#include <vector>

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
}

NVSDK_NGX_Result Nvngx_XeMfgXmx::D3D12_Init(unsigned long long InApplicationId, const wchar_t* InApplicationDataPath,
                                            ID3D12Device* InDevice, const NVSDK_NGX_FeatureCommonInfo* InFeatureInfo,
                                            NVSDK_NGX_Version InSDKVersion)
{
    ScopedSkipSpoofingGlobal skipSpoofingGlobal {};
    ScopedSkipHeapCapture skipHeapCapture {};
    return Nvngx_DllProxy::D3D12_Init(InApplicationId, InApplicationDataPath, InDevice, InFeatureInfo, InSDKVersion);
}

NVSDK_NGX_Result Nvngx_XeMfgXmx::D3D12_Init_Ext(unsigned long long InApplicationId, const wchar_t* InApplicationDataPath,
                                                ID3D12Device* InDevice, NVSDK_NGX_Version InSDKVersion,
                                                const NVSDK_NGX_FeatureCommonInfo* InFeatureInfo)
{
    ScopedSkipSpoofingGlobal skipSpoofingGlobal {};
    ScopedSkipHeapCapture skipHeapCapture {};
    return Nvngx_DllProxy::D3D12_Init_Ext(InApplicationId, InApplicationDataPath, InDevice, InSDKVersion, InFeatureInfo);
}

NVSDK_NGX_Result Nvngx_XeMfgXmx::D3D12_CreateFeature(ID3D12GraphicsCommandList* InCmdList, NVSDK_NGX_Feature InFeatureID,
                                                     NVSDK_NGX_Parameter* InParameters, NVSDK_NGX_Handle** OutHandle)
{
    ScopedSkipSpoofingGlobal skipSpoofingGlobal {};
    ScopedSkipHeapCapture skipHeapCapture {};
    const auto result = Nvngx_DllProxy::D3D12_CreateFeature(InCmdList, InFeatureID, InParameters, OutHandle);
    if (result != NVSDK_NGX_Result_Success)
        LOG_ERROR("XeMFG XMX: CreateFeature failed ({:X}); its log: dlssg_to_xefg_intel_is_better.log", (UINT) result);
    return result;
}

NVSDK_NGX_Result Nvngx_XeMfgXmx::D3D12_EvaluateFeature(ID3D12GraphicsCommandList* InCmdList,
                                                       const NVSDK_NGX_Handle* InFeatureHandle,
                                                       NVSDK_NGX_Parameter* InParameters,
                                                       PFN_NVSDK_NGX_ProgressCallback InCallback)
{
    ScopedSkipSpoofingGlobal skipSpoofingGlobal {};
    return Nvngx_DllProxy::D3D12_EvaluateFeature(InCmdList, InFeatureHandle, InParameters, InCallback);
}

// ---- Its log: which XeFG path runs ------------------------------------------------------------------------------------
static FILETIME ProcessStartTime()
{
    FILETIME created {}, unused {};
    GetProcessTimes(GetCurrentProcess(), &created, &unused, &unused, &unused);
    return created;
}

const Nvngx_XeMfgXmx::Backend& Nvngx_XeMfgXmx::ReadBackend()
{
    static Backend backend;
    static ULONGLONG checked = 0;
    static uintmax_t lastSize = 0;
    static fs::file_time_type lastWrite {};

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

    const fs::path log = install.dll.parent_path() / L"dlssg_to_xefg_intel_is_better.log";
    std::error_code ec;
    const auto size = fs::file_size(log, ec);
    if (ec)
    {
        backend = {};
        return backend;
    }
    const auto write = fs::last_write_time(log, ec);
    if (!ec && size == lastSize && write == lastWrite)
        return backend;
    lastSize = size;
    lastWrite = write;

    Backend b;
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
        else if (line.find("Ported XMX MFG model installed") != std::string::npos)
            b.xmxModel = true;
        else if (line.find("Creating XeFG context:") != std::string::npos)
        {
            b.lastError.clear(); // errors before this context belong to an earlier one
            if (auto m = after("model: "); !m.empty())
                b.contextModel = atoi(m.c_str());
        }
        else if (auto e = after("[error] "); !e.empty())
            b.lastError = e;
    }

    backend = b;
    return backend;
}
