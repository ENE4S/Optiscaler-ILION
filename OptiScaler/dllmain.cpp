#include "pch.h"
#include "dllmain.h"

#include "Util.h"
#include "Config.h"
#include "Logger.h"
#include "resource.h"
#include "DllNames.h"
#include "BuildInfo.h"
#include "misc/GameConfigs.h"
#include "plugins/PluginApi.h"

#include "proxies/Dxgi_Proxy.h"
#include "proxies/Kernel32_Proxy.h"
#include "proxies/KernelBase_Proxy.h"
#include "proxies/Ntdll_Proxy.h"
#include <proxies/IGDExt_Proxy.h>

#include <proxies/XeSS_Proxy.h>
#include <proxies/XeFG_Proxy.h>
#include <proxies/XeLL_Proxy.h>
#include <proxies/Nvngx_Proxy.h>
#include <proxies/FfxApi_Proxy.h>

#include "inputs/FSR2_Dx11.h"
#include "inputs/FSR2_Dx12.h"
#include "inputs/FSR2_Vk.h"
#include "inputs/FSR3_Dx12.h"
#include "inputs/FG/FSR3_Dx12_FG.h"
#include "inputs/FG/XeFG_Inputs_Dx12.h"

#include <hooks/Amdxc64_Hooks.h>
#include <hooks/Dxgi_Hooks.h>
#include <hooks/D3D11_Hooks.h>
#include <hooks/D3D12_Hooks.h>
#include <hooks/Vulkan_Hooks.h>
#include <hooks/Ntdll_Hooks.h>
#include <hooks/Kernel_Hooks.h>
#include <hooks/Gdi32_Hooks.h>
#include <hooks/Wintrust_Hooks.h>
#include <misc/IlionReShade.h>
#include <hooks/Crypt32_Hooks.h>
#include <hooks/Advapi32_Hooks.h>
#include <hooks/Streamline_Hooks.h>
#include <plugins/PluginApi.h>
#include <framegen/nvngx/Nvngx_XeMfgXmx.h>
#include <framegen/nvngx/Nvngx_FakeMichau.h>

#include <nvapi/NvApiHooks.h>

#include "spoofing/User32_Spoofing.h"

#include <cwctype>
#include <d3dkmthk.h>
#pragma comment(lib, "gdi32.lib")
#include <array>
#include <regex>
#include <fstream>
#include <magic_enum.hpp>
#include <version_check.h>
#include <misc/IdentifyGpu.h>
#include <misc/CrashDumps.h>
#include <misc/IlionOpenRr.h>
#include <sha1/sha1.hpp>

static std::vector<HMODULE> _asiHandles;
static std::vector<std::filesystem::directory_entry> _lateLoadingEntries;
static bool _passThruMode = false;
static bool _staticLoad = false;

typedef const char*(CDECL* PFN_wine_get_version)(void);
typedef void (*PFN_InitializeASI)(void);
typedef bool (*PFN_PatchResult)(void);

static bool IsRunningOnWine()
{
    LOG_FUNC();

    HMODULE ntdll = GetModuleHandle(L"ntdll.dll");

    if (!ntdll)
    {
        LOG_WARN("Not running on NT!?!");
        return true;
    }

    auto pWineGetVersion = (PFN_wine_get_version) KernelBaseProxy::GetProcAddress_()(ntdll, "wine_get_version");

    // Workaround for the ntdll-Hide_Wine_Exports patch
    if (!pWineGetVersion && KernelBaseProxy::GetProcAddress_()(ntdll, "wine_server_call") != nullptr)
        pWineGetVersion = (PFN_wine_get_version) ManualGetProcAddress(ntdll, "wine_get_version");

    if (pWineGetVersion)
    {
        LOG_INFO("Running on Wine {0}!", pWineGetVersion());
        return true;
    }

    LOG_WARN("Wine not detected");
    return false;
}

uint32_t CheckAgilitySDK(HMODULE exe)
{
    auto version = reinterpret_cast<const uint32_t*>(GetProcAddress(exe, "D3D12SDKVersion"));
    auto path = reinterpret_cast<const char* const*>(GetProcAddress(exe, "D3D12SDKPath"));

    LOG_INFO(" ");

    if (version && path)
    {
        LOG_INFO("Agility SDK Version: {}", *version);
        LOG_INFO("Agility SDK Path: {}", *path);

        return *version;
    }

    LOG_INFO("Agility SDK exports missing");
    return 0;
}

UINT customD3D12SDKVersion = 615;

static void RunAgilityUpgrade(HMODULE dx12Module)
{
    typedef HRESULT (*PFN_IsDeveloperModeEnabled)(BOOL* isEnabled);
    PFN_IsDeveloperModeEnabled o_IsDeveloperModeEnabled =
        (PFN_IsDeveloperModeEnabled) GetProcAddress(GetModuleHandle(L"kernelbase.dll"), "IsDeveloperModeEnabled");

    if (o_IsDeveloperModeEnabled == nullptr)
    {
        LOG_ERROR("Failed to get IsDeveloperModeEnabled function address");
        return;
    }

    auto hk_IsDeveloperModeEnabled = [](BOOL* isEnabled) -> HRESULT
    {
        *isEnabled = TRUE;
        return S_OK;
    };

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourAttach(&(PVOID&) o_IsDeveloperModeEnabled, static_cast<HRESULT (*)(BOOL*)>(hk_IsDeveloperModeEnabled));
    auto detourResult = DetourTransactionCommit();

    if (detourResult != NO_ERROR)
    {
        LOG_ERROR("Failed to attach detour: {:X}", detourResult);
        return;
    }

    if (Config::Instance()->FsrAgilitySDKUpgrade.value_or_default())
    {
        Microsoft::WRL::ComPtr<ID3D12SDKConfiguration> sdkConfig;
        auto hr = D3D12GetInterface(CLSID_D3D12SDKConfiguration, IID_PPV_ARGS(&sdkConfig));

        if (SUCCEEDED(hr))
        {
            static bool pathSet = false;
            static std::string path; // narrow string

            if (!pathSet)
            {
                std::error_code ec;
                std::wstring widePath = std::filesystem::relative(Config::Instance()->MainDllPath.value(),
                                                                  Util::ExePath().parent_path(), ec);

                // SDK path must be relative to the exe, not possible across drives
                if (!widePath.empty())
                {
                    widePath = widePath + L"\\D3D12_OptiScaler\\";

                    // Properly convert wstring → string
                    int size = WideCharToMultiByte(CP_UTF8, 0, widePath.c_str(), -1, nullptr, 0, nullptr, nullptr);
                    path.resize(size - 1);
                    WideCharToMultiByte(CP_UTF8, 0, widePath.c_str(), -1, path.data(), size, nullptr, nullptr);
                }

                pathSet = true;
            }

            auto corePath =
                std::filesystem::path(Config::Instance()->MainDllPath.value()) / L"D3D12_OptiScaler" / L"D3D12Core.dll";

            if (path.empty())
            {
                LOG_ERROR("Can't upgrade Agility SDK, Opti folder isn't reachable from the exe folder");
            }
            else if (std::error_code ec; !std::filesystem::exists(corePath, ec))
            {
                LOG_ERROR("Can't upgrade Agility SDK, {} is missing", wstring_to_string(corePath.wstring()));
            }
            else
            {
                hr = sdkConfig->SetSDKVersion(customD3D12SDKVersion, reinterpret_cast<LPCSTR>(path.c_str()));
                if (FAILED(hr))
                {
                    LOG_ERROR("Failed to upgrade Agility SDK: {0}", hr);
                }
                else
                {
                    LOG_INFO("Agility SDK upgraded successfully");
                }
            }
        }
        else
        {
            LOG_ERROR("Failed to get D3D12 SDK Configuration interface: {0}", hr);
        }
    }

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourDetach(&(PVOID&) o_IsDeveloperModeEnabled, static_cast<HRESULT (*)(BOOL*)>(hk_IsDeveloperModeEnabled));
    detourResult = DetourTransactionCommit();

    if (detourResult != NO_ERROR)
    {
        LOG_ERROR("Failed to detach detour: {:X}", detourResult);
        return;
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// ILION v1.4.1.3: NVIDIA MFG unlock. NVIDIA's DLSS FG answers "one generated frame" (2X) on RTX 40/30/20; ILION follows
// that maximum (Streamline_Hooks), so the game's DLSS FG and the DLSSG replacements (XeMFG XMX, DLSSG-XeFG, ANR FG) stay
// at 2X there. MFG-Unlocker (dashdogy, MIT) lifts it. ILION already ships it as the XeMFG variant
// plugins\XeMFG\dashdogy-v<version>\MFGUnlocker.asi; here only its unlock is wanted, so it's loaded only where its own
// XeMFG stays idle (its docs: XeMFG acts with FGOutput = XeFG, as the "MFGUnlocker" nvngx replacement, or standalone from
// its menu) and never as a second copy.
namespace
{
enum class NvidiaGen
{
    None,
    Other,
    Turing,
    Ampere,
    Ada,
    Blackwell
};

const wchar_t* NvidiaGenName(NvidiaGen gen)
{
    switch (gen)
    {
    case NvidiaGen::Turing:
        return L"RTX 20";
    case NvidiaGen::Ampere:
        return L"RTX 30";
    case NvidiaGen::Ada:
        return L"RTX 40";
    case NvidiaGen::Blackwell:
        return L"RTX 50";
    case NvidiaGen::Other:
        return L"other NVIDIA";
    default:
        return L"no NVIDIA";
    }
}

NvidiaGen NvidiaGenOfDevice(uint32_t device)
{
    if (device >= 0x1E00 && device <= 0x1F7F)
        return NvidiaGen::Turing; // TU102/TU104/TU106 (the GTX 16 TU116/TU117 are outside)
    if (device >= 0x2200 && device <= 0x25FF)
        return NvidiaGen::Ampere; // GA102-GA107
    if (device >= 0x2680 && device <= 0x28FF)
        return NvidiaGen::Ada; // AD102-AD107
    if (device >= 0x2B00 && device <= 0x2FFF)
        return NvidiaGen::Blackwell; // GB202-GB207
    return NvidiaGen::Other;
}

// The adapters Windows has active right now (D3DKMT, gdi32: no DXGI or NVAPI, which DllMain can't use under the loader
// lock; a removed card's registry keys stay behind, these don't). The unlock has to load before the game's Streamline
// asks for the GPU's generation. With an RTX 50 present the answer is RTX 50 (nothing to unlock)
NvidiaGen NvidiaGeneration(uint32_t& device)
{
    device = 0;
    auto best = NvidiaGen::None;

#ifdef ILION_LAB_FAKE_NVIDIA
    // Lab builds only (never in a published build): ILION_LAB_NVIDIA_DEVICE=2684 pretends that GPU, to test the rules
    if (wchar_t fake[16] {}; GetEnvironmentVariableW(L"ILION_LAB_NVIDIA_DEVICE", fake, 16) > 0)
    {
        device = static_cast<uint32_t>(wcstoul(fake, nullptr, 16));
        return NvidiaGenOfDevice(device);
    }
#endif

    D3DKMT_ENUMADAPTERS2 enumerate {};

    if (D3DKMTEnumAdapters2(&enumerate) != 0 || enumerate.NumAdapters == 0)
        return best;

    std::vector<D3DKMT_ADAPTERINFO> adapters(enumerate.NumAdapters);
    enumerate.pAdapters = adapters.data();

    if (D3DKMTEnumAdapters2(&enumerate) != 0)
        return best;

    for (ULONG i = 0; i < enumerate.NumAdapters; i++)
    {
        D3DKMT_QUERY_DEVICE_IDS ids {};
        D3DKMT_QUERYADAPTERINFO query {};
        query.hAdapter = adapters[i].hAdapter;
        query.Type = KMTQAITYPE_PHYSICALADAPTERDEVICEIDS;
        query.pPrivateDriverData = &ids;
        query.PrivateDriverDataSize = sizeof(ids);

        if (D3DKMTQueryAdapterInfo(&query) == 0)
            LOG_DEBUG("NVIDIA MFG unlock: active adapter {:04X}:{:04X}", ids.DeviceIds.VendorID, ids.DeviceIds.DeviceID);

        if (ids.DeviceIds.VendorID == 0x10DE)
        {
            const auto gen = NvidiaGenOfDevice(ids.DeviceIds.DeviceID);

            if (static_cast<int>(gen) > static_cast<int>(best))
            {
                best = gen;
                device = ids.DeviceIds.DeviceID;
            }
        }

        D3DKMT_CLOSEADAPTER close { adapters[i].hAdapter };
        D3DKMTCloseAdapter(&close);
    }

    return best;
}

// The game's exe imports this dll (static or delay-loaded import)
bool ExeImports(const char* dllName)
{
    const auto base = reinterpret_cast<const BYTE*>(GetModuleHandleW(nullptr));

    if (base == nullptr)
        return false;

    const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);

    if (dos->e_magic != IMAGE_DOS_SIGNATURE)
        return false;

    const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);

    if (nt->Signature != IMAGE_NT_SIGNATURE)
        return false;

    const auto& imports = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];

    if (imports.VirtualAddress != 0)
    {
        for (auto entry = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(base + imports.VirtualAddress); entry->Name != 0;
             entry++)
        {
            if (_stricmp(reinterpret_cast<const char*>(base + entry->Name), dllName) == 0)
                return true;
        }
    }

    const auto& delayed = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT];

    if (delayed.VirtualAddress != 0)
    {
        for (auto entry = reinterpret_cast<const IMAGE_DELAYLOAD_DESCRIPTOR*>(base + delayed.VirtualAddress);
             entry->DllNameRVA != 0; entry++)
        {
            if (_stricmp(reinterpret_cast<const char*>(base + entry->DllNameRVA), dllName) == 0)
                return true;
        }
    }

    return false;
}

