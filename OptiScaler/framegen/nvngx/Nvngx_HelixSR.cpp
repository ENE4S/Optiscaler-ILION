#include "pch.h"
#include "Nvngx_HelixSR.h"

#include <proxies/Ntdll_Proxy.h>

#include <atomic>
#include <fstream>
#include <mutex>
#include <vector>

namespace fs = std::filesystem;

static std::atomic<uint32_t> loadedVersion { 0 };

// Where HelixSR 2.0.0 puts its DLL (next to the game's .exe or this DLL), then an older install's helixsr.dll
static std::vector<fs::path> Candidates()
{
    const auto exeDir = Util::ExePath().parent_path();
    const auto dllDir = Util::DllPath().parent_path();
    std::vector<fs::path> list = { exeDir / L"HelixSR-fsrfg" / L"amd_fidelityfx_dx12.dll",
                                   dllDir / L"HelixSR-fsrfg" / L"amd_fidelityfx_dx12.dll" };

    if (const auto main = Config::Instance()->MainDllPath.value_or(L""); !main.empty())
        list.push_back(fs::path(main) / L"HelixSR-fsrfg" / L"amd_fidelityfx_dx12.dll");

    list.push_back(dllDir / L"HelixSR" / L"helixsr.dll");
    list.push_back(exeDir / L"HelixSR" / L"helixsr.dll");
    return list;
}

// helixsr.ini [DlssFrameGeneration] Preset, "" when not written
static std::string ReadPreset(const fs::path& ini)
{
    std::ifstream file(ini);
    std::string line, section;
    while (std::getline(file, line))
    {
        const auto trim = [](std::string s)
        {
            const auto a = s.find_first_not_of(" \t\r");
            const auto b = s.find_last_not_of(" \t\r");
            return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
        };
        line = trim(line);
        if (line.empty() || line[0] == ';' || line[0] == '#')
            continue;
        if (line.front() == '[' && line.back() == ']')
        {
            section = trim(line.substr(1, line.size() - 2));
            continue;
        }
        const auto eq = line.find('=');
        if (eq != std::string::npos && _stricmp(section.c_str(), "DlssFrameGeneration") == 0 &&
            _stricmp(trim(line.substr(0, eq)).c_str(), "Preset") == 0)
            return trim(line.substr(eq + 1));
    }
    return {};
}

Nvngx_HelixSR::Install Nvngx_HelixSR::FindInstall()
{
    Install install;
    std::error_code ec;
    for (const auto& path : Candidates())
    {
        if (!fs::is_regular_file(path, ec))
            continue;
        install.dll = path;
        install.fgKernels = fs::is_directory(path.parent_path() / L"helixsr-fg", ec);
        install.preset = ReadPreset(path.parent_path() / L"helixsr.ini");
        break;
    }
    return install;
}

bool Nvngx_HelixSR::UseLite(const Install& install)
{
    if (install.dll.empty())
        return false;
    const auto ini = install.dll.parent_path() / L"helixsr.ini";
    return WritePrivateProfileStringW(L"DlssFrameGeneration", L"Preset", L"c", ini.c_str()) != FALSE;
}

uint32_t Nvngx_HelixSR::LoadedVersion() { return loadedVersion.load(); }

static std::mutex folderLock;
static fs::path loadedFolder;

fs::path Nvngx_HelixSR::LoadedFolder()
{
    std::scoped_lock lock(folderLock);
    return loadedFolder;
}

void Nvngx_HelixSR::LoadLibraries()
{
    // The loader's own LoadLibrary: OptiScaler's hooks would hand a FidelityFX name to its own FFX handling
    std::error_code ec;
    for (const auto& path : Candidates())
    {
        if (!fs::is_regular_file(path, ec))
            continue;

        // Set before the load: HelixSR's DLL can load its helixsr-fg files while it starts
        {
            std::scoped_lock lock(folderLock);
            loadedFolder = path.parent_path().lexically_normal();
        }

        HMODULE module = NtdllProxy::LoadLibraryExW_Ldr(path.c_str(), NULL, 0);
        if (module == nullptr)
        {
            LOG_WARN("NVIDIA FG Lite: {} can't be loaded", wstring_to_string(path.wstring()));
            continue;
        }

        // HelixSR's FidelityFX DLL serves DLSS frame generation only when it says so; an older helixsr.dll has no
        // such export and is taken as it is (HelixSR Scaler does the same)
        using VersionFn = uint32_t (*)();
        const auto version = (VersionFn) GetProcAddress(module, "HelixSR_NgxFgVersion");
        const bool legacy = _wcsicmp(path.filename().c_str(), L"helixsr.dll") == 0;
        if (!legacy && (version == nullptr || version() < 1))
        {
            LOG_WARN("NVIDIA FG Lite: {} doesn't serve DLSS frame generation (no HelixSR_NgxFgVersion)",
                     wstring_to_string(path.wstring()));
            FreeLibrary(module);
            continue;
        }

        dll = module;
        loadedVersion = version != nullptr ? version() : 1;
        LOG_INFO("NVIDIA FG Lite: HelixSR frame generation from {} (NGX FG version {})",
                 wstring_to_string(path.wstring()), loadedVersion.load());
        break;
    }

    if (dll == nullptr)
    {
        LOG_WARN("NVIDIA FG Lite selected but no HelixSR 2.0.0 frame generation DLL was found");
        return;
    }

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
        LOG_WARN("NVIDIA FG Lite: the DLL lacks NVSDK_NGX_D3D12 entry points");
}