// A file whose version resource says ProductName "MFG Unlocker" (a copy under any name: a proxy dll beside the exe or an
// .asi). Read as bytes: loading it, even as data, could go through ILION's own LoadLibrary hooks
bool IsMfgUnlockerFile(const std::filesystem::path& file)
{
    std::error_code ec;

    if (!std::filesystem::is_regular_file(file, ec))
        return false;

    const auto length = std::filesystem::file_size(file, ec);

    if (ec || length > 64ull * 1024 * 1024)
        return false;

    std::ifstream stream(file, std::ios::binary);

    if (!stream)
        return false;

    const std::string bytes((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    const auto wide = [](std::string_view ascii)
    {
        std::string out;

        for (char c : ascii)
        {
            out.push_back(c);
            out.push_back('\0');
        }

        return out;
    };
    const auto key = wide("ProductName");
    const auto value = wide("MFG Unlocker");

    for (auto at = bytes.find(key); at != std::string::npos; at = bytes.find(key, at + 1))
    {
        // The value follows the key's terminating null and padding to a 4 byte boundary (UTF-16, so even offsets)
        for (size_t skip = 2; skip <= 8; skip += 2)
        {
            if (bytes.compare(at + key.size() + skip, value.size(), value) == 0)
                return true;
        }
    }

    return false;
}

// Newest plugins\XeMFG\dashdogy-v<x.y.z>\MFGUnlocker.asi, from 1.7.1 on (the build tested with ILION on RTX 40)
std::optional<std::filesystem::path> FindMfgUnlocker(const std::filesystem::path& pluginPath)
{
    std::optional<std::filesystem::path> best;
    std::array<int, 3> bestVersion { 1, 7, 1 };
    std::error_code ec;
    const auto folder = pluginPath / L"XeMFG";

    if (!std::filesystem::is_directory(folder, ec))
        return best;

    static const std::wregex pattern(LR"(^dashdogy-v?(\d+)\.(\d+)\.(\d+))", std::regex::icase);

    for (const auto& entry : std::filesystem::directory_iterator(folder, ec))
    {
        std::wsmatch match;
        const auto name = entry.path().filename().wstring();

        if (!entry.is_directory(ec) || !std::regex_search(name, match, pattern))
            continue;

        std::array<int, 3> version {};

        try
        {
            version = { std::stoi(match[1].str()), std::stoi(match[2].str()), std::stoi(match[3].str()) };
        }
        catch (const std::exception&)
        {
            continue;
        }

        const auto file = entry.path() / L"MFGUnlocker.asi";

        if (version >= bestVersion && std::filesystem::is_regular_file(file, ec))
        {
            best = file;
            bestVersion = version;
        }
    }

    return best;
}

// The plugin to load for the unlock, or none (the reason goes to the log). asiToLoad: the .asi files LoadAsiPlugins is
// about to load (a chosen dashdogy variant is MFG-Unlocker itself)
std::optional<std::filesystem::path> NvidiaMfgUnlockPlugin(const std::filesystem::path& pluginPath,
                                                           const std::vector<std::filesystem::path>& asiToLoad)
{
    const auto config = Config::Instance();
    // Unset = auto (RTX 40). Not value_for_config(): that one reports a value equal to the default (false) as unset
    const std::optional<bool> setting =
        config->NvidiaMfgUnlock.has_value() ? std::optional<bool>(config->NvidiaMfgUnlock.value()) : std::nullopt;

    if (setting.has_value() && !*setting)
    {
        LOG_INFO("NVIDIA MFG unlock: off (NvidiaMfgUnlock=false)");
        return std::nullopt;
    }

    uint32_t device = 0;
    const auto gen = NvidiaGeneration(device);

    if (gen == NvidiaGen::None)
        return std::nullopt; // AMD / Intel: nothing to do

    const bool olderRtx = gen == NvidiaGen::Ampere || gen == NvidiaGen::Turing;
    const bool wanted = gen == NvidiaGen::Ada || (setting.value_or(false) && olderRtx);

    if (!wanted)
    {
        LOG_INFO(L"NVIDIA MFG unlock: not needed or not enabled on {} (device {:04X}){}", NvidiaGenName(gen), device,
                 olderRtx ? L"; NvidiaMfgUnlock=true enables it (experimental there)" : L"");
        return std::nullopt;
    }

    // Where its XeMFG would act, or another unlock is in charge
    const auto input = config->FGInput.value_or_default();
    const auto output = config->FGOutput.value_or_default();
    const auto replacement = config->FGNvngxReplacement.value_or_default();
    const bool dlssgRoute = input == FGInput::NvngxFG || output == FGOutput::DLSSG;
    const bool fgReplacement =
        dlssgRoute && (replacement == FGNvngxReplacement::XeMfgXmx || replacement == FGNvngxReplacement::FakeMichauXeFG ||
                       replacement == FGNvngxReplacement::ApexFg);

    if (dlssgRoute && replacement == FGNvngxReplacement::Arturs)
    {
        LOG_INFO("NVIDIA MFG unlock: not loaded, Artur's MFG enabler is the DLSSG replacement");
        return std::nullopt;
    }

    if (!fgReplacement && (input == FGInput::XeFG || !(output == FGOutput::NoFG || output == FGOutput::DLSSG)))
    {
        LOG_INFO("NVIDIA MFG unlock: not loaded with the FG output {} (its XeMFG would run beside ILION's)",
                 magic_enum::enum_name(output));
        return std::nullopt;
    }

    if (ExeImports("vulkan-1.dll") && !ExeImports("d3d12.dll"))
    {
        LOG_INFO("NVIDIA MFG unlock: not loaded in a Vulkan game (DirectX 12 only)");
        return std::nullopt;
    }

    const auto exeDir = Util::ExePath().parent_path();
    std::error_code ec;

    if (std::filesystem::exists(exeDir / L"dlssg_sm86.ini", ec) ||
        std::filesystem::is_directory(exeDir / L"dlssg_sm86", ec))
    {
        LOG_INFO("NVIDIA MFG unlock: not loaded, DLSS for SM86 is installed in this game");
        return std::nullopt;
    }

    // Never a second copy: a chosen dashdogy variant, an .asi of its own in plugins, or a proxy dll beside the exe
    for (const auto& asi : asiToLoad)
    {
        if (IsMfgUnlockerFile(asi))
        {
            LOG_INFO(L"NVIDIA MFG unlock: MFG-Unlocker is already loaded from {}", asi.wstring());
            return std::nullopt;
        }
    }

    HMODULE self = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&NvidiaGenOfDevice), &self);
    wchar_t selfPath[MAX_PATH] {};
    GetModuleFileNameW(self, selfPath, MAX_PATH);

    for (const auto name : { L"version.dll", L"winmm.dll", L"dinput8.dll", L"dbghelp.dll", L"dxgi.dll", L"d3d12.dll",
                             L"d3d11.dll", L"wininet.dll", L"winhttp.dll", L"xinput1_3.dll", L"xinput1_4.dll" })
    {
        const auto file = exeDir / name;

        if (std::filesystem::equivalent(file, std::filesystem::path(selfPath), ec))
            continue;

        if (IsMfgUnlockerFile(file))
        {
            LOG_INFO(L"NVIDIA MFG unlock: MFG-Unlocker is installed beside the game as {}", name);
            return std::nullopt;
        }
    }

    const auto plugin = FindMfgUnlocker(pluginPath);

    if (!plugin.has_value())
    {
        LOG_WARN(L"NVIDIA MFG unlock: {} found, but no MFG-Unlocker 1.7.1 or newer in {}\\XeMFG\\dashdogy-v*: install "
                 L"MFG-Unlocker 1.7.1 with ILION Setup",
                 NvidiaGenName(gen), pluginPath.wstring());
        return std::nullopt;
    }

    LOG_INFO(L"NVIDIA MFG unlock: {} (device {:04X}), {}: loading {}", NvidiaGenName(gen), device,
             fgReplacement ? L"with a DLSSG replacement" : L"DLSS FG", plugin->wstring());
    return plugin;
}
} // namespace
// Remembers the plugin for the menu
static void RegisterLoadedAsi(const std::filesystem::path& path)
{
    std::string name = wstring_to_string(path.filename().wstring()); // .string() can crash
    std::string version = Util::GetFileVersionString(path.wstring());
    auto& state = State::Instance();
    std::lock_guard<std::mutex> lock(state.loadedAsiPluginsMutex);
    // Late-load entries can be handled more than once
    for (const auto& plugin : state.loadedAsiPlugins)
    {
        if (plugin.name == name)
            return;
    }
    state.loadedAsiPlugins.push_back({ name, version });
}

// ILION 1.5.0: OpenRR-ASI (Ray Reconstruction on Radeon, managed in Mejoras > Ray Reconstruction) is loaded when it is
// in the plugins folder even with LoadAsiPlugins off: ILION Setup's recipe installs only its files, never the ini.
// With LoadAsiPlugins on, LoadAsiPlugins() loads it with the rest
static void LoadOpenRrAlone()
{
    const std::filesystem::path asi = std::filesystem::path(Config::Instance()->PluginPath.value_or(L"plugins")) / L"openrr-amd.asi";
    std::error_code ec;
    if (!std::filesystem::is_regular_file(asi, ec))
        return;

    HMODULE hMod = NtdllProxy::LoadLibraryExW_Ldr(asi.c_str(), NULL, 0);
    if (hMod == nullptr)
    {
        LOG_WARN(L"OpenRR: could not load {}", asi.wstring());
        return;
    }

    LOG_INFO(L"OpenRR: loaded {} (LoadAsiPlugins off; only this plugin)", asi.wstring());
    _asiHandles.push_back(hMod);
    {
        std::scoped_lock lock(State::Instance().loadedAsiMutex);
        State::Instance().loadedAsiPaths.push_back(asi.wstring());
    }
    RegisterLoadedAsi(asi);

    if (auto init = (PFN_InitializeASI) KernelBaseProxy::GetProcAddress_()(hMod, "InitializeASI"); init != nullptr)
        init();

    PluginApi::InitPlugin(hMod, (PFN_OptiPluginInit) KernelBaseProxy::GetProcAddress_()(hMod, OPTI_PLUGIN_INIT_NAME), asi);
}

void LoadAsiPlugins()
{
    std::filesystem::path pluginPath(Config::Instance()->PluginPath.value_or(L"plugins"));
    auto folderPath = pluginPath.wstring();

    LOG_DEBUG(L"Checking {} for *.asi", folderPath);

    if (!std::filesystem::exists(pluginPath))
        return;

    // ILION 1.5.1: no XeMFG variant in the ini (auto) and the Clasica folder installed: Clasica, as the 1.4.1.2 ini had it.
    // 1.5.0's ini left it on auto, which loaded no variant: XeFG stayed at 2X and the variant selector was empty
    if (!Config::Instance()->XeMfgVariant.has_value())
    {
        std::error_code ec;
        if (std::filesystem::is_directory(pluginPath / L"XeMFG" / L"Clasica", ec))
        {
            Config::Instance()->XeMfgVariant.set_volatile_value(L"Clasica");
            LOG_INFO("XeMFG variant not set: Clasica");
        }
    }

    // ILION: XeMFG variants live in plugins/XeMFG/<variant>/. Only the chosen one is loaded, after the plugins of the
    // folder itself, and a plugin of the same file name in the folder itself is then left out: never two copies
    const auto lowerName = [](std::wstring name)
    {
        std::transform(name.begin(), name.end(), name.begin(), [](wchar_t c) { return std::towlower(c); });
        return name;
    };

    std::vector<std::filesystem::directory_entry> variantEntries;
    std::vector<std::wstring> variantNames;

    // ILION v1.4.0: XeMFG XMX (realdody) as the DLSSG replacement is another implementation of the same technique (its
    // XeFG port comes from MFG-Unlocker): no XeMFG variant with it, never two in one process. The choice stays in the
    // config
    // ILION v1.4.1.2: FakeMichau's DLSSG-XeFG is the same technique (XeSS FG past 2X/4X through a DLSSG replacement):
    // the same rule. The XeMFG variants serve ILION's XeFG output, which is never active with a DLSSG replacement
    const bool xmxReplacement = Nvngx_XeMfgXmx::InUse();
    const bool fakeMichauReplacement = Nvngx_FakeMichau::InUse();
    if (const auto variant = Config::Instance()->XeMfgVariant.value_or(L""); !variant.empty() && xmxReplacement)
    {
        LOG_INFO(L"XeMFG XMX selected: XeMFG variant {} not loaded", variant);
    }
    else if (const auto variant = Config::Instance()->XeMfgVariant.value_or(L""); !variant.empty() && fakeMichauReplacement)
    {
        LOG_INFO(L"DLSSG-XeFG selected: XeMFG variant {} not loaded", variant);
    }
    else if (const auto variant = Config::Instance()->XeMfgVariant.value_or(L""); !variant.empty())
    {
        const auto variantFolder = pluginPath / L"XeMFG" / variant;
        std::error_code ec;

        if (std::filesystem::is_directory(variantFolder, ec))
        {
            for (const auto& entry : std::filesystem::directory_iterator(variantFolder, ec))
            {
                if (entry.is_regular_file() && lowerName(entry.path().extension().wstring()) == L".asi")
                {
                    variantEntries.push_back(entry);
                    variantNames.push_back(lowerName(entry.path().filename().wstring()));
                }
            }

            LOG_INFO(L"XeMFG variant {}: {} plugin(s)", variant, variantEntries.size());
        }
        else
        {
            LOG_WARN(L"XeMFG variant folder not found: {}", variantFolder.wstring());
        }
    }

    std::vector<std::filesystem::directory_entry> entries;

    for (const auto& entry : std::filesystem::directory_iterator(folderPath))
    {
        if (entry.is_regular_file() &&
            std::find(variantNames.begin(), variantNames.end(), lowerName(entry.path().filename().wstring())) != variantNames.end())
        {
            LOG_INFO(L"Left out for the XeMFG variant: {}", entry.path().wstring());
            continue;
        }

        entries.push_back(entry);
    }

    entries.insert(entries.end(), variantEntries.begin(), variantEntries.end());

    // ILION v1.4.1.3: MFG-Unlocker's NVIDIA MFG unlock on RTX 40 (RTX 30/20 when enabled), loaded with the rest
    {
        std::vector<std::filesystem::path> asiToLoad;

        for (const auto& entry : entries)
        {
            if (entry.is_regular_file() && lowerName(entry.path().extension().wstring()) == L".asi")
                asiToLoad.push_back(entry.path());
        }

        if (const auto unlock = NvidiaMfgUnlockPlugin(pluginPath, asiToLoad); unlock.has_value())
            entries.emplace_back(*unlock);
    }

    for (const auto& entry : entries)
    {
        if (!entry.is_regular_file())
            continue;

        std::wstring ext = entry.path().extension().wstring();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](wchar_t c) { return std::towlower(c); });

        if (ext == L".asi")
        {
            std::wstring fileName = entry.path().filename().wstring();
            std::transform(fileName.begin(), fileName.end(), fileName.begin(),
                           [](wchar_t c) { return std::towlower(c); });

            if (fileName.rfind(L"-loadlate") != std::wstring::npos)
            {
                _lateLoadingEntries.push_back(entry);
                continue;
            }

            HMODULE hMod = NtdllProxy::LoadLibraryExW_Ldr(entry.path().c_str(), NULL, 0);

            if (hMod != nullptr)
            {
                LOG_INFO(L"Loaded: {}", entry.path().wstring());
                _asiHandles.push_back(hMod);
                {
                    std::scoped_lock lock(State::Instance().loadedAsiMutex);
                    State::Instance().loadedAsiPaths.push_back(entry.path().wstring());
                }

                auto init = (PFN_InitializeASI) KernelBaseProxy::GetProcAddress_()(hMod, "InitializeASI");
                auto patchResult = (PFN_PatchResult) KernelBaseProxy::GetProcAddress_()(hMod, "PatchResult");

                RegisterLoadedAsi(entry.path());

                if (init != nullptr)
                    init();

                // ILION v1.4.0: OptiScaler v10's plugin API, right after InitializeASI
                PluginApi::InitPlugin(
                    hMod, (PFN_OptiPluginInit) KernelBaseProxy::GetProcAddress_()(hMod, OPTI_PLUGIN_INIT_NAME),
                    entry.path());

                if (patchResult != nullptr)
                {
                    auto pr = patchResult();

                    if (pr)
                    {
                        LOG_INFO("Game patching is successful");
                        State::Instance().isOptiPatcherSucceed = true;

                        LOG_INFO("Disabling spoofing");

                        if (!Config::Instance()->DxgiSpoofing.has_value())
                            Config::Instance()->DxgiSpoofing.set_volatile_value(false);

                        if (!Config::Instance()->VulkanSpoofing.has_value() ||
                            !Config::Instance()->VulkanSpoofing.value_for_config())
                        {
                            Config::Instance()->VulkanSpoofing.set_volatile_value(false);
                        }

                        if (!Config::Instance()->VulkanExtensionSpoofing.has_value())
                            Config::Instance()->VulkanExtensionSpoofing.set_volatile_value(false);
                    }
                }
            }
            else
            {
                DWORD err = GetLastError();
                LOG_ERROR(L"Failed to load: {}, error {:X}", entry.path().wstring(), err);
            }
        }
    }

    if (_lateLoadingEntries.size() > 0)
    {
        std::thread(
            []()
            {
                try
                {

                    auto delay = Config::Instance()->LateAsiPluginsDelay.value_or_default();
                    LOG_INFO("Waiting {} seconds before loading late-load plugins...", delay);
                    std::this_thread::sleep_for(std::chrono::seconds(delay));

                    for (const auto& entry : _lateLoadingEntries)
                    {
                        HMODULE hMod = NtdllProxy::LoadLibraryExW_Ldr(entry.path().c_str(), NULL, 0);

                        if (hMod != nullptr)
                        {
                            LOG_INFO(L"Loaded late: {}", entry.path().wstring());
                            _asiHandles.push_back(hMod);

                            auto init = (PFN_InitializeASI) KernelBaseProxy::GetProcAddress_()(hMod, "InitializeASI");

                            auto patchResult =
                                (PFN_PatchResult) KernelBaseProxy::GetProcAddress_()(hMod, "PatchResult");

                            RegisterLoadedAsi(entry.path());
                            {
                                std::scoped_lock lock(State::Instance().loadedAsiMutex);
                                State::Instance().loadedAsiPaths.push_back(entry.path().wstring());
                            }

                            if (init != nullptr)
                                init();

                            PluginApi::InitPlugin(
                                hMod,
                                (PFN_OptiPluginInit) KernelBaseProxy::GetProcAddress_()(hMod, OPTI_PLUGIN_INIT_NAME),
                                entry.path());

                            if (patchResult != nullptr)
                            {
                                auto pr = patchResult();
                                if (pr)
                                {
                                    LOG_INFO("Game patching is successful");
                                    State::Instance().isOptiPatcherSucceed = true;
                                    LOG_INFO("Disabling spoofing");

                                    if (!Config::Instance()->DxgiSpoofing.has_value())
                                        Config::Instance()->DxgiSpoofing.set_volatile_value(false);

                                    if (!Config::Instance()->VulkanSpoofing.has_value() ||
                                        !Config::Instance()->VulkanSpoofing.value_for_config())
                                    {
                                        Config::Instance()->VulkanSpoofing.set_volatile_value(false);
                                    }

                                    if (!Config::Instance()->VulkanExtensionSpoofing.has_value())
                                        Config::Instance()->VulkanExtensionSpoofing.set_volatile_value(false);
                                }
                            }
                        }
                        else
                        {
                            DWORD err = GetLastError();
                            LOG_ERROR(L"Failed to load: {}, error {:X}", entry.path().wstring(), err);
                        }
                    }
                }
                catch (...)
                {
                    LOG_ERROR("Exception occurred while loading late-load plugins");
                }
            })
            .detach();
    }
}

// With Luma, both are loaded after its D3D12 device creation, Special K loads ReShade too
static bool ShouldLoadSpecialK()
{
    return skModule == nullptr && Config::Instance()->LoadSpecialK.value_or_default() &&
           !Config::Instance()->CreateD3D12DeviceForLuma.value_or_default();
}

static bool ShouldLoadReShade()
{
    return reshadeModule == nullptr && Config::Instance()->LoadReShade.value_or_default() &&
           !Config::Instance()->CreateD3D12DeviceForLuma.value_or_default();
}

static bool ShouldLoadForLuma()
{
    return Config::Instance()->CreateD3D12DeviceForLuma.value_or_default() &&
           ((skModule == nullptr && Config::Instance()->LoadSpecialK.value_or_default()) ||
            (reshadeModule == nullptr && Config::Instance()->LoadReShade.value_or_default()));
}

static void LoadInjectedDlls()
{
    // No RESHADE_DISABLE_GRAPHICS_HOOK, Special K sets it itself when it wants ReShade without hooks
    if (ShouldLoadSpecialK())
    {
        auto skFile = Util::ExePath().parent_path() / L"SpecialK64.dll";

        State::EnableServeOriginal(200);
        skModule = NtdllProxy::LoadLibraryExW_Ldr(skFile.c_str(), NULL, 0);
        State::DisableServeOriginal(200);

        LOG_INFO("Loading SpecialK64.dll, result: {0:X}", (UINT64) skModule);
    }

    // Special K loads ReShade itself
    if (ShouldLoadReShade() && skModule != nullptr)
    {
        LOG_WARN("Special K is loaded, skipping ReShade");
    }
    else if (ShouldLoadReShade())
    {
        auto rsFile = Util::ReShadePath();
        SetEnvironmentVariableW(L"RESHADE_DISABLE_LOADING_CHECK", L"1");

        State::EnableServeOriginal(201);
        reshadeModule = NtdllProxy::LoadLibraryExW_Ldr(rsFile.c_str(), NULL, 0);
        State::DisableServeOriginal(201);

        LOG_INFO("Loading {}, result: {:X}", rsFile.filename().string(), (size_t) reshadeModule);
    }
}

DWORD WINAPI getGpuInfo(LPVOID hModuleVoid);

typedef int(WINAPI* PFN_ExeEntry)(PVOID);
static PFN_ExeEntry o_ExeEntry = nullptr;
static std::atomic<bool> _entryLoadsStarted = false;
static std::atomic<bool> _entryLoadsDone = false;

// Not call_once, an asi creating a factory while loading would call this again
static void RunEntryLoads()
{
    if (_entryLoadsStarted.exchange(true))
        return;

    // hkExeEntry waits for it, also when a load throws on another thread
    struct DoneGuard
    {
        ~DoneGuard()
        {
            _entryLoadsDone = true;
            _entryLoadsDone.notify_all();
        }
    } doneGuard;

    LOG_INFO("Loading injected dlls");

    // So ReShade hooks the factory methods after us, Special K should hook before us
    if (skModule != nullptr || !Config::Instance()->LoadSpecialK.value_or_default())
        DxgiHooks::HookFactoryMethods();

    // Detection fails under the loader lock, nvapi loaded by an injected dll's DllMain would pick fakenvapi
    IdentifyGpu::getAllGpus();

    DxgiHooks::LoadForLuma();
    LoadInjectedDlls();

    // Started here to not create a factory together with the one above
    CreateThread(nullptr, 0, getGpuInfo, GetDllNameWModule(&dx12NamesW), 0, nullptr);
}

static int WINAPI hkExeEntry(PVOID param)
{
    // Restores the entry for anything checking the exe code
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourDetach(&(PVOID&) o_ExeEntry, hkExeEntry);
    DetourTransactionCommit();

    RunEntryLoads();

    // Another thread's factory creation can run the loads, game code shouldn't start before they finish
    _entryLoadsDone.wait(false);

    return o_ExeEntry(param);
}

// TLS callbacks run before the entry, protectors (Denuvo, VMProtect) use them to check the exe code
static bool ExeHasTlsCallbacks()
{
    auto base = (BYTE*) exeModule;
    auto dosHeader = (IMAGE_DOS_HEADER*) base;
    auto ntHeaders = (IMAGE_NT_HEADERS*) (base + dosHeader->e_lfanew);
    auto tlsData = ntHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_TLS];

    if (tlsData.VirtualAddress == 0)
        return false;

    auto tlsDir = (IMAGE_TLS_DIRECTORY*) (base + tlsData.VirtualAddress);
    auto callbacks = (PIMAGE_TLS_CALLBACK*) tlsDir->AddressOfCallBacks;

    return callbacks != nullptr && *callbacks != nullptr;
}

// Runs after the loader lock is released and before any game code
static bool HookExeEntry()
{
    if (o_ExeEntry != nullptr)
        return true;

    if (ExeHasTlsCallbacks())
    {
        LOG_INFO("Exe has TLS callbacks, not hooking the entry");
        return false;
    }

    o_ExeEntry = (PFN_ExeEntry) DetourGetEntryPoint(nullptr);

    if (o_ExeEntry == nullptr)
        return false;

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourAttach(&(PVOID&) o_ExeEntry, hkExeEntry);

    if (DetourTransactionCommit() != NO_ERROR)
    {
        LOG_ERROR("Can't hook the exe entry, loading now");
        o_ExeEntry = nullptr;
        return false;
    }

    DxgiHooks::PendingLoads = RunEntryLoads;
    return true;
}

static void CheckWorkingMode()
{
    if (!_passThruMode)
        LOG_FUNC();

    bool modeFound = false;
    std::string filename = wstring_to_string(Util::DllPath().filename().wstring()); // .string() can crash
    std::string lCaseFilename(filename);
    std::filesystem::path pluginPath(Config::Instance()->PluginPath.value_or(L"plugins"));
    auto optiDllPath = std::filesystem::path(Config::Instance()->MainDllPath.value());

    for (size_t i = 0; i < lCaseFilename.size(); i++)
        lCaseFilename[i] = std::tolower(lCaseFilename[i]);

    do
    {
        if (!_passThruMode && Config::Instance()->EarlyHooking.value_or_default())
        {
            NtdllHooks::Hook();
            KernelHooks::Hook();
            KernelHooks::HookBase();
        }

        // version.dll
        if (lCaseFilename == "version.dll")
        {
            do
            {
                auto pluginFilePath = pluginPath / L"version.dll";
                originalModule = NtdllProxy::LoadLibraryExW_Ldr(pluginFilePath.wstring().c_str(), NULL, 0);

                if (originalModule != nullptr)
                {
                    if (!_passThruMode)
                        LOG_INFO("OptiScaler working as version.dll, original dll loaded from plugin folder");

                    break;
                }

                originalModule = NtdllProxy::LoadLibraryExW_Ldr(L"version-original.dll", NULL, 0);

                if (originalModule != nullptr)
                {
                    if (!_passThruMode)
                        LOG_INFO("OptiScaler working as version.dll, version-original.dll loaded");

                    break;
                }

                originalModule = NtdllProxy::LoadLibraryExW_Ldr(L"version.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);

                if (originalModule != nullptr && !_passThruMode)
                    LOG_INFO("OptiScaler working as version.dll, system dll loaded");

            } while (false);

            if (originalModule != nullptr)
            {
                dllNames.push_back("version.dll");
                dllNames.push_back("version");
                dllNamesW.push_back(L"version.dll");
                dllNamesW.push_back(L"version");

                shared.LoadOriginalLibrary(originalModule);
                version.LoadOriginalLibrary(originalModule);

                modeFound = true;
            }
            else
            {
                if (!_passThruMode)
                    LOG_ERROR("OptiScaler can't find original version.dll!");
            }

            break;
        }

        // winmm.dll
        if (lCaseFilename == "winmm.dll")
        {
            do
            {
                auto pluginFilePath = pluginPath / L"winmm.dll";
                originalModule = NtdllProxy::LoadLibraryExW_Ldr(pluginFilePath.wstring().c_str(), NULL, 0);

                if (originalModule != nullptr)
                {
                    if (!_passThruMode)
                        LOG_INFO("OptiScaler working as winmm.dll, original dll loaded from plugin folder");

                    break;
                }

                originalModule = NtdllProxy::LoadLibraryExW_Ldr(L"winmm-original.dll", NULL, 0);

                if (originalModule != nullptr)
                {
                    if (!_passThruMode)
                        LOG_INFO("OptiScaler working as winmm.dll, winmm-original.dll loaded");

                    break;
                }

                originalModule = NtdllProxy::LoadLibraryExW_Ldr(L"winmm.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);

                if (originalModule != nullptr && !_passThruMode)
                    LOG_INFO("OptiScaler working as winmm.dll, system dll loaded");

            } while (false);

            if (originalModule != nullptr)
            {
                dllNames.push_back("winmm.dll");
                dllNames.push_back("winmm");
                dllNamesW.push_back(L"winmm.dll");
                dllNamesW.push_back(L"winmm");

                shared.LoadOriginalLibrary(originalModule);
                winmm.LoadOriginalLibrary(originalModule);
                modeFound = true;
            }
            else
            {
                if (!_passThruMode)
                    LOG_ERROR("OptiScaler can't find original winmm.dll!");
            }

            break;
        }

        // wininet.dll
        if (lCaseFilename == "wininet.dll")
        {
            do
            {
                auto pluginFilePath = pluginPath / L"wininet.dll";
                originalModule = NtdllProxy::LoadLibraryExW_Ldr(pluginFilePath.wstring().c_str(), NULL, 0);

                if (originalModule != nullptr)
                {
                    if (!_passThruMode)
                        LOG_INFO("OptiScaler working as wininet.dll, original dll loaded from plugin folder");

                    break;
                }

                originalModule = NtdllProxy::LoadLibraryExW_Ldr(L"wininet-original.dll", NULL, 0);

                if (originalModule != nullptr)
                {
                    if (!_passThruMode)
                        LOG_INFO("OptiScaler working as wininet.dll, wininet-original.dll loaded");

                    break;
                }

                originalModule = NtdllProxy::LoadLibraryExW_Ldr(L"wininet.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);

                if (originalModule != nullptr && !_passThruMode)
                    LOG_INFO("OptiScaler working as wininet.dll, system dll loaded");

            } while (false);

            if (originalModule != nullptr)
            {
                dllNames.push_back("wininet.dll");
                dllNames.push_back("wininet");
                dllNamesW.push_back(L"wininet.dll");
                dllNamesW.push_back(L"wininet");

                shared.LoadOriginalLibrary(originalModule);
                wininet.LoadOriginalLibrary(originalModule);
                modeFound = true;
            }
            else
            {
                if (!_passThruMode)
                    LOG_ERROR("OptiScaler can't find original wininet.dll!");
            }

            break;
        }

        // dbghelp.dll
        if (lCaseFilename == "dbghelp.dll")
        {
            do
            {
                auto pluginFilePath = pluginPath / L"dbghelp.dll";
                originalModule = NtdllProxy::LoadLibraryExW_Ldr(pluginFilePath.wstring().c_str(), NULL, 0);

                if (originalModule != nullptr)
                {
                    if (!_passThruMode)
                        LOG_INFO("OptiScaler working as dbghelp.dll, original dll loaded from plugin folder");

                    break;
                }

                originalModule = NtdllProxy::LoadLibraryExW_Ldr(L"dbghelp-original.dll", NULL, 0);

                if (originalModule != nullptr)
                {
                    if (!_passThruMode)
                        LOG_INFO("OptiScaler working as dbghelp.dll, dbghelp-original.dll loaded");

                    break;
                }

                originalModule = NtdllProxy::LoadLibraryExW_Ldr(L"dbghelp.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);

                if (originalModule != nullptr && !_passThruMode)
                    LOG_INFO("OptiScaler working as dbghelp.dll, system dll loaded");

            } while (false);

            if (originalModule != nullptr)
            {
                dllNames.push_back("dbghelp.dll");
                dllNames.push_back("dbghelp");
                dllNamesW.push_back(L"dbghelp.dll");
                dllNamesW.push_back(L"dbghelp");

                shared.LoadOriginalLibrary(originalModule);
                dbghelp.LoadOriginalLibrary(originalModule);
                modeFound = true;
            }
            else
            {
                if (!_passThruMode)
                    LOG_ERROR("OptiScaler can't find original dbghelp.dll!");
            }

            break;
        }

        // optiscaler.dll
        if (lCaseFilename == "optiscaler.dll")
        {
            if (!_passThruMode)
                LOG_INFO("OptiScaler working as OptiScaler.dll");

            // quick hack for testing
            originalModule = dllModule;

            dllNames.push_back("optiscaler.dll");
            dllNames.push_back("optiscaler");
            dllNamesW.push_back(L"optiscaler.dll");
            dllNamesW.push_back(L"optiscaler");

            modeFound = true;
            break;
        }

        // optiscaler.asi
        if (lCaseFilename == "optiscaler.asi")
        {
            if (!_passThruMode)
                LOG_INFO("OptiScaler working as OptiScaler.asi");

            // quick hack for testing
            originalModule = dllModule;

            dllNames.push_back("optiscaler.asi");
            dllNames.push_back("optiscaler");
            dllNamesW.push_back(L"optiscaler.asi");
            dllNamesW.push_back(L"optiscaler");

            modeFound = true;
            break;
        }

        // winhttp.dll
        if (lCaseFilename == "winhttp.dll")
        {
            do
            {
                auto pluginFilePath = pluginPath / L"winhttp.dll";
                originalModule = NtdllProxy::LoadLibraryExW_Ldr(pluginFilePath.wstring().c_str(), NULL, 0);

                if (originalModule != nullptr)
                {
                    if (!_passThruMode)
                        LOG_INFO("OptiScaler working as winhttp.dll, original dll loaded from plugin folder");

                    break;
                }

                originalModule = NtdllProxy::LoadLibraryExW_Ldr(L"winhttp-original.dll", NULL, 0);

                if (originalModule != nullptr)
                {
                    if (!_passThruMode)
                        LOG_INFO("OptiScaler working as winhttp.dll, winhttp-original.dll loaded");

                    break;
                }

                originalModule = NtdllProxy::LoadLibraryExW_Ldr(L"winhttp.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);

                if (originalModule != nullptr && !_passThruMode)
                    LOG_INFO("OptiScaler working as winhttp.dll, system dll loaded");

            } while (false);

            if (originalModule != nullptr)
            {
                dllNames.push_back("winhttp.dll");
                dllNames.push_back("winhttp");
                dllNamesW.push_back(L"winhttp.dll");
                dllNamesW.push_back(L"winhttp");

                shared.LoadOriginalLibrary(originalModule);
                winhttp.LoadOriginalLibrary(originalModule);
                modeFound = true;
            }
            else
            {
                if (!_passThruMode)
                    LOG_ERROR("OptiScaler can't find original winhttp.dll!");
            }

            break;
        }

        // dxgi.dll
        if (lCaseFilename == "dxgi.dll")
        {
            do
            {
                auto pluginFilePath = pluginPath / L"dxgi.dll";
                originalModule = NtdllProxy::LoadLibraryExW_Ldr(pluginFilePath.wstring().c_str(), NULL, 0);

                if (originalModule != nullptr)
                {
                    if (!_passThruMode)
                        LOG_INFO("OptiScaler working as dxgi.dll, original dll loaded from plugin folder");

                    break;
                }

                originalModule = NtdllProxy::LoadLibraryExW_Ldr(L"dxgi-original.dll", NULL, 0);

                if (originalModule != nullptr)
                {
                    if (!_passThruMode)
                        LOG_INFO("OptiScaler working as dxgi.dll, dxgi-original.dll loaded");

                    break;
                }

                originalModule = NtdllProxy::LoadLibraryExW_Ldr(L"dxgi.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);

                if (originalModule != nullptr && !_passThruMode)
                    LOG_INFO("OptiScaler working as dxgi.dll, system dll loaded");

            } while (false);

            if (originalModule != nullptr)
            {
                dllNames.push_back("dxgi.dll");
                dllNames.push_back("dxgi");
                dllNamesW.push_back(L"dxgi.dll");
                dllNamesW.push_back(L"dxgi");

                DxgiProxy::Init(originalModule);
                dxgi.LoadOriginalLibrary(originalModule);

                State::Instance().workingMode = WorkingMode::Dxgi;
                modeFound = true;
            }
            else
            {
                if (!_passThruMode)
                    LOG_ERROR("OptiScaler can't find original dxgi.dll!");
            }

            break;
        }

        // d3d12.dll
        if (lCaseFilename == "d3d12.dll")
        {
            do
            {
                // Moved here to cover agility sdk
                if (!_passThruMode)
                {
                    NtdllHooks::Hook();
                    KernelHooks::HookBase();
                }

                auto pluginFilePath = pluginPath / L"d3d12.dll";
                originalModule = NtdllProxy::LoadLibraryExW_Ldr(pluginFilePath.wstring().c_str(), NULL, 0);
                if (originalModule != nullptr)
                {
                    if (!_passThruMode)
                        LOG_INFO("OptiScaler working as d3d12.dll, original dll loaded from plugin folder");

                    break;
                }

                originalModule = NtdllProxy::LoadLibraryExW_Ldr(L"d3d12-original.dll", NULL, 0);
                if (originalModule != nullptr)
                {
                    if (!_passThruMode)
                        LOG_INFO("OptiScaler working as d3d12.dll, d3d12-original.dll loaded");

                    break;
                }

                originalModule = NtdllProxy::LoadLibraryExW_Ldr(L"d3d12.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);

                if (originalModule != nullptr && !_passThruMode)
                    LOG_INFO("OptiScaler working as d3d12.dll, system dll loaded");

            } while (false);

            if (originalModule != nullptr)
            {
                dllNames.push_back("d3d12.dll");
                dllNames.push_back("d3d12");
                dllNamesW.push_back(L"d3d12.dll");
                dllNamesW.push_back(L"d3d12");

                D3d12Proxy::Init(originalModule);
                d3d12.LoadOriginalLibrary(originalModule);

                State::Instance().workingMode = WorkingMode::D3d12;

                modeFound = true;
            }
            else
            {
                if (!_passThruMode)
                    LOG_ERROR("OptiScaler can't find original d3d12.dll!");
            }

            break;
        }

    } while (false);

    // Work as a dummy dll
    if (_passThruMode)
        return;

    if (!modeFound)
    {
        LOG_ERROR("Unsupported dll name: {0}", filename);
        return;
    }

    Config::Instance()->CheckUpscalerFiles();

    // Intel Extension Framework
    if (Config::Instance()->UESpoofIntelAtomics64.value_or_default())
    {
        HMODULE igdext = NtdllProxy::LoadLibraryExW_Ldr(L"igdext64.dll", NULL, 0);

        if (igdext == nullptr)
        {
            auto paths = Util::GetDriverStore();

            for (const auto& [luid, path] : paths)
            {
                auto dllPath = path / L"igdext64.dll";
                LOG_DEBUG("Trying to load: {}", wstring_to_string(dllPath.c_str()));
                igdext = NtdllProxy::LoadLibraryExW_Ldr(dllPath.c_str(), NULL, 0);

                if (igdext != nullptr)
                {
                    LOG_INFO(L"igdext64.dll loaded from {}", dllPath.wstring());
                    break;
                }
            }
        }
        else
        {
            LOG_INFO("igdext64.dll loaded from game folder");
        }

        if (igdext != nullptr)
            IGDExtProxy::Init(igdext);
        else
            LOG_ERROR("Failed to load igdext64.dll");
    }

    // DXGI
    if (DxgiProxy::Module() == nullptr)
    {
        LOG_DEBUG("Check for dxgi");
        HMODULE dxgiModule = nullptr;
        dxgiModule = GetDllNameWModule(&dxgiNamesW);
        if (dxgiModule != nullptr)
        {
            LOG_DEBUG("dxgi.dll already in memory");

            DxgiProxy::Init(dxgiModule);
            DxgiHooks::Hook();
        }
    }
    else
    {
        LOG_DEBUG("dxgi.dll already in memory");
        DxgiHooks::Hook();
    }

    // DirectX 12
    if (D3d12Proxy::Module() == nullptr)
    {
        // Moved here to cover agility sdk
        KernelHooks::HookBase();
        NtdllHooks::Hook();

        LOG_DEBUG("Check for d3d12");
        HMODULE d3d12Module = nullptr;
        d3d12Module = GetDllNameWModule(&dx12NamesW);
        if (Config::Instance()->OverlayMenu.value_or_default() && d3d12Module != nullptr)
        {
            LOG_DEBUG("d3d12.dll already in memory");
            D3d12Proxy::Init(d3d12Module);
            D3D12Hooks::Hook();
        }
    }
    else
    {
        LOG_DEBUG("d3d12.dll already in memory");
        D3D12Hooks::Hook();
    }

    if (D3d12Proxy::Module() == nullptr && State::Instance().gameQuirks & GameQuirk::LoadD3D12Manually)
    {
        LOG_DEBUG("Loading d3d12.dll manually");
        D3d12Proxy::Init();
        D3D12Hooks::Hook();
    }

    d3d12AgilityModule = GetDllNameWModule(&dx12agilityNamesW);
    if (d3d12AgilityModule != nullptr)
    {
        LOG_DEBUG("D3D12Core.dll already in memory");
        D3D12Hooks::HookAgility(d3d12AgilityModule);
    }

    if (d3d12AgilityModule == nullptr && State::Instance().gameQuirks & GameQuirk::LoadD3D12Manually)
    {
        auto path = Util::ExePath().parent_path() / L"D3D12" / L"D3D12Core.dll";
        d3d12AgilityModule = NtdllProxy::LoadLibraryExW_Ldr(path.c_str(), NULL, 0);

        if (d3d12AgilityModule == nullptr && Config::Instance()->FsrAgilitySDKUpgrade.value_or_default())
        {
            path = optiDllPath / L"D3D12_OptiScaler" / L"D3D12Core.dll";
            d3d12AgilityModule = NtdllProxy::LoadLibraryExW_Ldr(path.c_str(), NULL, 0);
        }

        if (d3d12AgilityModule == nullptr)
        {
            d3d12AgilityModule = NtdllProxy::LoadLibraryExW_Ldr(L"D3D12Core.dll", NULL, 0);
        }

        if (d3d12AgilityModule != nullptr)
        {
            LOG_DEBUG("D3D12Core.dll loaded");
            D3D12Hooks::HookAgility(d3d12AgilityModule);
        }
    }

    // DirectX 11
    d3d11Module = GetDllNameWModule(&dx11NamesW);
    if (Config::Instance()->OverlayMenu.value_or_default() && d3d11Module != nullptr)
    {
        LOG_DEBUG("d3d11.dll already in memory");
        D3D11Hooks::Hook(d3d11Module);
    }

    // Vulkan
    if (State::Instance().isRunningOnLinux || State::Instance().gameQuirks & GameQuirk::LoadVulkanManually)
    {
        vulkanModule = NtdllProxy::LoadLibraryExW_Ldr(L"vulkan-1.dll", NULL, 0);
        LOG_DEBUG("Loading vulkan-1.dll for Linux, result: {:X}", (size_t) vulkanModule);
    }
    else
    {
        vulkanModule = GetDllNameWModule(&vkNamesW);
    }

    if (vulkanModule != nullptr)
    {
        LOG_DEBUG("Hooking vulkan-1.dll");
        VulkanHooks::Hook(vulkanModule);
    }

    // NVAPI
    // Doesn't seem to like GetModuleHandle for some reason, so call our load to make sure
    if (!State::Instance().dlssExternalBackend && GetDllNameWModule(&nvapiNamesW) != nullptr)
    {
        // This hooks nvapi as well when possible
        auto nvapi64 = LibraryLoadHooks::LoadNvApi();
    }

    // GDI32
    hookGdi32();

    // Wintrust
    hookWintrust();

    // Crypt32
    hookCrypt32();

    // Advapi32
    if (Config::Instance()->DxgiSpoofing.value_or_default() ||
        Config::Instance()->StreamlineSpoofing.value_or_default())
    {
        hookAdvapi32();
    }

    // User32
    if (Config::Instance()->SpoofUser32.value_or_default())
    {
        User32Spoofing::Hook();
    }

    // hook streamline right away if it's already loaded
    HMODULE slModule = nullptr;
    slModule = GetDllNameWModule(&slInterposerNamesW);
    if (slModule != nullptr)
    {
        LOG_DEBUG("sl.interposer.dll already in memory");
        StreamlineHooks::hookInterposer(slModule);
        slInterposerModule = slModule;
    }

    HMODULE slDlss = nullptr;
    slDlss = GetDllNameWModule(&slDlssNamesW);
    if (slDlss != nullptr)
    {
        State::Instance().postCodes |= PostCode::SlPluginsAlreadyInMemory;
        LOG_WARN("sl.dlss.dll already in memory");
        StreamlineHooks::hookDlss(slDlss);
    }

    HMODULE slDlssd = GetDllNameWModule(&slDlssdNamesW);
    if (slDlssd != nullptr)
    {
        State::Instance().postCodes |= PostCode::SlPluginsAlreadyInMemory;
        LOG_WARN("sl.dlss_d.dll already in memory");
        StreamlineHooks::hookDlssd(slDlssd);
    }

    HMODULE slDlssg = nullptr;
    slDlssg = GetDllNameWModule(&slDlssgNamesW);
    if (slDlssg != nullptr)
    {
        State::Instance().postCodes |= PostCode::SlPluginsAlreadyInMemory;
        LOG_WARN("sl.dlss_g.dll already in memory");
        StreamlineHooks::hookDlssg(slDlssg);
    }

    HMODULE slReflex = nullptr;
    slReflex = GetDllNameWModule(&slReflexNamesW);
    if (slReflex != nullptr)
    {
        State::Instance().postCodes |= PostCode::SlPluginsAlreadyInMemory;
        LOG_WARN("sl.reflex.dll already in memory");
        StreamlineHooks::hookReflex(slReflex);
    }

    HMODULE slPcl = nullptr;
    slPcl = GetDllNameWModule(&slPclNamesW);
    if (slPcl != nullptr)
    {
        State::Instance().postCodes |= PostCode::SlPluginsAlreadyInMemory;
        LOG_WARN("sl.pcl.dll already in memory");
        StreamlineHooks::hookPcl(slPcl);
    }

    HMODULE slCommon = nullptr;
    slCommon = GetDllNameWModule(&slCommonNamesW);
    if (slCommon != nullptr)
    {
        State::Instance().postCodes |= PostCode::SlPluginsAlreadyInMemory;
        LOG_WARN("sl.common.dll already in memory");
        StreamlineHooks::hookCommon(slCommon);
    }

    // XeSS
    HMODULE xessModule = nullptr;
    xessModule = GetDllNameWModule(&xessNamesW);
    if (xessModule != nullptr)
    {
        LOG_DEBUG("libxess.dll already in memory");
        XeSSProxy::InitXeSS(xessModule);
    }

    HMODULE xessDx11Module = nullptr;
    xessDx11Module = GetDllNameWModule(&xessDx11NamesW);
    if (xessDx11Module != nullptr)
    {
        LOG_DEBUG("libxess_dx11.dll already in memory");
        XeSSProxy::InitXeSSDx11(xessDx11Module);
    }

    // NVNGX
    HMODULE nvngxModule = nullptr;
    nvngxModule = GetDllNameWModule(&nvngxNamesW);
    if (nvngxModule != nullptr)
    {
        LOG_DEBUG("nvngx.dll already in memory");
        NVNGXProxy::InitNVNGX(nvngxModule);
    }

    // FFX Dx12
    HMODULE ffxDx12Module = nullptr;
    ffxDx12Module = GetDllNameWModule(&ffxDx12NamesW);
    if (ffxDx12Module != nullptr)
    {
        LOG_DEBUG("amd_fidelityfx_dx12.dll already in memory");
        FfxApiProxy::InitFfxDx12(ffxDx12Module);
    }

    HMODULE ffxDx12SRModule = nullptr;
    ffxDx12SRModule = GetDllNameWModule(&ffxDx12UpscalerNamesW);
    if (ffxDx12SRModule != nullptr)
    {
        LOG_DEBUG("amd_fidelityfx_upscaler_dx12.dll already in memory");
        FfxApiProxy::InitFfxDx12_SR(ffxDx12SRModule);
    }

    HMODULE ffxDx12FGModule = nullptr;
    ffxDx12FGModule = GetDllNameWModule(&ffxDx12FGNamesW);
    if (ffxDx12FGModule != nullptr)
    {
        LOG_DEBUG("amd_fidelityfx_framegeneration_dx12.dll already in memory");
        FfxApiProxy::InitFfxDx12_FG(ffxDx12FGModule);
    }

    HMODULE ffxDx12DenoiserModule = nullptr;
    ffxDx12DenoiserModule = GetDllNameWModule(&ffxDx12DenoiserNamesW);
    if (ffxDx12DenoiserModule != nullptr)
    {
        LOG_DEBUG("amd_fidelityfx_denoiser_dx12.dll already in memory");
        FfxApiProxy::InitFfxDx12_Denoiser(ffxDx12DenoiserModule);
    }

    HMODULE ffxDx12RadianceModule = nullptr;
    ffxDx12RadianceModule = GetDllNameWModule(&ffxDx12RadianceNamesW);
    if (ffxDx12RadianceModule != nullptr)
    {
        LOG_DEBUG("amd_fidelityfx_radiancecache_dx12.dll already in memory");
        FfxApiProxy::InitFfxDx12_Radiance(ffxDx12RadianceModule);
    }

    // FFX Vulkan
    HMODULE ffxVkModule = nullptr;
    ffxVkModule = GetDllNameWModule(&ffxVkNamesW);
    if (ffxVkModule != nullptr)
    {
        LOG_DEBUG("amd_fidelityfx_vk.dll already in memory");
        FfxApiProxy::InitFfxVk(ffxVkModule);
    }

    HMODULE ffxVkSRModule = nullptr;
    ffxVkSRModule = GetDllNameWModule(&ffxVkUpscalerNamesW);
    if (ffxVkSRModule != nullptr)
    {
        LOG_DEBUG("amd_fidelityfx_upscaler_vk.dll already in memory");
        FfxApiProxy::InitFfxVk_SR(ffxVkSRModule);
    }

    HMODULE ffxVkFGModule = nullptr;
    ffxVkFGModule = GetDllNameWModule(&ffxVkFGNamesW);
    if (ffxVkFGModule != nullptr)
    {
        LOG_DEBUG("amd_fidelityfx_framegeneration_vk.dll already in memory");
        FfxApiProxy::InitFfxVk_FG(ffxVkFGModule);
    }

    // Hook kernel32 methods
    if (!Config::Instance()->EarlyHooking.value_or_default())
    {
        NtdllHooks::Hook();
        KernelHooks::Hook();
    }

    auto gameAgilityVersion = CheckAgilitySDK(exeModule);
    if (State::Instance().isWindows10 && !State::Instance().isRunningOnLinux &&
        !Config::Instance()->FsrAgilitySDKUpgrade.has_value())
    {
        auto corePath =
            std::filesystem::path(Config::Instance()->MainDllPath.value()) / L"D3D12_OptiScaler" / L"D3D12Core.dll";

        if (gameAgilityVersion < customD3D12SDKVersion)
        {
            if (std::error_code ec; std::filesystem::exists(corePath, ec))
            {
                LOG_INFO("Enabling AgilitySDKUpgrade");
                Config::Instance()->FsrAgilitySDKUpgrade.set_volatile_value(true);
            }
            else
            {
                LOG_WARN("Game's Agility SDK is older than {}, but AgilitySDKUpgrade can't be enabled, {} is missing",
                         customD3D12SDKVersion, wstring_to_string(corePath.wstring()));
            }
        }
    }

    // For Agility SDK Upgrade
    if (Config::Instance()->FsrAgilitySDKUpgrade.value_or_default())
    {
        RunAgilityUpgrade(GetDllNameWModule(&dx12NamesW));
    }

    if ((ShouldLoadSpecialK() || ShouldLoadReShade()) && (!_staticLoad || !HookExeEntry()))
        LoadInjectedDlls();

    // Luma's device can't be created under the loader lock, without the entry hook the first factory does it
    if (_staticLoad && ShouldLoadForLuma())
        HookExeEntry();

    // Version check
    if (Config::Instance()->CheckForUpdate.value_or_default())
        VersionCheck::Start();

    return;
}

static void printQuirks(flag_set<GameQuirk>& quirks)
{
    auto state = &State::Instance();
    std::vector<std::string> stringQuirks;

    if (quirks & GameQuirk::CyberpunkHudlessState)
        stringQuirks.push_back("Fixing DLSSG's hudless state in Cyberpunk");

    if (quirks & GameQuirk::FSRFGHudlessMismatchFixup)
        stringQuirks.push_back("FSR FG hudless mismatch fixup");

    if (quirks & GameQuirk::SkipFsr3Method)
        stringQuirks.push_back("Skipping first FSR 3 method");

    if (quirks & GameQuirk::LoadD3D12Manually)
        stringQuirks.push_back("Load d3d12.dll");

    if (quirks & GameQuirk::KernelBaseHooks)
        stringQuirks.push_back("Enable KernelBase hooks");

    if (quirks & GameQuirk::VulkanDLSSBarrierFixup)
        stringQuirks.push_back("Fix DLSS/DLSSG barriers on Vulkan");

    if (quirks & GameQuirk::ForceUnrealEngine)
        stringQuirks.push_back("Force detected engine as Unreal Engine");

    if (quirks & GameQuirk::NoFSRFGFirstSwapchain)
        stringQuirks.push_back("Skip turning the first swapchain created into an FSR swapchain");

    if (quirks & GameQuirk::FixSlSimulationMarkers)
        stringQuirks.push_back("Correct simulation start marker's frame id");

    if (quirks & GameQuirk::HitmanReflexHacks)
        stringQuirks.push_back("Hack for broken Hitman reflex");

    if (quirks & GameQuirk::SkipD3D11FeatureLevelElevation)
        stringQuirks.push_back("Skipping D3D11 feature level elevation, native FSR3.1 will be disabled!");

    if (quirks & GameQuirk::CreateD3D12DeviceForLuma)
        stringQuirks.push_back("Create D3D12 device for Luma before loading Reshade");

    if (quirks & GameQuirk::LoadVulkanManually)
        stringQuirks.push_back("Load vulkan-1.dll");

    if (quirks & GameQuirk::ForceCreateD3D12Device)
        stringQuirks.push_back("Force create D3D12 device for w/Dx12");

    if (quirks & GameQuirk::ForceDepthD32S8)
        stringQuirks.push_back("Force depth as D32S8");

    if (quirks & GameQuirk::PregmataFixDLSSModes)
        stringQuirks.push_back("Fix DLSS quality selection in Pragmata");

    if (quirks & GameQuirk::IgnoreValidUntilEvaluateForFG)
        stringQuirks.push_back("Ignore ValidUntilEvaluate resources for FG");

    if (quirks & GameQuirk::IgnoreTagsWithoutHudlessForFG)
        stringQuirks.push_back("Ignore tagging calls that lack Hudless resource for FG");

    if (quirks & GameQuirk::ForceFGRenderSizeMVs)
        stringQuirks.push_back("Force FG render size motion vectors");

    if (quirks & GameQuirk::CreateSLOnThe2ndDevice)
        stringQuirks.push_back("Create SL on the 2nd device");

    if (quirks & GameQuirk::XeFGCameraMotionFill)
        stringQuirks.push_back("Fill XeFG input motion vectors with camera motion");

    if (quirks & GameQuirk::DLSSGFrameLatencyWaitable)
        stringQuirks.push_back("Request frame latency waitable swapchain for DLSSG");

    if (quirks & GameQuirk::HideFsr4)
        stringQuirks.push_back("Hide FSR 4 from the game's FFX version list");

    if (quirks & GameQuirk::ExtendedSearch)
        stringQuirks.push_back("Extended DLL search");

    if (quirks & GameQuirk::PadToMaxUpscaleSize)
        stringQuirks.push_back("Keep DLSS/XeSS at the max FFX upscale size");

    state->detectedQuirks.append_range(stringQuirks);
    for (auto& stringQuirk : stringQuirks)
        spdlog::info("Quirk: {}", stringQuirk);

    return;
}

static void CheckQuirks(bool isNvidia)
{
    Util::GetExeInfo();

    LOG_INFO("Game's Exe: {0}", State::Instance().gameExe);
    LOG_INFO("Game Name: {0}", State::Instance().gameName);
    LOG_INFO("Game Version: {0}", State::Instance().gameVersion);
    LOG_INFO("Game Engine: {0}", magic_enum::enum_name(State::Instance().gameEngine));

#ifndef _DEBUG
    // Hash is very slow on Debug builds + we don't need to check our own hashes
    if (Config::Instance()->LogToFile.value_or_default() && Config::Instance()->LogLevel.value_or_default() == 0)
    {
        SHA1 checksum;
        std::ifstream file(Util::ExePath(), std::ios::binary);

        checksum.update(file);
        const std::string hash = checksum.final();

        LOG_TRACE("Game's Exe SHA1: {}", hash);
    }
#endif

    GameConfigContext context;
    context.isNvidia = isNvidia;

    auto quirks = ApplyGameConfigs(State::Instance().gameExe, context);

    // For Luma, we assume if Luma addon in game folder it's used
    const auto dir = Util::ExePath().parent_path();
    bool lumaDetected = false;

    for (const auto& entry : std::filesystem::directory_iterator(dir))
    {
        if (!entry.is_regular_file())
            continue;

        const auto& path = entry.path();
        if (path.extension() == L".addon" || path.extension() == L".addon64")
        {
            const auto fname = Util::ToLower(path.filename().wstring());

            // starts with "luma-" or "dlaa-inject"
            if ((fname.rfind(L"luma-", 0) == 0) || (fname.rfind(L"dlaa-inject", 0) == 0))
            {
                lumaDetected = true;
                break;
            }
        }
    }

    if (lumaDetected)
    {
        if (!Config::Instance()->DxgiSpoofing.has_value())
        {
            LOG_INFO("Luma detected, disabling DxgiSpoofing");
            State::Instance().detectedQuirks.push_back("Luma detected, disabling DxgiSpoofing");
            Config::Instance()->DxgiSpoofing.set_volatile_value(false);
        }

        if (!Config::Instance()->DontUseNTShared.has_value())
        {
            LOG_INFO("Luma detected, enabling DontUseNTShared");
            State::Instance().detectedQuirks.push_back("Luma detected, enabling DontUseNTShared");
            Config::Instance()->DontUseNTShared.set_volatile_value(true);
        }

        if (!Config::Instance()->CreateD3D12DeviceForLuma.has_value())
        {
            quirks |= GameQuirk::LoadD3D12Manually;

            if (Config::Instance()->LoadReShade.value_or_default())
            {
                Config::Instance()->CreateD3D12DeviceForLuma.set_volatile_value(true);
                quirks |= GameQuirk::CreateD3D12DeviceForLuma;
            }
        }
    }

    // For Sekiro TSR
    if (std::filesystem::exists(Util::ExePath().parent_path() / L"SekiroTSRLoader.addon"))
    {
        if (!Config::Instance()->DxgiSpoofing.has_value())
        {
            LOG_INFO("Sekiro TSR detected, disabling DxgiSpoofing");
            State::Instance().detectedQuirks.push_back("Luma UE detected, disabling DxgiSpoofing");
            Config::Instance()->DxgiSpoofing.set_volatile_value(false);
        }

        if (!Config::Instance()->DontUseNTShared.has_value())
        {
            LOG_INFO("Sekiro TSR detected, enabling DontUseNTShared");
            State::Instance().detectedQuirks.push_back("Sekiro TSR detected, enabling DontUseNTShared");
            Config::Instance()->DontUseNTShared.set_volatile_value(true);
        }

        if (!Config::Instance()->CreateD3D12DeviceForLuma.has_value())
        {
            quirks |= GameQuirk::LoadD3D12Manually;

            if (Config::Instance()->LoadReShade.value_or_default())
            {
                Config::Instance()->CreateD3D12DeviceForLuma.set_volatile_value(true);
                quirks |= GameQuirk::CreateD3D12DeviceForLuma;
            }
        }
    }

    // if (!Config::Instance()->DxgiFactoryWrapping.has_value() && Config::Instance()->LoadReShade.value_or_default() &&
    //     quirks & GameQuirk::CreateD3D12DeviceForLuma && State::Instance().activeFgInput != FGInput::NoFG &&
    //     State::Instance().activeFgInput != FGInput::NvngxFG)
    //{
    //     Config::Instance()->DxgiFactoryWrapping.set_volatile_value(true);
    //     State::Instance().detectedQuirks.push_back("Factory wrapping enabled due to delayed ReShade + FG");
    //     LOG_INFO("Factory wrapping enabled due to delayed ReShade + FG");
    // }

    // ILION v1.3.1: ReShade loaded by OptiScaler ([Plugins] LoadReShade) with an FG output that makes its own swapchain
    // (XeFG, FSR FG). With the DXGI factory hooked, ReShade wraps the swapchain the game gets, the FG's own, and the
    // first present ended in an access violation (Dawnwalker, RX 7900 XTX, every launch: XeFG in the D3D12 driver, then
    // E_ABORT from Streamline; OptiFG + FSR FG in D3D12Core, called from ReShade). With the factory wrapped, ReShade
    // wraps the swapchain the FG creates for itself, under the interpolation, and the game starts (startup trials in
    // the same game: XeFG with DLSSG / OptiFG input, FSR FG with DLSSG / OptiFG input). Only when the user didn't choose
    if (!Config::Instance()->DxgiFactoryWrapping.has_value() && Config::Instance()->LoadReShade.value_or_default() &&
        (State::Instance().activeFgOutput == FGOutput::XeFG || State::Instance().activeFgOutput == FGOutput::FSRFG) &&
        State::Instance().activeFgInput != FGInput::NoFG)
    {
        Config::Instance()->DxgiFactoryWrapping.set_volatile_value(true);
        State::Instance().detectedQuirks.push_back("Factory wrapping enabled: ReShade (LoadReShade) with XeFG / FSR FG");
        LOG_INFO("Factory wrapping enabled: ReShade (LoadReShade) with XeFG / FSR FG");
    }

    if (Config::Instance()->LoadSpecialK.value_or_default() && State::Instance().activeFgInput != FGInput::NoFG &&
        State::Instance().activeFgInput != FGInput::NvngxFG)
    {
        Config::Instance()->LoadSpecialK.set_volatile_value(false);
        State::Instance().detectedQuirks.push_back("FG Inputs are enabled, LoadSpecialK disabled");
        LOG_INFO("FG Inputs are enabled, LoadSpecialK disabled");
    }

    State::Instance().gameQuirks = quirks;

    printQuirks(quirks);
}

void CheckForExcludedProcess()
{
    std::wstring exeLower = Util::ExePath().filename().wstring();
    std::transform(exeLower.begin(), exeLower.end(), exeLower.begin(), ::towlower);

    // If target process is set, only that process is hooked
    if (Config::Instance()->TargetProcess.has_value())
    {
        // Config reads string as lowercase already
        std::wstring targetProcess = Config::Instance()->TargetProcess.value();

        if (exeLower != targetProcess)
        {
            _passThruMode = true;
            return;
        }
    }

    // Config reads string as lowercase already
    static const std::wstring exclusionList = Config::Instance()->ProcessExclusionList.value_or_default() + L"|";

    static std::vector<std::wstring> exclusions = []()
    {
        std::vector<std::wstring> result;
        size_t start = 0, end;

        while ((end = exclusionList.find(L'|', start)) != std::wstring::npos)
        {
            result.emplace_back(exclusionList.substr(start, end - start));
            start = end + 1;
        }
        return result;
    }();

    for (auto& e : exclusions)
    {
        if (exeLower == e)
        {
            _passThruMode = true;
            return;
        }
    }

    _passThruMode = false;
}

void CheckMemoryForProxies()
{
    FfxApiProxy::InitFfxDx12();
    FfxApiProxy::InitFfxDx12_SR();
    FfxApiProxy::InitFfxDx12_FG();
    FfxApiProxy::InitFfxDx12_Denoiser();
    FfxApiProxy::InitFfxDx12_Radiance();

    // Only the game's XeFG already in memory, the one OptiScaler loads next is hooked once the game loads it too
    auto gameXeFG = KernelBaseProxy::GetModuleHandleW_()(L"libxess_fg.dll");

    XeSSProxy::InitXeSS();
    XeSSProxy::InitXeSSDx11();
    // ILION v1.4.1: OptiScaler\libxess_fg.dll (1.3.1.78) for ILION's XeFG output and its XeMFG variants. With XeMFG XMX
    // in use, the 1.3.4 of its own folder instead, preloaded here the same way: realdody then finds that module (its
    // folder is the first place it looks), one libxess_fg in the process. Its XeFG context failed on AMD when realdody
    // loaded the library itself later
    if (Nvngx_XeMfgXmx::InUse())
    {
        if (const auto xmx = Nvngx_XeMfgXmx::FindInstall(); !xmx.dll.empty() && xmx.libxessFg)
            XeFGProxy::InitXeFG(xmx.dll.parent_path().wstring());
    }
    // ILION v1.4.1.2: DLSSG-XeFG uses first the libxess_fg.dll next to its own DLL, loaded by full path. When that copy
    // exists it is the one preloaded here, so the process holds a single libxess_fg (the cause of XeMFG XMX's
    // "Failed to create heap"); without it, OptiScaler's own copy as before, which DLSSG-XeFG then finds
    else if (const auto fakeMichau = Nvngx_FakeMichau::InUse() ? Nvngx_FakeMichau::FindDll() : std::filesystem::path {};
             !fakeMichau.empty() && std::filesystem::is_regular_file(fakeMichau.parent_path() / L"libxess_fg.dll"))
    {
        XeFGProxy::InitXeFG(fakeMichau.parent_path().wstring());
    }
    else
    {
        XeFGProxy::InitXeFG();
    }
    XeLLProxy::InitXeLL();
    XeFGInputs::Hook(gameXeFG);

    NVNGXProxy::InitNVNGX();
}

DWORD WINAPI getGpuInfo(LPVOID hModuleVoid)
{
    // TODO: dxvk deadlocks when the game and identify gpu calls create at the same time
    // Sleep(1000);

    auto primaryGpu = IdentifyGpu::getPrimaryGpu();

    // We don't yet know if the GPU supports FSR 4 so hook any AMD
    if (primaryGpu.vendorId == VendorId::AMD)
        Amdxc64Hooks::Init();

    else if (IsFsr4Int8(Config::Instance()->Fsr4GpuSupport.value_or_default()))
    {
        // We need spoofing hooks for FFX but want to avoid spoofing for the rest of the game
        if (!Config::Instance()->DxgiSpoofing.value_or_default())
        {
            std::wstring wname = string_to_wstring(primaryGpu.name);
            Config::Instance()->SpoofedVendorId.set_volatile_value(primaryGpu.vendorId);
            Config::Instance()->SpoofedDeviceId.set_volatile_value(primaryGpu.deviceId);
            Config::Instance()->SpoofedGPUName.set_volatile_value(wname);
        }

        Config::Instance()->DxgiSpoofing.set_volatile_value(true);
    }

    // If DX12 already loaded then grab the full GPU info right away
    if (hModuleVoid)
        IdentifyGpu::updateD3d12Capabilities();

    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
    {
        DisableThreadLibraryCalls(hModule);

        // Only a static load runs before the exe entry
        _staticLoad = lpReserved != nullptr;

        HMODULE handle = nullptr;
        OSVERSIONINFOW winVer { 0 };

        State::Instance().startTime = std::chrono::steady_clock::now();

        dllModule = hModule;
        exeModule = GetModuleHandle(nullptr);
        processId = GetCurrentProcessId();

        // Main Opti DLL path
        if (!Config::Instance()->MainDllPath.has_value())
        {
            Config::Instance()->MainDllPath.set_volatile_value(L"OptiScaler");
        }

        if (std::filesystem::path mainDllPath(Config::Instance()->MainDllPath.value()); mainDllPath.is_relative())
        {
            Config::Instance()->MainDllPath.set_volatile_value(Util::DllPath().parent_path() / mainDllPath);
        }

        // If path is invalid or doesn't exist, use Opti's folder as main
        if (!std::filesystem::exists(Config::Instance()->MainDllPath.value()) ||
            !std::filesystem::is_directory(Config::Instance()->MainDllPath.value()))
        {
            Config::Instance()->MainDllPath.set_volatile_value(Util::DllPath().parent_path());
        }

        // Clean up path
        Config::Instance()->MainDllPath.set_volatile_value(
            std::filesystem::absolute(Config::Instance()->MainDllPath.value()));

        // If path is not set or incorrect
        if (!Config::Instance()->PluginPath.has_value() ||
            (!std::filesystem::exists(Config::Instance()->PluginPath.value()) ||
             !std::filesystem::is_directory(Config::Instance()->PluginPath.value())))
        {
            Config::Instance()->PluginPath.set_volatile_value(
                std::filesystem::path(Config::Instance()->MainDllPath.value()) / L"plugins");
        }

        CheckForExcludedProcess();

        if (_passThruMode)
        {
            NtdllProxy::Init();
            KernelBaseProxy::Init();
            Kernel32Proxy::Init();

            CheckWorkingMode();
            return true;
        }

#ifdef _DEBUG // VER_PRE_RELEASE
        // Enable file logging for pre builds
        Config::Instance()->LogToFile.set_volatile_value(true);

        // Set log level to debug
        if (Config::Instance()->LogLevel.value_or_default() > 1)
            Config::Instance()->LogLevel.set_volatile_value(1);
#endif

        PrepareLogger();

        spdlog::warn("{0} loaded, ILION v{1}", BuildInfo::ProductName(), BuildInfo::IlionVersion());
        spdlog::warn("---------------------------------");
        spdlog::warn("OptiScaler is freely downloadable from");
        spdlog::warn("GitHub : https://github.com/optiscaler/OptiScaler/releases");
        spdlog::warn("Nexus  : https://www.nexusmods.com/site/mods/986");
        spdlog::warn("If you paid for these files, you've been scammed!");
        spdlog::warn("DO NOT USE IN MULTIPLAYER GAMES");
        spdlog::info("");
        spdlog::info("LogLevel: {}", Config::Instance()->LogLevel.value_or_default());

        CustomProviders::ResolveAll();

        spdlog::info("");
        if (Util::GetRealWindowsVersion(winVer))
            spdlog::info("Windows version: {} ({}.{}.{})", Util::GetWindowsName(winVer), winVer.dwMajorVersion,
                         winVer.dwMinorVersion, winVer.dwBuildNumber, winVer.dwPlatformId);
        else
            spdlog::warn("Can't read windows version");

        State::Instance().isWindows10 = winVer.dwMajorVersion == 10 && winVer.dwBuildNumber < 22000;

        spdlog::info("");

        spdlog::info("Config parameters:");
        for (const std::string& l : Config::Instance()->GetConfigLog())
            spdlog::info(l);

        spdlog::info("");
        spdlog::info("Setting DllPath to {}", wstring_to_string(Config::Instance()->MainDllPath.value()));
        spdlog::info("");

#ifdef VER_PRE_RELEASE
        spdlog::info("Pre-release build, disabling update checks");
        Config::Instance()->CheckForUpdate.set_volatile_value(false);
#endif

        // Initial state of FG
        State::Instance().activeFgInput = Config::Instance()->FGInput.value_or_default();
        State::Instance().activeFgOutput = Config::Instance()->FGOutput.value_or_default();
        State::Instance().activeFgNvngx = Config::Instance()->FGNvngxReplacement.value_or_default();
        State::Instance().activeFgNvngxCustom = State::Instance().fgNvngxCustom;
        State::Instance().activeFsrFgProvider = Config::Instance()->FSRFGProvider.value_or("");

        // Ensure valid FG configuration
        if (State::Instance().activeFgOutput == FGOutput::NoFG && State::Instance().activeFgInput != FGInput::NoFG &&
            State::Instance().activeFgInput != FGInput::NvngxFG &&
            State::Instance().activeFgInput != FGInput::ForceXeLL)
        {
            spdlog::warn("FG Input {} without an FG Output, using none",
                         magic_enum::enum_name(State::Instance().activeFgInput));
            State::Instance().activeFgInput = FGInput::NoFG;
        }

        if (State::Instance().activeFgInput != FGInput::NvngxFG &&
            State::Instance().activeFgOutput != FGOutput::DLSSG &&
            State::Instance().activeFgOutput != FGOutput::FlipMeter)
        {
            State::Instance().activeFgNvngx = FGNvngxReplacement::None;
        }

        if (State::Instance().activeFgInput == FGInput::NvngxFG)
            State::Instance().activeFgOutput = FGOutput::NoFG;

        // XeFG input to XeFG output runs the game's own XeFG, without OptiScaler's FG pipeline
        if (XeFGInputs::Passthrough())
            State::Instance().activeFgOutput = FGOutput::NoFG;

        // Init Kernel proxies
        NtdllProxy::Init();
        KernelBaseProxy::Init();
        Kernel32Proxy::Init();

        CrashDumps::Init();

        // Check for Wine
        spdlog::info("");
        State::Instance().isRunningOnLinux = IsRunningOnWine();

        // Not foolproof
        // calls LoadLibraryExW inside DllMain but seems mostly fine if we only call NvAPI_GetInterfaceVersionString
        auto isNvidiaViaNvapi = [&]()
        {
            bool nvidiaDetected = false;

            // Only try to load the real nvapi which is located in system32
            auto nvapiModule = NtdllProxy::LoadLibraryExW_Ldr(L"nvapi64.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);

            // No nvapi, should not be nvidia
            if (!nvapiModule)
            {
                spdlog::debug("Nvidia detected: {}", nvidiaDetected);
                return nvidiaDetected;
            }

            if (auto o_NvAPI_QueryInterface =
                    (PFN_NvApi_QueryInterface) KernelBaseProxy::GetProcAddress_()(nvapiModule, "nvapi_QueryInterface"))
            {
                // dxvk-nvapi calls CreateDxgiFactory which we can't do because we are inside DLL_PROCESS_ATTACH
                NvAPI_ShortString desc;
                auto* getVersion = GET_INTERFACE(NvAPI_GetInterfaceVersionString, o_NvAPI_QueryInterface);
                if (getVersion && getVersion(desc) == NVAPI_OK &&
                    (std::string_view(desc) == std::string_view("NVAPI Open Source Interface (DXVK-NVAPI)") ||
                     std::string_view(desc) == std::string_view("DXVK_NVAPI")))
                {
                    spdlog::debug("Using dxvk-nvapi");

                    // Display devices only list GPUs with a display attached, which misses PRIME offload GPUs,
                    // but the call also makes Wine refresh the GPU list in the registry
                    DISPLAY_DEVICEA dd = {};
                    dd.cb = sizeof(dd);
                    int deviceIndex = 0;

                    while (EnumDisplayDevicesA(nullptr, deviceIndex, &dd, 0))
                    {
                        if (dd.StateFlags & DISPLAY_DEVICE_ACTIVE && std::string_view(dd.DeviceID).contains("VEN_10DE"))
                        {
                            // Having any Nvidia GPU active will take precedence
                            nvidiaDetected = true;
                        }
                        deviceIndex++;
                    }

                    // Wine registers every GPU here and removes the ones that are gone
                    // Windows keeps removed GPUs here too, so it's only used on Wine
                    HKEY pciKey = nullptr;
                    if (!nvidiaDetected && State::Instance().isRunningOnLinux &&
                        RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"System\\CurrentControlSet\\Enum\\PCI", 0, KEY_READ,
                                      &pciKey) == ERROR_SUCCESS)
                    {
                        wchar_t deviceName[256];

                        for (DWORD i = 0; !nvidiaDetected; i++)
                        {
                            DWORD deviceNameLength = std::size(deviceName);
                            if (RegEnumKeyExW(pciKey, i, deviceName, &deviceNameLength, nullptr, nullptr, nullptr,
                                              nullptr) != ERROR_SUCCESS)
                            {
                                break;
                            }

                            if (!std::wstring_view(deviceName).starts_with(L"VEN_10DE"))
                                continue;

                            HKEY deviceKey = nullptr;
                            if (RegOpenKeyExW(pciKey, deviceName, 0, KEY_READ, &deviceKey) != ERROR_SUCCESS)
                                continue;

                            wchar_t instanceName[256];

                            for (DWORD j = 0; !nvidiaDetected; j++)
                            {
                                DWORD instanceNameLength = std::size(instanceName);
                                if (RegEnumKeyExW(deviceKey, j, instanceName, &instanceNameLength, nullptr, nullptr,
                                                  nullptr, nullptr) != ERROR_SUCCESS)
                                {
                                    break;
                                }

                                // Skip non display devices like the HDMI audio
                                wchar_t classGuid[64];
                                DWORD classGuidSize = sizeof(classGuid);
                                if (RegGetValueW(deviceKey, instanceName, L"ClassGUID", RRF_RT_REG_SZ, nullptr,
                                                 classGuid, &classGuidSize) == ERROR_SUCCESS &&
                                    _wcsicmp(classGuid, L"{4d36e968-e325-11ce-bfc1-08002be10318}") == 0)
                                {
                                    spdlog::debug("Nvidia GPU found: {}", wstring_to_string(deviceName));
                                    nvidiaDetected = true;
                                }
                            }

                            RegCloseKey(deviceKey);
                        }

                        RegCloseKey(pciKey);
                    }
                }
                else if (o_NvAPI_QueryInterface(0x21382138))
                {
                    spdlog::error("Using fakenvapi as nvapi64.dll, remove it!");
                    nvidiaDetected = false;
                }
                else
                {
                    spdlog::debug("Using Nvidia's nvapi");
                    auto init = GET_INTERFACE(NvAPI_Initialize, o_NvAPI_QueryInterface);
                    if (init && init() == NVAPI_OK)
                    {
                        nvidiaDetected = true;

                        if (auto unload = GET_INTERFACE(NvAPI_Unload, o_NvAPI_QueryInterface))
                            unload();
                    }
                }
            }

            NtdllProxy::FreeLibrary_Ldr(nvapiModule);

            spdlog::debug("Nvidia detected: {}", nvidiaDetected);

            return nvidiaDetected;
        };

        // Opt in only to a configured, versioned Windows d4r backend. The
        // marker performs no HIP/NGX initialization under DLL_PROCESS_ATTACH.
        // GPU support is still checked by its public NGX requirements API.
        if (Config::Instance()->DLSSEnabled.value_or_default() &&
            Config::Instance()->DLSSAllowExternalBackend.value_or_default() &&
            Config::Instance()->NvngxPath.has_value())
        {
            auto backendPath = std::filesystem::path(Config::Instance()->NvngxPath.value()) / L"_nvngx.dll";
            if (backendPath.is_absolute())
            {
                auto module = NtdllProxy::LoadLibraryExW_Ldr(backendPath.c_str(), nullptr,
                    LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
                if (module)
                {
                    using BackendVersion = unsigned (*)();
                    auto version = reinterpret_cast<BackendVersion>(
                        KernelBaseProxy::GetProcAddress_()(module, "d4r_WindowsBackendVersion"));
                    State::Instance().dlssExternalBackend = version && version() == 1;
                    NtdllProxy::FreeLibrary_Ldr(module);
                }
            }
            if (State::Instance().dlssExternalBackend)
                spdlog::info("Validated external Windows d4r DLSS backend; retaining physical GPU identity");
            else
                spdlog::error("AllowExternalBackend requires an absolute NvngxPath containing Windows d4r ABI 1");
        }
        bool possibleNvidia = !State::Instance().dlssExternalBackend && isNvidiaViaNvapi();
        IdentifyGpu::setNvidiaHint(possibleNvidia);

        // Quirks can extend the DLL search
        spdlog::info("");
        CheckQuirks(possibleNvidia);

        spdlog::info("");
        spdlog::info("Check for DLSS files");

        auto exePath = Util::ExePath().remove_filename();
        auto optiDllPath = std::filesystem::path(Config::Instance()->MainDllPath.value());

        // Same order as the NGX feature path list
        std::vector<std::filesystem::path> dllFolders;

        if (Config::Instance()->DLSSFeaturePath.has_value())
            dllFolders.push_back(Config::Instance()->DLSSFeaturePath.value());

        // Only the game folder is searched recursively
        auto findDll = [&](const wchar_t* name) -> std::optional<std::filesystem::path>
        {
            for (const auto& folder : dllFolders)
            {
                std::error_code ec;

                if (auto path = folder / name; std::filesystem::is_regular_file(path, ec))
                {
                    LOG_INFO(L"{} found at {}", name, folder.wstring());
                    return path;
                }
            }

            return Util::FindFilePath(exePath, name);
        };

        if (Config::Instance()->NVNGX_DLSS_Library.has_value())
        {
            std::filesystem::path dlssPath(Config::Instance()->NVNGX_DLSS_Library.value());
            std::error_code ec;

            // Accepts the dll or its folder
            if (std::filesystem::is_directory(dlssPath, ec))
                dlssPath /= L"nvngx_dlss.dll";

            if (std::filesystem::is_regular_file(dlssPath, ec))
            {
                Config::Instance()->NVNGX_DLSS_Library.set_volatile_value(dlssPath.wstring());
                State::Instance().NVNGX_DLSS_Path = dlssPath.wstring();
                dllFolders.push_back(dlssPath.parent_path());
                LOG_INFO("nvngx_dlss.dll override found at {}", wstring_to_string(dlssPath.wstring()));
            }
            else
            {
                LOG_WARN("nvngx_dlss.dll override not found at {}, ignoring it", wstring_to_string(dlssPath.wstring()));
                Config::Instance()->NVNGX_DLSS_Library.reset_volatile();
            }
        }

        dllFolders.push_back(optiDllPath);

        if (!State::Instance().NVNGX_DLSS_Path.has_value())
            State::Instance().NVNGX_DLSS_Path = findDll(L"nvngx_dlss.dll");

        State::Instance().NVNGX_DLSSD_Path = findDll(L"nvngx_dlssd.dll");
        State::Instance().NVNGX_DLSSG_Path = findDll(L"nvngx_dlssg.dll");

        // Not 100% accurate for Nvidia cards without DLSS
        if (Config::Instance()->DLSSEnabled.value_or_default() &&
            (possibleNvidia || State::Instance().dlssExternalBackend))
        {
            if (State::Instance().NVNGX_DLSS_Path.has_value())
            {
                spdlog::info("Enabling DLSS");
                Config::Instance()->DLSSEnabled.set_volatile_value(true);
            }
            else
            {
                spdlog::warn("nvngx_dlss.dll not found, disabling DLSS");
                Config::Instance()->DLSSEnabled.set_volatile_value(false);
            }

            // Assumes that dxgi spoofing is only used to enable DLSS
            if (!Config::Instance()->DxgiSpoofing.has_value())
            {
                spdlog::info("Disabling DxgiSpoofing");
                Config::Instance()->DxgiSpoofing.set_volatile_value(false);
            }
        }
        else
        {
            Config::Instance()->DLSSEnabled.set_volatile_value(false);
        }

        // Check for working mode and attach hooks
        spdlog::info("");
        CheckWorkingMode();
        CheckMemoryForProxies();

        // OptiFG & Overlay Checks
        if ((Config::Instance()->FGInput.value_or_default() == FGInput::Upscaler) &&
            !Config::Instance()->DisableOverlays.has_value())
            Config::Instance()->DisableOverlays.set_volatile_value(true);

        if (Config::Instance()->DisableOverlays.value_or_default())
        {
            _wputenv_s(L"SteamNoOverlayUIDrawing", L"1");
            SetEnvironmentVariableW(L"SteamNoOverlayUIDrawing", L"1");
        }

        // FSR4 Watermark, overrides environment variable only if set in config
        // The driver only checks whether the variable exists so disabling has to remove it
        if (Config::Instance()->Fsr4EnableWatermark.has_value())
        {
            Util::SetFlagEnvVar(L"MLSR-WATERMARK", Config::Instance()->Fsr4EnableWatermark.value());

            if (Config::Instance()->Fsr4EnableWatermark.value() && !Config::Instance()->FpsOverlayPosition.has_value())
                Config::Instance()->FpsOverlayPosition.set_volatile_value(FpsOverlayPos_TopRight);
        }

        if (Config::Instance()->FSRFGEnableWatermark.has_value())
        {
            Util::SetFlagEnvVar(L"MLFI-WATERMARK", Config::Instance()->FSRFGEnableWatermark.value());

            if (Config::Instance()->FSRFGEnableWatermark.value() && !Config::Instance()->FpsOverlayPosition.has_value())
                Config::Instance()->FpsOverlayPosition.set_volatile_value(FpsOverlayPos_TopRight);
        }

        // Asi plugins stay in DllMain, some expect to run before other dlls and the exe's TLS callbacks
        if (Config::Instance()->LoadAsiPlugins.value_or_default())
        {
            spdlog::info("");
            LoadAsiPlugins();
            // ILION 1.5.0: OpenRR's own menu off before any key can open it (its settings are in ILION)
            IlionOpenRr::EarlyTakeover();
        }
        else
        {
            LoadOpenRrAlone();
            IlionOpenRr::EarlyTakeover();
        }

        if (!Config::Instance()->DxgiSpoofing.has_value() && !State::Instance().nvngxReplacement.has_value())
        {
            LOG_WARN("Nvngx replacement not found!");

            if (!State::Instance().nvngxExists)
            {
                LOG_WARN("nvngx.dll not found! - disabling spoofing");
                Config::Instance()->DxgiSpoofing.set_volatile_value(false);
            }
        }

        if (Config::Instance()->EnableFsr2Inputs.value_or_default())
        {
            spdlog::info("");

            if (Config::Instance()->UseFsr2VulkanInputs.value_or_default())
                HookFSR2VkExeInputs();
            else if (Config::Instance()->UseFsr2Dx11Inputs.value_or_default())
                HookFSR2Dx11ExeInputs();
            else
            {
                handle = GetDllNameWModule(&fsr2NamesW);
                if (handle != nullptr)
                    HookFSR2Inputs(handle);

                handle = GetDllNameWModule(&fsr2BENamesW);
                if (handle != nullptr)
                    HookFSR2Dx12Inputs(handle);

                HookFSR2ExeInputs();
            }
        }

        if (Config::Instance()->EnableFsr3Inputs.value_or_default())
        {
            handle = GetDllNameWModule(&fsr3NamesW);
            if (handle != nullptr)
                HookFSR3Inputs(handle);

            handle = GetDllNameWModule(&fsr3BENamesW);
            if (handle != nullptr)
                HookFSR3Dx12Inputs(handle);

            HookFSR3ExeInputs();
        }
        // HookFfxExeInputs();

        if (State::Instance().activeFgInput == FGInput::FSRFG30)
        {
            FSR3FG::HookFSR3FGInputs();
            FSR3FG::HookFSR3FGExeInputs();
        }

        if (State::Instance().activeFgInput == FGInput::Upscaler &&
            State::Instance().gameEngine == GameEngineType::Unity && !Config::Instance()->FGResourceFlip.has_value())
        {
            LOG_WARN("Unity detected with Upscaler input, but FGResourceFlip is not set. Enabling it");
            Config::Instance()->FGResourceFlip.set_volatile_value(true);
        }

        for (size_t i = 0; i < 300; i++)
        {
            State::Instance().frameTimes.push_back(0.0f);
            State::Instance().upscaleTimes.push_back(0.0f);
        }

        spdlog::info("");
        spdlog::info("Init done");
        spdlog::info("---------------------------------------------");
        spdlog::info("");

        if (o_ExeEntry == nullptr)
            CreateThread(nullptr, 0, getGpuInfo, GetDllNameWModule(&dx12NamesW), 0, nullptr);

#ifndef _DEBUG
        if (Config::Instance()->LogLevel.value_or_default() == 0 && Config::Instance()->LogToFile.value_or_default())
        {
            std::thread(
                []()
                {
                    std::this_thread::sleep_for(std::chrono::minutes(10));

                    // If still logging after 10 minutes, send notification
                    if (Config::Instance()->LogLevel.value_or_default() == 0 &&
                        Config::Instance()->LogToFile.value_or_default())
                    {
                        ImGuiToast notification({ ImGuiToastType::Warning, 30000,
                                                  "That's likely unintended and will lead to big OptiScaler.log\n"
                                                  "Please disable logging to file and delete OptiScaler.log" });

                        notification.setTitle("Trace logging still active");

                        ImGui::InsertNotification(notification);
                    }
                })
                .detach();
        }
#endif

        break;
    }

    case DLL_PROCESS_DETACH:
        State::Instance().isShuttingDown = true;

        // Unhooking and cleaning stuff causing issues during shutdown.
        // Disabled for now to check if it cause any issues
        // UnhookApis();
        // unhookStreamline();
        // unhookGdi32();
        // unhookWintrust();
        // unhookCrypt32();
        // unhookAdvapi32();
        // DetachHooks();

        if (skModule != nullptr)
            NtdllProxy::FreeLibrary_Ldr(skModule);

        // ILION: the ReShade bridge leaves ReShade before ReShade is unloaded
        IlionReShade::Shutdown();

        if (reshadeModule != nullptr)
            NtdllProxy::FreeLibrary_Ldr(reshadeModule);

        if (_asiHandles.size() > 0)
        {
            for (size_t i = 0; i < _asiHandles.size(); i++)
                NtdllProxy::FreeLibrary_Ldr(_asiHandles[i]);
        }

        for (const PVOID& v : State::Instance().modulesToFree)
        {
            NtdllProxy::FreeLibrary_Ldr(v);
        }

        spdlog::info("");
        spdlog::info("DLL_PROCESS_DETACH");
        spdlog::info("Unloading OptiScaler");
        CloseLogger();

        break;

    case DLL_THREAD_ATTACH:
        // LOG_DEBUG_ONLY("DLL_THREAD_ATTACH from module: {0:X}, count: {1}", (UINT64)hModule, loadCount);
        break;

    case DLL_THREAD_DETACH:
        // LOG_DEBUG_ONLY("DLL_THREAD_DETACH from module: {0:X}, count: {1}", (UINT64)hModule, loadCount);
        break;

    default:
        LOG_WARN("Call reason: {0:X}", ul_reason_for_call);
        break;
    }

    return TRUE;
}
