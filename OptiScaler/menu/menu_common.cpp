#include "pch.h"
#include "menu_common.h"

#include "input/input_system.h"

#include "font/Hack_Compressed.h"
#include "font/RussoOne_Compressed.h"
#include "font/ShareTechMono_Compressed.h"

#include <proxies/XeSS_Proxy.h>
#include <proxies/XeFG_Proxy.h>
#include <proxies/FfxApi_Proxy.h>
#include <upscalers/ffx/FFXFeature.h>
#include <fsr4/FSR4Comparison.h>
#include <fsr4/FSR4Models.h>
#include <fsr4/FSR4DriverProviderVk.h>
#include <misc/HwTelemetry.h>
#include <misc/IlionBenchmark.h>
#include <misc/IlionCompare.h>
#include <misc/IlionCapture.h>
#include <misc/IlionReShade.h>
#include <misc/IlionReShadeCatalog.h>
#include <misc/IlionLatency.h>
#include <misc/IlionOverlay.h>
#include <misc/IlionVisual.h>
#include <misc/IlionVisualInterpret.h>
#include "ilion_lang.h"
#include <proxies/Streamline_Proxy.h>

#include <framegen/nvngx/Nvngx_FG.h>
#include <framegen/reprojection/Reprojection_Dx12.h>

#include <nvapi/fakenvapi.h>
#include <hooks/Reflex_Hooks.h>

#include <BuildInfo.h>
#include <version_check.h>

#include <imgui/imgui_internal.h>
#include "ilion_visual_panel.h"
#include "ilion_theme.h"
#include "ilion_info.h"
#include "ilion_d4r_panel.h"
#include <shaders/image_adjust/ImageAdjust_Common.h>
#include <imgui/ImGuiNotify.hpp>
#include <imgui/imgui_impl_win32.h>
#include <imgui/imgui_impl_uwp.h>

#include <mutex>
#include <atomic>
#include <functional>
#include <map>
#include <fstream>
#include <thread>
#include <cstdarg>

#include <array>
#include <numeric>
#include <chrono>
#include <memory>
#include <type_traits>
#include <misc/IdentifyGpu.h>
#include <misc/IlionExternalFg.h>
#include <hooks/Xell_Hooks.h>
#include <low_latency/input/input_common.h>

enum class UiTargetMode
{
    SDR,
    LinearHDR,
    ScRGB,
    PQ,
    HLG
};

#define MARK_ALL_BACKENDS_CHANGED()                                                                                    \
    for (auto& singleChangeBackend : State::Instance().changeBackend)                                                  \
        singleChangeBackend.second = true;

static float fontSize = 14.0f; // just changing this doesn't make other elements scale ideally
static ImVec2 overlaySize(0.0f, 0.0f);
static ImVec2 overlayPosition(-1000.0f, -1000.0f);
static bool _hdrTonemapApplied = false;
static ImVec4 SdrColors[ImGuiCol_COUNT];

static bool inputMenu = false;
static bool inputFG = false;
static bool fgHotkeyLeftDefault = false; // FG hotkey temporarily overrode Default (follow game)
static bool inputFps = false;
static bool inputFpsCycle = false;
static uint64_t lastInputTick = 0;
constexpr uint64_t debounceThreshold = 1000;

static bool hasGamepad = false;
static bool ffxInitTried = false;
static bool xefgInitTried = false;
static std::string windowTitle;
static std::string selectedUpscalerName = "";
static Upscaler currentBackend = Upscaler::Reset;
static std::string currentBackendName = "";
static int refreshRate = 0;
static ImVec2 lastPosition(-1000.0f, -1000.0f);

static ImVec2 splashPosition(-1000.0f, -1000.0f);
static ImVec2 splashSize(0.0f, 0.0f);
static double splashStart = 0.0;
static double splashLimit = 0.0;
static std::vector<std::string> splashText = { "Cope smarter, not harder",
                                               "Coping is strong with this one...",
                                               "This is where the fun begins...",
                                               "Got any more of them scalers?...",
                                               "Fake pixels and even faker frames...",
                                               "Fake frames, get your fake frames...",
                                               "I'm here to kick pixels and chew frames...",
                                               "I find your lack of supersampling disturbing...",
                                               "Frame by frame, I scale-up!",
                                               "Resistance is futile. Your pixels will be upscaled.",
                                               "I've got 99 problems, but low-res ain't one.",
                                               "It's over, DLSS, I have the higher ground!",
                                               "This isn't the resolution you're looking for",
                                               "To infinity and beyond... with ray tracing off",
                                               "I have a bad feeling about this frame pacing",
                                               "It's Dangerous to Go Alone-Take This Upscaler",
                                               "Upscaled beyond recognition.",
                                               "Trust the process. Ignore the shimmer.",
                                               "Real fake frames. Certified.",
                                               "The illusion of performance",
                                               "This upscaler belongs in a museum!",
                                               "Because native rendering is overrated.",
                                               "The more you upscaler, the more you save",
                                               "It's never too late to buy a better GPU",
                                               "We don't need real pixels where we're going",
                                               "Did you know that Intel released XeFG for everyone?",
                                               "MFG totally works with Nukem's 100%% no scam",
                                               "Some of those pixels might even be real!",
                                               "Just don't look too closely at the image",
                                               "Even supports \"software\" XeSS!",
                                               "It's too blurry to go alone, take RCAS with you",
                                               "Thanks nitec, back to you nitec",
                                               "Tested and approved by By-U",
                                               "0.8 was an inside job",
                                               "FSR4 DP4a wenETA, AMD plz",
                                               "OptiCopers, assemble!",
                                               "The Way It's Meant To Be Upscaled",
                                               "Your game may not even crash today",
                                               "Expanded and Enhanced",
                                               "It's only my 5th crash today",
                                               "Latency with FG? But I have good internet",
                                               "Console peasants can't do that",
                                               "Hope you don't have a good eyesight",
                                               "Such an aggressive upscaling? A bold move",
                                               "I almost don't feel the input lag",
                                               "And that's how you get to 60 FPS",
                                               "Together We Upscale",
                                               "For upscalers, by upscalers",
                                               "Opti Sports, it's in the sampling",
                                               "Render in your world. Upscale in ours",
                                               "All your pixels are belong to us",
                                               "Upscaling for the masses, not the classes",
                                               "Generating discord since 2023",
                                               "Enabling DLSS since 2023",
                                               "[REDACTED] never looked better",
                                               "Free and always free",
                                               "Getting unshackled from green chains in progress...",
                                               "Who's Nukem anyway?",
                                               "Compiling shaders... ETA: 05h:49m",
                                               "Did you really just pay 70 EUR for this game?!",
                                               "Guess who forgot about a nullptr check again",
                                               "AI can't outslop this",
                                               "Guess we're pre-alpha build demos now",
                                               "New app on the block - TH",
                                               "One more stutter and I might lose it",
                                               "Mostly stable, unlike the driver",
                                               "Vul... what? ~AMD",
                                               "My 8 points are floating",
                                               "No floating here - I'm strictly between -128 and 127",
                                               "Fake it til you bake it",
                                               "Worst case just turn it off and on",
                                               "*On a generative damage control mode at geometry level*",
                                               "Deep Learning Slop Sampling 5",
                                               "2D AI filters, now powered by just 2x 5090s",
                                               "Neural Slop Sampling with DLSS5",
                                               "DLSS 5 - the way it's meant to be slopped",
                                               "Just when I think I'm out, they scale me back in",
                                               "Like going in the first gear on the highway",
                                               "Nitec's Bizarre Upscaling",
                                               "\"Framegen really attracts some strange clientelle\"",
                                               "How to remove those corny messages?!",
                                               "<Your funny text goes here>" };

static std::string updateNoticeTag;
static std::string updateNoticeUrl;
static float lastMenuScale = 0.0f;
static CustomOptional<uint32_t> comboPreset { 0 };
static int lastKey = 0;
static bool capturingKey = false;
static int selectedMenuTab = 0;
static const char* requestedMenuTab = nullptr; // Label of a tab to switch to on the next frame
static bool personalPanelOpen = false;          // ILION: Customise panel of Personal, at the right of its boxes
static bool reshadePanelOpen = false;           // ILION: ReShade panel, joined to the right of the window (Graphics)

static void DedupeSrVariants(std::vector<FfxApiProxy::SrVariant>& list);

// The FSR upscaler dlls that can be selected. One list and one way of selecting for the Upscaling tab and for
// Benchmark, both show the same state.
static std::vector<FfxApiProxy::SrVariant>& SharedSrVariants()
{
    static std::vector<FfxApiProxy::SrVariant> variants = []
    {
        auto list = FfxApiProxy::SrVariants();
        DedupeSrVariants(list);
        return list;
    }();
    return variants;
}

static std::wstring& SharedDefaultSrDll()
{
    static std::wstring path = FfxApiProxy::SrVariantDllPath(L"");
    return path;
}

// Key of the upscaler of the list that is the same file as the default dll, empty when none is
static std::wstring& SharedDefaultTwin()
{
    static std::wstring key;
    return key;
}

// Same bytes in both files, read in chunks
static bool SameFileContent(const std::wstring& a, const std::wstring& b)
{
    std::error_code ec;

    if (a.empty() || b.empty() || std::filesystem::file_size(a, ec) != std::filesystem::file_size(b, ec) || ec)
        return false;

    std::ifstream fa(std::filesystem::path(a), std::ios::binary), fb(std::filesystem::path(b), std::ios::binary);
    std::vector<char> ba(1 << 16), bb(1 << 16);

    while (fa && fb)
    {
        fa.read(ba.data(), ba.size());
        fb.read(bb.data(), bb.size());

        if (fa.gcount() != fb.gcount() || memcmp(ba.data(), bb.data(), (size_t) fa.gcount()) != 0)
            return false;
    }

    return true;
}

// ILION: the same dll copied to two folders of one family (e.g. HelixSR and HelixSR\helix 1.8) is listed once; the
// saved choice is the copy kept. Different files keep their own entries
static void DedupeSrVariants(std::vector<FfxApiProxy::SrVariant>& list)
{
    const auto saved = Config::Instance()->Fsr4SrVariant.value_or(L"");

    for (size_t i = 0; i < list.size(); i++)
    {
        for (size_t j = i + 1; j < list.size();)
        {
            if (list[i].family == list[j].family && list[i].kind == list[j].kind && list[i].valid && list[j].valid &&
                SameFileContent(list[i].path, list[j].path))
            {
                if (_wcsicmp(list[j].key.c_str(), saved.c_str()) == 0)
                    std::swap(list[i], list[j]);

                LOG_INFO("Same dll listed once: {} (also {})", wstring_to_string(list[i].path), wstring_to_string(list[j].path));
                list.erase(list.begin() + j);
                continue;
            }

            j++;
        }
    }
}

static void RescanSrVariants()
{
    auto scanned = FfxApiProxy::SrVariants();
    DedupeSrVariants(scanned);
    SharedSrVariants() = scanned;
    SharedDefaultSrDll() = FfxApiProxy::SrVariantDllPath(L"");

    // The default dll is listed once: under its twin's name when it is the same file as one of the list
    SharedDefaultTwin().clear();

    for (const auto& entry : SharedSrVariants())
    {
        if (entry.kind == IlionUpscalers::Kind::Ffx && SameFileContent(entry.path, SharedDefaultSrDll()))
        {
            SharedDefaultTwin() = entry.key;
            break;
        }
    }
}

template <typename T, size_t N> struct RingBuffer
{
    std::array<T, N> data {};
    size_t head { 0 };
    size_t count { N };
    double sum { 0.0 };

    RingBuffer() { data.fill(static_cast<T>(0)); }

    void Push(T v)
    {
        if (count == N)
        {
            sum -= data[head];
        }
        else
        {
            ++count;
        }
        data[head] = v;
        sum += v;
        head = (head + 1) % N;
    }

    size_t Size() const { return N; }

    T At(size_t i) const
    {
        size_t start = head;
        return data[(start + i) % N];
    }

    float Average() const { return static_cast<float>(sum / static_cast<double>(N)); }
};

const int plotWidth = 360;
static RingBuffer<float, plotWidth> gFrameTimes;
static RingBuffer<float, plotWidth> gUpscalerTimes;

struct FsExistsCache
{
    std::wstring lastPath;
    bool cached { false };
    std::chrono::steady_clock::time_point nextRefresh { std::chrono::steady_clock::time_point::min() };
    std::chrono::milliseconds interval { 2000 };

    bool Get(const std::filesystem::path& path)
    {
        auto now = std::chrono::steady_clock::now();
        if (path != lastPath || now >= nextRefresh)
        {
            lastPath = path;
            cached = std::filesystem::exists(path);
            nextRefresh = now + interval;
        }
        return cached;
    }
};

static FsExistsCache nukemsExists;
static FsExistsCache enablerExists;

struct FlagDefinition
{
    std::string name;
    uint32_t mask;
    std::string description;
};

inline std::string StrFmt(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    int len = std::vsnprintf(nullptr, 0, fmt, args);
    va_end(args);
    std::string out(len, '\0');
    va_start(args, fmt);
    std::vsnprintf(out.data(), len + 1, fmt, args);
    va_end(args);
    return out;
}

using Ilion::Tr;
using Ilion::TrL;

// ILION: every tooltip of the menu as a compact box: at most about 32 characters of the menu font wide, the text
// wrapped inside it, so long explanations grow in height instead of crossing the screen. Padding, border, background
// and font are the ones of the theme in use; ImGui keeps the box inside the screen. Line breaks of a text stay.
static float TooltipWrapWidth() { return ImGui::GetFontSize() * 32.0f; }

static void WrappedTooltip(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(TooltipWrapWidth());
    ImGui::TextV(fmt, args);
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
    va_end(args);
}

// ILION Essential mode is on (Basic wins over it): technical and debug controls are left to Advanced
static bool EssentialView()
{
    auto config = Config::Instance();
    return !config->MenuBasicMode.value_or_default() && config->MenuEssentialMode.value_or_default();
}

// --- ILION's upscalers ----------------------------------------------------------------------------------------
// Everything that says which upscaler is selected, loaded or running takes it from here: the selector of the
// Upscaling tab and of Benchmark, the Benchmark labels, the overlay and the status rows. The list comes from
// IlionUpscalers (files on disk), what runs from the proxy (the dll that executed the last upscale) and from d4r.

static const char* FamilyName(IlionUpscalers::Family family)
{
    switch (family)
    {
    case IlionUpscalers::Family::HelixSR:
        return "HelixSR";
    case IlionUpscalers::Family::OtherFfx:
        return "FFX";
    case IlionUpscalers::Family::Dlss4:
        return "DLSS 4";
    default:
        return "FSR 4";
    }
}

// Name of an upscaler of the list as shown: translated name, plus the folder name of a build nobody named
static std::string EntryName(const IlionUpscalers::Entry& entry)
{
    // "FSR 4.1.1 · AMD SDK" carries its version, only the words after it are translated
    std::string name = Tr(entry.display.c_str());

    for (const char* suffix : { " official (AMD)" })
    {
        const auto at = entry.display.find(suffix);
        if (at != std::string::npos && at + strlen(suffix) == entry.display.size())
            name = entry.display.substr(0, at) + Tr(suffix);
    }

    if (!entry.detail.empty())
        name += " " + entry.detail;

    return name;
}

// Name of the upscaler a feature runs, the same in every place of the menu and the overlay. An FFX upscaler on DX12
// is the one of the list the dll in use belongs to (FSR 4 official, FSR 4.1.1B, HelixSR...) with the provider it
// reports; DLSS through d4r is marked as such. Anything else keeps OptiScaler's name and version.
// The FFX upscaler backend of the API in use: FFX in DX12, FFX through OptiScaler's DX12 interop in DX11 and Vulkan
// (where plain FFX is the Vulkan FSR 3.1 dll). Both run the dll ILION switches.
static Upscaler FfxBackendForApi() { return State::Instance().api == DX12 ? Upscaler::FFX : Upscaler::FFX_on12; }

// ILION: the FFX index of the list a backend uses. Vulkan native FFX has its own list and index (UpscalerIndexVk), so
// an index picked there never selects a provider of the DX12 list the Vulkan to DX12 interop uses, nor the other way
static CustomOptional<int>& FfxIndexFor(Upscaler upscaler, API api)
{
    auto config = Config::Instance();
    return upscaler == Upscaler::FFX && api == API::Vulkan ? config->FfxUpscalerIndexVk : config->FfxUpscalerIndex;
}

static bool RunsSwitchedFfx(Upscaler type)
{
    return (type == Upscaler::FFX && State::Instance().api == DX12) || type == Upscaler::FFX_on12;
}

static ImVec4 toneMapColor(const ImVec4& color);

// ILION: native Vulkan FFX providers of the Vulkan list. direct: ILION's own provider of AMD's amdvkffx64; sdkFsr4: an
// FSR 4 amd_fidelityfx_vk.dll lists (handing the upscale to the driver); sdkFirst: the first one it lists itself
struct VulkanNativeFfx
{
    int direct = -1;
    int sdkFsr4 = -1;
    int sdkFirst = -1;
};

static VulkanNativeFfx FindVulkanNativeFfx()
{
    VulkanNativeFfx found;

    if (State::Instance().api != Vulkan)
        return found;

    FFXFeature::EnsureVersionsVulkan();
    const auto& vk = State::Instance().ffxUpscalerVersionsVk;

    for (size_t i = 0; i < vk.ids.size() && i < vk.names.size(); i++)
    {
        if (FSR4DriverProviderVk::IsAvailable() && vk.ids[i] == FSR4DriverProviderVk::VersionId())
        {
            found.direct = (int) i;
            continue;
        }

        if (found.sdkFirst < 0)
            found.sdkFirst = (int) i;

        if (found.sdkFsr4 < 0 && vk.names[i] != nullptr && vk.names[i][0] == '4')
            found.sdkFsr4 = (int) i;
    }

    return found;
}

// ILION: the backend chosen for FFX in Vulkan games (Advanced): automatic, native Vulkan only, DX12 interop
enum class VulkanBackend
{
    Automatic = 0,
    Native = 1,
    Dx12Interop = 2,
};

static VulkanBackend VulkanBackendChoice()
{
    const int value = Config::Instance()->IlionVulkanBackend.value_or_default();
    return value == 1 ? VulkanBackend::Native : value == 2 ? VulkanBackend::Dx12Interop : VulkanBackend::Automatic;
}

// ILION: in a Vulkan game, the native Vulkan index an entry of the selector runs with; -1 when it runs through the DX12
// interop. AMD's installed driver runs its Vulkan provider (amdvkffx64, the same driver), the default dll runs
// amd_fidelityfx_vk.dll; both unless DX12 interop is chosen. Dlls that only exist for DX12 (community builds, Lite,
// AMD's SDK dll, a copied driver dll) always need the interop.
static int NativeVulkanIndexFor(const IlionUpscalers::Entry* entry)
{
    if (State::Instance().api != Vulkan || VulkanBackendChoice() == VulkanBackend::Dx12Interop)
        return -1;

    const auto native = FindVulkanNativeFfx();

    // The default dll: an FSR 4 that amd_fidelityfx_vk.dll lists, else the driver's Vulkan FSR 4; without either it
    // keeps the DX12 interop with the default FSR 4 dll instead of falling back to the FSR 3.1 of amd_fidelityfx_vk.dll
    if (entry == nullptr)
        return native.sdkFsr4 >= 0 ? native.sdkFsr4 : native.direct;

    if (entry->kind == IlionUpscalers::Kind::Provider && entry->key == L"FSR4/AMD driver")
        return native.direct >= 0 ? native.direct : native.sdkFsr4;

    return -1;
}

// The upscaler setting of the API in use
static Upscaler ConfiguredUpscaler()
{
    auto config = Config::Instance();

    switch (State::Instance().api)
    {
    case DX11:
        return config->Dx11Upscaler.value_or_default();
    case DX12:
        return config->Dx12Upscaler.value_or_default();
    default:
        return config->VulkanUpscaler.value_or_default();
    }
}

static void SetConfiguredUpscaler(Upscaler upscaler)
{
    auto config = Config::Instance();

    switch (State::Instance().api)
    {
    case DX11:
        config->Dx11Upscaler = upscaler;
        break;
    case DX12:
        config->Dx12Upscaler = upscaler;
        break;
    default:
        config->VulkanUpscaler = upscaler;
        break;
    }
}

// ILION: the entry of the list the FFX dll in use belongs to (or the default dll's twin), looked up again only when
// that dll, the twin key or the scan of the folders changes; the label is drawn on every frame
static std::optional<IlionUpscalers::Entry> ActiveSrEntry(const std::wstring& srPath)
{
    static std::wstring cachedPath, cachedTwin;
    static uint64_t cachedGeneration = 0;
    static bool cachedValid = false;
    static std::optional<IlionUpscalers::Entry> cachedEntry;

    const auto generation = IlionUpscalers::Generation();
    const auto& twin = SharedDefaultTwin();

    if (!cachedValid || generation != cachedGeneration || srPath != cachedPath || twin != cachedTwin)
    {
        cachedEntry = IlionUpscalers::FindByPath(srPath);

        if (!cachedEntry.has_value() && IlionUpscalers::SamePath(srPath, SharedDefaultSrDll()))
            cachedEntry = IlionUpscalers::FindByKey(twin);

        // The lookup may be the one that scanned the folders first
        cachedGeneration = IlionUpscalers::Generation();
        cachedPath = srPath;
        cachedTwin = twin;
        cachedValid = true;
    }

    return cachedEntry;
}

static std::string UpscalerLabel(IFeature* feature)
{
    if (feature == nullptr)
        return Tr("None");

    const auto version = StrFmt("%u.%u.%u", feature->Version().major, feature->Version().minor, feature->Version().patch);
    const auto type = feature->GetUpscalerType();

    // ILION: native Vulkan FFX running FSR 4 is the AMD Vulkan driver's: directly (ILION's provider) or handed over by
    // amd_fidelityfx_vk.dll
    if (type == Upscaler::FFX && State::Instance().api == Vulkan && feature->Version().major >= 4)
        return "FSR " + version + (FSR4DriverProviderVk::InUse() ? " · VK" : " · VK (SDK)");

    if (RunsSwitchedFfx(type))
    {
        // Short names everywhere; the full provider string stays in the log and the diagnostics
        const auto entry = ActiveSrEntry(FfxApiProxy::Dx12Module_SR_Path());

        // An AMD driver dll chosen in the selector: what runs is its FSR, with the version its provider created
        if (entry.has_value() && entry->kind == IlionUpscalers::Kind::Provider)
            return "FSR " + version + " · Driver";

        // HelixSR: "DLSS 3.7" is what its author says it runs. Shown only when the dll identified as HelixSR by its
        // file is the one the last upscale really executed (SR-DIAG); the version itself can't be checked from here
        if (entry.has_value() && entry->family == IlionUpscalers::Family::HelixSR)
        {
            const auto active = FfxApiProxy::SrDiagGetActive();
            std::wstring activePath;

            if (active.module != nullptr)
                FfxApiProxy::SrDiagDescribeModule(active.module, &activePath);

            if (IlionUpscalers::SamePath(activePath, entry->path))
                return EntryName(*entry) + " (DLSS 3.7)";
        }

        // An AMD dll can hand the upscale to the AMD driver's provider: the provider picked in its FFX list is an
        // external one (bit 31 of its id, as OptiScaler marks driver providers). What runs is then the driver's FSR,
        // through that dll; the version is the one the provider created
        if (State::Instance().api == DX12 || ConfiguredUpscaler() == Upscaler::FFX_on12)
        {
            const auto& versions = State::Instance().ffxUpscalerVersionsDx12;
            const int index = Config::Instance()->FfxUpscalerIndex.value_or_default();

            if (index >= 0 && index < (int) versions.ids.size() && (versions.ids[index] & 0x80000000ull) != 0 &&
                feature->Version().major >= 4)
                return "FSR " + version + " · Driver (" + Tr("via SDK") + ")";
        }

        // The default dll, or the game's own: AMD's FSR
        return entry.has_value() ? EntryName(*entry) : "FSR " + version;
    }

    if (type == Upscaler::DLSS && State::Instance().dlssExternalBackend)
        return "DLSS " + version + " (d4r)";

    return feature->ShortName() + " " + version;
}

struct UpscalerStatus
{
    bool selected = false; // the choice saved in the config
    bool loaded = false;   // FFX: the upscaler functions point to its dll. d4r: OptiScaler loaded it at start
    bool active = false;   // FFX: its dll executed the last upscale. d4r: NGX accepted its evaluations
};

static UpscalerStatus StatusOf(const IlionUpscalers::Entry* entry, IFeature* feature)
{
    auto config = Config::Instance();
    const auto& state = State::Instance();
    const bool dlssChosen = config->Dx12Upscaler.value_or_default() == Upscaler::DLSS &&
                            config->DLSSAllowExternalBackend.value_or_default();
    UpscalerStatus status;

    if (entry == nullptr || IlionUpscalers::SwitchesAsFfx(*entry))
    {
        const std::wstring key = entry != nullptr ? entry->key : L"";
        const std::wstring path = entry != nullptr ? entry->path : SharedDefaultSrDll();
        const auto saved = config->Fsr4SrVariant.value_or(L"");

        // ILION: running native Vulkan (AMD's driver or the default dll): its state is the Vulkan feature's
        if (const int native = NativeVulkanIndexFor(entry); native >= 0 && ConfiguredUpscaler() == Upscaler::FFX)
        {
            status.selected = !dlssChosen && _wcsicmp(saved.c_str(), key.c_str()) == 0 &&
                              config->FfxUpscalerIndexVk.value_or_default() == native;
            status.loaded = status.selected && feature != nullptr && feature->GetUpscalerType() == Upscaler::FFX &&
                            feature->IsInited();
            status.active = status.loaded && feature->FrameCount() > 0;
            return status;
        }

        // Outside DX12 the saved dll only counts while FFX through the interop is the chosen upscaler.
        // ILION: native DLSS chosen (NVIDIA) isn't the saved FFX dll either
        const bool nativeDlssChosen = ConfiguredUpscaler() == Upscaler::DLSS && !config->DLSSAllowExternalBackend.value_or_default();
        status.selected = !dlssChosen && !nativeDlssChosen && _wcsicmp(saved.c_str(), key.c_str()) == 0 &&
                          (State::Instance().api == DX12 || ConfiguredUpscaler() == Upscaler::FFX_on12);
        status.loaded = !path.empty() && FfxApiProxy::IsActiveSrDll(path);

        if (status.loaded && feature != nullptr && RunsSwitchedFfx(feature->GetUpscalerType()))
        {
            const auto active = FfxApiProxy::SrDiagGetActive();
            std::wstring activePath;

            if (active.module != nullptr)
                FfxApiProxy::SrDiagDescribeModule(active.module, &activePath);

            status.active = IlionUpscalers::SamePath(activePath, path);

            // An FSR 4 dll also offers FSR 3.1: it is FSR 4 that runs only when the provider created is a 4.x one
            if (entry == nullptr || entry->family == IlionUpscalers::Family::Fsr4)
                status.active = status.active && feature->Version().major >= 4;
        }

        return status;
    }

    const auto nvngx = config->NvngxPath.value_or(L"");
    const bool here = IlionUpscalers::SamePath(nvngx, entry->path);
    const auto& calls = IlionD4r::GetCalls();

    status.selected = dlssChosen && here;
    status.loaded = state.dlssExternalBackend && here;
    status.active = status.loaded && feature != nullptr && feature->GetUpscalerType() == Upscaler::DLSS &&
                    calls.evaluateOk.load() > 0 && calls.lastResult.load() == 1;

    return status;
}

static const char* StatusWord(const UpscalerStatus& status)
{
    if (status.active)
        return "active";
    if (status.loaded)
        return "loaded";
    if (status.selected)
        return "selected";
    return "";
}

// Picks an upscaler of the list (nullptr: the default FFX dll). FFX dlls switch while the game runs, through the
// proxy's hot swap. DLSS 4 through d4r switches while the game runs only when OptiScaler loaded d4r at start;
// otherwise it is saved and loaded at the next start, which is what the d4r backend needs (it hooks the D3D12 device
// when the game creates it). The choice is written to the ini at once.
// DLSS 4 through d4r on a GPU that isn't (or can't be told to be) AMD RDNA 3: asked once before it is chosen.
// Continue chooses it as usual, Cancel leaves the upscaler as it is. Nothing is blocked.
static std::wstring d4rWarningKey;
static bool d4rWarningOpen = false;

static bool GameGpuIsRdna3()
{
    const auto& gpu = IdentifyGpu::getPrimaryGpu();
    return gpu.vendorId == VendorId::AMD && gpu.amdHwGeneration == device_info::HwGeneration::kGfx11;
}

static void SelectUpscaler(const IlionUpscalers::Entry* entry)
{
    auto config = Config::Instance();
    auto& state = State::Instance();

    if (FfxApiProxy::SrSwitchLocked())
        return;

    if (entry == nullptr || IlionUpscalers::SwitchesAsFfx(*entry))
    {
        const std::wstring key = entry != nullptr ? entry->key : L"";
        const int native = NativeVulkanIndexFor(entry);

        // Native Vulkan only: a dll that needs the DX12 interop isn't started (the list shows why)
        if (native < 0 && state.api == Vulkan && VulkanBackendChoice() == VulkanBackend::Native)
            return;

        if (key.empty())
            config->Fsr4SrVariant.reset();
        else
            config->Fsr4SrVariant = key;

        // ILION: AMD's driver or the default dll in a Vulkan game: native Vulkan FFX with that provider of the
        // Vulkan list. The dll of the DX12 interop isn't touched; choosing a DX12 dll later uses the interop again
        if (native >= 0)
        {
            if (config->DLSSAllowExternalBackend.value_or_default())
                config->DLSSAllowExternalBackend = false;

            config->FfxUpscalerIndexVk = native;
            FfxApiProxy::SetSrIndexManual(false);
            SetConfiguredUpscaler(Upscaler::FFX);
            state.newBackend = Upscaler::FFX;
            MARK_ALL_BACKENDS_CHANGED();
            config->SaveUpscalerChoice();
            return;
        }

        // d4r is only loaded at start when DLSS 4 is the chosen upscaler; this session keeps what it loaded
        if (config->DLSSAllowExternalBackend.value_or_default())
            config->DLSSAllowExternalBackend = false;

        // The dll's own first provider (FSR 4, or HelixSR itself), also when FSR 3.1 of the same dll ran before.
        // ILION: saved too, so an index of another provider isn't read back from the ini at the next start; the index
        // is resolved again for the new dll (its own provider, not the AMD driver's it also lists)
        config->FfxUpscalerIndex = 0;
        FfxApiProxy::SetSrIndexManual(false);

        SetConfiguredUpscaler(FfxBackendForApi());
        FfxApiProxy::RequestSrVariant(key);
        state.newBackend = FfxBackendForApi();
        MARK_ALL_BACKENDS_CHANGED();
        config->SaveUpscalerChoice();
        return;
    }

    const bool loaded = state.dlssExternalBackend && IlionUpscalers::SamePath(config->NvngxPath.value_or(L""), entry->path);

    config->DLSSAllowExternalBackend = true;
    config->NvngxPath = entry->path;
    config->NVNGX_DLSS_Library = entry->path;
    config->DLSSFeaturePath = entry->path;
    config->Dx12Upscaler = Upscaler::DLSS;
    config->SaveUpscalerChoice();

    if (loaded)
    {
        state.newBackend = Upscaler::DLSS;
        MARK_ALL_BACKENDS_CHANGED();
        return;
    }

    ImGui::InsertNotification({ ImGuiToastType::Info, 12000,
                                Tr("DLSS 4 is saved as the upscaler. Restart the game to load it (d4r is loaded when the "
                                   "game starts).") });
}

// Tooltip of the item just drawn, as MenuCommon::ShowTooltip does it
static void UpscalerTip(const char* tip)
{
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        WrappedTooltip("%s", Tr(tip));
}

// Picks an upscaler OptiScaler has built in (FSR 2.2.1, FSR 2.1.2, XeSS): its own backend switch, as the backend
// list does it. d4r stops being loaded at the next start, as with any choice that isn't DLSS 4.
static void SelectBackend(Upscaler upscaler)
{
    auto config = Config::Instance();

    if (FfxApiProxy::SrSwitchLocked())
        return;

    if (config->DLSSAllowExternalBackend.value_or_default())
        config->DLSSAllowExternalBackend = false;

    SetConfiguredUpscaler(upscaler);
    State::Instance().newBackend = upscaler;
    MARK_ALL_BACKENDS_CHANGED();
    config->SaveUpscalerChoice();
}

static UpscalerStatus BackendStatus(Upscaler upscaler, IFeature* feature)
{
    UpscalerStatus status;
    // ILION: DLSS saved with AllowExternalBackend is d4r's entry, not native DLSS
    status.selected = ConfiguredUpscaler() == upscaler &&
                      !(upscaler == Upscaler::DLSS && Config::Instance()->DLSSAllowExternalBackend.value_or_default());
    status.loaded = feature != nullptr && feature->GetUpscalerType() == upscaler;
    status.active = status.loaded && feature->FrameCount() > 0;
    return status;
}

// FSR 3.1 in DX12: a provider of the FFX dll in use, picked by its index, as OptiScaler's FFX upscaler list does it
static void SelectFfxProvider(int index)
{
    auto config = Config::Instance();

    if (FfxApiProxy::SrSwitchLocked())
        return;

    if (config->DLSSAllowExternalBackend.value_or_default())
        config->DLSSAllowExternalBackend = false;

    FfxIndexFor(Upscaler::FFX, State::Instance().api) = index;

    // A provider picked by its index (FSR 3.1 of the dll in use) is kept as picked
    if (State::Instance().api != Vulkan)
        FfxApiProxy::SetSrIndexManual(true);

    SetConfiguredUpscaler(Upscaler::FFX);
    State::Instance().newBackend = Upscaler::FFX;
    MARK_ALL_BACKENDS_CHANGED();
    config->SaveUpscalerChoice();
}

static UpscalerStatus FfxProviderStatus(int index, IFeature* feature)
{
    UpscalerStatus status;
    status.selected = ConfiguredUpscaler() == Upscaler::FFX && FfxIndexFor(Upscaler::FFX, State::Instance().api).value_or_default() == index;
    status.loaded = feature != nullptr && feature->GetUpscalerType() == Upscaler::FFX && feature->Version().major == 3;
    status.active = status.loaded && feature->FrameCount() > 0;
    return status;
}

// ILION: an FSR 4 of the Vulkan native FFX list picked by its index: the AMD Vulkan driver's through ILION's provider
// (direct) or as amd_fidelityfx_vk.dll hands it over
static UpscalerStatus VkFsr4Status(int index, bool direct, IFeature* feature)
{
    UpscalerStatus status;
    const bool indexChosen = Config::Instance()->FfxUpscalerIndexVk.value_or_default() == index;
    status.selected = ConfiguredUpscaler() == Upscaler::FFX && indexChosen;
    status.loaded = indexChosen && feature != nullptr && feature->GetUpscalerType() == Upscaler::FFX &&
                    feature->Version().major >= 4 && FSR4DriverProviderVk::InUse() == direct;
    status.active = status.loaded && feature->FrameCount() > 0;
    return status;
}

// ILION: the upscaling backend of FFX in a Vulkan game (Advanced). Changing it applies the FFX upscaler chosen in the
// selector again with the new backend, while the game runs; with native Vulkan only, a dll that needs the DX12 interop
// gives way to AMD's driver (or the default dll)
static void RenderVulkanBackendChoice(float scale)
{
    auto config = Config::Instance();
    const char* names[] = { Tr("Automatic (recommended)"), Tr("Native Vulkan"), Tr("DX12 interop") };
    const int choice = std::clamp(config->IlionVulkanBackend.value_or_default(), 0, 2);

    ImGui::SetNextItemWidth(std::min(ImGui::GetContentRegionAvail().x * 0.6f,
                                     std::max(150.0f * scale, ImGui::CalcTextSize(names[0]).x + ImGui::GetFrameHeight() * 2.0f)));

    if (IlionTheme::BeginCombo(TrL("Upscaling backend"), names[choice]))
    {
        for (int i = 0; i < 3; i++)
        {
            if (ImGui::Selectable(names[i], choice == i) && choice != i && !FfxApiProxy::SrSwitchLocked())
            {
                config->IlionVulkanBackend = i;
                config->SaveUpscalerChoice();

                const auto current = ConfiguredUpscaler();
                if (current == Upscaler::FFX || current == Upscaler::FFX_on12)
                {
                    const auto saved = config->Fsr4SrVariant.value_or(L"");
                    auto entry = saved.empty() ? std::nullopt : IlionUpscalers::FindByKey(saved);

                    if (VulkanBackendChoice() == VulkanBackend::Native && NativeVulkanIndexFor(entry ? &*entry : nullptr) < 0)
                        entry = IlionUpscalers::FindByKey(L"FSR4/AMD driver");

                    SelectUpscaler(entry ? &*entry : nullptr);
                }
            }
        }

        ImGui::EndCombo();
    }

    UpscalerTip("Automatic: native Vulkan where the implementation has it (AMD's driver, the default dll), the DX12 interop "
                "for dlls that need it (community builds, Lite, AMD's SDK dll).\n"
                "Native Vulkan: only implementations that run natively.\n"
                "DX12 interop: every FFX dll through OptiScaler's DX12 interop (w/DX12).");
}

// The selector: every upscaler found, what is selected, loaded and active, and why one can't be used
// ILION layout helpers, defined next to SectionTitle and used from here on
static float ButtonWidth(const char* label);
static float CheckboxWidth(const char* label);
static void SameLineIfRoom(float nextWidth, float spacing = -1.0f);
static void TextWrappedDisabled(const char* fmt, ...);
static void TextWrappedColored(const ImVec4& colour, const char* fmt, ...);

static void DrawUpscalerSelector(IFeature* feature, float scale, const char* id)
{
    ImGui::PushID(id);

    auto& entries = SharedSrVariants();
    const auto& defaultDll = SharedDefaultSrDll();
    const bool showDefault = !defaultDll.empty() && SharedDefaultTwin().empty();
    const bool locked = FfxApiProxy::SrSwitchLocked();

    // Families in their order; the FFX ones and DLSS (native DLSS, DLSS 4 through d4r) only when something of them is there
    enum FamilyId { FamFsr4, FamFsr31, FamFsr2, FamXess, FamDlss4, FamHelix, FamFfx, FamCount };
    static const char* familyNames[FamCount] = { "FSR 4", "FSR 3.1", "FSR 2", "XeSS", "DLSS", "HelixSR", "FFX" };

    const auto api = State::Instance().api;

    // Vendor of the GPU, read once: FSR 4 needs AMD's driver, d4r runs DLSS on AMD GPUs, DX11 XeSS needs Intel
    static const auto vendor = IdentifyGpu::getPrimaryGpu().vendorId;

    const auto familyOf = [](const IlionUpscalers::Entry& entry)
    {
        switch (entry.family)
        {
        case IlionUpscalers::Family::HelixSR:
            return FamHelix;
        case IlionUpscalers::Family::OtherFfx:
            return FamFfx;
        case IlionUpscalers::Family::Dlss4:
            return FamDlss4;
        default:
            return FamFsr4;
        }
    };

    bool present[FamCount] = { showDefault, true, true, true, false, false, false };

    // FSR 3.1 in DX12 is a provider of the FFX dll in use; in Vulkan FSR 3.1 (amd_fidelityfx_vk.dll) and the AMD
    // Vulkan driver's FSR 4 are providers of the Vulkan FFX list
    int fsr31Index = -1;

    struct VkFsr4
    {
        int index;
        bool direct; // ILION's own provider of amdvkffx64; else as amd_fidelityfx_vk.dll lists it
        std::string name;
    };

    std::vector<VkFsr4> vkFsr4;

    if (api == Vulkan)
    {
        FFXFeature::EnsureVersionsVulkan();
        const auto& vk = State::Instance().ffxUpscalerVersionsVk;

        for (size_t i = 0; i < vk.ids.size() && i < vk.names.size(); i++)
        {
            if (vk.names[i] == nullptr)
                continue;

            // "4.1.2 *" as the driver names it: the number only, the full name stays in the tooltip
            const std::string full = vk.names[i];
            const std::string number = full.substr(0, full.find(' '));
            const bool direct = FSR4DriverProviderVk::IsAvailable() && vk.ids[i] == FSR4DriverProviderVk::VersionId();

            if (direct || number.rfind("4", 0) == 0)
                vkFsr4.push_back({ (int) i, direct, "FSR " + number + (direct ? " · VK" : " · VK (SDK)") });
            else if (fsr31Index < 0 && strncmp(vk.names[i], "3.1", 3) == 0)
                fsr31Index = (int) i;
        }
    }

    if (api == DX12)
    {
        FFXFeature::EnsureVersionsDx12();
        const auto& names = State::Instance().ffxUpscalerVersionsDx12.names;

        for (size_t i = 0; i < names.size() && fsr31Index < 0; i++)
        {
            if (names[i] != nullptr && strncmp(names[i], "3.1", 3) == 0)
                fsr31Index = (int) i;
        }
    }

    // The backends OptiScaler has for FSR 3.1, FSR 2 and XeSS in each API, with the names shown for them
    struct BuiltIn
    {
        int family;
        Upscaler backend;
        const char* name;
        const char* reason; // set when it can't be used here
    };

    std::vector<BuiltIn> builtIns;

    if (api == DX12)
    {
        builtIns = { { FamFsr2, Upscaler::FSR22, "FSR 2.2.1", nullptr },
                     { FamFsr2, Upscaler::FSR21, "FSR 2.1.2", nullptr },
                     { FamXess, Upscaler::XeSS, "XeSS", nullptr } };
    }
    else if (api == DX11)
    {
        builtIns = { { FamFsr31, Upscaler::FSR31, "FSR 3.1", nullptr },
                     { FamFsr2, Upscaler::FSR22, "FSR 2.2.1", nullptr },
                     { FamFsr2, Upscaler::FSR21_on12, "FSR 2.1.2 (DX12 interop)", nullptr },
                     { FamXess, Upscaler::XeSS, "XeSS",
                       vendor == VendorId::Intel ? nullptr
                                                 : "Hardware not compatible: XeSS in DirectX 11 runs natively only on Intel GPUs" },
                     { FamXess, Upscaler::XeSS_on12, "XeSS (DX12 interop)", nullptr } };
    }
    else
    {
        // FSR 3.1 is listed as a provider of the Vulkan FFX list below, by its index
        builtIns = { { FamFsr2, Upscaler::FSR22, "FSR 2.2.1", nullptr },
                     { FamFsr2, Upscaler::FSR21, "FSR 2.1.2", nullptr },
                     { FamXess, Upscaler::XeSS, "XeSS", nullptr } };
    }

    // ILION: native DLSS, where OptiScaler's own backend list offers it (a DLSS capable GPU, not while d4r runs)
    if (IdentifyGpu::getPrimaryGpu().dlssCapable && !State::Instance().dlssExternalBackend)
    {
        builtIns.push_back({ FamDlss4, Upscaler::DLSS, "DLSS", nullptr });

        if (api != DX12)
            builtIns.push_back({ FamDlss4, Upscaler::DLSS_on12, "DLSS (DX12 interop)", nullptr });
    }

    for (const auto& builtIn : builtIns)
        present[builtIn.family] = true;

    for (const auto& entry : entries)
        present[familyOf(entry)] = true;

    if (!vkFsr4.empty())
        present[FamFsr4] = true;

    // Family of what runs (or is chosen): the variant list opens on it
    int current = -1, rank = 0;
    const auto consider = [&](int family, const UpscalerStatus& status)
    {
        const int value = status.active ? 3 : status.loaded ? 2 : status.selected ? 1 : 0;
        if (value > rank)
        {
            rank = value;
            current = family;
        }
    };

    if (!defaultDll.empty())
        consider(FamFsr4, StatusOf(nullptr, feature));

    for (const auto& entry : entries)
        consider(familyOf(entry), StatusOf(&entry, feature));

    for (const auto& builtIn : builtIns)
        consider(builtIn.family, BackendStatus(builtIn.backend, feature));

    if (fsr31Index >= 0)
        consider(FamFsr31, FfxProviderStatus(fsr31Index, feature));

    for (const auto& vk : vkFsr4)
    {
        if (std::none_of(entries.begin(), entries.end(), [](const IlionUpscalers::Entry& e) { return e.key == L"FSR4/AMD driver"; }))
            consider(FamFsr4, VkFsr4Status(vk.index, vk.direct, feature));
    }

    static int shown[2] = { -1, -1 };
    int& family = shown[strcmp(id, "benchmark") == 0 ? 1 : 0];

    if (family < 0 || family >= FamCount || !present[family])
        family = current >= 0 ? current : FamFsr4;

    ImGui::BeginDisabled(locked);
    ImGui::PushItemWidth(110.0f * scale);

    if (IlionTheme::BeginCombo(TrL("Upscaler##IlionFamily"), familyNames[family]))
    {
        for (int i = 0; i < FamCount; i++)
        {
            if (present[i] && ImGui::Selectable(familyNames[i], family == i))
                family = i;
        }

        ImGui::EndCombo();
    }

    ImGui::PopItemWidth();
    SameLineIfRoom(std::min(190.0f * scale, ImGui::GetContentRegionAvail().x));

    // Variants of the family shown, with their state; what runs is the preview
    struct Variant
    {
        const IlionUpscalers::Entry* entry = nullptr; // FFX or d4r upscaler of the list
        Upscaler backend = Upscaler::Reset;            // or a backend OptiScaler has built in
        bool isDefault = false;
        std::string name;
        UpscalerStatus status;
        bool usable = true;
        std::string reason;  // why it can't be used: API, hardware, dll or dependency
        int ffxIndex = -1;   // FSR 3.1 as a provider of the FFX dll in use (DX12), or a Vulkan FFX provider
        bool vkDriver = false; // an FSR 4 of the Vulkan native list (the AMD Vulkan driver's)
        bool vkDirect = false; // through ILION's own provider of amdvkffx64, not handed over by amd_fidelityfx_vk.dll
    };

    // Why an upscaler of the list can't be used here, in the words of the four reasons
    const auto entryReason = [&](const IlionUpscalers::Entry& entry) -> std::string
    {
        if (entry.kind == IlionUpscalers::Kind::D4r)
        {
            if (api != DX12)
                return std::string(Tr("API not compatible")) + ": " + Tr("d4r only works in DirectX 12");
            if (!entry.valid)
                return std::string(Tr("Dependency not available")) + ": " + Tr(entry.problem.c_str());
            return "";
        }

        // ILION: native Vulkan only (Advanced): a dll that needs the DX12 interop can't run
        if (api == Vulkan && VulkanBackendChoice() == VulkanBackend::Native && IlionUpscalers::SwitchesAsFfx(entry) &&
            NativeVulkanIndexFor(&entry) < 0)
            return std::string(Tr("API not compatible")) + ": " +
                   Tr("it runs only through the DX12 interop, the upscaling backend is native Vulkan");

        // ILION: AMD's driver provider runs the AMD driver's own code: only on an AMD GPU (an NVIDIA PC with an AMD iGPU
        // also has this driver installed)
        if (entry.kind == IlionUpscalers::Kind::Provider && vendor != VendorId::AMD && vendor != VendorId::Invalid)
            return std::string(Tr("Hardware not compatible")) + ": " + Tr("FSR 4 through the AMD driver needs an AMD GPU");

        // Only AMD's own FSR 4 dll keeps AMD's hardware rule, unless an INT8 model is forced (Fsr4ForceModel), which is
        // how OptiScaler runs it on other GPUs; community builds decide by themselves whether they run
        if (entry.family == IlionUpscalers::Family::Fsr4 && entry.amdOfficial && vendor != VendorId::AMD &&
            !IsFsr4Int8(Config::Instance()->Fsr4ForceModel.value_or_default()))
            return std::string(Tr("Hardware not compatible")) + ": " +
                   Tr("AMD's official FSR 4 needs an AMD GPU, or an INT8 model forced with Fsr4ForceModel");

        if (!entry.valid)
            return std::string(Tr("DLL not available")) + ": " + Tr(entry.problem.c_str());

        return "";
    };

    std::vector<Variant> variants;

    if (family == FamFsr4 && showDefault)
    {
        // Usable on any GPU: it also provides FSR 3.1, and FSR 4 runs only where the dll itself allows it
        Variant variant { nullptr, Upscaler::Reset, true, Tr("FSR (default dll)"), StatusOf(nullptr, feature) };

        if (api == Vulkan && VulkanBackendChoice() == VulkanBackend::Native && NativeVulkanIndexFor(nullptr) < 0)
        {
            variant.usable = false;
            variant.reason = std::string(Tr("DLL not available")) + ": " + Tr("no native Vulkan FSR 4 provider was found");
        }

        variants.push_back(variant);
    }

    for (const auto& entry : entries)
    {
        if (familyOf(entry) != family)
            continue;

        auto status = StatusOf(&entry, feature);

        // The default dll is this file: its state is this entry's
        if (entry.key == SharedDefaultTwin())
        {
            const auto twin = StatusOf(nullptr, feature);
            status.loaded = status.loaded || twin.loaded;
            status.active = status.active || twin.active;
            status.selected = status.selected || twin.selected;
        }

        const auto reason = entryReason(entry);
        variants.push_back({ &entry, Upscaler::Reset, false, EntryName(entry), status, reason.empty(), reason });
    }

    for (const auto& builtIn : builtIns)
    {
        if (builtIn.family != family)
            continue;

        Variant variant { nullptr, builtIn.backend, false, Tr(builtIn.name), BackendStatus(builtIn.backend, feature) };

        if (builtIn.reason != nullptr)
        {
            variant.usable = false;
            variant.reason = Tr(builtIn.reason);
        }

        variants.push_back(variant);
    }

    // With AMD's installed driver in the list, its entry runs the Vulkan provider itself (one entry per driver)
    const bool driverListed = std::any_of(entries.begin(), entries.end(),
                                          [](const IlionUpscalers::Entry& e) { return e.key == L"FSR4/AMD driver"; });

    for (const auto& vk : vkFsr4)
    {
        if (family != FamFsr4 || driverListed)
            break;

        Variant variant { nullptr, Upscaler::Reset, false, vk.name, VkFsr4Status(vk.index, vk.direct, feature) };
        variant.ffxIndex = vk.index;
        variant.vkDriver = true;
        variant.vkDirect = vk.direct;
        variants.push_back(variant);
    }

    if (family == FamFsr31 && (api == DX12 || api == Vulkan))
    {
        Variant variant { nullptr, Upscaler::Reset, false, "FSR 3.1", FfxProviderStatus(fsr31Index, feature) };
        variant.ffxIndex = fsr31Index;

        if (fsr31Index < 0)
        {
            variant.usable = false;
            variant.reason = std::string(Tr("DLL not available")) + ": " + Tr("the FFX dll in use lists no FSR 3.1 provider");
        }

        variants.push_back(variant);
    }

    std::string preview = "-";
    int previewRank = 0;

    for (const auto& variant : variants)
    {
        const int value = variant.status.active ? 3 : variant.status.loaded ? 2 : variant.status.selected ? 1 : 0;
        if (value > previewRank)
        {
            previewRank = value;
            preview = variant.name;
        }
    }

    // ILION: wide enough for the name chosen (its version stays readable), never wider than the room left
    const float previewNeeded = ImGui::CalcTextSize(preview.c_str()).x + ImGui::GetFrameHeight() + ImGui::GetStyle().FramePadding.x * 3.0f;
    ImGui::PushItemWidth(std::max(ImGui::GetFrameHeight() * 4.0f,
                                  std::min(std::max(190.0f * scale, previewNeeded), ImGui::GetContentRegionAvail().x)));

    if (IlionTheme::BeginCombo(TrL("##IlionVariant"), preview.c_str()))
    {
        for (const auto& variant : variants)
        {
            const char* word = StatusWord(variant.status);
            auto label = variant.name;

            if (*word != 0)
                label += StrFmt("  [%s]", Tr(word));

            label += "###" + (variant.entry != nullptr ? wstring_to_string(variant.entry->key)
                                                       : variant.isDefault ? std::string("default") : variant.name);

            ImGui::BeginDisabled(!variant.usable);

            if (ImGui::Selectable(label.c_str(), variant.status.selected || variant.status.active) &&
                !(variant.status.selected && variant.status.loaded))
            {
                if (variant.ffxIndex >= 0)
                    SelectFfxProvider(variant.ffxIndex);
                else if (variant.backend != Upscaler::Reset)
                    SelectBackend(variant.backend);
                else if (variant.entry != nullptr && variant.entry->kind == IlionUpscalers::Kind::D4r && !GameGpuIsRdna3())
                {
                    d4rWarningKey = variant.entry->key;
                    d4rWarningOpen = true;
                }
                else
                    SelectUpscaler(variant.isDefault ? nullptr : variant.entry);
            }

            ImGui::EndDisabled();

            if (!ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                continue;

            if (!variant.usable)
            {
                WrappedTooltip("%s", variant.reason.c_str());
                continue;
            }

            if (variant.vkDriver && variant.vkDirect)
            {
                WrappedTooltip("%s\nFSR %s\n%s", Tr("FSR 4 of AMD's Vulkan driver, native Vulkan. Switches while the game runs; its "
                                                    "network is chosen with the FSR 4 model."),
                               FSR4DriverProviderVk::VersionName(), wstring_to_string(FSR4DriverProviderVk::Path()).c_str());
                continue;
            }

            if (variant.vkDriver)
            {
                WrappedTooltip("%s", Tr("FSR 4 of AMD's Vulkan driver, native Vulkan, as amd_fidelityfx_vk.dll lists it and hands "
                                        "the upscale over. Switches while the game runs."));
                continue;
            }

            if (variant.backend != Upscaler::Reset || variant.ffxIndex >= 0)
            {
                WrappedTooltip("%s", Tr("Built into OptiScaler, switches while the game runs."));
                continue;
            }

            if (variant.entry == nullptr)
            {
                WrappedTooltip("%s", Tr("AMD's FSR dll next to OptiScaler, or the game's own."));
                continue;
            }

            const auto& entry = *variant.entry;
            std::string tip = StrFmt("%s: %s\n%s", Tr("Technology"), FamilyName(entry.family), wstring_to_string(entry.path).c_str());

            if (!entry.fileVersion.empty())
                tip += StrFmt("\n%s: %s", Tr("File version"), entry.fileVersion.c_str());

            // Where an AMD driver dll comes from: the installed driver or a copy in an ILION folder
            if (entry.kind == IlionUpscalers::Kind::Provider)
                tip += std::string("\n") + Tr(entry.key == L"FSR4/AMD driver" ? "AMD driver dll, used from the installed driver."
                                                                             : "Copy of an AMD driver dll in an ILION folder.");

            if (IlionUpscalers::SwitchesAsFfx(entry))
                tip += std::string("\n") + Tr("Switches while the game runs.");

            if (IlionUpscalers::SwitchesAsFfx(entry) && api != DX12)
                tip += std::string("\n") + Tr("Runs through OptiScaler's DirectX 12 interop.");
            else if (!variant.status.loaded)
                tip += std::string("\n") + Tr("Needs a restart of the game: d4r is loaded when the game starts.");

            if (entry.forwardsToAmd)
                tip += std::string("\n") + Tr("AMD's FSR is next to it: HelixSR lists it after itself. ILION always "
                                              "starts HelixSR's own provider.");

            // What is checked and what is only stated, kept apart
            if (entry.family == IlionUpscalers::Family::HelixSR)
                tip += std::string("\n") + Tr("Identified as HelixSR by its file. While upscaling, ILION also checks "
                                              "that this dll is the one running.\nDLSS 3.7: the version its author "
                                              "states, it can't be verified from ILION.");

            WrappedTooltip("%s", tip.c_str());
        }

        ImGui::EndCombo();
    }

    ImGui::PopItemWidth();
    ImGui::EndDisabled();

    // The warning before DLSS 4 through d4r on a GPU other than AMD RDNA 3
    if (d4rWarningOpen)
    {
        ImGui::OpenPopup("DLSS 4 (d4r)###d4rWarning");
        d4rWarningOpen = false;
    }

    if (ImGui::BeginPopupModal("DLSS 4 (d4r)###d4rWarning", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28.0f);
        ImGui::TextUnformatted(Tr("Currently only available on AMD RDNA 3. Enabling it on other architectures may cause crashes"));
        ImGui::PopTextWrapPos();
        ImGui::Spacing();

        if (IlionTheme::Button(TrL("Continue##d4rWarning")))
        {
            if (const auto entry = IlionUpscalers::FindByKey(d4rWarningKey); entry.has_value())
                SelectUpscaler(&*entry);

            d4rWarningKey.clear();
            ImGui::CloseCurrentPopup();
        }

        ImGui::SameLine();

        if (IlionTheme::Button(TrL("Cancel##d4rWarning")))
        {
            d4rWarningKey.clear();
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    UpscalerTip("Upscalers ILION found in ILION\\Escaladores (next to the game exe):\n"
                "  FSR4\\<name>\\amd_fidelityfx_upscaler_dx12.dll   FSR 4 builds, switch while playing\n"
                "  HelixSR\\helixsr_upscaler.dll                    HelixSR, switches while playing\n"
                "  DLSS4\\<name>\\ (d4r folder)                     DLSS 4 model K, needs a restart\n"
                "Older ILION\\FSR_Versions folders keep working. What a dll is comes from the file, not from the\n"
                "name of its folder. Add one and press Rescan.");

    SameLineIfRoom(ButtonWidth(TrL("Rescan##IlionUpscaler")));

    if (IlionTheme::Button(TrL("Rescan##IlionUpscaler")))
        RescanSrVariants();

    if (locked)
        TextWrappedDisabled("%s", Tr("It can't be changed while a benchmark is running."));

    ImGui::PopID();
}


// Frame generation that isn't OptiScaler's own: the DLSSG of the game, done by the real nvngx or by what stands in
// for it (DLSS Enabler, Nukem's...). OptiScaler sees every one of its evaluations and how many frames each asks
// for; that count is what "Current DLSSG state" shows in the Frame Gen tab and what this reads.
struct ExternalFgStatus
{
    enum class Kind
    {
        Off,     // nothing is asking for generated frames
        Active,  // DLSSG is being evaluated, text says by whom and the multiplier
        Unknown, // a replacement is loaded but its evaluations can't be seen in this API
    };

    Kind kind = Kind::Off;
    std::string text;
};

static const char* NvngxFgName(FGNvngxReplacement provider)
{
    switch (provider)
    {
    case FGNvngxReplacement::Arturs:
        return "Enabler";
    case FGNvngxReplacement::Nukems:
        return "Nukems";
    case FGNvngxReplacement::FFX:
        return "FFX";
    case FGNvngxReplacement::Combo:
        return "Combo";
    default:
        return nullptr;
    }
}

// generatedFrames: frames generated per rendered one in the last DLSSG evaluation, 0 when there is none
static ExternalFgStatus GetExternalFgStatus(int generatedFrames, FGNvngxReplacement provider, bool evaluationsVisible)
{
    ExternalFgStatus status;
    const char* name = NvngxFgName(provider);

    if (generatedFrames > 0)
    {
        status.kind = ExternalFgStatus::Kind::Active;
        status.text = StrFmt("%s %dx", name != nullptr ? name : "DLSSG", generatedFrames + 1);
    }
    else if (name != nullptr && !evaluationsVisible)
    {
        status.kind = ExternalFgStatus::Kind::Unknown;
        status.text = StrFmt("%s ?", name);
    }

    return status;
}

// CARDS -----------------------------

// d4r writes its own log next to its loader, in the folder OptiScaler loads NGX from
static std::wstring IlionD4rLogPath()
{
    if (!Config::Instance()->NvngxPath.has_value())
        return {};

    return (std::filesystem::path(Config::Instance()->NvngxPath.value()) / L"d4r_nvngx.log").wstring();
}

// Set only while the Basic Mode panel is being drawn. The section functions below stop after their main
// controls when it's set, so both modes run the same code on the same config and state.
static bool basicView = false;

static void DrawCardTitle(const char* label)
{
    label = Tr(label);

    // Name plate of the panel: an olive band across the top of the card with a metal rule under it
    if (IlionTheme::industrial)
    {
        const ImGuiWindow* window = ImGui::GetCurrentWindow();
        const ImGuiStyle& style = ImGui::GetStyle();
        const ImVec2 cursor = window->DC.CursorPos;

        const float left = cursor.x - style.WindowPadding.x + 1.0f;
        const float right = window->WorkRect.Max.x + style.WindowPadding.x - 1.0f;
        const float top = cursor.y - style.WindowPadding.y * 0.75f + 1.0f;
        const float bottom = cursor.y + ImGui::GetTextLineHeight() + style.ItemSpacing.y * 0.5f;

        IlionTheme::DrawCardBand(window->DrawList, left, top, right, bottom);
    }

    // On the plate the name goes in upper case, only as it is drawn
    const std::string plate = IlionTheme::industrial ? IlionTheme::Upper(label) : std::string();

    const bool inverseHead = IlionTheme::industrial && IlionTheme::skin == IlionTheme::Skin::Crt;
    ImGui::PushStyleColor(ImGuiCol_Text, inverseHead               ? IlionTheme::GraphiteDeep
                                         : IlionTheme::industrial ? IlionTheme::TitleAmber
                                                                  : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));

    ImFont* titleFont = IlionTheme::TitleFont();
    if (titleFont != nullptr)
        ImGui::PushFont(titleFont, -1.0f);

    if (IlionTheme::industrial)
        ImGui::TextUnformatted(plate.c_str());
    else
        ImGui::TextUnformatted(label, ImGui::FindRenderedTextEnd(label));

    if (titleFont != nullptr)
        ImGui::PopFont();

    ImGui::PopStyleColor();

    // Spacing below is not an item the tooltips should attach to
    const ImRect titleRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
    const ImGuiID titleId = ImGui::GetItemID();

    ImGui::Spacing();
    ImGui::Spacing();
    // ImGui::Separator();

    // HoveredRect has to reflect the mouse, IsItemHovered trusts it
    const ImGuiItemStatusFlags hovered =
        ImGui::IsMouseHoveringRect(titleRect.Min, titleRect.Max) ? ImGuiItemStatusFlags_HoveredRect : 0;
    ImGui::SetLastItemData(titleId, 0, hovered, titleRect);
}

// ILION: the title a folded card already shows on its key, skipped once inside it
static const char* skipSectionTitle = nullptr;

// Used instead of ImGui::SeparatorText. The first one in a card becomes the title of the card
static void SectionTitle(const char* label)
{
    if (skipSectionTitle != nullptr && strcmp(skipSectionTitle, label) == 0)
    {
        skipSectionTitle = nullptr;
        return;
    }

    if (!ScopedCard::TryTitle(label))
        IlionTheme::SectionPlate(Tr(label));
}

// ILION: coloured text that wraps at the edge of the card instead of being cut by it
static void WrappedText(const ImVec4& colour, const char* text)
{
    ImGui::PushStyleColor(ImGuiCol_Text, colour);
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
}

// ILION: widths of the next control, to put it on the same line only when it fits there whole. A switch of the
// ILION themes is a little wider than an ImGui checkbox, both fit in CheckboxWidth.
static float ButtonWidth(const char* label)
{
    return ImGui::CalcTextSize(label, nullptr, true).x + ImGui::GetStyle().FramePadding.x * 2.0f;
}

static float CheckboxWidth(const char* label)
{
    return ImGui::GetFrameHeight() * 1.5f + ImGui::GetStyle().ItemInnerSpacing.x + ImGui::CalcTextSize(label, nullptr, true).x;
}

static void SameLineIfRoom(float nextWidth, float spacing)
{
    const float gap = spacing < 0.0f ? ImGui::GetStyle().ItemSpacing.x : spacing;

    if (ImGui::GetItemRectMax().x + gap + nextWidth <= ImGui::GetCurrentWindow()->WorkRect.Max.x)
        ImGui::SameLine(0.0f, spacing);
}

// ILION: the next switch on the same line only when it fits whole there (any language, font or theme)
static void SameLineIfFits(const char* nextLabel, float spacing) { SameLineIfRoom(CheckboxWidth(nextLabel), spacing); }

// ILION: TextDisabled / TextColored that wrap at the edge of their container instead of being cut by it
static void TextWrappedDisabled(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrappedV(fmt, args);
    ImGui::PopStyleColor();
    va_end(args);
}

static void TextWrappedColored(const ImVec4& colour, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    ImGui::PushStyleColor(ImGuiCol_Text, colour);
    ImGui::TextWrappedV(fmt, args);
    ImGui::PopStyleColor();
    va_end(args);
}

// ILION: a text that fits the width of the current item, or its shorter form when it doesn't
static void FitText(const std::string& full, const std::string& compact)
{
    const std::string& shown = ImGui::CalcTextSize(full.c_str()).x <= ImGui::CalcItemWidth() ? full : compact;
    ImGui::TextUnformatted(shown.c_str());
}

ScopedCard::ScopedCard(const char* title)
{
    // Cards don't nest, an inner one only gives a title to the outer one
    if (_current != nullptr)
    {
        if (title != nullptr)
            SectionTitle(title);

        return;
    }

    const ImGuiStyle& style = ImGui::GetStyle();

    _window = ImGui::GetCurrentWindow();
    _pad = ImVec2(style.WindowPadding.x, style.WindowPadding.y * 0.75f);
    _min = _window->DC.CursorPos;
    _width = ImGui::GetContentRegionAvail().x;

    // Content goes to the channel 1, the background is added to the channel 0 once the height is known
    _splitter.Split(_window->DrawList, 2);
    _splitter.SetCurrentChannel(_window->DrawList, 1);

    _window->DC.CursorPos.y += _pad.y;
    ImGui::Indent(_pad.x);

    // GetContentRegionAvail (used by child windows) reads ContentRegionRect, so both need the padding
    _oldWorkRectMaxX = _window->WorkRect.Max.x;
    _oldContentRectMaxX = _window->ContentRegionRect.Max.x;
    _window->WorkRect.Max.x -= _pad.x;
    _window->ContentRegionRect.Max.x -= _pad.x;

    _contentStartY = _window->DC.CursorPos.y;
    _active = true;
    _current = this;

    if (title != nullptr)
    {
        DrawCardTitle(title);
        _hasTitle = true;
    }
}

ScopedCard::~ScopedCard()
{
    if (!_active)
        return;

    _current = nullptr;

    const ImGuiStyle& style = ImGui::GetStyle();
    ImDrawList* drawList = _window->DrawList;

    ImGui::Unindent(_pad.x);
    _window->WorkRect.Max.x = _oldWorkRectMaxX;
    _window->ContentRegionRect.Max.x = _oldContentRectMaxX;

    // Nothing was drawn, skip the rectangle
    if (_window->DC.CursorPos.y <= _contentStartY)
    {
        _splitter.Merge(drawList);
        _window->DC.CursorPos.y = _min.y;
        return;
    }

    const float bottom = _window->DC.CursorPos.y - style.ItemSpacing.y + _pad.y;
    const ImVec2 max(_min.x + _width, bottom);

    _splitter.SetCurrentChannel(drawList, 0);
    // drawList->AddRectFilled(_min, max, ImGui::GetColorU32(ImGuiCol_WindowBg), style.ChildRounding);

    if (IlionTheme::industrial)
    {
        // A steel plate standing a little above the cabinet
        IlionTheme::DrawCardBody(drawList, _min, max);
    }
    else
    {
        drawList->AddRect(_min, max, ImGui::GetColorU32(ImGuiCol_Separator), style.ChildRounding);
    }

    _splitter.Merge(drawList);

    // Registers the size of the card and leaves a gap before the next one
    _window->DC.CursorPos.y = bottom;
    ImGui::Dummy(ImVec2(0.0f, 0.0f));
}

bool ScopedCard::TryTitle(const char* label)
{
    if (_current == nullptr || _current->_hasTitle)
        return false;

    // Only when it's the very first thing in the card
    if (ImGui::GetCurrentWindow() != _current->_window || _current->_window->DC.CursorPos.y != _current->_contentStartY)
        return false;

    DrawCardTitle(label);
    _current->_hasTitle = true;

    return true;
}

bool ScopedCard::Shows(const char* id)
{
    // Inside of a card everything belongs to that card
    return _filter == nullptr || (_current != nullptr && !_strictFilter) || strcmp(_filter, id) == 0;
}

void ScopedCard::SetFilter(const char* id) { _filter = id; }

void ScopedCard::SetStrictFilter(bool strict) { _strictFilter = strict; }

void MenuCommon::RenderCard(RenderMenuContext& ctx, void (*render)(RenderMenuContext&))
{
    ScopedCard card;
    render(ctx);
}

// ILION: a card that starts folded, only a key with its name until it is opened. For advanced and diagnostic
// settings; the section draws as always inside, without repeating the title the key already shows.
void MenuCommon::RenderFoldedCard(RenderMenuContext& ctx, const char* key, const char* title,
                                  void (*render)(RenderMenuContext&))
{
    ScopedCard card;

    if (auto ch = ScopedCollapsingHeader(TrL(key)); ch.IsHeaderOpen())
    {
        skipSectionTitle = title;
        render(ctx);
        skipSectionTitle = nullptr;
    }
}

void MenuCommon::UpdateManualInput(HWND targetHwnd)
{
    OptiInput::BeginFrame(targetHwnd);

    const auto config = Config::Instance();

    auto CheckShortcut = [&](int vk, bool& inputFlag, const char* logMessage)
    {
        if (inputFlag)
            return;

        if (vk <= 0 || vk >= 256)
            return;

        if (OptiInput::IsKeyReleased(vk))
        {
            lastKey = vk;
            // receivingWmInputs = false;
            inputFlag = true;
            LOG_DEBUG("{}", logMessage);
        }
    };

    const auto currentTick = GetTickCount64();
    const bool canAcceptInputs = lastInputTick + debounceThreshold < currentTick;

    if (!capturingKey && canAcceptInputs)
    {
        CheckShortcut(config->ShortcutKey.value_or_default(), inputMenu, "Menu key pressed, will be switching menu");
        CheckShortcut(config->FpsShortcutKey.value_or_default(), inputFps, "Menu key pressed, will be switching FPS");
        CheckShortcut(config->FGShortcutKey.value_or_default(), inputFG, "Menu key pressed, will be switching FG mode");
        CheckShortcut(config->FpsCycleShortcutKey.value_or_default(), inputFpsCycle,
                      "Menu key pressed, will be switching FPS mode");
    }
    else if (capturingKey)
    {
        lastInputTick = currentTick;
    }

    lastKey = OptiInput::GetLastPressedKey();
}

void MenuCommon::ShowTooltip(const char* tip)
{
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(TooltipWrapWidth());
        ImGui::Text(Tr(tip));
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

template <typename TOption>
bool MenuCommon::ConfigCheckbox(const char* label, TOption& option, const char* tip, bool needsRestart)
{
    bool value = option.value_or_default();
    const bool changed = IlionTheme::Checkbox(TrL(label), &value);

    if (changed)
        option = value;

    if (tip != nullptr)
    {
        if (needsRestart)
            ShowTooltip((std::string(Tr(tip)) + Tr("\n\nAfter changing this option, please Save Settings.\n"
                                                   "It will be applied on next launch."))
                            .c_str());
        else
            ShowTooltip(tip);
    }

    return changed;
}

void MenuCommon::ShowResetButton(CustomOptional<bool, NoDefault>* initFlag, std::string buttonName)
{
    ImGui::SameLine();

    ImGui::BeginDisabled(!initFlag->has_value());

    if (IlionTheme::Button(buttonName.c_str()))
    {
        initFlag->reset();
        ReInitUpscaler();
    }

    ImGui::EndDisabled();
}

inline void MenuCommon::ReInitUpscaler()
{
    if (!State::Instance().currentFeature)
        return;

    if (State::Instance().currentFeature->GetUpscalerType() == Upscaler::DLSSD)
        State::Instance().newBackend = Upscaler::DLSSD;
    else
        State::Instance().newBackend = currentBackend;

    MARK_ALL_BACKENDS_CHANGED();
}

void MenuCommon::SeparatorWithHelpMarker(const char* label, const char* tip)
{
    SectionTitle(label);
    ShowTooltip(tip);
}

bool MenuCommon::SliderUInt(const char* label, uint32_t* v, uint32_t v_min, uint32_t v_max, const char* format,
                            ImGuiSliderFlags flags)
{
    return ImGui::SliderScalar(label, ImGuiDataType_U32, v, &v_min, &v_max, format, flags);
}

class Keybind
{
    std::string name;
    int id;
    bool waitingForKey = false;

  public:
    Keybind(std::string name, int id) : name(name), id(id) {}

    static std::string KeyNameFromVirtualKeyCode(USHORT virtualKey)
    {
        if (virtualKey == (USHORT) UnboundKey)
            return "Unbound";

        UINT scanCode = MapVirtualKeyW(virtualKey, MAPVK_VK_TO_VSC);

        // Keys like Home would display as Num 0 without this fix
        switch (virtualKey)
        {
        case VK_INSERT:
        case VK_DELETE:
        case VK_HOME:
        case VK_END:
        case VK_PRIOR:
        case VK_NEXT:
        case VK_LEFT:
        case VK_RIGHT:
        case VK_UP:
        case VK_DOWN:
        case VK_NUMLOCK:
        case VK_DIVIDE:
        case VK_RCONTROL:
        case VK_RMENU:
            scanCode |= 0xE000;
            break;
        }

        LONG lParam = (scanCode & 0xFF) << 16;
        if (scanCode & 0xE000)
            lParam |= 1 << 24;

        wchar_t buf[64] = {};
        if (GetKeyNameTextW(lParam, buf, static_cast<int>(std::size(buf))) != 0)
            return wstring_to_string(buf);

        return "Unknown";
    }

    void Render(CustomOptional<int>& configKey)
    {
        ImGui::PushID(id);
        if (IlionTheme::Button(TrL(name.c_str())))
        {
            waitingForKey = true;
            capturingKey = true;
            lastKey = 0;
        }
        ImGui::PopID();

        if (waitingForKey)
        {
            ImGui::SameLine();
            ImGui::Text(Tr("Press any key..."));

            if (lastKey == 0 || lastKey == VK_LBUTTON || lastKey == VK_RBUTTON || lastKey == VK_MBUTTON)
                return;

            if (lastKey == VK_ESCAPE)
            {
                waitingForKey = false;
                capturingKey = false;
                return;
            }

            if (lastKey == VK_BACK)
                lastKey = UnboundKey;

            configKey = lastKey;
            waitingForKey = false;
            capturingKey = false;
            return;
        }

        ImGui::SameLine();
        ImGui::Text(KeyNameFromVirtualKeyCode(configKey.value_or_default()).c_str());

        ImGui::SameLine();
        ImGui::PushID(id);
        if (IlionTheme::Button("R"))
        {
            configKey.reset();
        }
        ImGui::PopID();
    }
};

Upscaler MenuCommon::GetBackendCode(const API api)
{
    if (auto feature = State::Instance().currentFeature)
        return feature->GetUpscalerType();

    Upscaler upscaler;

    if (api == DX11)
        upscaler = Config::Instance()->Dx11Upscaler.value_or_default();
    else if (api == DX12)
        upscaler = Config::Instance()->Dx12Upscaler.value_or_default();
    else
        upscaler = Config::Instance()->VulkanUpscaler.value_or_default();

    return upscaler;
}

void MenuCommon::GetCurrentBackendInfo(const API api, Upscaler& upscaler, std::string* name)
{
    upscaler = GetBackendCode(api);
    *name = UpscalerDisplayName(upscaler, api);
}

// List of upscalers reported by FFX for a given backend, nullptr if the backend doesn't use FFX
static State::FfxVersionList* GetFfxVersionList(Upscaler upscaler, API api)
{
    if (upscaler == Upscaler::FFX_on12 || (upscaler == Upscaler::FFX && api == API::DX12))
        return &State::Instance().ffxUpscalerVersionsDx12;

    if (upscaler == Upscaler::FFX && api == API::Vulkan)
        return &State::Instance().ffxUpscalerVersionsVk;

    return nullptr;
}

static bool IsOn12(Upscaler upscaler)
{
    switch (upscaler)
    {
    case Upscaler::XeSS_on12:
    case Upscaler::FSR21_on12:
    case Upscaler::FSR22_on12:
    case Upscaler::FFX_on12:
    case Upscaler::DLSS_on12:
        return true;
    default:
        return false;
    }
}

// Name without the w/Dx12 suffix, that one is shown as a tag in the list instead
static std::string ShortUpscalerName(Upscaler upscaler, API api, int ffxIndex)
{
    if (auto versions = GetFfxVersionList(upscaler, api);
        versions != nullptr && ffxIndex >= 0 && ffxIndex < versions->names.size())
        return StrFmt("FSR %s", versions->names[ffxIndex]);

    switch (upscaler)
    {
    case Upscaler::XeSS_on12:
        return UpscalerDisplayName(Upscaler::XeSS);
    case Upscaler::FSR21_on12:
        return UpscalerDisplayName(Upscaler::FSR21);
    case Upscaler::FSR22_on12:
        return UpscalerDisplayName(Upscaler::FSR22);
    case Upscaler::FFX_on12:
        return UpscalerDisplayName(Upscaler::FFX, API::DX12);
    case Upscaler::DLSS_on12:
        return UpscalerDisplayName(Upscaler::DLSS);
    default:
        return UpscalerDisplayName(upscaler, api);
    }
}

static feature_version UpscalerSortVersion(Upscaler upscaler)
{
    switch (upscaler)
    {
    case Upscaler::FSR21:
    case Upscaler::FSR21_on12:
        return { 2, 1, 2 };
    case Upscaler::FSR22:
    case Upscaler::FSR22_on12:
        return { 2, 2, 1 };
    case Upscaler::FSR31:
        return { 3, 1, 2 };
    case Upscaler::FFX:
    case Upscaler::FFX_on12:
        return { 3, 1, 0 };
    default:
        return {};
    }
}

// Indices into the FFX version list, newest first
static std::vector<int> FfxVersionsNewestFirst(const State::FfxVersionList& versions)
{
    std::vector<int> order(versions.names.size());
    std::iota(order.begin(), order.end(), 0);

    std::stable_sort(order.begin(), order.end(), [&versions](int a, int b)
                     { return feature_version(versions.names[b]) < feature_version(versions.names[a]); });

    return order;
}

// ILION: FSR 4 model (C of technology / implementation / model), under the upscaler selector. Shown only when the
// module in use has several networks; changing it makes the context again and reverts if that fails
static void RenderFsr4ModelControls(Upscaler currentBackend, IFeature* feature, float scale)
{
    auto config = Config::Instance();
    auto& state = State::Instance();

    // ILION: FSR 4 network (FSR4Models), when the dll in use has several. Changing it makes the context again,
    // like the preset above; if that context fails, the previous choice comes back (FfxApiProxy)
    // Only with an FFX backend chosen and running (switching to XeSS, DLSS... hides it; the preference stays saved)
    Upscaler target = state.newBackend != Upscaler::Reset ? state.newBackend : currentBackend;
    const bool ffxBackend = (target == Upscaler::FFX || target == Upscaler::FFX_on12) &&
                            feature != nullptr && (feature->GetUpscalerType() == Upscaler::FFX ||
                                                   feature->GetUpscalerType() == Upscaler::FFX_on12);
    if (!ffxBackend)
        return;

    // The module whose networks are offered: native Vulkan FFX runs the AMD Vulkan driver's (directly or through
    // amd_fidelityfx_vk.dll, the same amdvkffx64 module); DX12 and the Vulkan to DX12 interop the DX12 dll in use
    const bool vulkanNative = state.api == Vulkan && target == Upscaler::FFX;
    const HMODULE srModule = vulkanNative ? (FSR4DriverProviderVk::IsAvailable() ? FSR4DriverProviderVk::Module() : nullptr)
                                          : FfxApiProxy::Dx12Module_SR();

    if (auto models = FSR4Models::SelectableModels(srModule); !models.empty())
    {
        auto& selectedModel = config->Fsr4Model;
        const std::optional<FSR4Model> previousModel =
            selectedModel.has_value() ? std::optional<FSR4Model>(selectedModel.value()) : std::nullopt;
        const char* automatic = Tr("Automatic");
        bool modelChanged = false;

        // Radioactive amber (#FFB000) for the model (green stays the implementation's), darker on light themes; through
        // the menu's HDR mapping like the other colours, so it shows the same amber in SDR and HDR
        const ImVec4 modelColour = toneMapColor(IlionTheme::Fsr4ModelAmber(ImGui::GetStyleColorVec4(ImGuiCol_FrameBg)));
        const ImVec4 modelItemColour = toneMapColor(IlionTheme::Fsr4ModelAmber(ImGui::GetStyleColorVec4(ImGuiCol_PopupBg)));

        ImGui::SetNextItemWidth(150.0f * scale);
        if (IlionTheme::BeginCombo(TrL("FSR4 Model"), previousModel.has_value() ? FSR4Models::Name(*previousModel) : automatic, 0,
                                   &modelColour))
        {
            ImGui::PushStyleColor(ImGuiCol_Text, modelItemColour);

            if (ImGui::Selectable(automatic, !previousModel.has_value()) && previousModel.has_value())
            {
                selectedModel.reset();
                modelChanged = true;
            }

            for (const auto model : models)
            {
                if (ImGui::Selectable(FSR4Models::Name(model), previousModel == model) && previousModel != model)
                {
                    selectedModel = model;
                    modelChanged = true;
                }
            }

            ImGui::PopStyleColor();
            ImGui::EndCombo();
        }

        if (modelChanged)
        {
            FfxApiProxy::ArmSrModelRevert(previousModel);
            state.newBackend = currentBackend;
            MARK_ALL_BACKENDS_CHANGED();
        }

        UpscalerTip("FSR 4 network to run. Automatic lets FFX pick, as before.\n"
                    "A network meant for another GPU (e.g. INT8 RDNA2 elsewhere) is experimental.");
    }

    // ILION: requested and determined network, in Advanced. Determined = the one FFX picked in its support checks when
    // it made the context; it isn't a reading of what the GPU runs
    if (srModule != nullptr && !basicView && !EssentialView())
    {
        // Only while an FFX FSR 4 context really runs: after a failure (OptiScaler falls back to FSR 2.2) or with
        // another provider of the dll, the last support checks no longer tell anything
        const bool fsr4Runs = feature != nullptr && feature->IsInited() && feature->Version().major >= 4 &&
                              (feature->GetUpscalerType() == Upscaler::FFX || feature->GetUpscalerType() == Upscaler::FFX_on12);
        const auto determined = fsr4Runs ? FSR4Models::ActiveModel(srModule) : std::nullopt;

        if (determined.has_value() || config->Fsr4Model.has_value())
        {
            ImGui::TextDisabled("%s: %s  |  %s: %s", Tr("Requested model"),
                                config->Fsr4Model.has_value() ? FSR4Models::Name(config->Fsr4Model.value()) : Tr("Automatic"),
                                Tr("Determined model"), determined.has_value() ? FSR4Models::Name(*determined) : Tr("not determined"));
            UpscalerTip("Requested: the network chosen above.\n"
                        "Determined: the one FFX picked in its support checks when it made the context.\n"
                        "It isn't a reading of what the GPU runs: not confirmed in execution.");
        }
    }
}

void MenuCommon::RenderUpscalerCombo(const API api, Upscaler currentUpscaler, const std::vector<Upscaler>& options)
{
    auto primaryGpu = IdentifyGpu::getPrimaryGpu();
    auto config = Config::Instance();

    Upscaler targetBackend = State::Instance().newBackend;
    if (targetBackend == Upscaler::Reset)
        targetBackend = currentUpscaler;

    const int ffxIndex = FfxIndexFor(targetBackend, api).value_or_default();

    // Interop is only shown here, the list uses a tag for it
    auto selectedName = ShortUpscalerName(targetBackend, api, ffxIndex);
    if (IsOn12(targetBackend))
        selectedName += " (Dx12)";

    if (!IlionTheme::BeginCombo("##UpscalerCombo", selectedName.c_str(), ImGuiComboFlags_HeightLargest))
        return;

    struct Entry
    {
        Upscaler upscaler;
        int ffxIndex; // Newest upscaler reported by FFX, -1 when not using FFX
        feature_version version;
    };

    std::vector<Entry> entries;

    for (auto opt : options)
    {
        // Check if GPU is capable of a given backend
        if ((opt == Upscaler::DLSS || opt == Upscaler::DLSS_on12) && !primaryGpu.dlssCapable)
            continue;

        // Not all Intel GPUs support native DX11 XeSS but don't think we have a good way to check exactly
        if (opt == Upscaler::XeSS && api == API::DX11 && primaryGpu.vendorId != VendorId::Intel)
            continue;

        // Only the newest FFX upscaler is listed here, the rest are in the FFX Upscaler combo
        if (auto versions = GetFfxVersionList(opt, api); versions != nullptr)
        {
            if (versions == &State::Instance().ffxUpscalerVersionsVk)
                FFXFeature::EnsureVersionsVulkan();
            else
                FFXFeature::EnsureVersionsDx12();

            // Fall back to a generic entry if FFX couldn't be queried
            if (!versions->names.empty())
            {
                auto newest = FfxVersionsNewestFirst(*versions)[0];
                entries.push_back({ opt, newest, feature_version(versions->names[newest]) });
                continue;
            }
        }

        entries.push_back({ opt, -1, UpscalerSortVersion(opt) });
    }

    // Upscaler of the GPU vendor goes first, FSRs are sorted by version, newest first
    // Stable sort keeps native ahead of w/Dx12
    auto group = [&primaryGpu](Upscaler upscaler)
    {
        int family = IsFsr(upscaler) ? 1 : (upscaler == Upscaler::DLSS || upscaler == Upscaler::DLSS_on12) ? 2 : 0;

        const int vendorFamily = primaryGpu.vendorId == VendorId::AMD      ? 1
                                 : primaryGpu.vendorId == VendorId::Nvidia ? 2
                                                                           : 0;

        return family == vendorFamily ? -1 : family;
    };

    std::stable_sort(entries.begin(), entries.end(),
                     [&group](const Entry& a, const Entry& b)
                     {
                         if (group(a.upscaler) != group(b.upscaler))
                             return group(a.upscaler) < group(b.upscaler);

                         return b.version < a.version;
                     });

    for (size_t i = 0; i < entries.size(); i++)
    {
        const auto& entry = entries[i];

        if (i > 0 && group(entry.upscaler) != group(entries[i - 1].upscaler))
            ImGui::Separator();

        auto label =
            StrFmt("%s##%d", ShortUpscalerName(entry.upscaler, api, entry.ffxIndex).c_str(), (int) entry.upscaler);

        if (ImGui::Selectable(label.c_str(), currentUpscaler == entry.upscaler))
        {
            State::Instance().newBackend = entry.upscaler;

            // Switching to FFX starts with the newest one
            if (entry.ffxIndex >= 0 && currentUpscaler != entry.upscaler)
            {
                FfxIndexFor(entry.upscaler, api) = entry.ffxIndex;

                if (&FfxIndexFor(entry.upscaler, api) == &config->FfxUpscalerIndex)
                    FfxApiProxy::SetSrIndexManual(true);
            }
        }

        // Dimmed tags aligned to the right
        const char* tag = nullptr;

        if (entry.ffxIndex >= 0 && IsOn12(entry.upscaler))
            tag = "FFX Dx12";
        else if (entry.ffxIndex >= 0)
            tag = "FFX";
        else if (IsOn12(entry.upscaler))
            tag = "Dx12";

        if (tag != nullptr)
        {
            auto& style = ImGui::GetStyle();

            ImGui::SameLine();
            ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowWidth() - style.WindowPadding.x -
                                                                      ImGui::CalcTextSize(tag).x));
            ImGui::TextDisabled(tag);
        }
    }

    ImGui::EndCombo();
}

void MenuCommon::AddDx11Backends(Upscaler upscaler)
{
    RenderUpscalerCombo(API::DX11, upscaler,
                        { Upscaler::XeSS, Upscaler::FSR22, Upscaler::FSR31, Upscaler::XeSS_on12, Upscaler::FSR21_on12,
                          Upscaler::FSR22_on12, Upscaler::FFX_on12, Upscaler::DLSS, Upscaler::DLSS_on12 });
}

void MenuCommon::AddDx12Backends(Upscaler upscaler)
{
    RenderUpscalerCombo(API::DX12, upscaler,
                        { Upscaler::XeSS, Upscaler::FSR21, Upscaler::FSR22, Upscaler::FFX, Upscaler::DLSS });
}

void MenuCommon::AddVulkanBackends(Upscaler upscaler)
{
    RenderUpscalerCombo(API::Vulkan, upscaler,
                        { Upscaler::XeSS, Upscaler::FSR21, Upscaler::FSR22, Upscaler::FFX, Upscaler::FSR21_on12,
                          Upscaler::FFX_on12, Upscaler::DLSS, Upscaler::DLSS_on12 });
}

template <HasDefaultValue B> void MenuCommon::AddResourceBarrier(std::string name, CustomOptional<int32_t, B>* value)
{
    const char* states[] = { "AUTO",
                             "COMMON",
                             "VERTEX_AND_CONSTANT_BUFFER",
                             "INDEX_BUFFER",
                             "RENDER_TARGET",
                             "UNORDERED_ACCESS",
                             "DEPTH_WRITE",
                             "DEPTH_READ",
                             "NON_PIXEL_SHADER_RESOURCE",
                             "PIXEL_SHADER_RESOURCE",
                             "STREAM_OUT",
                             "INDIRECT_ARGUMENT",
                             "COPY_DEST",
                             "COPY_SOURCE",
                             "RESOLVE_DEST",
                             "RESOLVE_SOURCE",
                             "RAYTRACING_ACCELERATION_STRUCTURE",
                             "SHADING_RATE_SOURCE",
                             "GENERIC_READ",
                             "ALL_SHADER_RESOURCE",
                             "PRESENT",
                             "PREDICATION",
                             "VIDEO_DECODE_READ",
                             "VIDEO_DECODE_WRITE",
                             "VIDEO_PROCESS_READ",
                             "VIDEO_PROCESS_WRITE",
                             "VIDEO_ENCODE_READ",
                             "VIDEO_ENCODE_WRITE" };
    const int values[] = { -1,  0,   1,     2,      4,      8,      16,      32,       64,   128,
                           256, 512, 1024,  2048,   4096,   8192,   4194304, 16777216, 2755, 192,
                           0,   310, 65536, 131072, 262144, 524288, 2097152, 8388608 };

    int selected = value->value_or(-1);

    const char* selectedName = "";

    for (int n = 0; n < 28; n++)
    {
        if (values[n] == selected)
        {
            selectedName = states[n];
            break;
        }
    }

    if (IlionTheme::BeginCombo(name.c_str(), selectedName))
    {
        if (ImGui::Selectable(states[0], !value->has_value()))
            value->reset();

        for (int n = 1; n < 28; n++)
        {
            if (ImGui::Selectable(states[n], selected == values[n]))
                *value = values[n];
        }

        ImGui::EndCombo();
    }
}

static uint32_t GetPresetIndex(IFeature* feature, bool dlssd = false)
{
    auto ratio = (float) feature->TargetWidth() / (float) feature->RenderWidth();

    if (!dlssd)
    {
        if (State::Instance().dlssPresetsOverridenByOpti)
        {
            LOG_DEBUG("DLSS Presets overridden by Opti, using Opti preset indices with ratio: {}", ratio);

            if (ratio <= (Config::Instance()->QualityRatio_UltraPerformance.value_or_default() + 0.01f))
            {
                return Config::Instance()->RenderPresetForAll.value_or(
                    Config::Instance()->RenderPresetUltraPerformance.value_or_default());
            }
            else if (ratio <= (Config::Instance()->QualityRatio_Performance.value_or_default() + 0.01f))
            {
                return Config::Instance()->RenderPresetForAll.value_or(
                    Config::Instance()->RenderPresetPerformance.value_or_default());
            }
            else if (ratio <= (Config::Instance()->QualityRatio_Balanced.value_or_default() + 0.01f))
            {
                return Config::Instance()->RenderPresetForAll.value_or(
                    Config::Instance()->RenderPresetBalanced.value_or_default());
            }
            else if (ratio <= (Config::Instance()->QualityRatio_Quality.value_or_default() + 0.01f))
            {
                return Config::Instance()->RenderPresetForAll.value_or(
                    Config::Instance()->RenderPresetQuality.value_or_default());
            }
            else if (ratio <= (Config::Instance()->QualityRatio_UltraQuality.value_or_default() + 0.01f))
            {
                return Config::Instance()->RenderPresetForAll.value_or(
                    Config::Instance()->RenderPresetUltraQuality.value_or_default());
            }
            else
            {
                return Config::Instance()->RenderPresetForAll.value_or(
                    Config::Instance()->RenderPresetDLAA.value_or_default());
            }
        }
        else if (State::Instance().dlssPresetsOverriddenExternally)
        {
            LOG_DEBUG("DLSS Presets overridden externally, using external preset index: {}",
                      State::Instance().dlssRenderPresetExternal);

            return State::Instance().dlssRenderPresetExternal;
        }
        else
        {
            if (ratio <= (Config::Instance()->QualityRatio_UltraPerformance.value_or_default() + 0.01f))
            {
                return State::Instance().dlssRenderPresetUltraPerformance;
            }
            else if (ratio <= (Config::Instance()->QualityRatio_Performance.value_or_default() + 0.01f))
            {
                return State::Instance().dlssRenderPresetPerformance;
            }
            else if (ratio <= (Config::Instance()->QualityRatio_Balanced.value_or_default() + 0.01f))
            {
                return State::Instance().dlssRenderPresetBalanced;
            }
            else if (ratio <= (Config::Instance()->QualityRatio_Quality.value_or_default() + 0.01f))
            {
                return State::Instance().dlssRenderPresetQuality;
            }
            else if (ratio <= (Config::Instance()->QualityRatio_UltraQuality.value_or_default() + 0.01f))
            {
                return State::Instance().dlssRenderPresetUltraQuality;
            }
            else
            {
                return State::Instance().dlssRenderPresetDLAA;
            }
        }
    }
    else
    {
        if (State::Instance().dlssdPresetsOverridenByOpti)
        {
            if (ratio <= (Config::Instance()->QualityRatio_UltraPerformance.value_or_default() + 0.01f))
            {
                return Config::Instance()->DLSSDRenderPresetForAll.value_or(
                    Config::Instance()->DLSSDRenderPresetUltraPerformance.value_or_default());
            }
            else if (ratio <= (Config::Instance()->QualityRatio_Performance.value_or_default() + 0.01f))
            {
                return Config::Instance()->DLSSDRenderPresetForAll.value_or(
                    Config::Instance()->DLSSDRenderPresetPerformance.value_or_default());
            }
            else if (ratio <= (Config::Instance()->QualityRatio_Balanced.value_or_default() + 0.01f))
            {
                return Config::Instance()->DLSSDRenderPresetForAll.value_or(
                    Config::Instance()->DLSSDRenderPresetBalanced.value_or_default());
            }
            else if (ratio <= (Config::Instance()->QualityRatio_Quality.value_or_default() + 0.01f))
            {
                return Config::Instance()->DLSSDRenderPresetForAll.value_or(
                    Config::Instance()->DLSSDRenderPresetQuality.value_or_default());
            }
            else if (ratio <= (Config::Instance()->QualityRatio_UltraQuality.value_or_default() + 0.01f))
            {
                return Config::Instance()->DLSSDRenderPresetForAll.value_or(
                    Config::Instance()->DLSSDRenderPresetUltraQuality.value_or_default());
            }
            else
            {
                return Config::Instance()->DLSSDRenderPresetForAll.value_or(
                    Config::Instance()->DLSSDRenderPresetDLAA.value_or_default());
            }
        }
        else if (State::Instance().dlssdPresetsOverriddenExternally)
        {
            return State::Instance().dlssdRenderPresetExternal;
        }
        else
        {
            if (ratio <= (Config::Instance()->QualityRatio_UltraPerformance.value_or_default() + 0.01f))
            {
                return State::Instance().dlssdRenderPresetUltraPerformance;
            }
            else if (ratio <= (Config::Instance()->QualityRatio_Performance.value_or_default() + 0.01f))
            {
                return State::Instance().dlssdRenderPresetPerformance;
            }
            else if (ratio <= (Config::Instance()->QualityRatio_Balanced.value_or_default() + 0.01f))
            {
                return State::Instance().dlssdRenderPresetBalanced;
            }
            else if (ratio <= (Config::Instance()->QualityRatio_Quality.value_or_default() + 0.01f))
            {
                return State::Instance().dlssdRenderPresetQuality;
            }
            else if (ratio <= (Config::Instance()->QualityRatio_UltraQuality.value_or_default() + 0.01f))
            {
                return State::Instance().dlssdRenderPresetUltraQuality;
            }
            else
            {
                return State::Instance().dlssdRenderPresetDLAA;
            }
        }
    }

    return 0;
}

// TODO: disable presets based on the detected DLSS version
template <HasDefaultValue B> void MenuCommon::AddDLSSRenderPreset(std::string name, CustomOptional<uint32_t, B>* value)
{
    // clang-format off
    static const std::vector<MenuOption<uint32_t>> presets = {
        { NVSDK_NGX_DLSS_Hint_Render_Preset_Default, "DEFAULT", 
            "Whatever the game uses" },
        { NVSDK_NGX_DLSS_Hint_Render_Preset_A, "PRESET A",
            "Intended for Performance/Balanced/Quality modes.\nAn older variant best suited to combat ghosting...\nRemoved on recent versions!" },
        { NVSDK_NGX_DLSS_Hint_Render_Preset_B, "PRESET B",
            "Intended for Ultra Performance mode.\nSimilar to Preset A...\nRemoved on recent versions!" },
        { NVSDK_NGX_DLSS_Hint_Render_Preset_C, "PRESET C",
            "Intended for Performance/Balanced/Quality modes.\nGenerally favors current frame information...\nRemoved on recent versions!" },
        { NVSDK_NGX_DLSS_Hint_Render_Preset_D, "PRESET D",
            "Default preset for Performance/Balanced/Quality modes;\ngenerally favors image stability.\nRemoved on recent versions!" },
        { NVSDK_NGX_DLSS_Hint_Render_Preset_E, "PRESET E",
            "DLSS 3.7+, a better D preset\nRemoved on recent versions!" },
        { NVSDK_NGX_DLSS_Hint_Render_Preset_F, "PRESET F",
            "Default preset for Ultra Performance and DLAA modes\nRemoved on recent versions!" },
        { NVSDK_NGX_DLSS_Hint_Render_Preset_G, "PRESET G",
            "Unused" },
        { NVSDK_NGX_DLSS_Hint_Render_Preset_H_Reserved, "PRESET H",
            "Unused" },
        { NVSDK_NGX_DLSS_Hint_Render_Preset_I_Reserved, "PRESET I",
            "Unused" },
        { NVSDK_NGX_DLSS_Hint_Render_Preset_J, "PRESET J",
            "Similar to preset K. Preset J might exhibit slightly\nless ghosting...\n1st Gen Transformer" },
        { NVSDK_NGX_DLSS_Hint_Render_Preset_K, "PRESET K",
            "Default preset for DLAA/Balanced/Quality modes...\n1st Gen Transformer" },
        { NVSDK_NGX_DLSS_Hint_Render_Preset_L, "PRESET L",
            "Default for Ultra Perf mode\n2nd Gen Transformers" },
        { NVSDK_NGX_DLSS_Hint_Render_Preset_M, "PRESET M",
            "Default for Perf mode\n2nd Gen Transformer" },
        { NVSDK_NGX_DLSS_Hint_Render_Preset_N, "PRESET N",
            "Unused" },
        { NVSDK_NGX_DLSS_Hint_Render_Preset_O, "PRESET O",
            "Unused" },
        { NV_PRESET_LATEST, "Latest",
            "Latest supported by the dll" }
    };
    // clang-format on

    PopulateCombo(name, *value, presets);
}

template <HasDefaultValue B> void MenuCommon::AddDLSSDRenderPreset(std::string name, CustomOptional<uint32_t, B>* value)
{
    // We don't have DLSSD definitions so using raw values
    static const std::vector<MenuOption<uint32_t>> presets = {
        { 0, "DEFAULT", "Whatever the game uses" },
        { 1, "PRESET A", "Preset A\nRemoved on recent versions!" },
        { 2, "PRESET B", "Preset B\nRemoved on recent versions!" },
        { 3, "PRESET C", "Preset C\nRemoved on recent versions!" },
        { 4, "PRESET D", "Default model, Transformer" },
        { 5, "PRESET E", "Latest Transformer model\nMust use if DoF guide is needed" },
        { 6, "PRESET F", "Latest Transformer model\nMust use if DoF guide is needed" },
        { NV_PRESET_LATEST, "Latest", "Latest supported by the dll" }
    };

    PopulateCombo(name, *value, presets);
}

template <typename TStorage, typename T>
void MenuCommon::PopulateCombo(const std::string& name, TStorage& currentValue,
                               const std::vector<MenuOption<T>>& options)
{
    if (options.empty())
        return;

    // Assumes that different types mean that TStorage is std::optional
    T currentVal;
    if constexpr (std::is_same_v<TStorage, T>)
        currentVal = currentValue;
    else
        currentVal = currentValue.value_or(options[0].value);

    // Find the label for the currently selected item
    std::string preview = Tr("Unknown");
    for (const auto& opt : options)
    {
        if (opt.value == currentVal)
        {
            preview = opt.label;
            break;
        }
    }

    // ILION: names, options and their tips in the language of the menu, the English label stays the id
    if (IlionTheme::BeginCombo(TrL(name.c_str()), Tr(preview.c_str())))
    {
        for (const auto& opt : options)
        {
            if (opt.hidden)
                continue;

            if (opt.disabled)
                ImGui::BeginDisabled();

            bool isSelected = (currentVal == opt.value);
            if (ImGui::Selectable(TrL(opt.label.c_str()), isSelected))
                currentValue = opt.value;

            // Show tooltip for the individual item if it exists
            if (!opt.tooltip.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                WrappedTooltip("%s", Tr(opt.tooltip.c_str()));

            if (opt.disabled)
                ImGui::EndDisabled();
        }
        ImGui::EndCombo();
    }
}

static UiTargetMode getUiTargetMode()
{
    const auto& state = State::Instance();

    const bool fallback = !Config::Instance()->OverlayMenu.value_or_default();

    if (fallback)
    {
        // We have no reliable swapchain/output color-space information here.
        // Only classify the upscaled working image.
        if (state.currentFeature && state.currentFeature->IsHdr())
            return UiTargetMode::LinearHDR;

        return UiTargetMode::SDR;
    }

    const auto& output = state.outputColorSpace;

    // If SetColorSpace1 has not provided a known/valid color space,
    // fall back conservatively.
    if (!output.valid)
        return UiTargetMode::SDR;

    switch (output.transfer)
    {
    case ColorTransfer::Linear:
        // scRGB: linear Rec.709 RGB.
        //
        // hdrOutputActive is intentionally NOT required here.
        // A scRGB swapchain is still linear even when the physical output
        // is currently SDR. hdrOutputActive only affects the desired
        // reference-white scaling in toneMapColor().
        if (output.model == ColorModel::RGB && output.primaries == ColorPrimaries::Rec709)
        {
            return UiTargetMode::ScRGB;
        }

        break;

    case ColorTransfer::PQ:
        // Direct PQ UI rendering currently assumes RGB PQ / Rec.2020.
        //
        // Do not treat YCbCr PQ as an RGB render target.
        if (output.model == ColorModel::RGB && output.primaries == ColorPrimaries::Rec2020)
        {
            return UiTargetMode::PQ;
        }

        break;

    case ColorTransfer::HLG:
        // Current HLG UI path assumes an RGB render target.
        //
        // DXGI HLG modes are commonly YCbCr, so reject unsupported
        // combinations rather than applying an RGB HLG transform blindly.
        if (output.model == ColorModel::RGB && output.primaries == ColorPrimaries::Rec2020)
        {
            return UiTargetMode::HLG;
        }

        break;

    case ColorTransfer::SRGB:
        return UiTargetMode::SDR;

    case ColorTransfer::Unknown:
    default:
        break;
    }

    // Unsupported model / primaries / transfer combination.
    return UiTargetMode::SDR;
}

static float srgbToLinear(float x)
{
    x = std::clamp(x, 0.0f, 1.0f);

    if (x <= 0.04045f)
        return x / 12.92f;

    return std::pow((x + 0.055f) / 1.055f, 2.4f);
}

static float linearToPQ(float nits)
{
    // SMPTE ST.2084
    constexpr float m1 = 2610.0f / 16384.0f;
    constexpr float m2 = 2523.0f / 32.0f;
    constexpr float c1 = 3424.0f / 4096.0f;
    constexpr float c2 = 2413.0f / 128.0f;
    constexpr float c3 = 2392.0f / 128.0f;

    const float y = std::clamp(nits / 10000.0f, 0.0f, 1.0f);
    const float ym1 = std::pow(y, m1);

    return std::pow((c1 + c2 * ym1) / (1.0f + c3 * ym1), m2);
}

static float linearToHLG(float x)
{
    // BT.2100 HLG OETF
    constexpr float a = 0.17883277f;
    constexpr float b = 0.28466892f;
    constexpr float c = 0.55991073f;

    x = std::max(x, 0.0f);

    if (x <= (1.0f / 12.0f))
        return std::sqrt(3.0f * x);

    return a * std::log(12.0f * x - b) + c;
}

static ImVec4 linear709To2020(float r, float g, float b)
{
    return ImVec4(0.6274040f * r + 0.3292820f * g + 0.0433136f * b, 0.0690970f * r + 0.9195400f * g + 0.0113612f * b,
                  0.0163916f * r + 0.0880132f * g + 0.8955950f * b, 0.0f);
}

static ImVec4 legacyHdrToneMap(const ImVec4& color)
{
    constexpr float exposure = 1.0f;
    constexpr float strength = 1.0f;

    const float peak = std::max(color.x, std::max(color.y, color.z));

    if (peak <= 0.0f)
        return color;

    const float exposedPeak = peak * exposure;
    const float mappedPeak = exposedPeak / (1.0f + exposedPeak);

    const float reinhardScale = mappedPeak / peak;
    const float scale = 1.0f + (reinhardScale - 1.0f) * strength;

    return ImVec4(color.x * scale, color.y * scale, color.z * scale, color.w);
}

static ImVec4 toneMapColor(const ImVec4& color)
{
    const auto mode = getUiTargetMode();

    switch (mode)
    {
    case UiTargetMode::SDR:
        return color;

    case UiTargetMode::LinearHDR:
        return ImVec4(srgbToLinear(color.x), srgbToLinear(color.y), srgbToLinear(color.z), color.w);

    case UiTargetMode::ScRGB:
    {
        constexpr float scRgbReferenceWhiteNits = 80.0f;
        constexpr float hdrUiWhiteNits = 203.0f;

        const float uiWhiteNits = State::Instance().hdrOutputActive ? hdrUiWhiteNits : scRgbReferenceWhiteNits;

        const float scale = uiWhiteNits / scRgbReferenceWhiteNits;

        return ImVec4(srgbToLinear(color.x) * scale, srgbToLinear(color.y) * scale, srgbToLinear(color.z) * scale,
                      color.w);
    }

    case UiTargetMode::PQ:
        // Direct ImGui rendering into a nonlinear PQ target.
        //
        // Proper PQ encoding of vertex colors produces incorrect results
        // with the standard ImGui alpha blend state because blending then
        // happens in PQ space.
        //
        // Keep the known-good legacy compression until PQ rendering is
        // moved to a linear intermediate/composite pass.
        return legacyHdrToneMap(color);

    case UiTargetMode::HLG:
        // Same fundamental nonlinear-blending problem as PQ.
        // Conservative compatibility behavior for now.
        return legacyHdrToneMap(color);

    default:
        return color;
    }
}

static void MenuHdrCheck(ImGuiIO io)
{
    if (!_hdrTonemapApplied)
    {
        ImGuiStyle& style = ImGui::GetStyle();
        const auto mode = getUiTargetMode();

        LOG_INFO("Output HDR: {}, UI Mode: {}", State::Instance().hdrOutputActive, magic_enum::enum_name(mode));

        CopyMemory(SdrColors, style.Colors, sizeof(style.Colors));

        // Apply tone mapping to the ImGui style
        for (int i = 0; i < ImGuiCol_COUNT; ++i)
        {
            ImVec4 color = style.Colors[i];
            style.Colors[i] = toneMapColor(color);
        }

        _hdrTonemapApplied = true;
    }
}

static float MenuResolutionScale(ImGuiIO io)
{
    if (Config::Instance()->MenuScale.has_value())
        return Config::Instance()->MenuScale.value();

    // Calculate menu scale according to display resolution
    float y = State::Instance().screenHeight;

    if (io.DisplaySize.y != 0)
        y = (float) io.DisplaySize.y;

    // 1000p is minimum for 1.0 menu ratio
    float result = (float) ((int) (y / 108.0f)) / 10.0f;

    result = std::round(result * 10.0f) / 10.0f;

    if (result < 0.5f)
        result = 0.5f;

    if (result > 2.0f)
        result = 2.0f;

    return result;
}

inline static std::string GetSourceString(UINT source)
{
    switch (source)
    {
    case 1:
        return "RTV";
    case 2:
        return "SRV";
    case 4:
        return "UAV";
    case 8:
        return "OM";
    case 16:
        return "Ups";
    case 32:
        return "SCR";
    case 64:
        return "SGR";
    case 128:
        return "OMUAV";
    default:
        return std::format("{}", source);
    }
}

inline static std::string GetDispatchString(UINT source)
{
    switch (source)
    {
    case 0:
        return "-";
    case 512:
        return "DI";
    case 1024:
        return "DII";
    case 256:
        return "Disp";
    default:
        return std::format("{}", source);
    }
}

void MenuCommon::ApplyThemeStyle()
{
    if (ImGui::GetCurrentContext() == nullptr)
        return;

    ImGuiStyle& style = ImGui::GetStyle();

    auto conf = Config::Instance();
    bool lightTheme = conf->LightTheme.value_or_default();

    // The industrial theme has its own fixed palette, the color settings below belong to the classic one
    IlionTheme::industrial = conf->IndustrialTheme.value_or_default();
    IlionTheme::SetPalette(conf->IlionPalette.value_or_default());

    if (IlionTheme::industrial)
    {
        IlionTheme::ApplyStyle(style, conf->MenuBGColorA.value_or_default());
        return;
    }

    style.ChildBorderSize = 1.0f;
    style.WindowTitleAlign = ImVec2(0.0f, 0.5f);
    style.WindowRounding = 2.0f;
    style.ChildRounding = 1.0f;
    style.FrameRounding = 2.0f;
    style.PopupRounding = 2.0f;
    style.ScrollbarRounding = 2.0f;
    style.GrabRounding = 2.0f;
    style.TabRounding = 2.0f;

    style.WindowBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;

    style.FrameBorderSize = lightTheme ? 1.0f : 0.0f;
    style.TabBorderSize = lightTheme ? 1.0f : 0.0f;

    style.ScrollbarSize = 10.0f;
    style.GrabMinSize = 10.0f;

    auto Clamp01 = [](float v) { return std::max(0.0f, std::min(v, 1.0f)); };

    auto Mix = [](const ImVec4& a, const ImVec4& b, float t, float alpha = 1.0f)
    { return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, alpha); };

    auto Luminance = [](const ImVec4& c) { return c.x * 0.2126f + c.y * 0.7152f + c.z * 0.0722f; };

    auto Saturate = [&](const ImVec4& color, float amount)
    {
        float lum = Luminance(color);

        return ImVec4(Clamp01(lum + (color.x - lum) * amount), Clamp01(lum + (color.y - lum) * amount),
                      Clamp01(lum + (color.z - lum) * amount), color.w);
    };

    ImVec4 accent = ImVec4(conf->MenuAccentColorR.value_or_default(), conf->MenuAccentColorG.value_or_default(),
                           conf->MenuAccentColorB.value_or_default(), 1.0f);

    ImVec4 bgAccent = ImVec4(conf->MenuBGColorR.value_or_default(), conf->MenuBGColorG.value_or_default(),
                             conf->MenuBGColorB.value_or_default(), 1.0f);

    float luminance = Luminance(accent);

    const ImVec4 bgDark = lightTheme ? ImVec4(0.80f, 0.82f, 0.86f, 1.00f) : ImVec4(0.09f, 0.09f, 0.10f, 1.00f);
    const ImVec4 bgMid = lightTheme ? ImVec4(0.89f, 0.91f, 0.95f, 1.00f) : ImVec4(0.11f, 0.11f, 0.12f, 1.00f);
    const ImVec4 bgLight = lightTheme ? ImVec4(0.96f, 0.97f, 0.99f, 1.00f) : ImVec4(0.14f, 0.14f, 0.15f, 1.00f);

    const ImVec4 textPrimary = lightTheme ? ImVec4(0.05f, 0.06f, 0.08f, 1.00f) : ImVec4(0.90f, 0.93f, 0.95f, 1.00f);
    const ImVec4 textDim = lightTheme ? ImVec4(0.22f, 0.25f, 0.31f, 1.00f) : ImVec4(0.54f, 0.58f, 0.62f, 1.00f);

    const ImVec4 borderCol = lightTheme ? ImVec4(0.35f, 0.40f, 0.50f, 1.00f) : ImVec4(0.24f, 0.24f, 0.26f, 1.00f);
    const ImVec4 dimBg = lightTheme ? ImVec4(0.30f, 0.33f, 0.38f, 0.20f) : ImVec4(0.09f, 0.10f, 0.13f, 0.20f);
    const ImVec4 modalDimBg = lightTheme ? ImVec4(0.22f, 0.24f, 0.28f, 0.55f) : ImVec4(0.04f, 0.04f, 0.07f, 0.55f);

    // MenuBGColor: only background/surface tint.
    auto BgTint = [&](const ImVec4& base, float strength = 1.0f, float alpha = 1.0f)
    {
        float t = lightTheme ? (0.180f * strength) : (0.120f * strength);
        return Mix(base, bgAccent, t, alpha);
    };

    // MenuAccentColor: all visible interactive accent colors.
    auto AccentSoft = [&](float alpha = 1.0f)
    { return lightTheme ? Mix(bgLight, accent, 0.14f, alpha) : Mix(bgDark, accent, 0.32f, alpha); };

    auto AccentMed = [&](float alpha = 1.0f)
    { return lightTheme ? Mix(bgLight, accent, 0.42f, alpha) : Mix(bgDark, accent, 0.55f, alpha); };

    auto AccentStrong = [&](float alpha = 1.0f) { return ImVec4(accent.x, accent.y, accent.z, alpha); };

    const ImVec4 bgTitle = AccentSoft();

    auto SurfaceHover = [&](float alpha = 1.0f)
    { return lightTheme ? Mix(bgLight, accent, 0.12f, alpha) : Mix(bgLight, accent, 0.18f, alpha); };

    auto SurfaceActive = [&](float alpha = 1.0f)
    { return lightTheme ? Mix(bgLight, accent, 0.20f, alpha) : Mix(bgLight, accent, 0.28f, alpha); };

    auto TitleActive = [&](float alpha = 1.0f)
    { return lightTheme ? Mix(bgTitle, accent, 0.18f, alpha) : Mix(bgTitle, accent, 0.16f, alpha); };

    auto PlotAccent = [&](float alpha = 1.0f)
    {
        if (lightTheme)
        {
            // Darken slightly for contrast on light bg — no channel floors
            return Mix(accent, ImVec4(0.00f, 0.00f, 0.00f, 1.00f), 0.20f, alpha);
        }

        // Brighten slightly for visibility on dark bg — no channel floors
        return Mix(accent, ImVec4(1.00f, 1.00f, 1.00f, 1.00f), 0.35f, alpha);
    };

    auto PlotAccentHovered = [&](float alpha = 1.0f)
    {
        if (lightTheme)
        {
            return Mix(PlotAccent(alpha), ImVec4(0.00f, 0.00f, 0.00f, 1.00f), 0.15f, alpha);
        }

        return Mix(PlotAccent(alpha), ImVec4(1.00f, 1.00f, 1.00f, 1.00f), 0.25f, alpha);
    };

    auto AccentReadable = [&](float alpha = 1.0f)
    {
        // Apply saturation boost and luminance correction only here,
        // so AccentStrong / AccentMed / AccentSoft stay true to the user's pick.
        ImVec4 a = Saturate(accent, lightTheme ? 1.35f : 1.25f);
        float lum = Luminance(a);

        if (lightTheme && lum > 0.72f)
            a = Mix(a, ImVec4(0.0f, 0.0f, 0.0f, 1.0f), 0.35f, 1.0f);

        if (!lightTheme && lum < 0.25f)
            a = Mix(a, ImVec4(1.0f, 1.0f, 1.0f, 1.0f), 0.30f, 1.0f);

        return ImVec4(a.x, a.y, a.z, alpha);
    };

    ImVec4* c = ImGui::GetStyle().Colors;

    float minAlpha = Config::Instance()->MenuBGColorA.value_or_default() >= 0.5f
                         ? Config::Instance()->MenuBGColorA.value_or_default()
                         : 0.5f;

    c[ImGuiCol_Text] = textPrimary;
    c[ImGuiCol_TextDisabled] = textDim;
    c[ImGuiCol_TextLink] = AccentReadable();

    // MenuBGColor only.
    c[ImGuiCol_WindowBg] = BgTint(bgDark, 1.00f, Config::Instance()->MenuBGColorA.value_or_default());
    c[ImGuiCol_ChildBg] = BgTint(bgMid, 1.10f, minAlpha + 0.1f);
    c[ImGuiCol_PopupBg] =
        lightTheme ? BgTint(bgLight, 0.90f) : BgTint(ImVec4(0.09f, 0.10f, 0.13f, 0.97f), 0.90f, 0.97f);
    c[ImGuiCol_MenuBarBg] = BgTint(bgDark, 0.85f);
    c[ImGuiCol_DockingEmptyBg] = BgTint(bgDark, 0.75f);

    c[ImGuiCol_Border] = borderCol;
    c[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);

    // Neutral background, not MenuBGColor.
    c[ImGuiCol_FrameBg] = BgTint(bgLight, 0.50f, minAlpha + 0.15f);
    c[ImGuiCol_FrameBgHovered] = SurfaceHover();
    c[ImGuiCol_FrameBgActive] = SurfaceActive();

    c[ImGuiCol_TitleBg] = BgTint(bgTitle, 0.40f);
    c[ImGuiCol_TitleBgActive] = TitleActive();
    c[ImGuiCol_TitleBgCollapsed] = ImVec4(bgTitle.x, bgTitle.y, bgTitle.z, 0.75f);

    c[ImGuiCol_ScrollbarBg] = BgTint(bgDark, 0.60f, minAlpha + 0.2f);
    c[ImGuiCol_ScrollbarGrab] = AccentSoft();
    c[ImGuiCol_ScrollbarGrabHovered] = AccentMed();
    c[ImGuiCol_ScrollbarGrabActive] = AccentStrong();

    c[ImGuiCol_CheckMark] = AccentReadable();
    c[ImGuiCol_SliderGrab] = AccentMed();
    c[ImGuiCol_SliderGrabActive] = AccentReadable();
    c[ImGuiCol_InputTextCursor] = AccentReadable();

    c[ImGuiCol_Button] = AccentSoft();
    c[ImGuiCol_ButtonHovered] = AccentMed();
    c[ImGuiCol_ButtonActive] = AccentStrong();

    c[ImGuiCol_Header] = AccentSoft(0.90f);
    c[ImGuiCol_HeaderHovered] = AccentMed(0.95f);
    c[ImGuiCol_HeaderActive] = AccentStrong();

    c[ImGuiCol_Separator] = borderCol;
    c[ImGuiCol_SeparatorHovered] = AccentMed(0.85f);
    c[ImGuiCol_SeparatorActive] = AccentStrong();

    c[ImGuiCol_ResizeGrip] = AccentSoft(0.30f);
    c[ImGuiCol_ResizeGripHovered] = AccentStrong(0.70f);
    c[ImGuiCol_ResizeGripActive] = AccentStrong(0.95f);

    c[ImGuiCol_Tab] = BgTint(bgDark, 0.60f);
    c[ImGuiCol_TabHovered] = AccentMed(0.85f);
    c[ImGuiCol_TabSelected] = AccentMed();
    c[ImGuiCol_TabSelectedOverline] = AccentStrong();
    c[ImGuiCol_TabDimmed] = BgTint(bgDark, 0.60f);
    c[ImGuiCol_TabDimmedSelected] = AccentSoft(0.75f);
    c[ImGuiCol_TabDimmedSelectedOverline] = borderCol;

    c[ImGuiCol_DockingPreview] = AccentStrong(0.70f);

    c[ImGuiCol_PlotLines] = PlotAccent();
    c[ImGuiCol_PlotLinesHovered] = PlotAccentHovered();
    c[ImGuiCol_PlotHistogram] = PlotAccent(0.85f);
    c[ImGuiCol_PlotHistogramHovered] = PlotAccentHovered();

    c[ImGuiCol_TableHeaderBg] = BgTint(bgMid, 0.80f, minAlpha + 0.25f);
    c[ImGuiCol_TableBorderStrong] = borderCol;
    c[ImGuiCol_TableBorderLight] = lightTheme ? ImVec4(0.68f, 0.72f, 0.80f, 1.00f) : AccentSoft();
    c[ImGuiCol_TableRowBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.0f);
    c[ImGuiCol_TableRowBgAlt] = lightTheme ? ImVec4(0.00f, 0.00f, 0.00f, 0.045f) : ImVec4(1.00f, 1.00f, 1.00f, 0.03f);

    c[ImGuiCol_TreeLines] = borderCol;
    c[ImGuiCol_TextSelectedBg] = AccentMed(0.38f);
    c[ImGuiCol_DragDropTarget] = AccentStrong(0.90f);
    c[ImGuiCol_NavCursor] = AccentReadable();
    c[ImGuiCol_NavWindowingHighlight] = AccentStrong(0.70f);
    c[ImGuiCol_NavWindowingDimBg] = dimBg;
    c[ImGuiCol_ModalWindowDimBg] = modalDimBg;

    _hdrTonemapApplied = false;
    MenuHdrCheck(ImGui::GetIO());
}

static double lastTime = 0.0;
static double lastFrameTime = 0.0;
static UINT64 uwpTargetFrame = 0;

void MenuCommon::Present()
{
    _frameCount++;

    auto now = Util::MillisecondsNow();

    if (lastTime > 0.0)
        lastFrameTime = now - lastTime;

    lastTime = now;

    if (_handle != nullptr)
        UpdateManualInput(_handle);
}

struct VersionCheckStatus
{
    bool completed = false;
    bool updateAvailable = false;
    std::string latestTag;
    std::string latestUrl;
    std::string error;
};

struct MenuCommon::RenderMenuContext
{
    State& state;
    decltype(Config::Instance()) config;
    ImGuiIO& io;
    IFeature* currentFeature = nullptr;

    double now = 0.0;
    double frameTime = 0.0;
    double frameRate = 0.0;
    float menuResScale = 1.0f;
    float fpsScale = 1.0f;
    float averageFrameTime = 0.0f;
    float averageUpscalerFT = 0.0f;

    bool frameTimesCalculated = false;
    bool newFrame = false;

    VersionCheckStatus versionStatus;
    std::string currentVersionText;

    // Cached when the menu is visible and shared by RenderMainMenuWindow section helpers.
    std::unique_ptr<std::decay_t<decltype(IdentifyGpu::getPrimaryGpu())>> primaryGpu;
};

static std::string splashMessage;

void MenuCommon::UpdateRenderTiming(RenderMenuContext& ctx)
{
    auto& state = ctx.state;
    auto config = ctx.config;
    auto& now = ctx.now;
    auto& frameTime = ctx.frameTime;
    auto& frameRate = ctx.frameRate;

    if (config->OverlayMenu.value_or_default())
    {
        _frameCount++;

        // FPS & frame time calculation
        if (lastTime > 0.0)
        {
            frameTime = now - lastTime;
            frameRate = 1000.0 / frameTime;
        }

        lastTime = now;

        if (_handle != nullptr)
            UpdateManualInput(_handle);
    }
    else
    {
        if (state.activeFgInput == FGInput::NoFG || state.activeFgOutput == FGOutput::NoFG)
            MenuCommon::Present();

        frameTime = lastFrameTime;
        frameRate = 1000.0 / frameTime;
    }

    state.frameTimes.pop_front();
    state.frameTimes.push_back(frameTime);
}

// BENCHMARK COMPARISON -----------------------------
// Window of its own, opened from the Benchmark tab. Reads the saved tests back from benchmarks.csv.
static bool showBenchmarkCompare = false;
static bool benchmarkCompareReload = true;

static std::filesystem::path BenchmarkFolder() { return Util::ExePath().parent_path() / L"ILION" / L"Benchmarks"; }

static const char* CompareIssueText(IlionCompare::Issue issue)
{
    switch (issue)
    {
    case IlionCompare::Issue::DifferentGame:
        return "The tests are from different games.";
    case IlionCompare::Issue::DifferentResolution:
        return "Render or output resolution differs between the tests.";
    case IlionCompare::Issue::DifferentUpscaler:
        return "The tests used different upscaling technologies: performance and stability are compared, image "
               "quality is not.";
    case IlionCompare::Issue::DifferentModel:
        return "The tests used different models of the same technology.";
    case IlionCompare::Issue::DifferentFrameGeneration:
        return "Frame generation was not in the same state in all the tests.";
    case IlionCompare::Issue::DifferentDuration:
        return "The tests don't last the same (more than a tenth apart).";
    case IlionCompare::Issue::StoppedEarly:
        return "At least one test was stopped before its time.";
    case IlionCompare::Issue::FramesWithoutUpscale:
        return "At least one test has many frames without upscaling (menus, loading).";
    case IlionCompare::Issue::FewFrames:
        return "At least one test has fewer than 1000 frames, its lowest percentile is not available.";
    default:
        return "ILION can't check that the same scene was played in every test.";
    }
}

// Title, text and color level (0 = winner, 1 = neutral, 2 = warning) of every conclusion
struct CompareConclusion
{
    std::string title;
    std::string text;
    int level = 1;
};

static std::vector<CompareConclusion> BuildCompareConclusions(const IlionCompare::Analysis& analysis,
                                                              const std::vector<const IlionCompare::Test*>& tests)
{
    using Kind = IlionCompare::Kind;
    using Reason = IlionCompare::Reason;

    std::vector<CompareConclusion> result;

    const auto name = [&](int index) -> std::string
    {
        if (index < 0 || index >= (int) analysis.candidates.size())
            return "-";

        const auto& candidate = analysis.candidates[index];

        if (candidate.tests.size() > 1)
            return StrFmt(Tr("%s (average of %d tests)"), candidate.name.c_str(), (int) candidate.tests.size());

        return candidate.name;
    };

    const auto names = [&](const std::vector<int>& indices)
    {
        std::string joined;

        for (const int index : indices)
            joined += (joined.empty() ? "" : ", ") + name(index);

        return joined;
    };

    // Reasons that end the whole comparison
    const Reason overall = analysis.performance.reason;

    if (overall == Reason::NotComparable)
    {
        result.push_back({ Tr("INCONCLUSIVE RESULT"),
                           Tr("The tests were made under different conditions, no winner is named. "
                              "The differences below are shown with that limitation."),
                           2 });
        return result;
    }

    if (overall == Reason::SameDll)
    {
        double lowest = 0.0, highest = 0.0;

        for (const auto* test : tests)
        {
            if (!test->avgFps.valid)
                continue;

            lowest = lowest == 0.0 ? test->avgFps.value : std::min(lowest, test->avgFps.value);
            highest = std::max(highest, test->avgFps.value);
        }

        result.push_back({ Tr("INCONCLUSIVE RESULT"),
                           StrFmt(Tr("All the selected tests were made with the same dll. Their average FPS differ "
                                     "by %.1f%%, which is the variation between runs."),
                                  lowest > 0.0 ? (highest / lowest - 1.0) * 100.0 : 0.0),
                           1 });
        return result;
    }

    if (overall == Reason::MissingData)
    {
        result.push_back({ Tr("INCONCLUSIVE RESULT"), Tr("Some of the tests lack the values needed to compare."), 2 });
        return result;
    }

    // Performance
    const auto& performance = analysis.performance;

    if (performance.kind == Kind::Winner)
    {
        const auto& winner = analysis.candidates[performance.winner].average;
        const auto& runnerUp = analysis.candidates[performance.runnerUp].average;

        std::string text = StrFmt(Tr("%s reached %.1f average FPS, %.1f%% more than %s (%.1f)."),
                                  name(performance.winner).c_str(), winner.avgFps.value, performance.avgAdvantage,
                                  name(performance.runnerUp).c_str(), runnerUp.avgFps.value);

        if (performance.lowsWorseThan >= 0)
        {
            text += StrFmt(Tr(" Its 1%% low is clearly lower than the one of %s, the higher average comes with "
                              "worse lows."),
                           name(performance.lowsWorseThan).c_str());
        }
        else if (performance.low1Advantage.valid)
        {
            text += StrFmt(Tr(performance.low1Advantage.value >= 0.0 ? " Its 1%% low is %.1f%% higher."
                                                                     : " Its 1%% low is %.1f%% lower."),
                           std::abs(performance.low1Advantage.value));
        }

        result.push_back({ StrFmt(Tr("PERFORMANCE WINNER: %s"), name(performance.winner).c_str()), text,
                           performance.lowsWorseThan >= 0 ? 1 : 0 });
    }
    else
    {
        result.push_back({ Tr("PERFORMANCE: no clear winner"),
                           StrFmt(Tr("%s are within %.1f%% of each other in average FPS, the margin used here."),
                                  names(performance.tied).c_str(), analysis.marginAvg),
                           1 });
    }

    // Stability
    const auto& stability = analysis.stability;

    if (stability.kind == Kind::Winner)
    {
        std::string text = StrFmt(Tr("Frame time variation of %.1f%% against %.1f%% for %s."),
                                  stability.variationWinner, stability.variationRunnerUp,
                                  name(stability.runnerUp).c_str());

        if (stability.p99RatioWinner.valid && stability.p99RatioRunnerUp.valid)
        {
            text += StrFmt(Tr(" Its P99 is %.2fx the median against %.2fx."), stability.p99RatioWinner.value,
                           stability.p99RatioRunnerUp.value);
        }

        if (stability.stutterWinner.valid && stability.stutterRunnerUp.valid)
        {
            text += StrFmt(Tr(" Stutters per minute: %.1f against %.1f."), stability.stutterWinner.value,
                           stability.stutterRunnerUp.value);
        }

        result.push_back({ StrFmt(Tr("STABILITY WINNER: %s"), name(stability.winner).c_str()), text, 0 });
    }
    else if (stability.reason == Reason::Mixed)
    {
        result.push_back({ Tr("STABILITY: inconclusive"),
                           StrFmt(Tr("%s has the steadiest frame times but clearly more stutters than %s."),
                                  name(stability.winner).c_str(), name(stability.runnerUp).c_str()),
                           1 });
    }
    else
    {
        result.push_back({ Tr("STABILITY: no clear winner"),
                           StrFmt(Tr("Frame time variation differs by less than the margin (%.1f%% against %.1f%%)."),
                                  stability.variationWinner, stability.variationRunnerUp),
                           1 });
    }

    // Balance
    const auto& balance = analysis.balance;

    if (balance.kind == Kind::Winner)
    {
        const char* why = balance.reason == Reason::BothCategories      ? "It leads in performance and in stability."
                          : balance.reason == Reason::FasterAndAsStable ? "It is the fastest and as stable as the best."
                                                                        : "It is the most stable and as fast as the best.";

        result.push_back({ StrFmt(Tr("BEST OVERALL BALANCE: %s"), name(balance.winner).c_str()), Tr(why), 0 });
    }
    else if (balance.reason == Reason::TradeOff)
    {
        std::string text;

        if (balance.faster >= 0 && balance.moreStable >= 0)
            text = StrFmt(Tr("%s is faster, %s is more stable."), name(balance.faster).c_str(),
                          name(balance.moreStable).c_str());
        else if (balance.faster >= 0)
            text = StrFmt(Tr("%s is faster but is not among the most stable."), name(balance.faster).c_str());
        else
            text = StrFmt(Tr("%s is more stable but is not among the fastest."), name(balance.moreStable).c_str());

        result.push_back({ Tr("BALANCE: depends on the priority"), text, 1 });
    }
    else if (balance.kind == Kind::Tie)
    {
        result.push_back({ Tr("INCONCLUSIVE RESULT"),
                           Tr("No difference bigger than the margin was measured between these tests."), 1 });
    }
    else
    {
        result.push_back({ Tr("INCONCLUSIVE RESULT"), Tr("A best balance can't be named from these results."), 1 });
    }

    return result;
}

// --- Visual comparison of the captures ---------------------------------------------------------------------

static bool showVisualCompare = false;
static bool visualCompareReload = true;

// Counts the measurements written to disk, whoever shows saved results reads them again when it moves
static std::atomic<int> visualResultsSaved { 0 };

static std::filesystem::path CaptureFolder() { return Util::ExePath().parent_path() / L"ILION" / L"Captures"; }

// Measurements of captures that aren't in the folder anymore (deleted from the menu or by hand) are taken out of
// the saved results, nothing of a capture outlives it
static void PruneVisualResults(const std::vector<IlionVisual::Capture>& captures)
{
    std::vector<std::string> gone;

    for (const auto& row : IlionVisual::LoadResults(CaptureFolder()))
    {
        for (const std::string* id : { &row.referenceId, &row.testId })
        {
            const bool exists = std::any_of(captures.begin(), captures.end(),
                                            [&](const IlionVisual::Capture& capture) { return capture.id == *id; });

            if (!exists && std::find(gone.begin(), gone.end(), *id) == gone.end())
                gone.push_back(*id);
        }
    }

    if (!gone.empty())
    {
        IlionVisual::ForgetResults(CaptureFolder(), gone);
        visualResultsSaved++;
        LOG_INFO("[CAPTURE] results of {} captures that are gone were removed", gone.size());
    }
}

// Difference in SSIM two captures need to be told apart
static constexpr double VisualSsimMargin = 0.002;

// Most captures looked at together, each one is a whole picture in memory
static constexpr size_t VisualMaxSelected = 4;

// Reading the pictures and measuring them takes too long for a frame, a thread does one thing at a time
struct VisualWork
{
    std::mutex mutex;
    std::map<std::string, std::shared_ptr<const IlionVisual::Image>> images; // by capture id, empty = unreadable
    std::map<std::string, IlionVisual::Metrics> metrics;                     // by "reference|test"
    std::atomic<bool> busy { false };
};

static VisualWork& GetVisualWork()
{
    // Never freed, a thread may still be using it when the process ends
    static VisualWork* work = new VisualWork();
    return *work;
}

static std::string VisualPairKey(const std::string& referenceId, const std::string& testId)
{
    return referenceId + "|" + testId;
}

static void StartVisualLoad(const IlionVisual::Capture& capture)
{
    auto& work = GetVisualWork();
    work.busy = true;

    std::thread([id = capture.id, path = capture.path, &work]()
                {
                    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);

                    auto image = std::make_shared<IlionVisual::Image>();
                    IlionVisual::LoadPng(path, *image);

                    {
                        std::scoped_lock lock(work.mutex);
                        work.images[id] = image;
                    }

                    work.busy = false;
                })
        .detach();
}

static void StartVisualMeasure(const std::string& key, std::shared_ptr<const IlionVisual::Image> reference,
                               std::shared_ptr<const IlionVisual::Image> test, IlionVisual::SavedResult saved,
                               const std::filesystem::path& folder)
{
    auto& work = GetVisualWork();
    work.busy = true;

    std::thread([key, reference, test, saved, folder, &work]() mutable
                {
                    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);

                    const auto metrics = IlionVisual::Compare(*reference, *test);

                    // Kept on disk for the conclusions of the benchmark comparison
                    if (metrics.valid)
                    {
                        saved.ssim = metrics.ssim;
                        saved.meanDifference = metrics.meanDifference;
                        saved.differentPercent = metrics.differentPercent;
                        saved.edgeChangePercent = metrics.edgeChangePercent;
                        saved.edgesKeptPercent = metrics.edgesKeptPercent;
                        saved.overshootPercent = metrics.overshootPercent;
                        saved.unreliable = metrics.Unreliable();
                        IlionVisual::SaveResult(folder, saved);
                        visualResultsSaved++;
                    }

                    {
                        std::scoped_lock lock(work.mutex);
                        work.metrics[key] = metrics;
                    }

                    work.busy = false;
                })
        .detach();
}

static std::string VisualCaptureName(const IlionVisual::Capture& capture)
{
    const std::string name = capture.Name();
    return capture.fsrVersion.empty() && name == "Native" ? std::string(Tr("Native")) : name;
}

// Made with the same dll, or both without any upscaling
static bool SameVisualVersion(const IlionVisual::Capture& a, const IlionVisual::Capture& b)
{
    // The same dll upscaling and at native resolution are not the same thing
    if (!a.dllSha256.empty() || !b.dllSha256.empty())
        return a.dllSha256 == b.dllSha256 && a.resolution == b.resolution;

    return a.hasMeta && b.hasMeta && a.fsrVersion.empty() && b.fsrVersion.empty() &&
           a.GetOrigin() == IlionVisual::Origin::Native && b.GetOrigin() == IlionVisual::Origin::Native;
}

// A picture to measure fidelity against: native resolution and not made by any of the versions that are compared.
// The native antialiasing of an FSR dll is that dll's own picture.
static bool NeutralVisualReference(const IlionVisual::Capture& capture)
{
    return capture.GetOrigin() == IlionVisual::Origin::Native && capture.fsrVersion.empty() && capture.dllSha256.empty();
}

struct VisualEntry
{
    const IlionVisual::Capture* capture = nullptr;
    const IlionVisual::Metrics* metrics = nullptr; // nullptr while it is being measured
};

// A few lines on what was measured. A capture is only called closer to the reference when the reference is a
// native image and the difference is bigger than the margin and than what the scene changes by itself.
static std::vector<CompareConclusion> BuildVisualSummary(const IlionVisual::Capture& reference,
                                                         const std::vector<VisualEntry>& entries)
{
    std::vector<CompareConclusion> result;
    std::vector<VisualEntry> valid;

    for (const auto& entry : entries)
    {
        if (entry.metrics != nullptr && entry.metrics->valid)
            valid.push_back(entry);
    }

    if (valid.empty())
    {
        result.push_back({ Tr("VISUAL RESULT INCONCLUSIVE"),
                           Tr("None of the selected captures can be measured against the reference."), 2 });
        return result;
    }

    for (const auto& entry : valid)
    {
        if (entry.metrics->Unreliable())
        {
            result.push_back({ Tr("VISUAL RESULT INCONCLUSIVE"),
                               Tr("At least one capture doesn't show the same picture as the reference: the scene or "
                                  "the camera changed between them. Capture again without moving."),
                               2 });
            return result;
        }
    }

    // Captures of the reference's own version tell how much the picture changes by itself
    double noise = -1.0;
    std::vector<VisualEntry> candidates;

    for (const auto& entry : valid)
    {
        if (SameVisualVersion(reference, *entry.capture))
            noise = (std::max)(noise, 1.0 - entry.metrics->ssim);
        else
            candidates.push_back(entry);
    }

    if (candidates.empty())
    {
        result.push_back({ Tr("VISUAL RESULT INCONCLUSIVE"),
                           StrFmt(Tr("Only captures of the same version as the reference were selected. Their SSIM of "
                                     "%.4f is how much the picture changes by itself from one capture to the next."),
                                  1.0 - noise),
                           1 });
        return result;
    }

    // Captures of one version count as one, with the mean of their SSIM. How far apart they are is also what
    // the picture changes by itself.
    struct Version
    {
        const IlionVisual::Capture* capture;
        double ssim = 0.0;
        double lowest = 1.0, highest = 0.0;
        int captures = 0;
    };

    std::vector<Version> versions;

    for (const auto& entry : candidates)
    {
        auto version = std::find_if(versions.begin(), versions.end(),
                                    [&](const Version& v) { return SameVisualVersion(*v.capture, *entry.capture); });

        if (version == versions.end())
        {
            versions.push_back({ entry.capture });
            version = versions.end() - 1;
        }

        version->ssim += entry.metrics->ssim;
        version->lowest = (std::min)(version->lowest, entry.metrics->ssim);
        version->highest = (std::max)(version->highest, entry.metrics->ssim);
        version->captures++;
    }

    bool repeats = noise >= 0.0;

    for (auto& version : versions)
    {
        version.ssim /= version.captures;

        if (version.captures > 1)
        {
            noise = (std::max)(noise, version.highest - version.lowest);
            repeats = true;
        }
    }

    std::sort(versions.begin(), versions.end(), [](const Version& a, const Version& b) { return a.ssim > b.ssim; });

    const auto versionName = [&](const Version& version) -> std::string
    {
        if (version.captures > 1)
        {
            return StrFmt(Tr("%s (average of %d captures)"), VisualCaptureName(*version.capture).c_str(),
                          version.captures);
        }

        return VisualCaptureName(*version.capture);
    };

    // Two captures of one version are told apart by their time
    const auto name = [&](const VisualEntry& entry)
    {
        std::string text = VisualCaptureName(*entry.capture);

        for (const auto& other : candidates)
        {
            if (other.capture != entry.capture && VisualCaptureName(*other.capture) == text)
                return text + " (" + (entry.capture->dateTime.empty() ? entry.capture->id : entry.capture->dateTime) + ")";
        }

        return text;
    };

    const double margin = (std::max)(VisualSsimMargin, noise);
    const bool native = NeutralVisualReference(reference);
    const auto& best = versions.front();

    CompareConclusion main;

    if (!native)
    {
        main.title = Tr("VISUAL DIFFERENCES, without a fidelity verdict");
        main.text = StrFmt(Tr("The reference isn't a native image without upscaler: the values tell how much each "
                              "capture differs from %s, not which one is more faithful."),
                           VisualCaptureName(reference).c_str());

        if (noise >= 0.0 && 1.0 - versions.back().ssim <= noise * 1.5)
            main.text += Tr(" The differences are no bigger than those between two captures of one same version.");
    }
    else if (versions.size() == 1)
    {
        main.title = Tr("VISUAL: one version measured");
        main.text = StrFmt(Tr("%s has an SSIM of %.4f against the native reference. With one version there is nothing "
                              "to rank."),
                           versionName(best).c_str(), best.ssim);
    }
    else if (best.ssim - versions[1].ssim > margin)
    {
        main.title = StrFmt(Tr("CLOSEST TO THE NATIVE REFERENCE: %s"), versionName(best).c_str());
        main.text = StrFmt(Tr("SSIM of %.4f against %.4f for %s, the margin used is %.4f."), best.ssim,
                           versions[1].ssim, versionName(versions[1]).c_str(), margin);
        main.level = 0;
    }
    else
    {
        main.title = Tr("VISUAL RESULT INCONCLUSIVE");
        main.text = StrFmt(Tr("%s and %s are %.4f of SSIM apart, within the margin of %.4f."), versionName(best).c_str(),
                           versionName(versions[1]).c_str(), best.ssim - versions[1].ssim, margin);
    }

    if (native && !repeats)
        main.text += Tr(" Without a second capture of one same version, how much the picture changes by itself is unknown.");

    result.push_back(main);

    // Edges, a reading and not a verdict
    std::string edges;

    for (const auto& entry : candidates)
    {
        const double change = entry.metrics->edgeChangePercent;

        if (std::abs(change) < 5.0)
            continue;

        edges += StrFmt(Tr(change > 0.0 ? "%s has stronger edges than the reference (%+.1f%%). "
                                        : "%s has softer edges than the reference (%+.1f%%). "),
                        name(entry).c_str(), change);

        if (change > 0.0 && entry.metrics->overshootPercent >= 1.0)
        {
            edges += StrFmt(Tr("Its overshoot next to edges (%.1f%%) may be over-sharpening or halos. "),
                            entry.metrics->overshootPercent);
        }
    }

    if (!edges.empty())
        result.push_back({ Tr("EDGES AND SHARPNESS (exploratory)"), edges, 1 });

    return result;
}

// What the saved visual analyses say about the versions of the tests of a benchmark comparison. Its own
// category next to performance and stability, no score mixes them.
static void AddVisualConclusions(std::vector<CompareConclusion>& result, const IlionCompare::Analysis& analysis,
                                 const std::vector<const IlionCompare::Test*>& tests,
                                 const std::vector<IlionVisual::SavedResult>& saved, int* nativeBest = nullptr)
{
    struct Covered
    {
        int candidate;
        double ssim;
    };

    // With nativeBest the native capture is optional: nothing is said when there is none, and the dll that is
    // closest to it comes back when every dll was measured against it
    if (nativeBest != nullptr)
        *nativeBest = -1;

    // Per native capture used as reference, the versions measured against it. Later rows are newer.
    std::map<std::string, std::vector<Covered>> byReference;

    for (const auto& row : saved)
    {
        if (row.referenceOrigin != "native" || row.unreliable || row.testSha256.empty())
            continue;

        for (int i = 0; i < (int) analysis.candidates.size(); i++)
        {
            const auto& candidate = analysis.candidates[i];

            if (candidate.tests.empty())
                continue;

            const auto* test = tests[candidate.tests.front()];

            if (test->dllSha256 != row.testSha256 || test->game != row.game)
                continue;

            auto& covered = byReference[row.referenceId];
            const auto existing =
                std::find_if(covered.begin(), covered.end(), [&](const Covered& c) { return c.candidate == i; });

            if (existing != covered.end())
                existing->ssim = row.ssim;
            else
                covered.push_back({ i, row.ssim });
        }
    }

    // The reference that covers the most versions, the newest one among equals
    const std::string* referenceId = nullptr;
    std::vector<Covered> covered;

    for (const auto& [id, list] : byReference)
    {
        if (list.size() >= covered.size())
        {
            referenceId = &id;
            covered = list;
        }
    }

    if (referenceId == nullptr && nativeBest != nullptr)
        return;

    if (referenceId == nullptr)
    {
        result.push_back({ Tr("VISUAL QUALITY: not analyzed"),
                           Tr("No saved visual analysis against a native capture covers these versions. Capture them "
                              "and a native image in the same spot, then compare them in Visual analysis."),
                           1 });
        return;
    }

    std::sort(covered.begin(), covered.end(), [](const Covered& a, const Covered& b) { return a.ssim > b.ssim; });

    const auto name = [&](int index) { return analysis.candidates[index].name; };

    CompareConclusion conclusion;

    if (covered.size() == 1)
    {
        conclusion.title = Tr("VISUAL QUALITY: one version analyzed");
        conclusion.text = StrFmt(Tr("%s has an SSIM of %.4f against the native capture %s. The other versions have no "
                                    "analysis against that capture."),
                                 name(covered[0].candidate).c_str(), covered[0].ssim, referenceId->c_str());
    }
    else if (covered[0].ssim - covered[1].ssim > VisualSsimMargin)
    {
        conclusion.title = StrFmt(Tr("VISUALLY CLOSEST TO NATIVE: %s"), name(covered[0].candidate).c_str());
        conclusion.text = StrFmt(Tr("SSIM of %.4f against %.4f for %s, from still captures measured against %s."),
                                 covered[0].ssim, covered[1].ssim, name(covered[1].candidate).c_str(),
                                 referenceId->c_str());
        conclusion.level = 0;

        if (nativeBest != nullptr && covered.size() == analysis.candidates.size())
            *nativeBest = covered[0].candidate;

        if (analysis.performance.kind == IlionCompare::Kind::Winner)
        {
            if (analysis.performance.winner == covered[0].candidate)
            {
                conclusion.text += Tr(" It is also the performance winner.");
            }
            else
            {
                conclusion.text += StrFmt(Tr(" %s is faster: which one suits better depends on the priority."),
                                          name(analysis.performance.winner).c_str());
            }
        }
    }
    else
    {
        conclusion.title = Tr("VISUAL QUALITY: no clear difference");
        conclusion.text = StrFmt(Tr("%s and %s are %.4f of SSIM apart against the native capture %s, within the margin "
                                    "of %.4f."),
                                 name(covered[0].candidate).c_str(), name(covered[1].candidate).c_str(),
                                 covered[0].ssim - covered[1].ssim, referenceId->c_str(), VisualSsimMargin);
    }

    if (covered.size() > 1 && covered.size() < analysis.candidates.size())
        conclusion.text += Tr(" Not every version has an analysis against that capture.");

    conclusion.text += Tr(" ILION can't check that the captures are from the scene of the tests.");

    result.push_back(conclusion);
}

// What the measurements of every capture mean, and what they don't: four short lines and the limits of the
// comparison. The sorting is done by IlionVisualInterpret; nothing is measured again here and no capture is ever
// called better than another.
static void DrawVisualInterpretation(const IlionVisual::Capture& reference, const std::vector<VisualEntry>& entries)
{
    using Interpret = IlionVisualInterpret;

    const bool native = NeutralVisualReference(reference);
    const auto generated = [](const IlionVisual::Capture& capture)
    { return capture.frameGeneration != "Off" && !capture.frameGeneration.empty(); };
    const auto label = [](const IlionVisual::Capture& capture)
    {
        return StrFmt("%s  %s", VisualCaptureName(capture).c_str(),
                      capture.dateTime.empty() ? capture.id.c_str() : capture.dateTime.c_str());
    };

    // How much the picture changes by itself: other captures of the version of the reference
    double noise = -1.0;

    for (const auto& entry : entries)
    {
        if (entry.metrics != nullptr && entry.metrics->valid && !entry.metrics->Unreliable() &&
            SameVisualVersion(reference, *entry.capture))
            noise = (std::max)(noise, 1.0 - entry.metrics->ssim);
    }

    const ImVec4 titleColor = IlionTheme::industrial ? IlionTheme::TextTitle : ImGui::GetStyleColorVec4(ImGuiCol_Text);
    const ImVec4 cautionColor = toneMapColor(IlionTheme::Themed(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), IlionTheme::Role::Warn));

    for (const auto& entry : entries)
    {
        if (entry.metrics == nullptr)
            continue;

        const auto& m = *entry.metrics;
        const bool sameVersion = SameVisualVersion(reference, *entry.capture);

        Interpret::Conditions conditions;
        conditions.nativeReference = native;
        conditions.frameGeneration = generated(reference) || generated(*entry.capture);
        conditions.differentGame =
            !entry.capture->game.empty() && !reference.game.empty() && entry.capture->game != reference.game;
        conditions.hdrReduced = reference.hdr || entry.capture->hdr;
        conditions.sameVersionNoise = sameVersion ? -1.0 : noise;

        const auto reading = Interpret::Interpret(m, conditions);

        ImGui::TextColored(titleColor, Tr("%s against %s"), label(*entry.capture).c_str(),
                           VisualCaptureName(reference).c_str());

        if (!reading.measurable)
        {
            TextWrappedDisabled("%s", Tr("It can't be measured against the reference, see above."));
            continue;
        }

        // 1. How alike
        std::string similarity;

        switch (reading.similarity)
        {
        case Interpret::Similarity::Identical:
            similarity = Tr("1. Similarity: the two pictures are identical.");
            break;
        case Interpret::Similarity::VeryClose:
            similarity = StrFmt(Tr("1. Similarity: very close in structure (SSIM %.4f). A high SSIM is similar "
                                   "structure, not a better picture."),
                                m.ssim);
            break;
        case Interpret::Similarity::Close:
            similarity = StrFmt(Tr("1. Similarity: close, with differences that can be seen (SSIM %.4f)."), m.ssim);
            break;
        case Interpret::Similarity::Different:
            similarity = StrFmt(Tr("1. Similarity: clearly different in structure (SSIM %.4f)."), m.ssim);
            break;
        default:
            similarity = StrFmt(Tr("1. Similarity: very different (SSIM %.4f). Check that both show the same scene."),
                                m.ssim);
            break;
        }

        ImGui::TextWrapped("%s", similarity.c_str());

        // 2. Where, and what the edges do
        std::string where;

        if (reading.location == Interpret::Location::Concentrated)
        {
            where = StrFmt(Tr("2. Where: concentrated in one area (column %d, row %d, %.1f times the average). The "
                              "magnifier shows it pixel by pixel."),
                           m.worstRegionX + 1, m.worstRegionY + 1, reading.concentration);
        }
        else if (reading.location == Interpret::Location::Spread)
        {
            where = Tr("2. Where: spread over the picture, no area stands out much.");
        }
        else
        {
            where = Tr("2. Where: no area stands out.");
        }

        if (reading.edges == Interpret::Edges::SharperWithOvershoot)
        {
            where += StrFmt(Tr(" Edges %+.1f%% stronger with overshoot next to them (%.1f%%): it may be "
                               "over-sharpening or halos, not more fidelity."),
                            m.edgeChangePercent, m.overshootPercent);
        }
        else if (reading.edges == Interpret::Edges::Sharper)
        {
            where += StrFmt(Tr(" Edges %+.1f%% stronger: looking sharper is not the same as being more faithful."),
                            m.edgeChangePercent);
        }
        else if (reading.edges == Interpret::Edges::Softer)
        {
            where += StrFmt(Tr(" Edges %+.1f%% softer."), m.edgeChangePercent);
        }

        if (reading.edgesLost)
            where += StrFmt(Tr(" %.1f%% of the edges of the reference are kept."), m.edgesKeptPercent);

        ImGui::TextWrapped("%s", where.c_str());

        // 3. What else may be behind the differences
        std::string causes;

        if (reading.cameraMoved || reading.sceneChanged)
        {
            causes = Tr("3. Possible causes: the camera or the scene changed between the captures, which explains "
                        "differences by itself.");
        }
        else if (reading.manySmallDifferences)
        {
            causes = StrFmt(Tr("3. Possible causes: %.1f%% of the pixels differ but only %.1f%% clearly. Many small "
                               "differences are typical of light, water, vegetation, particles, TAA or what changes "
                               "from one frame to the next; they can't be put down to the upscaler."),
                            m.differentPercent, m.clearlyPercent);
        }
        else if (m.identical)
        {
            causes = Tr("3. Possible causes: none, nothing differs.");
        }
        else
        {
            causes = Tr("3. Possible causes: these captures alone don't separate the upscaler from what changes in "
                        "the scene between two moments.");
        }

        if (conditions.sameVersionNoise >= 0.0)
        {
            causes += StrFmt(Tr(" Two captures of one same version differ here by %.4f of SSIM."),
                             conditions.sameVersionNoise);
        }

        if (reading.generatedFrame)
            causes += Tr(" Frame generation was on: the frame may be a generated one.");

        ImGui::TextWrapped("%s", causes.c_str());

        // 4. What can be said
        std::string conclusion;

        if (reading.verdict == Interpret::Verdict::Inconclusive)
        {
            std::string reasons;
            const auto reason = [&](bool applies, const char* text)
            {
                if (applies)
                    reasons += (reasons.empty() ? "" : "; ") + std::string(text);
            };

            reason(reading.cameraMoved, Tr("the camera moved"));
            reason(reading.sceneChanged, Tr("they don't seem to show the same scene"));
            reason(reading.generatedFrame, Tr("a frame may be a generated one"));
            reason(reading.differentGame, Tr("they are from different games"));

            conclusion = StrFmt(Tr("4. Conclusion: inconclusive (%s)."), reasons.c_str());
        }
        else if (reading.verdict == Interpret::Verdict::Equivalent)
        {
            conclusion = Tr("4. Conclusion: roughly equivalent in these still captures.");
        }
        else
        {
            conclusion = Tr("4. Conclusion: a difference that can be detected in these still captures.");
        }

        if (reading.verdict != Interpret::Verdict::Inconclusive && !m.identical)
        {
            conclusion += Tr(reading.comparableToNative
                                 ? " It is measured against a native picture: being closer to it is not by itself "
                                   "looking better."
                                 : " Without a native reference this doesn't tell which one is more faithful.");
        }

        if (reading.verdict == Interpret::Verdict::Inconclusive)
            ImGui::PushStyleColor(ImGuiCol_Text, cautionColor);

        ImGui::TextWrapped("%s", conclusion.c_str());

        if (reading.verdict == Interpret::Verdict::Inconclusive)
            ImGui::PopStyleColor();

        // What limits this comparison. A list, not a score.
        std::string limits;
        const auto limit = [&](bool applies, const std::string& text)
        {
            if (applies)
                limits += (limits.empty() ? "" : "; ") + text;
        };

        limit(!native, Tr("no native reference"));
        limit(m.shifted, StrFmt(Tr("displacement of %d, %d px"), m.shiftX, m.shiftY));
        limit(conditions.frameGeneration, Tr("frame generation on"));
        limit(conditions.hdrReduced, Tr("HDR10 values reduced to 8 bit"));
        limit(!sameVersion && noise < 0.0, Tr("no second capture of one same version to know how much the picture "
                                               "changes by itself"));
        limit(true, Tr("still pictures only, motion isn't measured"));

        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextWrapped(Tr("Limits of this comparison: %s."), limits.c_str());
        ImGui::PopStyleColor();

        ImGui::Spacing();
    }
}

static void DrawVisualCompareWindow(float scale, ImGuiIO& io)
{
    using Capture = IlionVisual::Capture;
    using Metrics = IlionVisual::Metrics;
    using Origin = IlionVisual::Origin;

    static std::vector<Capture> captures;
    static std::vector<std::string> selectedIds; // in the order they were picked
    static std::string referenceId;
    static std::string panelIds[2];
    static int viewMode = 0;      // 0 = side by side, 1 = the right picture is the difference
    static int differenceGain = 8;
    static double viewZoom = 0.0; // 0 = the whole picture
    static double viewCenterX = 0.0, viewCenterY = 0.0;
    static bool holding = false;

    // Magnifier: a small area of both pictures, the same one, with every pixel drawn as a square
    static bool magnifier = false;
    static int magnifierLevel = 4;
    static bool magnifierPinned = false;
    static bool magnifierPlaced = false;
    static double magnifierX = 0.0, magnifierY = 0.0; // in pixels of the picture

    auto& work = GetVisualWork();

    MaintainVisualTextures();

    if (!showVisualCompare)
    {
        // Closed: the pictures and their textures are let go, the selection stays
        if (holding)
        {
            holding = false;

            for (auto& panel : visualPanels)
            {
                RetireVisualTexture(panel.texture);
                panel.shown.clear();
            }

            std::scoped_lock lock(work.mutex);
            work.images.clear();
            work.metrics.clear();
        }

        return;
    }

    holding = true;

    if (visualCompareReload)
    {
        visualCompareReload = false;
        captures = IlionVisual::ListCaptures(CaptureFolder());
        PruneVisualResults(captures);

        std::erase_if(selectedIds, [&](const std::string& id)
                      { return std::none_of(captures.begin(), captures.end(), [&](const Capture& c) { return c.id == id; }); });
    }

    ImGui::SetNextWindowSize(ImVec2(std::min(1180.0f * scale, io.DisplaySize.x - 20.0f),
                                    std::min(860.0f * scale, io.DisplaySize.y - 20.0f)),
                             ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_FirstUseEver,
                            ImVec2(0.5f, 0.5f));

    if (!ImGui::Begin(TrL("Visual comparison of captures"), &showVisualCompare,
                      ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse))
    {
        ImGui::End();
        return;
    }

    TextWrappedDisabled("%s", Tr("Still captures against a reference you pick. What happens in motion is not measured."));

    if (IlionTheme::LampButton(TrL("Back to Benchmark"), IlionTheme::Lamp::Neutral))
        showVisualCompare = false;

    SameLineIfRoom(IlionTheme::LampButtonWidth(TrL("Reload")));

    if (IlionTheme::LampButton(TrL("Reload"), IlionTheme::Lamp::Neutral))
        visualCompareReload = true;

    SameLineIfRoom(IlionTheme::LampButtonWidth(TrL("Clear selection")));

    if (IlionTheme::LampButton(TrL("Clear selection"), IlionTheme::Lamp::Neutral))
        selectedIds.clear();

    ImGui::SameLine();

    // Not while a picture of the selection may be open in the thread
    ImGui::BeginDisabled(selectedIds.empty() || work.busy.load());

    if (IlionTheme::DangerButton(TrL("Delete captures")))
        ImGui::OpenPopup("##DeleteCaptures");

    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    {
        WrappedTooltip("%s", Tr("Deletes the selected captures for good: their PNG, their data and their saved "
                                   "measurements.\nClear selection only unticks them."));
    }

    ImGui::EndDisabled();

    if (ImGui::BeginPopupModal("##DeleteCaptures", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar |
                                   ImGuiWindowFlags_NoSavedSettings))
    {
        ImGui::Text(Tr("Delete %d selected captures for good?"), (int) selectedIds.size());
        ImGui::TextDisabled("%s", Tr("Their files are removed from disk. This can't be undone."));
        ImGui::Spacing();

        if (IlionTheme::DangerButton(TrL("Delete")))
        {
            const int removed = IlionVisual::DeleteCaptures(captures, selectedIds);
            IlionVisual::ForgetResults(CaptureFolder(), selectedIds);

            LOG_INFO("[CAPTURE] deleted {} of {} selected captures", removed, selectedIds.size());

            selectedIds.clear();
            visualCompareReload = true;
            ImGui::CloseCurrentPopup();
        }

        SameLineIfRoom(IlionTheme::LampButtonWidth(TrL("Cancel")));

        if (IlionTheme::LampButton(TrL("Cancel"), IlionTheme::Lamp::Neutral))
            ImGui::CloseCurrentPopup();

        ImGui::EndPopup();
    }

    ImGui::SameLine();
    ImGui::TextDisabled(Tr("%d captures, %d selected (up to %d)"), (int) captures.size(), (int) selectedIds.size(),
                        (int) VisualMaxSelected);

    if (captures.empty())
    {
        ImGui::Spacing();
        ImGui::TextWrapped("%s", Tr("No captures yet. Use Capture image in the Benchmark tab, or copy PNG files into "
                                    "ILION\\Captures."));
        ImGui::End();
        return;
    }

    const auto isSelected = [&](const std::string& id)
    { return std::find(selectedIds.begin(), selectedIds.end(), id) != selectedIds.end(); };

    const auto originText = [](const Capture& capture)
    {
        const Origin origin = capture.GetOrigin();
        return Tr(origin == Origin::Native ? "Native" : origin == Origin::Upscaled ? "Upscaled" : "External");
    };

    // Captures, newest first
    const float listHeight = ImGui::GetTextLineHeightWithSpacing() * 6.5f;

    if (ImGui::BeginTable("visualCaptures", 7,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY |
                              ImGuiTableFlags_SizingFixedFit,
                          ImVec2(0.0f, listHeight)))
    {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("##pick");
        ImGui::TableSetupColumn(Tr("Capture"));
        ImGui::TableSetupColumn(Tr("Date and time"));
        ImGui::TableSetupColumn(Tr("Type"));
        ImGui::TableSetupColumn(Tr("Size"));
        ImGui::TableSetupColumn(Tr("Resolution"));
        ImGui::TableSetupColumn("Frame Generation", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        for (int i = (int) captures.size() - 1; i >= 0; i--)
        {
            const auto& capture = captures[i];

            ImGui::TableNextRow();
            ImGui::TableNextColumn();

            bool picked = isSelected(capture.id);

            // The record's own id: a row number alone is the same id the header of the column gets
            ImGui::PushID(StrFmt("capture_%s_%d", capture.id.c_str(), i).c_str());
            ImGui::BeginDisabled(!picked && selectedIds.size() >= VisualMaxSelected);

            if (ImGui::Checkbox("##select", &picked))
            {
                if (picked)
                    selectedIds.push_back(capture.id);
                else
                    std::erase(selectedIds, capture.id);
            }

            ImGui::EndDisabled();
            ImGui::PopID();

            ImGui::TableNextColumn();
            ImGui::TextUnformatted(VisualCaptureName(capture).c_str());

            if (ImGui::IsItemHovered())
                WrappedTooltip("%s", capture.path.filename().string().c_str());

            ImGui::TableNextColumn();
            ImGui::TextUnformatted(capture.dateTime.empty() ? "-" : capture.dateTime.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(originText(capture));
            ImGui::TableNextColumn();
            ImGui::Text("%dx%d%s", capture.width, capture.height, capture.hdr ? " HDR10" : "");
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(capture.resolution.empty() ? "-" : capture.resolution.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(capture.frameGeneration.empty() ? "-" : capture.frameGeneration.c_str());
        }

        ImGui::EndTable();
    }

    // Selected captures in the order they were picked
    std::vector<const Capture*> picked;

    for (const auto& id : selectedIds)
    {
        for (const auto& capture : captures)
        {
            if (capture.id == id)
                picked.push_back(&capture);
        }
    }

    // Pictures and measurements the thread has ready. Pictures that aren't selected anymore are let go.
    std::map<std::string, std::shared_ptr<const IlionVisual::Image>> images;
    std::map<std::string, Metrics> metrics;

    {
        std::scoped_lock lock(work.mutex);
        std::erase_if(work.images, [&](const auto& item) { return !isSelected(item.first); });
        images = work.images;
        metrics = work.metrics;
    }

    if (picked.size() < 2)
    {
        ImGui::Spacing();
        ImGui::TextWrapped("%s", Tr("Select two or more captures to compare them."));
        ImGui::End();
        return;
    }

    const auto find = [&](const std::string& id) -> const Capture*
    {
        for (const auto* capture : picked)
        {
            if (capture->id == id)
                return capture;
        }

        return nullptr;
    };

    // The reference is the user's choice. A native capture is offered first, never one version over another.
    if (find(referenceId) == nullptr)
    {
        referenceId = picked.front()->id;

        for (const auto* capture : picked)
        {
            if (NeutralVisualReference(*capture))
            {
                referenceId = capture->id;
                break;
            }
        }
    }

    const Capture* reference = find(referenceId);

    const auto columnName = [&](const Capture& capture)
    {
        return StrFmt("%s  %s", VisualCaptureName(capture).c_str(),
                      capture.dateTime.empty() ? capture.id.c_str() : capture.dateTime.c_str());
    };

    ImGui::Spacing();
    ImGui::SetNextItemWidth(360.0f * scale);

    if (IlionTheme::BeginCombo(TrL("Reference"), columnName(*reference).c_str()))
    {
        for (const auto* capture : picked)
        {
            if (ImGui::Selectable(StrFmt("%s##%s", columnName(*capture).c_str(), capture->id.c_str()).c_str(),
                                  capture->id == referenceId))
                referenceId = capture->id;
        }

        ImGui::EndCombo();
    }

    ImGui::SameLine();
    ImGui::TextDisabled("%s", Tr(NeutralVisualReference(*reference)
                                     ? "Native image: the measurements tell how close each capture is to it."
                                     : "Not a native image without upscaler: the measurements only tell how much the "
                                       "captures differ."));

    reference = find(referenceId);

    // One thing at a time for the thread: first the pictures, then the measurements against the reference
    if (!work.busy.load())
    {
        const Capture* toLoad = nullptr;

        for (const auto* capture : picked)
        {
            if (!images.contains(capture->id))
            {
                toLoad = capture;
                break;
            }
        }

        if (toLoad != nullptr)
        {
            StartVisualLoad(*toLoad);
        }
        else
        {
            for (const auto* capture : picked)
            {
                const std::string key = VisualPairKey(referenceId, capture->id);

                if (capture == reference || metrics.contains(key))
                    continue;

                SYSTEMTIME time;
                GetLocalTime(&time);

                IlionVisual::SavedResult saved;
                saved.dateTime = StrFmt("%04d-%02d-%02d %02d:%02d:%02d", time.wYear, time.wMonth, time.wDay,
                                        time.wHour, time.wMinute, time.wSecond);
                saved.game = capture->game;
                saved.referenceId = reference->id;
                saved.referenceName = reference->Name();
                saved.referenceSha256 = reference->dllSha256;
                // Only a neutral native picture counts as such for the conclusions of the benchmarks
                saved.referenceOrigin = NeutralVisualReference(*reference)                    ? "native"
                                        : reference->GetOrigin() == Origin::Native ? "native_by_version"
                                                                                   : IlionVisual::OriginText(reference->GetOrigin());
                saved.testId = capture->id;
                saved.testName = capture->Name();
                saved.testSha256 = capture->dllSha256;

                StartVisualMeasure(key, images[referenceId], images[capture->id], saved, CaptureFolder());
                break;
            }
        }
    }

    // --- Comparator: two pictures that always show the same part ---------------------------------------------
    IlionTheme::SectionPlate(Tr("Comparator"));

    if (find(panelIds[0]) == nullptr)
        panelIds[0] = referenceId;

    if (find(panelIds[1]) == nullptr || panelIds[1] == panelIds[0])
    {
        for (const auto* capture : picked)
        {
            if (capture->id != panelIds[0])
            {
                panelIds[1] = capture->id;
                break;
            }
        }
    }

    const IlionVisual::Image* pictures[2] = { nullptr, nullptr };
    bool unreadable[2] = { false, false };

    for (int i = 0; i < 2; i++)
    {
        const auto found = images.find(panelIds[i]);

        if (found == images.end())
            continue;

        if (found->second->Empty())
            unreadable[i] = true;
        else
            pictures[i] = found->second.get();
    }

    IlionTheme::RadioButton(TrL("Side by side"), &viewMode, 0);
    ImGui::SameLine();
    IlionTheme::RadioButton(TrL("Difference"), &viewMode, 1);

    if (ImGui::IsItemHovered())
    {
        WrappedTooltip("%s", Tr("The right picture shows where the two captures differ: black is equal,\n"
                                   "brighter is more different. The amplification makes small differences visible."));
    }

    if (viewMode == 1)
    {
        static const int gains[] = { 1, 4, 8, 16, 32 };

        ImGui::SameLine();
        ImGui::SetNextItemWidth(80.0f * scale);

        if (IlionTheme::BeginCombo("##gain", StrFmt("x%d", differenceGain).c_str()))
        {
            for (const int gain : gains)
            {
                if (ImGui::Selectable(StrFmt("x%d", gain).c_str(), gain == differenceGain))
                    differenceGain = gain;
            }

            ImGui::EndCombo();
        }
    }

    SameLineIfRoom(ButtonWidth(TrL("Fit")), 16.0f * scale);

    if (IlionTheme::Button(TrL("Fit")))
        viewZoom = 0.0;

    for (const int level : { 1, 2, 4, 8 })
    {
        ImGui::SameLine();

        if (IlionTheme::Button(StrFmt(level == 1 ? "1:1" : "%dx", level).c_str()))
            viewZoom = level;
    }

    // The part with the biggest difference between the two captures that are shown, when it was measured
    {
        const Metrics* pair = nullptr;

        if (const auto it = metrics.find(VisualPairKey(panelIds[0], panelIds[1])); it != metrics.end())
            pair = &it->second;
        else if (const auto it2 = metrics.find(VisualPairKey(panelIds[1], panelIds[0])); it2 != metrics.end())
            pair = &it2->second;

        if (pair != nullptr && pair->valid && !pair->identical && pictures[0] != nullptr)
        {
            SameLineIfRoom(ButtonWidth(TrL("Go to the most different area")), 16.0f * scale);

            if (IlionTheme::Button(TrL("Go to the most different area")))
            {
                viewCenterX = (pair->worstRegionX + 0.5) * pictures[0]->width / Metrics::GridX;
                viewCenterY = (pair->worstRegionY + 0.5) * pictures[0]->height / Metrics::GridY;
                viewZoom = (std::max)(1.0, viewZoom);
            }
        }
    }

    IlionTheme::Checkbox(TrL("Magnifier"), &magnifier);

    if (ImGui::IsItemHovered())
    {
        WrappedTooltip("%s", Tr("Shows the same small area of both pictures enlarged, every pixel as a square,\n"
                                   "nothing smoothed. The captures themselves are not changed."));
    }

    if (magnifier)
    {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(80.0f * scale);

        if (IlionTheme::BeginCombo("##magnifierLevel", StrFmt("%dx", magnifierLevel).c_str()))
        {
            for (const int level : { 2, 4, 8 })
            {
                if (ImGui::Selectable(StrFmt("%dx", level).c_str(), level == magnifierLevel))
                    magnifierLevel = level;
            }

            ImGui::EndCombo();
        }

        ImGui::SameLine(0.0f, 16.0f * scale);
        ImGui::TextDisabled("%s", Tr(magnifierPinned ? "Area pinned. Right click on a picture: release it."
                                                     : "Move the pointer over a picture. Right click: pin the area."));
    }
    else
    {
        // Its two textures are let go while it isn't used
        for (int i = 2; i < 4; i++)
        {
            RetireVisualTexture(visualPanels[i].texture);
            visualPanels[i].shown.clear();
        }

        magnifierPinned = false;
        magnifierPlaced = false;
    }

    const IlionVisual::Image* base = pictures[0] != nullptr ? pictures[0] : pictures[1];

    if (base == nullptr)
    {
        ImGui::TextDisabled("%s", Tr(unreadable[0] || unreadable[1] ? "This PNG can't be read" : "Loading the captures..."));
    }
    else
    {
        const float gap = ImGui::GetStyle().ItemSpacing.x;
        const int panelWidth = (std::max)(128, (int) ((ImGui::GetContentRegionAvail().x - gap) * 0.5f));
        const int panelHeight = std::clamp((int) (panelWidth * (double) base->height / base->width), 128,
                                           (std::max)(128, (int) (io.DisplaySize.y * 0.45f)));

        const double fitZoom =
            (std::min)((double) panelWidth / base->width, (double) panelHeight / base->height);
        const double zoom = viewZoom > 0.0 ? viewZoom : fitZoom;

        if (viewZoom <= 0.0)
        {
            viewCenterX = base->width * 0.5;
            viewCenterY = base->height * 0.5;
        }

        viewCenterX = std::clamp(viewCenterX, 0.0, (double) base->width);
        viewCenterY = std::clamp(viewCenterY, 0.0, (double) base->height);

        ImGui::SameLine(0.0f, 16.0f * scale);
        ImGui::TextDisabled(Tr("Zoom %.0f%%. Wheel: zoom, drag: move."), zoom * 100.0);

        // Magnifier: a square of whole pixels of the picture, an even number of them, each drawn level x level.
        // Its middle is a whole pixel and it never leaves the picture.
        const int magnifierCells =
            (std::max)(8, ((std::min)(panelWidth, (int) (320.0f * scale)) / magnifierLevel) / 2 * 2);
        const int magnifierSide = magnifierCells * magnifierLevel;
        const double magnifierHalf = magnifierCells * 0.5;

        const auto insidePicture = [&](double value, int length)
        {
            if (length <= magnifierCells)
                return std::floor(length * 0.5);

            return std::clamp(std::floor(value), magnifierHalf, (double) length - magnifierHalf);
        };

        const double magnifiedX = insidePicture(magnifierX, base->width);
        const double magnifiedY = insidePicture(magnifierY, base->height);
        float panelLeft[2] = { 0.0f, 0.0f };

        // Changes of the view apply to both pictures on the next frame
        double nextZoom = viewZoom, nextCenterX = viewCenterX, nextCenterY = viewCenterY;

        for (int i = 0; i < 2; i++)
        {
            auto& panel = visualPanels[i];
            const bool difference = i == 1 && viewMode == 1;

            if (i == 1)
                ImGui::SameLine();

            ImGui::BeginGroup();
            ImGui::PushID(i);

            const Capture* shownCapture = find(panelIds[i]);
            std::string label = shownCapture != nullptr ? columnName(*shownCapture) : std::string("-");

            if (difference)
                label = StrFmt(Tr("Difference with %s"), label.c_str());

            ImGui::SetNextItemWidth((float) panelWidth);

            if (IlionTheme::BeginCombo("##capture", label.c_str()))
            {
                for (const auto* capture : picked)
                {
                    if (ImGui::Selectable(StrFmt("%s##%s", columnName(*capture).c_str(), capture->id.c_str()).c_str(),
                                          capture->id == panelIds[i]))
                    {
                        // Picking the one of the other side swaps them
                        if (capture->id == panelIds[1 - i])
                            panelIds[1 - i] = panelIds[i];

                        panelIds[i] = capture->id;
                    }
                }

                ImGui::EndCombo();
            }

            const ImVec2 corner = ImFloor(ImGui::GetCursorScreenPos());
            const ImVec2 size((float) panelWidth, (float) panelHeight);
            const char* message = nullptr;
            bool drawn = false;

            panelLeft[i] = corner.x;

            const std::string view =
                StrFmt("%.2f|%.2f|%.4f|%d|%d", viewCenterX, viewCenterY, zoom, panelWidth, panelHeight);

            if (difference)
            {
                if (pictures[0] == nullptr || pictures[1] == nullptr)
                {
                    message = unreadable[0] || unreadable[1] ? "This PNG can't be read" : "Loading the captures...";
                }
                else if (pictures[0]->width != pictures[1]->width || pictures[0]->height != pictures[1]->height)
                {
                    message = "The two captures don't have the same size";
                }
                else
                {
                    UpdateVisualPanel(panel, panelWidth, panelHeight,
                                      StrFmt("d|%s|%s|%d|%s", panelIds[0].c_str(), panelIds[1].c_str(), differenceGain,
                                             view.c_str()),
                                      [&](uint8_t* out)
                                      {
                                          IlionVisual::RenderDifference(*pictures[0], *pictures[1], viewCenterX,
                                                                        viewCenterY, zoom, differenceGain, panelWidth,
                                                                        panelHeight, out);
                                      });
                    drawn = true;
                }
            }
            else if (pictures[i] == nullptr)
            {
                message = unreadable[i] ? "This PNG can't be read" : "Loading the captures...";
            }
            else
            {
                UpdateVisualPanel(panel, panelWidth, panelHeight, StrFmt("v|%s|%s", panelIds[i].c_str(), view.c_str()),
                                  [&](uint8_t* out)
                                  {
                                      IlionVisual::RenderView(*pictures[i], viewCenterX, viewCenterY, zoom, panelWidth,
                                                              panelHeight, out);
                                  });
                drawn = true;
            }

            ImGui::SetCursorScreenPos(corner);

            // The texture exists from the frame after it was asked for
            if (drawn && panel.texture != nullptr && panel.texture->GetTexID() != ImTextureID_Invalid)
            {
                ImGui::Image(panel.texture->GetTexRef(), size, ImVec2(0.0f, 0.0f),
                             ImVec2((float) panelWidth / panel.texture->Width, (float) panelHeight / panel.texture->Height));
            }
            else
            {
                ImGui::Dummy(size);
                ImGui::GetWindowDrawList()->AddRectFilled(corner, ImVec2(corner.x + size.x, corner.y + size.y),
                                                          IM_COL32(18, 18, 20, 255));

                if (message != nullptr)
                {
                    ImGui::GetWindowDrawList()->AddText(ImVec2(corner.x + 8.0f * scale, corner.y + 8.0f * scale),
                                                        ImGui::GetColorU32(ImGuiCol_TextDisabled), Tr(message));
                }
            }

            // Dragging moves the picture, the wheel zooms on what is under the pointer
            ImGui::SetCursorScreenPos(corner);
            ImGui::InvisibleButton("##view", size);
            ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);

            if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f))
            {
                nextCenterX -= io.MouseDelta.x / zoom;
                nextCenterY -= io.MouseDelta.y / zoom;

                // Moving a picture that is shown whole keeps its zoom
                if (nextZoom <= 0.0)
                    nextZoom = fitZoom;
            }

            if (ImGui::IsItemHovered() && io.MouseWheel != 0.0f)
            {
                double wanted = io.MouseWheel > 0.0f ? zoom * 2.0 : zoom * 0.5;

                // On the way up it stops at one pixel per pixel, from there on whole pixels
                if (io.MouseWheel > 0.0f && zoom < 1.0)
                    wanted = (std::min)(1.0, wanted);

                wanted = (std::min)(wanted, 16.0);

                if (io.MouseWheel < 0.0f && wanted <= fitZoom * 1.001)
                {
                    nextZoom = 0.0;
                }
                else
                {
                    const double pointerX = io.MousePos.x - (corner.x + size.x * 0.5);
                    const double pointerY = io.MousePos.y - (corner.y + size.y * 0.5);

                    nextCenterX = viewCenterX + pointerX / zoom - pointerX / wanted;
                    nextCenterY = viewCenterY + pointerY / zoom - pointerY / wanted;
                    nextZoom = wanted;
                }
            }

            // Magnifier: the area follows the pointer over either picture, a right click pins it or lets it go
            if (magnifier)
            {
                if (ImGui::IsItemHovered())
                {
                    const bool rightClick = ImGui::IsMouseClicked(ImGuiMouseButton_Right);

                    if (rightClick)
                        magnifierPinned = !magnifierPinned;

                    if (rightClick || !magnifierPinned)
                    {
                        magnifierX = viewCenterX + (io.MousePos.x - (corner.x + size.x * 0.5)) / zoom;
                        magnifierY = viewCenterY + (io.MousePos.y - (corner.y + size.y * 0.5)) / zoom;
                        magnifierPlaced = true;
                    }
                }

                if (magnifierPlaced)
                {
                    // The same area marked on both pictures
                    const ImVec2 areaMin(
                        std::floor(corner.x + size.x * 0.5f + (float) ((magnifiedX - magnifierHalf - viewCenterX) * zoom)),
                        std::floor(corner.y + size.y * 0.5f + (float) ((magnifiedY - magnifierHalf - viewCenterY) * zoom)));
                    const ImVec2 areaMax(areaMin.x + (std::max)(2.0f, (float) (magnifierCells * zoom)),
                                         areaMin.y + (std::max)(2.0f, (float) (magnifierCells * zoom)));

                    auto drawList = ImGui::GetWindowDrawList();

                    drawList->PushClipRect(corner, ImVec2(corner.x + size.x, corner.y + size.y), true);
                    drawList->AddRect(ImVec2(areaMin.x - 1.0f, areaMin.y - 1.0f), ImVec2(areaMax.x + 1.0f, areaMax.y + 1.0f),
                                      IM_COL32(0, 0, 0, 220), 0.0f, 0, 3.0f);
                    drawList->AddRect(ImVec2(areaMin.x - 1.0f, areaMin.y - 1.0f), ImVec2(areaMax.x + 1.0f, areaMax.y + 1.0f),
                                      magnifierPinned ? IM_COL32(255, 204, 0, 255) : IM_COL32(255, 255, 255, 235));
                    drawList->PopClipRect();
                }
            }

            ImGui::PopID();
            ImGui::EndGroup();
        }

        viewZoom = nextZoom;
        viewCenterX = nextCenterX;
        viewCenterY = nextCenterY;

        // --- Magnifier: what is inside the marked area, under each picture -----------------------------------
        if (magnifier && magnifierPlaced)
        {
            const bool sizesDiffer = pictures[0] != nullptr && pictures[1] != nullptr &&
                                     (pictures[0]->width != pictures[1]->width || pictures[0]->height != pictures[1]->height);

            const Metrics* pair = nullptr;

            if (const auto it = metrics.find(VisualPairKey(panelIds[0], panelIds[1])); it != metrics.end())
                pair = &it->second;
            else if (const auto it2 = metrics.find(VisualPairKey(panelIds[1], panelIds[0])); it2 != metrics.end())
                pair = &it2->second;

            ImGui::Spacing();
            ImGui::Text(Tr("Magnifier %dx: %d x %d pixels around %d, %d"), magnifierLevel, magnifierCells, magnifierCells,
                        (int) magnifiedX, (int) magnifiedY);

            const ImVec4 notAlignedColor = toneMapColor(IlionTheme::Themed(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), IlionTheme::Role::Warn));

            // The two areas are the same coordinates. Whether they are the same part of the scene is another thing.
            if (sizesDiffer)
            {
                TextWrappedColored(notAlignedColor, "%s",
                                   Tr("The two captures don't have the same size: the magnifier shows the same "
                                      "coordinates, which may not be the same part of the picture."));
            }
            else if (pair != nullptr && pair->valid && pair->sceneMismatch)
            {
                TextWrappedColored(notAlignedColor, "%s",
                                   Tr("These captures don't seem to show the same scene: the magnifier may show "
                                      "different parts in each one."));
            }
            else if (pair != nullptr && pair->valid && pair->shifted)
            {
                TextWrappedColored(notAlignedColor,
                                   Tr("These captures are displaced %d, %d pixels from each other: the magnifier may "
                                      "show different parts in each one."),
                                   pair->shiftX, pair->shiftY);
            }
            else if (pair == nullptr)
            {
                TextWrappedDisabled("%s", Tr("Whether these two captures are aligned hasn't been measured: the "
                                             "magnifier shows the same coordinates in both."));
            }

            const ImVec2 rowStart = ImFloor(ImGui::GetCursorScreenPos());
            const ImVec2 magnifiedSize((float) magnifierSide, (float) magnifierSide);
            const float textHeight = ImGui::GetTextLineHeightWithSpacing();
            auto drawList = ImGui::GetWindowDrawList();

            for (int i = 0; i < 2; i++)
            {
                auto& panel = visualPanels[2 + i];
                const bool difference = i == 1 && viewMode == 1;
                const ImVec2 corner(std::floor(panelLeft[i] + (float) (panelWidth - magnifierSide) * 0.5f), rowStart.y);
                bool drawn = false;

                const std::string view = StrFmt("%.0f|%.0f|%d|%d", magnifiedX, magnifiedY, magnifierLevel, magnifierSide);

                if (difference)
                {
                    if (pictures[0] != nullptr && pictures[1] != nullptr && !sizesDiffer)
                    {
                        UpdateVisualPanel(panel, magnifierSide, magnifierSide,
                                          StrFmt("md|%s|%s|%d|%s", panelIds[0].c_str(), panelIds[1].c_str(), differenceGain,
                                                 view.c_str()),
                                          [&](uint8_t* out)
                                          {
                                              IlionVisual::RenderDifference(*pictures[0], *pictures[1], magnifiedX,
                                                                            magnifiedY, magnifierLevel, differenceGain,
                                                                            magnifierSide, magnifierSide, out);
                                          });
                        drawn = true;
                    }
                }
                else if (pictures[i] != nullptr)
                {
                    UpdateVisualPanel(panel, magnifierSide, magnifierSide,
                                      StrFmt("mv|%s|%s", panelIds[i].c_str(), view.c_str()),
                                      [&](uint8_t* out)
                                      {
                                          IlionVisual::RenderView(*pictures[i], magnifiedX, magnifiedY, magnifierLevel,
                                                                  magnifierSide, magnifierSide, out);
                                      });
                    drawn = true;
                }

                ImGui::SetCursorScreenPos(corner);

                // Drawn one to one: every pixel of the picture is a square of level x level, as it is
                if (drawn && panel.texture != nullptr && panel.texture->GetTexID() != ImTextureID_Invalid)
                {
                    ImGui::Image(panel.texture->GetTexRef(), magnifiedSize, ImVec2(0.0f, 0.0f),
                                 ImVec2((float) magnifierSide / panel.texture->Width,
                                        (float) magnifierSide / panel.texture->Height));
                }
                else
                {
                    ImGui::Dummy(magnifiedSize);
                    drawList->AddRectFilled(corner, ImVec2(corner.x + magnifiedSize.x, corner.y + magnifiedSize.y),
                                            IM_COL32(18, 18, 20, 255));
                }

                drawList->AddRect(ImVec2(corner.x - 1.0f, corner.y - 1.0f),
                                  ImVec2(corner.x + magnifiedSize.x + 1.0f, corner.y + magnifiedSize.y + 1.0f),
                                  ImGui::GetColorU32(ImGuiCol_Border));

                // The pixel in the middle, and its values as they are in the capture
                const ImVec2 middle(corner.x + magnifierSide * 0.5f, corner.y + magnifierSide * 0.5f);

                drawList->AddRect(ImVec2(middle.x - 1.0f, middle.y - 1.0f),
                                  ImVec2(middle.x + magnifierLevel + 1.0f, middle.y + magnifierLevel + 1.0f),
                                  IM_COL32(0, 0, 0, 200));
                drawList->AddRect(middle, ImVec2(middle.x + magnifierLevel, middle.y + magnifierLevel),
                                  IM_COL32(255, 255, 255, 220));

                ImGui::SetCursorScreenPos(ImVec2(corner.x, corner.y + magnifiedSize.y + 2.0f));

                const int pixelX = (int) magnifiedX, pixelY = (int) magnifiedY;

                if (!difference && pictures[i] != nullptr && pixelX >= 0 && pixelY >= 0 && pixelX < pictures[i]->width &&
                    pixelY < pictures[i]->height)
                {
                    const uint8_t* pixel =
                        pictures[i]->rgba.data() + ((size_t) pixelY * pictures[i]->width + pixelX) * 4;

                    ImGui::TextDisabled(Tr("Pixel in the middle: R %d  G %d  B %d"), pixel[0], pixel[1], pixel[2]);
                }
                else
                {
                    ImGui::TextDisabled("%s", difference ? Tr("Difference, amplified") : "-");
                }
            }

            // On with what comes under the two of them
            ImGui::SetCursorScreenPos(ImVec2(rowStart.x, rowStart.y + magnifiedSize.y + 2.0f + textHeight));
            ImGui::Dummy(ImVec2(0.0f, 0.0f));
        }
    }

    // --- Measurements of every capture against the reference -------------------------------------------------
    std::vector<VisualEntry> entries;
    bool pending = false;

    for (const auto* capture : picked)
    {
        if (capture == reference)
            continue;

        VisualEntry entry;
        entry.capture = capture;

        if (const auto it = metrics.find(VisualPairKey(referenceId, capture->id)); it != metrics.end())
            entry.metrics = &it->second;
        else
            pending = true;

        entries.push_back(entry);
    }

    IlionTheme::SectionPlate(Tr("Visual summary"));

    if (pending)
    {
        ImGui::TextDisabled("%s", Tr("Measuring..."));
    }
    else
    {
        for (const auto& conclusion : BuildVisualSummary(*reference, entries))
        {
            const ImVec4 color = conclusion.level == 0   ? IlionTheme::Themed(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), IlionTheme::Role::Good)
                                 : conclusion.level == 2 ? IlionTheme::Themed(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), IlionTheme::Role::Warn)
                                                         : ImGui::GetStyleColorVec4(ImGuiCol_Text);

            ImGui::TextColored(conclusion.level == 1 ? color : toneMapColor(color), "%s", conclusion.title.c_str());
            ImGui::TextWrapped("%s", conclusion.text.c_str());
        }
    }

    // What keeps a capture from being measured, or makes its numbers doubtful
    const ImVec4 blockingColor = toneMapColor(IlionTheme::Themed(ImVec4(1.0f, 0.4f, 0.3f, 1.0f), IlionTheme::Role::Bad));
    const ImVec4 warningColor = toneMapColor(IlionTheme::Themed(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), IlionTheme::Role::Warn));

    bool generatedFrames = reference->frameGeneration != "Off" && !reference->frameGeneration.empty();
    bool differentGame = false;
    bool hdrValues = reference->hdr;

    for (const auto& entry : entries)
    {
        const std::string name = columnName(*entry.capture);

        generatedFrames |= entry.capture->frameGeneration != "Off" && !entry.capture->frameGeneration.empty();
        differentGame |= !entry.capture->game.empty() && !reference->game.empty() && entry.capture->game != reference->game;
        hdrValues |= entry.capture->hdr;

        if (entry.metrics == nullptr)
            continue;

        if (entry.metrics->sizeMismatch)
        {
            TextWrappedColored(blockingColor, Tr("%s: %dx%d against the %dx%d of the reference, it can't be measured."),
                               name.c_str(), entry.capture->width, entry.capture->height, reference->width,
                               reference->height);
        }
        else if (entry.metrics->depthMismatch)
        {
            TextWrappedColored(blockingColor, Tr("%s: one is HDR10 and the other isn't, it can't be measured."),
                               name.c_str());
        }
        else if (!entry.metrics->valid)
        {
            TextWrappedColored(blockingColor, Tr("%s: the picture can't be read or is too small."), name.c_str());
        }
        else if (entry.metrics->sceneMismatch)
        {
            TextWrappedColored(warningColor, Tr("%s: doesn't seem to show the same scene as the reference."),
                               name.c_str());
        }
        else if (entry.metrics->shifted)
        {
            TextWrappedColored(warningColor,
                               Tr("%s: displaced %d, %d pixels from the reference, the camera moved between captures."),
                               name.c_str(), entry.metrics->shiftX, entry.metrics->shiftY);
        }
    }

    if (differentGame)
        ImGui::TextColored(warningColor, "%s", Tr("The captures are from different games."));

    if (generatedFrames)
    {
        TextWrappedColored(warningColor, "%s",
                           Tr("Frame generation was on in at least one capture, its frame may be a generated one."));
    }

    if (hdrValues)
        TextWrappedDisabled("%s", Tr("HDR10 captures are shown and measured over their PQ values reduced to 8 bit."));

    IlionTheme::SectionPlate(Tr("Visual interpretation"));

    if (pending)
        ImGui::TextDisabled("%s", Tr("Measuring..."));
    else
        DrawVisualInterpretation(*reference, entries);

    IlionTheme::SectionPlate(Tr("Measurements against the reference"));

    if (ImGui::BeginTable("visualMetrics", (int) entries.size() + 1,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_SizingFixedFit))
    {
        ImGui::TableSetupColumn("##metric");

        for (const auto& entry : entries)
            ImGui::TableSetupColumn(StrFmt("%s##%s", columnName(*entry.capture).c_str(), entry.capture->id.c_str()).c_str());

        ImGui::TableHeadersRow();

        const auto row = [&](const char* label, const char* tooltip, const auto& value)
        {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(Tr(label));

            if (tooltip != nullptr && ImGui::IsItemHovered())
                WrappedTooltip("%s", Tr(tooltip));

            for (const auto& entry : entries)
            {
                ImGui::TableNextColumn();

                if (entry.metrics == nullptr)
                    ImGui::TextDisabled("...");
                else if (!entry.metrics->valid)
                    ImGui::TextDisabled("%s", Tr("N/A"));
                else
                    ImGui::TextUnformatted(value(*entry.metrics).c_str());
            }
        };

        row("SSIM (structure)",
            "Structural similarity of the brightness, in windows of 8x8 pixels.\n"
            "1 is the same structure as the reference.\n"
            "A high value is similar structure, not a better picture.",
            [](const Metrics& m) { return StrFmt("%.4f", m.ssim); });

        row("PSNR",
            "Signal to noise ratio of the brightness against the reference, higher is closer.\n"
            "It is a difference of signal, not how good the picture looks.",
            [](const Metrics& m) { return m.identical ? std::string(Tr("identical")) : StrFmt("%.1f dB", m.psnr); });

        row("Mean difference", "Mean difference of the brightness, in levels out of 255.",
            [](const Metrics& m) { return StrFmt("%.2f", m.meanDifference); });

        row("Pixels that differ", "Pixels with a color channel more than 2 levels out of 255 apart.",
            [](const Metrics& m) { return StrFmt("%.1f %%", m.differentPercent); });

        row("Pixels that clearly differ", "Pixels with a color channel more than 16 levels out of 255 apart.",
            [](const Metrics& m) { return StrFmt("%.1f %%", m.clearlyPercent); });

        row("Edge strength",
            "Mean step of brightness between neighbouring pixels, against the one of the reference.\n"
            "Higher is a sharper or noisier picture, lower is a softer one.",
            [](const Metrics& m) { return StrFmt("%.2f (%+.1f %%)", m.edgeStrengthTest, m.edgeChangePercent); });

        row("Edges of the reference kept",
            "Edges of the reference that are still at least half as strong in the capture.",
            [](const Metrics& m)
            { return m.edgesAvailable ? StrFmt("%.1f %%", m.edgesKeptPercent) : std::string(Tr("N/A")); });

        row("Overshoot next to edges (exploratory)",
            "Pixels next to an edge that are brighter or darker than everything around them in the reference.\n"
            "Halos and the ringing of sharpening read like this, but so does fine detail the reference lacks.",
            [](const Metrics& m) { return StrFmt("%.2f %%", m.overshootPercent); });

        row("Most different area", "Where the mean difference is biggest, in a grid of 6 columns and 4 rows.",
            [](const Metrics& m)
            {
                return m.identical ? std::string("-")
                                   : StrFmt(Tr("column %d, row %d (%.2f)"), m.worstRegionX + 1, m.worstRegionY + 1,
                                            m.regionDifference[m.worstRegionY][m.worstRegionX]);
            });

        row("Displacement", "Movement of the whole picture that matches the reference best. It should be none.",
            [](const Metrics& m)
            { return m.shifted ? StrFmt("%d, %d px", m.shiftX, m.shiftY) : std::string(Tr("none")); });

        ImGui::EndTable();
    }

    TextWrappedDisabled(Tr("Margin used: %.4f of SSIM, or the difference between captures of one same version when it "
                           "is bigger. No overall score is given."),
                        VisualSsimMargin);

    ImGui::End();
}

// --- Captures of the compared dlls against each other ------------------------------------------------------
// The newest capture of every dll of a comparison, measured pair by pair. No native picture is needed for this,
// and without one it only tells how much and where the pictures differ, never which one looks better.

static bool CaptureLinkedToCandidate(const IlionVisual::Capture& capture, const IlionCompare::Candidate& candidate,
                                     const std::vector<const IlionCompare::Test*>& tests);
static void StartVisualMeasureFiles(const std::string& key, const std::filesystem::path& referencePath,
                                    const std::filesystem::path& testPath, IlionVisual::SavedResult saved,
                                    const std::filesystem::path& folder);

struct DirectVisual
{
    enum class State
    {
        Measured,
        Unreliable, // not the same scene, or the camera moved
        Failed,     // another size, unreadable
        Pending,
    };

    struct Pair
    {
        int a = 0, b = 0; // positions in the candidates of the analysis
        State state = State::Pending;
        double ssim = 0.0;
        double differentPercent = 0.0;
        double edgeRatio = 1.0; // edge strength of b over the one of a
    };

    std::vector<std::string> capture; // id of the capture used for every dll, empty without one
    std::vector<Pair> pairs;
};

// Looks up the captures of every dll and what is saved about each pair, and has the missing pairs measured
static DirectVisual GatherDirectVisual(const IlionCompare::Analysis& analysis,
                                       const std::vector<const IlionCompare::Test*>& tests,
                                       const std::vector<IlionVisual::Capture>& captures,
                                       const std::vector<IlionVisual::SavedResult>& results)
{
    DirectVisual direct;
    std::vector<const IlionVisual::Capture*> used(analysis.candidates.size(), nullptr);

    for (size_t i = 0; i < analysis.candidates.size(); i++)
    {
        const auto& test = *tests[analysis.candidates[i].tests.front()];

        // Sorted by date, the last one that matches is the newest
        for (const auto& capture : captures)
        {
            if (CaptureLinkedToCandidate(capture, analysis.candidates[i], tests))
                used[i] = &capture;
        }

        direct.capture.push_back(used[i] != nullptr ? used[i]->id : std::string());
    }

    auto& work = GetVisualWork();
    std::map<std::string, IlionVisual::Metrics> tried;

    {
        std::scoped_lock lock(work.mutex);
        tried = work.metrics;
    }

    bool started = work.busy.load();

    for (int a = 0; a < (int) used.size(); a++)
    {
        for (int b = a + 1; b < (int) used.size(); b++)
        {
            if (used[a] == nullptr || used[b] == nullptr)
                continue;

            DirectVisual::Pair pair;
            pair.a = a;
            pair.b = b;

            const IlionVisual::SavedResult* saved = nullptr;
            bool reversed = false;

            for (const auto& row : results)
            {
                if (row.referenceId == used[a]->id && row.testId == used[b]->id)
                {
                    saved = &row;
                    reversed = false;
                }
                else if (row.referenceId == used[b]->id && row.testId == used[a]->id)
                {
                    saved = &row;
                    reversed = true;
                }
            }

            const std::string key = VisualPairKey(used[a]->id, used[b]->id);

            if (saved != nullptr)
            {
                const double ratio = 1.0 + saved->edgeChangePercent / 100.0;

                pair.state = saved->unreliable ? DirectVisual::State::Unreliable : DirectVisual::State::Measured;
                pair.ssim = saved->ssim;
                pair.differentPercent = saved->differentPercent;
                pair.edgeRatio = !reversed ? ratio : ratio > 0.0 ? 1.0 / ratio : 1.0;
            }
            else if (tried.contains(key))
            {
                pair.state = DirectVisual::State::Failed;
            }
            else if (!started)
            {
                SYSTEMTIME time;
                GetLocalTime(&time);

                IlionVisual::SavedResult row;
                row.dateTime = StrFmt("%04d-%02d-%02d %02d:%02d:%02d", time.wYear, time.wMonth, time.wDay, time.wHour,
                                      time.wMinute, time.wSecond);
                row.game = used[b]->game;
                row.referenceId = used[a]->id;
                row.referenceName = used[a]->Name();
                row.referenceSha256 = used[a]->dllSha256;
                row.referenceOrigin = "upscaled";
                row.testId = used[b]->id;
                row.testName = used[b]->Name();
                row.testSha256 = used[b]->dllSha256;

                StartVisualMeasureFiles(key, used[a]->path, used[b]->path, row, CaptureFolder());
                started = true;
            }

            direct.pairs.push_back(pair);
        }
    }

    return direct;
}

// What the captures of the dlls show when put against each other
static std::vector<CompareConclusion> BuildDirectVisualConclusions(const IlionCompare::Analysis& analysis,
                                                                   const DirectVisual& direct)
{
    using State = DirectVisual::State;

    std::vector<CompareConclusion> result;

    const auto name = [&](int index) { return analysis.candidates[index].name; };

    std::string missing;
    int withCapture = 0;

    for (int i = 0; i < (int) direct.capture.size(); i++)
    {
        if (direct.capture[i].empty())
            missing += (missing.empty() ? "" : ", ") + name(i);
        else
            withCapture++;
    }

    const std::string missingText = missing.empty() ? std::string() : StrFmt(Tr(" Without a saved capture: %s."), missing.c_str());

    if (withCapture < 2)
    {
        result.push_back({ Tr("VISUAL QUALITY: captures missing"),
                           Tr("A capture of at least two of these DLLs, taken in the same spot with the configuration of "
                              "the tests, is needed to compare their pictures.") +
                               missingText,
                           1 });
        return result;
    }

    std::vector<const DirectVisual::Pair*> measured;
    std::string notEquivalent;
    int pending = 0;

    for (const auto& pair : direct.pairs)
    {
        if (pair.state == State::Measured)
            measured.push_back(&pair);
        else if (pair.state == State::Pending)
            pending++;
        else
            notEquivalent += (notEquivalent.empty() ? "" : ", ") + name(pair.a) + " / " + name(pair.b);
    }

    if (pending > 0)
    {
        result.push_back({ Tr("VISUAL QUALITY: measuring"),
                           StrFmt(Tr("%d pairs of captures are being measured."), pending), 1 });
        return result;
    }

    const std::string notEquivalentText =
        notEquivalent.empty() ? std::string()
                              : StrFmt(Tr(" Not comparable, another scene, camera moved or another size: %s."),
                                       notEquivalent.c_str());

    if (measured.empty())
    {
        result.push_back({ Tr("VISUAL QUALITY: captures not equivalent"),
                           Tr("The saved captures of these DLLs don't show the same picture, they can't be compared. "
                              "Capture every DLL in the same spot without moving.") +
                               notEquivalentText + missingText,
                           2 });
        return result;
    }

    std::sort(measured.begin(), measured.end(),
              [](const DirectVisual::Pair* x, const DirectVisual::Pair* y) { return x->ssim > y->ssim; });

    const auto describe = [&](const DirectVisual::Pair& pair)
    {
        return StrFmt(Tr("%s / %s: SSIM %.4f, %.1f%% of the pixels differ. "), name(pair.a).c_str(), name(pair.b).c_str(),
                      pair.ssim, pair.differentPercent);
    };

    std::string text;

    if (measured.size() <= 3)
    {
        for (const auto* pair : measured)
            text += describe(*pair);
    }
    else
    {
        text += Tr("Most alike: ") + describe(*measured.front());
        text += Tr("Most different: ") + describe(*measured.back());
    }

    // Edge strength of every dll against the first one that has a capture, where that pair was measured
    int anchor = -1;

    for (int i = 0; i < (int) direct.capture.size() && anchor < 0; i++)
    {
        if (!direct.capture[i].empty())
            anchor = i;
    }

    int strongest = anchor, softest = anchor;
    double strongestRatio = 1.0, softestRatio = 1.0;

    for (const auto& pair : direct.pairs)
    {
        if (pair.state != State::Measured || pair.a != anchor)
            continue;

        if (pair.edgeRatio > strongestRatio)
        {
            strongestRatio = pair.edgeRatio;
            strongest = pair.b;
        }

        if (pair.edgeRatio < softestRatio)
        {
            softestRatio = pair.edgeRatio;
            softest = pair.b;
        }
    }

    if (strongest != softest && softestRatio > 0.0 && (strongestRatio / softestRatio - 1.0) * 100.0 >= 5.0)
    {
        text += StrFmt(Tr("Strongest edges: %s, %.1f%% more than %s. "), name(strongest).c_str(),
                       (strongestRatio / softestRatio - 1.0) * 100.0, name(softest).c_str());
    }

    text += Tr("These are differences between the pictures: without a native reference they don't show which DLL "
               "looks better.");

    result.push_back({ Tr("VISUAL DIFFERENCES between the captures of the DLLs"), text + notEquivalentText + missingText, 1 });

    return result;
}

// One conclusion over everything there is: the balance of performance and stability, and what the captures add
static CompareConclusion BuildGlobalConclusion(const IlionCompare::Analysis& analysis, const DirectVisual& direct,
                                               int nativeBest)
{
    const auto name = [&](int index) { return analysis.candidates[index].name; };

    bool differences = false;
    bool pending = false;

    for (const auto& pair : direct.pairs)
    {
        differences |= pair.state == DirectVisual::State::Measured;
        pending |= pair.state == DirectVisual::State::Pending;
    }

    const char* visual = pending     ? " The captures of these DLLs are still being measured."
                         : differences ?" The captures differ between the DLLs but, without a native reference, none is "
                                       "shown to look better: visual quality doesn't change this result."
                                     : " There are no comparable captures of these DLLs, visual quality isn't part of "
                                       "this result.";

    if (analysis.balance.kind == IlionCompare::Kind::Winner)
    {
        const int winner = analysis.balance.winner;

        if (nativeBest == winner)
        {
            return { StrFmt(Tr("GLOBAL CONCLUSION: %s"), name(winner).c_str()),
                     Tr("Best balance of performance and stability, and the closest to the native capture."), 0 };
        }

        if (nativeBest >= 0)
        {
            return { Tr("GLOBAL CONCLUSION: depends on the priority"),
                     StrFmt(Tr("%s has the best balance of performance and stability, %s is the closest to the native "
                               "capture."),
                            name(winner).c_str(), name(nativeBest).c_str()),
                     1 };
        }

        return { StrFmt(Tr("GLOBAL CONCLUSION: %s"), name(winner).c_str()),
                 std::string(Tr("Best balance of performance and stability.")) + Tr(visual), 0 };
    }

    if (nativeBest >= 0)
    {
        return { Tr("GLOBAL CONCLUSION: no overall winner"),
                 StrFmt(Tr("Performance and stability don't single out a DLL. %s is the closest to the native capture."),
                        name(nativeBest).c_str()),
                 1 };
    }

    return { Tr("GLOBAL CONCLUSION: no overall winner"),
             std::string(Tr("Performance and stability don't single out a DLL.")) + Tr(visual), 1 };
}

// The difference of a value next to it, in the same cell: green when it goes the way that is good for that
// metric, red when it goes the other way, plain when the metric has no good way or nothing changed.
static void DrawDifference(const std::string& text, double percent, int better)
{
    ImGui::SameLine(0.0f, 0.0f);

    if (better == 0 || std::abs(percent) < 0.05)
        ImGui::TextUnformatted(text.c_str());
    else if ((percent > 0.0) == (better > 0))
        ImGui::TextColored(toneMapColor(IlionTheme::Themed(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), IlionTheme::Role::Good)), "%s", text.c_str());
    else
        ImGui::TextColored(toneMapColor(IlionTheme::Themed(ImVec4(1.0f, 0.4f, 0.3f, 1.0f), IlionTheme::Role::Bad)), "%s", text.c_str());
}

static void DrawBenchmarkCompareWindow(float scale, ImGuiIO& io)
{
    if (!showBenchmarkCompare)
        return;

    using Test = IlionCompare::Test;
    using Number = IlionCompare::Number;

    static std::vector<Test> history;
    static std::vector<std::string> selectedIds; // in the order they were picked, one column each
    static std::string referenceId;
    static std::vector<IlionVisual::SavedResult> visualResults;
    static std::vector<IlionVisual::Capture> visualCaptures;
    static int visualResultsSeen = -1;

    if (benchmarkCompareReload)
    {
        benchmarkCompareReload = false;
        history = IlionCompare::LoadHistory(BenchmarkFolder() / L"benchmarks.csv");
        visualCaptures = IlionVisual::ListCaptures(CaptureFolder());
        PruneVisualResults(visualCaptures);
        visualResultsSeen = -1;

        // Tests that aren't in the file anymore drop out of the selection
        std::erase_if(selectedIds, [&](const std::string& id)
                      { return std::none_of(history.begin(), history.end(), [&](const Test& t) { return t.id == id; }); });
    }

    // Measurements of captures that were saved since the last look
    if (const int saved = visualResultsSaved.load(); saved != visualResultsSeen)
    {
        visualResultsSeen = saved;
        visualResults = IlionVisual::LoadResults(CaptureFolder());
    }

    ImGui::SetNextWindowSize(ImVec2(std::min(960.0f * scale, io.DisplaySize.x - 20.0f),
                                    std::min(720.0f * scale, io.DisplaySize.y - 20.0f)),
                             ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_FirstUseEver,
                            ImVec2(0.5f, 0.5f));

    if (!ImGui::Begin(TrL("Benchmark results comparison"), &showBenchmarkCompare,
                      ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse))
    {
        ImGui::End();
        return;
    }

    TextWrappedDisabled("%s", Tr("Performance and stability comparison. Images only count when captures were made for "
                                 "these tests."));

    if (IlionTheme::LampButton(TrL("Back to Benchmark"), IlionTheme::Lamp::Neutral))
        showBenchmarkCompare = false;

    SameLineIfRoom(IlionTheme::LampButtonWidth(TrL("Reload")));

    if (IlionTheme::LampButton(TrL("Reload"), IlionTheme::Lamp::Neutral))
        benchmarkCompareReload = true;

    SameLineIfRoom(IlionTheme::LampButtonWidth(TrL("Clear selection")));

    if (IlionTheme::LampButton(TrL("Clear selection"), IlionTheme::Lamp::Neutral))
        selectedIds.clear();

    ImGui::SameLine();

    // Deletes the picked tests from disk, asked for twice
    static std::string deleteError;

    ImGui::BeginDisabled(selectedIds.empty());

    if (IlionTheme::DangerButton(TrL("Delete tests")))
    {
        deleteError.clear();
        ImGui::OpenPopup("##DeleteTests");
    }

    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    {
        WrappedTooltip("%s", Tr("Deletes the selected tests for good: their saved result and their frame times.\n"
                                   "Clear selection only unticks them."));
    }

    ImGui::EndDisabled();

    if (ImGui::BeginPopupModal("##DeleteTests", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar |
                                   ImGuiWindowFlags_NoSavedSettings))
    {
        ImGui::Text(Tr("Delete %d selected tests for good?"), (int) selectedIds.size());
        ImGui::TextDisabled("%s", Tr("Their saved results and frame times are removed from disk. This can't be undone."));
        ImGui::Spacing();

        if (IlionTheme::DangerButton(TrL("Delete")))
        {
            const int removed = IlionCompare::DeleteTests(BenchmarkFolder(), selectedIds, &deleteError);

            LOG_INFO("[BENCHMARK] deleted {} of {} selected tests {}", removed, selectedIds.size(), deleteError);

            // Deleted tests leave the selection, the list and everything below is built again from disk
            if (deleteError.empty())
                selectedIds.clear();

            benchmarkCompareReload = true;
            ImGui::CloseCurrentPopup();
        }

        SameLineIfRoom(IlionTheme::LampButtonWidth(TrL("Cancel")));

        if (IlionTheme::LampButton(TrL("Cancel"), IlionTheme::Lamp::Neutral))
            ImGui::CloseCurrentPopup();

        ImGui::EndPopup();
    }

    if (!deleteError.empty())
    {
        ImGui::SameLine();
        ImGui::TextColored(toneMapColor(IlionTheme::Themed(ImVec4(1.0f, 0.4f, 0.3f, 1.0f), IlionTheme::Role::Bad)), "%s", Tr(deleteError.c_str()));
    }

    ImGui::SameLine();
    ImGui::TextDisabled(Tr("%d saved tests, %d selected"), (int) history.size(), (int) selectedIds.size());

    if (history.empty())
    {
        ImGui::Spacing();
        ImGui::TextWrapped("%s", Tr("No saved tests yet. Run a benchmark and press Save."));
        ImGui::End();
        return;
    }

    const auto isSelected = [&](const std::string& id)
    { return std::find(selectedIds.begin(), selectedIds.end(), id) != selectedIds.end(); };

    // Saved tests, newest first
    const float listHeight = ImGui::GetTextLineHeightWithSpacing() * 8.5f;

    if (ImGui::BeginTable("compareHistory", 7,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY |
                              ImGuiTableFlags_SizingFixedFit,
                          ImVec2(0.0f, listHeight)))
    {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("##pick");
        ImGui::TableSetupColumn(Tr("Implementation"));
        ImGui::TableSetupColumn(Tr("Date and time"));
        ImGui::TableSetupColumn(Tr("Duration"));
        ImGui::TableSetupColumn(Tr("Game"));
        ImGui::TableSetupColumn(Tr("Resolution"));
        ImGui::TableSetupColumn("Frame Generation", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        for (int i = (int) history.size() - 1; i >= 0; i--)
        {
            const auto& test = history[i];

            ImGui::TableNextRow();
            ImGui::TableNextColumn();

            bool picked = isSelected(test.id);

            // The record's own id: a row number alone is the same id the header of the column gets
            ImGui::PushID(StrFmt("test_%s_%d", test.id.c_str(), i).c_str());

            if (ImGui::Checkbox("##select", &picked))
            {
                if (picked)
                    selectedIds.push_back(test.id);
                else
                    std::erase(selectedIds, test.id);
            }

            ImGui::PopID();

            ImGui::TableNextColumn();
            ImGui::TextUnformatted(test.Name().c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(test.dateTime.c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%.0f s", test.durationMs.valid ? test.durationMs.value / 1000.0 : 0.0);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(test.game.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(test.resolution.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(test.frameGeneration.c_str());
        }

        ImGui::EndTable();
    }

    // Selected tests in the order they were picked
    std::vector<const Test*> tests;

    for (const auto& id : selectedIds)
    {
        for (const auto& test : history)
        {
            if (test.id == id)
                tests.push_back(&test);
        }
    }

    if (tests.size() < 2)
    {
        ImGui::Spacing();
        ImGui::TextWrapped("%s", Tr("Select two or more tests to compare them."));
        ImGui::End();
        return;
    }

    // Reference the differences are measured against
    if (!isSelected(referenceId))
        referenceId = tests.front()->id;

    const Test* reference = tests.front();

    for (const auto* test : tests)
    {
        if (test->id == referenceId)
            reference = test;
    }

    const auto columnName = [&](const Test& test) { return StrFmt("%s  %s", test.Name().c_str(), test.dateTime.c_str()); };

    ImGui::Spacing();
    ImGui::SetNextItemWidth(360.0f * scale);

    if (IlionTheme::BeginCombo(TrL("Reference"), columnName(*reference).c_str()))
    {
        for (const auto* test : tests)
        {
            if (ImGui::Selectable(StrFmt("%s##%s", columnName(*test).c_str(), test->id.c_str()).c_str(),
                                  test->id == referenceId))
                referenceId = test->id;
        }

        ImGui::EndCombo();
    }

    // Conclusions
    const auto analysis = IlionCompare::Analyze(tests);

    IlionTheme::SectionPlate(Tr("Conclusions"));

    auto conclusions = BuildCompareConclusions(analysis, tests);

    // Image quality is a category of its own: the captures of the same dlls against each other, against a native
    // capture when there is one, and one conclusion over everything
    if (analysis.candidates.size() >= 2 && analysis.blocking.empty())
    {
        const auto direct = GatherDirectVisual(analysis, tests, visualCaptures, visualResults);

        // Only with captures that were made for these very tests. Without any, this is a comparison of
        // performance and stability and nothing about images is said.
        const bool linked = std::any_of(direct.capture.begin(), direct.capture.end(),
                                        [](const std::string& id) { return !id.empty(); });

        if (linked)
        {
            for (auto& conclusion : BuildDirectVisualConclusions(analysis, direct))
                conclusions.push_back(std::move(conclusion));

            if (analysis.performance.kind == IlionCompare::Kind::Winner || analysis.performance.kind == IlionCompare::Kind::Tie)
                conclusions.push_back(BuildGlobalConclusion(analysis, direct, -1));
        }
    }

    for (const auto& conclusion : conclusions)
    {
        const ImVec4 color = conclusion.level == 0   ? IlionTheme::Themed(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), IlionTheme::Role::Good)
                             : conclusion.level == 2 ? IlionTheme::Themed(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), IlionTheme::Role::Warn)
                                                     : ImGui::GetStyleColorVec4(ImGuiCol_Text);

        ImGui::TextColored(conclusion.level == 1 ? color : toneMapColor(color), "%s", conclusion.title.c_str());
        ImGui::TextWrapped("%s", conclusion.text.c_str());
    }

    TextWrappedDisabled(Tr("Margins used: %.1f%% in average FPS, %.1f%% in 1%% low.%s"), analysis.marginAvg,
                        analysis.marginLow1,
                        analysis.marginFromRepeats ? Tr(" Taken from the spread between tests of the same dll.") : "");

    for (const auto issue : analysis.blocking)
        ImGui::TextColored(toneMapColor(IlionTheme::Themed(ImVec4(1.0f, 0.4f, 0.3f, 1.0f), IlionTheme::Role::Bad)), "%s", Tr(CompareIssueText(issue)));

    for (const auto issue : analysis.warnings)
        ImGui::TextColored(toneMapColor(IlionTheme::Themed(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), IlionTheme::Role::Warn)), "%s", Tr(CompareIssueText(issue)));

    // One column per test
    IlionTheme::SectionPlate(Tr("Results"));

    // better: +1 when a higher value is the good one, -1 when a lower one is, 0 when neither. Only for the colour
    // of the difference.
    struct MetricRow
    {
        const char* label;
        Number Test::*field;
        const char* format;
        bool differences;
        int better = 0;
    };

    static const MetricRow performanceRows[] = {
        { "Average FPS", &Test::avgFps, "%.1f", true, 1 },
        { "Median FPS", &Test::medianFps, "%.1f", true, 1 },
        { "1% low", &Test::low1Fps, "%.1f", true, 1 },
        { "0.1% low", &Test::low01Fps, "%.1f", true, 1 },
        { "Frame time mean", &Test::meanMs, "%.2f ms", true, -1 },
        { "Frame time P95", &Test::p95Ms, "%.2f ms", true, -1 },
        { "Frame time P99", &Test::p99Ms, "%.2f ms", true, -1 },
        { "Variability", &Test::variationPercent, "%.1f %%", true, -1 },
        { "Stutter events", &Test::stutterCount, "%.0f", true, -1 },
        { "Stutters per minute", &Test::stutterPerMinute, "%.1f", true, -1 },
        { "Frames", &Test::frames, "%.0f", false },
    };

    static const MetricRow hardwareRows[] = {
        { "GPU usage, average", &Test::gpuUsageAvg, "%.0f %%", false },
        { "CPU usage, average", &Test::cpuUsageAvg, "%.0f %%", false },
        { "GPU temperature, average", &Test::gpuTempAvg, "%.0f \xC2\xB0""C", false },
        { "GPU temperature, maximum", &Test::gpuTempMax, "%.0f \xC2\xB0""C", false },
        { "GPU hotspot, maximum", &Test::gpuHotspotMax, "%.0f \xC2\xB0""C", false },
        { "CPU temperature, average", &Test::cpuTempAvg, "%.0f \xC2\xB0""C", false },
        { "CPU temperature, maximum", &Test::cpuTempMax, "%.0f \xC2\xB0""C", false },
        { "VRAM used, average", &Test::vramUsedAvg, "%.0f MB", false },
        { "VRAM used, maximum", &Test::vramUsedMax, "%.0f MB", false },
    };

    const int columnCount = (int) tests.size() + 1;

    if (ImGui::BeginTable("compareResults", columnCount,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollX |
                              ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit,
                          ImVec2(0.0f, 0.0f)))
    {
        ImGui::TableSetupScrollFreeze(1, 1);
        ImGui::TableSetupColumn("##metric");

        for (const auto* test : tests)
        {
            ImGui::TableSetupColumn(StrFmt("%s%s##%s", test->Name().c_str(), test == reference ? Tr("  [reference]") : "",
                                           test->id.c_str())
                                        .c_str());
        }

        ImGui::TableHeadersRow();

        const auto textRow = [&](const char* label, const auto& value)
        {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", Tr(label));

            for (const auto* test : tests)
            {
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(value(*test).c_str());
            }
        };

        textRow("Date and time", [](const Test& t) { return t.dateTime; });
        textRow("Duration", [](const Test& t) { return StrFmt("%.1f s", t.durationMs.valid ? t.durationMs.value / 1000.0 : 0.0); });
        textRow("Game", [](const Test& t) { return t.game; });
        textRow("Upscaler", [](const Test& t) { return t.upscaler; });
        textRow("Resolution", [](const Test& t) { return t.resolution; });
        textRow("Frame Generation", [](const Test& t) { return t.frameGeneration; });
        textRow("DLL file version", [](const Test& t) { return t.dllFileVersion; });
        textRow("SHA-256", [](const Test& t) { return t.dllSha256.empty() ? std::string("-") : t.dllSha256.substr(0, 12) + "..."; });

        const auto metricRows = [&](const MetricRow* rows, size_t count)
        {
            for (size_t r = 0; r < count; r++)
            {
                const auto& row = rows[r];

                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(Tr(row.label));

                for (const auto* test : tests)
                {
                    ImGui::TableNextColumn();

                    const Number& value = test->*row.field;

                    if (!value.valid)
                    {
                        ImGui::TextDisabled("%s", Tr("N/A"));
                        continue;
                    }

                    ImGui::TextUnformatted(StrFmt(row.format, value.value).c_str());

                    // Difference against the reference, in % and in the unit of the value
                    if (row.differences && test != reference)
                    {
                        const auto percent = IlionCompare::PercentDifference(value, reference->*row.field);
                        const auto absolute = IlionCompare::AbsoluteDifference(value, reference->*row.field);

                        if (percent.valid && absolute.valid)
                        {
                            DrawDifference(StrFmt("   %+.1f %% (%+.2f)", percent.value, absolute.value), percent.value,
                                           row.better);
                        }
                    }
                }
            }
        };

        metricRows(performanceRows, std::size(performanceRows));

        // Hardware is context for the reader, it doesn't make an upscaler better or worse by itself
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextDisabled("%s", Tr("Hardware (informative)"));

        metricRows(hardwareRows, std::size(hardwareRows));

        ImGui::EndTable();
    }

    ImGui::End();
}

// --- Integrated final report -------------------------------------------------------------------------------
// Saved benchmarks and captures of every dll put together: performance, stability and visual quality side by
// side, with one conclusion. Nothing is measured here that the other screens don't measure, their results are
// joined by the dll (SHA-256) and by the configuration of the test.

static bool showFinalReport = false;
static bool finalReportReload = true;

// Visual measurements of one dll against the native reference of the report
struct ReportVisual
{
    int captures = 0;   // captures of this dll in this configuration
    int measured = 0;   // with a reliable measurement
    int unreliable = 0; // scene or camera not the same as in the reference
    int failed = 0;     // can't be measured
    int pending = 0;

    // Means over the reliable measurements
    double ssim = 0.0, meanDifference = 0.0, differentPercent = 0.0;
    double edgeChangePercent = 0.0, edgesKeptPercent = 0.0, overshootPercent = 0.0;
    double lowest = 1.0, highest = 0.0;
};

struct ReportVisualVerdict
{
    bool complete = false;    // every dll has a reliable measurement
    std::vector<int> missing; // dlls without one
    std::vector<int> top;     // closest to native, more than one when they are within the margin
    int best = -1;
    int second = -1;
    double margin = VisualSsimMargin;
};

static ReportVisualVerdict JudgeReportVisual(const std::vector<ReportVisual>& visual)
{
    ReportVisualVerdict verdict;
    std::vector<int> order;

    for (int i = 0; i < (int) visual.size(); i++)
    {
        if (visual[i].measured == 0)
        {
            verdict.missing.push_back(i);
            continue;
        }

        order.push_back(i);

        // What captures of one dll differ by is what the picture changes by itself
        if (visual[i].measured > 1)
            verdict.margin = (std::max)(verdict.margin, visual[i].highest - visual[i].lowest);
    }

    verdict.complete = !visual.empty() && verdict.missing.empty();

    if (order.empty())
        return verdict;

    std::sort(order.begin(), order.end(), [&](int a, int b) { return visual[a].ssim > visual[b].ssim; });

    verdict.best = order[0];
    verdict.second = order.size() > 1 ? order[1] : -1;

    for (const int index : order)
    {
        if (visual[verdict.best].ssim - visual[index].ssim <= verdict.margin)
            verdict.top.push_back(index);
    }

    return verdict;
}

// The visual category and the conclusion over the three categories. A dll is only named when it is among the
// best of all three with what was measured, nothing is weighted or scored.
static std::vector<CompareConclusion> BuildIntegratedConclusions(const IlionCompare::Analysis& analysis,
                                                                 const std::vector<ReportVisual>& visual,
                                                                 const ReportVisualVerdict& verdict,
                                                                 bool haveReference)
{
    using Kind = IlionCompare::Kind;
    using Reason = IlionCompare::Reason;

    std::vector<CompareConclusion> result;

    const auto name = [&](int index) { return analysis.candidates[index].name; };

    const auto names = [&](const std::vector<int>& indices)
    {
        std::string joined;

        for (const int index : indices)
            joined += (joined.empty() ? "" : ", ") + name(index);

        return joined;
    };

    // Visual quality
    if (!haveReference)
    {
        result.push_back({ Tr("VISUAL QUALITY: no native reference"),
                           Tr("There is no native capture without upscaler of this game at this output size. "
                              "Without one, how close each DLL is to native can't be measured."),
                           2 });
    }
    else if (verdict.best < 0)
    {
        result.push_back({ Tr("VISUAL QUALITY: no captures measured"),
                           Tr("None of these DLLs has a capture of this configuration with a reliable measurement "
                              "against the native reference."),
                           2 });
    }
    else if (!verdict.complete)
    {
        result.push_back({ Tr("VISUAL QUALITY: incomplete"),
                           StrFmt(Tr("Without a reliable capture: %s. The values of the others are in the table."),
                                  names(verdict.missing).c_str()),
                           2 });
    }
    else if (verdict.second < 0)
    {
        result.push_back({ Tr("VISUAL: one version measured"),
                           StrFmt(Tr("%s has an SSIM of %.4f against the native reference. With one version there is "
                                     "nothing to rank."),
                                  name(verdict.best).c_str(), visual[verdict.best].ssim),
                           1 });
    }
    else if (verdict.top.size() == 1)
    {
        result.push_back({ StrFmt(Tr("VISUALLY CLOSEST TO NATIVE: %s"), name(verdict.best).c_str()),
                           StrFmt(Tr("SSIM of %.4f against %.4f for %s, the margin used is %.4f."),
                                  visual[verdict.best].ssim, visual[verdict.second].ssim, name(verdict.second).c_str(),
                                  verdict.margin),
                           0 });
    }
    else
    {
        result.push_back({ Tr("VISUAL QUALITY: no clear difference"),
                           StrFmt(Tr("%s are within %.4f of SSIM of each other, the margin used here."),
                                  names(verdict.top).c_str(), verdict.margin),
                           1 });
    }

    // The three categories together
    const auto& performance = analysis.performance;
    const auto& stability = analysis.stability;

    const bool measured = analysis.candidates.size() >= 2 &&
                          (performance.kind == Kind::Winner || performance.kind == Kind::Tie) &&
                          stability.reason != Reason::MissingData && stability.reason != Reason::NotComparable &&
                          stability.reason != Reason::TooFewTests && stability.reason != Reason::SameDll;

    if (!measured)
    {
        result.push_back({ Tr("INTEGRATED RESULT: not enough data"),
                           Tr("Performance and stability can't be compared between these tests, no overall winner is "
                              "named."),
                           2 });
        return result;
    }

    if (!haveReference || !verdict.complete)
    {
        std::string text = Tr("Visual quality isn't measured for every DLL, so no overall winner is named.");

        if (analysis.balance.kind == Kind::Winner)
        {
            text += StrFmt(Tr(" In performance and stability alone, %s has the best balance."),
                           name(analysis.balance.winner).c_str());
        }

        result.push_back({ Tr("INTEGRATED RESULT: no overall winner"), text, 2 });
        return result;
    }

    // The best of each category, everything inside the margin of the best one counts as best
    std::vector<int> everyone;

    for (int i = 0; i < (int) analysis.candidates.size(); i++)
        everyone.push_back(i);

    const std::vector<int> fastest = performance.kind == Kind::Winner ? std::vector<int> { performance.winner }
                                                                      : performance.tied;
    const bool stabilityMixed = stability.reason == Reason::Mixed;
    const std::vector<int> steadiest = stability.kind == Kind::Winner ? std::vector<int> { stability.winner }
                                       : stability.kind == Kind::Tie  ? stability.tied
                                                                      : everyone;

    const auto in = [](const std::vector<int>& list, int value)
    { return std::find(list.begin(), list.end(), value) != list.end(); };

    std::vector<int> all;

    for (const int index : everyone)
    {
        if (in(fastest, index) && in(steadiest, index) && in(verdict.top, index))
            all.push_back(index);
    }

    if (all.size() == 1)
    {
        const int winner = all.front();
        std::string text;

        if (performance.kind == Kind::Winner)
        {
            text += StrFmt(Tr("Performance: %.1f average FPS, %.1f%% more than %s. "),
                           analysis.candidates[winner].average.avgFps.value, performance.avgAdvantage,
                           name(performance.runnerUp).c_str());
        }
        else
        {
            text += StrFmt(Tr("Performance: among the fastest, within %.1f%% of average FPS. "), analysis.marginAvg);
        }

        if (stability.kind == Kind::Winner)
        {
            text += StrFmt(Tr("Stability: frame time variation of %.1f%% against %.1f%% for %s. "),
                           stability.variationWinner, stability.variationRunnerUp, name(stability.runnerUp).c_str());
        }
        else if (stabilityMixed)
        {
            text += Tr("Stability: inconclusive between the tests. ");
        }
        else
        {
            text += Tr("Stability: among the steadiest, without a clear difference. ");
        }

        if (verdict.top.size() == 1)
        {
            text += StrFmt(Tr("Visual quality: SSIM of %.4f against native, %.4f for %s."), visual[winner].ssim,
                           visual[verdict.second].ssim, name(verdict.second).c_str());
        }
        else
        {
            text += StrFmt(Tr("Visual quality: SSIM of %.4f against native, among the closest (margin %.4f)."),
                           visual[winner].ssim, verdict.margin);
        }

        result.push_back({ StrFmt(Tr("BEST INTEGRATED BALANCE: %s"), name(winner).c_str()), text, 0 });
    }
    else if (all.size() > 1)
    {
        result.push_back({ Tr("INTEGRATED RESULT: no overall winner"),
                           StrFmt(Tr("%s are among the best in performance, stability and visual quality. No "
                                     "difference bigger than the margins separates them."),
                                  names(all).c_str()),
                           1 });
    }
    else
    {
        result.push_back({ Tr("INTEGRATED RESULT: depends on the priority"),
                           StrFmt(Tr("Fastest: %s. Steadiest: %s. Closest to native: %s. No DLL is among the best of "
                                     "the three categories."),
                                  names(fastest).c_str(),
                                  stabilityMixed ? Tr("inconclusive") : names(steadiest).c_str(),
                                  names(verdict.top).c_str()),
                           1 });
    }

    return result;
}

// A capture belongs to a candidate only when it was made for one of its tests: taken while that saved test was
// the result on screen, which is when its id gets written into the capture. The same dll in another capture is
// not enough, captures of other days or other spots are never picked up by themselves.
static bool CaptureLinkedToCandidate(const IlionVisual::Capture& capture, const IlionCompare::Candidate& candidate,
                                     const std::vector<const IlionCompare::Test*>& tests)
{
    if (capture.benchmarkId.empty())
        return false;

    for (const int index : candidate.tests)
    {
        if (tests[index]->id == capture.benchmarkId)
            return true;
    }

    return false;
}

// Reads the two pictures, measures them and saves the result, for pairs nobody has measured yet
static void StartVisualMeasureFiles(const std::string& key, const std::filesystem::path& referencePath,
                                    const std::filesystem::path& testPath, IlionVisual::SavedResult saved,
                                    const std::filesystem::path& folder)
{
    auto& work = GetVisualWork();
    work.busy = true;

    std::thread([key, referencePath, testPath, saved, folder, &work]() mutable
                {
                    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);

                    IlionVisual::Image reference, test;
                    IlionVisual::Metrics metrics;

                    if (IlionVisual::LoadPng(referencePath, reference) && IlionVisual::LoadPng(testPath, test))
                        metrics = IlionVisual::Compare(reference, test);

                    if (metrics.valid)
                    {
                        saved.ssim = metrics.ssim;
                        saved.meanDifference = metrics.meanDifference;
                        saved.differentPercent = metrics.differentPercent;
                        saved.edgeChangePercent = metrics.edgeChangePercent;
                        saved.edgesKeptPercent = metrics.edgesKeptPercent;
                        saved.overshootPercent = metrics.overshootPercent;
                        saved.unreliable = metrics.Unreliable();
                        IlionVisual::SaveResult(folder, saved);
                        visualResultsSaved++;
                    }

                    {
                        std::scoped_lock lock(work.mutex);
                        work.metrics[key] = metrics;
                    }

                    work.busy = false;
                })
        .detach();
}

static void DrawFinalReportWindow(float scale, ImGuiIO& io)
{
    if (!showFinalReport)
        return;

    using Test = IlionCompare::Test;
    using Number = IlionCompare::Number;
    using Capture = IlionVisual::Capture;

    // Tests made under the same conditions, the only ones that can be put side by side
    struct Configuration
    {
        std::string game, resolution, technology, frameGeneration;
        bool fgSeen = false;
        int fgMultiplier = 1;
        std::vector<int> tests; // positions in the history

        std::string Label() const
        {
            return StrFmt("%s  |  %s  |  %s  |  FG %s  |  %d %s", game.c_str(),
                          resolution.empty() ? Tr("no upscaling") : resolution.c_str(), technology.c_str(),
                          frameGeneration.c_str(), (int) tests.size(), Tr("tests"));
        }
    };

    static std::vector<Test> history;
    static std::vector<Capture> captures;
    static std::vector<IlionVisual::SavedResult> results;
    static std::vector<Configuration> configurations;
    static int configurationIndex = 0;
    static std::string referenceCaptureId;
    static int baseline = 0;
    static int resultsSeen = -1;

    auto& work = GetVisualWork();

    if (finalReportReload)
    {
        finalReportReload = false;
        history = IlionCompare::LoadHistory(BenchmarkFolder() / L"benchmarks.csv");
        captures = IlionVisual::ListCaptures(CaptureFolder());
        PruneVisualResults(captures);
        resultsSeen = -1;

        std::string previous;

        if (configurationIndex >= 0 && configurationIndex < (int) configurations.size())
            previous = configurations[configurationIndex].Label();

        configurations.clear();

        for (int i = 0; i < (int) history.size(); i++)
        {
            const auto& test = history[i];
            const auto space = test.upscaler.find(' ');
            const std::string technology = space == std::string::npos ? test.upscaler : test.upscaler.substr(0, space);

            Configuration* found = nullptr;

            for (auto& configuration : configurations)
            {
                if (_stricmp(configuration.game.c_str(), test.game.c_str()) == 0 &&
                    configuration.resolution == test.resolution && configuration.technology == technology &&
                    configuration.frameGeneration == test.frameGeneration && configuration.fgSeen == test.fgSeen &&
                    configuration.fgMultiplier == test.fgMultiplier)
                    found = &configuration;
            }

            if (found == nullptr)
            {
                configurations.push_back(
                    { test.game, test.resolution, technology, test.frameGeneration, test.fgSeen, test.fgMultiplier });
                found = &configurations.back();
            }

            found->tests.push_back(i);
        }

        // The one that was open stays open, otherwise the one of the newest test
        configurationIndex = -1;

        for (int i = 0; i < (int) configurations.size(); i++)
        {
            if (!previous.empty() && configurations[i].Label() == previous)
                configurationIndex = i;
        }

        if (configurationIndex < 0 && !history.empty())
        {
            for (int i = 0; i < (int) configurations.size(); i++)
            {
                if (configurations[i].tests.back() == (int) history.size() - 1)
                    configurationIndex = i;
            }
        }
    }

    // Measurements that were saved since the last look
    if (const int saved = visualResultsSaved.load(); saved != resultsSeen)
    {
        resultsSeen = saved;
        results = IlionVisual::LoadResults(CaptureFolder());
    }

    ImGui::SetNextWindowSize(ImVec2(std::min(1100.0f * scale, io.DisplaySize.x - 20.0f),
                                    std::min(820.0f * scale, io.DisplaySize.y - 20.0f)),
                             ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_FirstUseEver,
                            ImVec2(0.5f, 0.5f));

    if (!ImGui::Begin(TrL("Integrated final report"), &showFinalReport,
                      ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse))
    {
        ImGui::End();
        return;
    }

    TextWrappedDisabled("%s", Tr("Saved benchmarks and captures of every DLL, joined by the DLL and the configuration "
                                 "of the test."));

    if (IlionTheme::LampButton(TrL("Back to Benchmark"), IlionTheme::Lamp::Neutral))
        showFinalReport = false;

    SameLineIfRoom(IlionTheme::LampButtonWidth(TrL("Reload")));

    if (IlionTheme::LampButton(TrL("Reload"), IlionTheme::Lamp::Neutral))
        finalReportReload = true;

    ImGui::SameLine();
    ImGui::TextDisabled(Tr("%d saved tests, %d captures"), (int) history.size(), (int) captures.size());

    if (configurations.empty() || configurationIndex < 0)
    {
        ImGui::Spacing();
        ImGui::TextWrapped("%s", Tr("No saved tests yet. Run a benchmark and press Save."));
        ImGui::End();
        return;
    }

    const auto& configuration = configurations[configurationIndex];

    ImGui::Spacing();
    ImGui::SetNextItemWidth(620.0f * scale);

    if (IlionTheme::BeginCombo(TrL("Configuration"), configuration.Label().c_str()))
    {
        for (int i = 0; i < (int) configurations.size(); i++)
        {
            if (ImGui::Selectable(StrFmt("%s##configuration%d", configurations[i].Label().c_str(), i).c_str(),
                                  i == configurationIndex))
                configurationIndex = i;
        }

        ImGui::EndCombo();
    }

    if (ImGui::IsItemHovered())
    {
        WrappedTooltip("%s", Tr("Game, render and output resolution, upscaler and frame generation.\n"
                                   "Only tests made under the same configuration are compared."));
    }

    // Every test of the configuration, the ones of one dll are averaged into one candidate
    std::vector<const Test*> tests;

    for (const int index : configuration.tests)
        tests.push_back(&history[index]);

    const auto analysis = IlionCompare::Analyze(tests);
    const int dllCount = (int) analysis.candidates.size();

    if (dllCount == 0)
    {
        ImGui::Spacing();
        ImGui::TextWrapped("%s", Tr("This configuration has one saved test. Save tests of two or more DLLs made under "
                                    "the same configuration to get a report."));

        for (const auto issue : analysis.blocking)
            ImGui::TextColored(toneMapColor(IlionTheme::Themed(ImVec4(1.0f, 0.4f, 0.3f, 1.0f), IlionTheme::Role::Bad)), "%s", Tr(CompareIssueText(issue)));

        ImGui::End();
        return;
    }

    // Native captures without upscaler of the same game and output size, what closeness is measured against
    unsigned renderWidth = 0, renderHeight = 0, outputWidth = 0, outputHeight = 0;
    sscanf_s(configuration.resolution.c_str(), "%ux%u -> %ux%u", &renderWidth, &renderHeight, &outputWidth,
             &outputHeight);

    std::vector<const Capture*> references;

    for (const auto& capture : captures)
    {
        if (NeutralVisualReference(capture) && _stricmp(capture.game.c_str(), configuration.game.c_str()) == 0 &&
            capture.width == (int) outputWidth && capture.height == (int) outputHeight)
            references.push_back(&capture);
    }

    const Capture* reference = nullptr;

    for (const auto* capture : references)
    {
        if (capture->id == referenceCaptureId)
            reference = capture;
    }

    // The newest one unless the user picked another
    if (reference == nullptr && !references.empty())
    {
        reference = references.back();
        referenceCaptureId = reference->id;
    }

    // Images are part of the report only when a capture was made for one of these tests. Without any, it is a
    // report of performance and stability: no reference, no measuring, no visual rows.
    bool linkedAny = false;

    for (const auto& candidate : analysis.candidates)
    {
        for (const auto& capture : captures)
            linkedAny = linkedAny || CaptureLinkedToCandidate(capture, candidate, tests);
    }

    if (!linkedAny)
        reference = nullptr;

    if (reference != nullptr)
    {
        ImGui::SetNextItemWidth(620.0f * scale);

        if (IlionTheme::BeginCombo(TrL("Native reference"), StrFmt("%s  %s", reference->dateTime.c_str(), reference->id.c_str()).c_str()))
        {
            for (const auto* capture : references)
            {
                if (ImGui::Selectable(StrFmt("%s  %s##%s", capture->dateTime.c_str(), capture->id.c_str(),
                                             capture->id.c_str())
                                          .c_str(),
                                      capture == reference))
                    referenceCaptureId = capture->id;
            }

            ImGui::EndCombo();
        }

        if (ImGui::IsItemHovered())
        {
            WrappedTooltip("%s", Tr("Capture of the game at native resolution without upscaler.\n"
                                       "The captures of every DLL are measured against it. Use the one of the same spot."));
        }
    }

    // Captures of every dll and what was measured of them against the reference
    std::vector<ReportVisual> visual(dllCount);
    bool generatedFrames = false;

    std::map<std::string, IlionVisual::Metrics> measuredNow;

    {
        std::scoped_lock lock(work.mutex);
        measuredNow = work.metrics;
    }

    struct Pending
    {
        const Capture* capture;
        std::string key;
    };

    std::vector<Pending> toMeasure;

    for (int i = 0; i < dllCount; i++)
    {
        const Test& first = *tests[analysis.candidates[i].tests.front()];
        auto& entry = visual[i];

        for (const auto& capture : captures)
        {
            if (!CaptureLinkedToCandidate(capture, analysis.candidates[i], tests))
                continue;

            entry.captures++;
            generatedFrames |= !capture.frameGeneration.empty() && capture.frameGeneration != "Off";

            if (reference == nullptr)
                continue;

            const IlionVisual::SavedResult* saved = nullptr;

            for (const auto& row : results)
            {
                if (row.referenceId == reference->id && row.testId == capture.id)
                    saved = &row;
            }

            if (saved != nullptr)
            {
                if (saved->unreliable)
                {
                    entry.unreliable++;
                    continue;
                }

                entry.measured++;
                entry.ssim += saved->ssim;
                entry.meanDifference += saved->meanDifference;
                entry.differentPercent += saved->differentPercent;
                entry.edgeChangePercent += saved->edgeChangePercent;
                entry.edgesKeptPercent += saved->edgesKeptPercent;
                entry.overshootPercent += saved->overshootPercent;
                entry.lowest = (std::min)(entry.lowest, saved->ssim);
                entry.highest = (std::max)(entry.highest, saved->ssim);
                continue;
            }

            const std::string key = VisualPairKey(reference->id, capture.id);

            // Tried and it can't be measured (another size, unreadable)
            if (measuredNow.contains(key))
            {
                entry.failed++;
                continue;
            }

            entry.pending++;
            toMeasure.push_back({ &capture, key });
        }

        if (entry.measured > 0)
        {
            entry.ssim /= entry.measured;
            entry.meanDifference /= entry.measured;
            entry.differentPercent /= entry.measured;
            entry.edgeChangePercent /= entry.measured;
            entry.edgesKeptPercent /= entry.measured;
            entry.overshootPercent /= entry.measured;
        }
    }

    // Captures nobody has measured against this reference yet are measured here, one at a time
    if (!toMeasure.empty() && !work.busy.load())
    {
        const auto& next = toMeasure.front();

        SYSTEMTIME time;
        GetLocalTime(&time);

        IlionVisual::SavedResult saved;
        saved.dateTime = StrFmt("%04d-%02d-%02d %02d:%02d:%02d", time.wYear, time.wMonth, time.wDay, time.wHour,
                                time.wMinute, time.wSecond);
        saved.game = next.capture->game;
        saved.referenceId = reference->id;
        saved.referenceName = reference->Name();
        saved.referenceSha256 = reference->dllSha256;
        saved.referenceOrigin = "native";
        saved.testId = next.capture->id;
        saved.testName = next.capture->Name();
        saved.testSha256 = next.capture->dllSha256;

        StartVisualMeasureFiles(next.key, reference->path, next.capture->path, saved, CaptureFolder());
    }

    const auto verdict = JudgeReportVisual(visual);

    // --- Conclusions -----------------------------------------------------------------------------------------
    IlionTheme::SectionPlate(Tr("Conclusions"));

    auto conclusions = BuildCompareConclusions(analysis, tests);

    // The balance of two categories gives way to the one over the three, when there is a third
    if (linkedAny && conclusions.size() == 3)
        conclusions.pop_back();

    if (!linkedAny)
    {
        // No captures of these tests: the conclusions of performance and stability are the whole report
    }
    else if (!toMeasure.empty())
    {
        conclusions.push_back({ Tr("VISUAL QUALITY: measuring"),
                                StrFmt(Tr("%d captures are being measured against the native reference."),
                                       (int) toMeasure.size()),
                                1 });
    }
    else
    {
        // Without a native capture the captures of the dlls are put against each other
        if (reference == nullptr && dllCount >= 2 && analysis.blocking.empty())
        {
            const auto direct = GatherDirectVisual(analysis, tests, captures, results);

            for (auto& conclusion : BuildDirectVisualConclusions(analysis, direct))
                conclusions.push_back(std::move(conclusion));

            if (analysis.performance.kind == IlionCompare::Kind::Winner || analysis.performance.kind == IlionCompare::Kind::Tie)
            {
                // The balance of two categories was taken off above, here it is the base of the conclusion
                conclusions.push_back(BuildGlobalConclusion(analysis, direct, -1));
            }
        }
        else
        {
            for (auto& conclusion : BuildIntegratedConclusions(analysis, visual, verdict, reference != nullptr))
                conclusions.push_back(std::move(conclusion));
        }
    }

    for (const auto& conclusion : conclusions)
    {
        const ImVec4 color = conclusion.level == 0   ? IlionTheme::Themed(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), IlionTheme::Role::Good)
                             : conclusion.level == 2 ? IlionTheme::Themed(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), IlionTheme::Role::Warn)
                                                     : ImGui::GetStyleColorVec4(ImGuiCol_Text);

        ImGui::TextColored(conclusion.level == 1 ? color : toneMapColor(color), "%s", conclusion.title.c_str());
        ImGui::TextWrapped("%s", conclusion.text.c_str());
    }

    if (linkedAny)
    {
        TextWrappedDisabled(Tr("Margins used: %.1f%% in average FPS, %.1f%% in 1%% low, %.4f in SSIM. No score mixes the "
                               "categories."),
                            analysis.marginAvg, analysis.marginLow1, verdict.margin);
    }
    else
    {
        TextWrappedDisabled(Tr("Margins used: %.1f%% in average FPS, %.1f%% in 1%% low.%s"), analysis.marginAvg,
                            analysis.marginLow1,
                            analysis.marginFromRepeats ? Tr(" Taken from the spread between tests of the same dll.") : "");
    }

    for (const auto issue : analysis.blocking)
        ImGui::TextColored(toneMapColor(IlionTheme::Themed(ImVec4(1.0f, 0.4f, 0.3f, 1.0f), IlionTheme::Role::Bad)), "%s", Tr(CompareIssueText(issue)));

    for (const auto issue : analysis.warnings)
        ImGui::TextColored(toneMapColor(IlionTheme::Themed(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), IlionTheme::Role::Warn)), "%s", Tr(CompareIssueText(issue)));

    if (reference != nullptr)
    {
        TextWrappedColored(toneMapColor(IlionTheme::Themed(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), IlionTheme::Role::Warn)), "%s",
                           Tr("ILION can't check that the captures are from the scene of the tests."));
    }

    if (generatedFrames)
    {
        TextWrappedColored(toneMapColor(IlionTheme::Themed(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), IlionTheme::Role::Warn)), "%s",
                           Tr("Frame generation was on in at least one capture, its frame may be a generated one."));
    }

    // --- One column per dll ----------------------------------------------------------------------------------
    IlionTheme::SectionPlate(Tr("Results by DLL"));

    baseline = std::clamp(baseline, 0, dllCount - 1);

    ImGui::SetNextItemWidth(300.0f * scale);

    if (IlionTheme::BeginCombo(TrL("Differences against"), analysis.candidates[baseline].name.c_str()))
    {
        for (int i = 0; i < dllCount; i++)
        {
            if (ImGui::Selectable(StrFmt("%s##baseline%d", analysis.candidates[i].name.c_str(), i).c_str(), i == baseline))
                baseline = i;
        }

        ImGui::EndCombo();
    }

    if (ImGui::BeginTable("finalReport", dllCount + 1,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollX |
                              ImGuiTableFlags_SizingFixedFit))
    {
        ImGui::TableSetupScrollFreeze(1, 1);
        ImGui::TableSetupColumn("##metric");

        for (int i = 0; i < dllCount; i++)
        {
            ImGui::TableSetupColumn(StrFmt("%s%s##dll%d", analysis.candidates[i].name.c_str(),
                                           i == baseline ? Tr("  [reference]") : "", i)
                                        .c_str());
        }

        ImGui::TableHeadersRow();

        const auto section = [&](const char* title)
        {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", Tr(title));
        };

        // Value of every dll with its difference against the baseline, in % and in the unit of the value
        const auto row = [&](const char* label, const char* format, const auto& value, int better)
        {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(Tr(label));

            const Number base = value(baseline);

            for (int i = 0; i < dllCount; i++)
            {
                ImGui::TableNextColumn();

                const Number number = value(i);

                if (!number.valid)
                {
                    ImGui::TextDisabled("%s", Tr("N/A"));
                    continue;
                }

                ImGui::TextUnformatted(StrFmt(format, number.value).c_str());

                if (i != baseline)
                {
                    const auto percent = IlionCompare::PercentDifference(number, base);
                    const auto absolute = IlionCompare::AbsoluteDifference(number, base);

                    if (percent.valid && absolute.valid)
                        DrawDifference(StrFmt("   %+.1f %% (%+.3g)", percent.value, absolute.value), percent.value, better);
                }
            }
        };

        const auto benchmark = [&](Number Test::* field)
        { return [&, field](int i) { return analysis.candidates[i].average.*field; }; };

        const auto picture = [&](double ReportVisual::* field)
        { return [&, field](int i) { return Number { visual[i].measured > 0, visual[i].*field }; }; };

        section("Performance");
        // Last value: +1 when higher is the good way, -1 when lower is, 0 when the metric has none
        row("Average FPS", "%.1f", benchmark(&Test::avgFps), 1);
        row("1% low", "%.1f", benchmark(&Test::low1Fps), 1);
        row("0.1% low", "%.1f", benchmark(&Test::low01Fps), 1);
        row("Frame time mean", "%.2f ms", benchmark(&Test::meanMs), -1);

        section("Stability and frame times");
        row("Frame time P99", "%.2f ms", benchmark(&Test::p99Ms), -1);
        row("Variability", "%.1f %%", benchmark(&Test::variationPercent), -1);
        row("Stutters per minute", "%.1f", benchmark(&Test::stutterPerMinute), -1);

        // Closer to native is the good way; edge strength and overshoot are readings, not better or worse.
        // Only with captures of these tests.
        if (linkedAny)
        {
            section("Visual quality against the native reference");
            row("SSIM (structure)", "%.4f", picture(&ReportVisual::ssim), 1);
            row("Mean difference", "%.2f", picture(&ReportVisual::meanDifference), -1);
            row("Pixels that differ", "%.1f %%", picture(&ReportVisual::differentPercent), -1);
            row("Edge strength", "%+.1f %%", picture(&ReportVisual::edgeChangePercent), 0);
            row("Edges of the reference kept", "%.1f %%", picture(&ReportVisual::edgesKeptPercent), 1);
            row("Overshoot next to edges (exploratory)", "%.2f %%", picture(&ReportVisual::overshootPercent), 0);
        }

        // Where the numbers of every dll come from
        section("Data used");

        const auto count = [&](const char* label, const auto& value)
        {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(Tr(label));

            for (int i = 0; i < dllCount; i++)
            {
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(value(i).c_str());
            }
        };

        count("Saved tests", [&](int i) { return StrFmt("%d", (int) analysis.candidates[i].tests.size()); });
        if (linkedAny)
        {
            count("Captures of these tests", [&](int i) { return StrFmt("%d", visual[i].captures); });
            count("Captures measured", [&](int i) { return StrFmt("%d", visual[i].measured); });
            count("Left out: another scene or camera moved", [&](int i) { return StrFmt("%d", visual[i].unreliable); });
            count("Left out: can't be measured", [&](int i) { return StrFmt("%d", visual[i].failed); });
        }
        count("SHA-256", [&](int i)
              {
                  const auto& sha = tests[analysis.candidates[i].tests.front()]->dllSha256;
                  return sha.empty() ? std::string("-") : sha.substr(0, 12) + "...";
              });

        ImGui::EndTable();
    }

    TextWrappedDisabled("%s", Tr("Values of a DLL with several tests or captures are their mean. Visual values come from "
                                 "still captures, what happens in motion is not measured."));

    ImGui::End();
}

// Feeds the running benchmark with the frame that was just presented
// 1% low of the frame times the overlay keeps, recalculated twice a second while it's shown
static double gOverlayLow1Fps = 0.0;
static uint64_t gOverlayLow1Tick = 0;

static void UpdateBenchmark(State& state, double frameTime, double now)
{
    const bool running = IlionBenchmark::Running();

    // The upscaler dll can't be switched in the middle of a test
    FfxApiProxy::SetSrSwitchLocked(running);

    static int lastSlot = -1;
    static uint64_t lastDispatchCount = 0;

    const auto active = FfxApiProxy::SrDiagGetActive();
    const bool dispatched = active.slot >= 0 && (active.slot != lastSlot || active.dispatchCount != lastDispatchCount);

    lastSlot = active.slot;
    lastDispatchCount = active.dispatchCount;

    if (!running)
        return;

    // Hardware readings are taken during a test even when the overlay doesn't show them
    HwTelemetry::KeepAlive();

    const auto fg = state.currentFG;
    const bool fgActive = fg != nullptr && fg->IsActive() && !fg->IsPaused();

    IlionBenchmark::OnFrame(frameTime, now, active.module, dispatched, fgActive,
                            fgActive ? (int) fg->GetInterpolatedFrameCount() + 1 : 1, HwTelemetry::Get());
}

// One line in the performance overlay while a test runs, so it can be followed with the menu closed
static void DrawBenchmarkStatus(double now)
{
    const auto benchState = IlionBenchmark::GetState();

    if (benchState == IlionBenchmark::State::Stabilizing)
    {
        ImGui::Text(Tr("Benchmark: %s %.1f s"), Tr("STABILIZING"), IlionBenchmark::StabilizeLeftMs(now) / 1000.0);
    }
    else if (benchState == IlionBenchmark::State::Recording)
    {
        ImGui::Text(Tr("Benchmark: %s %.0f / %d s"), Tr("RECORDING"), IlionBenchmark::ElapsedMs() / 1000.0,
                    IlionBenchmark::RequestedSeconds());
    }
}

// Not available text of a hardware reading, nothing gets shown in its place
static const char* HwNotAvailable() { return Tr("N/A"); }

// Starts or stops the hardware readings depending on what the overlay shows
static void UpdateHardwareTelemetry(decltype(Config::Instance()) config)
{
    const bool wanted = config->ShowFps.value_or_default() &&
                        (config->HwGpuUsage.value_or_default() || config->HwGpuTemp.value_or_default() ||
                         config->HwGpuHotspot.value_or_default() || config->HwVram.value_or_default() ||
                         config->HwCpuUsage.value_or_default() || config->HwCpuTemp.value_or_default() ||
                         config->HwGpuPower.value_or_default() || config->HwCpuPower.value_or_default() ||
                         config->HwRam.value_or_default());

    HwTelemetry::SetPreferred((HwTelemetry::Preferred) std::clamp(config->HwSource.value_or_default(), 0, 2));
    HwTelemetry::SetEnabled(wanted);

    // Name and memory of the GPU in use, to pick the right one from the sources and for the VRAM total
    static bool hintSet = false;

    if (wanted && !hintSet)
    {
        hintSet = true;

        State::Instance().vulkanSkipHooks = true;
        const auto primaryGpu = IdentifyGpu::getPrimaryGpu();
        State::Instance().vulkanSkipHooks = false;

        HwTelemetry::SetGpuHint(primaryGpu.name, primaryGpu.dedicatedVramInBytes);
    }
}

// Rows of the vertical overlay of the industrial theme: the label at the left, the value with its unit against
// the right edge of a column of a fixed width.
static void OverlayRow(const char* label, const std::string& value, float width, const ImVec4* valueColor = nullptr)
{
    ImGui::TextDisabled("%s", label);
    ImGui::SameLine(ImGui::GetStyle().WindowPadding.x + width - ImGui::CalcTextSize(value.c_str()).x);

    if (valueColor != nullptr)
        ImGui::TextColored(*valueColor, "%s", value.c_str());
    else
        ImGui::TextUnformatted(value.c_str());
}

// A row whose value is made of pieces, each one in its own colour or in the one of the text. The pieces together
// take the place and the alignment the whole value would.
struct OverlayPart
{
    std::string text;
    const ImVec4* color = nullptr;
};

static void OverlayRowParts(const char* label, const std::vector<OverlayPart>& parts, float width)
{
    float valueWidth = 0.0f;

    for (const auto& part : parts)
        valueWidth += ImGui::CalcTextSize(part.text.c_str()).x;

    ImGui::TextDisabled("%s", label);
    ImGui::SameLine(ImGui::GetStyle().WindowPadding.x + width - valueWidth);

    bool first = true;

    for (const auto& part : parts)
    {
        if (part.text.empty())
            continue;

        if (!first)
            ImGui::SameLine(0.0f, 0.0f);

        if (part.color != nullptr)
            ImGui::TextColored(*part.color, "%s", part.text.c_str());
        else
            ImGui::TextUnformatted(part.text.c_str());

        first = false;
    }
}

// Horizontal overlay of the industrial theme: every block is a line that starts with its name, and its readings
// follow as cells, a fine metal rule between one and the next.
static void OverlayCellRowTitle(const char* title) { ImGui::TextColored(IlionTheme::TextTitle, "%s", title); }

static void OverlayCell(const char* label, const std::vector<OverlayPart>& parts)
{
    const float space = ImGui::CalcTextSize(" ").x;
    const bool labelled = label != nullptr && label[0] != 0;

    // A line never gets wider than the screen: the cell that wouldn't fit starts the next one
    float cellWidth = labelled ? ImGui::CalcTextSize(label).x + space : 0.0f;

    for (const auto& part : parts)
        cellWidth += ImGui::CalcTextSize(part.text.c_str()).x;

    const ImGuiWindow* window = ImGui::GetCurrentWindow();
    const float lineWidth = window->DC.CursorPosPrevLine.x - window->Pos.x + space * 2.5f + cellWidth;

    if (lineWidth > ImGui::GetIO().DisplaySize.x - ImGui::GetStyle().WindowPadding.x * 2.0f)
    {
        // Under the first cell of the line above, past where the name of the block is
        ImGui::Dummy(ImVec2(space * 2.0f, 0.0f));
    }

    ImGui::SameLine(0.0f, space * 2.5f);

    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float x = std::floor(pos.x - space * 1.25f);

    ImGui::GetWindowDrawList()->AddLine(ImVec2(x, pos.y + 1.0f), ImVec2(x, pos.y + ImGui::GetTextLineHeight() - 1.0f),
                                        ImGui::GetColorU32(IlionTheme::Alpha(IlionTheme::Metal, 0.80f)));

    // A cell without a label is only its value (the name of a component)

    if (labelled)
        ImGui::TextDisabled("%s", label);

    bool first = true;

    for (const auto& part : parts)
    {
        if (part.text.empty())
            continue;

        if (labelled || !first)
            ImGui::SameLine(0.0f, first ? space : 0.0f);

        if (part.color != nullptr)
            ImGui::TextColored(*part.color, "%s", part.text.c_str());
        else
            ImGui::TextUnformatted(part.text.c_str());

        first = false;
    }
}

static void OverlayCell(const char* label, const std::string& value, const ImVec4* valueColor = nullptr)
{
    OverlayCell(label, std::vector<OverlayPart> { { value, valueColor } });
}

// Name of a block of rows over a metal rule as wide as the column. Blocks after the first one keep their distance.
static void OverlayBlockTitle(const char* title, float width, bool first)
{
    const float gap = ImGui::GetStyle().ItemSpacing.y;

    if (!first)
        ImGui::Dummy(ImVec2(0.0f, gap));

    ImGui::TextColored(IlionTheme::TextTitle, "%s", title);

    const ImVec2 min = ImGui::GetItemRectMin();
    const float y = ImGui::GetItemRectMax().y + gap * 0.5f;

    ImGui::GetWindowDrawList()->AddLine(ImVec2(min.x, y), ImVec2(min.x + width, y),
                                        ImGui::GetColorU32(IlionTheme::Metal));
    ImGui::Dummy(ImVec2(0.0f, gap * 0.5f));
}

// GPU readings on the left, CPU readings on the right, only the ones that are enabled.
// With alignedWidth they are stacked as rows of that width instead, with cells they follow each other on one
// line. The same readings every time.
static void DrawHardwareTelemetry(decltype(Config::Instance()) config, float scale, float alignedWidth = 0.0f,
                                  bool cells = false)
{
    const bool gpuUsage = config->HwGpuUsage.value_or_default();
    const bool gpuTemp = config->HwGpuTemp.value_or_default();
    const bool gpuHotspot = config->HwGpuHotspot.value_or_default();
    const bool vram = config->HwVram.value_or_default();
    const bool cpuUsage = config->HwCpuUsage.value_or_default();
    const bool cpuTemp = config->HwCpuTemp.value_or_default();
    const bool gpuPower = config->HwGpuPower.value_or_default();
    const bool cpuPower = config->HwCpuPower.value_or_default();
    const bool ram = config->HwRam.value_or_default();

    const bool anyGpu = gpuUsage || gpuTemp || gpuHotspot || vram || gpuPower;
    const bool anyCpu = cpuUsage || cpuTemp || cpuPower || ram;

    if (!anyGpu && !anyCpu)
        return;

    const auto snapshot = HwTelemetry::Get();

    // Readings that stopped arriving aren't shown as current
    const bool stale = snapshot.polledTick == 0 || GetTickCount64() - snapshot.polledTick > 5000;

    // RAM in GB, from the MB the sources give
    const auto ramText = [&]() -> std::string
    {
        const auto& value = snapshot.values[HwTelemetry::RamUsed];
        return stale || !value.valid ? std::string(HwNotAvailable()) : StrFmt("%.1f GB", value.value / 1024.0f);
    };

    // Component names in #39FF14, temperatures in #CC0000 and the hotspot in #FF6A00, number and unit
    const ImVec4 componentColor = toneMapColor(ImVec4(57.0f / 255.0f, 1.0f, 20.0f / 255.0f, 1.0f));
    const ImVec4 temperatureColor = toneMapColor(ImVec4(204.0f / 255.0f, 0.0f, 0.0f, 1.0f));
    const ImVec4 hotspotColor = toneMapColor(ImVec4(1.0f, 106.0f / 255.0f, 0.0f, 1.0f));

    if (cells)
    {
        const auto cell = [&](const char* label, HwTelemetry::Metric metric, const char* format,
                              const ImVec4* valueColor = nullptr)
        {
            const auto& value = snapshot.values[metric];

            if (stale || !value.valid)
                OverlayCell(label, HwNotAvailable());
            else
                OverlayCell(label, StrFmt(format, value.value), valueColor);
        };

        OverlayCellRowTitle("HARDWARE");

        if (anyGpu)
        {
            OverlayCell("", "GPU", &componentColor);

            if (gpuUsage)
                cell(Tr("Usage"), HwTelemetry::GpuUsage, "%.0f %%");

            if (gpuTemp)
                cell(Tr("Temp"), HwTelemetry::GpuTemp, "%.0f \xC2\xB0""C", &temperatureColor);

            if (gpuHotspot)
                cell("Hotspot", HwTelemetry::GpuHotspot, "%.0f \xC2\xB0""C", &hotspotColor);

            if (gpuPower)
                cell(Tr("Power"), HwTelemetry::GpuPower, "%.0f W");

            if (vram)
            {
                const auto& used = snapshot.values[HwTelemetry::VramUsed];
                const auto& total = snapshot.values[HwTelemetry::VramTotal];

                if (stale || !used.valid)
                    OverlayCell("VRAM", HwNotAvailable());
                else if (total.valid)
                    OverlayCell("VRAM", StrFmt("%.1f / %.1f GB", used.value / 1024.0f, total.value / 1024.0f));
                else
                    OverlayCell("VRAM", StrFmt("%.1f GB", used.value / 1024.0f));
            }
        }

        if (anyCpu)
        {
            OverlayCell("", "CPU", &componentColor);

            if (cpuUsage)
                cell(Tr("Usage"), HwTelemetry::CpuUsage, "%.0f %%");

            if (cpuTemp)
                cell(Tr("Temp"), HwTelemetry::CpuTemp, "%.0f \xC2\xB0""C", &temperatureColor);

            if (cpuPower)
                cell(Tr("Power"), HwTelemetry::CpuPower, "%.0f W");

            if (ram)
                OverlayCell("RAM", ramText());
        }

        return;
    }

    if (alignedWidth > 0.0f)
    {
        const auto aligned = [&](const char* label, HwTelemetry::Metric metric, const char* format,
                                 const ImVec4* valueColor = nullptr)
        {
            const auto& value = snapshot.values[metric];

            if (stale || !value.valid)
                OverlayRow(label, HwNotAvailable(), alignedWidth);
            else
                OverlayRow(label, StrFmt(format, value.value), alignedWidth, valueColor);
        };

        OverlayBlockTitle("HARDWARE", alignedWidth, false);

        if (anyGpu)
        {
            ImGui::TextColored(componentColor, Tr("GPU"));

            if (gpuUsage)
                aligned(Tr("Usage"), HwTelemetry::GpuUsage, "%.0f %%");

            if (gpuTemp)
                aligned(Tr("Temp"), HwTelemetry::GpuTemp, "%.0f \xC2\xB0""C", &temperatureColor);

            if (gpuHotspot)
                aligned("Hotspot", HwTelemetry::GpuHotspot, "%.0f \xC2\xB0""C", &hotspotColor);

            if (gpuPower)
                aligned(Tr("Power"), HwTelemetry::GpuPower, "%.0f W");

            if (vram)
            {
                const auto& used = snapshot.values[HwTelemetry::VramUsed];
                const auto& total = snapshot.values[HwTelemetry::VramTotal];

                if (stale || !used.valid)
                    OverlayRow("VRAM", HwNotAvailable(), alignedWidth);
                else if (total.valid)
                    OverlayRow("VRAM", StrFmt("%.1f / %.1f GB", used.value / 1024.0f, total.value / 1024.0f), alignedWidth);
                else
                    OverlayRow("VRAM", StrFmt("%.1f GB", used.value / 1024.0f), alignedWidth);
            }
        }

        if (anyCpu)
        {
            if (anyGpu)
                ImGui::Dummy(ImVec2(0.0f, ImGui::GetStyle().ItemSpacing.y * 0.5f));

            ImGui::TextColored(componentColor, Tr("CPU"));

            if (cpuUsage)
                aligned(Tr("Usage"), HwTelemetry::CpuUsage, "%.0f %%");

            if (cpuTemp)
                aligned(Tr("Temp"), HwTelemetry::CpuTemp, "%.0f \xC2\xB0""C", &temperatureColor);

            if (cpuPower)
                aligned(Tr("Power"), HwTelemetry::CpuPower, "%.0f W");

            if (ram)
                OverlayRow("RAM", ramText(), alignedWidth);
        }

        return;
    }

    const auto row = [&](const char* label, HwTelemetry::Metric metric, const char* format,
                         const ImVec4* valueColor = nullptr)
    {
        const auto& value = snapshot.values[metric];

        if (stale || !value.valid)
        {
            ImGui::Text("%-8s %s", label, HwNotAvailable());
        }
        else if (valueColor == nullptr)
        {
            ImGui::Text("%-8s %s", label, StrFmt(format, value.value).c_str());
        }
        else
        {
            // The same line in two pieces, only the value takes the color
            ImGui::Text("%-8s ", label);
            ImGui::SameLine(0.0f, 0.0f);
            ImGui::TextColored(*valueColor, "%s", StrFmt(format, value.value).c_str());
        }
    };

    ImGui::Spacing();

    if (anyGpu)
    {
        ImGui::BeginGroup();
        ImGui::TextColored(componentColor, Tr("GPU"));

        if (gpuUsage)
            row(Tr("Usage"), HwTelemetry::GpuUsage, "%3.0f %%");

        if (gpuTemp)
            row(Tr("Temp"), HwTelemetry::GpuTemp, "%3.0f \xC2\xB0""C", &temperatureColor);

        if (gpuHotspot)
            row("Hotspot", HwTelemetry::GpuHotspot, "%3.0f \xC2\xB0""C", &hotspotColor);

        if (gpuPower)
            row(Tr("Power"), HwTelemetry::GpuPower, "%3.0f W");

        if (vram)
        {
            const auto& used = snapshot.values[HwTelemetry::VramUsed];
            const auto& total = snapshot.values[HwTelemetry::VramTotal];

            if (stale || !used.valid)
                ImGui::Text("%-8s %s", "VRAM", HwNotAvailable());
            else if (total.valid)
                ImGui::Text(Tr("%-8s %.1f / %.1f GB"), "VRAM", used.value / 1024.0f, total.value / 1024.0f);
            else
                ImGui::Text(Tr("%-8s %.1f GB"), "VRAM", used.value / 1024.0f);
        }

        ImGui::EndGroup();
    }

    if (anyCpu)
    {
        if (anyGpu)
            ImGui::SameLine(0.0f, 24.0f * scale);

        ImGui::BeginGroup();
        ImGui::TextColored(componentColor, Tr("CPU"));

        if (cpuUsage)
            row(Tr("Usage"), HwTelemetry::CpuUsage, "%3.0f %%");

        if (cpuTemp)
            row(Tr("Temp"), HwTelemetry::CpuTemp, "%3.0f \xC2\xB0""C", &temperatureColor);

        if (cpuPower)
            row(Tr("Power"), HwTelemetry::CpuPower, "%3.0f W");

        if (ram)
            ImGui::Text("%-8s %s", "RAM", ramText().c_str());

        ImGui::EndGroup();
    }
}

// Feeds the upscaler comparison with the frame time that was just measured and the dll that executed the upscaling
static void UpdateFsr4Comparison(State& state, double frameTime, double now)
{
    if (!FSR4Comparison::Enabled())
        return;

    const auto active = FfxApiProxy::SrDiagGetActive();
    const auto fg = state.currentFG;
    const bool fgActive = fg != nullptr && fg->IsActive() && !fg->IsPaused();

    const auto feature = state.currentFeature;

    // An FFX upscaler: the dll that executed the dispatch, as the proxy's diagnostics follow it
    if (feature == nullptr || RunsSwitchedFfx(feature->GetUpscalerType()))
    {
        FSR4Comparison::OnFrame(frameTime, now, active.module, active.name, active.slot, active.dispatchCount,
                                active.errorCount, active.requestTick, fgActive);
        return;
    }

    // Any other DX12 upscaler (DLSS through d4r, XeSS, FSR 2): its own count of evaluated frames. Each backend gets
    // its own box, told apart by an address that stands for it.
    static char backendMarks[64] = {};
    const int type = std::clamp((int) feature->GetUpscalerType(), 0, 63);
    const auto label = UpscalerLabel(feature);

    FSR4Comparison::OnFrame(frameTime, now, reinterpret_cast<HMODULE>(&backendMarks[type]), label.c_str(), 1000 + type,
                            (uint64_t) feature->FrameCount(), 0, active.requestTick, fgActive);
}

// A box per measured upscaler under the performance overlay's own lines, drawn with the window's draw list.
// With columnWidth they share exactly that width, the one of the rows above them: the same left and right
// edges, even gaps, and texts made smaller or split in two lines when a box is too narrow for them.
static void DrawFsr4ComparisonPanel(float scale, float columnWidth = 0.0f)
{
    if (!FSR4Comparison::Enabled())
        return;

    ImGui::Spacing();

    const auto font = ImGui::GetFont();
    const float textSize = ImGui::GetFontSize();
    const float smallSize = textSize * 0.85f;
    const float bigSize = textSize * 1.8f;
    const float padding = 5.0f * scale;
    const float gap = 6.0f * scale;
    const float lineGap = 2.0f * scale;

    const bool fitted = columnWidth > 0.0f;
    const float minBoxWidth = textSize * 7.5f;
    const int boxCount = FSR4Comparison::BoxCount();

    // Nothing executed yet since the mode was turned on
    if (boxCount == 0)
    {
        ImGui::TextDisabled("%s", Tr("Upscaler comparison: waiting for an upscale..."));
        return;
    }

    const float availWidth = fitted ? columnWidth : ImGui::GetContentRegionAvail().x;
    const float gaps = gap * (float) (boxCount - 1);
    const float boxWidth = fitted ? ImFloor((availWidth - gaps) / (float) boxCount)
                                  : std::max(minBoxWidth, (availWidth - gaps) / (float) boxCount);
    const float innerWidth = std::max(1.0f, boxWidth - padding * 2.0f);

    // What every box says, known before drawing so the three of them can use the same sizes
    struct BoxText
    {
        char average[16] = "--";
        char minMax[48] = "";
        char minimum[24] = "";
        char maximum[24] = "";
        const char* status = "";
    };

    BoxText texts[FSR4Comparison::MaxBoxes];

    for (int i = 0; i < boxCount; i++)
    {
        const auto& box = FSR4Comparison::GetBox(i);
        auto& text = texts[i];

        snprintf(text.minMax, sizeof(text.minMax), "%s", Tr("min --  max --"));
        snprintf(text.minimum, sizeof(text.minimum), "%s", Tr("min --"));
        snprintf(text.maximum, sizeof(text.maximum), "%s", Tr("max --"));

        if (box.HasResult())
        {
            snprintf(text.average, sizeof(text.average), "%.1f", box.AvgFps());
            snprintf(text.minMax, sizeof(text.minMax), Tr("min %.0f  max %.0f"), box.MinFps(), box.MaxFps());
            snprintf(text.minimum, sizeof(text.minimum), Tr("min %.0f"), box.MinFps());
            snprintf(text.maximum, sizeof(text.maximum), Tr("max %.0f"), box.MaxFps());
        }

        text.status = Tr(FSR4Comparison::StatusName(box.status));
    }

    // Size at which a text fits inside a box. Only used in a column of fixed width.
    const auto fit = [&](const char* text, float size)
    {
        if (!fitted)
            return size;

        const float width = font->CalcTextSizeA(size, FLT_MAX, 0.0f, text).x;
        return width > innerWidth ? size * innerWidth / width : size;
    };

    float labelSize = smallSize, averageSize = bigSize, minMaxSize = smallSize, statusSize = smallSize;
    bool twoLines = false;

    if (fitted)
    {
        for (int i = 0; i < boxCount; i++)
        {
            labelSize = std::min(labelSize, fit(Tr(FSR4Comparison::GetBox(i).label), smallSize));
            averageSize = std::min(averageSize, fit(texts[i].average, bigSize));
            statusSize = std::min(statusSize, fit(texts[i].status, smallSize));

            // Minimum and maximum go on a line each before they get too small to read
            if (fit(texts[i].minMax, smallSize) < smallSize * 0.85f)
                twoLines = true;
        }

        for (int i = 0; i < boxCount; i++)
        {
            if (twoLines)
            {
                minMaxSize = std::min(minMaxSize, fit(texts[i].minimum, smallSize));
                minMaxSize = std::min(minMaxSize, fit(texts[i].maximum, smallSize));
            }
            else
            {
                minMaxSize = std::min(minMaxSize, fit(texts[i].minMax, smallSize));
            }
        }
    }

    const int smallLines = twoLines ? 4 : 3;
    const float boxHeight = padding * 2.0f + smallSize * (float) smallLines + bigSize + lineGap * (float) smallLines;

    const ImVec2 origin = ImGui::GetCursorScreenPos();
    auto drawList = ImGui::GetWindowDrawList();

    const ImU32 textColor = ImGui::GetColorU32(ImGuiCol_Text);
    const ImU32 dimColor = ImGui::GetColorU32(ImGuiCol_Text, 0.6f);
    const ImU32 borderColor = ImGui::GetColorU32(ImGuiCol_Text, 0.25f);
    const ImU32 recordingColor = ImGui::ColorConvertFloat4ToU32(toneMapColor(IlionTheme::Themed(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), IlionTheme::Role::Good)));
    const ImU32 stabilizingColor = ImGui::ColorConvertFloat4ToU32(toneMapColor(IlionTheme::Themed(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), IlionTheme::Role::Warn)));

    const auto centered = [&](const char* text, float size, float boxLeft, float y, ImU32 color)
    {
        const float width = font->CalcTextSizeA(size, FLT_MAX, 0.0f, text).x;
        drawList->AddText(font, size, ImVec2(boxLeft + (boxWidth - width) * 0.5f, y), color, text);
    };

    bool fgSeen = false;

    for (int i = 0; i < boxCount; i++)
    {
        const auto& box = FSR4Comparison::GetBox(i);
        const bool current = i == FSR4Comparison::CurrentBox() && (box.status == FSR4Comparison::Status::Recording ||
                                                                   box.status == FSR4Comparison::Status::Stabilizing);

        // In a column the first box starts at its left edge and the last one ends at its right edge
        const float left = fitted ? ImFloor(origin.x + (boxCount > 1 ? (availWidth - boxWidth) / (float) (boxCount - 1) : 0.0f) * (float) i)
                                  : origin.x + (boxWidth + gap) * (float) i;
        const ImVec2 boxMin(left, origin.y);
        const ImVec2 boxMax(left + boxWidth, origin.y + boxHeight);

        drawList->AddRect(boxMin, boxMax, current ? recordingColor : borderColor, 3.0f * scale, 0,
                          current ? 2.0f * scale : 1.0f);

        const auto& text = texts[i];

        ImU32 statusColor = dimColor;

        if (box.status == FSR4Comparison::Status::Recording)
            statusColor = recordingColor;
        else if (box.status == FSR4Comparison::Status::Stabilizing)
            statusColor = stabilizingColor;

        float y = origin.y + padding;

        // A text made smaller stays in the middle of the height its line has
        const auto line = [&](const char* value, float size, float lineHeight, ImU32 color)
        {
            centered(value, size, left, y + (lineHeight - size) * 0.5f, color);
            y += lineHeight + lineGap;
        };

        line(Tr(box.label), labelSize, smallSize, dimColor);
        line(text.average, averageSize, bigSize, textColor);

        if (twoLines)
        {
            line(text.minimum, minMaxSize, smallSize, dimColor);
            line(text.maximum, minMaxSize, smallSize, dimColor);
        }
        else
        {
            line(text.minMax, minMaxSize, smallSize, dimColor);
        }

        line(text.status, statusSize, smallSize, statusColor);

        fgSeen = fgSeen || box.fgSeen;
    }

    ImGui::Dummy(ImVec2(fitted ? availWidth : boxWidth * (float) boxCount + gaps, boxHeight));

    // The notes under the boxes stay inside the column too
    const auto note = [&](const char* text)
    {
        if (!fitted)
        {
            ImGui::TextDisabled("%s", text);
            return;
        }

        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + availWidth);
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextWrapped("%s", text);
        ImGui::PopStyleColor();
        ImGui::PopTextWrapPos();
    };

    if (FSR4Comparison::OtherDllActive())
        note(Tr("More upscalers than boxes in this session, the last one isn't measured"));

    if (fgSeen)
        note(Tr("Includes frames presented by frame generation"));
}

void MenuCommon::UpdateMenuInputMode(RenderMenuContext& ctx)
{
    auto& io = ctx.io;

    // Moved here to prevent gamepad key replay
    if (_isVisible)
    {
        if (hasGamepad)
            io.BackendFlags |= ImGuiBackendFlags_HasGamepad;

        io.ConfigFlags = ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    }
    else
    {
        capturingKey = false;
        hasGamepad = (io.BackendFlags & ImGuiBackendFlags_HasGamepad) != 0;
        io.BackendFlags &= ~ImGuiBackendFlags_HasGamepad;
        io.ConfigFlags = ImGuiConfigFlags_NoMouse | ImGuiConfigFlags_NoMouseCursorChange | ImGuiConfigFlags_NoKeyboard;
    }
}

void MenuCommon::HandleMenuShortcuts(RenderMenuContext& ctx)
{
    auto& state = ctx.state;
    auto config = ctx.config;
    auto& io = ctx.io;

    // Handle Inputs
    {
        if (inputFG)
        {
            inputFG = false;

            if (state.activeFgInput != FGInput::NoFG && state.activeFgOutput != FGOutput::NoFG &&
                (state.currentFGSwapchain != nullptr || state.activeFgInput == FGInput::NvngxFG))
            {
                if (fgHotkeyLeftDefault)
                {
                    // Second press goes back to following the game
                    fgHotkeyLeftDefault = false;
                    config->FGEnabled = std::optional<bool> {};
                    LOG_DEBUG("FG toggle key pressed, following the game again");
                }
                else if (config->FGFollowsGame())
                {
                    // Volatile so the ini keeps auto
                    fgHotkeyLeftDefault = true;
                    config->FGEnabled.set_volatile_value(!config->IsFGEnabled());
                    LOG_DEBUG("FG toggle key pressed, overriding the game with {}", config->FGEnabled.value());
                }
                else
                {
                    config->FGEnabled = !config->IsFGEnabled();
                    LOG_DEBUG("FG toggle key pressed, setting FGEnabled to {}", config->FGEnabled.value());
                }

                if (config->IsFGEnabled())
                    state.fgChanged = true;
            }
        }

        if (inputFps)
        {
            inputFps = false;
            config->ShowFps = !config->ShowFps.value_or_default();
        }

        if (inputFpsCycle && config->ShowFps.value_or_default())
            config->FpsOverlayType = (FpsOverlay) ((config->FpsOverlayType.value_or_default() + 1) % FpsOverlay_COUNT);

        if (inputMenu)
        {
            inputMenu = false;
            _isVisible = !_isVisible;

            LOG_DEBUG("Menu key pressed, {0}", _isVisible ? "opening ImGui" : "closing ImGui");

            if (_isVisible)
            {
                io.ClearEventsQueue();
                io.ClearInputKeys();
                io.ClearInputMouse();

                OptiInput::ResetMenuInputTransientState();

                ApplyThemeStyle();

                refreshRate = Util::GetActiveRefreshRate(_handle);

                auto optiPath = std::filesystem::path(Config::Instance()->MainDllPath.value());
                state.artursFgFileAvailable = enablerExists.Get(optiPath / L"dlss-enabler-headless.dll");
                state.nukemsFgFileAvailable = nukemsExists.Get(optiPath / L"dlssg_to_fsr3_amd_is_better.dll");

                if (State::Instance().currentFeature != nullptr)
                {
                    auto upscalerType = State::Instance().currentFeature->GetUpscalerType();

                    if (upscalerType == Upscaler::DLSSD)
                        comboPreset = config->DLSSDRenderPresetForAll.value_or_default();
                    else if (upscalerType == Upscaler::DLSS || upscalerType == Upscaler::DLSS_on12)
                        comboPreset = config->RenderPresetForAll.value_or_default();
                }
            }
            else
            {
                ImGui::CloseCurrentPopup();

                _showMipmapCalcWindow = false;
                _showHudlessWindow = false;
            }

            io.MouseDrawCursor = _isVisible;
            io.WantCaptureKeyboard = _isVisible;
            io.WantCaptureMouse = _isVisible;
        }

        inputFpsCycle = false;
    }
}

void MenuCommon::UpdateVersionAndStartupNotifications(RenderMenuContext& ctx)
{
    auto& state = ctx.state;
    auto config = ctx.config;
    auto& now = ctx.now;
    auto& versionStatus = ctx.versionStatus;

    constexpr double splashTime = 7000.0;
    constexpr int updateNoticeTime = 10000;

    // Version check state is copied while locked, then consumed by the UI render pass.
    {
        std::scoped_lock lock(state.versionCheckMutex);
        versionStatus.completed = state.versionCheckCompleted;
        versionStatus.updateAvailable = state.updateAvailable;
        versionStatus.latestTag = state.latestVersionTag;
        versionStatus.latestUrl = state.latestVersionUrl;
        versionStatus.error = state.versionCheckError;
    }

    ctx.currentVersionText = VersionCheck::CurrentVersionString();

    if (versionStatus.completed && versionStatus.updateAvailable && !versionStatus.latestTag.empty())
    {
        if (updateNoticeTag != versionStatus.latestTag)
        {
            updateNoticeTag = versionStatus.latestTag;
            updateNoticeUrl = versionStatus.latestUrl;
            const auto notice = [&]()
            {
                ImGuiToast updateNotification { ImGuiToastType::Error, updateNoticeTime };
                updateNotification.setTitle("OptiScaler Update available");
                updateNotification.setContent(
                    "Press %s for more info",
                    Keybind::KeyNameFromVirtualKeyCode(config->ShortcutKey.value_or_default()).c_str());
                ImGui::InsertNotification(updateNotification);
                return true;
            };
            static auto res = notice();
        }
    }

    // One-shot startup warning notifications.
    if (!state.postDone)
    {
        if (state.postCodes & PostCode::SlPluginsAlreadyInMemory)
        {
            auto filename = Util::DllPath().filename().string();
            to_lower_in_place(filename);

            ImGuiToast notification { ImGuiToastType::Warning, 10000 };
            notification.setTitle("Late Streamline hook detected");
            notification.setContent(
                "Consider renaming OptiScaler from %s to other supported name.\nYou may experience issues otherwise.",
                filename.c_str());
            ImGui::InsertNotification(notification);
        }

        if (state.postCodes & PostCode::TryingFsr4Fp8OnUnsupported)
        {
            ImGuiToast notification { ImGuiToastType::Warning, 10000 };
            notification.setTitle("Silly goose detected");
            notification.setContent("FSR 4 FP8 only works on AMD");
            ImGui::InsertNotification(notification);
        }

        state.postDone = true;
    }

    // Initialize splash timing and select the splash text once per process.
    if (splashLimit < 1.0f)
    {
        splashStart = now + 100.0;
        splashLimit = splashStart + splashTime;

        std::srand(static_cast<unsigned>(std::time(nullptr)));
        splashMessage = splashText[std::rand() % splashText.size()];
    }
}

void MenuCommon::BeginMenuFrameIfNeeded(RenderMenuContext& ctx)
{
    auto& state = ctx.state;
    auto config = ctx.config;
    auto& io = ctx.io;
    auto& now = ctx.now;
    auto& newFrame = ctx.newFrame;

    // New frame check
    if ((!config->DisableSplash.value_or_default() && now > splashStart && now < splashLimit) ||
        config->ShowFps.value_or_default() || _isVisible || ImGui::notifications.size() > 0)
    {
        if (!_isUWP)
        {
            ImGui_ImplWin32_NewFrame();
        }
        else
        {
            ImVec2 displaySize { state.screenWidth, state.screenHeight };
            ImGui_ImplUwp_NewFrame(displaySize);
        }

        OptiInput::FeedImGui(_isVisible);

        MenuHdrCheck(io);
        ImGui::NewFrame();

        newFrame = true;
    }
}

void MenuCommon::RenderSplashWindow(RenderMenuContext& ctx)
{
    auto config = ctx.config;
    auto& io = ctx.io;
    auto& now = ctx.now;

    constexpr double fadeTime = 1000.0;

    // Splash screen
    if (!config->DisableSplash.value_or_default())
    {
        if (now > splashStart && now < splashLimit)
        {

            ImGui::SetNextWindowSize({ 0.0f, 0.0f });
            ImGui::SetNextWindowBgAlpha(config->FpsOverlayAlpha.value_or_default());
            ImGui::SetNextWindowPos(splashPosition, ImGuiCond_Always);

            float windowAlpha = 1.0f;
            if (auto diff = now - splashStart; diff < fadeTime)
                windowAlpha = static_cast<float>(diff / fadeTime);
            else if (auto diff = splashLimit - now; diff < fadeTime)
                windowAlpha = static_cast<float>(diff / fadeTime);

            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, windowAlpha);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12, 8));
            ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 0));

            if (!config->OverlaysUseTheme.value_or_default())
            {
                ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_Text, toneMapColor(ImVec4(1.0f, 1.0f, 1.0f, 1.0f)));
            }

            if (ImGui::Begin("Splash", nullptr,
                             ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDecoration |
                                 ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing |
                                 ImGuiWindowFlags_NoNav))
            {
                float splashScale = 1.0f;
                float baseScaleHeight = 720.0f;

                if (io.DisplaySize.y > baseScaleHeight)
                    splashScale = io.DisplaySize.y / baseScaleHeight;

                if (config->UseHQFont.value_or_default())
                    ImGui::PushFontSize(std::round(splashScale * fontSize));
                else
                    ImGui::SetWindowFontScale(splashScale);

                ImGui::Text(Tr("OptiScaler - %s for menu"),
                            Keybind::KeyNameFromVirtualKeyCode(config->ShortcutKey.value_or_default()).c_str());
                ImGui::TextColored(toneMapColor(ImVec4(1.0f, 1.0f, 1.0f, 0.7f)), splashMessage.c_str());

                splashSize = ImGui::GetWindowSize();

                if (config->UseHQFont.value_or_default())
                    ImGui::PopFontSize();

                ImGui::End();

                splashPosition.x = 0.0f; // io.DisplaySize.x - splashWinSize.x;
                splashPosition.y = io.DisplaySize.y - splashSize.y;
            }

            if (!config->OverlaysUseTheme.value_or_default())
                ImGui::PopStyleColor(4);
            else
                ImGui::PopStyleColor(2);

            ImGui::PopStyleVar(2);
        }
    }
}

void MenuCommon::RenderNotifications(RenderMenuContext& ctx)
{
    auto config = ctx.config;
    auto& io = ctx.io;

    // Notifications
    const UiTargetMode uiTargetMode = getUiTargetMode();
    const bool tonemapRequired = uiTargetMode != UiTargetMode::SDR;

    float screenHeight = State::Instance().screenHeight;
    if (io.DisplaySize.y != 0)
        screenHeight = io.DisplaySize.y;

    // Map resolution height to scale, 0.5 for 480p, 2.0 for 1440p
    constexpr float slope = (2.0f - 0.5f) / (1440.f - 480.f);
    float notificationScale = 0.5f + slope * (screenHeight - 480.f);
    notificationScale = std::clamp(notificationScale, 0.5f, 2.0f);

    if (config->UseHQFont.value_or_default())
        ImGui::PushFontSize(std::round(notificationScale * fontSize));

    // No fallback font, SetWindowFontScale needs to be called after Begin()

    ImGui::RenderNotifications(ImGuiToastPos::TopCenter, notificationScale, tonemapRequired);

    if (config->UseHQFont.value_or_default())
        ImGui::PopFontSize();
}

void MenuCommon::UpdateFrameTimeAverages(RenderMenuContext& ctx)
{
    auto& state = ctx.state;
    auto config = ctx.config;
    auto& frameTime = ctx.frameTime;
    auto& frameRate = ctx.frameRate;
    auto& frameTimesCalculated = ctx.frameTimesCalculated;
    auto& menuResScale = ctx.menuResScale;
    auto& fpsScale = ctx.fpsScale;
    auto& averageFrameTime = ctx.averageFrameTime;
    auto& averageUpscalerFT = ctx.averageUpscalerFT;

    // FPS Overlay font
    fpsScale = config->FpsScale.value_or(menuResScale);

    // Update frame time & upscaler time averages
    averageFrameTime = 0.0f;
    averageUpscalerFT = 0.0f;

    if (config->ShowFps.value_or_default() || _isVisible)
    {
        float frameCnt = 0;
        frameTime = 0;
        for (size_t i = 299; i > 199; i--)
        {
            if (state.frameTimes[i] > 0.0)
            {
                frameTime += state.frameTimes[i];
                frameCnt++;
            }
        }

        frameTime /= frameCnt;
        frameRate = 1000.0 / frameTime;
        frameTimesCalculated = true;

        float lastFT = static_cast<float>(state.frameTimes.empty() ? 0.0f : state.frameTimes.back());
        float lastUT = static_cast<float>(state.upscaleTimes.empty() ? 0.0f : state.upscaleTimes.back());
        gFrameTimes.Push(lastFT);
        gUpscalerTimes.Push(lastUT);

        averageFrameTime = gFrameTimes.Average();
        averageUpscalerFT = gUpscalerTimes.Average();

        if (config->FpsOverlayLow1.value_or_default() && GetTickCount64() - gOverlayLow1Tick > 500)
        {
            gOverlayLow1Tick = GetTickCount64();

            std::vector<double> times;
            times.reserve(state.frameTimes.size());

            for (const double time : state.frameTimes)
            {
                if (time > 0.0)
                    times.push_back(time);
            }

            gOverlayLow1Fps = 0.0;

            // Not before there are enough frames for 1% to mean something
            if (times.size() >= 100)
            {
                const size_t worst = (std::max)((size_t) 1, times.size() / 100);
                std::partial_sort(times.begin(), times.begin() + worst, times.end(), std::greater<double>());

                double sum = 0.0;
                for (size_t i = 0; i < worst; i++)
                    sum += times[i];

                gOverlayLow1Fps = 1000.0 / (sum / (double) worst);
            }
        }
    }
}

void MenuCommon::RenderPerformanceOverlay(RenderMenuContext& ctx)
{
    auto& state = ctx.state;
    auto config = ctx.config;
    auto& io = ctx.io;
    auto& currentFeature = ctx.currentFeature;
    auto& now = ctx.now;
    auto& frameTime = ctx.frameTime;
    auto& frameRate = ctx.frameRate;
    auto& menuResScale = ctx.menuResScale;
    auto& fpsScale = ctx.fpsScale;
    auto& averageFrameTime = ctx.averageFrameTime;
    auto& averageUpscalerFT = ctx.averageUpscalerFT;

    // If Fps overlay is visible
    if (config->ShowFps.value_or_default())
    {
        bool stylePushed = false;

        const static auto defaultStyle = ImGuiStyle();

        // Rescale the fps overlay every frame because it shares style with the main menu
        if (config->FpsScale.has_value() && config->FpsScale.value() != menuResScale)
        {
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, defaultStyle.WindowPadding * fpsScale);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, defaultStyle.FramePadding * fpsScale);
            ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, defaultStyle.CellPadding * fpsScale);
            ImGui::PushStyleVar(ImGuiStyleVar_SeparatorTextPadding, defaultStyle.SeparatorTextPadding * fpsScale);

            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, defaultStyle.ItemSpacing * fpsScale);
            ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, defaultStyle.ItemInnerSpacing * fpsScale);
            ImGui::PushStyleVar(ImGuiStyleVar_IndentSpacing, defaultStyle.IndentSpacing * fpsScale);

            stylePushed = true;
        }

        // Set overlay position
        ImGui::SetNextWindowPos(overlayPosition, ImGuiCond_Always);

        // Set overlay window properties
        // Industrial theme: a thin metal edge around a graphite plate. Classic: no border.
        const bool industrialOverlay = IlionTheme::industrial;

        ImGui::PushStyleColor(ImGuiCol_Border, industrialOverlay
                                                   ? ImGui::GetColorU32(IlionTheme::Alpha(IlionTheme::Metal, 0.70f))
                                                   : IM_COL32(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 0)); // Transparent frame background

        if (!config->OverlaysUseTheme.value_or_default())
        {
            ImGui::PushStyleColor(ImGuiCol_Text,
                                  toneMapColor(industrialOverlay ? IlionTheme::Text : ImVec4(1.0f, 1.0f, 1.0f, 1.0f)));
            ImGui::PushStyleColor(ImGuiCol_WindowBg,
                                  industrialOverlay ? IlionTheme::GraphiteDeep : ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
        }

        ImGui::SetNextWindowBgAlpha(config->FpsOverlayAlpha.value_or_default()); // Transparent background

        if (!config->OverlaysUseTheme.value_or_default())
        {
            ImVec4 green = industrialOverlay ? IlionTheme::LampGreen : ImVec4(0.0f, 1.0f, 0.0f, 1.0f);
            ImGui::PushStyleColor(ImGuiCol_PlotLines, toneMapColor(green));
        }

        if (ImGui::Begin("Performance Overlay", nullptr,
                         ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDecoration |
                             ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing |
                             ImGuiWindowFlags_NoNav))
        {
            std::string api;
            if (IdentifyGpu::gameUsesDxvk())
            {
                if (state.swapchainInteropApi == SwapchainInteropApi::None)
                    api = "DXVK";
                else
                    api = "DXVK w/Dx12";
            }
            else if (IdentifyGpu::gameUsesVkd3dProton())
            {
                api = "VKD3D";
            }
            else
            {
                switch (state.swapchainApi)
                {
                case Vulkan:
                    api = "VLK";
                    break;

                case DX11:
                    api = "D3D11";
                    break;

                case DX12:
                    if (state.swapchainInteropApi == SwapchainInteropApi::Dx11wDx12)
                        api = "D3D11 w/DX12";
                    else
                        api = "D3D12";

                    break;

                default:
                    switch (state.api)
                    {
                    case Vulkan:
                        api = "VLK";
                        break;

                    case DX11:
                        api = "D3D11";
                        break;

                    case DX12:
                        api = "D3D12";
                        break;

                    default:
                        api = "???";
                        break;
                    }

                    break;
                }
            }

            if (config->UseHQFont.value_or_default())
                ImGui::PushFontSize(std::round(fpsScale * fontSize));
            else
                ImGui::SetWindowFontScale(fpsScale);

            std::string firstLine = "";
            std::string secondLine = "";
            std::string thirdLine = "";

            auto fg = state.currentFG;
            auto fgText = (fg != nullptr && fg->IsActive() && !fg->IsPaused()) ? (" (" + std::string(fg->Name()) + ")")
                                                                               : std::string();

            const int fakeFramesCount = state.dlssgDetectedInterpolationCount;
            auto formatFg = [&](std::string_view name, int maxFakeFrames)
            {
                if (fakeFramesCount > maxFakeFrames)
                    return std::format(" ({} Doesn't support more than {}x)", name, maxFakeFrames);

                else if (fakeFramesCount == 0)
                    return std::format(" ({} off)", name);

                return std::format(" ({} x{})", name, fakeFramesCount + 1);
            };

            const FGNvngxReplacement activeNvngxFg = state.activeFgNvngx;
            if (activeNvngxFg == FGNvngxReplacement::Arturs)
            {
                fgText = formatFg("Enabler", Nvngx_FG::getMaxFakeFramesCount());
            }
            else if (activeNvngxFg == FGNvngxReplacement::Nukems)
            {
                fgText = formatFg("Nukems", Nvngx_FG::getMaxFakeFramesCount());
            }
            else if (activeNvngxFg == FGNvngxReplacement::FFX)
            {
                fgText = formatFg("FFX", Nvngx_FG::getMaxFakeFramesCount());
            }
            else if (activeNvngxFg == FGNvngxReplacement::Combo)
            {
                fgText = formatFg("Combo", Nvngx_FG::getMaxFakeFramesCount());
            }
            else if (state.activeFgOutput == FGOutput::DLSSG && fg)
            {
                fgText = formatFg("DLSSG", fg->GetMaxInterpolationCount());
            }

            const auto overlayType = config->FpsOverlayType.value_or_default();

            // ILION: what is drawn comes from the type, or from the elements of Custom (misc/IlionOverlay.h)
            const uint32_t overlayElements = IlionOverlay::Elements();
            const bool showFrameTimes = (overlayElements & IlionOverlay::FrameTimes) != 0;
            const bool showFrameGraph = (overlayElements & IlionOverlay::FrameGraph) != 0;
            const bool showUpscalerTime = (overlayElements & IlionOverlay::UpscalerTime) != 0;
            const bool showUpscalerGraph = (overlayElements & IlionOverlay::UpscalerGraph) != 0;
            const bool showReflexTimings = (overlayElements & IlionOverlay::ReflexTimings) != 0;
            const bool showIlionLatency = (overlayElements & IlionOverlay::IlionLatency) != 0;

            // Main figure of each latency meter (Reflex timings, ILION estimated latency): the same yellow in both, the
            // blocks are told apart by their titles
            const ImVec4 mainFigureYellow = toneMapColor(ImVec4(1.0f, 0.86f, 0.10f, 1.0f));
            const bool hasFeature = currentFeature && !currentFeature->IsFrozen();

            // Prepare Line 1
            std::string featurePart;
            std::string fpsPart;

            if (hasFeature)
            {
                const bool usesDx12CompatLayer = currentFeature->IsWithDx12();

                featurePart = StrFmt(" | %s -> %s%s", ApiUpscalerInputName(state.currentInputApiName).c_str(),
                                     UpscalerLabel(currentFeature).c_str(), usesDx12CompatLayer ? " w/Dx12" : "");
            }

            uint32_t interpolatedFrameCount = 0;
            if (fg != nullptr && fg->IsActive() && !fg->IsPaused())
                interpolatedFrameCount = fg->GetInterpolatedFrameCount();

            if (interpolatedFrameCount)
            {
                const double baseFps = frameRate / (double) (interpolatedFrameCount + 1);

                switch (overlayType)
                {
                case FpsOverlay_JustFPS:
                    fpsPart = StrFmt("%6.1f/%5.1f ", frameRate, baseFps);
                    break;

                case FpsOverlay_Simple:
                    fpsPart = StrFmt("FPS: %6.1f/%5.1f, %7.2f ms", frameRate, baseFps, frameTime);
                    break;

                default:
                    fpsPart = StrFmt("FPS: %6.1f/%5.1f, Avg: %6.1f", frameRate, baseFps, 1000.0f / averageFrameTime);
                    break;
                }
            }
            else
            {
                switch (overlayType)
                {
                case FpsOverlay_JustFPS:
                    fpsPart = StrFmt("%6.1f ", frameRate);
                    break;

                case FpsOverlay_Simple:
                    fpsPart = StrFmt("FPS: %6.1f, %7.2f ms", frameRate, frameTime);
                    break;

                default:
                    fpsPart = StrFmt("FPS: %6.1f, Avg: %6.1f", frameRate, 1000.0f / averageFrameTime);
                    break;
                }
            }

            if (overlayType == FpsOverlay_JustFPS)
                firstLine = StrFmt("%s", fpsPart.c_str());
            else
                firstLine = StrFmt("%s | %s%s%s", api.c_str(), fpsPart.c_str(), fgText.c_str(), featurePart.c_str());

            // Industrial theme and a vertical overlay: blocks of aligned rows instead of lines of text. The
            // values are the same ones, taken from the same places.
            const bool industrialVertical = IlionTheme::industrial && !config->FpsOverlayHorizontal.value_or_default() &&
                                            overlayType != FpsOverlay_JustFPS;

            // The same theme with the overlay laid out horizontally: the same blocks as lines of cells
            const bool industrialHorizontal = IlionTheme::industrial && config->FpsOverlayHorizontal.value_or_default() &&
                                              overlayType != FpsOverlay_JustFPS;
            float rowWidth = 0.0f;
            bool processingTitled = false;

            // Prepare Line 2
            if (showFrameTimes)
            {
                if (industrialHorizontal)
                {
                    // Cells, nothing to put between lines of text
                }
                else if (config->FpsOverlayHorizontal.value_or_default())
                {
                    ImGui::SameLine(0.0f, 0.0f);
                    ImGui::Text(" | ");
                    ImGui::SameLine(0.0f, 0.0f);
                }
                else if (!industrialVertical)
                {
                    ImGui::Spacing();
                }

                secondLine = StrFmt("Frame Time: %7.2f ms, Avg: %7.2f ms", state.frameTimes.back(), averageFrameTime);
            }

            // Prepare Line 3
            if (showUpscalerTime)
            {
                thirdLine =
                    StrFmt("Upscaler Time: %7.2f ms, Avg: %7.2f ms", state.upscaleTimes.back(), averageUpscalerFT);
            }

            ImVec2 plotSize;
            if (config->FpsOverlayHorizontal.value_or_default())
            {
                plotSize = { fpsScale * 150, fpsScale * 16 };
            }
            else
            {
                // Find the widest text width
                auto firstSize = ImGui::CalcTextSize(firstLine.c_str());
                auto secondSize = ImGui::CalcTextSize(secondLine.c_str());
                auto thirdSize = ImGui::CalcTextSize(thirdLine.c_str());
                auto textWidth = 0.0f;

                if (firstSize.x > secondSize.x)
                    textWidth = firstSize.x > thirdSize.x ? firstSize.x : thirdSize.x;
                else
                    textWidth = secondSize.x > thirdSize.x ? secondSize.x : thirdSize.x;

                auto minWidth = fpsScale * 300.0f;
                auto plotWidth = textWidth < minWidth ? minWidth : textWidth;

                plotSize = { plotWidth, fpsScale * 30 };
            }

            if (industrialVertical)
            {
                // What the first line carries after the numbers
                std::string upscalerValue = featurePart.size() > 3 ? featurePart.substr(3) : std::string();
                std::string fgValue = fgText;

                if (fgValue.size() > 3 && fgValue.rfind(" (", 0) == 0 && fgValue.back() == ')')
                    fgValue = fgValue.substr(2, fgValue.size() - 3);

                const char* upscalerLabel = Tr("Upscaler");
                const float gap = ImGui::CalcTextSize("  ").x;

                // Wide enough for the longest row, never a narrow strip
                rowWidth = fpsScale * 280.0f;
                rowWidth = std::max(rowWidth, ImGui::CalcTextSize(upscalerLabel).x + gap + ImGui::CalcTextSize(upscalerValue.c_str()).x);
                rowWidth = std::max(rowWidth, ImGui::CalcTextSize("Frame Gen").x + gap + ImGui::CalcTextSize(fgValue.c_str()).x);

                plotSize = { rowWidth, fpsScale * 34 };

                OverlayBlockTitle(Tr("PERFORMANCE"), rowWidth, true);

                // The frames per second of this moment in the green of the lamps, only the number itself
                const ImVec4 phosphor = toneMapColor(IlionTheme::LampGreen);
                // ILION: the FPS number itself in radioactive orange, every theme
                const ImVec4 fpsOrange = toneMapColor(ImVec4(1.0f, 0.45f, 0.02f, 1.0f));
                const ImVec4 inputOrange = toneMapColor(ImVec4(1.0f, 140.0f / 255.0f, 0.0f, 1.0f));

                OverlayRowParts("FPS",
                                { { StrFmt("%.1f", frameRate), &fpsOrange },
                                  { interpolatedFrameCount
                                        ? StrFmt(" / %.1f", frameRate / (double) (interpolatedFrameCount + 1))
                                        : std::string(),
                                    nullptr } },
                                rowWidth);

                if (!showFrameTimes)
                {
                    OverlayRow(Tr("Frame time"), StrFmt("%.2f ms", frameTime), rowWidth);
                }
                else
                {
                    OverlayRow(Tr("FPS, average"), StrFmt("%.1f", 1000.0f / averageFrameTime), rowWidth);
                }

                if (config->FpsOverlayLow1.value_or_default() && gOverlayLow1Fps > 0.0)
                    OverlayRow(Tr("1% low"), StrFmt("%.1f", gOverlayLow1Fps), rowWidth);

                if (showFrameTimes)
                {
                    OverlayRow(Tr("Frame time"), StrFmt("%.2f ms", state.frameTimes.back()), rowWidth);
                    OverlayRow(Tr("Frame time, average"), StrFmt("%.2f ms", averageFrameTime), rowWidth);
                }

                if (showFrameGraph)
                {
                    ImGui::PlotLines(
                        "##FrameTimeGraph",
                        [](void* rb, int idx) -> float { return static_cast<RingBuffer<float, plotWidth>*>(rb)->At(idx); },
                        &gFrameTimes, plotWidth, 0, nullptr, 0.0f, 66.6f, plotSize);
                }

                OverlayRow("API", api, rowWidth);

                // The same text as before in pieces: what the game asks for in orange, what really upscales in
                // green, the arrow and the rest as they were
                if (!upscalerValue.empty() && hasFeature)
                {
                    OverlayRowParts(upscalerLabel,
                                    { { ApiUpscalerInputName(state.currentInputApiName), &inputOrange },
                                      { " -> ", nullptr },
                                      { UpscalerLabel(currentFeature),
                                        &phosphor },
                                      { currentFeature->IsWithDx12() ? " w/Dx12" : "", nullptr } },
                                    rowWidth);
                }

                if (!fgValue.empty())
                    OverlayRow("Frame Gen", fgValue, rowWidth);

                if (showUpscalerTime)
                {
                    OverlayBlockTitle(Tr("PROCESSING TIMES"), rowWidth, false);
                    processingTitled = true;

                    OverlayRow(Tr("Upscaler"), StrFmt("%.2f ms", state.upscaleTimes.back()), rowWidth);
                    OverlayRow(Tr("Upscaler, average"), StrFmt("%.2f ms", averageUpscalerFT), rowWidth);
                }

                if (showUpscalerGraph)
                {
                    ImGui::PlotLines(
                        "##UpscalerFrameTimeGraph",
                        [](void* rb, int idx) -> float { return static_cast<RingBuffer<float, plotWidth>*>(rb)->At(idx); },
                        &gUpscalerTimes, plotWidth, 0, nullptr, 0.0f, 20.0f, plotSize);
                }
            }
            else if (industrialHorizontal)
            {
                const ImVec4 phosphor = toneMapColor(IlionTheme::LampGreen);
                // ILION: the FPS number itself in radioactive orange, every theme
                const ImVec4 fpsOrange = toneMapColor(ImVec4(1.0f, 0.45f, 0.02f, 1.0f));
                const ImVec4 inputOrange = toneMapColor(ImVec4(1.0f, 140.0f / 255.0f, 0.0f, 1.0f));
                const float graphGap = ImGui::CalcTextSize(" ").x * 2.5f;

                // Graphs as high as a line of text, so they sit in the line of cells
                plotSize = { fpsScale * 150.0f, ImGui::GetTextLineHeight() };

                std::string fgValue = fgText;

                if (fgValue.size() > 3 && fgValue.rfind(" (", 0) == 0 && fgValue.back() == ')')
                    fgValue = fgValue.substr(2, fgValue.size() - 3);

                OverlayCellRowTitle(Tr("PERFORMANCE"));

                OverlayCell("FPS", { { StrFmt("%.1f", frameRate), &fpsOrange },
                                     { interpolatedFrameCount
                                           ? StrFmt(" / %.1f", frameRate / (double) (interpolatedFrameCount + 1))
                                           : std::string(),
                                       nullptr } });

                if (!showFrameTimes)
                    OverlayCell(Tr("Frame time"), StrFmt("%.2f ms", frameTime));
                else
                    OverlayCell(Tr("FPS, average"), StrFmt("%.1f", 1000.0f / averageFrameTime));

                if (config->FpsOverlayLow1.value_or_default() && gOverlayLow1Fps > 0.0)
                    OverlayCell(Tr("1% low"), StrFmt("%.1f", gOverlayLow1Fps));

                if (showFrameTimes)
                {
                    OverlayCell(Tr("Frame time"), StrFmt("%.2f ms", state.frameTimes.back()));
                    OverlayCell(Tr("Frame time, average"), StrFmt("%.2f ms", averageFrameTime));
                }

                if (showFrameGraph)
                {
                    ImGui::SameLine(0.0f, graphGap);
                    ImGui::PlotLines(
                        "##FrameTimeGraph",
                        [](void* rb, int idx) -> float { return static_cast<RingBuffer<float, plotWidth>*>(rb)->At(idx); },
                        &gFrameTimes, plotWidth, 0, nullptr, 0.0f, 66.6f, plotSize);
                }

                OverlayCell("API", api);

                if (hasFeature)
                {
                    OverlayCell(Tr("Upscaler"),
                                { { ApiUpscalerInputName(state.currentInputApiName), &inputOrange },
                                  { " -> ", nullptr },
                                  { UpscalerLabel(currentFeature),
                                    &phosphor },
                                  { currentFeature->IsWithDx12() ? " w/Dx12" : "", nullptr } });
                }

                if (!fgValue.empty())
                    OverlayCell("Frame Gen", fgValue);

                if (showUpscalerTime)
                {
                    OverlayCellRowTitle(Tr("PROCESSING TIMES"));
                    processingTitled = true;

                    OverlayCell(Tr("Upscaler"), StrFmt("%.2f ms", state.upscaleTimes.back()));
                    OverlayCell(Tr("Upscaler, average"), StrFmt("%.2f ms", averageUpscalerFT));
                }

                if (showUpscalerGraph)
                {
                    ImGui::SameLine(0.0f, graphGap);
                    ImGui::PlotLines(
                        "##UpscalerFrameTimeGraph",
                        [](void* rb, int idx) -> float { return static_cast<RingBuffer<float, plotWidth>*>(rb)->At(idx); },
                        &gUpscalerTimes, plotWidth, 0, nullptr, 0.0f, 20.0f, plotSize);
                }
            }
            else
            {
            // Draw the overlay. ILION: the FPS number in radioactive orange, the rest of the line as it was
            {
                const std::string fpsNumber = StrFmt("%6.1f", frameRate);
                const size_t at = firstLine.find(fpsNumber);

                if (at == std::string::npos)
                {
                    ImGui::TextUnformatted(firstLine.c_str());
                }
                else
                {
                    if (at > 0)
                    {
                        ImGui::TextUnformatted(firstLine.c_str(), firstLine.c_str() + at);
                        ImGui::SameLine(0.0f, 0.0f);
                    }

                    ImGui::TextColored(toneMapColor(ImVec4(1.0f, 0.45f, 0.02f, 1.0f)), "%s", fpsNumber.c_str());

                    if (at + fpsNumber.size() < firstLine.size())
                    {
                        ImGui::SameLine(0.0f, 0.0f);
                        ImGui::TextUnformatted(firstLine.c_str() + at + fpsNumber.size());
                    }
                }
            }

            if (showFrameTimes)
            {
                if (config->FpsOverlayHorizontal.value_or_default())
                {
                    ImGui::SameLine(0.0f, 0.0f);
                    ImGui::Text(" | ");
                    ImGui::SameLine(0.0f, 0.0f);
                }
                else
                {
                    ImGui::Spacing();
                }

                ImGui::Text(secondLine.c_str());
            }

            if (showFrameGraph)
            {
                if (config->FpsOverlayHorizontal.value_or_default())
                    ImGui::SameLine(0.0f, 0.0f);

                // Graph of frame times
                ImGui::PlotLines(
                    "##FrameTimeGraph",
                    [](void* rb, int idx) -> float { return static_cast<RingBuffer<float, plotWidth>*>(rb)->At(idx); },
                    &gFrameTimes, plotWidth, 0, nullptr, 0.0f, 66.6f, plotSize);
            }

            if (showUpscalerTime)
            {
                if (config->FpsOverlayHorizontal.value_or_default())
                {
                    ImGui::SameLine(0.0f, 0.0f);
                    ImGui::Text(" | ");
                    ImGui::SameLine(0.0f, 0.0f);
                }
                else
                {
                    ImGui::Spacing();
                }

                ImGui::Text(thirdLine.c_str());
            }

            if (showUpscalerGraph)
            {
                if (config->FpsOverlayHorizontal.value_or_default())
                    ImGui::SameLine(0.0f, 0.0f);

                // Graph of upscaler times
                ImGui::PlotLines(
                    "##UpscalerFrameTimeGraph",
                    [](void* rb, int idx) -> float { return static_cast<RingBuffer<float, plotWidth>*>(rb)->At(idx); },
                    &gUpscalerTimes, plotWidth, 0, nullptr, 0.0f, 20.0f, plotSize);
            }

            }

            if (showReflexTimings)
            {
                constexpr auto delayBetweenPollsMs = 500;
                static auto previousPoll = 0.0;
                static bool gotData = false;

#ifdef LOW_LATENCY_INPUTS
                static TimingData timingData {};

                if (previousPoll <= 0.001 || previousPoll + delayBetweenPollsMs < now)
                {
                    gotData = InputCommon::get_timing_data(timingData);
                    previousPoll = now;
                }

                if (gotData && timingData.timeRange.has_value())
                {
                    ImDrawList* drawList = ImGui::GetWindowDrawList();
                    constexpr float offsetForText = 155;

                    const auto& rangeInNs = timingData.timeRange.value().length;

                    UINT64 localFrameCount = 0;

                    if (fg != nullptr)
                        localFrameCount = fg->FrameCount();

                    // Frame ids are shown in Help / Status instead, they only added noise here
                    if (industrialVertical)
                    {
                        rowWidth = std::max(rowWidth, ImGui::CalcTextSize("Reflex timings").x);
                        OverlayBlockTitle("Reflex timings", rowWidth, false);
                        OverlayRow(Tr("Whole frame"), StrFmt("%.1f ms", rangeInNs / 1000.0), rowWidth, &mainFigureYellow);
                    }
                    else if (industrialHorizontal)
                    {
                        OverlayCellRowTitle("Reflex timings");
                        OverlayCell(Tr("Whole frame"), StrFmt("%.1f ms", rangeInNs / 1000.0), &mainFigureYellow);
                    }
                    else
                    {
                        ImGui::TextUnformatted("Low latency timings, whole frame:");
                        ImGui::SameLine();
                        ImGui::TextColored(mainFigureYellow, "%.1fms", rangeInNs / 1000.0);
                    }

                    const auto maxWidth =
                        config->FpsOverlayHorizontal.value_or_default() ? ImGui::GetWindowWidth() : plotSize.x;

                    const auto drawTiming = [&](const auto& timingOpt, const char* desc, ImVec4 color)
                    {
                        if (!timingOpt.has_value())
                            return;

                        auto toneMappedColor = State::Instance().isHdrActive ? toneMapColor(color) : color;

                        const auto& timing = timingOpt.value();
                        float duration = static_cast<float>(timing.length * rangeInNs / 1000.0);

                        // The value in its row, without the bar
                        if (industrialVertical)
                        {
                            OverlayRow(desc, StrFmt("%.1f ms", duration), rowWidth);
                            return;
                        }

                        if (industrialHorizontal)
                        {
                            OverlayCell(desc, StrFmt("%.1f ms", duration));
                            return;
                        }

                        ImGui::TextColored(toneMappedColor, "%-12s %4.1fms", desc, duration);

                        auto leftLimit = ImGui::GetItemRectMin().x + offsetForText * fpsScale;

                        auto start = static_cast<float>(leftLimit + (ImGui::GetItemRectMin().x + maxWidth - leftLimit) *
                                                                        timing.position);

                        auto end = static_cast<float>(start + (ImGui::GetItemRectMin().x + maxWidth - leftLimit) *
                                                                  timing.length);

                        auto pos = ImVec2(start, ImGui::GetItemRectMin().y);
                        auto size = ImVec2(end, ImGui::GetItemRectMax().y);

                        drawList->AddRectFilled(pos, size, ImGui::ColorConvertFloat4ToU32(toneMappedColor));
                    };

                    drawTiming(timingData.simulation, "Simulation", ImVec4(0.768f, 0.169f, 0.169f, 1.0f));
                    drawTiming(timingData.renderSubmit, "RenderSubmit", ImVec4(0.235f, 0.705f, 0.294f, 1.0f));
                    drawTiming(timingData.present, "Present", ImVec4(1.0f, 0.88f, 0.098f, 1.0f));
                    drawTiming(timingData.driver, "Driver", ImVec4(0.263f, 0.388f, 0.847f, 1.0f));
                    drawTiming(timingData.osRenderQueue, "RenderQueue", ImVec4(0.76f, 0.51f, 0.188f, 1.0f));
                    drawTiming(timingData.gpuRender, "GpuRender", ImVec4(0.569f, 0.117f, 0.705f, 1.0f));
                }
#else
                if (previousPoll <= 0.001 || previousPoll + delayBetweenPollsMs < now)
                {
                    gotData = ReflexHooks::updateTimingData();
                    previousPoll = now;
                }

                auto& timingData = ReflexHooks::timingData;

                if (gotData && timingData[TimingType::TimeRange].has_value())
                {
                    ImDrawList* drawList = ImGui::GetWindowDrawList();
                    constexpr float offsetForText = 155;

                    const auto& rangeInNs = timingData[TimingType::TimeRange].value().length;

                    UINT64 localFrameCount = 0;

                    if (fg != nullptr)
                        localFrameCount = fg->FrameCount();

                    // Frame ids are shown in Help / Status instead, they only added noise here
                    if (industrialVertical)
                    {
                        rowWidth = std::max(rowWidth, ImGui::CalcTextSize("Reflex timings").x);
                        OverlayBlockTitle("Reflex timings", rowWidth, false);
                        OverlayRow(Tr("Whole frame"), StrFmt("%.1f ms", rangeInNs / 1000.0), rowWidth, &mainFigureYellow);
                    }
                    else if (industrialHorizontal)
                    {
                        OverlayCellRowTitle("Reflex timings");
                        OverlayCell(Tr("Whole frame"), StrFmt("%.1f ms", rangeInNs / 1000.0), &mainFigureYellow);
                    }
                    else
                    {
                        ImGui::TextUnformatted("Reflex timings, whole frame:");
                        ImGui::SameLine();
                        ImGui::TextColored(mainFigureYellow, "%.1fms", rangeInNs / 1000.0);
                    }

                    const auto maxWidth =
                        config->FpsOverlayHorizontal.value_or_default() ? ImGui::GetWindowWidth() : plotSize.x;

                    const auto drawTiming = [&](TimingType type, const char* desc, ImVec4 color)
                    {
                        if (!timingData[type].has_value())
                            return;

                        auto toneMappedColor = toneMapColor(color);

                        auto& timing = timingData[type].value();
                        float duration = static_cast<float>(timing.length * rangeInNs / 1000.0);

                        // The value in its row, without the bar
                        if (industrialVertical)
                        {
                            OverlayRow(desc, StrFmt("%.1f ms", duration), rowWidth);
                            return;
                        }

                        if (industrialHorizontal)
                        {
                            OverlayCell(desc, StrFmt("%.1f ms", duration));
                            return;
                        }

                        ImGui::TextColored(toneMappedColor, "%-12s %4.1fms", desc, duration);
                        auto leftLimit = ImGui::GetItemRectMin().x + offsetForText * fpsScale;
                        auto start = static_cast<float>(leftLimit + (ImGui::GetItemRectMin().x + maxWidth - leftLimit) *
                                                                        timing.position);
                        auto end = static_cast<float>(start + (ImGui::GetItemRectMin().x + maxWidth - leftLimit) *
                                                                  timing.length);
                        auto pos = ImVec2(start, ImGui::GetItemRectMin().y);
                        auto size = ImVec2(end, ImGui::GetItemRectMax().y);
                        drawList->AddRectFilled(pos, size, ImGui::ColorConvertFloat4ToU32(toneMappedColor));
                    };

                    drawTiming(TimingType::Simulation, "Simulation", ImVec4(0.768f, 0.169f, 0.169f, 1.0f));
                    drawTiming(TimingType::RenderSubmit, "RenderSubmit", ImVec4(0.235f, 0.705f, 0.294f, 1.0f));
                    drawTiming(TimingType::Present, "Present", ImVec4(1.0f, 0.88f, 0.098f, 1.0f));
                    drawTiming(TimingType::Driver, "Driver", ImVec4(0.263f, 0.388f, 0.847f, 1.0f));
                    drawTiming(TimingType::OsRenderQueue, "RenderQueue", ImVec4(0.76f, 0.51f, 0.188f, 1.0f));
                    drawTiming(TimingType::GpuRender, "GpuRender", ImVec4(0.569f, 0.117f, 0.705f, 1.0f));
                }
#endif
            }

            // ILION estimated latency (misc/IlionLatency.h): measured times of each frame in DirectX 12. Separate
            // from the Reflex timings above, which keep working as they were; both can be on at once.
            if (showIlionLatency)
            {
                const ImVec4& latencyYellow = mainFigureYellow;

                // The active reducer and whether it is on: a change makes the meter drop its frames and start again
                int reducerId = 0;
                if (fakenvapi::isUsingAsMainNvapi())
                    reducerId = 1 + (int) fakenvapi::getCurrentMode() * 2 + (fakenvapi::isLowLatencyActive() ? 1 : 0);

                const auto latency = IlionLatency::Get(reducerId);
                const auto ms = [](double value) { return value >= 0.0 ? StrFmt("%.1f ms", value) : std::string(Tr("N/A")); };
                const bool measured = state.swapchainApi == DX12 && latency.valid;

                const char* why = nullptr;
                switch (latency.missing)
                {
                case IlionLatency::Missing::Settling:
                    why = Tr("Reducer changed: new frames");
                    break;
                case IlionLatency::Missing::NoFrames:
                    why = Tr("No DirectX 12 frames");
                    break;
                case IlionLatency::Missing::NoSimulation:
                    why = Tr("No simulation marker from the game");
                    break;
                case IlionLatency::Missing::NoGpuTime:
                    why = Tr("No GPU timestamp");
                    break;
                case IlionLatency::Missing::Composed:
                    why = Tr("No DXGI frame statistics");
                    break;
                case IlionLatency::Missing::RefreshNotSeen:
                    why = Tr("Refresh of the frame not seen");
                    break;
                case IlionLatency::Missing::RefreshBeforeGpu:
                    why = Tr("DXGI refresh before GPU end");
                    break;
                case IlionLatency::Missing::Tearing:
                    why = Tr("Tearing: refresh time unusable");
                    break;
                default:
                    break;
                }

                // Label and value; the first one is the latency, in yellow
                std::vector<std::pair<std::string, std::string>> rows;

                if (state.swapchainApi != DX12)
                {
                    rows.push_back({ Tr("DirectX 12 only"), "" });
                }
                else if (!latency.valid)
                {
                    rows.push_back({ Tr("Waiting for frames"), "" });
                    if (why != nullptr)
                        rows.push_back({ why, "" });
                }
                else
                {
                    rows.push_back({ latency.onScreen ? Tr("Simulation to screen") : Tr("Simulation to GPU end"), ms(latency.system) });

                    if (latency.inputToScreen >= 0.0)
                        rows.push_back({ Tr("Input to screen"), ms(latency.inputToScreen) });

                    // Consecutive parts of the same frames: they add up to the figure above
                    rows.push_back({ "CPU", ms(latency.cpu) });

                    if (latency.gpu >= 0.0)
                        rows.push_back({ Tr("Present to GPU end"), ms(latency.gpu) }); // includes frames waiting in the queue

                    if (latency.display >= 0.0)
                        rows.push_back({ Tr("Screen"), ms(latency.display) });
                    else if (latency.afterPresent >= 0.0)
                        rows.push_back({ Tr("GPU + screen"), ms(latency.afterPresent) });

                    rows.push_back({ Tr("Displayed FPS"),
                                     latency.displayedFps >= 0.0 ? StrFmt("%.1f", latency.displayedFps) : std::string(Tr("N/A")) });

                    if (latency.presentation == 0)
                        rows.push_back({ Tr("Presentation"), Tr("composed") });
                    else if (latency.presentation == 1 || latency.presentation == 2)
                        rows.push_back({ Tr("Presentation"), Tr("independent flip") });

                    // Frame generation holds the real frame and paces the shown ones after this point: not in the figure
                    if ((fg != nullptr && fg->IsActive() && !fg->IsPaused()) || state.dlssgDetectedInterpolationCount > 0)
                        rows.push_back({ Tr("With FG: generator latency not included"), "" });

                    if (why != nullptr)
                        rows.push_back({ why, "" });
                }

                const char* title = Tr("ILION ESTIMATED LATENCY");

                if (industrialVertical)
                {
                    // As wide as its title and its longest row, like the other blocks
                    const float gap = ImGui::CalcTextSize("  ").x;
                    rowWidth = std::max(rowWidth, ImGui::CalcTextSize(title).x);
                    for (const auto& [label, value] : rows)
                        rowWidth = std::max(rowWidth, ImGui::CalcTextSize(label.c_str()).x + gap + ImGui::CalcTextSize(value.c_str()).x);

                    OverlayBlockTitle(title, rowWidth, false);

                    for (size_t i = 0; i < rows.size(); i++)
                        OverlayRow(rows[i].first.c_str(), rows[i].second, rowWidth, i == 0 && measured ? &latencyYellow : nullptr);
                }
                else if (industrialHorizontal)
                {
                    OverlayCellRowTitle(title);

                    for (size_t i = 0; i < rows.size(); i++)
                        OverlayCell(rows[i].first.c_str(), rows[i].second, i == 0 && measured ? &latencyYellow : nullptr);
                }
                else
                {
                    ImGui::TextUnformatted(Tr("ILION estimated latency"));

                    for (size_t i = 0; i < rows.size(); i++)
                    {
                        ImGui::TextUnformatted(rows[i].first.c_str());

                        if (!rows[i].second.empty())
                        {
                            ImGui::SameLine();

                            if (i == 0 && measured)
                                ImGui::TextColored(latencyYellow, "%s", rows[i].second.c_str());
                            else
                                ImGui::TextUnformatted(rows[i].second.c_str());
                        }
                    }
                }
            }

            DrawBenchmarkStatus(now);
            DrawHardwareTelemetry(config, fpsScale, industrialVertical ? rowWidth : 0.0f, industrialHorizontal);
            DrawFsr4ComparisonPanel(fpsScale, industrialVertical ? rowWidth : 0.0f);
        }

        // Restore the style
        if (!config->OverlaysUseTheme.value_or_default())
            ImGui::PopStyleColor(5);
        else
            ImGui::PopStyleColor(2);

        // Get size for postioning
        overlaySize = ImGui::GetWindowSize();

        if (config->UseHQFont.value_or_default())
            ImGui::PopFontSize();

        ImGui::End();

        if (stylePushed)
            ImGui::PopStyleVar(7);

        // Left / Right
        if (config->FpsOverlayPosition.value_or_default() == FpsOverlayPos_TopLeft ||
            config->FpsOverlayPosition.value_or_default() == FpsOverlayPos_BottomLeft)
        {
            overlayPosition.x = 0;
        }
        else
        {
            overlayPosition.x = io.DisplaySize.x - overlaySize.x;
        }

        // Top / Bottom
        if (config->FpsOverlayPosition.value_or_default() == FpsOverlayPos_TopLeft ||
            config->FpsOverlayPosition.value_or_default() == FpsOverlayPos_TopRight)
        {
            overlayPosition.y = 0;
        }
        else
        {
            // Prevent overlapping with splash message
            if (!config->DisableSplash.value_or_default() && now > splashStart && now < splashLimit)
                overlayPosition.y = io.DisplaySize.y - overlaySize.y - splashSize.y;
            else
                overlayPosition.y = io.DisplaySize.y - overlaySize.y;
        }
    }
}

// Basic | Essential | Advanced, only changes what is drawn. Written to the ini right away, on its own.
// Advanced is the former Full; Essential is Advanced with a strip of status screens above the tabs.
static void RenderModeSelector(decltype(Config::Instance()) config)
{
    const bool basic = config->MenuBasicMode.value_or_default();
    const bool essential = !basic && config->MenuEssentialMode.value_or_default();

    // Industrial theme: two keys of the same size with room around their word, the word in the middle.
    // Classic theme: the bare words, as always.
    const bool keys = IlionTheme::industrial;
    const ImVec2 keySize = IlionTheme::SelectorKeySize(
        (std::max)({ ImGui::CalcTextSize(Tr("Basic")).x, ImGui::CalcTextSize(Tr("Essential")).x,
                    ImGui::CalcTextSize(Tr("Advanced")).x }),
        false);

    const auto modeButton = [&](const char* label, const char* id, bool selected)
    {
        const ImVec2 textSize = ImGui::CalcTextSize(label);
        const ImVec2 size = keys ? keySize : textSize;
        const ImVec2 pos = ImGui::GetCursorScreenPos();

        const bool clicked = ImGui::InvisibleButton(id, size);
        const bool highlighted = selected || ImGui::IsItemHovered();

        if (keys)
        {
            IlionTheme::DrawSelectorKey(ImGui::GetWindowDrawList(), ImRect(pos, ImVec2(pos.x + size.x, pos.y + size.y)), selected,
                                        ImGui::IsItemHovered());
        }

        ImGui::GetWindowDrawList()->AddText(
            ImVec2(std::floor(pos.x + (size.x - textSize.x) * 0.5f), std::floor(pos.y + (size.y - textSize.y) * 0.5f)),
            ImGui::GetColorU32(highlighted ? ImGuiCol_Text : ImGuiCol_TextDisabled), label);

        return clicked && !selected;
    };

    if (modeButton(Tr("Basic"), "##ModeBasic", basic))
    {
        config->MenuBasicMode = true;
        config->SaveMenuPreferences();
    }

    ImGui::SameLine();
    IlionTheme::SelectorBar(keys ? keySize.y : 0.0f);
    ImGui::SameLine();

    if (modeButton(Tr("Essential"), "##ModeEssential", essential))
    {
        config->MenuBasicMode = false;
        config->MenuEssentialMode = true;
        config->SaveMenuPreferences();
    }

    ImGui::SameLine();
    IlionTheme::SelectorBar(keys ? keySize.y : 0.0f);
    ImGui::SameLine();

    if (modeButton(Tr("Advanced"), "##ModeFull", !basic && !essential))
    {
        config->MenuBasicMode = false;
        config->MenuEssentialMode = false;
        config->SaveMenuPreferences();
    }
}

// ES | EN, applies right away and is written to the ini right away, on its own
static void RenderLanguageSelector(decltype(Config::Instance()) config)
{
    const auto current = Ilion::CurrentLang();
    const auto& style = ImGui::GetStyle();

    // Industrial theme: two square keys of the same size, the letters in the middle. Classic theme: the bare
    // letters, as always.
    const bool keys = IlionTheme::industrial;
    const ImVec2 keySize =
        IlionTheme::SelectorKeySize((std::max)(ImGui::CalcTextSize("ES").x, ImGui::CalcTextSize("EN").x), true);

    const float width = (keys ? keySize.x * 2.0f : ImGui::CalcTextSize("ES").x + ImGui::CalcTextSize("EN").x) +
                        ImGui::CalcTextSize("|").x + style.ItemSpacing.x * 2.0f;

    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - width);

    const auto languageButton = [&](const char* label, Ilion::Lang lang)
    {
        const ImVec2 textSize = ImGui::CalcTextSize(label);
        const ImVec2 size = keys ? keySize : textSize;
        const ImVec2 pos = ImGui::GetCursorScreenPos();

        ImGui::PushID(label);
        const bool clicked = ImGui::InvisibleButton("##Language", size);
        ImGui::PopID();

        const bool highlighted = current == lang || ImGui::IsItemHovered();

        if (keys)
        {
            IlionTheme::DrawSelectorKey(ImGui::GetWindowDrawList(), ImRect(pos, ImVec2(pos.x + size.x, pos.y + size.y)),
                                        current == lang, ImGui::IsItemHovered());
        }

        ImGui::GetWindowDrawList()->AddText(
            ImVec2(std::floor(pos.x + (size.x - textSize.x) * 0.5f), std::floor(pos.y + (size.y - textSize.y) * 0.5f)),
            ImGui::GetColorU32(highlighted ? ImGuiCol_Text : ImGuiCol_TextDisabled), label);

        if (clicked && current != lang)
        {
            config->MenuLanguage = std::string(Ilion::LangCode(lang));
            Ilion::SetLang(lang);
            config->SaveMenuPreferences();
        }
    };

    languageButton("ES", Ilion::Lang::ES);
    ImGui::SameLine();
    IlionTheme::SelectorBar(keys ? keySize.y : 0.0f);
    ImGui::SameLine();
    languageButton("EN", Ilion::Lang::EN);
}

void MenuCommon::RenderMainMenuHeaderMessages(RenderMenuContext& ctx)
{
    auto& state = ctx.state;
    auto config = ctx.config;
    auto& menuResScale = ctx.menuResScale;
    auto& versionStatus = ctx.versionStatus;
    auto& currentVersionText = ctx.currentVersionText;

    if (!_showMipmapCalcWindow && !_showHudlessWindow && !ImGui::IsWindowFocused(ImGuiFocusedFlags_AnyWindow))
        ImGui::SetWindowFocus();

    if (config->MenuScale.has_value())
    {
        _selectedScale = ((int) (menuResScale * 10.0f)) - 4;
    }
    else
    {
        _selectedScale = 0;
    }

    if (versionStatus.completed)
    {
        if (versionStatus.updateAvailable && !versionStatus.latestTag.empty())
        {
            ImGui::Spacing();
            ImGui::TextColored(toneMapColor(IlionTheme::Themed(ImVec4(1.f, 0.8f, 0.f, 1.f), IlionTheme::Role::Warn)), Tr("Update available: %s (current %s)"),
                               versionStatus.latestTag.c_str(), currentVersionText.c_str());

            if (!versionStatus.latestUrl.empty())
            {
                ImGui::SameLine();
                ImGui::TextLinkOpenURL(Tr("Open release page"), versionStatus.latestUrl.c_str());
            }

            ImGui::Spacing();
        }
        else if (!versionStatus.error.empty())
        {
            LOG_ERROR("Version check failed: {0}", versionStatus.error);
            versionStatus.error.clear();
        }
        // Disabled error message
        // else if (!versionStatus.error.empty())
        //{
        //    ImGui::Spacing();
        //    ImGui::TextColored(toneMapColor(ImVec4(1.f, 0.4f, 0.f, 1.f)), "%s", versionStatus.error.c_str());
        //    ImGui::Spacing();
        //}
    }

    if (state.nvngxIniDetected)
    {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, toneMapColor(IlionTheme::Themed(ImVec4(1.f, 0.f, 0.f, 1.f), IlionTheme::Role::Bad)));
        ImGui::TextWrapped(Tr("nvngx.ini detected, please move over to using OptiScaler.ini and delete the old config"));
        ImGui::PopStyleColor();
        ImGui::Spacing();
    }
}

void MenuCommon::RenderUpscalerStateMessage(RenderMenuContext& ctx)
{
    auto& state = ctx.state;
    auto config = ctx.config;
    auto& currentFeature = ctx.currentFeature;
    auto& menuResScale = ctx.menuResScale;
    auto& primaryGpu = *ctx.primaryGpu;

    // No active upscaler message
    if (currentFeature == nullptr || !currentFeature->IsInited())
    {
        ImGui::Spacing();

        if (config->UseHQFont.value_or_default())
            ImGui::PushFontSize(std::round(fontSize * menuResScale * 2.0f));
        else
            ImGui::SetWindowFontScale(menuResScale * 2.0f);

        if (state.nvngxExists || state.nvngxReplacement.has_value() ||
            (state.libxessExists || XeSSProxy::Module() != nullptr))
        {
            ImGui::Spacing();

            std::vector<std::string> upscalers;

            if (state.fsrHooks)
                upscalers.push_back("FSR");

            if (state.nvngxExists || state.nvngxReplacement.has_value() || primaryGpu.dlssCapable)
                upscalers.push_back("DLSS");

            if (state.libxessExists || XeSSProxy::Module() != nullptr)
                upscalers.push_back("XeSS");

            auto joined = upscalers | std::views::join_with(std::string { " or " });

            std::string joinedUpscalers(joined.begin(), joined.end());

            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextWrapped(Tr("Please select %s as upscaler from game options and load a save game to enable Opti settings.\n"
                        "Upscalers don't always work in menus."),
                        joinedUpscalers.c_str());
            ImGui::PopTextWrapPos();

            if (config->UseHQFont.value_or_default())
                ImGui::PopFontSize();
            else
                ImGui::SetWindowFontScale(menuResScale);

            ImGui::Spacing();

            RenderDetectedCard(ctx);

            ImGui::Spacing();
        }
        else
        {
            ImGui::Spacing();
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextWrapped(Tr("Can't find nvngx.dll and libxess.dll and FSR inputs\nUpscaling support will NOT work."));
            ImGui::PopTextWrapPos();
            ImGui::Spacing();

            if (config->UseHQFont.value_or_default())
                ImGui::PopFont();
            else
                ImGui::SetWindowFontScale(menuResScale);

            RenderDetectedCard(ctx);
        }
    }
    else if (currentFeature->IsFrozen())
    {
        ImGui::Spacing();

        if (config->UseHQFont.value_or_default())
            ImGui::PushFontSize(std::round(fontSize * menuResScale * 2.0f));
        else
            ImGui::SetWindowFontScale(menuResScale * 2.0f);

        ImGui::PushTextWrapPos(0.0f);
        ImGui::Text(Tr("%s active, but currently not used by the game\nPlease load into the game"),
                    currentFeature->Name().c_str());
        ImGui::PopTextWrapPos();

        if (config->UseHQFont.value_or_default())
            ImGui::PopFont();
        else
            ImGui::SetWindowFontScale(menuResScale);
    }
}

static const char* ApiName(API api);

void MenuCommon::RenderActiveUpscalerSettings(RenderMenuContext& ctx)
{
    auto& state = ctx.state;
    auto config = ctx.config;
    auto& currentFeature = ctx.currentFeature;
    auto& menuResScale = ctx.menuResScale;
    auto& primaryGpu = *ctx.primaryGpu;

    // UPSCALERS -----------------------------
    SectionTitle("Upscalers");

    // Row - API | GPU | Upscaler input
    if (!basicView)
        ImGui::TextWrapped(Tr("%s%s | %s | Input: %s"), ApiName(state.api),
                       IdentifyGpu::gameUsesDxvk()          ? " (DXVK)"
                       : IdentifyGpu::gameUsesVkd3dProton() ? " (VKD3D)"
                                                            : "",
                       primaryGpu.name.c_str(), ApiUpscalerInputName(state.currentInputApiName).c_str());
    ImGui::Spacing();

    GetCurrentBackendInfo(state.api, currentBackend, &currentBackendName);

    const bool usesDlssd = currentFeature->GetUpscalerType() == Upscaler::DLSSD;

    if (!basicView)
    {
        ImGui::TextDisabled(Tr("%dx%d -> %dx%d (%.2fx)"), currentFeature->RenderWidth(), currentFeature->RenderHeight(),
                            currentFeature->TargetWidth(), currentFeature->TargetHeight(),
                            (float) currentFeature->TargetWidth() / (float) currentFeature->RenderWidth());
    }

    // ILION experiment: what the external d4r backend is really running, from its log and from NGX's answers
    if (state.dlssExternalBackend && currentFeature->GetUpscalerType() == Upscaler::DLSS)
        IlionD4rPanel::Draw(IlionD4rLogPath(), currentFeature->Version().major, currentFeature->Version().minor,
                            currentFeature->Version().patch);

    // ILION's upscalers: every FFX upscaler found and DLSS 4 through d4r, whatever runs now
    if (!usesDlssd)
    {
        ImGui::Spacing();
        DrawUpscalerSelector(currentFeature, menuResScale, "upscaling");

        // ILION: the FSR 4 model, in every mode (the same state and checks; its diagnosis only in Advanced)
        RenderFsr4ModelControls(currentBackend, currentFeature, menuResScale);

        // ILION: the FFX backend of a Vulkan game, Advanced only (Basic and Essential keep whatever it is set to)
        if (state.api == Vulkan && !basicView && !EssentialView())
            RenderVulkanBackendChoice(menuResScale);

        // ILION: OptiScaler's own backend list (native DLSS on NVIDIA included) and the FFX provider list, unchanged,
        // folded under one key; ILION's manager above stays the main selector. Not in Basic, which keeps it simple
        ImGui::Spacing();
        if (!basicView)
        if (auto ch = ScopedCollapsingHeader(TrL("Advanced selectors")); ch.IsHeaderOpen())
        {
            TextWrappedDisabled("%s", Tr("OptiScaler's own lists, for what the selector above doesn't offer: the backend (native "
                                         "DLSS on NVIDIA included) and the provider inside the FFX dll in use."));
            ImGui::TextDisabled("%s", Tr("OptiScaler's backend list"));
        ImGui::PushItemWidth(150.0f * menuResScale);

        switch (state.api)
        {
        case DX11:
            AddDx11Backends(currentBackend);
            break;

        case DX12:
            AddDx12Backends(currentBackend);
            break;

        default:
            AddVulkanBackends(currentBackend);
        }

        ImGui::PopItemWidth();
            if (currentBackend == Upscaler::FFX || currentBackend == Upscaler::FFX_on12)
            {
            if (auto versions = GetFfxVersionList(currentBackend, state.api);
                versions != nullptr && !versions->names.empty())
            {
                ImGui::Spacing();
    
                auto& ffxIndexConfig = FfxIndexFor(currentBackend, state.api);
                const int ffxIndex = ffxIndexConfig.value_or_default();
                auto currentName =
                    ffxIndex >= 0 && ffxIndex < versions->names.size() ? StrFmt("FSR %s", versions->names[ffxIndex]) : "";
    
                ImGui::PushItemWidth(150.0f * menuResScale);
    
                if (IlionTheme::BeginCombo(TrL("FFX provider"), currentName.c_str()))
                {
                    for (auto n : FfxVersionsNewestFirst(*versions))
                    {
                        if (ImGui::Selectable(StrFmt("FSR %s##%d", versions->names[n], n).c_str(), ffxIndex == n) &&
                            ffxIndex != n)
                        {
                            ffxIndexConfig = n;

                            // A provider picked by hand in Advanced stays until an implementation is chosen again
                            if (&ffxIndexConfig == &config->FfxUpscalerIndex)
                                FfxApiProxy::SetSrIndexManual(true);

                            state.newBackend = currentBackend;
                            MARK_ALL_BACKENDS_CHANGED();
                        }
                    }
    
                    ImGui::EndCombo();
                }
    
                ImGui::PopItemWidth();
    
                ShowTooltip("Provider inside the FFX dll in use, as that dll lists them: e.g. the AMD driver's FSR 4.1.2 that "
                            "AMD's SDK dll hands the upscale to, or the SDK's own 4.1.1. The selector above picks the dll.");

                // ILION: a provider picked here by hand, until an implementation is chosen again in the selector
                if (FfxApiProxy::SrIndexManual() && &ffxIndexConfig == &config->FfxUpscalerIndex)
                {
                    ImGui::SameLine();
                    ImGui::TextDisabled("%s", Tr("(manual)"));
                }
            }
            }
        }
    }

    if (!usesDlssd)
    {
        if (state.newBackend != Upscaler::Reset && state.newBackend != currentBackend)
        {
            if (state.newBackend == Upscaler::XeSS)
            {
                // Reseting them for xess
                config->DisableReactiveMask.reset();
                config->DlssReactiveMaskBias.reset();
            }

            MARK_ALL_BACKENDS_CHANGED();
        }
    }

    if (primaryGpu.dlssCapable && !state.NVNGX_DLSS_Path.has_value())
    {
        ImGui::Spacing();
        TextWrappedColored(toneMapColor(IlionTheme::Themed(ImVec4(1.f, 0.8f, 0.f, 1.f), IlionTheme::Role::Warn)), Tr("nvngx_dlss.dll not found, DLSS disabled!"));
    }

    // Dx11 with Dx12
    if (state.api == DX11 && currentFeature->IsWithDx12() && !basicView)
    {
        ImGui::Spacing();
        if (auto ch = ScopedCollapsingHeader(TrL("Dx11 with Dx12 Settings")); ch.IsHeaderOpen())
        {
            ScopedIndent indent {};
            ImGui::Spacing();

            if (bool dontUseNTShared = config->DontUseNTShared.value_or_default();
                IlionTheme::Checkbox(TrL("Don't Use NTShared"), &dontUseNTShared))
                config->DontUseNTShared = dontUseNTShared;

            ImGui::Spacing();
            ImGui::Spacing();
        }
    }

    if (state.api == Vulkan && currentFeature->IsWithDx12() && !basicView)
    {
        ImGui::Spacing();
        if (auto ch = ScopedCollapsingHeader(TrL("Vulkan with Dx12 Settings")); ch.IsHeaderOpen())
        {
            ScopedIndent indent {};
            ImGui::Spacing();

            if (bool inputsUseCopy = config->VulkanUseCopyForInputs.value_or_default();
                IlionTheme::Checkbox(TrL("Use CopyResource for Inputs"), &inputsUseCopy))
                config->VulkanUseCopyForInputs = inputsUseCopy;

            if (bool outputUseCopy = config->VulkanUseCopyForOutput.value_or_default();
                IlionTheme::Checkbox(TrL("Use CopyResource for Output"), &outputUseCopy))
                config->VulkanUseCopyForOutput = outputUseCopy;

            ImGui::Spacing();
            ImGui::Spacing();
        }
    }

    // UPSCALER SPECIFIC -----------------------------

    // XeSS -----------------------------
    if (currentBackend == Upscaler::XeSS && !usesDlssd)
    {
        ImGui::Spacing();
        if (auto ch = ScopedCollapsingHeader(TrL("XeSS Settings")); ch.IsHeaderOpen())
        {
            ScopedIndent indent {};
            ImGui::Spacing();

            const char* models[] = { "KPSS", "SPLAT", "MODEL_3", "MODEL_4", "MODEL_5", "MODEL_6" };
            auto configModes = config->NetworkModel.value_or_default();

            if (configModes < 0 || configModes > 5)
                configModes = 0;

            const char* selectedModel = models[configModes];

            if (IlionTheme::BeginCombo(TrL("Network Models"), selectedModel))
            {
                for (int n = 0; n < 6; n++)
                {
                    if (ImGui::Selectable(models[n], (config->NetworkModel.value_or_default() == n)))
                    {
                        config->NetworkModel = n;
                        state.newBackend = currentBackend;
                        MARK_ALL_BACKENDS_CHANGED();
                    }
                }

                ImGui::EndCombo();
            }
            ShowTooltip("Likely doesn't do much");

            if (!basicView)
            {
            if (bool dbg = state.xessDebug; IlionTheme::Checkbox(TrL("Dump (Shift+Del)"), &dbg))
                state.xessDebug = dbg;

            ImGui::SameLine(0.0f, 6.0f);
            int dbgCount = state.xessDebugFrames;

            ImGui::PushItemWidth(95.0f * menuResScale);
            if (ImGui::InputInt(TrL("frames"), &dbgCount))
            {
                if (dbgCount < 4)
                    dbgCount = 4;
                else if (dbgCount > 999)
                    dbgCount = 999;

                state.xessDebugFrames = dbgCount;
            }

            ImGui::PopItemWidth();
            }

            ImGui::Spacing();
            ImGui::Spacing();
        }
    }

    // FFX -----------------
    if (!usesDlssd && (currentBackend == Upscaler::FFX || currentBackend == Upscaler::FFX_on12))
    {
        // Upscaler dll selection, kept outside of the version checks so it's still reachable when a dll fails
        if (RunsSwitchedFfx(currentBackend))
        {
            // Path, hash, diagnostics and comparison stay in the full menu only
            if (!basicView)
            {
            const auto activeSrPath = FfxApiProxy::Dx12Module_SR_Path();

            // Hash of the dll in use, calculated in the background and looked up again only until it's there
            static std::wstring hashedSrPath;
            static std::string activeSrHash;
            static double lastHashLookup = 0.0;

            if (hashedSrPath != activeSrPath)
            {
                hashedSrPath = activeSrPath;
                activeSrHash.clear();
                FSR4Versions::RequestHash(activeSrPath);
            }

            if (activeSrHash.empty() && ctx.now - lastHashLookup > 500.0)
            {
                lastHashLookup = ctx.now;
                activeSrHash = FSR4Versions::HashForPath(activeSrPath);
            }

            // Path and hash of the dll that's loaded, closed unless the user opens it
            // Path and the hot-swap diagnostics: Advanced only
            if (!EssentialView() && ImGui::TreeNode(TrL("Loaded DLL path")))
            {
                ImGui::TextWrapped("%s", wstring_to_string(activeSrPath).c_str());
                ImGui::TextDisabled(Tr("SHA-256: %s"), activeSrHash.empty() ? Tr("calculating...") : activeSrHash.c_str());

                ImGui::TreePop();
            }

            // Read-only, everything comes from the dispatch calls that were actually made
            if (!EssentialView() && ImGui::TreeNode(TrL("UPSCALER HOT-SWAP DIAGNOSTICS")))
            {
                const auto diag = FfxApiProxy::SrDiagGetSnapshot();

                const ImVec4 statusColor = diag.statusLevel == 0   ? ImVec4(0.3f, 1.f, 0.3f, 1.f)
                                           : diag.statusLevel == 2 ? IlionTheme::Themed(ImVec4(1.f, 0.2f, 0.2f, 1.f), IlionTheme::Role::Bad)
                                                                   : IlionTheme::Themed(ImVec4(1.f, 0.8f, 0.f, 1.f), IlionTheme::Role::Warn);

                ImGui::Text(Tr("Selected: %s"), Tr(diag.selectedName.c_str()));
                ImGui::Text(Tr("Active: %s"), Tr(diag.activeName.c_str()));
                ShowTooltip("Dll that owns the ffxDispatch function called by the last upscale dispatch");
                ImGui::TextWrapped(Tr("DLL Path: %s"),
                                   diag.activePath.empty() ? "-" : wstring_to_string(diag.activePath).c_str());
                ImGui::TextColored(toneMapColor(statusColor), Tr("Status: %s"), Tr(diag.status.c_str()));
                ImGui::Text(Tr("Dispatch Count: %llu (errors: %llu)"), diag.activeDispatchCount, diag.activeErrorCount);

                for (const auto& module : diag.modules)
                    ImGui::TextDisabled("  %s: %llu", module.name.c_str(), module.dispatchCount);

                ImGui::Text(Tr("Last Switch: %s"), diag.observedSwitch.empty() ? "-" : diag.observedSwitch.c_str());
                ShowTooltip("Change of the dll that executes the dispatch, as seen in the dispatch calls");
                ImGui::TextDisabled(Tr("Request: %s"), diag.switchResult.empty() ? "-" : diag.switchResult.c_str());
                ImGui::TextDisabled(Tr("Context: %s"), diag.createResult.empty() ? "-" : diag.createResult.c_str());
                ImGui::TextDisabled(Tr("Traced by: %s"), Tr(diag.resolvedBy.c_str()));
                ImGui::TextDisabled(Tr("Driver provider: %s"),
                                    diag.driverProvider.empty() ? Tr("not requested") : diag.driverProvider.c_str());
                ShowTooltip("Result of amdxcffx64's provider update for the upscaler dll that asked for it.\n"
                            "When it succeeds the dll may run the driver's FSR 4 instead of its own,\n"
                            "that can't be told apart from the dispatch call.");

                ImGui::TreePop();
            }

            // Experimental, the marker color is keyed to the dll that recorded the GPU work, not to the selection
            if (ImGui::TreeNode(TrL("UPSCALER VISUAL VERIFICATION")))
            {
                bool visualEnabled = FSR4VisualVerify::Enabled();

                if (IlionTheme::Checkbox(TrL("Color marker on upscaler output"), &visualEnabled))
                    FSR4VisualVerify::SetEnabled(visualEnabled);

                ShowTooltip("Writes color bars into the output of the upscale dispatch, before the game's post\n"
                            "processing. Top and bottom bars: the dll below recorded the compute work of the frame.\n"
                            "Blue = FSR 4 official, Green = FSR 4.1.1B, Magenta = FSR 4 Lite, White = any other dll (HelixSR...).\n"
                            "Yellow top bar only = could not be verified.\n\n"
                            "The bars are drawn by OptiScaler, not by the dll. Not saved, off on next launch.");

                const auto evidence = FSR4VisualVerify::GetEvidence();

                if (!evidence.enabled)
                {
                    ImGui::TextDisabled("%s", Tr("Disabled"));
                }
                else if (!evidence.hasFrame || evidence.msSinceLastFrame > 1000)
                {
                    TextWrappedColored(toneMapColor(IlionTheme::Themed(ImVec4(1.f, 0.8f, 0.f, 1.f), IlionTheme::Role::Warn)),
                                       "%s", Tr("Verdict: NO DATA (no upscale dispatch in the last second)"));
                }
                else
                {
                    std::wstring execPath;
                    const auto execName = FfxApiProxy::SrDiagDescribeModule(evidence.execModule, &execPath);

                    ImGui::Text(Tr("Executing DLL: %s"), Tr(execName.c_str()));
                    ImGui::TextWrapped(Tr("DLL Path: %s"), execPath.empty() ? "-" : wstring_to_string(execPath).c_str());
                    ImGui::Text(Tr("Context created by: %s"),
                                Tr(evidence.ownerModule == nullptr               ? "unknown"
                                   : evidence.ownerModule == evidence.execModule ? "the same dll"
                                                                                 : "A DIFFERENT DLL"));
                    ImGui::TextWrapped(Tr("Compute dispatches: %u by this dll, %u by others"), evidence.computeFromExec,
                                evidence.computeFromOther);

                    if (evidence.otherModule != nullptr)
                    {
                        ImGui::Text(Tr("Other module: %s"),
                                    FfxApiProxy::SrDiagDescribeModule(evidence.otherModule).c_str());
                    }

                    ImGui::Text(Tr("Marker: %s"), Tr(FSR4VisualVerify::MarkerName(evidence.marker)));
                    ImGui::TextDisabled(Tr("Marker draw: %s"), Tr(FSR4VisualVerify::DrawStatusName(evidence.draw)));

                    if (evidence.verified)
                    {
                        ImGui::TextColored(toneMapColor(ImVec4(0.3f, 1.f, 0.3f, 1.f)), "%s", Tr("Verdict: VERIFIED"));
                    }
                    else
                    {
                        const char* reason = !evidence.instrumented             ? "command list can't be observed"
                                             : evidence.execModule == nullptr   ? "dispatch function not traced"
                                             : evidence.ownerModule == nullptr  ? "context owner unknown"
                                             : evidence.ownerModule != evidence.execModule
                                                 ? "context belongs to another dll"
                                             : evidence.computeFromOther > 0    ? "GPU work recorded by another module"
                                             : evidence.computeFromExec == 0    ? "no compute dispatch seen"
                                                                                : "dispatch failed";

                        ImGui::TextColored(toneMapColor(IlionTheme::Themed(ImVec4(1.f, 0.2f, 0.2f, 1.f), IlionTheme::Role::Bad)),
                                           Tr("Verdict: NOT VERIFIED (%s)"), Tr(reason));
                    }

                    ImGui::TextDisabled(Tr("Frames: %llu verified of %llu"), evidence.verifiedFrames, evidence.frames);
                }

                ImGui::TreePop();
            }

            // Not saved, results are shown under the performance overlay
            bool comparisonEnabled = FSR4Comparison::Enabled();

            if (IlionTheme::Checkbox(TrL("Upscaler comparison mode"), &comparisonEnabled))
            {
                FSR4Comparison::SetEnabled(comparisonEnabled);

                // The results panel is part of the performance overlay
                if (comparisonEnabled && !config->ShowFps.value_or_default())
                    config->ShowFps.set_volatile_value(true);
            }

            ShowTooltip("Measures every upscaler dll while it's the one executing the upscaling and shows\n"
                        "the results under the performance overlay, one box per upscaler, named as ILION's list\n"
                        "knows it. Switch the upscaler to measure the next one. Live only for FFX upscalers\n"
                        "(FSR 4 builds, HelixSR); DLSS is compared through saved Benchmark tests.\n\n"
                        "AVG = counted frames / their total time\n"
                        "MIN / MAX = slowest / fastest single frame, as fps (not 1%% lows)\n\n"
                        "The first 2 seconds after a switch aren't counted.\n"
                        "Enabling it starts a new session and shows the performance overlay.");
            }
        }

        if (basicView)
            return;

        auto majorFsrVersion = currentFeature->Version().major;

        if (majorFsrVersion >= 4)
        {
            ImGui::Spacing();

            // Colorspaces
            const char* colorSpaces[] = { Tr("Linear (Default)"), Tr("Non-Linear"), Tr("Non-Linear sRGB"), Tr("Non-Linear PQ") };
            int currentColorSpace = 0;
            if (config->FsrNonLinearPQ.value_or_default())
                currentColorSpace = 3;
            else if (config->FsrNonLinearSRGB.value_or_default())
                currentColorSpace = 2;
            else if (config->FsrNonLinearColorSpace.value_or_default())
                currentColorSpace = 1;

            ImGui::SetNextItemWidth(150.0f * menuResScale);
            if (ImGui::Combo(TrL("Input Color Space"), &currentColorSpace, colorSpaces, IM_ARRAYSIZE(colorSpaces)))
            {
                bool isSrgb = (currentColorSpace == 2);
                bool isPq = (currentColorSpace == 3);

                config->FsrNonLinearSRGB = isSrgb;
                config->FsrNonLinearPQ = isPq;

                if (isSrgb || isPq)
                {
                    config->FsrNonLinearColorSpace.set_volatile_value(true);
                }
                else if (currentColorSpace == 1) // Just non-Linear
                {
                    config->FsrNonLinearColorSpace = true;
                }
                else // Linear
                {
                    config->FsrNonLinearColorSpace = false;
                }

                state.newBackend = currentBackend;
                MARK_ALL_BACKENDS_CHANGED();
            }
            ShowTooltip("Select the input color space that the game uses.\n"
                        "Non-Linear / sRGB: Might improve FSR4 upscaling quality, might increase ghosting.\n"
                        "PQ: Rarest, might increase ghosting and break lights.");

            // FSR 4 Presets
            const char* presets[] = {
                Tr("Default"), Tr("Preset 0"), Tr("Preset 1"), Tr("Preset 2"), Tr("Preset 3"), Tr("Preset 4"), Tr("Preset 5")
            };
            int currentPresetIdx = config->Fsr4Preset.has_value() ? config->Fsr4Preset.value() + 1 : 0;

            if (currentPresetIdx < 0 || currentPresetIdx >= IM_ARRAYSIZE(presets))
                currentPresetIdx = 0;

            ImGui::SetNextItemWidth(150.0f * menuResScale);
            if (ImGui::Combo(TrL("FSR4 Preset"), &currentPresetIdx, presets, IM_ARRAYSIZE(presets)))
            {
                if (currentPresetIdx == 0)
                    config->Fsr4Preset.reset();
                else
                    config->Fsr4Preset = currentPresetIdx - 1;

                state.newBackend = currentBackend;
                MARK_ALL_BACKENDS_CHANGED();
            }
            ShowTooltip("Each internal FSR4 preset is tuned for a specific resolution.\n"
                        "Selecting an FSR4 preset won't change the in-game\nupscaler preset!!!\n\n"
                        "Preset 0 is meant for FSR Native AA\n"
                        "Preset 1 is meant for Quality/Ultra Quality\n"
                        "Preset 2 is meant for Balanced\n"
                        "Preset 3 is meant for Performance\n"
                        "Preset 4 is meant for DRS\n"
                        "Preset 5 is meant for Ultra Performance");

            // Display the active preset right next to the combo box instead of using a table
            ImGui::SameLine();
            if (state.currentFsr4Preset.has_value())
                ImGui::TextDisabled(Tr("(Active: %d)"), state.currentFsr4Preset.value());
            else if (FSR4ModelSelection::IsInt8FsrHooked())
                ImGui::TextColored(toneMapColor(IlionTheme::Themed(ImVec4(1.f, 0.8f, 0.f, 1.f), IlionTheme::Role::Warn)), Tr("(Potential FSR3 fallback)"));
            else
                ImGui::TextDisabled(Tr("(Failed to hook)"));

        }

        if (majorFsrVersion >= 3)
        {
            ImGui::Spacing();

            // The debug view is for Advanced; Essential keeps the watermark, the visible check of what runs
            const bool showDebugView = !EssentialView();

            if (showDebugView)
            {
                bool debugView = config->FsrDebugView.value_or_default();
                if (IlionTheme::Checkbox(TrL("Upscaler Debug View"), &debugView))
                {
                    config->FsrDebugView = debugView;

                    // FSR 4's debug view requires backend reinit
                    if (majorFsrVersion > 3)
                    {
                        state.newBackend = currentBackend;
                        MARK_ALL_BACKENDS_CHANGED();
                    }
                }

                if (majorFsrVersion > 3)
                {
                    ShowTooltip("Top left: Dilated Motion Vectors\n"
                                "Top right: Predicted Blend Factor");
                }
                else
                {
                    ShowTooltip("Top left: Dilated Motion Vectors\n"
                                "Top middle: Protected Areas\n"
                                "Top right: Dilated Depth\n"
                                "Middle: Upscaled frame\n"
                                "Bottom left: Disocclusion mask\n"
                                "Bottom middle: Reactiveness\n"
                                "Bottom right: Detail Protection Takedown");
                }
            }

            if (majorFsrVersion > 3)
            {
                if (showDebugView)
                    SameLineIfRoom(CheckboxWidth(TrL("Watermark")), 20.0f * menuResScale);
                bool fsr4wm = config->Fsr4EnableWatermark.value_or_default();
                if (IlionTheme::Checkbox(TrL("Watermark"), &fsr4wm))
                {
                    LOG_DEBUG("FSR4 Watermark set to {}", fsr4wm);
                    config->Fsr4EnableWatermark = fsr4wm;
                }

                ShowTooltip("After changing this option, please Save Settings.\n"
                            "It will be applied on next launch.");
            }
        }

        if (currentFeature->Version() >= feature_version { 3, 1, 1 } &&
            currentFeature->Version() < feature_version { 4, 0, 0 })
        {
            ImGui::Spacing();

            if (currentFeature != nullptr)
            {
                ImGui::Text(Tr("FSR 3.1 Presets:"));

                SameLineIfRoom(ButtonWidth(TrL("Stability")), 6.0f);

                // This will be applied by default
                if (IlionTheme::Button(TrL("Stability")))
                {
                    auto const scaleRatioX =
                        (float) currentFeature->TargetWidth() / (float) currentFeature->RenderWidth();
                    auto const scaleRatioY =
                        (float) currentFeature->TargetHeight() / (float) currentFeature->RenderHeight();
                    auto const scaleRatio = std::max(scaleRatioX, scaleRatioY);

                    config->FsrVelocity = 0.5f;
                    config->FsrReactiveScale = 0.25f;

                    config->FsrShadingScale.reset();
                    config->FsrAccAddPerFrame.reset();
                    config->FsrMinDisOccAcc.reset();
                    config->FsrShadingScale.set_volatile_value(0.5f / scaleRatio);
                    config->FsrAccAddPerFrame.set_volatile_value(scaleRatio / 10.0f);
                    config->FsrMinDisOccAcc.set_volatile_value(scaleRatio / 20.0f);
                }

                SameLineIfRoom(ButtonWidth(TrL("Motion")), 6.0f);

                if (IlionTheme::Button(TrL("Motion")))
                {
                    auto const scaleRatioX =
                        (float) currentFeature->TargetWidth() / (float) currentFeature->RenderWidth();
                    auto const scaleRatioY =
                        (float) currentFeature->TargetHeight() / (float) currentFeature->RenderHeight();
                    auto const scaleRatio = std::max(scaleRatioX, scaleRatioY);

                    config->FsrVelocity = 1.0f;
                    config->FsrReactiveScale = 0.5f;

                    config->FsrShadingScale.reset();
                    config->FsrAccAddPerFrame.reset();
                    config->FsrMinDisOccAcc.reset();
                    config->FsrShadingScale.set_volatile_value(1.0f / scaleRatio);
                    config->FsrAccAddPerFrame.set_volatile_value(scaleRatio / 10.0f);
                    config->FsrMinDisOccAcc.set_volatile_value(scaleRatio / 20.0f);
                }

                SameLineIfRoom(ButtonWidth(TrL("Default")), 6.0f);

                if (IlionTheme::Button(TrL("Default")))
                {
                    config->FsrVelocity = 1.0f;
                    config->FsrReactiveScale = 1.0f;
                    config->FsrShadingScale = 1.0f;
                    config->FsrAccAddPerFrame = 0.333f;
                    config->FsrMinDisOccAcc = -0.333f;
                }
            }

            ImGui::Spacing();

            if (auto ch = ScopedCollapsingHeader(TrL("FSR 3 Upscaler Manual Tuning")); ch.IsHeaderOpen())
            {
                ScopedIndent indent {};
                ImGui::Spacing();
                ImGui::Spacing();

                ImGui::PushItemWidth(220.0f * menuResScale);

                float velocity = config->FsrVelocity.value_or_default();
                if (IlionTheme::SliderFloat(TrL("Velocity Factor"), &velocity, 0.00f, 1.0f, "%.2f"))
                    config->FsrVelocity = velocity;

                ShowTooltip("Value of 0.0f can improve temporal stability of bright pixels\n"
                            "Lower values are more stable with ghosting\n"
                            "Higher values are more pixelly, but less ghosting");

                if (currentFeature->Version() >= feature_version { 3, 1, 4 })
                {
                    // Reactive Scale
                    float reactiveScale = config->FsrReactiveScale.value_or_default();
                    if (IlionTheme::SliderFloat(TrL("Reactive Scale"), &reactiveScale, 0.0f, 1.0f, "%.3f"))
                        config->FsrReactiveScale = reactiveScale;

                    ShowTooltip("Meant for development purpose to test if\n"
                                "writing a larger value to reactive mask, reduces ghosting.");

                    // Shading Scale
                    float shadingScale = config->FsrShadingScale.value_or_default();
                    if (IlionTheme::SliderFloat(TrL("Shading Scale"), &shadingScale, 0.0f, 1.0f, "%.3f"))
                        config->FsrShadingScale = shadingScale;

                    ShowTooltip("Increasing this scales FSR3.1 computed shading\n"
                                "change value at read to have higher reactiveness.");

                    // Accumulation Added Per Frame
                    float accAddPerFrame = config->FsrAccAddPerFrame.value_or_default();
                    if (IlionTheme::SliderFloat(TrL("Acc. Added Per Frame"), &accAddPerFrame, 0.0f, 1.0f, "%.3f"))
                        config->FsrAccAddPerFrame = accAddPerFrame;

                    ShowTooltip("Corresponds to amount of accumulation added per frame\n"
                                "at pixel coordinate where disocclusion occured or when\n"
                                "reactive mask value is > 0.0f. Decreasing this and \n"
                                "drawing the ghosting object (IE no mv) to reactive mask \n"
                                "with value close to 1.0f can decrease temporal ghosting.\n"
                                "Decreasing this could result in more thin feature pixels flickering.");

                    // Min Disocclusion Accumulation
                    float minDisOccAcc = config->FsrMinDisOccAcc.value_or_default();
                    if (IlionTheme::SliderFloat(TrL("Min. Disocclusion Acc."), &minDisOccAcc, -1.0f, 1.0f, "%.3f"))
                        config->FsrMinDisOccAcc = minDisOccAcc;

                    ShowTooltip("Increasing this value may reduce white pixel temporal\n"
                                "flickering around swaying thin objects that are disoccluding \n"
                                "one another often. Too high value may increase ghosting.");
                }

                ImGui::PopItemWidth();

                ImGui::Spacing();
                ImGui::Spacing();
            }
        }
    }

    if (basicView)
        return;

    // DLSS -----------------
    if ((config->DLSSEnabled.value_or_default() &&
         (currentBackend == Upscaler::DLSS || currentBackend == Upscaler::DLSS_on12) &&
         currentFeature->Version().major > 2) ||
        usesDlssd)
    {
        ImGui::Spacing();

        auto overridden = usesDlssd ? state.dlssdPresetsOverriddenExternally : state.dlssPresetsOverriddenExternally;

        if (overridden)
        {
            ImGui::TextColored(toneMapColor(IlionTheme::Themed(ImVec4(1.f, 0.8f, 0.f, 1.f), IlionTheme::Role::Warn)), Tr("Presets are overridden externally"));
            ShowTooltip("This usually happens due to using tools\n"
                        "such as Nvidia App or Nvidia Inspector");
            // ImGui::TextWrapped(Tr("Selecting setting below will disable that external override\n")
            //             "but you need to Save Settings and restart the game");

            ImGui::Spacing();
        }

        // With d4r the neural model is set in d4r.ini and imposed by its shim on every evaluation
        if (state.dlssExternalBackend && !usesDlssd)
            IlionD4rPanel::PresetNote();

        if (usesDlssd)
        {
            if (bool pOverride = config->DLSSDRenderPresetOverride.value_or_default();
                IlionTheme::Checkbox(TrL("Render Presets Override"), &pOverride))
                config->DLSSDRenderPresetOverride = pOverride;

            ShowTooltip("Each render preset has it strengths and weaknesses\n"
                        "Override to potentially improve image quality\n"
                        "Press apply after enable/disable");

            /*
            auto currentPresetIndex = GetPresetIndex(currentFeature, true);

            if (currentPresetIndex == 0)
                ImGui::Text(Tr("Current Preset: Default"));
            else
                ImGui::Text(Tr("Current Preset: %c"), 64 + currentPresetIndex);
            */

            ImGui::BeginDisabled(!config->DLSSDRenderPresetOverride.value_or_default() /*|| overridden*/);
            ImGui::PushItemWidth(135.0f * menuResScale);

            AddDLSSDRenderPreset("Override Preset", &comboPreset);

            ImGui::PopItemWidth();
            ImGui::EndDisabled();
        }
        else
        {
            if (bool pOverride = config->RenderPresetOverride.value_or_default();
                IlionTheme::Checkbox(TrL("Render Presets Override"), &pOverride))
                config->RenderPresetOverride = pOverride;

            ShowTooltip("Each render preset has it strengths and weaknesses\n"
                        "Override to potentially improve image quality\n"
                        "Press Apply after enable/disable");

            /*
            auto currentPresetIndex = GetPresetIndex(currentFeature, false);

            if (currentPresetIndex == 0)
                ImGui::Text(Tr("Current Preset: Default"));
            else
                ImGui::Text(Tr("Current Preset: %c"), 64 + currentPresetIndex);
            */

            ImGui::BeginDisabled(!config->RenderPresetOverride.value_or_default() /*|| overridden*/);

            ImGui::PushItemWidth(135.0f * menuResScale);

            AddDLSSRenderPreset("Override Preset", &comboPreset);

            ImGui::PopItemWidth();
            ImGui::EndDisabled();
        }

        SameLineIfRoom(ButtonWidth(TrL("Apply Changes")), 6.0f);

        if (IlionTheme::Button(TrL("Apply Changes")))
        {
            LOG_DEBUG("Applying DLSS/DLSSD preset override changes, preset index: {}", comboPreset.value_or_default());

            if (usesDlssd)
            {
                config->DLSSDRenderPresetForAll = comboPreset.value_or_default();
                state.newBackend = Upscaler::DLSSD;
            }
            else
            {
                config->RenderPresetForAll = comboPreset.value_or_default();
                state.newBackend = currentBackend;
            }

            MARK_ALL_BACKENDS_CHANGED();
        }

        ImGui::Spacing();

        if (auto ch = ScopedCollapsingHeader(TrL(usesDlssd ? "Advanced DLSSD Settings" : "Advanced DLSS Settings"));
            ch.IsHeaderOpen())
        {
            ScopedIndent indent {};
            ImGui::Spacing();

            bool appIdOverride = config->UseGenericAppIdWithDlss.value_or_default();
            if (IlionTheme::Checkbox(TrL("Use Generic App Id with DLSS"), &appIdOverride))
                config->UseGenericAppIdWithDlss = appIdOverride;

            ShowTooltip("Use generic appid with NGX\n"
                        "Fixes OptiScaler preset override not working with certain games\n"
                        "Requires a game restart");

            ImGui::BeginDisabled(!config->RenderPresetOverride.value_or_default() || overridden);
            ImGui::Spacing();
            ImGui::PushItemWidth(135.0f * menuResScale);

            if (usesDlssd)
            {
                AddDLSSDRenderPreset("DLAA Preset", &config->DLSSDRenderPresetDLAA);
                AddDLSSDRenderPreset("UltraQ Preset", &config->DLSSDRenderPresetUltraQuality);
                AddDLSSDRenderPreset("Quality Preset", &config->DLSSDRenderPresetQuality);
                AddDLSSDRenderPreset("Balanced Preset", &config->DLSSDRenderPresetBalanced);
                AddDLSSDRenderPreset("Perf Preset", &config->DLSSDRenderPresetPerformance);
                AddDLSSDRenderPreset("UltraP Preset", &config->DLSSDRenderPresetUltraPerformance);
            }
            else
            {
                AddDLSSRenderPreset("DLAA Preset", &config->RenderPresetDLAA);
                AddDLSSRenderPreset("UltraQ Preset", &config->RenderPresetUltraQuality);
                AddDLSSRenderPreset("Quality Preset", &config->RenderPresetQuality);
                AddDLSSRenderPreset("Balanced Preset", &config->RenderPresetBalanced);
                AddDLSSRenderPreset("Perf Preset", &config->RenderPresetPerformance);
                AddDLSSRenderPreset("UltraP Preset", &config->RenderPresetUltraPerformance);
            }
            ImGui::PopItemWidth();
            ImGui::EndDisabled();

            ImGui::Spacing();
            ImGui::Spacing();
        }
    }
}

// ILION: the Frame Generation screen, a small CRT next to the FG input and output. It names the implementation that
// generates the frames (engine, variant, dll or asi, version) and its state. Everything comes from what ILION
// observes; nothing is started or asked to any provider. Each generator shows only its own data: a loaded .asi is
// shown only when it belongs to the generator in use (XeMFG plugins with XeFG), and a loaded .asi or a variant read
// from a file never counts as a confirmation of what runs inside it.
struct FgCrtData
{
    std::string engine = "-";     // the generator: XeFG, XeMFG, FSR FG, DLSS Enabler, ...
    std::string variant = "-";    // its variant when it can be told from its file (XeMFG: WMMA or classic)
    std::string file = "-";       // the dll or asi of that generator
    std::string version = "-";    // the version of that file or the version the generator reports
    std::string multiplier = "-"; // real multiplier while frames are generated
    bool fromAsi = false;
    bool variantFromFile = false; // the variant comes from the file; what runs at the moment isn't confirmed
    int status = 0;               // 0 off, 1 not loaded, 2 loaded, 3 paused, 4 active
};

static std::string FgVersionText(const feature_version& version)
{
    if (version.major == 0 && version.minor == 0 && version.patch == 0)
        return "-";

    return StrFmt("%u.%u.%u", (unsigned) version.major, (unsigned) version.minor, (unsigned) version.patch);
}

static std::string FileVersionText(const std::wstring& path)
{
    version_t version;
    if (path.empty() || !Util::GetFileVersion(path, &version))
        return "-";

    return StrFmt("%u.%u.%u.%u", (unsigned) version.major, (unsigned) version.minor, (unsigned) version.patch,
                  (unsigned) version.reserved);
}

static std::wstring ModulePath(HMODULE module)
{
    if (module == nullptr)
        return L"";

    wchar_t path[MAX_PATH] {};
    return GetModuleFileNameW(module, path, MAX_PATH) > 0 ? std::wstring(path) : std::wstring();
}

// An .asi read once: whether it is an XeMFG plugin and which variant its file is. MFG Unlocker builds with the
// WMMA route carry it by name ("XeFG WMMA route"); a build without those strings is the classic one.
struct XeMfgFile
{
    bool xemfg = false;
    std::string variant; // "WMMA", "classic" or empty
    std::string product;
    std::string version;
};

static XeMfgFile ReadXeMfgFile(const std::wstring& path)
{
    static std::mutex cacheLock;
    static std::map<std::wstring, XeMfgFile> cache;

    std::scoped_lock lock(cacheLock);

    if (const auto found = cache.find(path); found != cache.end())
        return found->second;

    XeMfgFile info;
    info.version = FileVersionText(path);

    std::string name = wstring_to_string(std::filesystem::path(path).filename().wstring());
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return (char) std::tolower(c); });

    std::ifstream file(path, std::ios::binary);
    const std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    const bool mfgUnlocker = content.find("MFG Unlocker") != std::string::npos || name.find("mfgunlocker") != std::string::npos;
    info.xemfg = mfgUnlocker && content.find("libxess_fg") != std::string::npos;

    if (info.xemfg)
    {
        info.product = "MFG Unlocker";
        info.variant = content.find("XeFG WMMA route") != std::string::npos || content.find("WMMA-port") != std::string::npos
                           ? "WMMA"
                           : "classic";
    }

    cache[path] = info;
    return info;
}

static FgCrtData GatherFgCrt(State& state)
{
    FgCrtData d;
    auto fg = state.currentFG;

    // The DLSSG replacement is the generator when the game's DLSSG is redirected
    const auto nvngx = state.activeFgNvngx;
    const bool nvngxOutput = nvngx != FGNvngxReplacement::None &&
                             (state.activeFgInput == FGInput::NvngxFG || state.activeFgOutput == FGOutput::DLSSG);
    const bool nvngxLoaded = nvngxOutput && (state.swapchainApi == Vulkan ? Nvngx_FG::isVulkanAvailable()
                                                                          : Nvngx_FG::isDx12Available());

    // State and real multiplier
    if (fg != nullptr && fg->IsActive() && !fg->IsPaused())
    {
        d.status = 4;
        d.multiplier = StrFmt("%ux", (unsigned) fg->GetInterpolatedFrameCount() + 1);
    }
    else if (state.dlssgDetectedInterpolationCount > 0)
    {
        d.status = 4;
        d.multiplier = StrFmt("%dx", state.dlssgDetectedInterpolationCount + 1);
    }
    else if (fg != nullptr && fg->IsActive())
    {
        d.status = 3;
    }
    else if (fg != nullptr || nvngxLoaded)
    {
        d.status = 2;
    }
    else if (state.activeFgInput != FGInput::NoFG || state.activeFgOutput != FGOutput::NoFG || nvngxOutput)
    {
        d.status = 1;
    }

    if (nvngxOutput)
    {
        // DLSS Enabler and the other DLSSG replacements: their own dll and the version they report, never an .asi
        const wchar_t* dll = nvngx == FGNvngxReplacement::Arturs   ? L"dlss-enabler-headless.dll"
                             : nvngx == FGNvngxReplacement::Nukems ? L"dlssg_to_fsr3_amd_is_better.dll"
                                                                   : nullptr;

        d.engine = nvngx == FGNvngxReplacement::Arturs   ? "DLSS Enabler"
                   : nvngx == FGNvngxReplacement::Nukems ? "Nukem's FSR3"
                   : nvngx == FGNvngxReplacement::FFX    ? "FFX FG"
                                                         : "FFX + Enabler";

        if (dll != nullptr)
            d.file = wstring_to_string(dll);
        else if (const auto path = FfxApiProxy::Dx12Module_FG_Path(); !path.empty())
            d.file = wstring_to_string(std::filesystem::path(path).filename().wstring());

        if (nvngxLoaded)
            d.version = FgVersionText(Nvngx_FG::version());

        return d;
    }

    switch (state.activeFgOutput)
    {
    case FGOutput::XeFG:
    {
        d.engine = "XeFG";
        const auto path = ModulePath(XeFGProxy::Module());

        if (!path.empty())
        {
            d.file = wstring_to_string(std::filesystem::path(path).filename().wstring());
            d.version = FileVersionText(path);
        }

        // An XeMFG plugin the loader loaded takes over XeFG's frame count: it is the implementation shown
        std::vector<std::wstring> asis;
        {
            std::scoped_lock lock(state.loadedAsiMutex);
            asis = state.loadedAsiPlugins;
        }

        for (const auto& asi : asis)
        {
            const auto info = ReadXeMfgFile(asi);
            if (!info.xemfg)
                continue;

            d.engine = "XeMFG";
            d.file = wstring_to_string(std::filesystem::path(asi).filename().wstring());
            d.version = info.version;
            d.variant = info.variant;
            d.fromAsi = true;
            d.variantFromFile = true;
            break;
        }

        break;
    }
    case FGOutput::FSRFG:
    {
        d.engine = "FSR FG";
        if (const auto path = FfxApiProxy::Dx12Module_FG_Path(); !path.empty())
            d.file = wstring_to_string(std::filesystem::path(path).filename().wstring());
        if (fg != nullptr)
            d.version = FgVersionText(fg->Version());
        break;
    }
    case FGOutput::DLSSG:
        d.engine = "DLSSG";
        d.file = "Streamline";
        d.version = FgVersionText(state.streamlineVersion);
        break;
    case FGOutput::Reprojection:
        d.engine = "Reprojection";
        d.file = "OptiScaler";
        break;
    default:
        break;
    }

    return d;
}

// Short words of the screen in the language of the menu
static const char* FgCrtWord(const char* english, const char* spanish)
{
    return Ilion::CurrentLang() == Ilion::Lang::EN ? english : spanish;
}

// Draws the CRT: a thin metal bezel with four screws, the dark recess, the deep green curved glass, then the text
static void DrawFgCrt(const ImVec2& min, const ImVec2& size, const FgCrtData& d)
{
    ImDrawList* draw = ImGui::GetWindowDrawList();
    ImFont* font = IlionTheme::crtFont != nullptr ? IlionTheme::crtFont : ImGui::GetFont();
    ImFont* bigFont = IlionTheme::crtTitleFont != nullptr ? IlionTheme::crtTitleFont : font;
    const float fs = ImGui::GetFontSize();
    const ImVec2 max(min.x + size.x, min.y + size.y);
    const auto col = [](float r, float g, float b, float a = 1.0f) { return ImGui::GetColorU32(ImVec4(r, g, b, a)); };

    // Bezel: brushed metal, lighter at the top, a fine highlight and a dark outline
    const float bezel = std::floor(fs * 0.55f);
    const float rounding = std::floor(fs * 0.45f);
    draw->AddRectFilled(ImVec2(min.x + 2.0f, min.y + 3.0f), ImVec2(max.x + 2.0f, max.y + 3.0f), col(0, 0, 0, 0.45f), rounding);
    draw->AddRectFilledMultiColor(min, max, col(0.40f, 0.41f, 0.38f), col(0.40f, 0.41f, 0.38f), col(0.17f, 0.18f, 0.16f),
                                  col(0.17f, 0.18f, 0.16f));
    draw->AddRect(min, max, col(0.05f, 0.05f, 0.05f), rounding, 0, 1.5f);
    draw->AddLine(ImVec2(min.x + rounding, min.y + 1.0f), ImVec2(max.x - rounding, min.y + 1.0f), col(0.75f, 0.76f, 0.72f, 0.55f));

    // Screws in the corners of the bezel
    const float screw = std::max(2.0f, std::floor(fs * 0.17f));
    for (const ImVec2 at : { ImVec2(min.x + bezel * 0.5f, min.y + bezel * 0.5f), ImVec2(max.x - bezel * 0.5f, min.y + bezel * 0.5f),
                             ImVec2(min.x + bezel * 0.5f, max.y - bezel * 0.5f), ImVec2(max.x - bezel * 0.5f, max.y - bezel * 0.5f) })
    {
        draw->AddCircleFilled(at, screw, col(0.55f, 0.56f, 0.52f));
        draw->AddCircle(at, screw, col(0.08f, 0.08f, 0.08f), 0, 1.0f);
        draw->AddLine(ImVec2(at.x - screw * 0.6f, at.y + screw * 0.6f), ImVec2(at.x + screw * 0.6f, at.y - screw * 0.6f),
                      col(0.15f, 0.15f, 0.14f), 1.0f);
    }

    // Recess, then the glass set a little into it
    const ImVec2 rMin(min.x + bezel, min.y + bezel), rMax(max.x - bezel, max.y - bezel);
    draw->AddRectFilled(rMin, rMax, col(0.0f, 0.0f, 0.0f), rounding * 1.6f);

    const ImVec2 gMin(rMin.x + 2.0f, rMin.y + 2.0f), gMax(rMax.x - 2.0f, rMax.y - 2.0f);
    const float glassRound = rounding * 1.5f;
    const float gw = gMax.x - gMin.x, gh = gMax.y - gMin.y;
    draw->AddRectFilled(gMin, gMax, col(0.016f, 0.070f, 0.036f), glassRound);

    draw->PushClipRect(ImVec2(gMin.x + 1.0f, gMin.y + 1.0f), ImVec2(gMax.x - 1.0f, gMax.y - 1.0f), true);

    // Depth: the tube darkens towards every edge (no lit block behind the text), the recess casts a soft shadow
    // on the top of the glass
    const ImU32 clear = col(0, 0, 0, 0), edge = col(0, 0, 0, 0.50f), shadow = col(0, 0, 0, 0.45f);
    draw->AddRectFilledMultiColor(gMin, ImVec2(gMin.x + gw * 0.22f, gMax.y), edge, clear, clear, edge);
    draw->AddRectFilledMultiColor(ImVec2(gMax.x - gw * 0.22f, gMin.y), gMax, clear, edge, edge, clear);
    draw->AddRectFilledMultiColor(gMin, ImVec2(gMax.x, gMin.y + gh * 0.14f), shadow, shadow, clear, clear);
    draw->AddRectFilledMultiColor(ImVec2(gMin.x, gMax.y - gh * 0.18f), gMax, clear, clear, edge, edge);

    // Fine scan lines
    for (float y = gMin.y + 2.0f; y < gMax.y; y += 3.0f)
        draw->AddLine(ImVec2(gMin.x, y), ImVec2(gMax.x, y), col(0, 0, 0, 0.14f));

    // Inner depth: the glass sits behind the bezel, a soft dark band follows its rounded edge
    draw->AddRect(ImVec2(gMin.x + 1.5f, gMin.y + 1.5f), ImVec2(gMax.x - 1.5f, gMax.y - 1.5f), col(0, 0, 0, 0.35f), glassRound, 0, 3.0f);
    draw->AddRect(ImVec2(gMin.x + 4.0f, gMin.y + 4.0f), ImVec2(gMax.x - 4.0f, gMax.y - 4.0f), col(0, 0, 0, 0.15f), glassRound, 0, 3.0f);

    // Reflections, very faint: a curved highlight at the top left and a thin sheen along the bottom
    draw->AddBezierQuadratic(ImVec2(gMin.x + gw * 0.07f, gMin.y + gh * 0.28f), ImVec2(gMin.x + gw * 0.10f, gMin.y + gh * 0.07f),
                             ImVec2(gMin.x + gw * 0.38f, gMin.y + gh * 0.06f), col(0.85f, 1.0f, 0.90f, 0.07f), 1.5f);
    draw->AddLine(ImVec2(gMin.x + gw * 0.15f, gMax.y - 2.5f), ImVec2(gMax.x - gw * 0.15f, gMax.y - 2.5f),
                  col(0.70f, 0.95f, 0.75f, 0.06f), 1.0f);

    // The bulge of the tube: a faint sheen across the upper third and a second, shorter arc at the lower right
    draw->AddRectFilledMultiColor(ImVec2(gMin.x + gw * 0.06f, gMin.y + gh * 0.04f), ImVec2(gMax.x - gw * 0.06f, gMin.y + gh * 0.32f),
                                  col(0.80f, 1.0f, 0.85f, 0.035f), col(0.80f, 1.0f, 0.85f, 0.035f), clear, clear);
    draw->AddBezierQuadratic(ImVec2(gMax.x - gw * 0.30f, gMax.y - gh * 0.06f), ImVec2(gMax.x - gw * 0.08f, gMax.y - gh * 0.08f),
                             ImVec2(gMax.x - gw * 0.06f, gMax.y - gh * 0.30f), col(0.85f, 1.0f, 0.90f, 0.05f), 1.2f);

    // Text: bright amber labels, lemon yellow values (clearly apart from the amber), ACTIVE in vivid phosphor green
    const ImVec4 amber(1.00f, 0.80f, 0.28f, 1.0f), yellow(1.00f, 1.00f, 0.42f, 1.0f), dim(0.56f, 0.64f, 0.48f, 1.0f);
    const ImVec4 phosphor(0.30f, 1.00f, 0.42f, 1.0f);
    const auto text = [&](float sizePx, const ImVec2& at, const ImVec4& c, const char* s, ImFont* f)
    {
        // Defined letters: a dark edge one pixel down-right separates them from the glass, a faint phosphor bleed
        // below, then the glyph itself; same position and size as before
        draw->AddText(f, sizePx, ImVec2(at.x + 1.0f, at.y + 1.0f), col(0, 0, 0, 0.55f), s);
        draw->AddText(f, sizePx, ImVec2(at.x, at.y + 1.0f), col(c.x, c.y, c.z, 0.14f), s);
        draw->AddText(f, sizePx, at, ImGui::GetColorU32(c), s);
    };

    const float pad = std::floor(fs * 0.55f);
    const float x = gMin.x + pad;
    const float right = gMax.x - pad;
    float y = gMin.y + std::floor(pad * 0.7f);

    // Title line: FG and its state
    const char* statusText = d.status == 4   ? FgCrtWord("ACTIVE", "ACTIVO")
                             : d.status == 3 ? FgCrtWord("PAUSED", "PAUSA")
                             : d.status == 2 ? FgCrtWord("LOADED", "CARGADO")
                             : d.status == 1 ? FgCrtWord("NOT LOADED", "SIN CARGAR")
                                             : FgCrtWord("OFF", "APAGADO");
    const ImVec4 statusColour = d.status == 4 ? phosphor : d.status >= 2 ? amber : dim;
    const float titleSize = std::floor(fs * 1.15f);
    text(titleSize, ImVec2(x, y), amber, "FG", bigFont);
    const float statusWidth = bigFont->CalcTextSizeA(titleSize, FLT_MAX, 0.0f, statusText).x;
    text(titleSize, ImVec2(right - statusWidth, y), statusColour, statusText, bigFont);
    y += titleSize + std::floor(fs * 0.25f);
    draw->AddLine(ImVec2(x, y), ImVec2(right, y), col(amber.x, amber.y, amber.z, 0.50f), 1.0f);
    y += std::floor(fs * 0.3f);

    // Rows: label, colon, value cut with a dot when it doesn't fit
    const float labelWidth = font->CalcTextSizeA(fs, FLT_MAX, 0.0f, "MULT ").x;
    const auto row = [&](const char* label, const std::string& value)
    {
        text(fs, ImVec2(x, y), amber, label, font);
        text(fs, ImVec2(x + labelWidth, y), amber, ":", font);

        const float valueX = x + labelWidth + font->CalcTextSizeA(fs, FLT_MAX, 0.0f, ": ").x;
        std::string shown = value;
        while (shown.size() > 1 && font->CalcTextSizeA(fs, FLT_MAX, 0.0f, shown.c_str()).x > right - valueX)
        {
            shown.pop_back();
            while (!shown.empty() && ((unsigned char) shown.back() & 0xC0) == 0x80)
                shown.pop_back();
            if (!shown.empty())
                shown.pop_back();
            shown += ".";
        }

        text(fs, ImVec2(valueX, y), shown == "-" ? dim : yellow, shown.c_str(), font);
        y += std::floor(fs * 1.15f);
    };

    const std::string variant = d.variant == "classic" ? std::string(Tr("CLASSIC")) : d.variant;
    row(FgCrtWord("ENG", "MOT"), d.engine);
    row("VAR", variant);
    row(d.fromAsi ? "ASI" : "DLL", d.file);
    row("VER", d.version);

    y += std::floor(fs * 0.1f);
    draw->AddLine(ImVec2(x, y), ImVec2(right, y), col(amber.x, amber.y, amber.z, 0.50f), 1.0f);
    y += std::floor(fs * 0.3f);

    // Multiplier, the largest figure of the screen
    const float bigSize = std::floor(fs * 1.6f);
    text(fs, ImVec2(x, y + (bigSize - fs) * 0.5f), amber, "MULT", font);
    text(fs, ImVec2(x + labelWidth, y + (bigSize - fs) * 0.5f), amber, ":", font);
    text(bigSize, ImVec2(x + labelWidth + font->CalcTextSizeA(fs, FLT_MAX, 0.0f, ": ").x, y), d.multiplier == "-" ? dim : yellow,
         d.multiplier.c_str(), bigFont);

    draw->PopClipRect();

    // Thin rim of the glass where it meets the recess
    draw->AddRect(gMin, gMax, col(0.30f, 0.55f, 0.35f, 0.30f), glassRound, 0, 1.0f);
}

// The size the screen needs at the menu's font size (rows above, plus the bezel)
static ImVec2 FgCrtSize()
{
    const float fs = ImGui::GetFontSize();
    return ImVec2(std::floor(fs * 14.5f), std::floor(fs * 11.2f));
}

// The tooltip: the same data written out, plus what a variant read from a file does and doesn't prove
static void FgCrtTooltip(const FgCrtData& d)
{
    const char* state = d.status == 4   ? Tr("active, generating frames")
                        : d.status == 3 ? Tr("set up, paused")
                        : d.status == 2 ? Tr("loaded, not generating")
                        : d.status == 1 ? Tr("selected, not loaded")
                                        : Tr("off");

    const std::string variant = d.variant == "classic" ? std::string(Tr("classic")) : d.variant;

    std::string tip = Tr("Frame Generation diagnosis: data ILION observes, without starting any provider.");
    tip += StrFmt("\n%s: %s\n%s: %s\n%s: %s\n%s: %s\n%s: %s\n%s: %s", Tr("Engine"), d.engine.c_str(), Tr("Variant"),
                  variant.c_str(), d.fromAsi ? Tr("ASI plugin loaded") : Tr("File"), d.file.c_str(), Tr("Version"),
                  d.version.c_str(), Tr("State"), state, Tr("Multiplier"), d.multiplier.c_str());

    if (d.variantFromFile)
        tip += std::string("\n\n") + Tr("The variant is read from the plugin's file. Which route it really runs (WMMA or its "
                                        "fallback) can't be confirmed from ILION: not confirmed.");

    WrappedTooltip("%s", tip.c_str());
}

// ILION: «Coste MFG», a small amber screen under the FG screen. It compares the frames the game really renders while
// frames are generated with a reference the user captures: 10 s of real play with FG off, on request.
//   real frames  counters that move once per game frame: OptiScaler's upscaler (every backend counts its Evaluate),
//                OptiScaler's FG (IFGFeature::StartNewFrame), the game's DLSSG evaluate (DLSS Enabler, Nukem's, FFX);
//                the presents too while nothing is generated. Never the output divided by the multiplier.
//   reference    one average per counter over the capture, each from at least 9 s of valid quarters; the comparison
//                uses the one of its own counter, the presents' only for a DLSSG-only route (without FG every real frame
//                is presented once). Kept through multiplier changes, until another capture or the end of the session.
//   total        the presents while generating: the output ILION sees, generated frames included; shown only when it
//                is clearly above the real FPS, else that route's generated frames aren't seen by ILION.
//   sampling     quarters of a second from RenderMenu on every present, also with the menu closed; a few additions.
struct MfgCostState
{
    enum Source
    {
        Upscaler,
        FgInput,
        Dlssg,
        Presents,
        SourceCount
    };

    enum Notice
    {
        None,
        Canceled,     // FG came on during the capture
        Insufficient, // no counter had 9 valid seconds
    };

    double bucketStart = 0.0;
    uint64_t bucketCounts[SourceCount] {};
    const void* bucketOwners[SourceCount] {}; // a recreated upscaler or FG restarts the quarter

    bool engaged = false;      // a FG session runs: its multiplier may change frame by frame (dynamic MFG)
    bool generating = false;   // engaged and not paused
    bool unmeasurable = false; // generating, and no counter of game frames moves
    int source = -1;           // the counter of the current generation, chosen at its first quarter
    double endedAt = -1.0e9;   // a stop shorter than 3 s (a multiplier change) keeps that counter
    unsigned multiplier = 0;

    double reference[SourceCount] {}; // FPS per counter from the last capture; 0: none

    bool capturing = false;
    double captureMs = 0.0;
    double captureFrames[SourceCount] {};
    double captureValidMs[SourceCount] {};
    Notice notice = None;
    double noticeAt = 0.0;

    double current = 0.0; // real FPS of the last quarter; 0: not measured
    double total = 0.0;   // presented FPS of the last quarter; 0: not verifiable
};

static MfgCostState mfgCost;

static const char* MfgCostSourceName(int source)
{
    switch (source)
    {
    case MfgCostState::Upscaler:
        return FgCrtWord("OptiScaler's upscaler", "escalador de OptiScaler");
    case MfgCostState::FgInput:
        return FgCrtWord("OptiScaler's FG", "FG de OptiScaler");
    case MfgCostState::Dlssg:
        return FgCrtWord("the game's DLSSG calls", "llamadas al DLSSG del juego");
    default:
        return "-";
    }
}

// The reference the screen compares with: the one of the generation's counter; with FG off, the best one captured
static double MfgCostReference()
{
    using S = MfgCostState;
    const auto& m = mfgCost;

    if (m.source >= 0)
    {
        if (m.reference[m.source] > 0.0)
            return m.reference[m.source];

        return m.source == S::Dlssg ? m.reference[S::Presents] : 0.0;
    }

    if (m.generating)
        return 0.0;

    for (int i : { S::Upscaler, S::FgInput, S::Presents })
    {
        if (m.reference[i] > 0.0)
            return m.reference[i];
    }

    return 0.0;
}

static void StartMfgCostCapture()
{
    auto& m = mfgCost;
    if (m.generating)
        return;

    m.capturing = true;
    m.captureMs = 0.0;
    std::fill(std::begin(m.captureFrames), std::end(m.captureFrames), 0.0);
    std::fill(std::begin(m.captureValidMs), std::end(m.captureValidMs), 0.0);
    m.notice = MfgCostState::None;
}

static void UpdateMfgCost(State& state, double nowMs)
{
    using S = MfgCostState;
    auto& m = mfgCost;
    IFGFeature* fg = state.currentFG;
    IFeature* upscaler = state.currentFeature;

    // The FG session: OptiScaler's FG set up, or the game calling its DLSSG in the last half second (it calls it every
    // frame, also when dynamic MFG generates nothing for a frame). Not the multiplier of each frame.
    const bool optiFg = fg != nullptr && fg->IsActive();
    const uint64_t lastDlssg = state.dlssgFgLastEvaluateTick;
    const bool dlssgFg = lastDlssg != 0 && GetTickCount64() - lastDlssg < 500;
    m.engaged = optiFg || dlssgFg;
    const bool generating = (optiFg && !fg->IsPaused()) || (!optiFg && dlssgFg);

    if (optiFg && fg->GetInterpolatedFrameCount() > 0)
        m.multiplier = fg->GetInterpolatedFrameCount() + 1;
    else if (dlssgFg && state.dlssgDetectedInterpolationCount > 0)
        m.multiplier = state.dlssgDetectedInterpolationCount + 1;

    const uint64_t counts[S::SourceCount] = { upscaler != nullptr ? (uint64_t) upscaler->FrameCount() : 0,
                                              fg != nullptr ? fg->FrameCount() : 0, state.dlssgFgEvaluations.load(), 0 };
    const void* owners[S::SourceCount] = { upscaler, fg, nullptr, nullptr };
    m.bucketCounts[S::Presents]++;

    const auto restartBucket = [&]()
    {
        m.bucketStart = nowMs;
        for (int i = 0; i < S::SourceCount; i++)
        {
            m.bucketCounts[i] = counts[i];
            m.bucketOwners[i] = owners[i];
        }
        m.bucketCounts[S::Presents] = 0;
    };

    // Generation starts or stops: the quarter that mixes both is dropped; a capture running is cancelled
    if (generating != m.generating)
    {
        m.generating = generating;

        if (generating)
        {
            if (nowMs - m.endedAt >= 3000.0)
                m.source = -1;

            if (m.capturing)
            {
                m.capturing = false;
                m.notice = S::Canceled;
                m.noticeAt = nowMs;
            }
        }
        else
        {
            m.endedAt = nowMs;
        }

        m.current = 0.0;
        m.total = 0.0;
        restartBucket();
        return;
    }

    bool ownersChanged = m.bucketStart == 0.0;
    for (int i = 0; i < S::SourceCount; i++)
        ownersChanged = ownersChanged || owners[i] != m.bucketOwners[i];

    if (ownersChanged)
    {
        restartBucket();
        return;
    }

    const double elapsed = nowMs - m.bucketStart;
    if (elapsed < 250.0)
        return;

    // Frames of each counter in this quarter; 0 when it didn't move, or the quarter took over a second (a stall)
    double frames[S::SourceCount] {};
    for (int i = 0; i < S::SourceCount; i++)
    {
        const uint64_t delta = i == S::Presents ? m.bucketCounts[i] : counts[i] - m.bucketCounts[i];
        if (elapsed <= 1000.0 && delta * 1000.0 / elapsed >= 5.0)
            frames[i] = (double) delta;
    }

    const double presented = frames[S::Presents] * 1000.0 / elapsed;

    if (generating)
    {
        if (m.source < 0)
        {
            for (int i = S::Upscaler; i < S::Presents && m.source < 0; i++)
            {
                if (frames[i] > 0.0)
                    m.source = i;
            }
        }

        m.unmeasurable = m.source < 0;
        m.current = m.source >= 0 ? frames[m.source] * 1000.0 / elapsed : 0.0;
        m.total = m.current > 0.0 && presented >= m.current * (m.multiplier > 1 ? 1.25 : 1.0) ? presented : 0.0;
    }
    else
    {
        // With FG off the presents are real frames too; a game frame counter is preferred when one moves
        int best = -1;
        for (int i : { S::Upscaler, S::FgInput, S::Presents })
        {
            if (best < 0 && frames[i] > 0.0)
                best = i;
        }

        m.unmeasurable = false;
        m.current = best >= 0 ? frames[best] * 1000.0 / elapsed : 0.0;
        m.total = presented;

        if (m.capturing)
        {
            m.captureMs += elapsed;
            for (int i = 0; i < S::SourceCount; i++)
            {
                if (frames[i] > 0.0)
                {
                    m.captureFrames[i] += frames[i];
                    m.captureValidMs[i] += elapsed;
                }
            }

            if (m.captureMs >= 10000.0)
            {
                double captured[S::SourceCount] {};
                bool any = false;
                for (int i = 0; i < S::SourceCount; i++)
                {
                    if (m.captureValidMs[i] >= 9000.0)
                    {
                        captured[i] = m.captureFrames[i] * 1000.0 / m.captureValidMs[i];
                        any = true;
                    }
                }

                // A capture without enough valid data leaves the previous reference as it was
                if (any)
                    std::copy(std::begin(captured), std::end(captured), std::begin(m.reference));
                else
                    m.notice = S::Insufficient;

                m.noticeAt = nowMs;
                m.capturing = false;
            }
        }
    }

    restartBucket();
}

// ILION's own scale of the loss: 0 low, 1 moderate, 2 high, 3 very high, 4 critical; -1 gain
static int MfgCostLevel(double loss) { return loss < 0.0 ? -1 : std::min(4, (int) (loss / 10.0)); }

// The rows of the screen, in font heights: one place for its size and its drawing
struct MfgCostLayout
{
    static constexpr float padding = 0.35f, title = 0.95f, label = 0.78f, big = 1.35f, totalLine = 0.75f, figures = 2.5f,
                           change = 1.4f, graph = 1.8f, button = 1.25f, frame = 0.22f;

    static float Height()
    {
        return padding * 2.0f + title + 0.15f + label + 0.1f + figures + 0.2f + label + 0.1f + change + 0.2f + graph + 0.25f + button +
               frame * 2.0f;
    }
};

static ImVec2 MfgCostSize()
{
    const float fs = ImGui::GetFontSize();
    return ImVec2(FgCrtSize().x, std::floor(fs * MfgCostLayout::Height()));
}

struct MfgCostAreas
{
    ImRect change;    // the change and its label, for the tooltip
    ImVec2 buttonPos; // the capture key, placed by the caller
    ImVec2 buttonSize;
};

static MfgCostAreas DrawMfgCost(const ImVec2& min, const ImVec2& size)
{
    using L = MfgCostLayout;
    const auto& m = mfgCost;
    ImDrawList* draw = ImGui::GetWindowDrawList();
    ImFont* font = IlionTheme::crtFont != nullptr ? IlionTheme::crtFont : ImGui::GetFont();
    ImFont* bigFont = IlionTheme::crtTitleFont != nullptr ? IlionTheme::crtTitleFont : font;
    const float fs = ImGui::GetFontSize();
    const ImVec2 max(min.x + size.x, min.y + size.y);
    const auto col = [](float r, float g, float b, float a = 1.0f) { return ImGui::GetColorU32(ImVec4(r, g, b, a)); };
    const ImU32 window = col(0.08f, 0.07f, 0.05f), windowEdge = col(0.0f, 0.0f, 0.0f, 0.6f), soft = col(0.70f, 0.64f, 0.50f);
    const float windowRound = std::floor(fs * 0.2f);

    // Frame: dark brown case, a fine highlight, the aged amber face with faint scan lines
    const float rounding = std::floor(fs * 0.35f);
    const float frame = std::max(2.0f, std::floor(fs * L::frame));
    draw->AddRectFilled(min, max, col(0.20f, 0.15f, 0.09f), rounding);
    draw->AddRect(min, max, col(0.55f, 0.45f, 0.30f, 0.55f), rounding, 0, 1.0f);

    const ImVec2 fMin(min.x + frame, min.y + frame), fMax(max.x - frame, max.y - frame);
    draw->AddRectFilledMultiColor(fMin, fMax, col(0.88f, 0.70f, 0.28f), col(0.86f, 0.68f, 0.26f), col(0.66f, 0.49f, 0.17f),
                                  col(0.68f, 0.51f, 0.18f));
    for (float y = fMin.y + 2.0f; y < fMax.y; y += 3.0f)
        draw->AddLine(ImVec2(fMin.x, y), ImVec2(fMax.x, y), col(0.30f, 0.18f, 0.02f, 0.06f));
    draw->AddRect(fMin, fMax, col(0.0f, 0.0f, 0.0f, 0.35f), std::max(0.0f, rounding - frame), 0, 1.5f);

    const ImU32 ink = col(0.17f, 0.10f, 0.03f), inkSoft = col(0.23f, 0.14f, 0.04f);
    const float pad = std::floor(fs * 0.5f);
    const float x = fMin.x + pad, right = fMax.x - pad, width = right - x;
    float y = fMin.y + std::floor(fs * L::padding);

    // A text never wider than its room: its size goes down instead of being cut
    const auto fitted = [&](ImFont* f, float sizePx, const char* s, float room)
    {
        const float w = f->CalcTextSizeA(sizePx, FLT_MAX, 0.0f, s).x;
        return w > room && w > 0.0f ? std::floor(sizePx * room / w) : sizePx;
    };

    const auto darkWindow = [&](const ImVec2& a, const ImVec2& b)
    {
        draw->AddRectFilled(a, b, window, windowRound);
        draw->AddRect(a, b, windowEdge, windowRound, 0, 1.0f);
    };

    // Title and the multiplier in use
    const float titleSize = std::floor(fs * L::title);
    const std::string multiplier = m.generating && m.multiplier > 1 ? StrFmt("\xC3\x97%u", m.multiplier) : std::string();
    const float multiplierWidth = multiplier.empty() ? 0.0f : bigFont->CalcTextSizeA(titleSize, FLT_MAX, 0.0f, multiplier.c_str()).x;
    const char* title = FgCrtWord("ILION \xC2\xB7 MFG COST", "ILION \xC2\xB7 COSTE MFG");
    draw->AddText(bigFont, fitted(bigFont, titleSize, title, width - multiplierWidth - pad), ImVec2(x, y), ink, title);
    if (!multiplier.empty())
        draw->AddText(bigFont, titleSize, ImVec2(right - multiplierWidth, y), ink, multiplier.c_str());
    y += titleSize + std::floor(fs * 0.15f);

    // Average before / real FPS now, and the total presented below them, in a dark window
    const float labelSize = std::floor(fs * L::label);
    const char* figuresLabel = FgCrtWord("AVG BEFORE / FPS NOW", "AVG ANTES / FPS AHORA");
    draw->AddText(font, fitted(font, labelSize, figuresLabel, width), ImVec2(x, y), inkSoft, figuresLabel);
    y += labelSize + std::floor(fs * 0.1f);

    const double referenceFps = MfgCostReference();
    const std::string before = referenceFps > 0.0 ? StrFmt("%.0f", referenceFps) : std::string("---");
    const std::string now = !m.unmeasurable && m.current > 0.0 ? StrFmt("%.0f", m.current) : std::string("---");
    const std::string separator = " / ", unit = " FPS";

    const float figuresHeight = std::floor(fs * L::figures);
    darkWindow(ImVec2(x, y), ImVec2(right, y + figuresHeight));

    const float bigSize = fitted(bigFont, std::floor(fs * L::big), (before + separator + now + unit).c_str(), width - pad);
    float cursor = x + std::floor(pad * 0.5f);
    const float bigY = y + std::floor(fs * 0.15f);
    const std::pair<const std::string*, ImU32> parts[] = { { &before, col(0.40f, 1.00f, 0.45f) },
                                                           { &separator, soft },
                                                           { &now, col(1.00f, 0.95f, 0.78f) },
                                                           { &unit, soft } };
    for (const auto& [text, colour] : parts)
    {
        draw->AddText(bigFont, bigSize, ImVec2(cursor, bigY), colour, text->c_str());
        cursor += bigFont->CalcTextSizeA(bigSize, FLT_MAX, 0.0f, text->c_str()).x;
    }

    const std::string totalLabel = std::string(FgCrtWord("TOTAL FPS", "FPS TOTAL")) + " ";
    const std::string totalValue = m.total > 0.0 ? StrFmt("%.0f", m.total) : std::string("---");
    const float smallSize = fitted(font, std::floor(fs * L::totalLine), (totalLabel + totalValue).c_str(), width - pad);
    const float smallY = y + figuresHeight - smallSize - std::floor(fs * 0.15f);
    draw->AddText(font, smallSize, ImVec2(x + std::floor(pad * 0.5f), smallY), soft, totalLabel.c_str());
    draw->AddText(font, smallSize,
                  ImVec2(x + std::floor(pad * 0.5f) + font->CalcTextSizeA(smallSize, FLT_MAX, 0.0f, totalLabel.c_str()).x, smallY),
                  col(0.70f, 0.76f, 1.00f), totalValue.c_str());
    y += figuresHeight + std::floor(fs * 0.2f);

    // Current change, in a dark window so its colour reads on the amber
    const ImVec2 changeMin(x, y);
    const char* changeLabel = FgCrtWord("CURRENT CHANGE", "VARIACI\xC3\x93N ACTUAL");
    draw->AddText(font, fitted(font, labelSize, changeLabel, width), ImVec2(x, y), inkSoft, changeLabel);
    y += labelSize + std::floor(fs * 0.1f);

    const float changeHeight = std::floor(fs * L::change);
    const ImVec2 insetMin(x, y), insetMax(right, y + changeHeight);
    darkWindow(insetMin, insetMax);

    static int level = 0;
    std::string change;
    ImVec4 changeColour(0.62f, 0.60f, 0.52f, 1.0f);
    const double nowMs = Util::MillisecondsNow();

    if (m.capturing)
    {
        change = StrFmt(FgCrtWord("CAPTURING %d/10 s", "CAPTURANDO %d/10 s"), (int) std::min(10.0, m.captureMs / 1000.0));
    }
    else if (m.notice != MfgCostState::None && nowMs - m.noticeAt < 6000.0)
    {
        change = m.notice == MfgCostState::Canceled ? FgCrtWord("CAPTURE CANCELLED: FG ON", "CAPTURA CANCELADA: FG ACTIVO")
                                                    : FgCrtWord("NOT ENOUGH VALID DATA", "DATOS INSUFICIENTES");
    }
    else if (m.unmeasurable)
    {
        change = FgCrtWord("REAL FPS NOT MEASURABLE", "FPS REALES NO MEDIBLES");
    }
    else if (!m.generating)
    {
        change = referenceFps > 0.0 ? FgCrtWord("REFERENCE READY", "REFERENCIA LISTA") : FgCrtWord("NO REFERENCE", "SIN REFERENCIA");
    }
    else if (referenceFps <= 0.0)
    {
        change = FgCrtWord("TURN FG OFF AND CAPTURE 10 s OF PLAY", "DESACTIVA FG Y CAPTURA 10 s DE JUEGO");
    }
    else if (m.current <= 0.0)
    {
        change = FgCrtWord("MEASURING", "MIDIENDO");
    }
    else
    {
        // A level changes only once the loss is 0.75 points past its limit: no flicker at a limit, no delay elsewhere
        const double loss = (referenceFps - m.current) / referenceFps * 100.0;
        if (level < MfgCostLevel(loss - 0.75) || level > MfgCostLevel(loss + 0.75))
            level = MfgCostLevel(loss);

        static const ImVec4 colours[] = { ImVec4(0.40f, 1.00f, 0.42f, 1.0f), ImVec4(1.00f, 0.93f, 0.25f, 1.0f),
                                          ImVec4(1.00f, 0.62f, 0.15f, 1.0f), ImVec4(1.00f, 0.40f, 0.14f, 1.0f),
                                          ImVec4(1.00f, 0.16f, 0.12f, 1.0f) };
        static const char* const names[][2] = { { "LOW", "BAJO" },        { "MODERATE", "MODERADO" }, { "HIGH", "ALTO" },
                                                 { "VERY HIGH", "MUY ALTO" }, { "CRITICAL", "CR\xC3\x8DTICO" } };

        if (level < 0)
        {
            changeColour = ImVec4(0.35f, 0.85f, 1.00f, 1.0f);
            change = StrFmt("+%.0f %% \xC2\xB7 %s", std::fabs(loss), FgCrtWord("GAIN", "GANANCIA"));
        }
        else
        {
            changeColour = colours[level];
            change = StrFmt("-%.0f %% \xC2\xB7 %s", std::max(0.0, loss), FgCrtWord(names[level][0], names[level][1]));
        }
    }

    const float changeSize = fitted(bigFont, std::floor(fs * 1.15f), change.c_str(), width - pad);
    draw->AddText(bigFont, changeSize, ImVec2(insetMin.x + std::floor(pad * 0.5f), insetMin.y + std::floor((changeHeight - changeSize) * 0.5f)),
                  ImGui::GetColorU32(changeColour), change.c_str());
    y = insetMax.y + std::floor(fs * 0.2f);
    const ImRect changeArea(changeMin, insetMax);

    // Presentation frame time: the measured interval of every presented frame (generated ones included), last 120.
    // Its caption has its own line; the line is drawn only inside the plot below it, clipped there.
    const float graphHeight = std::floor(fs * L::graph);
    const ImVec2 gMin(x, y), gMax(right, y + graphHeight);
    darkWindow(gMin, gMax);

    const float inner = std::max(2.0f, std::floor(fs * 0.15f));
    const float captionSize = std::floor(fs * 0.65f);
    const ImVec2 plotMin(gMin.x + inner, gMin.y + inner + captionSize + inner), plotMax(gMax.x - inner, gMax.y - inner);

    constexpr size_t samples = 120;
    float sum = 0.0f, peak = 0.0f;
    int valid = 0;
    for (size_t i = plotWidth - samples; i < (size_t) plotWidth; i++)
    {
        const float v = gFrameTimes.At(i);
        if (v > 0.0f)
        {
            sum += v;
            peak = std::max(peak, v);
            valid++;
        }
    }

    if (valid < 30)
    {
        const char* none = FgCrtWord("NO DATA AVAILABLE", "DATOS NO DISPONIBLES");
        const float s = fitted(font, captionSize, none, width - inner * 2.0f);
        draw->AddText(font, s, ImVec2(gMin.x + inner * 2.0f, gMin.y + std::floor((graphHeight - s) * 0.5f)), soft, none);
    }
    else
    {
        const float average = sum / valid;
        const std::string caption = StrFmt("%s %.1f ms", FgCrtWord("PRESENTATION", "PRESENTACI\xC3\x93N"), average);
        draw->AddText(font, fitted(font, captionSize, caption.c_str(), width - inner * 4.0f), ImVec2(gMin.x + inner * 2.0f, gMin.y + inner),
                      col(0.85f, 0.78f, 0.60f, 0.90f), caption.c_str());

        // The scale: twice the average, or the highest peak when lower; higher peaks are clipped at the top
        const float top = std::max(average * 2.0f, std::min(peak, average * 4.0f));
        const float plotHeight = std::max(1.0f, plotMax.y - plotMin.y - 2.0f);
        ImVec2 points[samples];
        int n = 0;

        for (size_t i = 0; i < samples; i++)
        {
            const float raw = gFrameTimes.At(plotWidth - samples + i);
            if (raw <= 0.0f)
                continue; // no sample: nothing drawn for it

            const float v = std::min(top, raw);
            points[n++] = ImVec2(plotMin.x + (plotMax.x - plotMin.x) * i / (samples - 1), plotMax.y - 1.0f - plotHeight * (v / top));
        }

        draw->PushClipRect(plotMin, plotMax, true);
        draw->AddPolyline(points, n, col(0.40f, 1.00f, 0.45f), 0, 1.4f);
        draw->PopClipRect();
    }

    y = gMax.y + std::floor(fs * 0.25f);
    return { changeArea, ImVec2(x, y), ImVec2(width, std::floor(fs * L::button)) };
}

// ILION: XeMFG variants, each one in its own folder of plugins/XeMFG. The plugin loader loads only the chosen one (at
// the next start), so two copies never run together. Shown only when that folder has variants.
static void RenderXeMfgVariantSelector(decltype(Config::Instance()) config)
{
    const std::filesystem::path root = std::filesystem::path(config->PluginPath.value_or(L"plugins")) / L"XeMFG";

    struct Variant
    {
        std::wstring folder;
        std::string label;
    };

    // The folders are looked at again every few seconds, not every frame
    static std::vector<Variant> variants;
    static double lastScan = -100.0;

    if (ImGui::GetTime() - lastScan > 3.0)
    {
        lastScan = ImGui::GetTime();
        variants.clear();

        std::error_code ec;
        if (std::filesystem::is_directory(root, ec))
        {
            for (const auto& folder : std::filesystem::directory_iterator(root, ec))
            {
                if (!folder.is_directory())
                    continue;

                for (const auto& file : std::filesystem::directory_iterator(folder.path(), ec))
                {
                    std::wstring ext = file.path().extension().wstring();
                    std::transform(ext.begin(), ext.end(), ext.begin(), [](wchar_t c) { return std::towlower(c); });

                    if (!file.is_regular_file() || ext != L".asi")
                        continue;

                    const auto info = ReadXeMfgFile(file.path().wstring());
                    std::string label = wstring_to_string(folder.path().filename().wstring());

                    if (info.xemfg)
                        label += StrFmt("  (%s %s)", info.variant == "classic" ? Tr("classic") : info.variant.c_str(),
                                        info.version.c_str());

                    variants.push_back({ folder.path().filename().wstring(), label });
                    break;
                }
            }
        }
    }

    if (variants.empty())
        return;

    const std::wstring chosen = config->XeMfgVariant.value_or(L"");
    std::string preview = Tr("Plugins folder as it is");

    for (const auto& variant : variants)
    {
        if (_wcsicmp(variant.folder.c_str(), chosen.c_str()) == 0)
            preview = variant.label;
    }

    ImGui::Spacing();
    ImGui::BeginDisabled(!config->LoadAsiPlugins.value_or_default());
    ImGui::SetNextItemWidth(std::min(ImGui::GetFontSize() * 18.0f, ImGui::GetContentRegionAvail().x * 0.6f));

    static std::wstring startVariant = chosen;

    if (IlionTheme::BeginCombo(TrL("XeMFG variant"), preview.c_str()))
    {
        if (ImGui::Selectable(Tr("Plugins folder as it is"), chosen.empty()) && !chosen.empty())
        {
            config->XeMfgVariant = std::wstring();
            config->SaveXeMfgVariant();
        }

        for (const auto& variant : variants)
        {
            const bool selected = _wcsicmp(variant.folder.c_str(), chosen.c_str()) == 0;

            if (ImGui::Selectable(variant.label.c_str(), selected) && !selected)
            {
                config->XeMfgVariant = variant.folder;
                config->SaveXeMfgVariant();
            }
        }

        ImGui::EndCombo();
    }

    ImGui::EndDisabled();

    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        WrappedTooltip("%s", Tr("XeMFG variants in plugins\\XeMFG\\<name>\\. Only the chosen one is loaded at the next start, "
                                "and a plugin with the same file name in the plugins folder is then left out. Needs ASI "
                                "plugins loading (Compatibility)."));

    if (_wcsicmp(config->XeMfgVariant.value_or(L"").c_str(), startVariant.c_str()) != 0)
        TextWrappedColored(toneMapColor(IlionTheme::Themed(ImVec4(1.f, 0.15f, 0.1f, 1.f), IlionTheme::Role::Bad)), "%s",
                           Tr("Restart the game to load the chosen XeMFG variant"));
}

void MenuCommon::RenderFrameGenerationSelection(RenderMenuContext& ctx)
{
    auto& state = ctx.state;
    auto config = ctx.config;
    auto& menuResScale = ctx.menuResScale;
    auto& primaryGpu = *ctx.primaryGpu;

    /// FG INPUTS

    static std::vector<MenuOption<FGInput>> inputOptions;
    inputOptions.clear();

    // clang-format off

    inputOptions = {
        { FGInput::NoFG, "None" },
        { FGInput::Upscaler, "OptiFG (Upscaler)",
            "Upscaler must be enabled\n\nCan be used with any FG Output, but might be imperfect with some\nTo prevent UI glitching, HUDfix required" },
        { FGInput::DLSSG, "DLSSG via Streamline",
            "Can be used with any FG Output\n\nRequires enabling DLSS-FG in game settings\nSupports HUDless out of the box\n\nLimited to games that use Streamline" },
        { FGInput::NvngxFG, "DLSSG via Nvngx",
            "Limited to variants of FSR FG\n\nRequires enabling DLSS-FG in game settings\nSupports HUDless out of the box\nUses Streamline swapchain for pacing" },
        { FGInput::FSRFG, "FSR 3.1 FG",
            "Can be used with any FG Output\n\nRequires enabling FSR-FG in game settings\nSupports HUDless out of the box" },
        { FGInput::FSRFG30, "FSR 3.0 FG",
            "Can be used with any FG Output\n\nRequires enabling FSR-FG in game settings\nSupports HUDless out of the box" },
        { FGInput::XeFG, "XeFG" }
    };

    // clang-format on

    auto constexpr nvngxInputIndex = (uint32_t) FGInput::NvngxFG;

    // XeFG input requirements
    auto constexpr xefgInputIndex = (uint32_t) FGInput::XeFG;
    inputOptions[xefgInputIndex].set_disabled(true, "Support not implemented, they meant FG Output");

    // OptiFG requirements
    auto constexpr optiFgIndex = (uint32_t) FGInput::Upscaler;
    inputOptions[optiFgIndex].set_disabled(state.swapchainApi == API::Vulkan, "Unsupported API");

    if (!inputOptions[optiFgIndex].disabled && state.activeFgOutput == FGOutput::FSRFG && !FfxApiProxy::IsFGReady() &&
        !ffxInitTried)
    {
        ffxInitTried = true;
        FfxApiProxy::InitFfxDx12();
        inputOptions[optiFgIndex].set_disabled(!FfxApiProxy::IsFGReady(), "amd_fidelityfx_dx12.dll is missing");
    }
    else if (!inputOptions[optiFgIndex].disabled && state.activeFgOutput == FGOutput::XeFG && !xefgInitTried &&
             XeFGProxy::Module() == nullptr)
    {
        xefgInitTried = true;
        XeFGProxy::InitXeFG();
        inputOptions[optiFgIndex].set_disabled(XeFGProxy::Module() == nullptr, "libxess_fg.dll is missing");
    }

    // DLSSG inputs requirements
    auto constexpr dlssgInputIndex = (uint32_t) FGInput::DLSSG;
    // inputOptions[dlssgInputIndex].set_disabled(state.streamlineVersion.major == 0, "Game doesn't use streamline");
    inputOptions[dlssgInputIndex].set_disabled(state.swapchainApi == API::DX11, "Unsupported API");

    // FSRFG inputs requirements
    auto constexpr fsrfgInputIndex = (uint32_t) FGInput::FSRFG;
    inputOptions[fsrfgInputIndex].set_disabled(state.swapchainApi != API::DX12, "Unsupported API");

    // FSRFG30 inputs requirements
    auto constexpr fsrfg30InputIndex = (uint32_t) FGInput::FSRFG30;
    inputOptions[fsrfg30InputIndex].set_disabled(state.swapchainApi != API::DX12, "Unsupported API");

    if (!config->FGInput.has_value())
        config->FGInput = config->FGInput.value_or_default(); // need to have a value before combo

    /// FG OUTPUTS

    static std::vector<MenuOption<FGOutput>> outputOptions;
    outputOptions.clear();

    // clang-format off

    outputOptions = {
        { FGOutput::NoFG, "None" },
        { FGOutput::FSRFG, "FSR FG", "FSR3/4-FG, RDNA4 autoupgrades to FSR4-FG\n\nFSR4-FG sometimes better/worse than XeFG" },
        { FGOutput::DLSSG, "DLSSG", "DLSSG output\nCan be used in conjuction with Nukem's for example" },
        { FGOutput::XeFG, "XeFG", "XeFG - heaviest, but best universal FG\n\nXeFG 3 overall deals best with HUD\n\nEnable UI Composition if HUD ghosting" },
        { FGOutput::Reprojection, "Reprojection (WIP)", "Reprojects the game image using new mouse data\nKinda like Reflex 2, perceived latency improvement\n\n- REQUIRES DLSSG VIA STREAMLINE AS INPUT\n- Only works with first person perspective games\n- Only mouse, no controller\n- If possible, disable any mouse/camera smoothing in the game\n" },
    };

    // clang-format on

    // DLSSG output requirements
    auto constexpr dlssgOutputIndex = (uint32_t) FGOutput::DLSSG;
    const bool maySupportDlssg = primaryGpu.vendorId == VendorId::Nvidia;
    const bool hasDlssgReplacement =
        state.nukemsFgFileAvailable || state.artursFgFileAvailable || FfxApiProxy::IsFGReady(false);

    if (!maySupportDlssg && hasDlssgReplacement)
    {
        outputOptions[dlssgOutputIndex].tooltip =
            "No real DLSSG, unsupported hardware\nOnly Nvngx FG replacements available";
    }

    outputOptions[dlssgOutputIndex].set_disabled(state.swapchainApi == API::Vulkan, "Unsupported API");
    outputOptions[dlssgOutputIndex].set_disabled(!maySupportDlssg && !hasDlssgReplacement,
                                                 "Unsupported hardware and no replacements");

    // For that one case of DX11 DLSSG
    const auto streamlineVersion = state.streamlineVersion;
    const bool nukemsUnsupportedApi =
        state.swapchainApi == API::DX11 &&
        (streamlineVersion == feature_version { 0, 0, 0 } || streamlineVersion > feature_version { 2, 0, 1 });
    inputOptions[nvngxInputIndex].set_disabled(nukemsUnsupportedApi, "Unsupported API");

    // FSR FG output requirements
    auto constexpr fsrfgOutputIndex = (uint32_t) FGOutput::FSRFG;
    outputOptions[fsrfgOutputIndex].set_disabled(state.swapchainApi == API::Vulkan, "Unsupported API");

    // XeFG output requirements
    auto constexpr xefgOutputIndex = (uint32_t) FGOutput::XeFG;
    outputOptions[xefgOutputIndex].set_disabled(state.swapchainApi == API::Vulkan, "Unsupported API");
    // Unsupported FG input selected
    const auto currentInputIndex = (uint32_t) state.activeFgInput;
    if (config->FGInput != FGInput::NoFG && inputOptions.size() > currentInputIndex &&
        inputOptions[currentInputIndex].disabled && state.activeFgInput == config->FGInput)
    {
        LOG_WARN("Resetting FGInput to NoFG: {}", inputOptions[currentInputIndex].label);
        config->FGInput = FGInput::NoFG;

        // Changing active can be dangerous but we are talking about an unsupported mode
        // which shouldn't even actually have taken affect
        state.activeFgInput = FGInput::NoFG;
    }

    // Unsupported FG output selected
    const auto currentOutputIndex = (uint32_t) state.activeFgOutput;
    if (config->FGOutput != FGOutput::NoFG && outputOptions.size() > currentOutputIndex &&
        outputOptions[currentOutputIndex].disabled && state.activeFgOutput == config->FGOutput)
    {
        LOG_WARN("Resetting FGOutput to NoFG: {}", outputOptions[currentOutputIndex].label);
        config->FGOutput = FGOutput::NoFG;
        state.activeFgOutput = FGOutput::NoFG;
    }

    if (!config->FGOutput.has_value())
        config->FGOutput = config->FGOutput.value_or_default(); // need to have a value before combo

    /// FG NVNGX REPLACEMENT

    static std::vector<MenuOption<FGNvngxReplacement>> nvngxOptions;
    nvngxOptions.clear();

    // clang-format off

    nvngxOptions = {
        { FGNvngxReplacement::None, "None (Real DLSSG)", "Real DLSSG, For RTX 40xx and above"},
        { FGNvngxReplacement::Nukems, "Nukem's", "FSR 3 FG" },
        { FGNvngxReplacement::Arturs, "Enabler", "FSR 3 MFG mod" },
        { FGNvngxReplacement::FFX, "FSR 3/4 FG", "FSR 3/4 FG using the FFX upgrade\n\n"
                                                 "Partially based on Nukems, uses SL swapchain\n"
                                                 "Possibly better performance and frame pacing compared to FSR-FG output"},
        { FGNvngxReplacement::Combo, "FFX + Enabler", "Use if FSR4-FG is supported, otherwise stick to Enabler\n\n"
                                                      "FFX used for the middle fake frame, Enabler for the rest\n\n"
                                                      "2x - FFX\n3x - Enabler\n4x - FFX + Enabler\n5x - Enabler\n6x - FFX + Enabler\n\n"
                                                      "Due to pacing, only odd number of fake frames are able to use FFX"},
    };

    // clang-format on

    bool replaceFgOutputWithNvngx = false;
    bool showNvngxFgDowndown = false;

    if (config->FGInput == FGInput::NvngxFG)
    {
        config->FGOutput = FGOutput::NoFG;
        replaceFgOutputWithNvngx = true;
    }
    else if (config->FGOutput == FGOutput::DLSSG)
    {
        showNvngxFgDowndown = true;
    }

    auto constexpr fgNvngxNoneIndex = (uint32_t) FGNvngxReplacement::None;
    nvngxOptions[fgNvngxNoneIndex].set_disabled(!maySupportDlssg, "Unsupported hardware");

    if (replaceFgOutputWithNvngx)
    {
        nvngxOptions[fgNvngxNoneIndex].label = "None";
        nvngxOptions[fgNvngxNoneIndex].set_hidden(true);
    }

    auto constexpr fgNvngxNukemsIndex = (uint32_t) FGNvngxReplacement::Nukems;
    nvngxOptions[fgNvngxNukemsIndex].set_disabled(!state.nukemsFgFileAvailable,
                                                  "Missing dlssg_to_fsr3_amd_is_better.dll");

    auto constexpr fgNvngxArtursIndex = (uint32_t) FGNvngxReplacement::Arturs;
    nvngxOptions[fgNvngxArtursIndex].set_disabled(!state.artursFgFileAvailable, "Missing dlss-enabler-headless.dll");

    auto constexpr fgNvngxFfxIndex = (uint32_t) FGNvngxReplacement::FFX;
    nvngxOptions[fgNvngxFfxIndex].set_disabled(state.swapchainApi == API::Vulkan, "Unsupported API");
    nvngxOptions[fgNvngxFfxIndex].set_disabled(!FfxApiProxy::IsFGReady(false),
                                               "Missing amd_fidelityfx_framegeneration_dx12.dll");

    auto constexpr fgNvngxComboIndex = (uint32_t) FGNvngxReplacement::Combo;
    nvngxOptions[fgNvngxComboIndex].set_disabled(state.swapchainApi == API::Vulkan, "Unsupported API");
    nvngxOptions[fgNvngxComboIndex].set_disabled(
        !FfxApiProxy::IsFGReady(false) || !state.artursFgFileAvailable,
        "Missing amd_fidelityfx_framegeneration_dx12.dll\nor missing dlss-enabler-headless.dll");

    // TODO: Automatically switch to any other option

    if (!config->FGNvngxReplacement.has_value())
        config->FGNvngxReplacement = config->FGNvngxReplacement.value_or_default(); // need to have a value before combo

    if (state.activeFgInput != FGInput::ForceXeLL)
    {
        SectionTitle("Frame Generation");
        ShowTooltip("Which copium do you choose?");

        // ILION: the FG screen next to the input and output (to their right when there is room, else centred below)
        const ImVec2 crtSize = FgCrtSize();
        const float crtGap = ImGui::GetStyle().ItemSpacing.x * 2.0f;
        const float selectionAvail = ImGui::GetContentRegionAvail().x;
        // The selectors keep room for their widest label and a combo long enough for the names of the technologies
        float selectorLabel = 0.0f;
        for (const char* label : { "FG Input", "FG Output", "FG Nvngx", "FG Nvngx Replacement" })
            selectorLabel = std::max(selectorLabel, ImGui::CalcTextSize(Tr(label)).x);
        selectorLabel += ImGui::GetStyle().ItemInnerSpacing.x * 2.0f;

        // Basic keeps it beside the selectors when there is room; Essential and Advanced put it below them, its left
        // edge on the selectors' left edge (Frame Gen, Personal and any tab that shows this box)
        const bool crtBeside = config->MenuBasicMode.value_or_default() &&
                               selectionAvail >= crtSize.x + crtGap + selectorLabel + ImGui::GetFontSize() * 14.0f;
        const ImVec2 selectionStart = ImGui::GetCursorScreenPos();
        const float selectionWidth = crtBeside ? selectionAvail - crtSize.x - crtGap : 0.0f;

        if (ImGui::BeginTable("fgSelection", 1, ImGuiTableFlags_SizingStretchSame, ImVec2(selectionWidth, 0.0f)))
        {
            ImGui::TableNextColumn();

            // The combo fills its column minus its label: neither the names nor the label are cut
            ImGui::SetNextItemWidth(std::max(ImGui::GetFontSize() * 8.0f, ImGui::GetContentRegionAvail().x - selectorLabel));
            PopulateCombo(TrL("FG Input"), config->FGInput, inputOptions);
            ShowTooltip("The data source to be used for FG\n"
                        "The native FG which the game supports");

            ImGui::TableNextColumn();

            if (replaceFgOutputWithNvngx)
            {
                // Disable None?
                // The combo fills its column minus its label: neither the names nor the label are cut
                ImGui::SetNextItemWidth(std::max(ImGui::GetFontSize() * 8.0f, ImGui::GetContentRegionAvail().x - selectorLabel));
                PopulateCombo(TrL("FG Nvngx"), config->FGNvngxReplacement, nvngxOptions);
                ShowTooltip("What backend to use instead of the real DLSSG");
            }
            else
            {
                // The combo fills its column minus its label: neither the names nor the label are cut
                ImGui::SetNextItemWidth(std::max(ImGui::GetFontSize() * 8.0f, ImGui::GetContentRegionAvail().x - selectorLabel));
                PopulateCombo(TrL("FG Output"), config->FGOutput, outputOptions);
                ShowTooltip("The FG that you will actually be using");
            }

            if (showNvngxFgDowndown)
            {
                ImGui::TableNextColumn();

                // The combo fills its column minus its label: neither the names nor the label are cut
                ImGui::SetNextItemWidth(std::max(ImGui::GetFontSize() * 8.0f, ImGui::GetContentRegionAvail().x - selectorLabel));
                PopulateCombo(TrL("FG Nvngx Replacement"), config->FGNvngxReplacement, nvngxOptions);
                ShowTooltip("What backend to use instead of the real DLSSG");
            }

            ImGui::EndTable();
        }

        {
            const FgCrtData crt = GatherFgCrt(state);
            const float selectionBottom = ImGui::GetCursorScreenPos().y;
            ImVec2 crtPos;

            if (crtBeside)
            {
                // In the space right of the selectors, level with them (centred on them when they are taller)
                const float selectionHeight = selectionBottom - selectionStart.y;
                crtPos = ImVec2(std::floor(selectionStart.x + selectionWidth + crtGap),
                                std::floor(selectionStart.y + std::max(0.0f, (selectionHeight - crtSize.y) * 0.5f)));
            }
            else
            {
                crtPos = ImVec2(config->MenuBasicMode.value_or_default() ? std::floor(selectionStart.x + (selectionAvail - crtSize.x) * 0.5f)
                                                                         : selectionStart.x,
                                selectionBottom + ImGui::GetStyle().ItemSpacing.y);
            }

            DrawFgCrt(crtPos, crtSize, crt);

            // The screen takes its place in the layout and shows the tooltip
            ImGui::SetCursorScreenPos(crtPos);
            ImGui::InvisibleButton("##fgCrt", crtSize);
            if (ImGui::IsItemHovered())
                FgCrtTooltip(crt);

            // ILION: the MFG cost screen under it while a FG is selected or running (its capture key is used with FG off)
            float screensBottom = crtPos.y + crtSize.y;
            if (crt.status >= 1 || mfgCost.engaged)
            {
                const ImVec2 costSize = MfgCostSize();
                const ImVec2 costPos(crtPos.x, std::floor(screensBottom + ImGui::GetFontSize() * 0.25f));
                const MfgCostAreas areas = DrawMfgCost(costPos, costSize);

                if (ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(areas.change.Min, areas.change.Max))
                    WrappedTooltip("%s\n\n%s: %s", Tr("Compares the current real FPS with the average recorded before Multi Frame Generation was switched on. "
                                                    "The percentage changes dynamically during play. Changes can also come from a different graphics "
                                                    "load and don't necessarily show the cost of MFG alone."),
                                   Tr("Real frames counted by"), MfgCostSourceName(mfgCost.source));

                ImGui::SetCursorScreenPos(areas.buttonPos);
                ImGui::BeginDisabled(mfgCost.generating || mfgCost.capturing);
                const std::string key = mfgCost.capturing
                                            ? StrFmt("%s %d/10 s###mfgCapture", Tr("Capturing"), (int) std::min(10.0, mfgCost.captureMs / 1000.0))
                                            : std::string(Tr("Capture reference")) + "###mfgCapture";
                if (IlionTheme::Button(key.c_str(), areas.buttonSize))
                    StartMfgCostCapture();
                ImGui::EndDisabled();

                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                    WrappedTooltip("%s", Tr("Turn FG off, stand in a real scene of the game and press: ILION measures the real FPS for 10 s and "
                                            "keeps their average as the reference for the comparison."));

                screensBottom = costPos.y + costSize.y;
            }

            ImGui::SetCursorScreenPos(ImVec2(selectionStart.x, std::max(selectionBottom, screensBottom + ImGui::GetStyle().ItemSpacing.y)));
            ImGui::Dummy(ImVec2(0.0f, 0.0f));
        }

        // ILION: the XeMFG variant to load at the next start, when plugins/XeMFG holds variants; only with XeFG selected,
        // the route XeMFG uses (another generator hides it, the choice stays in the config)
        if (config->FGOutput.value_or_default() == FGOutput::XeFG)
            RenderXeMfgVariantSelector(config);

        // Try to avoid having None selected when the gpu doesn't support DLSSG + some fallbacks
        if (!maySupportDlssg && (replaceFgOutputWithNvngx || showNvngxFgDowndown) &&
            config->FGNvngxReplacement.value_or_default() == FGNvngxReplacement::None)
        {
            if (state.nukemsFgFileAvailable)
                config->FGNvngxReplacement.set_volatile_value(FGNvngxReplacement::Nukems);

            else if (state.artursFgFileAvailable)
                config->FGNvngxReplacement.set_volatile_value(FGNvngxReplacement::Arturs);

            else if (FfxApiProxy::IsFGReady(false))
                config->FGNvngxReplacement.set_volatile_value(FGNvngxReplacement::FFX);
        }

        const bool nvngxFgChanged = (replaceFgOutputWithNvngx || showNvngxFgDowndown) &&
                                    state.activeFgNvngx != config->FGNvngxReplacement.value_or_default();
        state.fgSettingsChanged = state.activeFgOutput != config->FGOutput.value_or_default() ||
                                  state.activeFgInput != config->FGInput.value_or_default() || nvngxFgChanged;

        if (state.fgSettingsChanged)
        {
            ImGui::Spacing();
            TextWrappedColored(toneMapColor(ImVec4(1.f, 0.f, 0.0f, 1.f)),
                               Tr("Save Settings and restart to apply the changes"));
            ImGui::Spacing();
        }

        const bool dlssgInputOrOutput =
            state.activeFgOutput == FGOutput::DLSSG || state.activeFgInput == FGInput::DLSSG;

        ImGui::BeginDisabled(state.dlssgGameDMFGSupported && config->FGDLSSGOverrideForceDMFG.value_or_default());
        if (state.dlssgMfgMax.has_value() && state.dlssgMfgMax.value() >= 1 && !dlssgInputOrOutput)
        {
            auto maxInterpolationCount = state.dlssgMfgMax.value();

            if (maxInterpolationCount >= 1)
            {
                // Map config value to UI index
                int currentSet = 0;
                if (config->FGDLSSGOverrideInterpolationCount.has_value())
                {
                    currentSet = config->FGDLSSGOverrideInterpolationCount.value() + 1;
                }

                std::string currentIntCountStr;
                if (currentSet == 0)
                    currentIntCountStr = "Default";
                else if (currentSet == 1)
                    currentIntCountStr = "Off";
                else
                    currentIntCountStr = std::to_string(currentSet) + "X";

                ImGui::PushItemWidth(95.0f * menuResScale);

                if (IlionTheme::BeginCombo(TrL("Override DLSSG Ratio"), currentIntCountStr.c_str()))
                {
                    for (int i = 0; i <= maxInterpolationCount + 1; i++)
                    {
                        std::string modeStr;
                        if (i == 0)
                            modeStr = "Default";
                        else if (i == 1)
                            modeStr = "Off";
                        else
                            modeStr = std::to_string(i) + "X";

                        if (ImGui::Selectable(modeStr.c_str(), (currentSet == i)))
                        {
                            if (i == 0)
                            {
                                // Default, no override
                                config->FGDLSSGOverrideInterpolationCount.reset();
                            }
                            else
                            {
                                // UI index, store value
                                int framesToGenerate = i - 1;

                                LOG_DEBUG("DLSSG Interpolation Count set to: {}", framesToGenerate);
                                config->FGDLSSGOverrideInterpolationCount = framesToGenerate;
                            }

                            StreamlineHooks::updateDlssgOptions();
                        }
                    }

                    ImGui::EndCombo();
                }

                ImGui::PopItemWidth();
            }
        }

        ImGui::EndDisabled();

        // Input, output and the multiplier are all the Basic Mode shows of this section
        if (basicView)
            return;

        if (state.dlssgGameDMFGSupported && !dlssgInputOrOutput)
        {
            ImGui::SameLine(0.0f, 16.0f);

            if (bool dynamicMFG = config->FGDLSSGOverrideForceDMFG.value_or_default();
                IlionTheme::Checkbox(TrL("Force Dynamic MFG"), &dynamicMFG))
            {
                config->FGDLSSGOverrideForceDMFG = dynamicMFG;
                StreamlineHooks::updateDlssgOptions();
            }

            ImGui::BeginDisabled(state.dlssgLastSetMode != sl::DLSSGMode::eDynamic);
            static float fpsTarget = config->FGDLSSGFramerateTargetDMFG.value_or_default();
            IlionTheme::SliderFloat(TrL("DMFG FPS Target"), &fpsTarget, 0, 200, "%.0f");

            ShowTooltip("An active limit of 0 means auto-detect the display refresh rate");

            if (IlionTheme::Button(TrL("Apply Target")))
            {
                config->FGDLSSGFramerateTargetDMFG = fpsTarget;
                StreamlineHooks::updateDlssgOptions();
            }

            SameLineIfRoom(ButtonWidth(TrL("Reset Target")), 16.0f);

            if (IlionTheme::Button(TrL("Reset Target")))
            {
                fpsTarget = 0.0f;
                config->FGDLSSGFramerateTargetDMFG.reset();
            }

            ImGui::EndDisabled();
        }

        // The detected UI view and the UI drawing options are for Advanced
        auto fgOutput = reinterpret_cast<IFGFeature_Dx12*>(state.currentFG);
        if ((state.activeFgOutput != FGOutput::Reprojection && state.activeFgOutput != FGOutput::NoFG &&
             state.activeFgInput != FGInput::NoFG && state.activeFgInput != FGInput::NvngxFG) &&
            fgOutput && !EssentialView())
        {
            IlionTheme::Checkbox(TrL("Show Detected UI"), &state.fgHudlessCompare);
            ShowTooltip("Needs HUDless texture to compare with final image.\n"
                        "UI elements and ONLY UI elements should have a pink tint!");

            const auto isUsingUIAny = fgOutput->IsUsingUIAny();

            ImGui::BeginDisabled(!isUsingUIAny);

            if (bool drawUIOverFG = config->FGDrawUIOverFG.value_or_default();
                IlionTheme::Checkbox(TrL("Draw UI over"), &drawUIOverFG))
            {
                config->FGDrawUIOverFG = drawUIOverFG;
            }
            ShowTooltip("Draws UI resource over the final image\n"
                        "If no UI visible, enable this!");

            ImGui::EndDisabled();

            ImGui::SameLine(0.0f, 16.0f);

            ImGui::BeginDisabled(!isUsingUIAny || !config->FGDrawUIOverFG.value_or_default());

            if (bool uiPremultipliedAlpha = config->FGUIPremultipliedAlpha.value_or_default();
                IlionTheme::Checkbox(TrL("UI Premult. alpha"), &uiPremultipliedAlpha))
            {
                config->FGUIPremultipliedAlpha = uiPremultipliedAlpha;
            }
            ShowTooltip("If UI is too faint, disable this option");

            ImGui::EndDisabled();
        }

        const bool showOutputSpecificFGSettings = state.activeFgInput == FGInput::DLSSG ||
                                                  state.activeFgInput == FGInput::FSRFG ||
                                                  state.activeFgInput == FGInput::FSRFG30;

        const bool showHudCutoff = state.activeFgInput == FGInput::NvngxFG || state.activeFgOutput == FGOutput::FSRFG;

        if (showOutputSpecificFGSettings || showHudCutoff)
        {
            if (auto ch = ScopedCollapsingHeader(TrL("Advanced FG Settings")); ch.IsHeaderOpen())
            {
                ScopedIndent indent {};
                ImGui::Spacing();

                if (showOutputSpecificFGSettings)
                {
                    auto fgOutput = reinterpret_cast<IFGFeature_Dx12*>(state.currentFG);
                    if (fgOutput)
                    {
                        ImGui::BeginDisabled(!fgOutput->IsActive());

                        const auto isUsingUIAny = fgOutput->IsUsingUIAny();
                        const auto isUsingHudlessAny = fgOutput->IsUsingHudlessAny();

                        bool disableUI = config->FGDisableUI.value_or_default();
                        ImGui::BeginDisabled(!isUsingUIAny && !disableUI);

                        if (IlionTheme::Checkbox(TrL("Disable UI texture"), &disableUI))
                        {
                            config->FGDisableUI = disableUI;
                            fgOutput->UpdateTarget();
                        }

                        ShowTooltip("For when the game sends a UI texture, but you want to disable it");

                        ImGui::EndDisabled();

                        SameLineIfRoom(CheckboxWidth(TrL("Disable HUDless")), 16.0f);

                        bool disableHudless = config->FGDisableHudless.value_or_default();
                        ImGui::BeginDisabled(!isUsingHudlessAny && !disableHudless);

                        if (IlionTheme::Checkbox(TrL("Disable HUDless"), &disableHudless))
                        {
                            config->FGDisableHudless = disableHudless;
                        }

                        ShowTooltip("For when the game sends HUDless, but you want to disable it");

                        ImGui::EndDisabled();

                        bool depthValidNow = config->FGDepthValidNow.value_or_default();
                        if (IlionTheme::Checkbox(TrL("Depth as ValidNow"), &depthValidNow))
                            config->FGDepthValidNow = depthValidNow;

                        ShowTooltip("Will use more VRAM, but Uniscaler needs this\n"
                                    "Maybe some other games might need too");

                        SameLineIfRoom(CheckboxWidth(TrL("Velocity as ValidNow")), 16.0f);

                        bool velocityValidNow = config->FGVelocityValidNow.value_or_default();
                        if (IlionTheme::Checkbox(TrL("Velocity as ValidNow"), &velocityValidNow))
                            config->FGVelocityValidNow = velocityValidNow;

                        ShowTooltip("Will use more VRAM, but Uniscaler needs this\n"
                                    "Maybe some other games might need too");

                        bool hudlessValidNow = config->FGHudlessValidNow.value_or_default();
                        if (IlionTheme::Checkbox(TrL("HUDless as ValidNow"), &hudlessValidNow))
                            config->FGHudlessValidNow = hudlessValidNow;

                        ShowTooltip("Will use more VRAM, but some games might need this");

                        SameLineIfRoom(CheckboxWidth(TrL("Accept First HUDless")), 16.0f);

                        bool firstHudless = config->FGOnlyAcceptFirstHudless.value_or_default();
                        if (IlionTheme::Checkbox(TrL("Accept First HUDless"), &firstHudless))
                            config->FGOnlyAcceptFirstHudless = firstHudless;

                        ShowTooltip("If source tags more than one HUDless, only use the first one");

                        if (bool skipReset = config->FGSkipReset.value_or_default();
                            IlionTheme::Checkbox(TrL("Skip Reset"), &skipReset))
                        {
                            config->FGSkipReset = skipReset;
                        }

                        ShowTooltip("Don't use reset signals from FG Inputs");

                        ImGui::EndDisabled();

                        ImGui::PushItemWidth(80.0f * menuResScale);

                        auto frameAhead = config->FGAllowedFrameAhead.value_or_default();
                        if (ImGui::InputInt(TrL("Frame Ahead"), &frameAhead, 1, 1) && frameAhead > 0 && frameAhead < 4)
                        {
                            config->FGAllowedFrameAhead = frameAhead;
                        }

                        ShowTooltip("Number of frames the FG is allowed to be ahead of the game\n"
                                    "Might prevent FG on/off switching, but also might cause issues");

                        ImGui::PopItemWidth();

                        ImGui::SameLine(0.0f, 16.0f);

                        const char* ftSources[] = { Tr("Input"), Tr("Opti"), Tr("Zero") };
                        const char* ftSourceInfos[] = { Tr("Uses frametimes provided by\nDLSSG or FSR-FG "),
                                                        Tr("Uses frametimes calculated by Opti"),
                                                        Tr("Let XeFG to handle frametimes") };

                        auto currentSet = (int) config->FTInput.value_or_default();
                        auto currentSourceCount = state.activeFgOutput == FGOutput::XeFG ? 3 : 2;

                        ImGui::PushItemWidth(95.0f * menuResScale);

                        if (IlionTheme::BeginCombo(TrL("FT Input"), ftSources[currentSet]))
                        {
                            for (size_t i = 0; i < currentSourceCount; i++)
                            {

                                if (ImGui::Selectable(ftSources[i], currentSet == i))
                                {
                                    LOG_DEBUG("FTInput has changed {} -> {}", ftSources[currentSet], ftSources[i]);
                                    config->FTInput = (FrameTimeSource) i;
                                }

                                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                                    WrappedTooltip(ftSourceInfos[i]);
                            }

                            ImGui::EndCombo();
                        }

                        ImGui::PopItemWidth();

                        ShowTooltip("Select source for frametime\n"
                                    "Might help frame pacing and stutter issues");
                    }
                }

                if (showHudCutoff)
                {
                    float fgHudCutoff = config->FGHudCutoff.value_or_default();
                    if (IlionTheme::SliderFloat(TrL("Hud Cutoff"), &fgHudCutoff, 0.00f, 1.0f, "%.2f"))
                        config->FGHudCutoff = fgHudCutoff;

                    ShowTooltip("Cutoffs transparency from UI to help with interpolation\n"
                                "You can use Show Detected UI to see the difference\n0.0 is auto");
                }
            }
        }
    }
}

// Replaces the Active checkbox + MFG combo of FG outputs
// maxInterpolationCount of 0 means the output doesn't generate frames, "On" is shown instead of "2X"
void MenuCommon::RenderFGModeCombo(RenderMenuContext& ctx, const char* label, int maxInterpolationCount,
                                   CustomOptional<int>* countConfig)
{
    auto& state = ctx.state;
    auto config = ctx.config;

    // -1 = Default (follow game), 0 = Off, >0 = interpolated frame count
    auto modeName = [&](int mode) -> std::string
    {
        if (mode < 0)
            return Tr("Default");

        if (mode == 0)
            return Tr("Off");

        if (maxInterpolationCount == 0)
            return Tr("On");

        return std::format("{}X", mode + 1);
    };

    const int maxMode = std::max(maxInterpolationCount, 1);

    int current = 0;
    if (config->FGFollowsGame())
        current = -1;
    else if (config->FGEnabled.value_or_default())
        current = countConfig != nullptr ? std::clamp(countConfig->value_or_default(), 1, maxMode) : 1;

    std::string preview = modeName(current);
    if (current < 0)
    {
        const int gameMode =
            state.dlssgInputGameEnabled ? std::min(state.dlssgInputGameInterpolationCount, maxMode) : 0;
        preview += std::format(" ({})", modeName(gameMode));
    }

    ImGui::PushItemWidth(115.0f * ctx.menuResScale);

    if (IlionTheme::BeginCombo(label, preview.c_str()))
    {
        const int firstMode = Config::FGCanFollowGame() ? -1 : 0;

        for (int mode = firstMode; mode <= maxMode; mode++)
        {
            if (!ImGui::Selectable(modeName(mode).c_str(), current == mode) || current == mode)
                continue;

            fgHotkeyLeftDefault = false;

            if (mode < 0)
            {
                config->FGEnabled = std::optional<bool> {};
            }
            else
            {
                config->FGEnabled = mode > 0;

                if (mode > 0 && countConfig != nullptr)
                    *countConfig = mode;
            }

            LOG_DEBUG("FG mode set to: {}", modeName(mode));

            if (config->IsFGEnabled())
                state.fgChanged = true;
        }

        ImGui::EndCombo();
    }

    ImGui::PopItemWidth();

    if (Config::FGCanFollowGame())
        ShowTooltip("Frame Generation mode\nDefault follows the game's DLSS FG setting");
    else
        ShowTooltip("Frame Generation mode");
}

void MenuCommon::RenderFrameGenerationRuntimeSettings(RenderMenuContext& ctx)
{
    auto& state = ctx.state;
    auto config = ctx.config;
    auto& currentFeature = ctx.currentFeature;
    auto& menuResScale = ctx.menuResScale;
    auto& primaryGpu = *ctx.primaryGpu;
    auto fgOutput = state.currentFG;

    const UiTargetMode uiTargetMode = getUiTargetMode();
    const bool outputIsHdr = uiTargetMode != UiTargetMode::SDR;

    // FSR FG controls
    if (ScopedCard::Shows("fg_fsrfg") && state.activeFgOutput == FGOutput::FSRFG &&
        state.activeFgInput != FGInput::NoFG && state.currentFGSwapchain != nullptr)
    {
        if (state.activeFgInput != FGInput::Upscaler ||
            (currentFeature != nullptr && !currentFeature->IsFrozen()) && FfxApiProxy::IsFGReady())
        {
            ScopedCard card { "FSR FG Settings" };

            if (_ffxFGIndex < 0)
                _ffxFGIndex = config->FfxFGIndex.value_or_default();

            if (state.ffxFGVersionNames.size() > 0 && !basicView)
            {
                ImGui::PushItemWidth(115.0f * menuResScale);

                auto currentName = StrFmt("FSR %s", state.ffxFGVersionNames[_ffxFGIndex]);
                if (IlionTheme::BeginCombo(TrL("FFX FG Version"), currentName.c_str()))
                {
                    for (int n = 0; n < state.ffxFGVersionIds.size(); n++)
                    {
                        auto name = StrFmt("FSR %s", state.ffxFGVersionNames[n]);
                        if (ImGui::Selectable(name.c_str(), config->FfxFGIndex.value_or_default() == n))
                        {
                            _ffxFGIndex = n;
                            config->FfxFGIndex = _ffxFGIndex;
                            state.fgChanged = true;
                            state.scChanged = true;
                        }
                    }

                    ImGui::EndCombo();
                }
                ImGui::PopItemWidth();

                ShowTooltip("List of FGs reported by FFX SDK");
            }

            RenderFGModeCombo(ctx, "Frame Gen##fsrfg", 1, nullptr);

            if (basicView)
                return;

            bool fgAsync = config->FGAsync.value_or_default();
            if (IlionTheme::Checkbox(TrL("Allow Async"), &fgAsync))
            {
                config->FGAsync = fgAsync;

                if (config->IsFGEnabled())
                {
                    state.fgChanged = true;
                    state.scChanged = true;
                    LOG_DEBUG("Async set FGChanged");
                }
            }
            ShowTooltip("Enable Async for better FG performance\nMight cause crashes, especially with HUD Fix!");

            // ILION: debug view and watermark under one folded key
            // Display, debug and extended settings: Advanced only
            if (!EssentialView())
            if (auto display = ScopedCollapsingHeader(TrL("Display and debug##fsrfg")); display.IsHeaderOpen())
            {
            bool fgDV = config->FGDebugView.value_or_default();
            if (IlionTheme::Checkbox(TrL("Debug View##2"), &fgDV))
            {
                config->FGDebugView = fgDV;

                if (config->IsFGEnabled())
                {
                    state.fgChanged = true;
                    LOG_DEBUG("DebugView set FGChanged");
                }
            }
            ShowTooltip("Enable FSR3.1-FG Debug view\n\n"
                        "Top left: Game Motion Vectors\n"
                        "Top middle: GMV Depth\n"
                        "Top right: Optical Flow MV\n"
                        "Middle: Interpolated frame only\n"
                        "Bottom left: Disocclusion mask\n"
                        "Bottom middle: Interpolation source (w/o UI)\n"
                        "Bottom right: HUDless resource");

            ImGui::SameLine(0.0f, 16.0f);

            if (state.currentFG && state.currentFG->Version().major > 3)
            {
                if (bool fgwm = config->FSRFGEnableWatermark.value_or_default();
                    IlionTheme::Checkbox(TrL("Enable Watermark"), &fgwm))
                {
                    LOG_DEBUG("FSRFGEnableWatermark set FGWatermark: {}", fgwm);
                    config->FSRFGEnableWatermark = fgwm;
                }

                ShowTooltip("After changing this option, please Save Settings\n"
                            "It will be applied on next launch.");
            }
            } // display and debug

            ImGui::Spacing();

            if (!EssentialView())
            if (auto ch = ScopedCollapsingHeader(TrL("Extended FSR FG Settings")); ch.IsHeaderOpen())
            {
                ScopedIndent indent {};
                ImGui::Spacing();

                IlionTheme::Checkbox(TrL("FG Only Generated"), &state.fgOnlyGenerated);
                ShowTooltip("Display only FSR 3.1 Generated frames");

                SameLineIfRoom(CheckboxWidth(TrL("Debug Reset Lines")), 16.0f);
                auto debugResetLines = config->FGDebugResetLines.value_or_default();
                if (IlionTheme::Checkbox(TrL("Debug Reset Lines"), &debugResetLines))
                {
                    config->FGDebugResetLines = debugResetLines;
                    LOG_DEBUG("Enabled set FGDebugLines: {}", debugResetLines);
                }
                ShowTooltip("Enables drawing of Interpolation skip lines");

                auto debugTearLines = config->FGDebugTearLines.value_or_default();
                if (IlionTheme::Checkbox(TrL("Debug Tear Lines"), &debugTearLines))
                {
                    config->FGDebugTearLines = debugTearLines;
                    LOG_DEBUG("Enabled set FGDebugLines: {}", debugTearLines);
                }
                ShowTooltip("Enables drawing of Tear and Interpolation skip lines");

                SameLineIfRoom(CheckboxWidth(TrL("Debug Pacing Lines")), 16.0f);
                auto debugPacingLines = config->FGDebugPacingLines.value_or_default();
                if (IlionTheme::Checkbox(TrL("Debug Pacing Lines"), &debugPacingLines))
                {
                    config->FGDebugPacingLines = debugPacingLines;
                    LOG_DEBUG("Enabled set FGDebugLines: {}", debugPacingLines);
                }
                ShowTooltip("Enables drawing of Pacing lines");

                ImGui::Spacing();
                if (ImGui::TreeNode(TrL("FG Rectangle Settings")))
                {
                    ImGui::PushItemWidth(95.0f * menuResScale);
                    int rectLeft = config->FGRectLeft.value_or(0);
                    if (ImGui::InputInt(TrL("Rect Left"), &rectLeft))
                        config->FGRectLeft = rectLeft;

                    ImGui::SameLine(0.0f, 16.0f);
                    int rectTop = config->FGRectTop.value_or(0);
                    if (ImGui::InputInt(TrL("Rect Top"), &rectTop))
                        config->FGRectTop = rectTop;

                    int rectWidth = config->FGRectWidth.value_or(0);
                    if (ImGui::InputInt(TrL("Rect Width"), &rectWidth))
                        config->FGRectWidth = rectWidth;

                    ImGui::SameLine(0.0f, 16.0f);
                    int rectHeight = config->FGRectHeight.value_or(0);
                    if (ImGui::InputInt(TrL("Rect Height"), &rectHeight))
                        config->FGRectHeight = rectHeight;

                    ImGui::PopItemWidth();
                    ShowTooltip("Frame generation rectangle, adjust for letterboxed content");

                    ImGui::BeginDisabled(!config->FGRectLeft.has_value() && !config->FGRectTop.has_value() &&
                                         !config->FGRectWidth.has_value() && !config->FGRectHeight.has_value());

                    if (IlionTheme::Button(TrL("Reset FG Rect")))
                    {
                        config->FGRectLeft.reset();
                        config->FGRectTop.reset();
                        config->FGRectWidth.reset();
                        config->FGRectHeight.reset();
                    }

                    ShowTooltip("Resets Frame generation rectangle");

                    ImGui::EndDisabled();
                    ImGui::TreePop();
                }

                auto fg = state.currentFG;
                if (fg != nullptr && strcmp(fg->Name(), "FSR-FG") == 0 &&
                    FfxApiProxy::VersionDx12_FG() >= feature_version { 3, 1, 3 })
                {
                    ImGui::Spacing();

                    if (ImGui::TreeNode(TrL("Frame Pacing Tuning")))
                    {
                        auto fptEnabled = config->FGFramePacingTuning.value_or_default();
                        if (IlionTheme::Checkbox(TrL("Enable Tuning"), &fptEnabled))
                        {
                            config->FGFramePacingTuning = fptEnabled;
                            state.fsrfgFramePaceTuningChanged = true;
                        }

                        ImGui::BeginDisabled(!config->FGFramePacingTuning.value_or_default());

                        ImGui::PushItemWidth(115.0f * menuResScale);
                        auto fptSafetyMargin = config->FGFPTSafetyMarginInMs.value_or_default();
                        if (ImGui::InputFloat(TrL("Safety Margins in ms"), &fptSafetyMargin, 0.01f, 0.1f, "%.2f"))
                            config->FGFPTSafetyMarginInMs = fptSafetyMargin;
                        ShowTooltip("Safety margins in millisecons\n"
                                    "FSR default value: 0.1ms\n"
                                    "Opti default value: 0.01ms");

                        auto fptVarianceFactor = config->FGFPTVarianceFactor.value_or_default();
                        if (IlionTheme::SliderFloat(TrL("Variance Factor"), &fptVarianceFactor, 0.0f, 1.0f, "%.2f"))
                            config->FGFPTVarianceFactor = fptVarianceFactor;
                        ShowTooltip("Variance factor\n"
                                    "FSR default value: 0.1\n"
                                    "Opti default value: 0.3");
                        ImGui::PopItemWidth();

                        auto fpHybridSpin = config->FGFPTAllowHybridSpin.value_or_default();
                        if (IlionTheme::Checkbox(TrL("Enable Hybrid Spin"), &fpHybridSpin))
                            config->FGFPTAllowHybridSpin = fpHybridSpin;
                        ShowTooltip("Allows pacing spinlock to sleep, should reduce CPU usage\n"
                                    "Might cause slow ramp up of FPS");

                        ImGui::PushItemWidth(115.0f * menuResScale);
                        auto fptHybridSpinTime = config->FGFPTHybridSpinTime.value_or_default();
                        if (IlionTheme::SliderInt(TrL("Hybrid Spin Time"), &fptHybridSpinTime, 0, 100))
                            config->FGFPTHybridSpinTime = fptHybridSpinTime;
                        ShowTooltip("How long to spin if FPTHybridSpin is true. Measured in timer "
                                    "resolution units.\n"
                                    "Not recommended to go below 2. Will result in frequent overshoots");
                        ImGui::PopItemWidth();

                        auto fpWaitForSingleObjectOnFence =
                            config->FGFPTAllowWaitForSingleObjectOnFence.value_or_default();
                        if (IlionTheme::Checkbox(TrL("Enable WaitForSingleObjectOnFence"), &fpWaitForSingleObjectOnFence))
                        {
                            config->FGFPTAllowWaitForSingleObjectOnFence = fpWaitForSingleObjectOnFence;
                        }
                        ShowTooltip("Allows WaitForSingleObject instead of spinning for fence value");

                        if (IlionTheme::Button(TrL("Apply Timing Changes")))
                            state.fsrfgFramePaceTuningChanged = true;

                        ImGui::EndDisabled();
                        ImGui::TreePop();
                    }
                }

                ImGui::Spacing();
                ImGui::Spacing();
            }
        }
    }

    // XeFG controls
    if (ScopedCard::Shows("fg_xefg") && state.activeFgOutput == FGOutput::XeFG &&
        state.activeFgInput != FGInput::NoFG && state.activeFgInput != FGInput::ForceXeLL &&
        state.currentFGSwapchain != nullptr && XeFGProxy::InitXeFG() && fgOutput)
    {
        ScopedCard card { "XeFG Settings" };

        bool ignoreChecks = config->FGXeFGIgnoreInitChecks.value_or_default();

        bool nativeAA = false;
        if (state.activeFgInput == FGInput::Upscaler && currentFeature != nullptr)
            nativeAA = currentFeature->RenderWidth() == currentFeature->DisplayWidth();

        const bool correctMVs = fgOutput->IsLowResMV() || nativeAA ||
                                (State::Instance().gameQuirks & GameQuirk::ForceFGRenderSizeMVs) || ignoreChecks;

        if (!correctMVs || state.realExclusiveFullscreen)
        {
            config->FGEnabled.set_volatile_value(false);
            config->FGXeFGDebugView.reset();
        }

        const bool restartNeeded = config->FGXeFGDepthInverted.value_or_default() != fgOutput->IsInvertedDepth() ||
                                   config->FGXeFGJitteredMV.value_or_default() != fgOutput->IsJitteredMVs() ||
                                   config->FGXeFGHighResMV.value_or_default() == fgOutput->IsLowResMV();

        bool cantActivate = false;
        if (restartNeeded)
        {
            TextWrappedColored(toneMapColor(IlionTheme::Themed(ImVec4(1.f, 0.8f, 0.f, 1.f), IlionTheme::Role::Warn)),
                               Tr("Restart the game to apply correct XeFG settings!"));
        }
        else
        {
            if (!correctMVs)
                TextWrappedColored(toneMapColor(IlionTheme::Themed(ImVec4(1.f, 0.f, 0.f, 1.f), IlionTheme::Role::Bad)),
                                   Tr("Requires disabling dilated motion vectors"));

            if (!ignoreChecks && state.realExclusiveFullscreen)
            {
                cantActivate = true;
                ImGui::TextColored(toneMapColor(IlionTheme::Themed(ImVec4(1.f, 0.f, 0.f, 1.f), IlionTheme::Role::Bad)), Tr("Borderless display mode required!"));
            }

            if (!ignoreChecks && outputIsHdr)
            {
                if (state.currentSwapchainDesc.BufferDesc.Format >= DXGI_FORMAT_R32G32B32A32_TYPELESS &&
                    state.currentSwapchainDesc.BufferDesc.Format <= DXGI_FORMAT_R16G16B16A16_SINT)
                {
                    cantActivate = true;
                    ImGui::TextColored(toneMapColor(ImVec4(1.0f, 0.0f, 0.0f, 1.f)), Tr("XeFG only supports HDR10"));
                }
            }
        }

        if (!correctMVs || cantActivate || ignoreChecks)
        {
            if (IlionTheme::Checkbox(TrL("Ignore Init Checks"), &ignoreChecks))
                config->FGXeFGIgnoreInitChecks = ignoreChecks;

            ShowTooltip("Ignores all prechecks for XeFG\n"
                        "Don't use this option to skip MV size warning for UE games!\n"
                        "It might cause crashes and bad IQ!");
        }

        ImGui::BeginDisabled(!correctMVs || cantActivate);

        RenderFGModeCombo(ctx, "Frame Gen##xefg", fgOutput->GetMaxInterpolationCount(),
                          &config->FGXeFGInterpolationCount);

        if (basicView)
        {
            ImGui::EndDisabled();
            return;
        }

        ImGui::EndDisabled();

        // ILION: UI composition, debug view and borderless of XeFG under one folded key, same controls and states
        // Display, debug and extended settings: Advanced only
        if (!EssentialView())
        if (auto display = ScopedCollapsingHeader(TrL("Display and debug##xefg")); display.IsHeaderOpen())
        {
        ImGui::BeginDisabled(!correctMVs || cantActivate);
        ImGui::BeginDisabled(!fgOutput->IsUsingHudlessAny() || XeFGProxy::SetUiCompositionState() == nullptr);
        bool fgCompositeUI = config->FGXeFGUIComposition.value_or_default();
        if (IlionTheme::Checkbox(TrL("UI Composition"), &fgCompositeUI))
            config->FGXeFGUIComposition = fgCompositeUI;

        ShowTooltip("Disable HUD/UI interpolation\n"
                    "Reverts back to previous XeFG 2 behaviour\n\n"
                    "Fixes artifacting transparent HUD/UI");
        ImGui::EndDisabled();

        bool fgDV = config->FGXeFGDebugView.value_or_default();
        if (IlionTheme::Checkbox(TrL("Debug View##2"), &fgDV))
        {
            config->FGXeFGDebugView = fgDV;

            if (config->FGXeFGDebugView.value_or_default())
            {
                state.fgChanged = true;
                LOG_DEBUG("DebugView set FGChanged");
            }
        }
        ShowTooltip("Enable XeFG Debug view");

        ImGui::EndDisabled();

        bool fgBorderless = config->FGXeFGForceBorderless.value_or_default();
        if (IlionTheme::Checkbox(TrL("Force Borderless"), &fgBorderless))
            config->FGXeFGForceBorderless = fgBorderless;

        ShowTooltip("Forces Borderless display mode\n\n"
                    "For best results, set fullscreen \n"
                    "resolution to your display resolution\n"
                    "Might cause some instability issues.\n\n"
                    "NEEDS GAME RESTART TO BE ACTIVE!");
        } // display and debug

        // Disable this for now
        // ImGui::SameLine(0.0f, 16.0f);
        // IlionTheme::Checkbox(TrL("Only Generated##2"), &state.fgOnlyGenerated);
        // ShowTooltip("Display only XeFG generated frames");

        ImGui::Spacing();
        if (!EssentialView())
        if (auto ch = ScopedCollapsingHeader(TrL("Extended XeFG Settings")); ch.IsHeaderOpen())
        {
            ImGui::Spacing();
            if (ImGui::TreeNode(TrL("Rectangle Settings")))
            {
                ImGui::PushItemWidth(95.0f * menuResScale);
                int rectLeft = config->FGRectLeft.value_or(0);
                if (ImGui::InputInt(TrL("Rect Left##2"), &rectLeft))
                    config->FGRectLeft = rectLeft;

                ImGui::SameLine(0.0f, 16.0f);
                int rectTop = config->FGRectTop.value_or(0);
                if (ImGui::InputInt(TrL("Rect Top##2"), &rectTop))
                    config->FGRectTop = rectTop;

                int rectWidth = config->FGRectWidth.value_or(0);
                if (ImGui::InputInt(TrL("Rect Width##2"), &rectWidth))
                    config->FGRectWidth = rectWidth;

                ImGui::SameLine(0.0f, 16.0f);
                int rectHeight = config->FGRectHeight.value_or(0);
                if (ImGui::InputInt(TrL("Rect Height##2"), &rectHeight))
                    config->FGRectHeight = rectHeight;

                ImGui::PopItemWidth();
                ShowTooltip("Frame generation rectangle, adjust for letterboxed content##2");

                ImGui::BeginDisabled(!config->FGRectLeft.has_value() && !config->FGRectTop.has_value() &&
                                     !config->FGRectWidth.has_value() && !config->FGRectHeight.has_value());

                if (IlionTheme::Button(TrL("Reset FG Rect##2")))
                {
                    config->FGRectLeft.reset();
                    config->FGRectTop.reset();
                    config->FGRectWidth.reset();
                    config->FGRectHeight.reset();
                }

                ShowTooltip("Resets Frame generation rectangle##2");

                ImGui::EndDisabled();
                ImGui::TreePop();
            }

            ImGui::Spacing();
            ImGui::Spacing();
        }
    }

    // DLSSG controls
    if (ScopedCard::Shows("fg_dlssg") && state.activeFgOutput == FGOutput::DLSSG &&
        state.activeFgInput != FGInput::NoFG && state.currentFGSwapchain != nullptr &&
        StreamlineProxy::LoadStreamline() && fgOutput)
    {
        ScopedCard card { "DLSSG Settings" };

        if (state.activeFgNvngx == FGNvngxReplacement::None && (state.hdrOutputActive && outputIsHdr))
        {
            if (state.currentSwapchainDesc.BufferDesc.Format >= DXGI_FORMAT_R32G32B32A32_TYPELESS &&
                state.currentSwapchainDesc.BufferDesc.Format <= DXGI_FORMAT_R16G16B16A16_SINT)
            {
                ImGui::TextColored(toneMapColor(ImVec4(1.0f, 0.0f, 0.0f, 1.f)), Tr("DLSSG only supports HDR10"));
            }
        }

        ImGui::Text(Tr("Current DLSSG state:"));
        ImGui::SameLine();
        if (auto count = state.dlssgDetectedInterpolationCount; count > 0)
        {
            ImGui::TextColored(toneMapColor(IlionTheme::Themed(ImVec4(0.f, 1.f, 0.25f, 1.f), IlionTheme::Role::Good)), std::format("ON {}x", count + 1).c_str());
        }
        else
        {
            ImGui::TextColored(toneMapColor(IlionTheme::Themed(ImVec4(1.f, 0.f, 0.f, 1.f), IlionTheme::Role::Bad)), Tr("OFF"));
        }

        RenderFGModeCombo(ctx, "Frame Gen##dlssg", fgOutput->GetMaxInterpolationCount(),
                          &config->FGDLSSGInterpolationCount);

        if (basicView)
            return;

        if (fgOutput->GetMaxInterpolationCount() > 1)
        {
            if (fgOutput->GetDMFGSupport())
            {
                ImGui::SameLine(0.0f, 16.0f);

                if (bool dynamicMFG = config->FGDLSSGForceDMFG.value_or_default();
                    IlionTheme::Checkbox(TrL("Force Dynamic MFG"), &dynamicMFG))
                {
                    config->FGDLSSGForceDMFG = dynamicMFG;
                    state.fgChanged = true;
                }

                ImGui::BeginDisabled(!config->FGDLSSGForceDMFG.value_or_default());
                static float fpsTarget = config->FGDLSSGFramerateTargetDMFG.value_or_default();
                float fpsSliderMax = (float) std::max(200, refreshRate);
                IlionTheme::SliderFloat(TrL("DMFG FPS Target"), &fpsTarget, 0, fpsSliderMax, "%.0f");

                ShowTooltip("An active limit of 0 means auto-detect the display refresh rate");

                if (IlionTheme::Button(TrL("Apply Target")))
                {
                    config->FGDLSSGFramerateTargetDMFG = fpsTarget;
                }

                SameLineIfRoom(ButtonWidth(TrL("Reset Target")), 16.0f);

                if (IlionTheme::Button(TrL("Reset Target")))
                {
                    fpsTarget = 0.0f;
                    config->FGDLSSGFramerateTargetDMFG.reset();
                }

                ImGui::EndDisabled();
            }
        }

        bool useGamesMarkers = config->FGDLSSGUseGamesReflexMarkers.value_or_default();
        ImGui::BeginDisabled(!ReflexHooks::gameIsSendingMarkers());
        if (IlionTheme::Checkbox(TrL("Use Game's Reflex Markers"), &useGamesMarkers))
        {
            config->FGDLSSGUseGamesReflexMarkers = useGamesMarkers;
            LOG_DEBUG("Changed set FGDLSSGUseGamesReflexMarkers: {}", useGamesMarkers);
        }
        ImGui::EndDisabled();
    }

    if (ScopedCard::Shows("fg_reprojection") && fgOutput && fgOutput->HasReprojection())
    {
        ScopedCard card { "Reprojection Settings" };

        if (fgOutput->IsActive() && fgOutput->IsReprojectionActive())
        {
            ImGui::Text(Tr("Updated camera rotation by: %.1fms"),
                        (float) fgOutput->GetLastTimeSinceSimStartNs() / 1'000'000.f);
        }
        else
        {
            ImGui::TextDisabled(Tr("Not updating camera rotation"));
        }

        if (state.activeFgOutput == FGOutput::Reprojection)
        {
            RenderFGModeCombo(ctx, "Reprojection##mode", 0, nullptr);
        }
        else
        {
            bool reprojectionActive = config->FGReprojectionEnabled.value_or_default();
            if (IlionTheme::Checkbox(TrL("Active##5"), &reprojectionActive))
            {
                config->FGReprojectionEnabled = reprojectionActive;
                LOG_DEBUG("Reprojection enabled: {}", reprojectionActive);
            }
            ShowTooltip("Enable reprojection");
        }

        SameLineIfRoom(CheckboxWidth(TrL("Show static elements")));

        IlionTheme::Checkbox(TrL("Show static elements"), &state.fgHudlessCompare);
        ShowTooltip("For fine tuning the depth cutoff\n"
                    "Shows UI and depth cutoff areas\n"
                    "Adjust depth cutoff so that only stuff like your gun and hands are marked");

        ImGui::Spacing();

        // clang-format off
        static std::vector<MenuOption<ReprojectionFill>> fillModes = {
            { ReprojectionFill::StrechEdge, "Strech edge" },
            { ReprojectionFill::Dithering, "Dithering" },
            { ReprojectionFill::Noise, "Noise" },
            { ReprojectionFill::Debug, "Debug" }
        };
        // clang-format on

        // need to have a value before combo
        if (!config->ReprojectionFillMode.has_value())
            config->ReprojectionFillMode = config->ReprojectionFillMode.value_or_default();

        PopulateCombo(TrL("Edge fill mode"), config->ReprojectionFillMode, fillModes);
        ShowTooltip("You want either dither or noise\n"
                    "Those two use the unprojected image as fill\n"
                    "and then some blending on the edges to fool the eye");

        float cutoff = config->ReprojectionDepthCutoff.value_or_default();
        if (IlionTheme::SliderFloat(TrL("Depth cutoff"), &cutoff, 0.0f, 1.0f, "%.3f", ImGuiSliderFlags_Logarithmic))
            config->ReprojectionDepthCutoff = cutoff;
        ShowTooltip("Selects how many elements close to the camera\n"
                    "should be shown over the reprojected image.\n"
                    "This prevents your gun from being moved in weird ways.\n\n"
                    "Use \"Show static elements\" to help you adjust it\n"
                    "Unreal Engine games are usually around 0.10\n"
                    "Cyberpunk is around 0.02");

        uint32_t cutoffExpandPx = config->ReprojectionCutoffExpand.value_or_default();
        if (SliderUInt(TrL("Cutoff expand"), &cutoffExpandPx, 0, 8))
            config->ReprojectionCutoffExpand = cutoffExpandPx;
        ShowTooltip("Expands the cutoff area by this many depth pixels\n"
                    "Use only when you see an outline left by the cutoff process");

        if (bool lateLatch = config->ReprojectionLateLatch.value_or_default();
            IlionTheme::Checkbox(TrL("Late latch mouse"), &lateLatch))
        {
            config->ReprojectionLateLatch = lateLatch;
        }
        ShowTooltip("Reads the mouse right when the GPU starts the reprojection\n"
                    "instead of when the frame is presented on the CPU.\n"
                    "Lowers latency, especially when GPU bound");
    }

    // OptiFG
    if (ScopedCard::Shows("fg_optifg") && state.api != API::Vulkan && state.currentFGSwapchain != nullptr &&
        state.activeFgInput == FGInput::Upscaler)
    {
        ScopedCard card { "OptiFG Settings" };

        if (currentFeature != nullptr && !currentFeature->IsFrozen() &&
            ((state.activeFgOutput == FGOutput::FSRFG && FfxApiProxy::IsFGReady()) ||
             (state.activeFgOutput == FGOutput::XeFG && XeFGProxy::Module() != nullptr) ||
             (state.activeFgOutput == FGOutput::DLSSG && StreamlineProxy::Module() != nullptr) ||
             state.activeFgOutput == FGOutput::Reprojection))
        {
            const bool dx11HudfixTracking = state.swapchainInteropApi == SwapchainInteropApi::Dx11wDx12;
            const bool hudfixTrackingSupported =
                !Config::Instance()->FGDisableHUDFix.value_or_default() &&
                (state.swapchainInteropApi == SwapchainInteropApi::None || dx11HudfixTracking);

            // HUDFix is drawn in the Anti-Ghosting tab (RenderOptiFgHudFix)

            bool depthScale = config->FGEnableDepthScale.value_or_default();
            if (IlionTheme::Checkbox(TrL("Scale Depth to fix DLSS RR"), &depthScale))
                config->FGEnableDepthScale = depthScale;
            ShowTooltip("Fix for DLSS-D wrong depth inputs");

            bool resourceFlip = config->FGResourceFlip.value_or_default();
            if (IlionTheme::Checkbox(TrL("Flip (Unity)"), &resourceFlip))
                config->FGResourceFlip = resourceFlip;
            ShowTooltip("Flip Velocity & Depth resources of Unity games");

            SameLineIfRoom(CheckboxWidth(TrL("Flip Use Offset")), 16.0f);

            bool resourceFlipOffset = config->FGResourceFlipOffset.value_or_default();
            if (IlionTheme::Checkbox(TrL("Flip Use Offset"), &resourceFlipOffset))
                config->FGResourceFlipOffset = resourceFlipOffset;
            ShowTooltip("Use height difference as offset");

            if (auto ch = ScopedCollapsingHeader(TrL("Advanced OptiFG Settings")); ch.IsHeaderOpen())
            {
                ScopedIndent indent {};

                ImGui::Spacing();

                auto disablehudfix = config->FGDisableHUDFix.value_or_default();
                {
                    if (IlionTheme::Checkbox(TrL("Disable HUDFix Resource Tracking"), &disablehudfix))
                    {
                        config->FGDisableHUDFix = disablehudfix;
                        LOG_DEBUG("Enabled FGDisableHUDFix: {}", disablehudfix);
                    }
                    ShowTooltip("Disables HUDFix resource tracking and\n"
                                "reduces CPU overhead.\n"
                                "Useful for MLFG which deals well with the HUD.\n\n"
                                "After changing, please Save Settings and restart.");
                }

                if (hudfixTrackingSupported)
                {
                    ImGui::Spacing();

                    auto rb = config->FGResourceBlocking.value_or_default();
                    if (IlionTheme::Checkbox(TrL("Resource Blocking"), &rb))
                    {
                        config->FGResourceBlocking = rb;
                        LOG_DEBUG("Enabled set FGResourceBlocking: {}", rb);
                    }
                    ShowTooltip("Block rarely used resources from using as HUDless \n"
                                "to prevent flickers and other issues\n\n"
                                "HUDfix enable/disable will reset the block list!");

                    SameLineIfRoom(CheckboxWidth(TrL("Relaxed Resource Check")), 16.0f);

                    auto rrc = config->FGRelaxedResolutionCheck.value_or_default();
                    if (IlionTheme::Checkbox(TrL("Relaxed Resource Check"), &rrc))
                    {
                        config->FGRelaxedResolutionCheck = rrc;
                        LOG_DEBUG("Enabled set FGRelaxedResolutionCheck: {}", rrc);
                    }
                    ShowTooltip("Relax resolution checks for HUDless by 32 pixels \n"
                                "Helps games which use black borders for some \n"
                                "resolutions and screen ratios (e.g. Witcher 3)");

                    ImGui::BeginDisabled(state.fgResetCapturedResources);
                    ImGui::PushItemWidth(95.0f * menuResScale);
                    if (IlionTheme::Checkbox(TrL("FG Create List"), &state.fgCaptureResources))
                    {
                        if (!state.fgCaptureResources)
                            config->FGHUDLimit = 1;
                        else
                            state.fgOnlyUseCapturedResources = false;
                    }

                    SameLineIfRoom(CheckboxWidth(TrL("FG Use List")), 16.0f);
                    if (IlionTheme::Checkbox(TrL("FG Use List"), &state.fgOnlyUseCapturedResources))
                    {
                        if (state.fgCaptureResources)
                        {
                            state.fgCaptureResources = false;
                            config->FGHUDLimit = 1;
                        }
                    }

                    ImGui::SameLine(0.0f, 8.0f);
                    ImGui::Text("(%d)", state.fgCapturedResourceCount);

                    ImGui::PopItemWidth();

                    SameLineIfRoom(ButtonWidth(TrL("Reset List")), 16.0f);

                    if (IlionTheme::Button(TrL("Reset List")))
                    {
                        LOG_DEBUG("Resetting captured resource list");

                        state.fgResetCapturedResources = true;
                        state.fgOnlyUseCapturedResources = false;
                    }

                    ImGui::EndDisabled();

                    ImGui::Spacing();
                    ImGui::Spacing();
                    if (ImGui::TreeNode(TrL("Tracking Settings")))
                    {
                        ImGui::BeginDisabled(dx11HudfixTracking);

                        auto ath = config->FGAlwaysTrackHeaps.value_or_default();
                        if (IlionTheme::Checkbox(TrL("Always Track Heaps"), &ath))
                        {
                            config->FGAlwaysTrackHeaps = ath;
                            LOG_DEBUG("Enabled set FGAlwaysTrackHeaps: {}", ath);
                        }
                        ImGui::EndDisabled();

                        ShowTooltip(dx11HudfixTracking
                                        ? "D3D12 only; not applicable to DX11."
                                        : "Always track resources, might cause performance issues\n, but also might "
                                          "fix HUDFix related crashes!");

                        auto disableRTV = config->FGHudfixDisableRTV.value_or_default();
                        if (IlionTheme::Checkbox(TrL("Disable RTV Tracking"), &disableRTV))
                            config->FGHudfixDisableRTV = disableRTV;
                        ShowTooltip("Disable tracking of CreateRenderTargetView\n"
                                    "This might help filtering of wrong HUDless resources");

                        SameLineIfRoom(CheckboxWidth(TrL("Disable SRV Tracking")), 16.0f);

                        auto disableSRV = config->FGHudfixDisableSRV.value_or_default();
                        if (IlionTheme::Checkbox(TrL("Disable SRV Tracking"), &disableSRV))
                            config->FGHudfixDisableSRV = disableSRV;
                        ShowTooltip("Disable tracking of CreateShaderResourceView\n"
                                    "This might help filtering of wrong HUDless resources");

                        auto disableUAV = config->FGHudfixDisableUAV.value_or_default();
                        if (IlionTheme::Checkbox(TrL("Disable UAV Tracking"), &disableUAV))
                            config->FGHudfixDisableUAV = disableUAV;
                        ShowTooltip("Disable tracking of CreateUnorderedAccessView\n"
                                    "This might help filtering of wrong HUDless resources");

                        SameLineIfRoom(CheckboxWidth(TrL("Disable OM Tracking")), 16.0f);

                        auto disableOM = config->FGHudfixDisableOM.value_or_default();
                        if (IlionTheme::Checkbox(TrL("Disable OM Tracking"), &disableOM))
                            config->FGHudfixDisableOM = disableOM;
                        ShowTooltip("Disable tracking of OMSetRenderTargets\n"
                                    "This might help filtering of wrong HUDless resources");

                        auto disableSCR = config->FGHudfixDisableSCR.value_or_default();
                        if (IlionTheme::Checkbox(TrL("Disable SCR Tracking"), &disableSCR))
                            config->FGHudfixDisableSCR = disableSCR;
                        ShowTooltip("Disable tracking of SetComputeRootDescriptorTable\n"
                                    "This might help filtering of wrong HUDless resources");

                        SameLineIfRoom(CheckboxWidth(TrL("Disable SGR Tracking")), 16.0f);

                        auto disableSGR = config->FGHudfixDisableSGR.value_or_default();
                        if (IlionTheme::Checkbox(TrL("Disable SGR Tracking"), &disableSGR))
                            config->FGHudfixDisableSGR = disableSGR;
                        ShowTooltip("Disable tracking of SetGraphicsRootDescriptorTable\n"
                                    "This might help filtering of wrong HUDless resources");

                        ImGui::Spacing();

                        auto disableDI = config->FGHudfixDisableDI.value_or_default();
                        if (IlionTheme::Checkbox(TrL("Disable DI Tracking"), &disableDI))
                            config->FGHudfixDisableDI = disableDI;
                        ShowTooltip("Disable tracking of DrawInstanced\n"
                                    "This might help filtering of wrong HUDless resources");

                        SameLineIfRoom(CheckboxWidth(TrL("Disable DII Tracking")), 16.0f);

                        auto disableDII = config->FGHudfixDisableDII.value_or_default();
                        if (IlionTheme::Checkbox(TrL("Disable DII Tracking"), &disableDII))
                            config->FGHudfixDisableDII = disableDII;
                        ShowTooltip("Disable tracking of DrawIndexedInstanced\n"
                                    "This might help filtering of wrong HUDless resources");

                        auto disableDispatch = config->FGHudfixDisableDispatch.value_or_default();
                        if (IlionTheme::Checkbox(TrL("Disable Dispatch Tracking"), &disableDispatch))
                            config->FGHudfixDisableDispatch = disableDispatch;
                        ShowTooltip("Disable tracking of Dispatch\n"
                                    "This might help filtering of wrong HUDless resources");

                        ImGui::TreePop();
                    }
                }

                ImGui::Spacing();
                if (ImGui::TreeNode(TrL("Resource Settings")))
                {
                    bool makeMVCopies = config->FGMakeMVCopy.value_or_default();
                    if (IlionTheme::Checkbox(TrL("FG Make MV Copies"), &makeMVCopies))
                        config->FGMakeMVCopy = makeMVCopies;
                    ShowTooltip("Make a copy of motion vectors to use with OptiFG\n"
                                "For preventing corruptions that might happen");

                    bool makeDepthCopies = config->FGMakeDepthCopy.value_or_default();
                    if (IlionTheme::Checkbox(TrL("FG Make Depth Copies"), &makeDepthCopies))
                        config->FGMakeDepthCopy = makeDepthCopies;
                    ShowTooltip("Make a copy of depth to use with OptiFG\n"
                                "For preventing corruptions that might happen");

                    ImGui::PushItemWidth(115.0f * menuResScale);
                    float depthScaleMax = config->FGDepthScaleMax.value_or_default();
                    if (ImGui::InputFloat(TrL("FG Scale Depth Max"), &depthScaleMax, 10.0f, 100.0f, "%.1f"))
                        config->FGDepthScaleMax = depthScaleMax;
                    ShowTooltip("Depth values will be divided to this value");
                    ImGui::PopItemWidth();

                    ImGui::TreePop();
                }

                ImGui::Spacing();
                if (ImGui::TreeNode(TrL("Syncing Settings")))
                {
                    bool useMutexForPresent = config->FGUseMutexForSwapchain.value_or_default();
                    if (IlionTheme::Checkbox(TrL("FG Use Mutex for Present"), &useMutexForPresent))
                        config->FGUseMutexForSwapchain = useMutexForPresent;
                    ShowTooltip("Use mutex to prevent desync of FG and crashes\n"
                                "Disabling might improve the perf but decrease stability");

                    ImGui::TreePop();
                }

                ImGui::Spacing();
                ImGui::Spacing();
            }
        }
        else if (currentFeature == nullptr || currentFeature->IsFrozen())
        {
            ImGui::Text(Tr("Upscaler is not active")); // Probably never will be visible
        }
        else if (state.activeFgOutput == FGOutput::FSRFG && !FfxApiProxy::IsFGReady())
        {
            ImGui::TextColored(toneMapColor({ 1.0f, 0.0f, 0.0f, 1.0f }),
                               Tr("amd_fidelityfx_dx12.dll is missing!")); // Probably never will be visible
        }
        else if (state.activeFgOutput == FGOutput::XeFG && XeFGProxy::Module() == nullptr)
        {
            ImGui::TextColored(toneMapColor({ 1.0f, 0.0f, 0.0f, 1.0f }),
                               Tr("libxess_fg.dll is missing!")); // Probably never will be visible
        }
    }

    const FGNvngxReplacement activeNvngxFg = state.activeFgNvngx;
    if (ScopedCard::Shows("fg_nvngx") && activeNvngxFg != FGNvngxReplacement::None)
    {
        ScopedCard card;

        if (activeNvngxFg == FGNvngxReplacement::Nukems)
        {
            SeparatorWithHelpMarker("Frame Generation (FSR3-FG via Nukem's DLSSG)",
                                    "Requires Nukem's dlssg_to_fsr3 dll");

            if (!state.nukemsFgFileAvailable)
            {
                TextWrappedColored(toneMapColor(IlionTheme::Themed(ImVec4(1.f, 0.f, 0.f, 1.f), IlionTheme::Role::Bad)),
                                   Tr("Please put dlssg_to_fsr3_amd_is_better.dll into OptiScaler folder"));
            }
        }
        else if (activeNvngxFg == FGNvngxReplacement::Arturs)
        {
            SeparatorWithHelpMarker("Frame Generation (FSR3-MFG via DLSS Enabler)",
                                    "DLSS Enabler as dlss-enabler-headless.dll");

            if (!state.artursFgFileAvailable)
            {
                TextWrappedColored(toneMapColor(IlionTheme::Themed(ImVec4(1.f, 0.f, 0.f, 1.f), IlionTheme::Role::Bad)),
                                   Tr("Please put dlss-enabler-headless.dll into OptiScaler folder"));
            }

            ImGui::TextColored(toneMapColor(IlionTheme::Themed(ImVec4(1.f, 0.8f, 0.f, 1.f), IlionTheme::Role::Warn)),
                               Tr("Using a subset of features from DLSS Enabler"));
        }
        else if (activeNvngxFg == FGNvngxReplacement::FFX)
        {
            SeparatorWithHelpMarker("Frame Generation (FSRFG via FFX)", "FFX using the DLSSG swapchain");
        }
        else if (activeNvngxFg == FGNvngxReplacement::Combo)
        {
            SeparatorWithHelpMarker("Frame Generation (Enabler + FFX)",
                                    "FFX for middle fake frames, and Enabler for the rest\n\n2x - FFX\n"
                                    "3x - Enabler\n4x - FFX + Enabler\n5x - Enabler\n6x - FFX + Enabler");
        }

        if (state.activeFgInput == FGInput::NvngxFG)
        {

            bool dmfgActive = state.dlssgGameDMFGSupported && config->FGDLSSGOverrideForceDMFG.value_or_default();

            if (!ReflexHooks::isReflexHooked())
            {
                ImGui::TextColored(toneMapColor(IlionTheme::Themed(ImVec4(1.f, 0.f, 0.f, 1.f), IlionTheme::Role::Bad)), Tr("Reflex not hooked"));
                ImGui::TextWrapped(Tr("If you are using an AMD/Intel GPU, then make sure you have Fakenvapi"));
            }
            else if (ReflexHooks::dlssgFrameCountToGenerate() == 0 && !dmfgActive)
            {
                ImGui::TextWrapped(Tr("Please select DLSS Frame Generation in the game options\n"
                            "You might need to select DLSS first"));
            }

            if (state.swapchainApi == DX12)
            {
                ImGui::Text(Tr("Current DLSSG state:"));
                ImGui::SameLine();
                if (auto count = state.dlssgDetectedInterpolationCount; count > 0)
                {
                    ImGui::TextColored(toneMapColor(IlionTheme::Themed(ImVec4(0.f, 1.f, 0.25f, 1.f), IlionTheme::Role::Good)),
                                       std::format("ON {}x", count + 1).c_str());
                }
                else
                {
                    ImGui::TextColored(toneMapColor(IlionTheme::Themed(ImVec4(1.f, 0.f, 0.f, 1.f), IlionTheme::Role::Bad)), Tr("OFF"));
                }

                // Issue mostly shows up on AMD on Windows on pre-RDNA3 in some non-UE games
                // Hide to reduce confusion, config is still read
                const bool isUnrealEngine = State::Instance().NVNGX_Engine == NVSDK_NGX_ENGINE_TYPE_UNREAL ||
                                            State::Instance().gameQuirks & GameQuirk::ForceUnrealEngine;
                const bool isDllProxyNvngxType =
                    activeNvngxFg == FGNvngxReplacement::Nukems || activeNvngxFg == FGNvngxReplacement::Arturs;
                if (isDllProxyNvngxType && !primaryGpu.dlssCapable && primaryGpu.fsr4Support == FSR4Support::None &&
                    !primaryGpu.d3d12IsVkd3dProton && !isUnrealEngine)
                {
                    if (bool makeDepthCopy = config->NvngxFGMakeDepthCopy.value_or_default();
                        IlionTheme::Checkbox(TrL("Fix broken visuals"), &makeDepthCopy))
                    {
                        config->NvngxFGMakeDepthCopy = makeDepthCopy;
                    }
                    ShowTooltip("Makes a copy of the depth buffer\nCan fix broken visuals in some games on AMD "
                                "GPUs under Windows\nCan cause stutters, so best to use only when necessary");
                }
            }
            else if (state.swapchainApi == Vulkan)
            {
                TextWrappedColored(toneMapColor(IlionTheme::Themed(ImVec4(1.f, 0.8f, 0.f, 1.f), IlionTheme::Role::Warn)),
                                   Tr("DLSSG is purposefully disabled when this menu is visible"));
                ImGui::Spacing();
            }
        }

        bool isLoaded = false;
        if (state.swapchainApi == Vulkan)
            isLoaded = Nvngx_FG::isVulkanAvailable();
        if (state.swapchainApi == DX12)
            isLoaded = Nvngx_FG::isDx12Available();

        if (isLoaded)
        {
            // Dispatch Flags are drawn in the Anti-Ghosting tab (RenderDispatchFlags)

            if (activeNvngxFg == FGNvngxReplacement::Nukems)
            {
                if (IlionTheme::Checkbox(TrL("Enable Debug View"), &state.dlssgDebugView))
                {
                    Nvngx_FG::setDebugView(state.dlssgDebugView);
                }
                if (IlionTheme::Checkbox(TrL("Interpolated frames only"), &state.dlssgInterpolatedOnly))
                {
                    Nvngx_FG::setInterpolatedOnly(state.dlssgInterpolatedOnly);
                }
            }

            if (activeNvngxFg == FGNvngxReplacement::FFX || activeNvngxFg == FGNvngxReplacement::Combo)
            {
                if (_ffxFGIndex < 0)
                    _ffxFGIndex = config->FfxFGIndex.value_or_default();

                if (state.ffxFGVersionNames.size() > 0)
                {
                    ImGui::PushItemWidth(135.0f * menuResScale);

                    auto currentName = StrFmt("FSR %s", state.ffxFGVersionNames[_ffxFGIndex]);
                    if (IlionTheme::BeginCombo(TrL("FFX FG"), currentName.c_str()))
                    {
                        for (int n = 0; n < state.ffxFGVersionIds.size(); n++)
                        {
                            auto name = StrFmt("FSR %s", state.ffxFGVersionNames[n]);
                            if (ImGui::Selectable(name.c_str(), config->FfxFGIndex.value_or_default() == n))
                                _ffxFGIndex = n;
                        }

                        ImGui::EndCombo();
                    }
                    ImGui::PopItemWidth();

                    ShowTooltip("List of FGs reported by FFX SDK");

                    SameLineIfRoom(ButtonWidth(TrL("Change FG")), 6.0f);

                    if (IlionTheme::Button(TrL("Change FG")) && _ffxFGIndex != config->FfxFGIndex.value_or_default())
                    {
                        config->FfxFGIndex = _ffxFGIndex;
                        state.fgChanged = true;
                    }
                }

                bool fgAsync = config->FGAsync.value_or_default();
                if (IlionTheme::Checkbox(TrL("Allow Async##2"), &fgAsync))
                {
                    config->FGAsync = fgAsync;

                    if (config->IsFGEnabled())
                    {
                        state.fgChanged = true;
                        LOG_DEBUG("Async set FGChanged");
                    }
                }
                ShowTooltip("Enable Async for better FG performance\nMight cause crashes, especially with HUD Fix!");

                SameLineIfRoom(CheckboxWidth(TrL("Debug View##3")), 20.0f * menuResScale);
                bool fgDV = config->FGDebugView.value_or_default();
                if (IlionTheme::Checkbox(TrL("Debug View##3"), &fgDV))
                {
                    config->FGDebugView = fgDV;

                    if (config->IsFGEnabled())
                    {
                        state.fgChanged = true;
                        LOG_DEBUG("DebugView set FGChanged");
                    }
                }
                ShowTooltip("Enable FSR3.1-FG Debug view\n\n"
                            "Top left: Game Motion Vectors\n"
                            "Top middle: GMV Depth\n"
                            "Top right: Optical Flow MV\n"
                            "Middle: Interpolated frame only\n"
                            "Bottom left: Disocclusion mask\n"
                            "Bottom middle: Interpolation source (w/o UI)\n"
                            "Bottom right: HUDless resource");

                if (Nvngx_FG::version().major > 3)
                {
                    SameLineIfRoom(CheckboxWidth(TrL("Enable Watermark")), 20.0f * menuResScale);
                    if (bool fgwm = config->FSRFGEnableWatermark.value_or_default();
                        IlionTheme::Checkbox(TrL("Enable Watermark"), &fgwm))
                    {
                        LOG_DEBUG("FSRFGEnableWatermark set FGWatermark: {}", fgwm);
                        config->FSRFGEnableWatermark = fgwm;
                    }

                    ShowTooltip("After changing this option, please Save Settings\n"
                                "It will be applied on next launch.");
                }
            }

            if (bool disableHudless = config->NvngxFGDisableHudless.value_or_default();
                IlionTheme::Checkbox(TrL("Disable HUDless"), &disableHudless))
            {
                config->NvngxFGDisableHudless = disableHudless;
            }
            ShowTooltip("Might be required for some sets of DispatchFlags");
        }
    }

    // FSR-FG Inputs
    if (ScopedCard::Shows("fg_fsrfg_inputs") && state.currentFGSwapchain != nullptr &&
        (state.activeFgInput == FGInput::FSRFG || state.activeFgInput == FGInput::FSRFG30))
    {
        ScopedCard card;

        SeparatorWithHelpMarker("Frame Generation (FSR-FG Inputs)", "Select FSR-FG in-game");

        auto fgOutput = reinterpret_cast<IFGFeature_Dx12*>(state.currentFG);
        if (fgOutput != nullptr)
        {
            ImGui::Text(Tr("Current FSR-FG state:"));
            ImGui::SameLine();
            if (state.fsrfgInputActive)
            {
                if (fgOutput->IsActive())
                    ImGui::TextColored(toneMapColor(IlionTheme::Themed(ImVec4(0.f, 1.f, 0.25f, 1.f), IlionTheme::Role::Good)), Tr("ON"));
                else
                    ImGui::TextColored(toneMapColor(ImVec4(1.0f, 0.647f, 0.0f, 1.f)), Tr("ACTIVATE FG"));
            }
            else
            {
                ImGui::TextColored(toneMapColor(IlionTheme::Themed(ImVec4(1.f, 0.f, 0.f, 1.f), IlionTheme::Role::Bad)), Tr("OFF"));
                ImGui::TextWrapped(Tr("Please select FSR Frame Generation in the game options\n"
                            "You might need to select FSR first"));
            }
        }

        bool skipConfig = config->FSRFGSkipConfigForHudless.value_or_default();
        if (IlionTheme::Checkbox(TrL("Skip Config for HUDless"), &skipConfig))
            config->FSRFGSkipConfigForHudless = skipConfig;

        ShowTooltip("Do not use HUDless set at ffxConfig");

        SameLineIfRoom(CheckboxWidth(TrL("Skip Dispatch for HUDless")), 6.0f);

        bool skipDispatch = config->FSRFGSkipDispatchForHudless.value_or_default();
        if (IlionTheme::Checkbox(TrL("Skip Dispatch for HUDless"), &skipDispatch))
            config->FSRFGSkipDispatchForHudless = skipDispatch;

        ShowTooltip("Do not use HUDless set at ffxDispatch");
    }

    // Streamline FG Inputs
    if (ScopedCard::Shows("fg_sl_inputs") && state.currentFGSwapchain != nullptr &&
        state.activeFgInput == FGInput::DLSSG)
    {
        ScopedCard card;

        SeparatorWithHelpMarker("Frame Generation (Streamline FG Inputs)", "Select DLSS-FG in-game");

        auto fgOutput = reinterpret_cast<IFGFeature_Dx12*>(state.currentFG);

        if (!ReflexHooks::isReflexHooked())
        {
            ImGui::TextColored(toneMapColor(IlionTheme::Themed(ImVec4(1.f, 0.f, 0.f, 1.f), IlionTheme::Role::Bad)), Tr("Reflex not hooked"));
            ImGui::TextWrapped(Tr("If you are using an AMD/Intel GPU, then make sure you have fakenvapi"));
        }
        else if (fgOutput != nullptr)
        {
            ImGui::Text(Tr("Current Streamline FG state:"));
            ImGui::SameLine();
            if ((state.fgLastFrame - state.dlssgLastFrame) < 3)
            {
                if (fgOutput->IsActive())
                    ImGui::TextColored(toneMapColor(IlionTheme::Themed(ImVec4(0.f, 1.f, 0.25f, 1.f), IlionTheme::Role::Good)), Tr("ON"));
                else
                    ImGui::TextColored(toneMapColor(ImVec4(1.0f, 0.647f, 0.0f, 1.f)), Tr("ACTIVATE FG"));
            }
            else
            {
                ImGui::TextColored(toneMapColor(IlionTheme::Themed(ImVec4(1.f, 0.f, 0.f, 1.f), IlionTheme::Role::Bad)), Tr("OFF"));
                ImGui::TextWrapped(Tr("Please select DLSS Frame Generation in the game options\n"
                            "You might need to select DLSS first"));
            }
        }
    }
}

void MenuCommon::RenderFsrCommonSettings(RenderMenuContext& ctx)
{
    auto& state = ctx.state;
    auto config = ctx.config;
    auto& currentFeature = ctx.currentFeature;

    if (currentFeature != nullptr && !currentFeature->IsFrozen())
    {
        // FSR Common -----------------
        if (currentFeature != nullptr && !currentFeature->IsFrozen() &&
            (state.activeFgOutput == FGOutput::FSRFG || IsFsr(currentBackend)))
        {
            SeparatorWithHelpMarker("FSR Common Settings", "Affects both FSR-FG & Upscalers");

            bool useFsrVales = config->FsrUseFsrInputValues.value_or_default();
            if (IlionTheme::Checkbox(TrL("Use FSR Input Values"), &useFsrVales))
                config->FsrUseFsrInputValues = useFsrVales;

            ImGui::Spacing();
            if (auto ch = ScopedCollapsingHeader(TrL("FoV & Camera Values")); ch.IsHeaderOpen())
            {
                ScopedIndent indent {};
                ImGui::Spacing();

                bool useVFov = config->FsrVerticalFov.has_value() || !config->FsrHorizontalFov.has_value();

                float vfov = config->FsrVerticalFov.value_or_default();
                float hfov = config->FsrHorizontalFov.value_or(90.0f);

                if (useVFov && !config->FsrVerticalFov.has_value())
                    config->FsrVerticalFov = vfov;
                else if (!useVFov && !config->FsrHorizontalFov.has_value())
                    config->FsrHorizontalFov = hfov;

                if (IlionTheme::RadioButton(TrL("Use Vert. Fov"), useVFov))
                {
                    config->FsrHorizontalFov.reset();
                    config->FsrVerticalFov = vfov;
                    useVFov = true;
                }

                ImGui::SameLine(0.0f, 6.0f);

                if (IlionTheme::RadioButton(TrL("Use Horz. Fov"), !useVFov))
                {
                    config->FsrVerticalFov.reset();
                    config->FsrHorizontalFov = hfov;
                    useVFov = false;
                }

                if (useVFov)
                {
                    if (IlionTheme::SliderFloat(TrL("Vert. FOV"), &vfov, 0.0f, 180.0f, "%.1f"))
                        config->FsrVerticalFov = vfov;

                    ShowTooltip("Might help achieve better image quality");
                }
                else
                {
                    if (IlionTheme::SliderFloat(TrL("Horz. FOV"), &hfov, 0.0f, 180.0f, "%.1f"))
                        config->FsrHorizontalFov = hfov;

                    ShowTooltip("Might help achieve better image quality");
                }

                float cameraNear;
                float cameraFar;

                cameraNear = config->FsrCameraNear.value_or_default();
                cameraFar = config->FsrCameraFar.value_or_default();

                if (IlionTheme::SliderFloat(TrL("Camera Near"), &cameraNear, 0.1f, 500000.0f, "%.1f"))
                    config->FsrCameraNear = cameraNear;
                ShowTooltip("Might help achieve better image quality\n"
                            "And potentially less ghosting");

                if (IlionTheme::SliderFloat(TrL("Camera Far"), &cameraFar, 0.1f, 500000.0f, "%.1f"))
                    config->FsrCameraFar = cameraFar;
                ShowTooltip("Might help achieve better image quality\n"
                            "And potentially less ghosting");

                if (IlionTheme::Button(TrL("Reset Camera Values")))
                {
                    config->FsrVerticalFov.reset();
                    config->FsrHorizontalFov.reset();
                    config->FsrCameraNear.reset();
                    config->FsrCameraFar.reset();
                }

                ImGui::SameLine(0.0f, 6.0f);
                ImGui::Text(Tr("Near: %.1f Far: %.1f"),
                            state.lastFsrCameraNear < 500000.0f ? state.lastFsrCameraNear : 500000.0f,
                            state.lastFsrCameraFar < 500000.0f ? state.lastFsrCameraFar : 500000.0f);

                ImGui::Spacing();
                ImGui::Spacing();
            }
        }
    }
}

void MenuCommon::RenderFramerateSettings(RenderMenuContext& ctx)
{
    auto& state = ctx.state;
    auto config = ctx.config;
    auto& menuResScale = ctx.menuResScale;

    // Framerate ---------------------
    if (state.reflexLimitsFps || config->OverlayMenu.value_or_default())
    {
        SeparatorWithHelpMarker(
            "Framerate", "Uses Reflex when possible\nOn AMD/Intel cards, you can use Fakenvapi to substitute Reflex");

        static std::string currentMethod {};
        LowLatencyMode fakenvapiMode = {};
        if (state.reflexLimitsFps)
        {
            fakenvapiMode = fakenvapi::getCurrentMode();

            if (fakenvapiMode == LowLatencyMode::AntiLag2)
                currentMethod = "FSR Anti-Lag 2.0";
            else if (fakenvapiMode == LowLatencyMode::LatencyFlex)
                currentMethod = "LatencyFlex";
            else if (fakenvapiMode == LowLatencyMode::XeLL)
                currentMethod = "XeLL";
            else if (fakenvapiMode == LowLatencyMode::AntiLagVk)
                currentMethod = "Vulkan AntiLag";
            else if (fakenvapiMode == LowLatencyMode::None)
            {
                if (fakenvapi::isUsingAsMainNvapi())
                    currentMethod = Tr("None");
                else
                    currentMethod = "Reflex";
            }

            if (state.rtssReflexInjection && fakenvapiMode == LowLatencyMode::AntiLag2 &&
                config->FGOutput.value_or_default() == FGOutput::FSRFG)
                TextWrappedColored(toneMapColor(IlionTheme::Themed(ImVec4(1.f, 0.8f, 0.f, 1.f), IlionTheme::Role::Warn)),
                                   "%s", Tr("Using RTSS Reflex injection with FSR Anti-Lag 2.0 and FSR FG "
                                   "might cause issues"));
        }
        else
        {
            if (XellHooks::canLimit())
                currentMethod = Tr("Game's XeLL");
            else
                currentMethod = Tr("Fallback");
        }

        if (state.rtssReflexInjection)
            currentMethod.append(" (RTSS)");

        const bool fakenvapiInactive = (fakenvapi::isUsingAsMainNvapi() || fakenvapiMode == LowLatencyMode::XeLL) &&
                                       !fakenvapi::isLowLatencyActive() && state.reflexLimitsFps;

        if (fakenvapiInactive)
            currentMethod.append(Tr(" (inactive)"));

        ImGui::Text(Tr("Current method: %s"), currentMethod.c_str());

        if (fakenvapiMode == LowLatencyMode::AntiLag2)
            ShowTooltip("FSR Anti-Lag 2.0 is AMD's current name for Anti-Lag 2");

        if (state.reflexShowWarning)
        {
            TextWrappedColored(toneMapColor(IlionTheme::Themed(ImVec4(1.f, 0.f, 0.f, 1.f), IlionTheme::Role::Bad)),
                               "%s", Tr("Using Reflex's limit with FSR FG has performance overhead"));

            ImGui::Spacing();
        }

        // set initial value
        if (std::isinf(_limitFps))
            _limitFps = config->FramerateLimit.value_or_default();

        float fpsSliderMax = (float) std::max(200, refreshRate);
        IlionTheme::SliderFloat(TrL("FPS Limit"), &_limitFps, 0, fpsSliderMax, "%.0f");

        if (IlionTheme::Button(TrL("Apply Limit")))
        {
            config->FramerateLimit = _limitFps;
        }

        SameLineIfRoom(ButtonWidth(TrL("Reset Limit")), 16.0f);

        if (IlionTheme::Button(TrL("Reset Limit")))
        {
            _limitFps = 0.0f;
            config->FramerateLimit = _limitFps;
        }

        ImGui::Spacing();
        if (auto ch = ScopedCollapsingHeader(TrL("VRR Frame Cap Calculator")); ch.IsHeaderOpen())
        {
            ScopedIndent indent {};
            ImGui::Spacing();

            ImGui::PushItemWidth(105.0f * menuResScale);
            ImGui::InputInt(TrL("Refresh Rate"), &refreshRate, 1, 1, ImGuiInputTextFlags_None);
            ImGui::PopItemWidth();

            float refreshRateF = static_cast<float>(refreshRate);
            // it's fine to use with real reflex, we only care about antilag
            auto fpsLimitTech = fakenvapi::getCurrentMode();
            constexpr float margin = 0.3f; // in ms
            float frameCap = std::round(10000.f / (1000.f / refreshRateF + margin)) / 10.f;

            if (fpsLimitTech == LowLatencyMode::AntiLag2 || fpsLimitTech == LowLatencyMode::AntiLagVk)
                frameCap = std::round(frameCap);

            ImGui::Text(Tr("Calculated Cap: %.1f"), frameCap);

            SameLineIfRoom(ButtonWidth(TrL("Set as FPS Limit")), 16.0f);

            if (IlionTheme::Button(TrL("Set as FPS Limit")))
            {
                _limitFps = frameCap;
                config->FramerateLimit = _limitFps;
            }
        }
    }
}

void MenuCommon::RenderFakenvapiSettings(RenderMenuContext& ctx)
{
    auto& state = ctx.state;
    auto config = ctx.config;

    // Using state.reflexLimitsFps as a detection for Reflex being used on Nvidia
    const bool showLatencyFlex =
        fakenvapi::isUsingAsMainNvapi() || (state.activeFgOutput == FGOutput::XeFG && state.reflexLimitsFps);
    const bool showForceXell = state.swapchainApi != API::Vulkan;

    if (!showLatencyFlex && !showForceXell)
        return;

    // FAKENVAPI ---------------------------
    SectionTitle("fakenvapi");

    if (showLatencyFlex)
    {
        ImGui::BeginDisabled(state.activeFgOutput == FGOutput::XeFG || state.activeFgInput == FGInput::ForceXeLL);
        if (bool forceLFX = config->FN_ForceLatencyFlex.value_or_default();
            IlionTheme::Checkbox(TrL("Force LatencyFlex"), &forceLFX))
        {
            config->FN_ForceLatencyFlex = forceLFX;
        }
        ShowTooltip("By default FSR Anti-Lag 2.0, or else XeLL, is used when available; neither is used with Multi Frame "
                    "Generation or dynamic DLSS-G.\nThis forces LatencyFleX instead, without restarting the game");
        ImGui::EndDisabled();
    }

    if (showForceXell)
    {
        if (showLatencyFlex)
            SameLineIfFits(TrL("Force XeLL"), 16.0f);

        bool forceXell = config->ForceXeLL.value_or_default();
        static bool activeForceXeLL = forceXell;

        if (IlionTheme::Checkbox(TrL("Force XeLL"), &forceXell))
        {
            config->ForceXeLL = forceXell;
        }
        ShowTooltip("Allows XeLL to work without FG on non-Intel cards.\n\nDisables FG "
                    "options\n\nRequires a restart");

        if (activeForceXeLL != forceXell)
        {
            ImGui::Spacing();
            TextWrappedColored(toneMapColor(ImVec4(1.f, 0.f, 0.0f, 1.f)), "%s", Tr("Save INI and restart to apply the changes"));
            ImGui::Spacing();
        }
    }

    if (showLatencyFlex)
    {
        // clang-format off
        const std::vector<MenuOption<LFXMode>> lfx_modes = {
            { LFXMode::Conservative, Tr("Conservative"),
                Tr("The safest, but might not reduce latency well") },
            { LFXMode::Aggressive, Tr("Aggressive"),
                Tr("Improves latency, but in some cases will lower FPS more than expected") },
            { LFXMode::ReflexIDs, Tr("Reflex ID"),
                "Best when can be used, some games are not compatible (e.g. Cyberpunk)\n"
                "and will fallback to Aggressive" }
        };

        bool usingLFX = fakenvapi::getCurrentMode() == LowLatencyMode::LatencyFlex;

        ImGui::BeginDisabled(!usingLFX);
        PopulateCombo(TrL("LatencyFlex mode"), config->FN_LatencyFlexMode, lfx_modes);
        ImGui::EndDisabled();

        const std::vector<MenuOption<ForceReflex>> reflex_modes = { { ForceReflex::InGame, Tr("Follow in-game") },
                                                                { ForceReflex::ForceDisable, Tr("Force Disable") },
                                                                { ForceReflex::ForceEnable, Tr("Force Enable") } };

        PopulateCombo(TrL("Force Reflex"), config->FN_ForceReflex, reflex_modes);
        // clang-format on
    }
}

template <typename T> std::string GetMenuOptionLabel(const std::vector<MenuOption<T>>& options, T targetValue)
{
    auto it = std::find_if(options.begin(), options.end(),
                           [targetValue](const MenuOption<T>& option) { return option.value == targetValue; });

    if (it != options.end())
    {
        return it->label;
    }

    return "Unknown";
}

void MenuCommon::RenderLowLatencySettings(RenderMenuContext& ctx)
{
    auto& state = ctx.state;
    auto config = ctx.config;

    // Low Latency ---------------------------
    SectionTitle("Low Latency");

    static std::vector<MenuOption<LowLatencyInput>> lowLatencyInput = {
        { LowLatencyInput::None, "None (Off)" },    { LowLatencyInput::Auto, "Auto" },
        { LowLatencyInput::AntiLag2, "AntiLag 2" }, { LowLatencyInput::Reflex, "Reflex" },
        { LowLatencyInput::XeLL, "XeLL" },          { LowLatencyInput::UeLowLatency, "UeLowLatency" },
    };

    static std::vector<MenuOption<LowLatencyMode>> lowLatencyOutput = {
        { LowLatencyMode::None, "None (Off)" },
        { LowLatencyMode::Auto, "Auto" },
        { LowLatencyMode::LatencyFlex, "LatencyFlex" },
        { LowLatencyMode::AntiLag2, "AntiLag 2" },
        { LowLatencyMode::XeLL, "XeLL" },
        { LowLatencyMode::AntiLagVk, "AntiLag Vk" },
        { LowLatencyMode::Reflex, "Reflex" },
    };

    LowLatencyInput activeInput {};
    LowLatencyMode activeOutput {};

    if (ImGui::BeginTable("lowLatencyActive", 2, ImGuiTableFlags_SizingStretchSame))
    {
        InputCommon::get_currently_active(activeInput, activeOutput);

        ImGui::TableNextColumn();

        ImGui::Text(Tr("Active input: %s"), GetMenuOptionLabel(lowLatencyInput, activeInput).c_str());

        ImGui::TableNextColumn();

        ImGui::Text(Tr("Active output: %s"), GetMenuOptionLabel(lowLatencyOutput, activeOutput).c_str());

        ImGui::EndTable();
    }

    if (ImGui::BeginTable("lowLatencySelection", 2, ImGuiTableFlags_SizingStretchSame))
    {
        ImGui::TableNextColumn();

        auto avalibleInputs = InputCommon::get_avaliable_inputs();

        lowLatencyInput[(uint32_t) LowLatencyInput::AntiLag2].set_disabled(!avalibleInputs[LowLatencyInput::AntiLag2]);
        lowLatencyInput[(uint32_t) LowLatencyInput::Reflex].set_disabled(!avalibleInputs[LowLatencyInput::Reflex]);
        lowLatencyInput[(uint32_t) LowLatencyInput::XeLL].set_disabled(!avalibleInputs[LowLatencyInput::XeLL]);
        lowLatencyInput[(uint32_t) LowLatencyInput::UeLowLatency].set_disabled(
            !avalibleInputs[LowLatencyInput::UeLowLatency]);

        // need to have a value before combo
        if (!config->LowLatencyInput.has_value())
            config->LowLatencyInput = config->LowLatencyInput.value_or_default();

        PopulateCombo(TrL("Input"), config->LowLatencyInput, lowLatencyInput);

        ImGui::TableNextColumn();

        lowLatencyOutput[(uint32_t) LowLatencyMode::AntiLagVk].set_disabled(true, "No support");
        lowLatencyOutput[(uint32_t) LowLatencyMode::Reflex].set_disabled(true, "No support");

        // need to have a value before combo
        if (!config->LowLatencyOutput.has_value())
            config->LowLatencyOutput = config->LowLatencyOutput.value_or_default();

        PopulateCombo(TrL("Output"), config->LowLatencyOutput, lowLatencyOutput);

        ImGui::EndTable();
    }

    if (activeOutput == LowLatencyMode::LatencyFlex)
    {
        static const std::vector<MenuOption<LFXMode>> lfx_modes = {
            { LFXMode::Conservative, "Conservative", "The safest, but might not reduce latency well" },
            { LFXMode::Aggressive, "Aggressive",
              "Improves latency, but in some cases will lower FPS more than expected" },
            { LFXMode::ReflexIDs, "Reflex ID",
              "Best when can be used, some games are not compatible (e.g. Cyberpunk)\n"
              "and will fallback to Aggressive" }
        };

        PopulateCombo(TrL("LatencyFlex mode"), config->FN_LatencyFlexMode, lfx_modes);
    }

    static std::vector<MenuOption<ForceReflex>> lowlatency_states = { { ForceReflex::InGame, "Follow in-game" },
                                                                      { ForceReflex::ForceDisable, "Force Disable" },
                                                                      { ForceReflex::ForceEnable, "Force Enable" } };

    ImGui::SetNextItemWidth(150.0f * ctx.menuResScale);
    PopulateCombo(TrL("Force State"), config->FN_ForceReflex, lowlatency_states);
}

void MenuCommon::RenderActiveImageSettings(RenderMenuContext& ctx)
{
    auto& state = ctx.state;
    auto config = ctx.config;
    auto& currentFeature = ctx.currentFeature;
    auto& menuResScale = ctx.menuResScale;

    bool rcasEnabled = false;

    // SHARPNESS -----------------------------
    SectionTitle("Sharpness");

    if (bool overrideSharpness = config->OverrideSharpness.value_or_default();
        IlionTheme::Checkbox(TrL("Override"), &overrideSharpness))
    {
        config->OverrideSharpness = overrideSharpness;

        if ((currentBackend == Upscaler::DLSS || currentBackend == Upscaler::DLSS_on12) &&
            currentFeature->Version().major < 3)
        {
            state.newBackend = currentBackend;
            MARK_ALL_BACKENDS_CHANGED();
        }
    }
    ShowTooltip("Ignores the value sent by the game\n"
                "and uses the value set below");

    ImGui::SameLine(0.0f, 16.0f * menuResScale);

    float featuresCurrentSharpness = currentFeature->Sharpness();
    if (featuresCurrentSharpness > 0.0f)
        ImGui::TextDisabled(Tr("(Current: %.3f)"), featuresCurrentSharpness);
    else
        ImGui::TextDisabled(Tr("(Current: disabled)"));

    ImGui::BeginDisabled(!config->OverrideSharpness.value_or_default());

    float sharpness = config->Sharpness.value_or_default();

    if (IlionTheme::SliderFloat(TrL("Strength"), &sharpness, 0.0f, 1.0f))
        config->Sharpness = sharpness;

    ImGui::EndDisabled();

    // RCAS
    {
        // xess or dlss version >= 2.5.1
        constexpr feature_version requiredDlssVersion = { 2, 5, 1 };
        const bool isDlss = currentBackend == Upscaler::DLSS || currentBackend == Upscaler::DLSS_on12;
        rcasEnabled = currentBackend == Upscaler::XeSS || (isDlss && currentFeature->Version() >= requiredDlssVersion);

        // Determine current mode for the dropdown
        bool currentRcasEnabled = config->RcasEnabled.value_or(rcasEnabled);
        auto currentShader = Config::Instance()->SharpnessShader.value_or_default();

        int selectedMode = 0; // 0 = Upscaler's Default
        if (currentRcasEnabled)
        {
            if (currentShader == SharpenShader::RCAS)
                selectedMode = 1;
            else if (currentShader == SharpenShader::DepthAware)
                selectedMode = 2;
            else if (currentShader == SharpenShader::LocalContrastDepthAware)
                selectedMode = 3;
        }

        const char* sharpenModes[] = { Tr("Upscaler's default"), "RCAS", Tr("Depth Aware (RCAS)"), Tr("Depth Aware (DAS)") };

        if (ImGui::Combo(TrL("Method"), &selectedMode, sharpenModes, IM_ARRAYSIZE(sharpenModes)))
        {
            if (selectedMode == 0)
            {
                config->RcasEnabled = false;
            }
            else
            {
                config->RcasEnabled = true;
                if (selectedMode == 1)
                    Config::Instance()->SharpnessShader = SharpenShader::RCAS;
                else if (selectedMode == 2)
                    Config::Instance()->SharpnessShader = SharpenShader::DepthAware;
                else if (selectedMode == 3)
                    Config::Instance()->SharpnessShader = SharpenShader::LocalContrastDepthAware;
            }
        }

        ShowTooltip("Select the sharpening filter to apply.\n\n"
                    "Upscaler's default: Uses the sharpening provided by the upscaler.\n"
                    "RCAS: AMD's RCAS, modified for Contrast and MAS.\n"
                    "Depth Aware: Smarter sharpening with fewer artifacts (heavier).\n\n"
                    "By default uses a sharpening value provided by the game.\n"
                    "Select 'Override' under 'Sharpness' and adjust the slider to change it.");

        // Only show MAS and Advanced parameters if a custom sharpness mode is active
        if (selectedMode != 0)
        {
            ImGui::Spacing();

            if (bool overrideMotionSharpness = config->MotionSharpnessEnabled.value_or_default();
                IlionTheme::Checkbox(TrL("Enable Motion Adaptive Sharpness"), &overrideMotionSharpness))
                config->MotionSharpnessEnabled = overrideMotionSharpness;
            ShowTooltip("Enables sharpness adjustments according to the motion");

            if (Config::Instance()->SharpnessShader.value_or_default() != SharpenShader::RCAS)
            {
                if (bool overrideMSDebug = config->MotionSharpnessDebug.value_or_default();
                    IlionTheme::Checkbox(TrL("DA + MAS Debug"), &overrideMSDebug))
                    config->MotionSharpnessDebug = overrideMSDebug;

                ShowTooltip("Enable DA + MAS debug views\n"
                            "Blue tint for DA detected edges\n\n"
                            "More red areas will have more sharpness applied\n"
                            "Green areas will get reduced sharpness");

                if (auto ch = ScopedCollapsingHeader(TrL("Advanced DA Parameters")); ch.IsHeaderOpen())
                {
                    ScopedIndent indent {};
                    ImGui::Spacing();

                    if (bool clamp = config->DAClampOutput.value_or(false); IlionTheme::Checkbox(TrL("Clamp Output"), &clamp))
                    {
                        if (clamp)
                            config->DAClampOutput = true;
                        else
                            config->DAClampOutput.reset();
                    }

                    ShowTooltip("Clamps the final image to the [0, 1] range.\n\n"
                                "Prevents overshoot artifacts such as bright halos or negative colors.\n"
                                "Recommended for LDR pipelines; optional for HDR depending on tone-mapping.\n\n"
                                "When not set OptiScaler controls it via upscalers HDR flag");

                    if (currentFeature->DepthLinear())
                    {
                        float depthBias = config->DADepthBias.value_or(0.0015f);
                        if (IlionTheme::SliderFloat(TrL("Depth Bias"), &depthBias, 0.005f, 0.03f, "%.4f"))
                            config->DADepthBias = depthBias;

                        ShowTooltip("Ignores small depth differences before edge detection.\n\n"
                                    "Higher values reduce flickering and noise from minor depth changes, but may "
                                    "soften real geometry edges.\n"
                                    "Lower values preserve fine detail but can cause unstable or noisy edge "
                                    "detection.");

                        float depthScale = config->DADepthScale.value_or(250.0f);
                        if (IlionTheme::SliderFloat(TrL("Depth Scale"), &depthScale, 100.0f, 600.0f, "%.1f"))
                            config->DADepthScale = depthScale;

                        ShowTooltip("Controls how strongly sharpening is reduced across depth edges.\n\n"
                                    "Higher values more aggressively prevent sharpening across object boundaries "
                                    "(reduces halos).\n"
                                    "Lower values allow more sharpening to pass across edges (sharper but "
                                    "riskier).");
                    }
                    else
                    {
                        float depthBias = config->DADepthBias.value_or(0.001f);
                        if (IlionTheme::SliderFloat(TrL("Depth Bias"), &depthBias, 0.0001f, 0.003f, "%.4f"))
                            config->DADepthBias = depthBias;

                        ShowTooltip("Ignores small depth differences before edge detection.\n\n"
                                    "Higher values reduce flickering and noise from minor depth changes, but may "
                                    "soften real geometry edges.\n"
                                    "Lower values preserve fine detail but can cause unstable or noisy edge "
                                    "detection.");

                        float depthScale = config->DADepthScale.value_or(35.0f);
                        if (IlionTheme::SliderFloat(TrL("Depth Scale"), &depthScale, 25.0f, 400.0f, "%.1f"))
                            config->DADepthScale = depthScale;

                        ShowTooltip("Controls how strongly sharpening is reduced across depth edges.\n\n"
                                    "Higher values more aggressively prevent sharpening across object boundaries "
                                    "(reduces halos).\n"
                                    "Lower values allow more sharpening to pass across edges (sharper but "
                                    "riskier).");
                    }

                    if (IlionTheme::Button(TrL("Reset Depth Values")))
                    {
                        config->DADepthBias.reset();
                        config->DADepthScale.reset();
                    }
                }
            }
            else
            {
                if (bool contrastEnabled = config->ContrastEnabled.value_or_default();
                    IlionTheme::Checkbox(TrL("Contrast Enabled"), &contrastEnabled))
                    config->ContrastEnabled = contrastEnabled;

                ShowTooltip("Controls sharpness at high contrast areas.");

                ImGui::BeginDisabled(!config->ContrastEnabled.value_or_default());

                float contrast = config->Contrast.value_or_default();
                if (IlionTheme::SliderFloat(TrL("Contrast##RCAS"), &contrast, -2.0f, 2.0f, "%.2f"))
                    config->Contrast = contrast;

                ShowTooltip("Positive values decrease sharpness at high contrast areas.\n"
                            "Negative values increase sharpness at high contrast areas.");

                ImGui::EndDisabled();
            }

            ImGui::Spacing();

            if (auto ch = ScopedCollapsingHeader(TrL("Motion Adaptive Sharpness##2")); ch.IsHeaderOpen())
            {
                ScopedIndent indent {};
                ImGui::Spacing();

                ImGui::BeginDisabled(!config->MotionSharpnessEnabled.value_or_default());

                if (Config::Instance()->SharpnessShader.value_or_default() == SharpenShader::RCAS)
                {
                    if (bool overrideMSDebug = config->MotionSharpnessDebug.value_or_default();
                        IlionTheme::Checkbox(TrL("MAS Debug"), &overrideMSDebug))
                        config->MotionSharpnessDebug = overrideMSDebug;
                    ShowTooltip("Areas that are more red will have more sharpness applied\n"
                                "Green areas will get reduced sharpness");
                }

                float motionSharpness = config->MotionSharpness.value_or_default();
                if (IlionTheme::SliderFloat(TrL("MotionSharpness"), &motionSharpness, -1.0f, 1.0f, "%.3f"))
                    config->MotionSharpness = motionSharpness;

                ShowTooltip("Maximum amount of sharpness that motion can add or remove.\n\n"
                            "Negative values reduce sharpening in motion (recommended).\n"
                            "Positive values increase sharpening in motion.\n\n"
                            "The final adjustment scales with motion and is capped at this value.");

                float motionThreshod = config->MotionThreshold.value_or_default();
                if (IlionTheme::SliderFloat(TrL("MotionThreshod"), &motionThreshod, 0.0f, 100.0f, "%.2f"))
                    config->MotionThreshold = motionThreshod;

                ShowTooltip("Minimum motion required before motion-based sharpening adjustment begins.\n\n"
                            "Higher values ignore small movements (more stable).\n"
                            "Lower values react to subtle motion (more sensitive).");

                float motionScale = config->MotionScaleLimit.value_or_default();
                if (IlionTheme::SliderFloat(TrL("MotionRange"), &motionScale, 0.01f, 100.0f, "%.2f"))
                    config->MotionScaleLimit = motionScale;

                ShowTooltip("Defines the motion range over which the effect ramps from zero to full strength.\n\n"
                            "Values above the threshold are mapped into this range.\n"
                            "Larger values make the response smoother and more gradual.\n"
                            "Smaller values make the effect react more quickly and aggressively.");

                ImGui::EndDisabled();

                ImGui::Spacing();
                ImGui::Spacing();
            }
        }
    }
}

void MenuCommon::RenderUpscaleRatioSettings(RenderMenuContext& ctx)
{
    auto& state = ctx.state;
    auto config = ctx.config;
    auto& currentFeature = ctx.currentFeature;
    auto& menuResScale = ctx.menuResScale;

    // UPSCALE RATIO OVERRIDE -----------------

    auto minSliderLimit = config->ExtendedLimits.value_or_default() ? 0.1f : 1.0f;
    auto maxSliderLimit = config->ExtendedLimits.value_or_default() ? 6.0f : 3.0f;

    SectionTitle("Upscale Ratio Override");

    if (ImGui::BeginTable("ratioOverride", 2, ImGuiTableFlags_SizingFixedFit))
    {
        ImGui::TableNextColumn();

        if (bool upOverride = config->UpscaleRatioOverrideEnabled.value_or_default();
            IlionTheme::Checkbox(TrL("Override all"), &upOverride))
        {
            config->UpscaleRatioOverrideEnabled = upOverride;

            if (upOverride)
                config->QualityRatioOverrideEnabled = false;
        }
        ShowTooltip("Overrides every upscaler preset with the set value\n\n"
                    "1.5x on a 1080p screen means an internal res of 720p\n"
                    "1080 / 1.5 = 720");

        ImGui::TableNextColumn();

        if (bool qOverride = config->QualityRatioOverrideEnabled.value_or_default();
            IlionTheme::Checkbox(TrL("Override per quality preset"), &qOverride))
        {
            config->QualityRatioOverrideEnabled = qOverride;

            if (qOverride)
                config->UpscaleRatioOverrideEnabled = false;
        }

        ShowTooltip("Lets you override each preset's ratio individually\n"
                    "Note that not every game supports every quality preset\n\n"
                    "1.5x on a 1080p screen means internal resolution of 720p\n"
                    "1080 / 1.5 = 720");

        ImGui::EndTable();
    }

    if (config->UpscaleRatioOverrideEnabled.value_or_default())
    {
        float urOverride = config->UpscaleRatioOverrideValue.value_or_default();
        IlionTheme::SliderFloat(TrL("All Ratios"), &urOverride, minSliderLimit, maxSliderLimit, "%.3f");
        config->UpscaleRatioOverrideValue = urOverride;
    }

    if (config->QualityRatioOverrideEnabled.value_or_default())
    {
        float qDlaa = config->QualityRatio_DLAA.value_or_default();
        if (IlionTheme::SliderFloat(TrL("DLAA"), &qDlaa, minSliderLimit, maxSliderLimit, "%.3f"))
            config->QualityRatio_DLAA = qDlaa;

        float qUq = config->QualityRatio_UltraQuality.value_or_default();
        if (IlionTheme::SliderFloat(TrL("Ultra Quality"), &qUq, minSliderLimit, maxSliderLimit, "%.3f"))
            config->QualityRatio_UltraQuality = qUq;

        float qQ = config->QualityRatio_Quality.value_or_default();
        if (IlionTheme::SliderFloat(TrL("Quality"), &qQ, minSliderLimit, maxSliderLimit, "%.3f"))
            config->QualityRatio_Quality = qQ;

        float qB = config->QualityRatio_Balanced.value_or_default();
        if (IlionTheme::SliderFloat(TrL("Balanced"), &qB, minSliderLimit, maxSliderLimit, "%.3f"))
            config->QualityRatio_Balanced = qB;

        float qP = config->QualityRatio_Performance.value_or_default();
        if (IlionTheme::SliderFloat(TrL("Performance"), &qP, minSliderLimit, maxSliderLimit, "%.3f"))
            config->QualityRatio_Performance = qP;

        float qUp = config->QualityRatio_UltraPerformance.value_or_default();
        if (IlionTheme::SliderFloat(TrL("Ultra Performance"), &qUp, minSliderLimit, maxSliderLimit, "%.3f"))
            config->QualityRatio_UltraPerformance = qUp;
    }
}

void MenuCommon::RenderOutputScalingSettings(RenderMenuContext& ctx)
{
    auto& state = ctx.state;
    auto config = ctx.config;
    auto& currentFeature = ctx.currentFeature;
    auto& menuResScale = ctx.menuResScale;

    // OUTPUT SCALING -----------------------------
    // if (state.api == DX12 || state.api == DX11)
    {
        // if motion vectors are not display size
        ImGui::BeginDisabled(!currentFeature->LowResMV() &&
                             currentFeature->RenderWidth() != currentFeature->DisplayWidth());

        SectionTitle("Output Scaling");

        float defaultRatio = 1.5f;

        if (_ssRatio == 0.0f)
        {
            _ssRatio = config->OutputScalingMultiplier.value_or(defaultRatio);
            _ssEnabled = config->OutputScalingEnabled.value_or_default();
            _ssDownsampler = config->OutputScalingDownscaler.value_or_default();
        }

        ImGui::BeginDisabled((currentBackend == Upscaler::XeSS || currentBackend == Upscaler::DLSS ||
                              currentBackend == Upscaler::DLSS_on12) &&
                             currentFeature->RenderWidth() > currentFeature->DisplayWidth());
        IlionTheme::Checkbox(TrL("Enable"), &_ssEnabled);
        ImGui::EndDisabled();

        ShowTooltip("Upscales the image internally to a higher output resolution\n"
                    "then downscales it back to your display resolution\n\n"
                    "Values <1.0 make the upscaler cheaper\n"
                    "Values >1.0 make image sharper at the cost of performance\n\n"
                    "If greyed out, please check Git Wiki - Unreal Engine tweaks\n\n"
                    "Target res and total ratio at the bottom (max. total 3.0!)");

        ImGui::SameLine(0.0f, 6.0f);

        ImGui::BeginDisabled(!_ssEnabled);
        {
            ImGui::PushItemWidth(105.0f * menuResScale);

            // clang-format off
                    std::vector<MenuOption<Scaler>> ds_options = {
                        { Scaler::FSR1, "FSR1",
                            "Default option.\nGood enough image quality and very fast." },
                        { Scaler::Bicubic, "Bicubic",
                            "Fastest traditional option.\nProduces a very soft/blurry image, but might be okay for downscaling." },
                        { Scaler::CatmullRom, "Catmull-Rom",
                            "Designed primarily for downscaling.\nRetains good contrast with minimal artefacts, but softer than Lanczos." },
                        { Scaler::Lanczos2, "Lanczos2",
                            "Lighter and faster than Lanczos3.\nLess prone to ringing artefacts, but slightly blurrier." },
                        { Scaler::Lanczos3, "Lanczos3",
                            "Heavier version of Lanczos2.\nOffers the sharpest image, but is the most prone to ringing.\nConsidered the best along with Kaiser3." },
                        { Scaler::Kaiser2, "Kaiser2",
                            "Similar to Lanczos2.\nSmoother and less prone to artefacts than Lanczos, but slightly blurrier." },
                        { Scaler::Kaiser3, "Kaiser3",
                            "Similar to Lanczos3.\nFar less prone to artefacting than Lanczos3, but much heavier on the GPU.\nConsidered the best along with Lanczos3." },
                        { Scaler::Magic, "MAGIC",
                            "Specialised to prevent artifacts.\nEliminates harsh halos for a natural look, but can appear slightly soft." }
                    };
            // clang-format on

            const bool isUpsampleRatio = _ssRatio < 1.0f;
            const std::string disabledReason = Tr("Only FSR1 and Bicubic are supported when Ratio is below 1.0.");

            for (auto& opt : ds_options)
            {
                if (isUpsampleRatio && opt.value > Scaler::Bicubic)
                    opt.set_disabled(true, opt.tooltip + "\n\n" + disabledReason);
            }

            if (isUpsampleRatio && _ssDownsampler > Scaler::Bicubic)
                _ssDownsampler = Scaler::FSR1;

            PopulateCombo(TrL("Downscaler"), _ssDownsampler, ds_options);

            ImGui::PopItemWidth();
        }
        ImGui::EndDisabled();

        bool applyEnabled = _ssEnabled != config->OutputScalingEnabled.value_or_default() ||
                            _ssRatio != config->OutputScalingMultiplier.value_or(defaultRatio) ||
                            _ssDownsampler != config->OutputScalingDownscaler.value_or_default();

        ImGui::BeginDisabled(!applyEnabled);
        if (IlionTheme::Button(TrL("Apply Change")))
        {
            config->OutputScalingEnabled = _ssEnabled;
            config->OutputScalingMultiplier = _ssRatio;

            if (_ssRatio < 1.0f && _ssDownsampler > Scaler::Bicubic)
                _ssDownsampler = Scaler::FSR1;

            config->OutputScalingDownscaler = _ssDownsampler;

            const bool usesDlssd = currentFeature->GetUpscalerType() == Upscaler::DLSSD;
            if (usesDlssd)
                state.newBackend = Upscaler::DLSSD;
            else
                state.newBackend = currentBackend;

            MARK_ALL_BACKENDS_CHANGED();
        }
        ImGui::EndDisabled();

        ImGui::BeginDisabled(!_ssEnabled || currentFeature->RenderWidth() > currentFeature->DisplayWidth());
        IlionTheme::SliderFloat(TrL("Ratio"), &_ssRatio, 0.5f, 3.0f, "%.2f");
        ImGui::EndDisabled();

        if (currentFeature != nullptr && !currentFeature->IsFrozen())
        {
            ImGui::TextWrapped(Tr("Output Scaling is %s, Target Res: %dx%d (%.2f)\nJitter Count: %d"),
                        config->OutputScalingEnabled.value_or_default() ? "ENABLED" : "DISABLED",
                        (uint32_t) (currentFeature->DisplayWidth() * _ssRatio),
                        (uint32_t) (currentFeature->DisplayHeight() * _ssRatio),
                        ((float) currentFeature->DisplayWidth() * _ssRatio) / (float) currentFeature->RenderWidth(),
                        currentFeature->JitterCount());
        }

        ImGui::EndDisabled();
    }
}

void MenuCommon::RenderInitFlagsSettings(RenderMenuContext& ctx)
{
    auto& state = ctx.state;
    auto config = ctx.config;
    auto& currentFeature = ctx.currentFeature;

    if (currentFeature == nullptr || currentFeature->IsFrozen())
        return;

    // INIT -----------------------------
    SectionTitle("Init Flags");

    if (ImGui::BeginTable("init", 2, ImGuiTableFlags_SizingStretchSame))
    {
        ImGui::TableNextColumn();

        // AutoExposure is always enabled for XeSS with native Dx11
        bool autoExposureDisabled = state.api == API::DX11 && currentBackend == Upscaler::XeSS;
        ImGui::BeginDisabled(autoExposureDisabled);

        if (bool autoExposure = currentFeature->AutoExposure(); IlionTheme::Checkbox(TrL("Auto Exposure"), &autoExposure))
        {
            config->AutoExposure = autoExposure;
            ReInitUpscaler();
        }
        ShowTooltip("Some Unreal Engine games need this\n\n"
                    "Try using if colours flickering or\n"
                    "objects have ghosting trails");
        ShowResetButton(&config->AutoExposure, "R");

        ImGui::EndDisabled();

        ImGui::TableNextColumn();
        if (bool depth = currentFeature->DepthInverted(); IlionTheme::Checkbox(TrL("Depth Inverted"), &depth))
        {
            config->DepthInverted = depth;
            ReInitUpscaler();
        }
        ShowTooltip("You shouldn't need to change it");
        ShowResetButton(&config->DepthInverted, "R##2");

        ImGui::TableNextColumn();
        if (bool hdr = currentFeature->IsHdr(); IlionTheme::Checkbox(TrL("HDR"), &hdr))
        {
            config->HDR = hdr;
            ReInitUpscaler();
        }
        ShowTooltip("Might help with purple hue in some games");
        ShowResetButton(&config->HDR, "R##1");

        ImGui::TableNextColumn();
        if (bool jitter = currentFeature->JitteredMV(); IlionTheme::Checkbox(TrL("Jitter Cancellation"), &jitter))
        {
            config->JitterCancellation = jitter;
            ReInitUpscaler();
        }
        ShowTooltip("Fix for games that send motion data with preapplied jitter");
        ShowResetButton(&config->JitterCancellation, "R##3");

        ImGui::TableNextColumn();
        if (bool mv = !currentFeature->LowResMV(); IlionTheme::Checkbox(TrL("Display Res. MV"), &mv))
        {
            config->DisplayResolution = mv;

            // Disable output scaling when
            // Display res MV is active
            if (mv)
            {
                config->OutputScalingEnabled = false;
                _ssEnabled = false;
            }

            ReInitUpscaler();
        }
        ShowTooltip("Mostly a fix for Unreal Engine games\n"
                    "Top left part of the screen will be blurry");
        ShowResetButton(&config->DisplayResolution, "R##4");

        ImGui::TableNextColumn();
        auto accessToReactiveMask = currentFeature->AccessToReactiveMask();
        ImGui::BeginDisabled(!accessToReactiveMask);

        bool canUseReactiveMask =
            accessToReactiveMask && currentBackend != Upscaler::DLSS && currentBackend != Upscaler::DLSS_on12 &&
            (currentBackend != Upscaler::XeSS || currentFeature->Version() >= feature_version { 2, 0, 1 });

        bool disableReactiveMask = config->DisableReactiveMask.value_or(!canUseReactiveMask);

        if (IlionTheme::Checkbox(TrL("Disable Reactive Mask"), &disableReactiveMask))
        {
            config->DisableReactiveMask = disableReactiveMask;

            if (currentBackend == Upscaler::XeSS)
            {
                state.newBackend = currentBackend;
                MARK_ALL_BACKENDS_CHANGED();
            }
        }

        ImGui::EndDisabled();

        if (accessToReactiveMask)
            ShowTooltip("Allows the use of a Reactive mask\n"
                        "Keep in mind that a Reactive mask sent to DLSS\n"
                        "will not produce a good image in combination with FSR/XeSS");
        else
            ShowTooltip("Option disabled because the game doesn't provide a Reactive mask");

        ImGui::EndTable();
    }

    if (currentFeature->AccessToReactiveMask())
    {
        ImGui::BeginDisabled(config->DisableReactiveMask.value_or(false));

        auto useAsTransparency = config->FsrUseMaskForTransparency.value_or_default();
        if (IlionTheme::Checkbox(TrL("Use Reactive Mask as Transparency Mask"), &useAsTransparency))
            config->FsrUseMaskForTransparency = useAsTransparency;

        ImGui::EndDisabled();
    }

    if (currentFeature->AccessToReactiveMask() && currentBackend != Upscaler::DLSS &&
        currentBackend != Upscaler::DLSS_on12)
    {
        ImGui::BeginDisabled(config->DisableReactiveMask.value_or(currentBackend == Upscaler::XeSS));

        bool binaryMask = state.api == Vulkan || currentBackend == Upscaler::XeSS;
        auto defaultBias = binaryMask ? 0.0f : 0.45f;
        auto maskBias = config->DlssReactiveMaskBias.value_or(defaultBias);

        if (!binaryMask)
        {
            if (IlionTheme::SliderFloat(TrL("React. Mask Bias"), &maskBias, 0.0f, 0.9f, "%.2f"))
                config->DlssReactiveMaskBias = maskBias;

            ShowTooltip("Values above 0 activate usage of Reactive mask");
        }
        else
        {
            bool useRM = maskBias > 0.0f;
            if (IlionTheme::Checkbox(TrL("Use Binary Reactive Mask"), &useRM))
            {
                if (useRM)
                    config->DlssReactiveMaskBias = 0.45f;
                else
                    config->DlssReactiveMaskBias.reset();
            }
        }

        ImGui::EndDisabled();
    }
}

// ILION graphics enhancements ------------------------------------------------------------------------------------
// One row per control in a table: switch | name | slider | value | reset. The ILION themes show the value in the
// slider's own window, so their table has no value column. Changes are live, the pass reads the config every frame.
struct GfxRow
{
    const char* label;
    const char* tip;
    CustomOptional<bool>* enabled;
    CustomOptional<float>* value;
    float min;
    float max;
};

static void GfxTable(const char* id, const GfxRow* rows, int count)
{
    const bool valueColumn = !IlionTheme::industrial;

    float nameWidth = 0.0f;
    for (int i = 0; i < count; i++)
        nameWidth = std::max(nameWidth, ImGui::CalcTextSize(Tr(rows[i].label)).x);

    const float resetWidth = ImGui::CalcTextSize(Tr("Reset")).x + ImGui::GetStyle().FramePadding.x * 2.0f;
    const float valueWidth = ImGui::CalcTextSize("+0.00").x;

    // The slider keeps at least five frame heights: a long name (Spanish, big font) wraps instead
    const ImGuiStyle& style = ImGui::GetStyle();
    const float fixed = ImGui::GetFrameHeight() * 1.8f + resetWidth + (valueColumn ? valueWidth : 0.0f) +
                        style.CellPadding.x * 2.0f * (valueColumn ? 5.0f : 4.0f);
    const float sliderMin = ImGui::GetFrameHeight() * 5.0f;
    nameWidth = std::max(ImGui::GetFrameHeight() * 3.0f,
                         std::min(nameWidth, ImGui::GetContentRegionAvail().x - fixed - sliderMin));

    if (!ImGui::BeginTable(id, valueColumn ? 5 : 4, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoSavedSettings))
        return;

    ImGui::TableSetupColumn("on", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFrameHeight() * 1.8f);
    ImGui::TableSetupColumn("name", ImGuiTableColumnFlags_WidthFixed, nameWidth);
    ImGui::TableSetupColumn("slider", ImGuiTableColumnFlags_WidthStretch);
    if (valueColumn)
        ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthFixed, valueWidth);
    ImGui::TableSetupColumn("reset", ImGuiTableColumnFlags_WidthFixed, resetWidth);

    for (int i = 0; i < count; i++)
    {
        const GfxRow& row = rows[i];
        ImGui::PushID(row.label);
        ImGui::TableNextRow();

        ImGui::TableNextColumn();
        bool on = row.enabled->value_or_default();
        if (IlionTheme::Checkbox("##on", &on))
            *row.enabled = on;
        if (ImGui::IsItemHovered())
            WrappedTooltip("%s", Tr("Switches this adjustment on or off, its value is kept"));

        ImGui::BeginDisabled(!on);

        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        ImGui::TextWrapped("%s", Tr(row.label));
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            WrappedTooltip("%s", Tr(row.tip));

        ImGui::TableNextColumn();
        float v = row.value->value_or_default();
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (IlionTheme::SliderFloat("##slider", &v, row.min, row.max, valueColumn ? "" : "%+.2f"))
            *row.value = v;
        if (ImGui::IsItemHovered())
            WrappedTooltip("%s", Tr(row.tip));

        if (valueColumn)
        {
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::Text("%+.2f", v);
        }

        ImGui::TableNextColumn();
        if (IlionTheme::Button(TrL("Reset"), ImVec2(-FLT_MIN, 0.0f)))
            row.value->reset();

        ImGui::EndDisabled();
        ImGui::PopID();
    }

    ImGui::EndTable();
}

void MenuCommon::RenderGraphicsColorSettings(RenderMenuContext& ctx)
{
    auto config = ctx.config;

    SectionTitle("Colour and image");

    bool enabled = config->GfxEnabled.value_or_default();
    if (IlionTheme::Checkbox(TrL("Enable graphics enhancements"), &enabled))
        config->GfxEnabled = enabled;
    ShowTooltip("One colour pass after the upscaler and the sharpening, before the game's HUD.\n"
                "Off, or with every value neutral, the pass is not run: the picture is untouched.");

    // What the pass is doing now
    const bool upscaling = ctx.currentFeature != nullptr && !ctx.currentFeature->IsFrozen();

    if (!upscaling)
        WrappedText(IlionTheme::TextDim, Tr("Applied only while OptiScaler upscales the game"));
    else if (enabled && ImageAdjust::bypass)
        WrappedText(IlionTheme::LampAmber, Tr("Showing the original picture"));
    else if (ImageAdjust_Common::ShouldRun())
        WrappedText(IlionTheme::LampGreen, Tr("Colour pass running"));
    else
        WrappedText(IlionTheme::TextDim, Tr("Colour pass not running: off or every value neutral"));

    if (upscaling && ctx.currentFeature->IsHdr())
        WrappedText(IlionTheme::TextDim, Tr("HDR picture: brightness and contrast work on linear light"));

    ImGui::Spacing();
    ImGui::BeginDisabled(!enabled);

    const GfxRow rows[] = {
        { "Temperature", "Below 0 cooler (blue), above 0 warmer (orange). Brightness is kept.",
          &config->GfxTemperatureEnabled, &config->GfxTemperature, -1.0f, 1.0f },
        { "Brightness", "Exposure, up to one stop darker or brighter.", &config->GfxBrightnessEnabled,
          &config->GfxBrightness, -1.0f, 1.0f },
        { "Contrast", "Curve around mid grey. In SDR black and white stay where they are.",
          &config->GfxContrastEnabled, &config->GfxContrast, -1.0f, 1.0f },
        { "Saturation", "Strength of every colour alike. -1 is black and white.", &config->GfxSaturationEnabled,
          &config->GfxSaturation, -1.0f, 1.0f },
        { "Vibrance", "Raises dull colours more than the ones already strong, so skin and skies don't burn.",
          &config->GfxVibranceEnabled, &config->GfxVibrance, -1.0f, 1.0f },
    };
    GfxTable("##gfxColour", rows, (int) std::size(rows));

    ImGui::Spacing();
    if (IlionTheme::Button(TrL("Reset all")))
    {
        for (auto* value : { &config->GfxTemperature, &config->GfxBrightness, &config->GfxContrast,
                             &config->GfxSaturation, &config->GfxVibrance, &config->GfxLocalContrast })
            value->reset();

        for (auto* on : { &config->GfxTemperatureEnabled, &config->GfxBrightnessEnabled, &config->GfxContrastEnabled,
                          &config->GfxSaturationEnabled, &config->GfxVibranceEnabled,
                          &config->GfxLocalContrastEnabled })
            on->reset();
    }
    ShowTooltip("Every value back to neutral; saved with Save Settings, for this game");

    ImGui::EndDisabled();
}

void MenuCommon::RenderGraphicsCompareSettings(RenderMenuContext& ctx)
{
    SectionTitle("Comparison");

    ImGui::BeginDisabled(!ctx.config->GfxEnabled.value_or_default());
    IlionTheme::Checkbox(TrL("Split screen"), &ImageAdjust::compareSplit);
    ShowTooltip("Left half of the picture without the adjustments, right half with them");
    SameLineIfFits(TrL("Show original"), 16.0f * ctx.menuResScale);
    IlionTheme::Checkbox(TrL("Show original"), &ImageAdjust::bypass);
    ShowTooltip("The whole picture without the adjustments, while it stays checked");
    ImGui::EndDisabled();
}

void MenuCommon::RenderGraphicsAdvancedSettings(RenderMenuContext& ctx)
{
    auto config = ctx.config;

    SectionTitle("Advanced adjustments");

    ImGui::BeginDisabled(!config->GfxEnabled.value_or_default());
    const GfxRow rows[] = {
        { "Local contrast (experimental)",
          "Raises detail against its surroundings. Strong values can draw halos round edges.",
          &config->GfxLocalContrastEnabled, &config->GfxLocalContrast, 0.0f, 1.0f },
    };
    GfxTable("##gfxAdvanced", rows, (int) std::size(rows));
    ImGui::EndDisabled();

    // Depth Aware (DAS) is a local contrast sharpening too, small radius: both together can stack halos
    if (config->GfxEnabled.value_or_default() && config->GfxLocalContrastEnabled.value_or_default() &&
        config->RcasEnabled.value_or_default() &&
        config->SharpnessShader.value_or_default() == SharpenShader::LocalContrastDepthAware)
        WrappedText(IlionTheme::LampAmber, Tr("With Depth Aware (DAS) sharpening halos can add up"));
    else
        WrappedText(IlionTheme::TextDim, Tr("Uses the switch of Colour and image; off by default"));
}

// ILION Intelligent Activation -----------------------------------------------------------------------------------
// A layer over fakenvapi's own choice, not a selector: Automatic and Off put volatile values on the existing
// options (never saved, the user's own values stay in the ini and come back in Manual); the choice of technology
// is fakenvapi's. The mode itself is saved and applied again at start (Config.cpp).
namespace IlionLatency
{
enum Mode : int
{
    Auto = 0,
    Manual = 1,
    Off = 2
};

// Without a saved choice: Manual when the original options were changed, Auto otherwise
inline Mode Current()
{
    auto config = Config::Instance();

    if (config->IlionLatencyMode.has_value())
        return (Mode) std::clamp(config->IlionLatencyMode.value(), 0, 2);

    const bool changed = config->FN_ForceLatencyFlex.value_for_config().has_value() ||
                         config->FN_ForceReflex.value_for_config().has_value() ||
                         config->ForceXeLL.value_for_config().has_value();
    return changed ? Manual : Auto;
}

// More than one generated frame: OptiScaler itself turns fakenvapi's Reflex off then (Reflex_Hooks)
inline bool MultiFrameGen(const State& state)
{
    return state.dlssgDetectedInterpolationCount > 1 || state.dlssgLastSetMode == sl::DLSSGMode::eDynamic;
}

inline void Apply(Mode mode)
{
    auto config = Config::Instance();
    const bool mfg = MultiFrameGen(State::Instance());
    config->IlionLatencyMode = (int) mode;

    if (mode == Auto)
    {
        config->FN_ForceLatencyFlex.set_volatile_value(false);
        if (!mfg)
            config->FN_ForceReflex.set_volatile_value(ForceReflex::InGame);
    }
    else if (mode == Off)
    {
        config->FN_ForceReflex.set_volatile_value(ForceReflex::ForceDisable);
    }
    else
    {
        // Manual: the user's saved values back. With more generated frames OptiScaler's own Force Disable stays.
        config->FN_ForceLatencyFlex = config->FN_ForceLatencyFlex.value_for_config();
        if (!mfg)
            config->FN_ForceReflex = config->FN_ForceReflex.value_for_config();
    }
}

inline const char* TechName(LowLatencyMode mode)
{
    switch (mode)
    {
    case LowLatencyMode::AntiLag2:
        return "AMD Anti-Lag 2";
    case LowLatencyMode::XeLL:
        return "Intel XeLL";
    case LowLatencyMode::LatencyFlex:
        return "LatencyFleX";
    case LowLatencyMode::AntiLagVk:
        return "AMD Anti-Lag (Vulkan)";
    case LowLatencyMode::Reflex:
        return "NVIDIA Reflex";
    default:
        return nullptr;
    }
}

inline const char* SwapchainApiName(API api)
{
    return api == DX11 ? "DX11" : api == DX12 ? "DX12" : api == Vulkan ? "Vulkan" : "?";
}
} // namespace IlionLatency

void MenuCommon::RenderIntelligentLatency(RenderMenuContext& ctx)
{
    using namespace IlionLatency;
    auto& state = ctx.state;
    auto config = ctx.config;

    SectionTitle("Intelligent Activation");

    // Mode: three keys on one line
    const Mode mode = Current();
    const char* labels[] = { "Automatic##IlionLat", "Manual##IlionLat", "Off##IlionLat" };
    const char* tips[] = { "ILION lets OptiScaler pick the compatible technology",
                           "The original options below stay in your hands",
                           "Asks OptiScaler to turn latency reduction off" };

    for (int i = 0; i < 3; i++)
    {
        if (i > 0)
            ImGui::SameLine();

        const bool selected = mode == i;
        if (selected)
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        if (IlionTheme::Button(TrL(labels[i])) && !selected)
            Apply((Mode) i);
        if (selected)
            ImGui::PopStyleColor();
        ShowTooltip(tips[i]);
    }

    // What fakenvapi and the game report
    const auto vendor = IdentifyGpu::getPrimaryGpu().vendorId;
    const bool fakenvapiInUse = fakenvapi::isUsingAsMainNvapi();
    const LowLatencyMode tech = fakenvapiInUse ? fakenvapi::getCurrentMode() : LowLatencyMode::None;
    const bool enabled = fakenvapiInUse && fakenvapi::isLowLatencyActive();
    const bool mfg = MultiFrameGen(state);
    const char* api = SwapchainApiName(state.swapchainApi);
    const std::optional<bool> gameRequest = ReflexHooks::gameLowLatencyRequest();
    const bool xellForced = config->ForceXeLL.value_or_default() || state.activeFgOutput == FGOutput::XeFG;
    const bool reflexForcedOff = config->FN_ForceReflex.value_or_default() == ForceReflex::ForceDisable;

    // Reflex markers from the game moving on: the frames really go through the low latency path
    static uint64_t lastFrameId = 0;
    static auto lastMove = std::chrono::steady_clock::time_point {};
    const auto now = std::chrono::steady_clock::now();
    if (state.reflexFrameId != lastFrameId)
    {
        lastFrameId = state.reflexFrameId;
        lastMove = now;
    }
    const bool markersMoving = lastFrameId != 0 && now - lastMove < std::chrono::seconds(1);

    const char* name = TechName(tech);
    std::string reason;
    const char* status = nullptr;
    const char* note = nullptr; // what keeps the mode from applying fully, or what to do
    bool running = false;
    ImVec4 statusColour = IlionTheme::TextDim;

    if (!fakenvapiInUse)
    {
        if (vendor == VendorId::Nvidia)
        {
            name = "NVIDIA Reflex";
            status = Tr("Handled by the game");
            reason = Tr("Native Reflex: OptiScaler doesn't select it");
            if (mode == Off)
                note = Tr("Native Reflex is controlled by the game: Off can't turn it off");
        }
        else
        {
            status = Tr("Not compatible");
            reason = Tr("OptiScaler's nvapi is not in use: no Reflex input from the game");
        }
    }
    else if (tech == LowLatencyMode::None)
    {
        status = mode == Off ? Tr("Off") : Tr("Available");
        reason = mfg ? Tr("Off while more than one frame is generated") : Tr("Waiting for the game's Reflex request");
    }
    else
    {
        // A technology answers: fakenvapi initialised it. Forced XeLL ignores Force Reflex (ll_xell.cpp).
        const bool active = enabled || (tech == LowLatencyMode::XeLL && xellForced);
        running = active && markersMoving;

        if (running)
            status = Tr("Running");
        else if (active)
            status = Tr("Enabled, no frame markers yet");
        else if (mode == Off || reflexForcedOff)
            status = Tr("Off");
        else
            status = Tr("Initialised");

        statusColour = running ? IlionTheme::LampGreen : active ? IlionTheme::LampAmber : IlionTheme::TextDim;

        if (mode == Off && active)
            note = Tr("Forced XeLL (XeFG or Force XeLL) keeps working: Off can't turn it off");
        else if (!active && mode != Off && !reflexForcedOff && gameRequest.has_value() && !*gameRequest)
            note = Tr("Turn Reflex on in the game's options, or Force Enable in Manual");
        else if (!active && mode != Off && reflexForcedOff && mfg)
            note = Tr("OptiScaler turns Reflex off while more than one frame is generated");

        if (tech == LowLatencyMode::AntiLag2)
            reason = StrFmt(Tr("Chosen for AMD GPU, %s and Reflex input"), api);
        else if (tech == LowLatencyMode::AntiLagVk)
            reason = Tr("Chosen for AMD GPU, Vulkan and Reflex input");
        else if (tech == LowLatencyMode::XeLL)
            reason = xellForced ? Tr("Forced by XeFG or Force XeLL")
                                : StrFmt(Tr("Chosen for %s: Anti-Lag 2 not available"), api);
        else if (tech == LowLatencyMode::LatencyFlex)
            reason = config->FN_ForceLatencyFlex.value_or_default() ? Tr("Forced with Force LatencyFlex")
                     : mfg ? Tr("Anti-Lag 2 and XeLL are not used with multi frame generation")
                           : Tr("General fallback: Anti-Lag 2 and XeLL not available");
    }

    // What the game asked for, from its own SetSleepMode calls
    const char* request = !gameRequest.has_value() ? Tr("No request from the game")
                          : *gameRequest           ? Tr("Turned on by the game")
                                                   : Tr("Turned off by the game");

    if (ImGui::BeginTable("##ilionLatency", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings))
    {
        float keyWidth = 0.0f;
        for (const char* key : { "Technology", "State", "Game", "Reason" })
            keyWidth = std::max(keyWidth, ImGui::CalcTextSize(Tr(key)).x);

        ImGui::TableSetupColumn("key", ImGuiTableColumnFlags_WidthFixed, keyWidth);
        ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);

        auto row = [](const char* key, const ImVec4& colour, const char* value)
        {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextColored(IlionTheme::TextDim, "%s", Tr(key));
            ImGui::TableNextColumn();
            WrappedText(colour, value);
        };

        row("Technology", IlionTheme::Text, name != nullptr ? name : Tr("None"));
        row("State", statusColour, status);
        if (fakenvapiInUse || gameRequest.has_value())
            row("Game", IlionTheme::TextDim, request);
        row("Reason", IlionTheme::TextDim, reason.c_str());
        ImGui::EndTable();
    }

    if (note != nullptr)
        WrappedText(IlionTheme::LampAmber, note);

    // Changes that only apply at the next start
    if (config->ForceXeLL.value_or_default() && mode == Auto)
        WrappedText(IlionTheme::LampAmber, Tr("Force XeLL stays on in Automatic; changing it requires restarting the game"));

    if (running)
        WrappedText(IlionTheme::TextDim, Tr("Latency not measured here"));

    // The technical diagnosis is for Advanced; mode, state and reason stay above
    if (!EssentialView())
    if (auto ch = ScopedCollapsingHeader(TrL("Diagnostics")); ch.IsHeaderOpen())
    {
        ScopedIndent indent {};
        const char* yes = Tr("yes");
        const char* no = Tr("no");
        const char* gpu = vendor == VendorId::AMD      ? "AMD"
                          : vendor == VendorId::Nvidia ? "NVIDIA"
                          : vendor == VendorId::Intel  ? "Intel"
                                                       : "?";
        ImGui::Text(Tr("GPU: %s   API: %s"), gpu, api);
        ImGui::Text(Tr("OptiScaler nvapi (fakenvapi): %s"), fakenvapiInUse ? yes : no);
        ImGui::Text(Tr("Low latency enabled: %s"), enabled ? yes : no);
        ImGui::Text(Tr("Reflex frame id: %llu (%s)"), (unsigned long long) state.reflexFrameId,
                    markersMoving ? Tr("moving") : Tr("still"));
        ImGui::Text(Tr("More than one generated frame: %s"), mfg ? yes : no);

        // fakenvapi's own order (nvapi/fakenvapi/low_latency_d3d.cpp, low_latency_vk.cpp), not ILION's
        ImGui::Spacing();
        WrappedText(IlionTheme::Text, Tr("fakenvapi's order, picked when the game uses Reflex:"));
        WrappedText(IlionTheme::TextDim, Tr("DX11/DX12: forced XeLL (XeFG, Force XeLL), Anti-Lag 2, XeLL, LatencyFleX"));
        WrappedText(IlionTheme::TextDim, Tr("Vulkan: AntiLag Vk, LatencyFleX"));
        WrappedText(IlionTheme::TextDim,
                    Tr("More than one generated frame or dynamic DLSS-G: no Anti-Lag 2 or XeLL, Reflex off"));
        WrappedText(IlionTheme::TextDim, Tr("Force LatencyFlex skips Anti-Lag 2 and XeLL"));
        WrappedText(IlionTheme::TextDim, Tr("NVIDIA GPU: native Reflex, fakenvapi not used"));
    }
}

void MenuCommon::RenderMagnifierSettings(RenderMenuContext& ctx)
{
    auto& state = ctx.state;
    auto config = ctx.config;

    // Magnifier -----------------------------
    ImGui::Spacing();
    if (auto ch = ScopedCollapsingHeader(TrL("Magnifier")); ch.IsHeaderOpen())
    {
        ScopedIndent indent {};
        ImGui::Spacing();

        bool magnifierEnabled = config->MagnifierEnabled.value_or_default();
        if (IlionTheme::Checkbox(TrL("Enable Magnifier"), &magnifierEnabled))
            config->MagnifierEnabled = magnifierEnabled;

        ImGui::BeginDisabled(!magnifierEnabled);

        float magnifierSize = config->MagnifierSize.value_or_default();
        if (IlionTheme::SliderFloat(TrL("Size"), &magnifierSize, 5.0f, 50.0f, Tr("%.1f%% of screen")))
            config->MagnifierSize = magnifierSize;

        int zoomFactor = config->MagnifierZoomFactor.value_or_default();
        if (IlionTheme::SliderInt(TrL("Zoom Factor"), &zoomFactor, 2, 20, "%dx"))
            config->MagnifierZoomFactor = zoomFactor;

        float borderSize = config->MagnifierBorderSize.value_or_default();
        if (IlionTheme::SliderFloat(TrL("Border Size"), &borderSize, 0.0f, 2.0f, Tr("%.2f%% of screen")))
            config->MagnifierBorderSize = borderSize;

        ImGui::Separator();
        ImGui::Text(Tr("Positioning"));

        bool staticMode = config->MagnifierStaticPosX.has_value() && config->MagnifierStaticPosY.has_value();
        if (staticMode)
        {
            float staticX = config->MagnifierStaticPosX.value();
            if (IlionTheme::SliderFloat(TrL("Static Pos X"), &staticX, 0.0f, 100.0f, "%.1f%%"))
                config->MagnifierStaticPosX = staticX;

            float staticY = config->MagnifierStaticPosY.value();
            if (IlionTheme::SliderFloat(TrL("Static Pos Y"), &staticY, 0.0f, 100.0f, "%.1f%%"))
                config->MagnifierStaticPosY = staticY;

            if (IlionTheme::Button(TrL("Reset Static Position (Follow Cursor)")))
            {
                config->MagnifierStaticPosX.reset();
                config->MagnifierStaticPosY.reset();
            }
        }
        else
        {
            // Button to initialize static position mode
            if (IlionTheme::Button(TrL("Set Static Position")))
            {
                config->MagnifierStaticPosX = 50.0f;
                config->MagnifierStaticPosY = 50.0f;
            }
            ImGui::SameLine();
            ImGui::TextDisabled(Tr("(Currently following cursor)"));

            float offsetX = config->MagnifierCursorOffsetX.value_or_default();
            if (IlionTheme::SliderFloat(TrL("Cursor Offset X"), &offsetX, -300.0f, 300.0f, "%.0f px"))
                config->MagnifierCursorOffsetX = offsetX;

            float offsetY = config->MagnifierCursorOffsetY.value_or_default();
            if (IlionTheme::SliderFloat(TrL("Cursor Offset Y"), &offsetY, -300.0f, 300.0f, "%.0f px"))
                config->MagnifierCursorOffsetY = offsetY;
        }

        ImGui::EndDisabled();
        ImGui::Spacing();
    }
}
void MenuCommon::RenderQuirksSettings(RenderMenuContext& ctx)
{
    auto& state = ctx.state;

    // QUIRKS -----------------------------
    if (state.detectedQuirks.size() > 0)
    {
        SectionTitle("Active Quirks");

        for (const auto& quirk : state.detectedQuirks)
        {
            ImGui::TextWrapped("%s", quirk.c_str());
        }
    }
}

void MenuCommon::RenderAdvancedSettings(RenderMenuContext& ctx)
{
    auto& state = ctx.state;
    auto config = ctx.config;
    auto& currentFeature = ctx.currentFeature;

    const bool upscalerActive = currentFeature != nullptr && !currentFeature->IsFrozen();
    const bool nonDlssActive =
        upscalerActive && currentBackend != Upscaler::DLSS && currentBackend != Upscaler::DLSS_on12;

    // SPOOFING/HOOKING -----------------------------
    if (ScopedCard::Shows("spoofing") && nonDlssActive)
    {
        ScopedCard card { "Spoofing / Hooking" };

        if (ImGui::BeginTable("hooking", 2, ImGuiTableFlags_SizingStretchSame))
        {
            ImGui::TableNextColumn();
            ConfigCheckbox("DXGI Spoofing", config->DxgiSpoofing,
                           "Enable Nvidia GPU spoofing for DXGI adapter\n"
                           "Detailed modifications available in the INI",
                           true);

            ImGui::TableNextColumn();
            ConfigCheckbox("Ntdll Hooks", config->UseNtdllHooks,
                           "Only hook ntdll.dll methods\n"
                           "Disable for switching back to kernel hooks",
                           true);

            if (state.api == Vulkan)
            {
                ImGui::TableNextColumn();
                ConfigCheckbox("VLK Spoofing", config->VulkanSpoofing,
                               "Enable Nvidia GPU spoofing for Vulkan\n"
                               "Detailed modifications available in the INI",
                               true);

                ImGui::TableNextColumn();
                ConfigCheckbox("VLK Extension Spoofing", config->VulkanExtensionSpoofing,
                               "Enable Nvidia Extension spoofing for Vulkan\n"
                               "Detailed modifications available in the INI",
                               true);
            }

            ImGui::TableNextColumn();
            ConfigCheckbox("Disable Overlays", config->DisableOverlays,
                           "Disable supported overlays (autoenabled with OptiFG)\n"
                           "Including Steam Input (controller issues)",
                           true);

            ImGui::EndTable();
        }
    }

    // PLUGINS/MISC -----------------------------
    if (ScopedCard::Shows("plugins"))
    {
        ScopedCard card { "Plugins / Startup" };

        if (ImGui::BeginTable("plugins", 2, ImGuiTableFlags_SizingStretchSame))
        {
            ImGui::TableNextColumn();
            ConfigCheckbox("Load ASI plugins", config->LoadAsiPlugins,
                           "Let OptiScaler load *.asi files from plugins folder", true);

            ImGui::TableNextColumn();
            ConfigCheckbox("Disable Splash message", config->DisableSplash,
                           "Disables Startup Splash message (bottom left corner)", true);

            ImGui::TableNextColumn();
            ConfigCheckbox("Check for Update", config->CheckForUpdate,
                           "Enable checking Github for latest version\n"
                           "Only works on stable/final releases",
                           true);

            ImGui::EndTable();
        }
    }

    // SHADERS, LIMITS AND DRS -----------------------------
    if (ScopedCard::Shows("shaders_limits"))
    {
        ScopedCard card { "Shaders / Limits" };

        if (ImGui::BeginTable("limits", 2, ImGuiTableFlags_SizingStretchSame))
        {
            ImGui::TableNextColumn();
            if (ConfigCheckbox("Use Precompiled Shaders", config->UsePrecompiledShaders))
            {
                state.newBackend = currentBackend;
                MARK_ALL_BACKENDS_CHANGED();
            }

            // DRS
            ImGui::TableNextColumn();
            ConfigCheckbox("DRS Override Minimum", config->DrsMinOverrideEnabled,
                           "Dynamic Resolution Scaling\nFix for games ignoring official DRS limits");

            ImGui::TableNextColumn();
            if (upscalerActive)
                ConfigCheckbox("Enable Extended Limits", config->ExtendedLimits,
                               "Extended sliders limit for quality presets\n\n"
                               "Using this option changes resolution detection logic\n"
                               "and might cause issues and crashes!");

            ImGui::TableNextColumn();
            ConfigCheckbox("DRS Override Maximum", config->DrsMaxOverrideEnabled,
                           "Dynamic Resolution Scaling\nFix for games ignoring official DRS limits");

            ImGui::EndTable();
        }
    }

    if (!nonDlssActive)
        return;

    // BARRIERS -----------------------------
    if (ScopedCard::Shows("barriers"))
    {
        ScopedCard card { "Resource Barriers" };

        if (ImGui::BeginTable("barriers", 2, ImGuiTableFlags_SizingStretchSame))
        {
            ImGui::TableNextColumn();
            AddResourceBarrier("Color", &config->ColorResourceBarrier);

            ImGui::TableNextColumn();
            AddResourceBarrier("Depth", &config->DepthResourceBarrier);

            ImGui::TableNextColumn();
            AddResourceBarrier("Motion", &config->MVResourceBarrier);

            ImGui::TableNextColumn();
            AddResourceBarrier("Exposure", &config->ExposureResourceBarrier);

            ImGui::TableNextColumn();
            AddResourceBarrier("Mask", &config->MaskResourceBarrier);

            ImGui::TableNextColumn();
            AddResourceBarrier("Output", &config->OutputResourceBarrier);

            ImGui::EndTable();
        }
    }

    // HOTFIXES -----------------------------
    if (ScopedCard::Shows("root_signatures") && state.api == DX12)
    {
        ScopedCard card { "Root Signatures" };

        if (ImGui::BeginTable("rootSignatures", 2, ImGuiTableFlags_SizingStretchSame))
        {
            ImGui::TableNextColumn();
            ConfigCheckbox("Restore Compute Root Signature", config->RestoreComputeSignature);

            ImGui::TableNextColumn();
            ConfigCheckbox("Restore Graphic Root Signature", config->RestoreGraphicSignature);

            ImGui::EndTable();
        }
    }
}

void MenuCommon::RenderLoggingSettings(RenderMenuContext& ctx)
{
    auto config = ctx.config;

    // LOGGING -----------------------------
    SectionTitle("Logging");

    if (config->LogToConsole.value_or_default() || config->LogToFile.value_or_default() ||
        config->LogToNGX.value_or_default())
        spdlog::default_logger()->set_level((spdlog::level::level_enum) config->LogLevel.value_or_default());
    else
        spdlog::default_logger()->set_level(spdlog::level::off);

    if (bool toFile = config->LogToFile.value_or_default(); IlionTheme::Checkbox(TrL("To File"), &toFile))
    {
        config->LogToFile = toFile;
        PrepareLogger();
    }

    SameLineIfRoom(CheckboxWidth(TrL("To Console")), 6.0f);
    if (bool toConsole = config->LogToConsole.value_or_default(); IlionTheme::Checkbox(TrL("To Console"), &toConsole))
    {
        config->LogToConsole = toConsole;
        PrepareLogger();
    }

    SameLineIfRoom(CheckboxWidth(TrL("Single File")), 6.0f);
    if (auto SingleFile = config->LogSingleFile.value_or_default(); IlionTheme::Checkbox(TrL("Single File"), &SingleFile))
    {
        config->LogSingleFile = SingleFile;
        PrepareLogger();
    }

    const char* logLevels[] = { Tr("Trace"), Tr("Debug"), Tr("Information"), Tr("Warning"), Tr("Error") };
    const char* selectedLevel = logLevels[config->LogLevel.value_or_default()];

    if (IlionTheme::BeginCombo(TrL("Log Level"), selectedLevel))
    {
        for (int n = 0; n < 5; n++)
        {
            if (ImGui::Selectable(logLevels[n], (config->LogLevel.value_or_default() == n)))
            {
                config->LogLevel = n;
                spdlog::default_logger()->set_level((spdlog::level::level_enum) config->LogLevel.value_or_default());
            }
        }

        ImGui::EndCombo();
    }
}

void MenuCommon::RenderThemeSettings(RenderMenuContext& ctx)
{
    auto config = ctx.config;

    // THEME -----------------------------
    ImGui::Spacing();
    if (auto ch = ScopedCollapsingHeader(TrL("Menu Theme and Color")); ch.IsHeaderOpen())
    {
        ScopedIndent indent {};
        ImGui::Spacing();

        // The three ILION themes share every control and the layout, only their palette changes.
        // Classic: everything below works as it always did. The choice is saved at once.
        const bool industrialTheme = config->IndustrialTheme.value_or_default();
        // Palette 1 (the first Fallout) is retired: Config reads it as 3, the CRT one, now simply called Fallout
        const int palette = std::clamp(config->IlionPalette.value_or_default(), 0, 3);
        static const char* paletteNames[] = { "ILION (original)", nullptr, "Light", "Fallout" };

        ImGui::SetNextItemWidth(180.0f * ctx.menuResScale);

        const char* paletteName = paletteNames[palette] != nullptr ? paletteNames[palette] : paletteNames[3];

        if (IlionTheme::BeginCombo(TrL("Theme"), Tr(industrialTheme ? paletteName : "Classic")))
        {
            // Shown in this order: ILION (original), Fallout, Light; Classic after them. The saved numbers don't change.
            static constexpr int shownOrder[] = { 0, 3, 2 };

            for (const int n : shownOrder)
            {
                const bool chosen = industrialTheme && palette == n;

                if (ImGui::Selectable(Tr(paletteNames[n]), chosen) && !chosen)
                {
                    config->IndustrialTheme = true;
                    config->IlionPalette = n;
                    ApplyThemeStyle();
                    config->SaveMenuPreferences();
                }
            }

            if (ImGui::Selectable(TrL("Classic"), !industrialTheme) && industrialTheme)
            {
                config->IndustrialTheme = false;
                ApplyThemeStyle();
                config->SaveMenuPreferences();
            }

            ImGui::EndCombo();
        }

        ShowTooltip("ILION (original), Fallout and Light: the instrument panel look of ILION in three styles.\n"
                    "Classic: the original look, with the colour settings below.");

        // The colours below are the ones of the classic theme: hidden with the ILION themes, they do nothing there
        if (!industrialTheme)
        {

        bool lightTheme = config->LightTheme.value_or_default();

        const ImVec4 bgDark = lightTheme ? ImVec4(0.80f, 0.82f, 0.86f, 1.00f) : ImVec4(0.09f, 0.09f, 0.10f, 1.00f);
        const ImVec4 bgMid = lightTheme ? ImVec4(0.89f, 0.91f, 0.95f, 1.00f) : ImVec4(0.11f, 0.11f, 0.12f, 1.00f);
        const ImVec4 bgLight = lightTheme ? ImVec4(0.96f, 0.97f, 0.99f, 1.00f) : ImVec4(0.14f, 0.14f, 0.15f, 1.00f);

        auto Mix = [](const ImVec4& a, const ImVec4& b, float t, float alpha = 1.0f)
        { return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, alpha); };

        auto AccentSoft = [&](ImVec4 accent, float alpha = 1.0f)
        { return toneMapColor(lightTheme ? Mix(bgLight, accent, 0.24f, alpha) : Mix(bgDark, accent, 0.32f, alpha)); };

        auto AccentMed = [&](ImVec4 accent, float alpha = 1.0f)
        { return toneMapColor(lightTheme ? Mix(bgLight, accent, 0.42f, alpha) : Mix(bgDark, accent, 0.55f, alpha)); };

        auto AccentStrong = [&](ImVec4 accent, float alpha = 1.0f)
        { return toneMapColor(ImVec4(accent.x, accent.y, accent.z, alpha)); };

        if (IlionTheme::Checkbox(TrL("Light Theme"), &lightTheme))
        {
            config->LightTheme = lightTheme;
            ApplyThemeStyle();
        }

        SectionTitle("Accent Colour");

        ImGui::Text(Tr("Presets:"));
        ImGui::SameLine(0.0f, 6.0f);

        ImVec4 colorBlue = { 0.00f, 0.40f, 0.77f, 1.0f };
        ImVec4 colorTeal = { 0.00f, 1.00f, 0.91f, 1.0f };
        ImVec4 colorGray = { 0.54f, 0.54f, 0.54f, 1.0f };
        ImVec4 colorYellow = { 1.00f, 0.89f, 0.00f, 1.0f };
        ImVec4 colorGreen = { 0.25f, 1.00f, 0.00f, 1.0f };
        ImVec4 colorRed = { 1.00f, 0.00f, 0.00f, 1.0f };
        ImVec4 colorOrange = { 1.00f, 0.52f, 0.00f, 1.0f };
        ImVec4 colorPurple = { 0.576f, 0.00f, 1.00f, 1.0f };

        ImVec4 color = {};

        color = colorBlue;
        ImGui::PushStyleColor(ImGuiCol_Button, AccentSoft(color));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, AccentMed(color));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, AccentStrong(color));

        if (IlionTheme::Button(TrL("Blue")))
        {
            ImGui::PopStyleColor(3);

            config->MenuAccentColorR = color.x;
            config->MenuAccentColorG = color.y;
            config->MenuAccentColorB = color.z;
            ApplyThemeStyle();
        }
        else
        {
            ImGui::PopStyleColor(3);
        }

        ImGui::SameLine(0.0f, 6.0f);

        color = colorTeal;
        ImGui::PushStyleColor(ImGuiCol_Button, AccentSoft(color));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, AccentMed(color));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, AccentStrong(color));

        if (IlionTheme::Button(TrL("Teal")))
        {
            ImGui::PopStyleColor(3);

            config->MenuAccentColorR = color.x;
            config->MenuAccentColorG = color.y;
            config->MenuAccentColorB = color.z;
            ApplyThemeStyle();
        }
        else
        {
            ImGui::PopStyleColor(3);
        }

        ImGui::SameLine(0.0f, 6.0f);

        color = colorGray;
        ImGui::PushStyleColor(ImGuiCol_Button, AccentSoft(color));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, AccentMed(color));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, AccentStrong(color));

        if (IlionTheme::Button(TrL("Gray")))
        {
            ImGui::PopStyleColor(3);

            config->MenuAccentColorR = color.x;
            config->MenuAccentColorG = color.y;
            config->MenuAccentColorB = color.z;
            ApplyThemeStyle();
        }
        else
        {
            ImGui::PopStyleColor(3);
        }

        ImGui::SameLine(0.0f, 6.0f);

        color = colorYellow;
        ImGui::PushStyleColor(ImGuiCol_Button, AccentSoft(color));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, AccentMed(color));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, AccentStrong(color));

        if (IlionTheme::Button(TrL("Yellow")))
        {
            ImGui::PopStyleColor(3);

            config->MenuAccentColorR = color.x;
            config->MenuAccentColorG = color.y;
            config->MenuAccentColorB = color.z;
            ApplyThemeStyle();
        }
        else
        {
            ImGui::PopStyleColor(3);
        }

        ImGui::SameLine(0.0f, 6.0f);

        color = colorGreen;
        ImGui::PushStyleColor(ImGuiCol_Button, AccentSoft(color));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, AccentMed(color));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, AccentStrong(color));

        if (IlionTheme::Button(TrL("Green")))
        {
            ImGui::PopStyleColor(3);

            config->MenuAccentColorR = color.x;
            config->MenuAccentColorG = color.y;
            config->MenuAccentColorB = color.z;
            ApplyThemeStyle();
        }
        else
        {
            ImGui::PopStyleColor(3);
        }

        ImGui::SameLine(0.0f, 6.0f);

        color = colorRed;
        ImGui::PushStyleColor(ImGuiCol_Button, AccentSoft(color));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, AccentMed(color));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, AccentStrong(color));

        if (IlionTheme::Button(TrL("Red")))
        {
            ImGui::PopStyleColor(3);

            config->MenuAccentColorR = color.x;
            config->MenuAccentColorG = color.y;
            config->MenuAccentColorB = color.z;
            ApplyThemeStyle();
        }
        else
        {
            ImGui::PopStyleColor(3);
        }

        ImGui::SameLine(0.0f, 6.0f);

        color = colorOrange;
        ImGui::PushStyleColor(ImGuiCol_Button, AccentSoft(color));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, AccentMed(color));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, AccentStrong(color));

        if (IlionTheme::Button(TrL("Orange")))
        {
            ImGui::PopStyleColor(3);

            config->MenuAccentColorR = color.x;
            config->MenuAccentColorG = color.y;
            config->MenuAccentColorB = color.z;
            ApplyThemeStyle();
        }
        else
        {
            ImGui::PopStyleColor(3);
        }

        ImGui::SameLine(0.0f, 6.0f);

        color = colorPurple;
        ImGui::PushStyleColor(ImGuiCol_Button, AccentSoft(color));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, AccentMed(color));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, AccentStrong(color));

        if (IlionTheme::Button(TrL("Purple")))
        {
            ImGui::PopStyleColor(3);

            config->MenuAccentColorR = color.x;
            config->MenuAccentColorG = color.y;
            config->MenuAccentColorB = color.z;
            ApplyThemeStyle();
        }
        else
        {
            ImGui::PopStyleColor(3);
        }

        float accentColor[3] = { config->MenuAccentColorR.value_or_default(),
                                 config->MenuAccentColorG.value_or_default(),
                                 config->MenuAccentColorB.value_or_default() };

        if (ImGui::ColorEdit3("Custom Accent Color", accentColor))
        {
            config->MenuAccentColorR = accentColor[0];
            config->MenuAccentColorG = accentColor[1];
            config->MenuAccentColorB = accentColor[2];
            ApplyThemeStyle();
        }

        ImGui::Spacing();

        if (IlionTheme::Button(TrL("Reset Accent Color")))
        {
            config->MenuAccentColorR.reset();
            config->MenuAccentColorG.reset();
            config->MenuAccentColorB.reset();
            ApplyThemeStyle();
        }

        ImGui::Spacing();

        SectionTitle("Background Colour");

        ImGui::Text(Tr("Presets:"));
        ImGui::SameLine(0.0f, 6.0f);

        color = colorBlue;
        ImGui::PushStyleColor(ImGuiCol_Button, AccentSoft(color));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, AccentMed(color));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, AccentStrong(color));

        if (IlionTheme::Button(TrL("Blue##2")))
        {
            ImGui::PopStyleColor(3);

            config->MenuBGColorR = color.x;
            config->MenuBGColorG = color.y;
            config->MenuBGColorB = color.z;
            ApplyThemeStyle();
        }
        else
        {
            ImGui::PopStyleColor(3);
        }

        ImGui::SameLine(0.0f, 6.0f);

        color = colorTeal;
        ImGui::PushStyleColor(ImGuiCol_Button, AccentSoft(color));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, AccentMed(color));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, AccentStrong(color));

        if (IlionTheme::Button(TrL("Teal##2")))
        {
            ImGui::PopStyleColor(3);

            config->MenuBGColorR = color.x;
            config->MenuBGColorG = color.y;
            config->MenuBGColorB = color.z;
            ApplyThemeStyle();
        }
        else
        {
            ImGui::PopStyleColor(3);
        }

        ImGui::SameLine(0.0f, 6.0f);

        color = colorGray;
        ImGui::PushStyleColor(ImGuiCol_Button, AccentSoft(color));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, AccentMed(color));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, AccentStrong(color));

        if (IlionTheme::Button(TrL("Gray##2")))
        {
            ImGui::PopStyleColor(3);

            config->MenuBGColorR = color.x;
            config->MenuBGColorG = color.y;
            config->MenuBGColorB = color.z;
            ApplyThemeStyle();
        }
        else
        {
            ImGui::PopStyleColor(3);
        }

        ImGui::SameLine(0.0f, 6.0f);

        color = colorYellow;
        ImGui::PushStyleColor(ImGuiCol_Button, AccentSoft(color));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, AccentMed(color));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, AccentStrong(color));

        if (IlionTheme::Button(TrL("Yellow##2")))
        {
            ImGui::PopStyleColor(3);

            config->MenuBGColorR = color.x;
            config->MenuBGColorG = color.y;
            config->MenuBGColorB = color.z;
            ApplyThemeStyle();
        }
        else
        {
            ImGui::PopStyleColor(3);
        }

        ImGui::SameLine(0.0f, 6.0f);

        color = colorGreen;
        ImGui::PushStyleColor(ImGuiCol_Button, AccentSoft(color));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, AccentMed(color));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, AccentStrong(color));

        if (IlionTheme::Button(TrL("Green##2")))
        {
            ImGui::PopStyleColor(3);

            config->MenuBGColorR = color.x;
            config->MenuBGColorG = color.y;
            config->MenuBGColorB = color.z;
            ApplyThemeStyle();
        }
        else
        {
            ImGui::PopStyleColor(3);
        }

        ImGui::SameLine(0.0f, 6.0f);

        color = colorRed;
        ImGui::PushStyleColor(ImGuiCol_Button, AccentSoft(color));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, AccentMed(color));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, AccentStrong(color));

        if (IlionTheme::Button(TrL("Red##2")))
        {
            ImGui::PopStyleColor(3);

            config->MenuBGColorR = color.x;
            config->MenuBGColorG = color.y;
            config->MenuBGColorB = color.z;
            ApplyThemeStyle();
        }
        else
        {
            ImGui::PopStyleColor(3);
        }

        ImGui::SameLine(0.0f, 6.0f);

        color = colorOrange;
        ImGui::PushStyleColor(ImGuiCol_Button, AccentSoft(color));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, AccentMed(color));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, AccentStrong(color));

        if (IlionTheme::Button(TrL("Orange##2")))
        {
            ImGui::PopStyleColor(3);

            config->MenuBGColorR = color.x;
            config->MenuBGColorG = color.y;
            config->MenuBGColorB = color.z;
            ApplyThemeStyle();
        }
        else
        {
            ImGui::PopStyleColor(3);
        }

        ImGui::SameLine(0.0f, 6.0f);

        color = colorPurple;
        ImGui::PushStyleColor(ImGuiCol_Button, AccentSoft(color));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, AccentMed(color));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, AccentStrong(color));

        if (IlionTheme::Button(TrL("Purple##2")))
        {
            ImGui::PopStyleColor(3);

            config->MenuBGColorR = color.x;
            config->MenuBGColorG = color.y;
            config->MenuBGColorB = color.z;
            ApplyThemeStyle();
        }
        else
        {
            ImGui::PopStyleColor(3);
        }

        float bgColor[3] = { config->MenuBGColorR.value_or_default(), config->MenuBGColorG.value_or_default(),
                             config->MenuBGColorB.value_or_default() };

        if (ImGui::ColorEdit3("Custom BG Colour", bgColor))
        {
            config->MenuBGColorR = bgColor[0];
            config->MenuBGColorG = bgColor[1];
            config->MenuBGColorB = bgColor[2];
            ApplyThemeStyle();
        }

        } // classic theme colours

        ImGui::Spacing();

        auto alpha = config->MenuBGColorA.value_or_default();
        if (IlionTheme::SliderFloat(TrL("Background Alpha"), &alpha, 0.0f, 1.0f))
        {
            config->MenuBGColorA = alpha;
            ApplyThemeStyle();
        }

        if (_bgBlurTexture != ImTextureID_Invalid)
        {
            ConfigCheckbox("Background Blur", config->MenuBlur,
                           "Blur the game behind the menu\nOnly visible when Background Alpha is below 1");

            ImGui::BeginDisabled(!config->MenuBlur.value_or_default());

            auto blurStrength = config->MenuBlurStrength.value_or_default();
            if (IlionTheme::SliderFloat(TrL("Blur Strength"), &blurStrength, 0.0f, 4.0f, "%.2f"))
                config->MenuBlurStrength = blurStrength;

            ImGui::EndDisabled();
        }

        ImGui::Spacing();

        if (IlionTheme::Button(TrL("Reset BG Colour")))
        {
            config->MenuBGColorR.reset();
            config->MenuBGColorG.reset();
            config->MenuBGColorB.reset();
            config->MenuBGColorA.reset();
            ApplyThemeStyle();
        }

        ImGui::Spacing();
    }
}

void MenuCommon::RenderFpsOverlaySettings(RenderMenuContext& ctx)
{
    auto config = ctx.config;

    // FPS OVERLAY -----------------------------
    SectionTitle("FPS Overlay");
    {
        ScopedID sectionId { "FPS Overlay" };

        bool fpsEnabled = config->ShowFps.value_or_default();
        if (IlionTheme::Checkbox(TrL("FPS Overlay Enabled"), &fpsEnabled))
            config->ShowFps = fpsEnabled;

        SameLineIfRoom(CheckboxWidth(TrL("Horizontal")), 6.0f);

        bool fpsHorizontal = config->FpsOverlayHorizontal.value_or_default();
        if (IlionTheme::Checkbox(TrL("Horizontal"), &fpsHorizontal))
            config->FpsOverlayHorizontal = fpsHorizontal;

        const char* fpsPosition[] = { Tr("Top Left"), Tr("Top Right"), Tr("Bottom Left"), Tr("Bottom Right") };
        const char* selectedPosition = fpsPosition[config->FpsOverlayPosition.value_or_default()];

        if (IlionTheme::BeginCombo(TrL("Overlay Position"), selectedPosition))
        {
            for (int n = 0; n < std::size(fpsPosition); n++)
            {
                if (ImGui::Selectable(fpsPosition[n], (config->FpsOverlayPosition.value_or_default() == n)))
                    config->FpsOverlayPosition = (FpsOverlayPos) n;
            }

            ImGui::EndCombo();
        }

        const char* fpsType[] = { Tr("Just FPS"), Tr("Simple"),       Tr("Detailed"),      Tr("Detailed + Graph"),
                                  Tr("Full"),     Tr("Full + Graph"), Tr("Reflex timings"), Tr("Latency (both meters)"),
                                  Tr("Custom##overlay") };
        const char* selectedType = fpsType[config->FpsOverlayType.value_or_default()];

        if (IlionTheme::BeginCombo(TrL("Overlay Type"), selectedType))
        {
            for (int n = 0; n < std::size(fpsType); n++)
            {
                if (ImGui::Selectable(fpsType[n], (config->FpsOverlayType.value_or_default() == n)))
                    config->FpsOverlayType = (FpsOverlay) n;
            }

            ImGui::EndCombo();
        }
        ShowTooltip("A type sets the elements below. Changing any of them turns the type into Custom.");

        // ILION: the elements of the type in use; one changed here becomes Custom, starting from what was shown
        {
            struct ElementOption
            {
                const char* label;
                const char* tip;
                IlionOverlay::Element element;
            };

            static const ElementOption elementOptions[] = {
                { "Frame times", "FPS average, frame time and its average", IlionOverlay::FrameTimes },
                { "Frame time graph", "Graph of the last frame times", IlionOverlay::FrameGraph },
                { "Upscaler time", "GPU time of the upscaler and its average", IlionOverlay::UpscalerTime },
                { "Upscaler time graph", "Graph of the last upscaler times", IlionOverlay::UpscalerGraph },
                { "Reflex timings##element", "OptiScaler's Reflex timings: the frame report of Reflex or fakenvapi. With fakenvapi only its CPU times "
                  "are real: Whole frame ends at the last marker of the frame, not on screen",
                  IlionOverlay::ReflexTimings },
                { "ILION estimated latency##element",
                  "ILION's latency, DirectX 12: simulation start to the refresh that shows the frame, or to the end of the GPU "
                  "work when the refresh can't be known. CPU: simulation start to Present. Present to GPU end: includes "
                  "frames waiting in the queue. With frame generation the generator's own delay is not included",
                  IlionOverlay::IlionLatency },
            };

            const uint32_t shown = IlionOverlay::Elements();

            // Two columns only when the longest name fits in each, in either language
            float widest = 0.0f;
            for (const auto& option : elementOptions)
                widest = std::max(widest, CheckboxWidth(TrL(option.label)));

            const int columns =
                ImGui::GetContentRegionAvail().x >= (widest + ImGui::GetStyle().CellPadding.x * 2.0f) * 2.0f ? 2 : 1;

            if (ImGui::BeginTable("##overlayElements", columns, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings))
            {
                for (const auto& option : elementOptions)
                {
                    ImGui::TableNextColumn();

                    bool on = (shown & option.element) != 0;
                    if (IlionTheme::Checkbox(TrL(option.label), &on))
                    {
                        config->FpsOverlayElements = on ? (shown | option.element) : (shown & (IlionOverlay::AllElements ^ (uint32_t) option.element));
                        config->FpsOverlayType = FpsOverlay_Custom;
                    }
                    ShowTooltip(option.tip);
                }

                ImGui::EndTable();
            }
        }

        float fpsAlpha = config->FpsOverlayAlpha.value_or_default();
        if (IlionTheme::SliderFloat(TrL("Background Alpha"), &fpsAlpha, 0.0f, 1.0f, "%.2f"))
            config->FpsOverlayAlpha = fpsAlpha;

        const char* options[] = { Tr("Same as menu"), "0.5", "0.6", "0.7", "0.8", "0.9", "1.0", "1.1", "1.2",
                                  "1.3",          "1.4", "1.5", "1.6", "1.7", "1.8", "1.9", "2.0" };
        int currentIndex = std::max(((int) (config->FpsScale.value_or(0.0f) * 10.0f)) - 4, 0);
        float values[] = { 0.0f, 0.5f, 0.6f, 0.7f, 0.8f, 0.9f, 1.0f, 1.1f, 1.2f,
                           1.3f, 1.4f, 1.5f, 1.6f, 1.7f, 1.8f, 1.9f, 2.0f };

        if (IlionTheme::SliderInt(TrL("Scale"), &currentIndex, 0, IM_ARRAYSIZE(options) - 1, options[currentIndex],
                             ImGuiSliderFlags_ClampOnInput))
        {
            if (currentIndex == 0)
                config->FpsScale.reset();
            else
                config->FpsScale = values[currentIndex];
        }

        bool useTheme = config->OverlaysUseTheme.value_or_default();
        if (IlionTheme::Checkbox(TrL("Use Theme Colors"), &useTheme))
            config->OverlaysUseTheme = useTheme;

        ConfigCheckbox("1% low (last 300 frames)", config->FpsOverlayLow1,
                       "FPS of the slowest 1% of the frames the overlay keeps. Shown in the ILION layouts.");

        // Hardware readings, every one can be shown on its own. A section that folds, like the others of the menu.
        ImGui::Spacing();

        auto hardwareHeader = ScopedCollapsingHeader(TrL("Hardware monitoring"));

        ShowTooltip("Readings come from HWiNFO or MSI Afterburner while they are running, about once per second.\n"
                    "HWiNFO needs Shared Memory Support enabled in its settings (12 hours per run in the free version).\n"
                    "A reading that no program provides is shown as N/A, nothing is estimated.");

        if (hardwareHeader.IsHeaderOpen())
        {
        // Keeps the sources below up to date while this section is on screen
        HwTelemetry::KeepAlive();

        if (ImGui::BeginTable("hwMetrics", 2, ImGuiTableFlags_SizingStretchSame))
        {
            ImGui::TableNextColumn();
            ImGui::TextDisabled(Tr("GPU"));
            ConfigCheckbox("GPU usage", config->HwGpuUsage, nullptr);
            ConfigCheckbox("GPU temperature", config->HwGpuTemp, nullptr);
            ConfigCheckbox("GPU hotspot", config->HwGpuHotspot, nullptr);
            ConfigCheckbox("VRAM used / total", config->HwVram, nullptr);
            ConfigCheckbox("GPU power", config->HwGpuPower, nullptr);

            ImGui::TableNextColumn();
            ImGui::TextDisabled(Tr("CPU"));
            ConfigCheckbox("CPU usage", config->HwCpuUsage, nullptr);
            ConfigCheckbox("CPU temperature", config->HwCpuTemp, nullptr);
            ConfigCheckbox("CPU power", config->HwCpuPower, nullptr);
            ConfigCheckbox("RAM used", config->HwRam, nullptr);

            ImGui::EndTable();
        }

        const char* hwSources[] = { "Auto (HWiNFO, then MSI Afterburner)", "HWiNFO", "MSI Afterburner" };
        const int hwSource = std::clamp(config->HwSource.value_or_default(), 0, 2);

        if (IlionTheme::BeginCombo(TrL("Source"), Tr(hwSources[hwSource])))
        {
            for (int n = 0; n < (int) std::size(hwSources); n++)
            {
                if (ImGui::Selectable(Tr(hwSources[n]), hwSource == n))
                    config->HwSource = n;
            }

            ImGui::EndCombo();
        }

        const auto hwSnapshot = HwTelemetry::Get();

        // ILION: where every reading comes from, folded; the readings above don't depend on it being open
        if (!EssentialView())
        if (auto sources = ScopedCollapsingHeader(TrL("Sensor sources and diagnostics")); sources.IsHeaderOpen())
        {
        ImGui::TextDisabled(Tr("HWiNFO: %s  |  MSI Afterburner: %s"),
                            Tr(hwSnapshot.hwinfoAvailable ? "detected" : "not detected"),
                            Tr(hwSnapshot.afterburnerAvailable ? "detected" : "not detected"));

        // Where every reading comes from and how its sensor is called there
        const auto hwSourceRow = [&](const char* name, HwTelemetry::Metric metric)
        {
            const auto& value = hwSnapshot.values[metric];

            if (value.valid)
                ImGui::TextDisabled("  %s: %s (%s)", Tr(name), HwTelemetry::SourceName(value.source), value.label);
            else
                ImGui::TextDisabled("  %s: %s", Tr(name), HwNotAvailable());
        };

        hwSourceRow("GPU usage", HwTelemetry::GpuUsage);
        hwSourceRow("GPU temperature", HwTelemetry::GpuTemp);
        hwSourceRow("GPU hotspot", HwTelemetry::GpuHotspot);
        hwSourceRow("VRAM used", HwTelemetry::VramUsed);
        hwSourceRow("VRAM total", HwTelemetry::VramTotal);
        hwSourceRow("CPU usage", HwTelemetry::CpuUsage);
        hwSourceRow("CPU temperature", HwTelemetry::CpuTemp);
        hwSourceRow("GPU power", HwTelemetry::GpuPower);
        hwSourceRow("CPU power", HwTelemetry::CpuPower);
        hwSourceRow("RAM used", HwTelemetry::RamUsed);
        } // sensor sources
        }
    }
}

void MenuCommon::RenderUpscalerInputsSettings(RenderMenuContext& ctx)
{
    auto config = ctx.config;

    // UPSCALER INPUTS -----------------------------
    SectionTitle("Upscaler Inputs");
    {
        ScopedID sectionId { "Upscaler Inputs" };

        // Values when the menu was first drawn, enabling and pattern matching apply on next launch
        static const bool startFsr2 = config->EnableFsr2Inputs.value_or_default();
        static const bool startFsr3 = config->EnableFsr3Inputs.value_or_default();
        static const bool startFfx = config->EnableFfxInputs.value_or_default();
        static const bool startFsr2Pattern = config->Fsr2Pattern.value_or_default();
        static const bool startFsr3Pattern = config->Fsr3Pattern.value_or_default();

        if (ImGui::BeginTable("upscalerInputs", 4, ImGuiTableFlags_SizingStretchSame))
        {
            ImGui::TableSetupColumn("Input", ImGuiTableColumnFlags_WidthStretch, 0.7f);
            ImGui::TableSetupColumn("Enable", ImGuiTableColumnFlags_WidthStretch, 1.0f);
            ImGui::TableSetupColumn("Use", ImGuiTableColumnFlags_WidthStretch, 1.0f);
            ImGui::TableSetupColumn("Pattern", ImGuiTableColumnFlags_WidthStretch, 1.0f);

            ImGui::TableNextRow();
            for (int i = 0; i < 4; i++)
            {
                ImGui::TableSetColumnIndex(i);
                ImGui::TextDisabled("%s", ImGui::TableGetColumnName(i));

                if (i == 1)
                    ShowTooltip("Hook the inputs of this API, needed before they can be used");
                else if (i == 2)
                    ShowTooltip("Take the upscaler inputs from this API");
                else if (i == 3)
                    ShowTooltip("Find the inputs by pattern matching");
            }

            auto row = [&](const char* name, auto& enable, auto& use, auto pattern)
            {
                ScopedID rowId { name };

                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(name);

                ImGui::TableNextColumn();
                ConfigCheckbox("##enable", enable, "Hook the inputs of this API", true);

                ImGui::BeginDisabled(!enable.value_or_default());

                ImGui::TableNextColumn();
                ConfigCheckbox("##use", use, "Take the upscaler inputs from this API");

                ImGui::TableNextColumn();
                if constexpr (!std::is_null_pointer_v<decltype(pattern)>)
                    ConfigCheckbox("##pattern", *pattern, "Find the inputs by pattern matching", true);

                ImGui::EndDisabled();
            };

            row("FSR2", config->EnableFsr2Inputs, config->UseFsr2Inputs, &config->Fsr2Pattern);
            row("FSR3", config->EnableFsr3Inputs, config->UseFsr3Inputs, &config->Fsr3Pattern);
            row("FFX", config->EnableFfxInputs, config->UseFfxInputs, nullptr);

            ImGui::EndTable();
        }

        const bool restartNeeded = startFsr2 != config->EnableFsr2Inputs.value_or_default() ||
                                   startFsr3 != config->EnableFsr3Inputs.value_or_default() ||
                                   startFfx != config->EnableFfxInputs.value_or_default() ||
                                   startFsr2Pattern != config->Fsr2Pattern.value_or_default() ||
                                   startFsr3Pattern != config->Fsr3Pattern.value_or_default();

        if (restartNeeded)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, toneMapColor(IlionTheme::Themed(ImVec4(1.f, 0.8f, 0.f, 1.f), IlionTheme::Role::Warn)));
            ImGui::TextWrapped(Tr("Save Settings and restart the game for Enable and Pattern changes to apply"));
            ImGui::PopStyleColor();
        }
    }
}

void MenuCommon::RenderVsyncSettings(RenderMenuContext& ctx)
{
    auto& state = ctx.state;
    auto config = ctx.config;
    auto& currentFeature = ctx.currentFeature;
    auto& menuResScale = ctx.menuResScale;

    // DX11 & DX12 -----------------------------
    if (state.swapchainApi != Vulkan)
    {
        // V-SYNC -----------------------------
        SectionTitle("V-Sync Settings");
        {
            ScopedID sectionId { "V-Sync Settings" };

            auto OverrideVsync = config->OverrideVsync.value_or_default();
            {
                if (IlionTheme::Checkbox(TrL("Override Vsync"), &OverrideVsync))
                {
                    config->OverrideVsync = OverrideVsync;
                    LOG_DEBUG("Enabled OverrideVsync: {}", OverrideVsync);
                }
                ShowTooltip("Force override the game's V-Sync settings.\n\n"
                            "Useful for example when XeFG is locked to V-sync\n"
                            "frame cap despite toggling Vsync off (e.g. Nioh 3)\n\n"
                            "After changing, please Save Settings and restart.");
            }

            auto forceVsyncOn = config->ForceVsync.has_value() && config->ForceVsync.value();
            auto forceVsyncOff = config->ForceVsync.has_value() && !config->ForceVsync.value();
            bool vsyncChanged = false;

            if (IlionTheme::Checkbox(TrL("V-Sync On"), &forceVsyncOn))
            {
                if (forceVsyncOn)
                {
                    config->ForceVsync = true;
                    vsyncChanged = true;
                }
                else
                {
                    config->ForceVsync.reset();
                    vsyncChanged = true;
                }
            }
            SameLineIfRoom(CheckboxWidth(TrL("V-Sync Off")), 16.0f);

            if (IlionTheme::Checkbox(TrL("V-Sync Off"), &forceVsyncOff))
            {
                if (forceVsyncOff)
                {
                    config->ForceVsync = false;
                    vsyncChanged = true;
                }
                else
                {
                    config->ForceVsync.reset();
                    vsyncChanged = true;
                }
            }
            SameLineIfRoom(50.0f * menuResScale + ImGui::GetStyle().ItemInnerSpacing.x + ImGui::CalcTextSize(TrL("Sync Int."), nullptr, true).x,
                           16.0f);

            ImGui::BeginDisabled(!forceVsyncOn);

            ImGui::PushItemWidth(50.0f * menuResScale);

            auto vsyncBuf = StrFmt("%d", config->VsyncInterval.value_or_default());
            if (IlionTheme::BeginCombo(TrL("Sync Int."), vsyncBuf.c_str()))
            {
                if (ImGui::Selectable("0", config->VsyncInterval.value_or_default() == 0))
                {
                    config->VsyncInterval = 0;
                    vsyncChanged = true;
                }

                if (ImGui::Selectable("1", config->VsyncInterval.value_or_default() == 1))
                {
                    config->VsyncInterval = 1;
                    vsyncChanged = true;
                }

                if (ImGui::Selectable("2", config->VsyncInterval.value_or_default() == 2))
                {
                    config->VsyncInterval = 2;
                    vsyncChanged = true;
                }

                if (ImGui::Selectable("3", config->VsyncInterval.value_or_default() == 3))
                {
                    config->VsyncInterval = 3;
                    vsyncChanged = true;
                }

                ImGui::EndCombo();
            }
            ImGui::PopItemWidth();

            ShowTooltip("Controls the DXGI Present sync interval, which determines how\n"
                        "the swap chain waits for vertical refresh.\n\n"
                        "0  = Present immediately, no VSync wait.\n"
                        "1  = Sync to every refresh, normal VSync.\n"
                        "2+ = Present every N refreshes, reducing effective frame rate.\n\n"
                        "Higher values can reduce tearing but may increase latency and cap FPS.\n"
                        "For most games, use 0 for lowest latency or 1 for normal VSync.");

            ImGui::EndDisabled();
            SameLineIfRoom(ButtonWidth(TrL("Reset##10")), 16.0f);

            if (IlionTheme::Button(TrL("Reset##10")))
            {
                config->ForceVsync.reset();
                vsyncChanged = true;
            }

            ShowTooltip("Force V-Sync On/Off & Sync Interval options");

            if (vsyncChanged && state.activeFgOutput == FGOutput::XeFG && state.currentFG != nullptr)
            {
                // To prevent XeLL issues
                LOG_DEBUG("V-Sync change detected, forcing XeFG reset");
                state.WAR_xefgRequestFGToggle = true;
            }
        }
    }
}

void MenuCommon::RenderApiAndTextureSettings(RenderMenuContext& ctx)
{
    auto& state = ctx.state;
    auto config = ctx.config;
    auto& currentFeature = ctx.currentFeature;
    auto& menuResScale = ctx.menuResScale;

    // DX11 & DX12 -----------------------------
    if (state.swapchainApi != Vulkan)
    {
        // MIPMAP BIAS & Anisotropy -----------------------------
        if (ScopedCard::Shows("mipmap"))
        {
            ScopedCard card { "Mipmap Bias" };
            ScopedID sectionId { "Mipmap Bias" };
            if (config->MipmapBiasOverride.has_value() && _mipBias == 0.0f)
                _mipBias = config->MipmapBiasOverride.value();

            IlionTheme::SliderFloat(TrL("Mipmap Bias##2"), &_mipBias, -15.0f, 15.0f, "%.6f");
            ShowTooltip("Can help with blurry textures in broken games\n"
                        "Negative values will make textures sharper\n"
                        "Positive values will make textures more blurry\n\n"
                        "Has a small performance impact");

            ImGui::BeginDisabled(!config->MipmapBiasOverride.has_value());
            {
                ImGui::BeginDisabled(config->MipmapBiasScaleOverride.has_value() &&
                                     config->MipmapBiasScaleOverride.value());
                {
                    bool mbFixed = config->MipmapBiasFixedOverride.value_or_default();
                    if (IlionTheme::Checkbox(TrL("MB Fixed Override"), &mbFixed))
                    {
                        config->MipmapBiasScaleOverride.reset();
                        config->MipmapBiasFixedOverride = mbFixed;
                    }

                    ShowTooltip("Apply same override value to all textures");
                }
                ImGui::EndDisabled();

                SameLineIfFits(TrL("MB Scale Override"), 6.0f);

                ImGui::BeginDisabled(config->MipmapBiasFixedOverride.has_value() &&
                                     config->MipmapBiasFixedOverride.value());
                {
                    bool mbScale = config->MipmapBiasScaleOverride.value_or_default();
                    if (IlionTheme::Checkbox(TrL("MB Scale Override"), &mbScale))
                    {
                        config->MipmapBiasFixedOverride.reset();
                        config->MipmapBiasScaleOverride = mbScale;
                    }

                    ShowTooltip("Apply override value as scale multiplier\n"
                                "When using scale mode, please use positive\n"
                                "override values to increase sharpness!");
                }
                ImGui::EndDisabled();

                bool mbAll = config->MipmapBiasOverrideAll.value_or_default();
                if (IlionTheme::Checkbox(TrL("MB Override All Textures"), &mbAll))
                    config->MipmapBiasOverrideAll = mbAll;

                ShowTooltip("Override all textures mipmap values\n"
                            "Normally OptiScaler only overrides\n"
                            "below zero mipmap values!");
            }
            ImGui::EndDisabled();

            ImGui::BeginDisabled(config->MipmapBiasOverride.has_value() &&
                                 config->MipmapBiasOverride.value() == _mipBias);
            {
                if (IlionTheme::Button(TrL("Set")))
                {
                    config->MipmapBiasOverride = _mipBias;
                    state.lastMipBias = 100.0f;
                    state.lastMipBiasMax = -100.0f;
                }
            }
            ImGui::EndDisabled();

            SameLineIfRoom(ButtonWidth(TrL("Reset")), 6.0f);

            ImGui::BeginDisabled(!config->MipmapBiasOverride.has_value());
            {
                if (IlionTheme::Button(TrL("Reset")))
                {
                    config->MipmapBiasOverride.reset();
                    _mipBias = 0.0f;
                    state.lastMipBias = 100.0f;
                    state.lastMipBiasMax = -100.0f;
                }
            }
            ImGui::EndDisabled();

            if (currentFeature != nullptr && !currentFeature->IsFrozen())
            {
                if (IlionTheme::Button(TrL("Calculate Mipmap Bias")))
                    _showMipmapCalcWindow = true;
            }

            if (config->MipmapBiasOverride.has_value())
            {
                if (config->MipmapBiasFixedOverride.value_or_default())
                {
                    ImGui::TextWrapped(Tr("Current : %.3f / %.3f, Target: %.3f"), state.lastMipBias, state.lastMipBiasMax,
                                config->MipmapBiasOverride.value());
                }
                else if (config->MipmapBiasScaleOverride.value_or_default())
                {
                    ImGui::TextWrapped(Tr("Current : %.3f / %.3f, Target: Base * %.3f"), state.lastMipBias, state.lastMipBiasMax,
                                config->MipmapBiasOverride.value());
                }
                else
                {
                    ImGui::TextWrapped(Tr("Current : %.3f / %.3f, Target: Base + %.3f"), state.lastMipBias, state.lastMipBiasMax,
                                config->MipmapBiasOverride.value());
                }
            }
            else
            {
                ImGui::TextWrapped(Tr("Current : %.3f / %.3f"), state.lastMipBias, state.lastMipBiasMax);
            }

            ImGui::TextWrapped(Tr("Will be applied after RESOLUTION/PRESET change !!!"));
        }

        if (ScopedCard::Shows("anisotropy"))
        {
            ScopedCard card { "Anisotropic Filtering" };
            ScopedID sectionId { "Anisotropic Filtering" };
            ImGui::PushItemWidth(65.0f * menuResScale);

            auto selectedAF =
                config->AnisotropyOverride.has_value() ? std::to_string(config->AnisotropyOverride.value()) : "Auto";
            if (IlionTheme::BeginCombo(TrL("Force Anisotropic Filtering"), selectedAF.c_str()))
            {
                if (ImGui::Selectable(TrL("Auto"), !config->AnisotropyOverride.has_value()))
                    config->AnisotropyOverride.reset();

                if (ImGui::Selectable("1", config->AnisotropyOverride.value_or(0) == 1))
                    config->AnisotropyOverride = 1;

                if (ImGui::Selectable("2", config->AnisotropyOverride.value_or(0) == 2))
                    config->AnisotropyOverride = 2;

                if (ImGui::Selectable("4", config->AnisotropyOverride.value_or(0) == 4))
                    config->AnisotropyOverride = 4;

                if (ImGui::Selectable("8", config->AnisotropyOverride.value_or(0) == 8))
                    config->AnisotropyOverride = 8;

                if (ImGui::Selectable("16", config->AnisotropyOverride.value_or(0) == 16))
                    config->AnisotropyOverride = 16;

                ImGui::EndCombo();
            }

            ImGui::PopItemWidth();

            bool afComp = config->AnisotropyModifyComp.value_or_default();
            if (IlionTheme::Checkbox(TrL("Modify Compare"), &afComp))
                config->AnisotropyModifyComp = afComp;

            ShowTooltip("Update comparison filters");

            SameLineIfFits(TrL("Modify Min/Max"), 6.0f);

            bool afMinMax = config->AnisotropyModifyMinMax.value_or_default();
            if (IlionTheme::Checkbox(TrL("Modify Min/Max"), &afMinMax))
                config->AnisotropyModifyMinMax = afMinMax;

            ShowTooltip("Update min/max filters");

            bool afSkipPoint = config->AnisotropySkipPointFilter.value_or_default();
            if (IlionTheme::Checkbox(TrL("Skip Point Filters"), &afSkipPoint))
                config->AnisotropySkipPointFilter = afSkipPoint;

            ShowTooltip("Skip updating of point filters");

            ImGui::TextWrapped(Tr("Will might be applied after RESOLUTION/PRESET change !!!"));
        }
    }
}

void MenuCommon::RenderKeybindSettings(RenderMenuContext& ctx)
{
    auto config = ctx.config;

    SectionTitle("Keybinds");
    {
        ScopedID sectionId { "Keybinds" };

        ImGui::TextWrapped(Tr("Key combinations are currently NOT supported!"));
        ImGui::Text(Tr("Escape to cancel, Backspace to unbind"));
        ImGui::Spacing();

        static auto menu = Keybind("Menu", 10);
        static auto fpsOverlay = Keybind("FPS Overlay", 11);
        static auto fpsOverlayCycle = Keybind("FPS Overlay Cycle", 12);
        static auto fgEnable = Keybind("Frame Generation", 13);

        menu.Render(config->ShortcutKey);
        fpsOverlay.Render(config->FpsShortcutKey);
        fpsOverlayCycle.Render(config->FpsCycleShortcutKey);
        fgEnable.Render(config->FGShortcutKey);
    }
}

void MenuCommon::RenderMenuScaleSettings(RenderMenuContext& ctx)
{
    auto config = ctx.config;
    auto& menuResScale = ctx.menuResScale;

    SectionTitle("Menu");

    ImGui::PushItemWidth(100.0f * menuResScale);

    auto autoText = config->MenuScale.has_value() ? "Auto" : StrFmt("Auto (%3.1f)", menuResScale);
    // clang-format off
    const char* uiScales[] = { autoText.c_str(), "0.5", "0.6", "0.7", "0.8", "0.9", "1.0", "1.1",
                               "1.2", "1.3", "1.4", "1.5", "1.6", "1.7", "1.8", "1.9", "2.0" };
    // clang-format on

    const char* selectedScaleName = uiScales[_selectedScale];

    if (IlionTheme::BeginCombo(TrL("Menu Scale"), selectedScaleName))
    {
        for (int n = 0; n < std::size(uiScales); n++)
        {
            if (ImGui::Selectable(uiScales[n], (_selectedScale == n)))
            {
                _selectedScale = n;

                if (n == 0)
                    config->MenuScale.reset();
                else
                    config->MenuScale = 0.4f + (float) n / 10.0f;
            }
        }

        ImGui::EndCombo();
    }

    ImGui::PopItemWidth();
}

static const char* ApiName(API api)
{
    if (api == DX11)
        return "D3D11";

    if (api == DX12)
        return "D3D12";

    if (api == Vulkan)
        return "Vulkan";

    return "Unknown";
}

// Filled dot for on / found, hollow dot for off / missing. Goes into the next table cell
static void StatusFlag(const char* label, bool on, bool warnWhenOn = false)
{
    const ImVec4 onColor = toneMapColor(IlionTheme::Themed(ImVec4(0.35f, 0.85f, 0.45f, 1.f), IlionTheme::Role::Good));
    const ImVec4 warnColor = toneMapColor(IlionTheme::Themed(ImVec4(1.f, 0.8f, 0.f, 1.f), IlionTheme::Role::Warn));

    ImGui::TableNextColumn();

    const float lineHeight = ImGui::GetTextLineHeight();
    const float radius = lineHeight * 0.28f;
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const ImVec2 center(pos.x + lineHeight * 0.5f, pos.y + lineHeight * 0.5f);
    auto drawList = ImGui::GetWindowDrawList();

    if (on)
        drawList->AddCircleFilled(center, radius, ImGui::GetColorU32(warnWhenOn ? warnColor : onColor));
    else
        drawList->AddCircle(center, radius, ImGui::GetColorU32(ImGuiCol_TextDisabled), 0, 1.5f);

    ImGui::Dummy(ImVec2(lineHeight, lineHeight));
    ImGui::SameLine(0.0f, 0.0f);

    if (on)
        ImGui::TextUnformatted(Tr(label));
    else
        ImGui::TextDisabled("%s", Tr(label));
}

// Flags in as many columns as the widest translated label allows (up to four), so none is cut
struct StatusFlagItem
{
    const char* label;
    bool on;
    bool warnWhenOn = false;
};

static void StatusFlagGrid(const char* id, std::initializer_list<StatusFlagItem> items)
{
    const auto& style = ImGui::GetStyle();
    float widest = 0.0f;

    for (const auto& item : items)
        widest = std::max(widest, ImGui::CalcTextSize(Tr(item.label)).x);

    // Dot + label + the cell padding on both sides + the spacing between columns
    const float cell = ImGui::GetTextLineHeight() + widest + style.CellPadding.x * 2.0f + style.ItemSpacing.x;
    const int columns = std::clamp((int) (ImGui::GetContentRegionAvail().x / cell), 1, 4);

    if (ImGui::BeginTable(id, columns, ImGuiTableFlags_SizingStretchSame))
    {
        for (const auto& item : items)
            StatusFlag(item.label, item.on, item.warnWhenOn);

        ImGui::EndTable();
    }
}

// Which libraries and GPU features were found. Used by the Status tab and when no upscaler is available
void MenuCommon::RenderDetectedInfo(RenderMenuContext& ctx)
{
    auto& state = ctx.state;
    auto& primaryGpu = *ctx.primaryGpu;

    if (!ScopedCard::Shows("status_detected"))
        return;

    ScopedCard card { "Detected" };

    StatusFlagGrid("statusDetected", {
        { "nvngx.dll", state.nvngxExists },
        { "nvngx_dlss", state.NVNGX_DLSS_Path.has_value() },
        { "nvngx_dlssd", state.NVNGX_DLSSD_Path.has_value() },
        { "nvngx replacement", state.nvngxReplacement.has_value() },
        { "libxess", state.libxessExists || XeSSProxy::Module() != nullptr },
        { "FSR hooks", state.fsrHooks },
        { "FFX 3.1", FfxApiProxy::Dx12Module() != nullptr },
        { "FFX SR", FfxApiProxy::Dx12Module_SR() != nullptr },
        { "FFX FG", FfxApiProxy::Dx12Module_FG() != nullptr },
        { "OptiPatcher", state.isOptiPatcherSucceed },
        { "DLSS capable GPU", primaryGpu.dlssCapable },
        { "FSR4 capable GPU", primaryGpu.fsr4Support != FSR4Support::None, IsFsr4Int8(primaryGpu.fsr4Support) },
    });

    // Frame ids that used to be in the performance overlay
    const auto statusFg = state.currentFG;
    ImGui::TextDisabled(Tr("FGId: %llu, RfxId: %llu"), statusFg != nullptr ? (UINT64) statusFg->FrameCount() : 0ull,
                        (UINT64) state.reflexFrameId);

    TextWrappedDisabled("%s", Tr("Unofficial modified build, not an OptiScaler release"));
    ImGui::TextDisabled(Tr("Based on %s"), BuildInfo::ProductName());
    ImGui::TextDisabled("%s %s", state.gameExe.c_str(), state.gameName.c_str());
}

// Same dense look as the Status tab
void MenuCommon::RenderDetectedCard(RenderMenuContext& ctx)
{
    const auto& style = ImGui::GetStyle();

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(style.ItemSpacing.x, style.ItemSpacing.y * 0.5f));
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(style.CellPadding.x, 1.0f * ctx.menuResScale));

    RenderDetectedInfo(ctx);

    ImGui::PopStyleVar(2);
}

// ILION: the five things most asked for, only from what OptiScaler reports now; nothing when it doesn't know
void MenuCommon::RenderStatusSummary(RenderMenuContext& ctx)
{
    auto& state = ctx.state;
    auto& currentFeature = ctx.currentFeature;

    SectionTitle("Summary");

    if (!ImGui::BeginTable("##statusSummary", 2, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoSavedSettings))
        return;

    ImGui::TableSetupColumn("##label");
    ImGui::TableSetupColumn("##value", ImGuiTableColumnFlags_WidthStretch);

    auto row = [](const char* label, const std::string& value)
    {
        ImGui::TableNextColumn();
        ImGui::TextDisabled("%s", Tr(label));
        ImGui::TableNextColumn();
        ImGui::TextWrapped("%s", value.c_str());
    };

    if (!state.gameExe.empty())
        row("Game", state.gameExe + (state.gameName.empty() ? "" : " (" + state.gameName + ")"));

    const char* api = state.swapchainApi == DX11 ? "DirectX 11" : state.swapchainApi == DX12 ? "DirectX 12"
                      : state.swapchainApi == Vulkan                                     ? "Vulkan"
                                                                                         : nullptr;
    if (api != nullptr)
        row("API", api);

    const bool upscaling = currentFeature != nullptr && currentFeature->IsInited() && !currentFeature->IsFrozen();
    row("Upscaler", upscaling ? UpscalerLabel(currentFeature) : std::string(Tr("None")));

    auto fg = state.currentFG;
    if (fg != nullptr && fg->IsActive() && !fg->IsPaused())
        row("Frame Gen", StrFmt("%s %ux", fg->Name(), fg->GetInterpolatedFrameCount() + 1));
    else
        row("Frame Gen", Tr("Off"));

    const LowLatencyMode tech = fakenvapi::isUsingAsMainNvapi() ? fakenvapi::getCurrentMode() : LowLatencyMode::None;
    const char* techName = IlionLatency::TechName(tech);
    row("Latency", techName != nullptr ? std::string(techName) + (fakenvapi::isLowLatencyActive() ? "" : Tr(" (not enabled)"))
                                       : std::string(Tr("None")));

    ImGui::EndTable();
}

// ILION Essential mode: four small CRT screens above the tabs with what ILION already knows. Nothing is measured
// here; a value without evidence shows a "-".
//   Upscaler   the name of what runs (the dll that executed the last upscale for FFX, d4r for DLSS 4), as everywhere
//   Frame Gen  OptiScaler's FG and its real multiplier, or the multiplier DLSSG reports
//   Latency    the reducer fakenvapi has created, dimmed while it is not enabled
//   API        the API of the swapchain
void MenuCommon::RenderEssentialStrip(RenderMenuContext& ctx)
{
    auto& state = ctx.state;
    auto& currentFeature = ctx.currentFeature;

    struct Screen
    {
        const char* label;
        std::string value;
        bool lit = true; // false: the value is known but the thing is off
    };

    Screen screens[4];

    screens[0] = { "UPSCALER", currentFeature != nullptr && !currentFeature->IsFrozen() ? UpscalerLabel(currentFeature) : std::string("-") };

    auto fg = state.currentFG;
    if (fg != nullptr && fg->IsActive() && !fg->IsPaused())
    {
        // Some names already end with the multiplier ("XeFG 2x"): written once, as in the sidebar
        const unsigned multiplier = (unsigned) fg->GetInterpolatedFrameCount() + 1;
        std::string fgName = fg->Name();
        const std::string suffix = StrFmt(" %ux", multiplier);
        if (fgName.ends_with(suffix))
            fgName.erase(fgName.size() - suffix.size());
        screens[1] = { "FRAME GEN", StrFmt("%s %ux", fgName.c_str(), multiplier) };
    }
    else if (state.dlssgDetectedInterpolationCount > 0)
        screens[1] = { "FRAME GEN", StrFmt("DLSSG %dx", state.dlssgDetectedInterpolationCount + 1) };
    else
        screens[1] = { "FRAME GEN", Tr("Off"), false };

    const bool fakenvapiInUse = fakenvapi::isUsingAsMainNvapi();
    const char* tech = fakenvapiInUse ? IlionLatency::TechName(fakenvapi::getCurrentMode()) : nullptr;
    if (tech != nullptr)
        screens[2] = { "LATENCY", tech, fakenvapi::isLowLatencyActive() };
    else
        screens[2] = { "LATENCY", "-", false };

    const char* api = IlionLatency::SwapchainApiName(state.swapchainApi);
    screens[3] = { "API", strcmp(api, "?") == 0 ? std::string("-") : std::string(api) };

    const auto& style = ImGui::GetStyle();
    ImFont* font = IlionTheme::crtFont != nullptr ? IlionTheme::crtFont : ImGui::GetFont();
    const float fontSize = ImGui::GetFontSize();
    const float labelSize = std::floor(fontSize * 0.78f);
    const float pad = std::floor(fontSize * 0.45f);
    const float height = std::floor(labelSize + fontSize + pad * 2.5f);
    const float width = std::floor((ImGui::GetContentRegionAvail().x - style.ItemSpacing.x * 3.0f) / 4.0f);

    // Seen through the curved glass the characters look a little bigger: about 7 % for the value, 5 % for the label.
    // The screen keeps its size; the bigger text still fits in it (the slack of the padding is larger than the gain).
    constexpr float valueLens = 1.07f;
    constexpr float labelLens = 1.05f;
    const float valueSize = fontSize * valueLens;
    const float labelShown = labelSize * labelLens;

    // Lit phosphor on a dark green-black tube: radioactive yellow values, phosphor green labels, radioactive orange
    // for something known but switched off
    const ImVec4 yellow = toneMapColor(ImVec4(1.00f, 0.88f, 0.14f, 1.0f));
    const ImVec4 orange = toneMapColor(ImVec4(1.00f, 0.52f, 0.10f, 1.0f));
    const ImVec4 green = toneMapColor(ImVec4(0.40f, 0.88f, 0.42f, 1.0f));
    const ImU32 tube = ImGui::GetColorU32(ImVec4(0.016f, 0.028f, 0.022f, 1.0f));
    const ImU32 tubeDeep = ImGui::GetColorU32(ImVec4(0.000f, 0.006f, 0.004f, 0.55f));
    const ImU32 clear = ImGui::GetColorU32(ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    const ImU32 rim = ImGui::GetColorU32(ImVec4(0.32f, 0.34f, 0.31f, 1.0f));
    const ImU32 rimDark = ImGui::GetColorU32(ImVec4(0.0f, 0.0f, 0.0f, 0.55f));
    ImDrawList* draw = ImGui::GetWindowDrawList();

    for (int i = 0; i < 4; i++)
    {
        if (i > 0)
            ImGui::SameLine();

        ImGui::PushID(i);
        const ImVec2 min = ImGui::GetCursorScreenPos();
        const ImVec2 max(min.x + width, min.y + height);
        ImGui::InvisibleButton("##screen", ImVec2(width, height));
        const bool hovered = ImGui::IsItemHovered();
        ImGui::PopID();

        const float rounding = std::floor(fontSize * 0.35f);
        const ImVec2 center((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);

        // The same bezel line as before, and the tube behind the glass
        draw->AddRectFilled(ImVec2(min.x - 1.0f, min.y - 1.0f), ImVec2(max.x + 1.0f, max.y + 1.0f), rimDark, rounding + 1.0f);
        draw->AddRectFilled(min, max, tube, rounding);

        draw->PushClipRect(ImVec2(min.x + 1.0f, min.y + 1.0f), ImVec2(max.x - 1.0f, max.y - 1.0f), true);

        // Concave glass: the surface sinks towards the middle, so the edges catch the light and the centre is deeper.
        // Left and right edges lighter, fading to the middle; the centre darkened a little more.
        const ImU32 edgeLight = ImGui::GetColorU32(ImVec4(0.75f, 1.0f, 0.80f, 0.060f));
        draw->AddRectFilledMultiColor(min, ImVec2(center.x, max.y), edgeLight, clear, clear, edgeLight);
        draw->AddRectFilledMultiColor(ImVec2(center.x, min.y), max, clear, edgeLight, edgeLight, clear);
        draw->AddRectFilledMultiColor(ImVec2(min.x + width * 0.18f, min.y), ImVec2(center.x, max.y), clear, tubeDeep, tubeDeep, clear);
        draw->AddRectFilledMultiColor(ImVec2(center.x, min.y), ImVec2(max.x - width * 0.18f, max.y), tubeDeep, clear, clear, tubeDeep);

        // The lower lip of the hollow catches the light from above; the top edge stays in the shadow of the bezel
        draw->AddRectFilledMultiColor(ImVec2(min.x, max.y - height * 0.22f), max, clear, clear, edgeLight, edgeLight);
        draw->AddRectFilledMultiColor(min, ImVec2(max.x, min.y + height * 0.20f), rimDark, rimDark, clear, clear);

        // A window reflected on the curved surface: a thin arc that bows towards the centre
        draw->AddBezierQuadratic(ImVec2(min.x + width * 0.10f, min.y + height * 0.16f),
                                 ImVec2(center.x, min.y + height * 0.36f),
                                 ImVec2(max.x - width * 0.10f, min.y + height * 0.16f),
                                 ImGui::GetColorU32(ImVec4(0.85f, 1.0f, 0.90f, 0.07f)), 1.0f);

        // Fine scan lines of the old tube
        const ImU32 scan = ImGui::GetColorU32(ImVec4(0.0f, 0.0f, 0.0f, 0.20f));
        for (float y = min.y + 2.0f; y < max.y - 1.0f; y += 3.0f)
            draw->AddLine(ImVec2(min.x + 1.0f, y), ImVec2(max.x - 1.0f, y), scan);

        draw->PopClipRect();
        draw->AddRect(min, max, rim, rounding);

        // Label and value, cut with dots when the value doesn't fit; the whole value in the tooltip
        const Screen& screen = screens[i];
        const char* label = Tr(screen.label);
        const float inner = width - pad * 2.0f;

        std::string value = screen.value;
        const auto measure = [&](const std::string& text) { return font->CalcTextSizeA(valueSize, FLT_MAX, 0.0f, text.c_str()).x; };
        const bool cut = measure(value) > inner;

        if (cut)
        {
            // One whole character at a time: its continuation bytes, then its first byte
            while (!value.empty() && measure(value + "...") > inner)
            {
                while (!value.empty() && ((unsigned char) value.back() & 0xC0) == 0x80)
                    value.pop_back();
                if (!value.empty())
                    value.pop_back();
            }

            value += "...";
        }

        draw->PushClipRect(min, max, true);

        // Phosphor glow: the letters spread a little light around them, then the letters
        const auto glowText = [&](float size, const ImVec2& pos, const ImVec4& colour, const char* text)
        {
            const ImU32 glow = ImGui::GetColorU32(ImVec4(colour.x, colour.y, colour.z, 0.18f));
            draw->AddText(font, size, ImVec2(pos.x - 0.6f, pos.y), glow, text);
            draw->AddText(font, size, ImVec2(pos.x + 0.6f, pos.y), glow, text);
            draw->AddText(font, size, ImVec2(pos.x, pos.y + 0.6f), glow, text);
            draw->AddText(font, size, pos, ImGui::GetColorU32(colour), text);
        };

        glowText(labelShown, ImVec2(std::floor(min.x + pad), std::floor(min.y + pad * 0.75f)),
                 ImVec4(green.x, green.y, green.z, 0.85f), label);

        const ImVec2 valuePos(std::floor(min.x + pad), std::floor(max.y - pad * 0.9f - valueSize));
        glowText(valueSize, valuePos, screen.lit ? yellow : orange, value.c_str());

        draw->PopClipRect();

        if (hovered && (cut || !screen.lit))
            WrappedTooltip("%s%s", screen.value.c_str(), screen.lit ? "" : Tr(" (not enabled)"));
    }

    ImGui::Spacing();
}

void MenuCommon::RenderStatusInfo(RenderMenuContext& ctx)
{
    auto& state = ctx.state;
    auto config = ctx.config;
    auto& currentFeature = ctx.currentFeature;
    auto& menuResScale = ctx.menuResScale;
    auto& primaryGpu = *ctx.primaryGpu;

    const bool active = currentFeature != nullptr && !currentFeature->IsFrozen();

    // Dense on purpose, the whole page is meant to fit in a single screenshot
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,
                        ImVec2(ImGui::GetStyle().ItemSpacing.x, ImGui::GetStyle().ItemSpacing.y * 0.5f));
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(ImGui::GetStyle().CellPadding.x, 1.0f * menuResScale));

    // Label and value, two pairs per row
    auto pair = [&](const char* label, const std::string& value, bool dim = false)
    {
        ImGui::TableNextColumn();
        TextWrappedDisabled("%s", Tr(label));

        ImGui::TableNextColumn();
        if (dim)
            TextWrappedDisabled("%s", value.c_str());
        else
            ImGui::TextWrapped("%s", value.c_str());
    };

    auto flag = [](const char* label, bool on, bool warnWhenOn = false) { StatusFlag(label, on, warnWhenOn); };

    auto setupPairs = []()
    {
        ImGui::TableSetupColumn("##l1", ImGuiTableColumnFlags_WidthStretch, 0.9f);
        ImGui::TableSetupColumn("##v1", ImGuiTableColumnFlags_WidthStretch, 1.1f);
        ImGui::TableSetupColumn("##l2", ImGuiTableColumnFlags_WidthStretch, 0.9f);
        ImGui::TableSetupColumn("##v2", ImGuiTableColumnFlags_WidthStretch, 1.1f);
    };

    // SYSTEM -----------------------------
    if (ScopedCard::Shows("status_system"))
    {
        ScopedCard card { "System" };

        // Long values get the whole row
        if (ImGui::BeginTable("statusSystem", 2, ImGuiTableFlags_SizingFixedFit))
        {
            ImGui::TableSetupColumn("##label");
            ImGui::TableSetupColumn("##value", ImGuiTableColumnFlags_WidthStretch);

            auto row = [&](const char* label, const std::string& value)
            {
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%s", Tr(label));
                ImGui::TableNextColumn();
                ImGui::TextWrapped("%s", value.c_str());
            };

            row("GPU", primaryGpu.name);
            row("Game", state.gameExe + (state.gameName.empty() ? "" : " (" + state.gameName + ")"));

            if (state.detectedQuirks.size() > 0)
            {
                std::string quirks;
                for (const auto& quirk : state.detectedQuirks)
                    quirks += quirks.empty() ? quirk : ", " + quirk;

                ImGui::TableNextColumn();
                ImGui::TextDisabled(Tr("Quirks"));
                ImGui::TableNextColumn();
                ImGui::TextWrapped("%s", quirks.c_str());
            }

            ImGui::EndTable();
        }

        std::string ratioOverride = "Off";
        if (config->UpscaleRatioOverrideEnabled.value_or_default())
            ratioOverride = StrFmt("All %.2f", config->UpscaleRatioOverrideValue.value_or_default());
        else if (config->QualityRatioOverrideEnabled.value_or_default())
            ratioOverride = "Per preset";

        if (ImGui::BeginTable("statusRuntime", 4, ImGuiTableFlags_SizingStretchProp))
        {
            setupPairs();

            pair("API", StrFmt("%s%s", ApiName(state.api),
                               IdentifyGpu::gameUsesDxvk()          ? " (DXVK)"
                               : IdentifyGpu::gameUsesVkd3dProton() ? " (VKD3D)"
                                                                    : ""));
            pair("Swapchain", ApiName(state.swapchainApi));
            pair("Input", ApiUpscalerInputName(state.currentInputApiName));

            if (active)
            {
                pair("Upscaler",
                     StrFmt("%s%s", UpscalerLabel(currentFeature).c_str(),
                            (currentFeature->IsWithDx12() && state.api != DX12) ? " w/Dx12" : ""));
                pair("Render", StrFmt("%dx%d", currentFeature->RenderWidth(), currentFeature->RenderHeight()));
                pair("Output", StrFmt("%dx%d (%.2fx)", currentFeature->TargetWidth(), currentFeature->TargetHeight(),
                                      (float) currentFeature->TargetWidth() / (float) currentFeature->RenderWidth()));
                pair("Display", StrFmt("%dx%d (%.2fx)", currentFeature->DisplayWidth(), currentFeature->DisplayHeight(),
                                       (float) currentFeature->DisplayWidth() / (float) currentFeature->RenderWidth()));
                pair("Frames", std::to_string(currentFeature->FrameCount()));
            }
            else
            {
                pair("Upscaler", "None", true);
                pair("Render", "-", true);
                pair("Output", "-", true);
                pair("Display", "-", true);
                pair("Frames", "-", true);
            }

            auto fgInput = state.activeFgInput;
            if (fgInput != FGInput::NoFG)
                pair("FG Input", EnumToCode(fgInput));
            else
                pair("FG Input", "Off", true);

            // FG Output is OptiScaler's own frame generation. With a replacement of DLSSG (DLSS Enabler, Nukem's...)
            // there is none of that and the replacement is what produces the frames: it gets its own rows, so that
            // "Off" here isn't read as no frame generation at all.
            auto fgOutput = state.activeFgOutput;
            if (fgOutput != FGOutput::NoFG)
                pair("FG Output", EnumToCode(fgOutput));
            else
                pair("FG Output (OptiScaler)", "Off", true);

            if (const char* nvngxFg = NvngxFgName(state.activeFgNvngx); nvngxFg != nullptr)
                pair("FG Nvngx", nvngxFg);

            if (state.activeFgNvngx != FGNvngxReplacement::None || state.dlssgDetectedInterpolationCount > 0)
            {
                if (state.dlssgDetectedInterpolationCount > 0)
                    pair("DLSSG", StrFmt("ON %dx", state.dlssgDetectedInterpolationCount + 1));
                else if (state.swapchainApi == DX12)
                    pair("DLSSG", "Off", true);
                else
                    pair("DLSSG", "?", true);
            }

            pair("Ratio override", ratioOverride, ratioOverride == "Off");
            pair("Mipmap bias",
                 config->MipmapBiasOverride.has_value() ? StrFmt("%.3f", config->MipmapBiasOverride.value()) : "Off",
                 !config->MipmapBiasOverride.has_value());
            pair("Anisotropy",
                 config->AnisotropyOverride.has_value() ? std::to_string(config->AnisotropyOverride.value()) : "Auto",
                 !config->AnisotropyOverride.has_value());

            ImGui::EndTable();
        }
    }

    // DETECTED -----------------------------
    RenderDetectedInfo(ctx);

    // HOOKS AND CONFIG -----------------------------
    if (ScopedCard::Shows("status_hooks"))
    {
        ScopedCard card { "Hooks / Config" };

        StatusFlagGrid("statusConfig", {
            { "DXGI spoofing", config->DxgiSpoofing.value_or_default() },
            { "VLK spoofing", config->VulkanSpoofing.value_or_default() },
            { "VLK ext. spoofing", config->VulkanExtensionSpoofing.value_or_default() },
            { "Ntdll hooks", config->UseNtdllHooks.value_or_default() },
            { "Disable overlays", config->DisableOverlays.value_or_default(), true },
            { "ASI plugins", config->LoadAsiPlugins.value_or_default() },
            { "Precomp. shaders", config->UsePrecompiledShaders.value_or_default() },
            { "Extended limits", config->ExtendedLimits.value_or_default(), true },
            { "DRS min override", config->DrsMinOverrideEnabled.value_or_default() },
            { "DRS max override", config->DrsMaxOverrideEnabled.value_or_default() },
            { "Compute root sig.", config->RestoreComputeSignature.value_or_default() },
            { "Graphic root sig.", config->RestoreGraphicSignature.value_or_default() },
        });

        // Only shown when something was overridden
        std::string barriers;
        auto addBarrier = [&](const char* name, bool set)
        {
            if (!set)
                return;

            barriers += barriers.empty() ? name : std::string(", ") + name;
        };
        addBarrier("Color", config->ColorResourceBarrier.has_value());
        addBarrier("Depth", config->DepthResourceBarrier.has_value());
        addBarrier("Motion", config->MVResourceBarrier.has_value());
        addBarrier("Exposure", config->ExposureResourceBarrier.has_value());
        addBarrier("Mask", config->MaskResourceBarrier.has_value());
        addBarrier("Output", config->OutputResourceBarrier.has_value());

        if (!barriers.empty())
        {
            TextWrappedDisabled("%s", Tr("Barriers overridden"));
            ImGui::TextWrapped("%s", barriers.c_str());
        }
    }

    // INIT FLAGS -----------------------------
    if (ScopedCard::Shows("status_init") && active)
    {
        ScopedCard card { "Init Flags" };

        StatusFlagGrid("statusInit", {
            { "Auto exposure", currentFeature->AutoExposure() },
            { "Depth inverted", currentFeature->DepthInverted() },
            { "HDR", currentFeature->IsHdr() },
            { "Display res. MV", !currentFeature->LowResMV() },
            { "Jitter cancel.", currentFeature->JitteredMV() },
            { "Output scaling", config->OutputScalingEnabled.value_or_default() },
            { "Sharp. override", config->OverrideSharpness.value_or_default() },
            { "V-Sync override", config->OverrideVsync.value_or_default() },
        });
    }

    ImGui::PopStyleVar(2);
}

static bool IsUpscalerActive(IFeature* feature) { return feature != nullptr && !feature->IsFrozen(); }

static float FooterPlotHeight() { return ImGui::GetFrameHeight() * 1.0f; }

// Everything RenderMainMenuFooter draws, every item is followed by the item spacing
static float FooterHeight()
{
    const auto& style = ImGui::GetStyle();

    // The last frame height and item spacing are the INFO line
    return 1.0f + style.ItemSpacing.y * 6.0f + ImGui::GetTextLineHeight() + FooterPlotHeight() +
           ImGui::GetFrameHeight() * 2.0f;
}

// ILION: ReShade inside ILION (misc/IlionReShade.h). A card in Graphics and a wide side panel joined to the main
// window: the window grows by the panel's width, the tabs keep theirs. Everything shown comes from what ReShade
// reports back; controls appear only for effects ReShade has loaded.
static float ReShadePanelWidth(float scale) { return std::floor(440.0f * scale); }

// How far the panel is open, 0..1: it slides open and closed in about a sixth of a second, only while it moves
static float reshadePanelAmount = 0.0f;

static void AdvanceReShadePanel()
{
    const float target = reshadePanelOpen ? 1.0f : 0.0f;
    if (reshadePanelAmount == target)
        return;

    const float step = std::min(ImGui::GetIO().DeltaTime, 0.05f) * 6.0f;
    reshadePanelAmount = target > reshadePanelAmount ? std::min(target, reshadePanelAmount + step)
                                                     : std::max(target, reshadePanelAmount - step);
}

// The user's added effects, saved as "file|technique;file|technique"
static std::vector<IlionReShade::EffectRef> ReShadeAdded()
{
    std::vector<IlionReShade::EffectRef> list;
    const std::string saved = Config::Instance()->ReShadeAddedEffects.value_or("");
    size_t start = 0;

    while (start < saved.size())
    {
        const size_t end = std::min(saved.find(';', start), saved.size());
        const std::string item = saved.substr(start, end - start);
        const size_t bar = item.find('|');

        if (!item.empty())
            list.push_back({ item.substr(0, bar), bar == std::string::npos ? std::string() : item.substr(bar + 1) });

        start = end + 1;
    }

    return list;
}

static void SaveReShadeAdded(const std::vector<IlionReShade::EffectRef>& list)
{
    std::string saved;
    for (const auto& ref : list)
        saved += (saved.empty() ? "" : ";") + ref.file + "|" + ref.technique;

    Config::Instance()->ReShadeAddedEffects = saved;
    Config::Instance()->SaveMenuPreferences();
}

// The panel's entries for the bridge: the catalogue, then the added ones (no duplicates); sent only when they change
static void SyncReShadeEffects()
{
    static std::string sent = "-";
    const std::string saved = Config::Instance()->ReShadeAddedEffects.value_or("");

    if (saved == sent)
        return;

    sent = saved;
    std::vector<IlionReShade::EffectRef> list;
    for (const char* file : IlionReShade::kCatalogue)
        list.push_back({ file, "" });
    for (const auto& ref : ReShadeAdded())
        list.push_back(ref);

    IlionReShade::SetPanelEffects(list);
}

static const char* ReShadeEffectName(const std::string& file)
{
    static const std::pair<const char*, const char*> names[] = {
        { "FakeHDR.fx", "Fake HDR (SDR simulation)" },
        { "Levels.fx", "Levels" },
        { "Tonemap.fx", "Tonemap" },
        { "Curves.fx", "Curves" },
        { "LiftGammaGain.fx", "Lift Gamma Gain" },
        { "qUINT_lightroom.fx", "Lightroom" },
        { "CAS.fx", "CAS (contrast adaptive sharpening)" },
        { "LumaSharpen.fx", "LumaSharpen" },
        { "AdaptiveSharpen.fx", "AdaptiveSharpen" },
        { "Deband.fx", "Deband" },
    };

    for (const auto& [f, name] : names)
    {
        if (file == f)
            return name;
    }

    return nullptr;
}

// What each catalogue effect is for, in one line; the sharpening ones say how they differ from ILION's own RCAS
static const char* ReShadeEffectNote(const std::string& file)
{
    if (file == "FakeHDR.fx")
        return "A visual simulation on the SDR image, not real HDR.";
    if (file == "Levels.fx")
        return "Black and white points of the image.";
    if (file == "Tonemap.fx")
        return "Gamma, exposure and tonal response.";
    if (file == "Curves.fx")
        return "Contrast through a tone curve.";
    if (file == "LiftGammaGain.fx")
        return "Colour correction of shadows, midtones and highlights.";
    if (file == "qUINT_lightroom.fx")
        return "Advanced colour and tone adjustments in one effect.";
    if (file == "CAS.fx")
        return "AMD's contrast adaptive sharpening as a ReShade effect, on the final image, HUD included. ILION's own sharpness works inside the upscaler instead.";
    if (file == "LumaSharpen.fx")
        return "Sharpens brightness only, without colour fringes. Applies on the final image, HUD included.";
    if (file == "AdaptiveSharpen.fx")
        return "Sharpens edges more than flat areas. Applies on the final image, HUD included.";
    if (file == "Deband.fx")
        return "Reduces colour banding in gradients (skies, fog).";
    return nullptr;
}

// ILION: why ReShade isn't working, when the reason is known; nullptr otherwise. ReShade loaded as ReShade64.dll
// (LoadReShade, Instalar_ReShade.bat) hooks DirectX and OpenGL only: in Vulkan it works only as its own Vulkan layer.
// ReShade refuses a second instance in a process, so LoadReShade's copy, loaded first, also keeps its layer out
static const char* ReShadeDiagnosis(const IlionReShade::Snapshot& rs)
{
    const bool vulkan = State::Instance().api == Vulkan;
    const bool silent = rs.registered && !rs.presented && rs.registeredMs > 5000;

    if (!rs.moduleFound)
        return vulkan ? "In Vulkan ReShade only works through its own Vulkan layer: install it with ReShade's official setup "
                        "(choose Vulkan and the version with add-ons) and restart the game. Instalar_ReShade.bat covers "
                        "DirectX only."
                      : "ReShade not detected. Install it with Instalar_ReShade.bat (it enables LoadReShade in "
                        "OptiScaler.ini) and restart the game.";

    if (silent && vulkan && rs.fromLoadReShade)
        return "ReShade loaded through LoadReShade hooks DirectX and OpenGL only, not Vulkan, and keeps ReShade's Vulkan "
               "layer from loading. For this Vulkan game set [Plugins] LoadReShade=false and install ReShade with its "
               "official setup (Vulkan, version with add-ons).";

    if (silent)
        return "ReShade is connected but presents no frames. ReShade's own log (ReShade.log next to the game) tells why.";

    return nullptr;
}

void MenuCommon::RenderReShadeCard(RenderMenuContext& ctx)
{
    IlionReShade::Poll();
    const auto rs = IlionReShade::Get();

    SectionTitle("ReShade");

    const char* diagnosis = ReShadeDiagnosis(rs);

    if (!rs.moduleFound)
        TextWrappedDisabled("%s", Tr(diagnosis));
    else if (!rs.registered)
        TextWrappedColored(toneMapColor(IlionTheme::Themed(ImVec4(1.f, 0.8f, 0.f, 1.f), IlionTheme::Role::Warn)), "%s", Tr(rs.error.c_str()));
    else
        ImGui::TextColored(toneMapColor(IlionTheme::Themed(ImVec4(0.35f, 0.85f, 0.45f, 1.f), IlionTheme::Role::Good)), "%s %s",
                           Tr("ReShade connected"), rs.version.c_str());

    if (rs.moduleFound && diagnosis != nullptr)
        TextWrappedColored(toneMapColor(IlionTheme::Themed(ImVec4(1.f, 0.8f, 0.f, 1.f), IlionTheme::Role::Warn)), "%s", Tr(diagnosis));

    const char* key = reshadePanelOpen ? "Close ReShade panel" : "Open ReShade panel";
    if (IlionTheme::Button(TrL(key)))
        reshadePanelOpen = !reshadePanelOpen;

    if (ImGui::IsItemHovered())
        WrappedTooltip("%s", Tr("ReShade's effects controlled from ILION, in a panel next to this window. ReShade's own menu isn't needed."));
}

// The controls of one effect: on/off, reset, its parameters as ReShade defines them; added effects get a remove key
static void RenderReShadeEffect(const IlionReShade::Effect& effect, bool added)
{
    const std::string id = effect.ref.file + "|" + effect.ref.technique;
    ImGui::PushID(id.c_str());

    const char* known = ReShadeEffectName(effect.ref.file);
    std::string title = known != nullptr ? std::string(Tr(known)) : effect.ref.file;
    if (added && !effect.ref.technique.empty())
        title += "  (" + effect.ref.technique + ")";

    if (auto ch = ScopedCollapsingHeader((title + "###" + id).c_str()); ch.IsHeaderOpen())
    {
        ScopedIndent indent {};

        if (const char* note = ReShadeEffectNote(effect.ref.file); note != nullptr && !added)
            TextWrappedDisabled("%s", Tr(note));

        if (!effect.loaded)
        {
            TextWrappedDisabled("%s %s", effect.ref.file.c_str(),
                                Tr("isn't loaded: it isn't installed, it failed to compile or a file it includes is missing (see ReShade.log)."));
        }
        else
        {
            bool enabled = effect.enabled;
            if (IlionTheme::Checkbox(TrL("On##effect"), &enabled))
                IlionReShade::SetTechnique(effect.ref, enabled);

            SameLineIfRoom(ButtonWidth(TrL("Reset values")), 16.0f);
            if (IlionTheme::Button(TrL("Reset values")))
                IlionReShade::ResetEffect(effect.ref.file);

            const float width = std::min(ImGui::GetFontSize() * 14.0f, ImGui::GetContentRegionAvail().x * 0.55f);

            for (const auto& u : effect.uniforms)
            {
                ImGui::PushID(u.name.c_str());
                float values[4] = { u.values[0], u.values[1], u.values[2], u.values[3] };
                bool changed = false;

                ImGui::SetNextItemWidth(width);

                if (u.type == 2)
                {
                    bool on = values[0] != 0.0f;
                    if (IlionTheme::Checkbox(u.label.c_str(), &on))
                    {
                        values[0] = on ? 1.0f : 0.0f;
                        changed = true;
                    }
                }
                else if (u.type == 1)
                {
                    int ints[4] = { (int) values[0], (int) values[1], (int) values[2], (int) values[3] };
                    const int lo = (int) u.min, hi = (int) u.max;
                    changed = u.hasRange ? ImGui::SliderScalarN(u.label.c_str(), ImGuiDataType_S32, ints, u.count, &lo, &hi)
                                         : ImGui::DragScalarN(u.label.c_str(), ImGuiDataType_S32, ints, u.count, 1.0f);

                    for (int i = 0; i < 4; i++)
                        values[i] = (float) ints[i];
                }
                else
                {
                    changed = u.hasRange ? ImGui::SliderScalarN(u.label.c_str(), ImGuiDataType_Float, values, u.count, &u.min, &u.max, "%.3f")
                                         : ImGui::DragScalarN(u.label.c_str(), ImGuiDataType_Float, values, u.count, 0.01f, nullptr, nullptr, "%.3f");
                }

                if (changed)
                    IlionReShade::SetUniform(effect.ref.file, u.name, values, u.count);

                ImGui::PopID();
            }

            if (effect.skipped > 0)
                TextWrappedDisabled(Tr("%d parameter(s) of this effect can't be shown here (matrices or arrays)."), effect.skipped);
        }

        if (added)
        {
            if (IlionTheme::Button(TrL("Remove from the panel")))
            {
                auto list = ReShadeAdded();
                list.erase(std::remove_if(list.begin(), list.end(),
                                          [&](const IlionReShade::EffectRef& r) { return r.file == effect.ref.file && r.technique == effect.ref.technique; }),
                           list.end());
                SaveReShadeAdded(list);
            }

            if (ImGui::IsItemHovered())
                WrappedTooltip("%s", Tr("Only takes the effect out of ILION's panel: its file stays installed."));
        }
    }

    ImGui::PopID();
}

// ILION: effect catalogue (misc/IlionReShadeCatalog.h). ReShade's official package list, read only when asked; one
// effect at a time is downloaded with the files it needs on a background thread. When files changed, ReShade loads
// its effects again (a new file isn't picked up by reloading it by name, only by a full reload).
static IlionReShadeCatalog::Snapshot reshadeCatalogue;
static uint64_t reshadeCatalogueRevision = 0;
static std::string reshadeWaitingFor; // installed, until ReShade lists it
static double reshadeWaitingSince = 0.0;

static bool SameFile(const std::string& a, const std::string& b) { return _stricmp(a.c_str(), b.c_str()) == 0; }

static bool InDefaultCatalogue(const std::string& file)
{
    for (const char* f : IlionReShade::kCatalogue)
    {
        if (SameFile(file, f))
            return true;
    }

    return false;
}

// Finished installs and removals: the panel's list follows them and ReShade reloads only when files changed
static void ProcessReShadeCatalogue()
{
    for (const auto& done : IlionReShadeCatalog::TakeFinished())
    {
        auto list = ReShadeAdded();
        const auto same = [&](const IlionReShade::EffectRef& r) { return SameFile(r.file, done.effect); };

        if (done.installed)
        {
            if (!InDefaultCatalogue(done.effect) && std::none_of(list.begin(), list.end(), same))
            {
                list.push_back({ done.effect, "" });
                SaveReShadeAdded(list);
            }

            reshadeWaitingFor = done.effect;
            reshadeWaitingSince = ImGui::GetTime();
        }
        else
        {
            const size_t before = list.size();
            list.erase(std::remove_if(list.begin(), list.end(), same), list.end());
            if (list.size() != before)
                SaveReShadeAdded(list);

            if (SameFile(reshadeWaitingFor, done.effect))
                reshadeWaitingFor.clear();
        }

        if (done.changed)
            IlionReShade::ReloadEffect("");
    }
}

static std::string CatalogueLower(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return (char) std::tolower(c); });
    return text;
}

// One effect of the catalogue: its state and the key for it
static void RenderCatalogueEffect(size_t package, const std::string& file, bool busy, const ImVec4& good, const ImVec4& warn)
{
    using Kind = IlionReShadeCatalog::EffectState::Kind;
    const auto state = IlionReShadeCatalog::StateOf(file);
    ImGui::PushID(file.c_str());

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(file.c_str());

    if (state.needsDepth)
    {
        ImGui::SameLine();
        ImGui::TextColored(warn, "%s", Tr("depth"));
        if (ImGui::IsItemHovered())
            WrappedTooltip("%s", Tr("Reads the game's depth buffer: it may show nothing, or look wrong, in games that don't give ReShade their depth."));
    }

    const char* key = nullptr;

    if (state.kind == Kind::Working)
    {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", Tr("downloading..."));
    }
    else if (state.kind == Kind::OnDisk && !state.byIlion)
    {
        ImGui::SameLine();
        ImGui::TextColored(good, "%s", Tr("installed"));
        if (ImGui::IsItemHovered())
            WrappedTooltip("%s", Tr("Already in the shaders folder (put there by Instalar_ReShade.bat or by you): the catalogue doesn't touch it."));
    }
    else
    {
        key = state.kind == Kind::OnDisk ? "Remove##catalogue" : "Install##catalogue";
    }

    if (key != nullptr)
    {
        ImGui::SameLine();
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, ImGui::GetContentRegionAvail().x - ButtonWidth(TrL(key))));
        ImGui::BeginDisabled(busy);

        if (IlionTheme::Button(TrL(key)))
        {
            if (state.kind == Kind::OnDisk)
                IlionReShadeCatalog::Remove(file);
            else
                IlionReShadeCatalog::Install(package, file);
        }

        ImGui::EndDisabled();

        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            WrappedTooltip("%s", Tr(state.kind == Kind::OnDisk
                                        ? "Deletes the files the catalogue installed for this effect. Files other effects use, and files that were already there, stay."
                                        : "Downloads only this effect and the files it needs, then ReShade loads its effects again."));
    }

    if (state.kind == Kind::Failed)
        TextWrappedColored(warn, "%s%s%s", Tr(state.message.c_str()), state.detail.empty() ? "" : ": ", state.detail.c_str());

    ImGui::PopID();
}

static void RenderReShadeCatalogue(const IlionReShade::Snapshot& rs, const ImVec4& good, const ImVec4& warn)
{
    if (const uint64_t revision = IlionReShadeCatalog::Revision(); revision != reshadeCatalogueRevision)
    {
        reshadeCatalogueRevision = revision;
        reshadeCatalogue = IlionReShadeCatalog::Get();
    }

    const auto& cat = reshadeCatalogue;
    using Index = IlionReShadeCatalog::Snapshot::Index;

    ImGui::Spacing();
    SectionTitle("Effect catalogue");

    if (cat.index == Index::NotLoaded || cat.index == Index::IndexFailed)
    {
        TextWrappedDisabled("%s", Tr("Effects of ReShade's official package list, installed one by one: only the chosen effect and the files it needs are downloaded."));

        if (cat.index == Index::IndexFailed)
            TextWrappedColored(warn, "%s%s%s", Tr("The package list couldn't be downloaded"), cat.indexError.empty() ? "" : ": ", cat.indexError.c_str());

        if (IlionTheme::Button(TrL(cat.index == Index::IndexFailed ? "Retry##catalogue" : "Load catalogue")))
            IlionReShadeCatalog::LoadIndex();

        return;
    }

    if (cat.index == Index::Loading)
    {
        ImGui::TextDisabled("%s", Tr("Loading the package list..."));
        return;
    }

    // The operation running, and the effect waiting for ReShade
    if (cat.busy)
        ImGui::TextColored(warn, "%s %s", Tr(cat.removing ? "Removing" : "Downloading"), cat.busyWith.c_str());

    if (!reshadeWaitingFor.empty())
    {
        bool listed = false;
        for (const auto& t : rs.available)
            listed = listed || SameFile(t.file, reshadeWaitingFor);

        if (listed)
        {
            reshadeWaitingFor.clear();
        }
        else if (ImGui::GetTime() - reshadeWaitingSince < 20.0)
        {
            ImGui::TextDisabled("%s", Tr("ReShade is loading its effects again..."));
        }
        else
        {
            TextWrappedColored(warn, "%s %s", reshadeWaitingFor.c_str(),
                               Tr("is installed but ReShade hasn't loaded it: it may have failed to compile (see ReShade.log) or need the game restarted."));
            if (IlionTheme::Button(TrL("OK##catalogueWait")))
                reshadeWaitingFor.clear();
        }
    }

    // Search and package
    static char search[64] {};
    static int package = -1;
    if (package >= (int) cat.packages.size())
        package = -1;

    const float half = std::floor((ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f);
    ImGui::SetNextItemWidth(half);
    ImGui::InputTextWithHint("##catalogueSearch", Tr("Search effect"), search, sizeof(search));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(half);

    if (IlionTheme::BeginCombo("##cataloguePackage", package < 0 ? Tr("All packages") : cat.packages[package].name.c_str()))
    {
        if (ImGui::Selectable(Tr("All packages"), package < 0))
            package = -1;

        for (int i = 0; i < (int) cat.packages.size(); i++)
        {
            if (ImGui::Selectable(cat.packages[i].name.c_str(), package == i))
                package = i;
            if (ImGui::IsItemHovered() && !cat.packages[i].description.empty())
                WrappedTooltip("%s", cat.packages[i].description.c_str());
        }

        ImGui::EndCombo();
    }

    TextWrappedDisabled("%s", Tr("Every effect keeps its author's licence (see its package's repository). Installed effects appear under Added effects."));

    const std::string needle = CatalogueLower(search);
    bool any = false;

    ImGui::BeginChild("##catalogueList", ImVec2(0.0f, std::floor(ImGui::GetFontSize() * 15.0f)), ImGuiChildFlags_Borders);

    for (size_t p = 0; p < cat.packages.size(); p++)
    {
        if (package >= 0 && (size_t) package != p)
            continue;

        const auto& pkg = cat.packages[p];
        const bool packageMatches = needle.empty() || CatalogueLower(pkg.name).find(needle) != std::string::npos;
        std::vector<const std::string*> effects;
        for (const auto& e : pkg.effects)
        {
            if (packageMatches || CatalogueLower(e).find(needle) != std::string::npos)
                effects.push_back(&e);
        }

        if (effects.empty())
            continue;

        any = true;
        ImGui::PushID((int) p);

        // A search, or a single package picked, shows its effects without opening it
        if (!needle.empty() || package >= 0)
            ImGui::SetNextItemOpen(true, ImGuiCond_Always);

        const std::string title = pkg.name + "  (" + std::to_string(effects.size()) + ")";
        const bool open = ImGui::TreeNodeEx(title.c_str(), ImGuiTreeNodeFlags_SpanAvailWidth);

        if (ImGui::IsItemHovered())
            WrappedTooltip("%s%s%s", pkg.description.c_str(), pkg.repositoryUrl.empty() ? "" : "\n", pkg.repositoryUrl.c_str());

        if (open)
        {
            for (const auto* e : effects)
                RenderCatalogueEffect(p, *e, cat.busy, good, warn);

            ImGui::TreePop();
        }

        ImGui::PopID();
    }

    if (!any)
        ImGui::TextDisabled("%s", Tr("No effect matches the search."));

    ImGui::EndChild();
}

void MenuCommon::RenderReShadePanel(RenderMenuContext& ctx)
{
    IlionReShade::Poll();
    SyncReShadeEffects();
    const auto rs = IlionReShade::Get();
    const ImVec4 good = toneMapColor(IlionTheme::Themed(ImVec4(0.35f, 0.85f, 0.45f, 1.f), IlionTheme::Role::Good));
    const ImVec4 warn = toneMapColor(IlionTheme::Themed(ImVec4(1.f, 0.8f, 0.f, 1.f), IlionTheme::Role::Warn));

    SectionTitle("ReShade");

    // The close key against the right edge of the panel
    ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, ImGui::GetContentRegionAvail().x - ButtonWidth(TrL("Close##reshade"))));
    if (IlionTheme::Button(TrL("Close##reshade")))
        reshadePanelOpen = false;

    // Status and connection
    if (ImGui::BeginTable("##reshadeStatus", 2, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoSavedSettings))
    {
        ImGui::TableSetupColumn("##l");
        ImGui::TableSetupColumn("##v", ImGuiTableColumnFlags_WidthStretch);

        const auto row = [](const char* label, const ImVec4* colour, const std::string& value)
        {
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", Tr(label));
            ImGui::TableNextColumn();
            if (colour != nullptr)
                TextWrappedColored(*colour, "%s", value.c_str());
            else
                ImGui::TextWrapped("%s", value.c_str());
        };

        row("ReShade", rs.moduleFound ? &good : &warn,
            rs.moduleFound ? std::string(Tr("detected")) + (rs.version.empty() ? "" : " " + rs.version) : std::string(Tr("not detected")));
        row("Bridge", rs.registered ? &good : &warn,
            rs.registered ? std::string(Tr("connected")) : std::string(Tr(rs.error.empty() ? "disconnected" : rs.error.c_str())));
        row("Runtime", rs.runtime ? &good : &warn,
            rs.runtime ? std::string(Tr("presenting frames")) : std::string(Tr("waiting for ReShade to present a frame")));

        if (rs.moduleFound && !rs.path.empty())
            row("File", nullptr, rs.path);

        ImGui::EndTable();
    }

    const char* diagnosis = ReShadeDiagnosis(rs);

    if (!rs.moduleFound)
    {
        TextWrappedDisabled("%s", Tr(diagnosis));
        return;
    }

    if (diagnosis != nullptr)
        TextWrappedColored(warn, "%s", Tr(diagnosis));

    if (!rs.registered || !rs.runtime)
        return;

    // ReShade's processing, as ReShade reports it
    bool effectsOn = rs.effectsOn;
    if (IlionTheme::Checkbox(TrL("ReShade processing"), &effectsOn))
        IlionReShade::SetEffectsState(effectsOn);

    if (rs.pending > 0)
    {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", Tr("applying..."));
    }

    TextWrappedDisabled("%s", Tr("Each active effect costs GPU time; switched off, an effect costs nothing."));

    // Default catalogue, then the effects the user added
    const size_t catalogue = std::size(IlionReShade::kCatalogue);

    ImGui::Spacing();
    SectionTitle("Default effects");
    for (size_t i = 0; i < rs.effects.size() && i < catalogue; i++)
        RenderReShadeEffect(rs.effects[i], false);

    ImGui::Spacing();
    SectionTitle("Added effects");

    if (rs.effects.size() <= catalogue)
        TextWrappedDisabled("%s", Tr("None yet: pick an installed effect below."));

    for (size_t i = catalogue; i < rs.effects.size(); i++)
        RenderReShadeEffect(rs.effects[i], true);

    // Add effect: the techniques ReShade has loaded that aren't in the panel yet
    ImGui::Spacing();
    SectionTitle("Add effect");

    std::vector<IlionReShade::Technique> choices;
    for (const auto& t : rs.available)
    {
        bool shown = false;
        for (const auto& e : rs.effects)
        {
            if (e.ref.file == t.file && (e.ref.technique.empty() ? e.technique == t.name : e.ref.technique == t.name))
                shown = true;
        }

        if (!shown)
            choices.push_back(t);
    }

    static std::string picked;
    std::string preview = picked.empty() ? std::string(Tr("Choose an installed effect")) : picked;

    ImGui::SetNextItemWidth(std::min(ImGui::GetFontSize() * 16.0f, ImGui::GetContentRegionAvail().x - ButtonWidth(TrL("Add##reshade")) - 8.0f));

    if (IlionTheme::BeginCombo("##reshadeAdd", preview.c_str()))
    {
        for (const auto& t : choices)
        {
            const std::string label = t.name + "  (" + t.file + ")";
            if (ImGui::Selectable(label.c_str(), label == picked))
                picked = label;
        }

        ImGui::EndCombo();
    }

    ImGui::SameLine();
    ImGui::BeginDisabled(picked.empty());

    if (IlionTheme::Button(TrL("Add##reshade")))
    {
        for (const auto& t : choices)
        {
            if (t.name + "  (" + t.file + ")" == picked)
            {
                auto list = ReShadeAdded();
                list.push_back({ t.file, t.name });
                SaveReShadeAdded(list);
                break;
            }
        }

        picked.clear();
    }

    ImGui::EndDisabled();

    if (choices.empty())
        TextWrappedDisabled("%s", Tr("Every effect ReShade has loaded is already in the panel."));

    RenderReShadeCatalogue(rs, good, warn);
}

// CUSTOM TAB -----------------------------
struct MenuCommon::MenuBox
{
    const char* id;   // Saved to the config
    const char* tab;  // Label of the tab it belongs to
    const char* name; // Shown in the Custom tab picker
    void (*draw)(RenderMenuContext&);
    // Function that draws several boxes, ScopedCard::Shows(id) selects the one to draw
    void (*source)(RenderMenuContext&) = nullptr;
    bool pinnable = true;
};

// Every box of every tab in the order they are drawn
const MenuCommon::MenuBox* MenuCommon::GetMenuBoxes(size_t& count)
{
    // clang-format off
    static const MenuBox boxes[] = {
        { "upscaler", "Upscaling", "Upscalers",
          [](RenderMenuContext& c) { if (IsUpscalerActive(c.currentFeature)) RenderCard(c, RenderActiveUpscalerSettings); } },
        { "upscale_ratio", "Upscaling", "Upscale Ratio Override",
          [](RenderMenuContext& c) { if (IsUpscalerActive(c.currentFeature)) RenderFoldedCard(c, "Advanced: Upscale Ratio Override", "Upscale Ratio Override", RenderUpscaleRatioSettings); } },
        { "output_scaling", "Upscaling", "Output Scaling",
          [](RenderMenuContext& c) { if (IsUpscalerActive(c.currentFeature)) RenderFoldedCard(c, "Advanced: Output Scaling (FSR1)", "Output Scaling", RenderOutputScalingSettings); } },
        { "shader_times", "Upscaling", "Per shader GPU times",
          [](RenderMenuContext& c) { if (IsUpscalerActive(c.currentFeature) && !c.state.detailedGpuTimes.empty()) RenderFoldedCard(c, "Diagnostics: Per shader GPU times", "Per shader GPU times", RenderShaderTimes); } },

        { "gfx_color", "Graphics", "Colour and image", [](RenderMenuContext& c) { RenderCard(c, RenderGraphicsColorSettings); } },
        { "sharpness", "Graphics", "Sharpness",
          [](RenderMenuContext& c) { if (IsUpscalerActive(c.currentFeature)) RenderCard(c, RenderActiveImageSettings); } },
        { "mipmap", "Graphics", "Mipmap Bias", nullptr, RenderApiAndTextureSettings },
        { "anisotropy", "Graphics", "Anisotropic Filtering", nullptr, RenderApiAndTextureSettings },
        { "gfx_compare", "Graphics", "Comparison", [](RenderMenuContext& c) { RenderCard(c, RenderGraphicsCompareSettings); } },
        { "magnifier", "Graphics", "Magnifier",
          [](RenderMenuContext& c) { if (IsUpscalerActive(c.currentFeature)) RenderCard(c, RenderMagnifierSettings); } },
        { "gfx_advanced", "Graphics", "Advanced adjustments", [](RenderMenuContext& c) { RenderCard(c, RenderGraphicsAdvancedSettings); } },
        { "reshade", "Graphics", "ReShade", [](RenderMenuContext& c) { RenderCard(c, RenderReShadeCard); } },

        { "fg_selection", "Frame Gen", "Frame Generation", [](RenderMenuContext& c) { RenderCard(c, RenderFrameGenerationSelection); } },
        { "fg_fsrfg", "Frame Gen", "FSR FG Settings", nullptr, RenderFrameGenerationRuntimeSettings },
        { "fg_xefg", "Frame Gen", "XeFG Settings", nullptr, RenderFrameGenerationRuntimeSettings },
        { "fg_dlssg", "Frame Gen", "DLSSG Settings", nullptr, RenderFrameGenerationRuntimeSettings },
        { "fg_reprojection", "Frame Gen", "Reprojection Settings", nullptr, RenderFrameGenerationRuntimeSettings },
        { "fg_optifg", "Frame Gen", "OptiFG Settings", nullptr, RenderFrameGenerationRuntimeSettings },
        { "fg_nvngx", "Frame Gen", "Nvngx FG replacement", nullptr, RenderFrameGenerationRuntimeSettings },
        { "fg_fsrfg_inputs", "Frame Gen", "FSR-FG Inputs", nullptr, RenderFrameGenerationRuntimeSettings },
        { "fg_sl_inputs", "Frame Gen", "Streamline FG Inputs", nullptr, RenderFrameGenerationRuntimeSettings },

        { "framerate", "Latency & FPS", "Framerate", [](RenderMenuContext& c) { RenderCard(c, RenderFramerateSettings); } },
#ifdef LOW_LATENCY_INPUTS
        { "low_latency", "Latency & FPS", "Low Latency", [](RenderMenuContext& c) { RenderCard(c, RenderLowLatencySettings); } },
#else
        { "ilion_latency", "Latency & FPS", "Intelligent Activation", [](RenderMenuContext& c) { RenderCard(c, RenderIntelligentLatency); } },
        { "low_latency", "Latency & FPS", "fakenvapi",
          [](RenderMenuContext& c)
          {
              ImGui::BeginDisabled(IlionLatency::Current() != IlionLatency::Manual);
              RenderCard(c, RenderFakenvapiSettings);
              ImGui::EndDisabled();
          } },
#endif
        { "vsync", "Latency & FPS", "V-Sync", [](RenderMenuContext& c) { RenderCard(c, RenderVsyncSettings); } },

        { "fps_overlay", "Overlay", "FPS Overlay", [](RenderMenuContext& c) { RenderCard(c, RenderFpsOverlaySettings); } },

        { "benchmark", "Benchmark", "Benchmark", [](RenderMenuContext& c) { RenderCard(c, RenderBenchmark); } },


        { "init_flags", "Compatibility", "Init Flags", [](RenderMenuContext& c) { RenderCard(c, RenderInitFlagsSettings); } },
        { "fsr_common", "Compatibility", "FSR Common Settings", [](RenderMenuContext& c) { RenderCard(c, RenderFsrCommonSettings); } },
        { "upscaler_inputs", "Compatibility", "Upscaler Inputs", [](RenderMenuContext& c) { RenderCard(c, RenderUpscalerInputsSettings); } },
        { "spoofing", "Compatibility", "Spoofing / Hooking", nullptr, RenderAdvancedSettings },
        { "plugins", "Compatibility", "Plugins / Startup", nullptr, RenderAdvancedSettings },
        { "shaders_limits", "Compatibility", "Shaders / Limits", nullptr, RenderAdvancedSettings },
        { "barriers", "Compatibility", "Resource Barriers", nullptr, RenderAdvancedSettings },
        { "root_signatures", "Compatibility", "Root Signatures", nullptr, RenderAdvancedSettings },

        { "menu_scale", "Menu", "Menu Scale", [](RenderMenuContext& c) { RenderCard(c, RenderMenuScaleSettings); } },
        { "theme", "Menu", "Theme and Color", [](RenderMenuContext& c) { RenderCard(c, RenderThemeSettings); } },
        { "keybinds", "Menu", "Keybinds", [](RenderMenuContext& c) { RenderCard(c, RenderKeybindSettings); } },
        { "custom_tab", "Menu", "Custom Tab", [](RenderMenuContext& c) { RenderCard(c, RenderCustomTabSettings); }, nullptr, false },

        { "status_summary", "Help / Status", "Summary", [](RenderMenuContext& c) { RenderCard(c, RenderStatusSummary); } },
        { "status_system", "Help / Status", "System", nullptr, RenderStatusInfo },
        { "status_detected", "Help / Status", "Detected", nullptr, RenderStatusInfo },
        { "status_hooks", "Help / Status", "Hooks / Config", nullptr, RenderStatusInfo },
        { "status_init", "Help / Status", "Init Flags (active)", nullptr, RenderStatusInfo },
        { "logging", "Help / Status", "Logging", [](RenderMenuContext& c) { RenderCard(c, RenderLoggingSettings); } },
    };
    // clang-format on

    count = std::size(boxes);
    return boxes;
}

// Draws a single box, even when it's one of many drawn by the same function
void MenuCommon::RenderMenuBox(RenderMenuContext& ctx, const MenuBox& box)
{
    if (box.draw != nullptr)
    {
        box.draw(ctx);
        return;
    }

    ScopedCard::SetFilter(box.id);
    box.source(ctx);
    ScopedCard::SetFilter(nullptr);
}

// Ids are kept in a comma separated list
static bool IsBoxPinned(const std::string& pinned, const char* id)
{
    const size_t length = strlen(id);

    for (size_t pos = pinned.find(id); pos != std::string::npos; pos = pinned.find(id, pos + 1))
    {
        const bool startOk = pos == 0 || pinned[pos - 1] == ',';
        const bool endOk = pos + length == pinned.size() || pinned[pos + length] == ',';

        if (startOk && endOk)
            return true;
    }

    return false;
}

void MenuCommon::RenderCustomTab(RenderMenuContext& ctx)
{
    auto config = ctx.config;

    const std::string pinned = config->CustomTabCards.value_or_default();

    size_t count = 0;
    const MenuBox* boxes = GetMenuBoxes(count);

    bool anyPinned = false;

    // ILION: the key of the Customise panel, at the right above the boxes
    {
        const char* key = personalPanelOpen ? "Close customisation" : "Customise";
        const float width = ButtonWidth(TrL(key));
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImMax(0.0f, ImGui::GetContentRegionAvail().x - width));
        if (IlionTheme::Button(TrL(key)))
            personalPanelOpen = !personalPanelOpen;
        ShowTooltip("Choose the boxes of this tab; the changes show at once");
    }

    const float startY = ImGui::GetCursorPosY();

    for (size_t i = 0; i < count; i++)
    {
        if (!boxes[i].pinnable || !IsBoxPinned(pinned, boxes[i].id))
            continue;

        anyPinned = true;

        ImGui::PushID(boxes[i].id);

        // Explains why the upscaler settings are missing, the Upscaling tab draws it above its boxes
        if (strcmp(boxes[i].id, "upscaler") == 0)
        {
            auto feature = ctx.currentFeature;
            if (feature == nullptr || !feature->IsInited() || feature->IsFrozen())
                RenderCard(ctx, RenderUpscalerStateMessage);
        }

        RenderMenuBox(ctx, boxes[i]);
        ImGui::PopID();
    }

    // Boxes only show up when their feature is in use
    if (!anyPinned)
        TextWrappedDisabled("%s", Tr("Nothing picked yet: press Customise to choose the boxes"));
    else if (ImGui::GetCursorPosY() == startY)
        TextWrappedDisabled("%s", Tr("None of the picked boxes are available right now"));
}

void MenuCommon::RenderCustomTabSettings(RenderMenuContext& ctx)
{
    auto config = ctx.config;

    SectionTitle("Custom Tab");

    // Only the switch stays here, so a hidden Personal tab can always come back; its boxes are chosen in it
    ConfigCheckbox("Show Personal tab", config->CustomTabEnabled, "The boxes it shows are chosen in Personal > Customise");
}

// ILION: Customise panel of Personal, at the right of its boxes. The same list of boxes the Menu tab had, the same
// setting (CustomTabCards); every change shows at once on the left.
void MenuCommon::RenderPersonalPanel(RenderMenuContext& ctx)
{
    auto config = ctx.config;

    SectionTitle("Customise");

    if (IlionTheme::Button(TrL("Close##personal")))
        personalPanelOpen = false;
    SameLineIfRoom(ButtonWidth(TrL("Restore default layout")));
    if (IlionTheme::Button(TrL("Restore default layout")))
        config->CustomTabCards.reset();
    ShowTooltip("Back to the boxes Personal shows by default");

    ImGui::Spacing();

    // Its own scroll: the panel never grows past the window
    ImGui::BeginChild("##PersonalBoxes", ImVec2(0.0f, 0.0f));

    const std::string pinned = config->CustomTabCards.value_or_default();
    std::string newPinned;
    bool changed = false;

    size_t count = 0;
    const MenuBox* boxes = GetMenuBoxes(count);
    const char* lastTab = nullptr;

    for (size_t i = 0; i < count; i++)
    {
        const MenuBox& box = boxes[i];

        if (!box.pinnable)
            continue;

        if (lastTab == nullptr || strcmp(lastTab, box.tab) != 0)
        {
            if (lastTab != nullptr)
                ImGui::Spacing();

            ImGui::TextDisabled("%s", Tr(box.tab));
            lastTab = box.tab;
        }

        bool on = IsBoxPinned(pinned, box.id);

        ImGui::PushID(box.id);
        changed |= IlionTheme::Checkbox("##on", &on);
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextWrapped("%s", Tr(box.name));
        if (ImGui::IsItemClicked())
        {
            on = !on;
            changed = true;
        }
        ImGui::PopID();

        if (on)
        {
            if (!newPinned.empty())
                newPinned += ',';

            newPinned += box.id;
        }
    }

    ImGui::EndChild();

    if (changed)
        config->CustomTabCards = newPinned;
}

// ILION: OptiFG's HUDFix controls, drawn in the Anti-Ghosting tab only. The same controls, settings and conditions
// they had in the OptiFG card (OptiFG input, a ready FG output, HUDless tracking available). False: they don't apply.
bool MenuCommon::RenderOptiFgHudFix(RenderMenuContext& ctx)
{
    auto& state = ctx.state;
    auto config = ctx.config;
    auto& currentFeature = ctx.currentFeature;
    auto& menuResScale = ctx.menuResScale;

    if (state.api == API::Vulkan || state.currentFGSwapchain == nullptr || state.activeFgInput != FGInput::Upscaler)
        return false;

    if (currentFeature == nullptr || currentFeature->IsFrozen() ||
        !((state.activeFgOutput == FGOutput::FSRFG && FfxApiProxy::IsFGReady()) ||
          (state.activeFgOutput == FGOutput::XeFG && XeFGProxy::Module() != nullptr) ||
          (state.activeFgOutput == FGOutput::DLSSG && StreamlineProxy::Module() != nullptr) ||
          state.activeFgOutput == FGOutput::Reprojection))
        return false;

    const bool dx11HudfixTracking = state.swapchainInteropApi == SwapchainInteropApi::Dx11wDx12;
    const bool hudfixTrackingSupported = !Config::Instance()->FGDisableHUDFix.value_or_default() &&
                                         (state.swapchainInteropApi == SwapchainInteropApi::None || dx11HudfixTracking);

    if (!hudfixTrackingSupported)
        return false;

    ScopedCard card { "HUDFix" };

    {
        bool fgHudfix = config->FGHUDFix.value_or_default();

        if (IlionTheme::Checkbox(TrL("HUDFix"), &fgHudfix))
        {
            config->FGHUDFix = fgHudfix;
            LOG_DEBUG("Enabled set FGHUDFix: {}", fgHudfix);
            state.clearCapturedHudlesses = true;
            state.fgChanged = true;
        }

        ShowTooltip("Enable HUD stability fix, might cause crashes!");

        ImGui::BeginDisabled(!config->FGHUDFix.value_or_default());

        ImGui::SameLine(0.0f, 16.0f);
        ImGui::PushItemWidth(95.0f * menuResScale);
        int hudFixLimit = config->FGHUDLimit.value_or_default();
        if (ImGui::InputInt(TrL("Limit"), &hudFixLimit))
        {
            if (hudFixLimit < 1)
                hudFixLimit = 1;
            else if (hudFixLimit > 999)
                hudFixLimit = 999;

            config->FGHUDLimit = hudFixLimit;
            LOG_DEBUG("Enabled set FGHUDLimit: {}", hudFixLimit);
        }
        ShowTooltip("Delay HUDless capture, high values might cause crash!");

        SameLineIfRoom(ButtonWidth(TrL("Res##2")), 16.0f);
        if (IlionTheme::Button(TrL("Res##2")))
            _showHudlessWindow = !_showHudlessWindow;

        ImGui::EndDisabled();

        auto hudExtended = config->FGHUDFixExtended.value_or_default();
        if (IlionTheme::Checkbox(TrL("Extended"), &hudExtended))
        {
            LOG_DEBUG("Enabled set FGHUDFixExtended: {}", hudExtended);
            config->FGHUDFixExtended = hudExtended;
        }
        ShowTooltip("Extended format checks for possible HUDless\nMight cause crashes and slowdowns!");
        SameLineIfRoom(CheckboxWidth(TrL("Immediate Capture")), 16.0f);

        ImGui::BeginDisabled(!config->FGHUDFix.value_or_default());

        auto immediate = config->FGImmediateCapture.value_or_default();
        if (IlionTheme::Checkbox(TrL("Immediate Capture"), &immediate))
        {
            LOG_DEBUG("Enabled set FGImmediateCapture: {}", immediate);
            config->FGImmediateCapture = immediate;
        }
        ShowTooltip("Enables capturing of resources before shader execution.\nIncrease HUDless "
                    "capture chances, but might cause capturing of unnecessary resources.");

        ImGui::PopItemWidth();

        ImGui::EndDisabled();
    }

    return true;
}

// ILION: the Dispatch Flags of DLSS Enabler's frame generation (Arturs or Combo replacement), drawn in the
// Anti-Ghosting tab only. The same controls and setting they had in the Nvngx FG card. False: they don't apply.
bool MenuCommon::RenderDispatchFlags(RenderMenuContext& ctx)
{
    auto& state = ctx.state;
    auto config = ctx.config;
    auto& menuResScale = ctx.menuResScale;

    const FGNvngxReplacement activeNvngxFg = state.activeFgNvngx;
    if (activeNvngxFg != FGNvngxReplacement::Arturs && activeNvngxFg != FGNvngxReplacement::Combo)
        return false;

    bool isLoaded = false;
    if (state.swapchainApi == Vulkan)
        isLoaded = Nvngx_FG::isVulkanAvailable();
    if (state.swapchainApi == DX12)
        isLoaded = Nvngx_FG::isDx12Available();

    if (!isLoaded)
        return false;

    ScopedCard card { "Dispatch Flags" };

    {
        auto featureVer = Nvngx_FG::version();
        auto antighostingVer = Nvngx_FG::extraVersion();
        ImGui::Text(Tr("DE Ver: %d.%d.%d.%d   GB Ver: %d.%d"), featureVer.major, featureVer.minor, featureVer.patch,
                    featureVer.reserved, antighostingVer.major, antighostingVer.minor);

        static std::vector<FlagDefinition> common_flags = {
            { "Antighosting (GB)", 0x00100000, "Enable anti-ghosting correction" },
            { "Temporal HUD pin", 0x04000000, "Enable temporal HUD pinning (present-backbuffer stability)" }
        };

        static std::vector<FlagDefinition> uncommon_flags = {
            //{ "Hudless UI mask", 0x02000000, "Use HUD-less as UI mask (DL2 inverted semantics)" },
            { "HUD interpolation", 0x08000000, "HUD OF interpolation (0=legacy pin-present, 1=OF warp)" },
            { "Ignore UI texture", 0x10000000, "Ignore dedicated DLSSG.UI texture (force legacy HUD path)" },
            //{ "Dp4a active", 0x20000000, "OF pipeline using dp4a-accelerated SSD (SM 6.4+)" },
            { "Pin backbuffer", 0x40000000, "Pin DLSSG.Backbuffer to subframe-1 snapshot across MFG frame" }
        };

        static std::vector<FlagDefinition> debug_flags = {
            { "Antighosting red tint", 0x00200000, "Debug: red tint on corrected pixels" },
            { "Antighosting split screen", 0x00400000, "Debug: split screen comparison" },
            { "Frame index line", 0x00010000, "" },
            { "HUD detection", 0x00020000, "" },
            { "Disocclusion tint", 0x00040000, "" },
            { "Artifacts detection", 0x00080000, "" },
            { "Camera MV debug", 0x00800000, "Debug: blue tint where camera MV fallback is used" },
            { "Generic visualization", 0x01000000, "Debug: trapezoid zone visualization" }
        };

        uint32_t temp_flags = config->NvngxFGDispatchFlags.value_or_default();
        bool changed = false;

        ImGui::Text(Tr("Raw DispatchFlags:"));
        changed |= ImGui::InputScalar("##RawFlags", ImGuiDataType_U32, &temp_flags, NULL, NULL, "%08X",
                                      ImGuiInputTextFlags_CharsHexadecimal);

        SameLineIfRoom(CheckboxWidth(TrL("Show Debug")), 20.0f * menuResScale);
        if (bool showDebug = config->NvngxFGShowDebug.value_or_default();
            IlionTheme::Checkbox(TrL("Show Debug"), &showDebug))
        {
            config->NvngxFGShowDebug = showDebug;
        }
        ShowTooltip("Required for Debug flags to work correctly");

        ImGui::Spacing();

        if (auto ch = ScopedCollapsingHeader(TrL("Active DispatchFlags")); ch.IsHeaderOpen())
        {
            ScopedIndent indent {};

            auto render_flags = [&](const std::vector<FlagDefinition>& flags)
            {
                for (const auto& flag : flags)
                {
                    changed |= ImGui::CheckboxFlags(TrL(flag.name.c_str()), &temp_flags, flag.mask);

                    if (ImGui::IsItemHovered() && !flag.description.empty())
                    {
                        WrappedTooltip("%s", Tr(flag.description.c_str()));
                    }
                }
            };

            ImGui::TextDisabled(Tr("Common"));
            render_flags(common_flags);

            ImGui::Spacing();
            ImGui::TextDisabled(Tr("Uncommon"));
            render_flags(uncommon_flags);

            if (config->NvngxFGShowDebug.value_or_default())
            {
                ImGui::Spacing();
                ImGui::TextDisabled(Tr("Debug"));
                render_flags(debug_flags);
            }
        }

        if (changed)
        {
            config->NvngxFGDispatchFlags = temp_flags;
        }
    }

    return true;
}

// ILION Essential mode: which boxes it shows. Main: in the tab as always. Secondary: folded under "More options".
// Anything else (diagnostics, comparison and debug tools, the Custom tab settings) stays in Advanced only.
enum class EssentialRole
{
    Main,
    Secondary,
    AdvancedOnly,
};

static EssentialRole EssentialRoleOf(const char* id)
{
    static constexpr const char* main[] = { "upscaler",  "gfx_color",   "sharpness", "fg_selection", "fg_fsrfg",
                                            "fg_xefg",   "fg_dlssg",    "fg_optifg", "fg_reprojection", "fg_nvngx",
                                            "framerate", "ilion_latency", "vsync",   "fps_overlay",  "menu_scale",
                                            "theme", "reshade" };
    static constexpr const char* secondary[] = { "mipmap", "anisotropy", "gfx_advanced", "low_latency", "keybinds" };

    for (const char* name : main)
    {
        if (strcmp(name, id) == 0)
            return EssentialRole::Main;
    }

    for (const char* name : secondary)
    {
        if (strcmp(name, id) == 0)
            return EssentialRole::Secondary;
    }

    return EssentialRole::AdvancedOnly;
}

static bool EssentialHidesTab(const char* label)
{
    return strcmp(label, "Custom") == 0 || strcmp(label, "Compatibility") == 0 || strcmp(label, "Quirks") == 0 ||
           strcmp(label, "Help / Status") == 0 || strcmp(label, "Benchmark") == 0;
}

// ILION: the options that act on ghosting and trails, from the boxes they already live in. Nothing is copied: each
// box is drawn by its own code, so a change here is the same change in its own tab. Each group says what it works with.
void MenuCommon::RenderAntiGhostingTab(RenderMenuContext& ctx)
{
    size_t count = 0;
    const MenuBox* boxes = GetMenuBoxes(count);

    // Draws a box by its id; false when it drew nothing (its feature isn't in use)
    const auto drawBox = [&](const char* id)
    {
        for (size_t i = 0; i < count; i++)
        {
            if (strcmp(boxes[i].id, id) != 0)
                continue;

            const float before = ImGui::GetCursorPosY();
            ImGui::PushID(id);
            RenderMenuBox(ctx, boxes[i]);
            ImGui::PopID();
            return ImGui::GetCursorPosY() != before;
        }

        return false;
    };

    TextWrappedDisabled("%s", Tr("Each group works only with the technology it names. These are the same controls as in "
                                 "their own tabs."));

    // 1. HUD with frame generation
    SectionTitle("HUD with Frame Generation");
    TextWrappedDisabled("%s", Tr("HUDFix: only with OptiFG as the frame generation input, with FSR FG, XeFG, DLSSG or "
                                 "Reprojection as output. It may cause crashes."));
    if (!RenderOptiFgHudFix(ctx))
        TextWrappedDisabled("%s", Tr("Shown while OptiFG is the frame generation input."));

    // 2. DLSS Enabler's frame generation
    SectionTitle("DLSS Enabler Frame Generation");
    TextWrappedDisabled("%s", Tr("Dispatch Flags, Antighosting (GB) among them: only with DLSS Enabler's DLSSG "
                                 "replacement. They do nothing with FSR FG, XeFG or the game's own DLSSG."));
    if (!RenderDispatchFlags(ctx))
        TextWrappedDisabled("%s", Tr("Shown while DLSS Enabler's frame generation is in use."));

    // 3. The upscaler
    SectionTitle("Upscaler");
    TextWrappedDisabled("%s", Tr("Trails behind moving objects usually come from the upscaler: DLSS render presets "
                                 "and the FSR settings are in Upscaling, the reactive mask in Init Flags."));
    drawBox("init_flags");

    if (IlionTheme::Button(TrL("Open Upscaling##AntiGhosting")))
        requestedMenuTab = "Upscaling";
}

// ILION: External Frame Gen. Settings and the search for MFG SUPER MOTION, started the first time the menu is drawn
// (off the render thread) so the engine is already found when the tab is opened.
struct ExternalFgShared
{
    IlionExternalFg::Settings settings;
    IlionExternalFg::Finder finder;
    std::filesystem::path iniPath;
};

static ExternalFgShared& ExternalFg()
{
    static ExternalFgShared shared;
    static std::once_flag loaded;
    std::call_once(loaded,
                   []
                   {
                       shared.iniPath = Util::DllPath().parent_path() / L"ILION" / L"ExternalFG.ini";
                       IlionExternalFg::Load(shared.settings, shared.iniPath);
                   });

    shared.finder.Start(shared.settings.folder);
    return shared;
}

// What the external FG screen shows: only what MFG SUPER MOTION's engine says about itself (its "@" lines), never
// OptiScaler's frame generation or the overlay's counters
struct ExtFgCrtData
{
    int state = 0;            // 0 off, 1 starting, 2 active, 3 error
    std::string generator;    // the generator the engine was started with
    std::string multiplier = "-";
    std::string capture = "-";
    double baseFps = -1.0;    // real frames of the game a second
    double outputFps = -1.0;  // every frame shown a second
};

static std::string ExtFgGeneratorName(int generator, int msmSearch)
{
    switch (generator)
    {
    case 0:
        return "DLSS MFG";
    case 1:
        return "XeSS MFG";
    default:
        return msmSearch == 1 ? "SMFG FAST" : "SMFG 1.0";
    }
}

// The screen: a dark bezel with four screws and a blue-green glass, figures in the CRT font. Same family as the FG
// screen, its own tint, so it can't be taken for OptiScaler's frame generation.
static void DrawExtFgCrt(const ImVec2& min, const ImVec2& size, const ExtFgCrtData& d)
{
    ImDrawList* draw = ImGui::GetWindowDrawList();
    ImFont* font = IlionTheme::crtFont != nullptr ? IlionTheme::crtFont : ImGui::GetFont();
    ImFont* bigFont = IlionTheme::crtTitleFont != nullptr ? IlionTheme::crtTitleFont : font;
    const float fs = ImGui::GetFontSize();
    const ImVec2 max(min.x + size.x, min.y + size.y);
    const auto col = [](float r, float g, float b, float a = 1.0f) { return ImGui::GetColorU32(ImVec4(r, g, b, a)); };

    // Bezel, screws, recess
    const float bezel = std::floor(fs * 0.55f);
    const float rounding = std::floor(fs * 0.45f);
    draw->AddRectFilled(ImVec2(min.x + 2.0f, min.y + 3.0f), ImVec2(max.x + 2.0f, max.y + 3.0f), col(0, 0, 0, 0.45f), rounding);
    draw->AddRectFilledMultiColor(min, max, col(0.36f, 0.38f, 0.40f), col(0.36f, 0.38f, 0.40f), col(0.15f, 0.16f, 0.18f),
                                  col(0.15f, 0.16f, 0.18f));
    draw->AddRect(min, max, col(0.04f, 0.04f, 0.05f), rounding, 0, 1.5f);
    draw->AddLine(ImVec2(min.x + rounding, min.y + 1.0f), ImVec2(max.x - rounding, min.y + 1.0f), col(0.75f, 0.78f, 0.80f, 0.5f));

    const float screw = std::max(2.0f, std::floor(fs * 0.17f));
    for (const ImVec2 at : { ImVec2(min.x + bezel * 0.5f, min.y + bezel * 0.5f), ImVec2(max.x - bezel * 0.5f, min.y + bezel * 0.5f),
                             ImVec2(min.x + bezel * 0.5f, max.y - bezel * 0.5f), ImVec2(max.x - bezel * 0.5f, max.y - bezel * 0.5f) })
    {
        draw->AddCircleFilled(at, screw, col(0.52f, 0.54f, 0.56f));
        draw->AddCircle(at, screw, col(0.07f, 0.07f, 0.08f), 0, 1.0f);
        draw->AddLine(ImVec2(at.x - screw * 0.6f, at.y + screw * 0.6f), ImVec2(at.x + screw * 0.6f, at.y - screw * 0.6f),
                      col(0.14f, 0.14f, 0.15f), 1.0f);
    }

    const ImVec2 rMin(min.x + bezel, min.y + bezel), rMax(max.x - bezel, max.y - bezel);
    draw->AddRectFilled(rMin, rMax, col(0, 0, 0), rounding * 1.6f);
    const ImVec2 gMin(rMin.x + 2.0f, rMin.y + 2.0f), gMax(rMax.x - 2.0f, rMax.y - 2.0f);
    const float glassRound = rounding * 1.5f, gw = gMax.x - gMin.x, gh = gMax.y - gMin.y;
    draw->AddRectFilled(gMin, gMax, col(0.012f, 0.050f, 0.062f), glassRound);

    draw->PushClipRect(ImVec2(gMin.x + 1.0f, gMin.y + 1.0f), ImVec2(gMax.x - 1.0f, gMax.y - 1.0f), true);
    const ImU32 clear = col(0, 0, 0, 0), edge = col(0, 0, 0, 0.5f);
    draw->AddRectFilledMultiColor(gMin, ImVec2(gMin.x + gw * 0.2f, gMax.y), edge, clear, clear, edge);
    draw->AddRectFilledMultiColor(ImVec2(gMax.x - gw * 0.2f, gMin.y), gMax, clear, edge, edge, clear);
    draw->AddRectFilledMultiColor(gMin, ImVec2(gMax.x, gMin.y + gh * 0.16f), edge, edge, clear, clear);
    for (float y = gMin.y + 2.0f; y < gMax.y; y += 3.0f)
        draw->AddLine(ImVec2(gMin.x, y), ImVec2(gMax.x, y), col(0, 0, 0, 0.14f));
    draw->AddRectFilledMultiColor(ImVec2(gMin.x + gw * 0.06f, gMin.y + gh * 0.04f), ImVec2(gMax.x - gw * 0.06f, gMin.y + gh * 0.30f),
                                  col(0.8f, 0.95f, 1.0f, 0.035f), col(0.8f, 0.95f, 1.0f, 0.035f), clear, clear);

    // Text: cyan labels, white-blue values, ACTIVE in phosphor green
    const ImVec4 label(0.42f, 0.86f, 0.95f, 1.0f), value(0.88f, 0.97f, 1.0f, 1.0f), dim(0.40f, 0.52f, 0.56f, 1.0f);
    const ImVec4 phosphor(0.30f, 1.0f, 0.42f, 1.0f), warn(1.0f, 0.55f, 0.25f, 1.0f);
    const auto text = [&](ImFont* f, float px, const ImVec2& at, const ImVec4& c, const char* s)
    {
        draw->AddText(f, px, ImVec2(at.x + 1.0f, at.y + 1.0f), col(0, 0, 0, 0.55f), s);
        draw->AddText(f, px, ImVec2(at.x, at.y + 1.0f), col(c.x, c.y, c.z, 0.14f), s);
        draw->AddText(f, px, at, ImGui::GetColorU32(c), s);
    };
    const auto width = [&](ImFont* f, float px, const char* s) { return f->CalcTextSizeA(px, FLT_MAX, 0.0f, s).x; };

    const float pad = std::floor(fs * 0.6f);
    const float x = gMin.x + pad, right = gMax.x - pad;
    float y = gMin.y + std::floor(pad * 0.7f);

    // Title: the program and its state
    const char* stateText = d.state == 2   ? FgCrtWord("ACTIVE", "ACTIVO")
                            : d.state == 1 ? FgCrtWord("STARTING", "INICIANDO")
                            : d.state == 3 ? "ERROR"
                                           : FgCrtWord("OFF", "APAGADO");
    const ImVec4 stateColour = d.state == 2 ? phosphor : d.state == 3 ? warn : d.state == 1 ? label : dim;
    const float titlePx = std::floor(fs * 1.1f);
    text(bigFont, titlePx, ImVec2(x, y), label, "SUPER MOTION");
    text(bigFont, titlePx, ImVec2(right - width(bigFont, titlePx, stateText), y), stateColour, stateText);
    y += titlePx + std::floor(fs * 0.25f);
    draw->AddLine(ImVec2(x, y), ImVec2(right, y), col(label.x, label.y, label.z, 0.45f), 1.0f);
    y += std::floor(fs * 0.35f);

    // Generator and multiplier on one line, capture under it
    const bool on = d.state == 2;
    text(font, fs, ImVec2(x, y), label, "GEN");
    text(font, fs, ImVec2(x + width(font, fs, "GEN "), y), on ? value : dim, d.generator.c_str());
    const std::string mult = d.multiplier == "-" ? std::string("-") : "x" + d.multiplier;
    const float multX = right - width(font, fs, mult.c_str());
    text(font, fs, ImVec2(multX, y), on ? value : dim, mult.c_str());
    text(font, fs, ImVec2(multX - width(font, fs, "MULT "), y), label, "MULT");
    y += std::floor(fs * 1.5f);

    // Three figures: base, generated, shown
    const float bigPx = std::floor(fs * 1.9f);
    const float column = (right - x) / 3.0f;
    const auto figure = [&](int i, const char* caption, double fps)
    {
        const std::string number = fps >= 0.0 ? StrFmt("%.0f", fps) : std::string("-");
        const float cx = x + column * (i + 0.5f);
        text(font, fs * 0.85f, ImVec2(std::floor(cx - width(font, fs * 0.85f, caption) * 0.5f), y), label, caption);
        text(bigFont, bigPx, ImVec2(std::floor(cx - width(bigFont, bigPx, number.c_str()) * 0.5f), y + std::floor(fs * 1.05f)),
             fps >= 0.0 ? (i == 1 ? phosphor : value) : dim, number.c_str());
    };

    const double generated = on && d.baseFps >= 0.0 && d.outputFps >= d.baseFps ? d.outputFps - d.baseFps : -1.0;
    figure(0, FgCrtWord("BASE", "BASE"), on ? d.baseFps : -1.0);
    figure(1, FgCrtWord("GENERATED", "GENERADOS"), generated);
    figure(2, FgCrtWord("SHOWN", "MOSTRADOS"), on ? d.outputFps : -1.0);
    y += std::floor(fs * 1.05f) + bigPx + std::floor(fs * 0.2f);

    draw->AddLine(ImVec2(x, y), ImVec2(right, y), col(label.x, label.y, label.z, 0.30f), 1.0f);
    y += std::floor(fs * 0.25f);
    const std::string captureLine = std::string(FgCrtWord("CAPTURE ", "CAPTURA ")) + (on ? d.capture : std::string("-"));
    text(font, fs * 0.85f, ImVec2(x, y), dim, captureLine.c_str());
    const char* fpsWord = "FPS";
    text(font, fs * 0.85f, ImVec2(right - width(font, fs * 0.85f, fpsWord), y), dim, fpsWord);

    draw->PopClipRect();
    draw->AddRect(gMin, gMax, col(0.30f, 0.55f, 0.62f, 0.30f), glassRound, 0, 1.0f);
}

static ImVec2 ExtFgCrtSize()
{
    const float fs = ImGui::GetFontSize();
    return ImVec2(std::floor(fs * 19.0f), std::floor(fs * 9.6f));
}

// ILION: External FG tab (Advanced mode only). MFG SUPER MOTION's engine runs as its own process on the game's
// window (misc/IlionExternalFg.h); nothing of OptiScaler's frame generation is touched. The screen shows only that
// engine's own figures. Settings are in small key-selected pages, the program's location and notes under Info.
void MenuCommon::RenderExternalFgTab(RenderMenuContext& ctx)
{
    static IlionExternalFg::Engine engine;
    static char folderText[MAX_PATH] {};
    static std::string startError;
    static bool needsRestart = false;
    static int page = 0;
    static int startedGenerator = -1, startedSearch = 0;

    auto& shared = ExternalFg();
    auto& settings = shared.settings;
    const auto& iniPath = shared.iniPath;

    const float scale = ctx.menuResScale;
    const ImVec4 warnColour = IlionTheme::Themed(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), IlionTheme::Role::Warn);

    const bool searching = !shared.finder.Done();
    const std::filesystem::path enginePath = searching ? std::filesystem::path() : shared.finder.Result();

    // What was found is kept, so the next start finds it at once
    if (!enginePath.empty())
    {
        const auto app = enginePath.parent_path().parent_path().wstring();
        if (settings.folder != app)
        {
            settings.folder = app;
            IlionExternalFg::Save(settings, iniPath);
        }
    }

    const auto status = engine.GetStatus();
    const bool running = status.running;

    // Only one frame generation at a time
    const auto fg = ctx.state.currentFG;
    if (fg != nullptr && fg->IsActive() && !fg->IsPaused())
        TextWrappedColored(warnColour, "%s", Tr("OptiScaler's frame generation is on. Turn it off in Frame Gen "
                                                 "before starting an external one."));

    // The screen, centred, and the key that starts or stops the engine under it
    ExtFgCrtData d;
    d.generator = ExtFgGeneratorName(running ? startedGenerator : settings.generator,
                                     running ? startedSearch : settings.msmSearch);
    if (running)
    {
        d.state = !status.error.empty() ? 3 : status.baseFps > 0.0 ? 2 : 1;
        d.baseFps = status.baseFps;
        d.outputFps = status.outputFps;
        if (!status.mfg.empty() && isdigit((unsigned char) status.mfg[0]))
            d.multiplier = status.mfg.substr(0, status.mfg.find(' '));

        // "@source <hwnd> <w> <h> <dxgi|wgc> <placement>": the fourth word
        char hwndText[32] {}, api[16] {};
        int w = 0, h = 0;
        if (sscanf_s(status.source.c_str(), "%31s %d %d %15s", hwndText, 32, &w, &h, api, 16) == 4)
            d.capture = IlionTheme::Upper(api);
    }
    else if (!startError.empty() || !status.error.empty())
    {
        d.state = 3;
    }

    const ImVec2 crt = ExtFgCrtSize();
    const float avail = ImGui::GetContentRegionAvail().x;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, std::floor((avail - crt.x) * 0.5f)));
    const ImVec2 crtPos = ImGui::GetCursorScreenPos();
    ImGui::Dummy(crt);
    DrawExtFgCrt(crtPos, crt, d);
    if (ImGui::IsItemHovered())
        WrappedTooltip("%s", Tr("Figures of MFG SUPER MOTION itself: the game's real frames, the frames it generates "
                                "and all it shows. OptiScaler's frame generation is not counted here."));

    const char* action = running ? TrL("Stop##ExtFg") : TrL("Start##ExtFg");
    const float actionWidth = IlionTheme::LampButtonWidth(action);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, std::floor((avail - actionWidth) * 0.5f)));
    ImGui::BeginDisabled(!running && enginePath.empty());
    if (IlionTheme::LampButton(action, running ? IlionTheme::Lamp::Red : IlionTheme::Lamp::Green))
    {
        if (running)
        {
            engine.Stop();
            LOG_INFO("External FG: stopped");
        }
        else
        {
            startError.clear();
            if (engine.Start(enginePath, Util::GetProcessWindow(), settings, startError))
            {
                startedGenerator = settings.generator;
                startedSearch = settings.msmSearch;
                LOG_INFO("External FG: started {}", wstring_to_string(enginePath.wstring()));
            }
            else
            {
                LOG_WARN("External FG: {}", startError);
            }
        }
        needsRestart = false;
    }
    ImGui::EndDisabled();

    // One short line only when something needs the user
    if (searching)
        TextWrappedDisabled("%s", Tr("Looking for MFG SUPER MOTION..."));
    else if (enginePath.empty())
        TextWrappedColored(warnColour, "%s", Tr("MFG SUPER MOTION not found. See Info."));
    if (!startError.empty())
        TextWrappedColored(warnColour, "%s: %s", Tr("Could not start"), startError.c_str());
    if (!status.error.empty())
        TextWrappedColored(warnColour, "%s: %s", Tr("Error"), status.error.c_str());
    if (needsRestart)
        TextWrappedColored(warnColour, "%s", Tr("Stop and start again to apply the generator or the window mode."));

    // Page keys, centred: the same keys as the menu's mode selector
    ImGui::Dummy(ImVec2(0.0f, std::floor(4.0f * scale)));
    const char* pages[] = { Tr("Generation"), Tr("Pacing"), Tr("Capture"), Tr("Info") };
    float widest = 0.0f;
    for (const char* p : pages)
        widest = std::max(widest, ImGui::CalcTextSize(p).x);
    const ImVec2 keySize = IlionTheme::SelectorKeySize(widest, false);
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float rowWidth = keySize.x * 4.0f + spacing * 3.0f;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, std::floor((avail - rowWidth) * 0.5f)));

    for (int i = 0; i < 4; i++)
    {
        if (i > 0)
            ImGui::SameLine();

        const ImVec2 pos = ImGui::GetCursorScreenPos();
        ImGui::PushID(i);
        if (ImGui::InvisibleButton("##ExtFgPage", keySize))
            page = i;
        const bool hovered = ImGui::IsItemHovered();
        ImGui::PopID();

        IlionTheme::DrawSelectorKey(ImGui::GetWindowDrawList(), ImRect(pos, ImVec2(pos.x + keySize.x, pos.y + keySize.y)),
                                    page == i, hovered);
        const ImVec2 textSize = ImGui::CalcTextSize(pages[i]);
        ImGui::GetWindowDrawList()->AddText(
            ImVec2(std::floor(pos.x + (keySize.x - textSize.x) * 0.5f), std::floor(pos.y + (keySize.y - textSize.y) * 0.5f)),
            ImGui::GetColorU32(page == i || hovered ? ImGuiCol_Text : ImGuiCol_TextDisabled), pages[i]);
    }

    ImGui::Dummy(ImVec2(0.0f, std::floor(4.0f * scale)));

    // Controls: a change is saved and, while the engine runs, sent to it
    const auto changed = [&](const char* key, int value)
    {
        IlionExternalFg::Save(settings, iniPath);
        if (running)
        {
            if (IlionExternalFg::IsLiveKey(key))
                engine.Send(key, value);
            else
                needsRestart = true;
        }
    };

    const float fieldWidth = std::min(ImGui::GetContentRegionAvail().x * 0.55f, 240.0f * scale);
    const auto combo = [&](const char* label, const char* key, int& value, std::initializer_list<const char*> names,
                           int first)
    {
        std::vector<const char*> items;
        for (const char* n : names)
            items.push_back(Tr(n));

        const int index = std::clamp(value - first, 0, (int) items.size() - 1);
        ImGui::SetNextItemWidth(fieldWidth);
        if (IlionTheme::BeginCombo(TrL(label), items[index]))
        {
            for (int i = 0; i < (int) items.size(); i++)
            {
                if (ImGui::Selectable(items[i], index == i) && index != i)
                {
                    value = i + first;
                    changed(key, value);
                }
            }
            ImGui::EndCombo();
        }
    };

    const auto check = [&](const char* label, const char* key, int& value)
    {
        bool on = value != 0;
        if (IlionTheme::Checkbox(TrL(label), &on))
        {
            value = on ? 1 : 0;
            changed(key, value);
        }
    };

    switch (page)
    {
    case 0: // Generation
        combo("Generator##ExtFg", "generator", settings.generator, { "DLSS MFG (NVIDIA RTX)", "XeSS MFG", "SMFG (any card)" },
              0);
        combo("Multiplier##ExtFg", "multiplier", settings.multiplier, { "x2", "x3", "x4", "x5", "x6" }, 2);
        combo("Kind of game##ExtFg", "game", settings.game,
              { "Automatic", "2D", "2.5D", "First person", "Third person", "Racing", "Open world", "Manual size" }, 0);
        if (settings.game == 7)
        {
            ImGui::SetNextItemWidth(fieldWidth);
            if (IlionTheme::SliderInt(TrL("Generated frame size (%)##ExtFg"), &settings.flow, 10, 100))
                changed("flow", settings.flow);
        }
        if (settings.generator == 2)
            combo("SMFG motion search##ExtFg", "msm_search", settings.msmSearch, { "Full", "Fast" }, 0);
        check("Adaptive Motion Learning##ExtFg", "aml", settings.aml);
        check("No generated frames in very fast motion##ExtFg", "guard", settings.guard);
        check("Keep still pixels##ExtFg", "still", settings.still);
        break;

    case 1: // Pacing
        combo("Queue##ExtFg", "queue", settings.queue, { "Least delay", "Balanced", "Smoothest" }, 0);
        combo("Sync##ExtFg", "sync", settings.sync,
              { "Off (tearing)", "Newest frame each refresh", "VSync", "Every 2nd refresh", "Every 3rd refresh" }, 0);
        check("Low Latency##ExtFg", "low_latency", settings.lowLatency);
        check("VRR##ExtFg", "vrr", settings.vrr);
        check("Limit to refresh rate##ExtFg", "screen_limit", settings.screenLimit);
        check("FPS counter of the program##ExtFg", "counter", settings.counter);
        break;

    case 2: // Capture
        combo("Capture##ExtFg", "capture_api", settings.captureApi, { "Automatic", "DXGI", "WGC" }, -1);
        combo("Game window##ExtFg", "window_mode", settings.windowMode, { "Borderless", "Scaled to screen", "As it is" }, 0);
        TextWrappedDisabled("%s", Tr("Generator and Game window are applied when the engine starts. The engine puts the "
                                     "game's window back as it was when it stops."));
        break;

    default: // Info
    {
        TextWrappedDisabled("%s", Tr("Frame generation from outside the game: a separate program captures the game's "
                                     "window and shows real and generated frames in its own window over it. Use only "
                                     "one frame generation at a time."));
        TextWrappedDisabled("%s", Tr("Free program by NODIX TECH, installed by you (github.com/NODIX-TECH/MFG-SUPER-MOTION). "
                                     "ILION does not include it: it starts its engine and sends it your settings. SMFG "
                                     "works on any card; DLSS MFG needs NVIDIA RTX."));

        if (!searching)
        {
            if (enginePath.empty())
                TextWrappedColored(warnColour, "%s", Tr("MFG SUPER MOTION was not found. Install it, or copy its folder (the "
                                                         "one with the Engine folder) and press Paste."));
            else
                TextWrappedDisabled("%s: %s", Tr("Found automatically"), wstring_to_string(enginePath.wstring()).c_str());

            const auto useFolder = [&](const std::wstring& folder)
            {
                settings.folder = folder;
                strncpy_s(folderText, wstring_to_string(folder).c_str(), _TRUNCATE);
                IlionExternalFg::Save(settings, iniPath);
                shared.finder.Restart(folder);
            };

            ImGui::SetNextItemWidth(fieldWidth);
            if (ImGui::InputText(TrL("Program folder##ExtFg"), folderText, sizeof(folderText),
                                 ImGuiInputTextFlags_EnterReturnsTrue))
                useFolder(IlionExternalFg::CleanPath(string_to_wstring(folderText)));

            if (IlionTheme::Button(TrL("Paste##ExtFg")))
            {
                const auto pasted = IlionExternalFg::ClipboardText();
                if (!pasted.empty())
                    useFolder(pasted);
            }
            ImGui::SameLine();
            if (IlionTheme::Button(TrL("Search again##ExtFg")))
                shared.finder.Restart(settings.folder);
        }

        SectionTitle("AMD Fluid Motion Frames (AFMF)");
        TextWrappedDisabled("%s", Tr("Frame generation of the Radeon driver. It is switched in AMD Software: Adrenalin "
                                     "Edition (Alt+R > Gaming > Graphics). ILION does not switch it yet: it needs AMD's "
                                     "ADLX library, a later step. Do not use it together with MFG SUPER MOTION or "
                                     "OptiScaler's frame generation."));
        break;
    }
    }
}

void MenuCommon::RenderMainMenuTabs(RenderMenuContext& ctx)
{
    ExternalFg(); // ILION: the search for MFG SUPER MOTION starts with the menu
    auto& state = ctx.state;
    auto config = ctx.config;
    auto& currentFeature = ctx.currentFeature;
    auto& menuResScale = ctx.menuResScale;

    using DrawFn = void (*)(RenderMenuContext&);

    struct Tab
    {
        const char* group;
        const char* label;
        const char* description;
        DrawFn draw; // Drawn above the boxes of the tab
        bool hideLabelInMenu;
        bool (*visible)(RenderMenuContext&) = nullptr; // Tab hidden when returned false
    };

    // Boxes of a tab come from GetMenuBoxes, by the label of the tab
    // clang-format off
    static const Tab tabs[] = {
        { "MAIN", "Custom", "Boxes picked in Menu > Custom Tab.", RenderCustomTab },
        { "MAIN", "Upscaling", "Backend and output scaling.", RenderUpscalerStateMessage },
        { "MAIN", "Frame Gen", "Frame generation source, output and pacing." },
        { "MAIN", "External FG", "MFG SUPER MOTION started and steered from here; AFMF explained. Advanced mode only.",
          RenderExternalFgTab, false,
          [](RenderMenuContext& c) { return !c.config->MenuBasicMode.value_or_default() && !c.config->MenuEssentialMode.value_or_default(); } },
        { "MAIN", "Anti-Ghosting", "Options against ghosting and trails, grouped by the technology they work with.",
          RenderAntiGhostingTab },
        { "MAIN", "Latency & FPS", "Frame limiter, low latency and V-Sync." },
        { "MAIN", "Graphics", "Colour, sharpness, textures and detail of the picture." },
        { "MAIN", "Overlay", "Real-time metrics over the game: FPS, frame times and hardware sensors." },
        { "MAIN", "Benchmark", "Manual performance tests of the upscaler in use." },
        { "SYSTEM", "Compatibility", "Init flags, hooks, plugins and game specific workarounds." },
        { "ADVANCED", "Menu", "Menu scale, theme and keybinds." },
        { "ADVANCED", "Quirks", "Game specific quirks applied by OptiScaler.",
+          [](RenderMenuContext& c) { RenderCard(c, RenderQuirksSettings); }, false,
           // Only a list of texts, the same list is in Help / Status > System. Kept out of the sidebar.
+          [](RenderMenuContext& c) { return false; } },
        { "ADVANCED", "Help / Status", "Support info. Filled dot: on or found, hollow dot: off or missing.", nullptr, true },
    };
    // clang-format on

    constexpr int customTab = 0;
    const bool customTabEnabled = config->CustomTabEnabled.value_or_default();

    // ILION Essential mode: the everyday tabs only; Custom, Compatibility, Quirks and Help / Status stay in Advanced
    const bool essential = !config->MenuBasicMode.value_or_default() && config->MenuEssentialMode.value_or_default();

    auto isTabVisible = [&](int i)
    {
        if (essential && EssentialHidesTab(tabs[i].label))
            return false;
        if (i == customTab)
            return customTabEnabled;
        return tabs[i].visible == nullptr || tabs[i].visible(ctx);
    };

    if (requestedMenuTab != nullptr)
    {
        for (int i = 0; i < (int) std::size(tabs); i++)
        {
            if (strcmp(tabs[i].label, requestedMenuTab) == 0)
                selectedMenuTab = i;
        }

        requestedMenuTab = nullptr;
    }

    selectedMenuTab = std::clamp(selectedMenuTab, 0, (int) std::size(tabs) - 1);

    // Selected tab can disappear, pick the first one that's visible
    if (!isTabVisible(selectedMenuTab))
    {
        for (int i = 0; i < (int) std::size(tabs); i++)
        {
            if (isTabVisible(i))
            {
                selectedMenuTab = i;
                break;
            }
        }
    }

    const auto& style = ImGui::GetStyle();

    // Space kept free below for the frametime graph and the buttons
    const float footerHeight = FooterHeight();
    // The industrial theme has a pilot lamp before every tab, it needs the room
    const float sidebarWidth = (IlionTheme::industrial ? 160.0f : 130.0f) * menuResScale;
    const float tabHeight = ImGui::GetFrameHeight() * 1.35f;

    // Sidebar -----------------------------
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::BeginChild("##MenuSidebar", ImVec2(sidebarWidth, -footerHeight),
                      ImGuiChildFlags_Borders | ImGuiChildFlags_NavFlattened);
    {
        const char* lastGroup = nullptr;

        for (int i = 0; i < (int) std::size(tabs); i++)
        {
            if (!isTabVisible(i))
                continue;

            if (lastGroup == nullptr || strcmp(lastGroup, tabs[i].group) != 0)
            {
                if (lastGroup != nullptr)
                    ImGui::Dummy(ImVec2(0.0f, style.ItemSpacing.y));

                // Groups are names, tabs are keys: only the keys carry a lamp
                if (IlionTheme::industrial)
                    IlionTheme::GroupPlate(Tr(tabs[i].group));
                else
                    ImGui::TextDisabled("%s", Tr(tabs[i].group));

                lastGroup = tabs[i].group;
            }

            const bool selected = selectedMenuTab == i;

            // The key of the industrial theme is drawn over the selectable, which keeps doing the work unseen
            if (IlionTheme::industrial)
            {
                const ImVec4 none(0.0f, 0.0f, 0.0f, 0.0f);

                ImGui::PushStyleColor(ImGuiCol_Header, none);
                ImGui::PushStyleColor(ImGuiCol_HeaderHovered, none);
                ImGui::PushStyleColor(ImGuiCol_HeaderActive, none);
            }

            ImGui::PushID(i);
            if (ImGui::Selectable("##Tab", selected, 0, ImVec2(0.0f, tabHeight)))
                selectedMenuTab = i;
            ImGui::PopID();

            if (IlionTheme::industrial)
                ImGui::PopStyleColor(3);

            // Label is drawn manually to center it vertically and to leave room for the accent bar
            const ImVec2 rectMin = ImGui::GetItemRectMin();
            const ImVec2 rectMax = ImGui::GetItemRectMax();
            auto drawList = ImGui::GetWindowDrawList();

            if (IlionTheme::industrial)
            {
                // A key with its pilot, lit on the tab that is open
                IlionTheme::DrawTabPlate(drawList, ImVec2(rectMin.x, rectMin.y + 1.0f), ImVec2(rectMax.x, rectMax.y - 1.0f),
                                         selected, ImGui::IsItemHovered(), ImGui::IsItemActive(), Tr(tabs[i].label),
                                         menuResScale);
                continue;
            }

            if (selected)
            {
                drawList->AddRectFilled(rectMin, ImVec2(rectMin.x + 3.0f * menuResScale, rectMax.y),
                                        ImGui::GetColorU32(ImGuiCol_CheckMark));
            }

            drawList->AddText(ImVec2(rectMin.x + 10.0f * menuResScale,
                                     rectMin.y + (tabHeight - ImGui::GetTextLineHeight()) * 0.5f),
                              ImGui::GetColorU32(ImGuiCol_Text), Tr(tabs[i].label));
        }

        // OptiPatcher status only visible when used
        const bool optiPatcher = state.isOptiPatcherSucceed;

        // Compact state of the upscaler and FG pinned to the bottom of the sidebar
        const float statusLines = 5.0f + (optiPatcher ? 1.0f : 0.0f); // SR and FG can take two rows each
        const float statusHeight = 1.0f + ImGui::GetTextLineHeightWithSpacing() * statusLines + style.ItemSpacing.y;
        const float gap = ImGui::GetContentRegionAvail().y - statusHeight - style.ItemSpacing.y;
        if (gap > 0.0f)
            ImGui::Dummy(ImVec2(0.0f, gap));

        ImGui::Separator();
        ImGui::Spacing();

        ImGui::TextDisabled(Tr("SR"));
        ImGui::SameLine(0.0f, 8.0f * menuResScale);
        if (currentFeature == nullptr || !currentFeature->IsInited())
            ImGui::TextDisabled("%s", Tr("None"));
        else if (currentFeature->IsFrozen())
            ImGui::TextColored(toneMapColor(IlionTheme::Themed(ImVec4(1.f, 0.8f, 0.f, 1.f), IlionTheme::Role::Warn)), "%s", Tr("Idle"));
        else
        {
            const float renderWidth = (float) currentFeature->RenderWidth();
            const float upscaleRatio = renderWidth > 0.0f ? (float) currentFeature->TargetWidth() / renderWidth : 0.0f;

            TextWrappedColored(toneMapColor(IlionTheme::Themed(ImVec4(0.35f, 0.85f, 0.45f, 1.f), IlionTheme::Role::Active)), "%s (%.2f)",
                               UpscalerLabel(currentFeature).c_str(), upscaleRatio);
        }

        ImGui::TextDisabled(Tr("FG"));
        ImGui::SameLine(0.0f, 8.0f * menuResScale);
        auto fg = state.currentFG;
        if (fg != nullptr && fg->IsActive() && !fg->IsPaused())
        {
            const UINT multiplier = fg->GetInterpolatedFrameCount() + 1;

            // XeFG already has multiplier in name when MFG detected
            std::string fgName = fg->Name();
            const std::string multiplierSuffix = StrFmt(" %ux", multiplier);
            if (fgName.ends_with(multiplierSuffix))
                fgName.erase(fgName.size() - multiplierSuffix.size());

            TextWrappedColored(toneMapColor(IlionTheme::Themed(ImVec4(0.35f, 0.85f, 0.45f, 1.f), IlionTheme::Role::Active)), "%s %ux", fgName.c_str(), multiplier);
        }
        else
        {
            // Not OptiScaler's own FG: the DLSSG of the game, or what replaces it. The same count the Frame Gen
            // tab shows as "Current DLSSG state".
            const auto external = GetExternalFgStatus(state.dlssgDetectedInterpolationCount, state.activeFgNvngx,
                                                      state.swapchainApi == DX12);

            if (external.kind == ExternalFgStatus::Kind::Active)
                TextWrappedColored(toneMapColor(IlionTheme::Themed(ImVec4(0.35f, 0.85f, 0.45f, 1.f), IlionTheme::Role::Active)), "%s", external.text.c_str());
            else if (external.kind == ExternalFgStatus::Kind::Unknown)
                TextWrappedColored(toneMapColor(IlionTheme::Themed(ImVec4(1.f, 0.8f, 0.f, 1.f), IlionTheme::Role::Warn)), "%s", external.text.c_str());
            else
                ImGui::TextDisabled("%s", Tr("Off"));
        }

        const bool spoofing = config->DxgiSpoofing.value_or_default();
        ImGui::TextDisabled(Tr("Spoof"));
        ImGui::SameLine(0.0f, 8.0f * menuResScale);
        if (spoofing)
            ImGui::TextColored(toneMapColor(IlionTheme::Themed(ImVec4(0.35f, 0.85f, 0.45f, 1.f), IlionTheme::Role::Good)), Tr("Yes"));
        else
            ImGui::TextDisabled(Tr("No"));

        if (optiPatcher)
        {
            ImGui::TextDisabled(Tr("OP"));
            ImGui::SameLine(0.0f, 8.0f * menuResScale);
            ImGui::TextColored(toneMapColor(IlionTheme::Themed(ImVec4(0.35f, 0.85f, 0.45f, 1.f), IlionTheme::Role::Good)), Tr("Yes"));
        }
    }

    ImGui::EndChild();

    ImGui::SameLine();

    // Tab title, short explanation and the scrollable content -----------------------------
    ImGui::BeginChild("##MenuContent", ImVec2(0.0f, -footerHeight), ImGuiChildFlags_NavFlattened,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    {
        const Tab& tab = tabs[selectedMenuTab];

        // if (!tab.hideLabelInMenu)
        //{
        //     if (config->UseHQFont.value_or_default())
        //         ImGui::PushFontSize(std::round(fontSize * menuResScale * 1.3f));

        //    ImGui::TextUnformatted(tab.label);

        //    if (config->UseHQFont.value_or_default())
        //        ImGui::PopFontSize();
        //}

        // ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        // ImGui::TextWrapped("%s", tab.description);
        // ImGui::PopStyleColor();

        // ImGui::Separator();

        // Own ID per tab, so every tab remembers its scroll position
        // ILION: Personal with its Customise panel open, the boxes at the left and the panel at the right
        // ILION: the ReShade panel only beside Graphics; leaving the tab closes it
        if (strcmp(tab.label, "Graphics") != 0)
        {
            reshadePanelOpen = false;
            reshadePanelAmount = 0.0f;
        }

        const bool reshadePanel = reshadePanelAmount > 0.0f;
        const bool personalPanel = selectedMenuTab == customTab && personalPanelOpen;
        float panelWidth = 0.0f;

        if (personalPanel)
        {
            // As wide as its longest name or key, between 32% and 55% of the content: the boxes stay in view
            size_t boxCount = 0;
            const MenuBox* boxes = GetMenuBoxes(boxCount);
            float need = std::max(ButtonWidth(TrL("Restore default layout")), ButtonWidth(TrL("Close##personal")));

            for (size_t i = 0; i < boxCount; i++)
            {
                if (boxes[i].pinnable)
                    need = std::max(need, ImGui::GetFrameHeight() * 1.5f + style.ItemSpacing.x +
                                              ImGui::CalcTextSize(Tr(boxes[i].name)).x);
            }

            need += style.WindowPadding.x * 2.0f + style.ScrollbarSize + style.ItemSpacing.x;

            const float avail = ImGui::GetContentRegionAvail().x;
            panelWidth = ImFloor(std::clamp(need, avail * 0.32f, avail * 0.55f));
        }
        else if (reshadePanel)
        {
            // The window grew by this width (see the main window), so the tab keeps its own
            // The same amount the window grows by (width and gap), so the tab keeps its width while the panel slides
            panelWidth = std::max(1.0f, std::floor((ReShadePanelWidth(menuResScale) + style.ItemSpacing.x) * reshadePanelAmount) - style.ItemSpacing.x);
        }

        // Catalogue installs finish in the background: their result is taken whichever tab is shown
        ProcessReShadeCatalogue();

        ImGui::PushID(selectedMenuTab);
        ImGui::BeginChild("##MenuTabBody", ImVec2((personalPanel || reshadePanel) ? -(panelWidth + style.ItemSpacing.x) : 0.0f, 0.0f));

        if (tab.draw != nullptr)
            tab.draw(ctx);

        size_t boxCount = 0;
        const MenuBox* boxes = GetMenuBoxes(boxCount);
        DrawFn lastSource = nullptr;

        // Essential: the everyday boxes of the tab, the secondary ones folded under "More options", the diagnostic and
        // very specialised ones left to Advanced. Each box is drawn by its own code, nothing changes in what it does.
        std::vector<const MenuBox*> secondary;

        for (size_t i = 0; i < boxCount; i++)
        {
            if (strcmp(boxes[i].tab, tab.label) != 0)
                continue;

            if (essential)
            {
                const EssentialRole role = EssentialRoleOf(boxes[i].id);

                if (role == EssentialRole::Main)
                {
                    ImGui::PushID(boxes[i].id);
                    RenderMenuBox(ctx, boxes[i]);
                    ImGui::PopID();
                }
                else if (role == EssentialRole::Secondary)
                {
                    secondary.push_back(&boxes[i]);
                }

                continue;
            }

            // A function with several boxes is drawn once for all of them
            if (boxes[i].draw == nullptr)
            {
                if (boxes[i].source != lastSource)
                    boxes[i].source(ctx);
            }
            else
            {
                boxes[i].draw(ctx);
            }

            lastSource = boxes[i].source;
        }

        if (!secondary.empty())
        {
            ImGui::Spacing();

            if (auto ch = ScopedCollapsingHeader(TrL("More options##Essential")); ch.IsHeaderOpen())
            {
                ScopedIndent indent {};

                for (const MenuBox* box : secondary)
                {
                    ImGui::PushID(box->id);
                    RenderMenuBox(ctx, *box);
                    ImGui::PopID();
                }
            }
        }

        ImGui::EndChild();
        ImGui::PopID();

        if (personalPanel)
        {
            ImGui::SameLine();
            ImGui::BeginChild("##PersonalPanel", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);
            RenderPersonalPanel(ctx);
            ImGui::EndChild();
        }
        else if (reshadePanel)
        {
            ImGui::SameLine();
            ImGui::BeginChild("##ReShadePanel", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);
            // The controls only once it is fully open: nothing is drawn cut while it slides
            if (reshadePanelAmount >= 1.0f)
                RenderReShadePanel(ctx);
            ImGui::EndChild();
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

void MenuCommon::PlotLinesWithRange(const char* label, float (*values_getter)(void* data, int idx), void* data,
                                    int values_count, int values_offset, const char* overlay_text, ImVec2 graph_size,
                                    float padding)
{
    float v_min = FLT_MAX;
    float v_max = -FLT_MAX;
    for (int i = 0; i < plotWidth; i++)
    {
        const float v = values_getter(data, i);
        if (v != v) // Ignore NaN values
            continue;
        v_min = ImMin(v_min, v);
        v_max = ImMax(v_max, v);
    }

    ImGui::PlotEx(ImGuiPlotType_Lines, label, values_getter, data, values_count, values_offset, overlay_text, v_min,
                  v_max, graph_size);

    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        WrappedTooltip(Tr("Range: %.2f ms - %.2f ms"), v_min, v_max);
}

void MenuCommon::RenderFrameTimeGraph(RenderMenuContext& ctx)
{
    auto& frameTime = ctx.frameTime;
    auto& frameRate = ctx.frameRate;
    auto& menuResScale = ctx.menuResScale;
    auto& averageFrameTime = ctx.averageFrameTime;

    FitText(StrFmt(Tr("Frametime: %.2fms / %.1ffps (avg: %.1ffps)"), frameTime, frameRate, 1000.f / averageFrameTime),
            StrFmt("%.2f ms | %.1f fps | %.1f", frameTime, frameRate, 1000.f / averageFrameTime));

    PlotLinesWithRange(
        "##FrameTime",
        [](void* rb, int idx) -> float { return static_cast<RingBuffer<float, plotWidth>*>(rb)->At(idx); },
        &gFrameTimes, plotWidth, 0, nullptr, ImVec2(0.0f, FooterPlotHeight()), 4.0f * menuResScale);
}

void MenuCommon::RenderUpscalerTimeGraph(RenderMenuContext& ctx)
{
    auto& state = ctx.state;
    auto& currentFeature = ctx.currentFeature;
    auto& menuResScale = ctx.menuResScale;
    auto& averageUpscalerFT = ctx.averageUpscalerFT;

    if (!IsUpscalerActive(currentFeature))
        return;

    FitText(StrFmt(Tr("Upscaler GPU time: %.2fms (avg: %.2fms)"), state.upscaleTimes.back(), averageUpscalerFT),
            StrFmt("SR %.2f ms (%.2f)", state.upscaleTimes.back(), averageUpscalerFT));

    PlotLinesWithRange(
        "##UpscalerTime",
        [](void* rb, int idx) -> float { return static_cast<RingBuffer<float, plotWidth>*>(rb)->At(idx); },
        &gUpscalerTimes, plotWidth, 0, nullptr, ImVec2(0.0f, FooterPlotHeight()), 4.0f * menuResScale);
}

// INFO ILION: its ON/OFF key and the line of the old green screen (menu/ilion_info.h). Off: the glass stays empty and
// nothing is updated or animated.
static void RenderInfoLine(decltype(Config::Instance()) config)
{
    const bool on = config->IlionInfoLine.value_or_default();

    if (IlionTheme::Button(on ? "INFO ON##IlionInfo" : "INFO OFF##IlionInfo"))
    {
        config->IlionInfoLine = !on;
        config->SaveMenuPreferences();
    }

    if (ImGui::IsItemHovered())
        WrappedTooltip("%s", Tr("INFO ILION: short tips about the technologies ILION uses.\nOFF stops the line and its animation."));

    ImGui::SameLine();

    const ImVec2 min = ImGui::GetCursorScreenPos();
    const ImVec2 size(std::max(1.0f, ImGui::GetContentRegionAvail().x), ImGui::GetFrameHeight());
    ImGui::Dummy(size);

    ImFont* font = IlionTheme::crtFont != nullptr ? IlionTheme::crtFont : ImGui::GetFont();

    // Aged phosphor and radioactive amber on a dark tube, the same in every theme: it is a screen, not a panel
    IlionInfo::Draw(ImGui::GetWindowDrawList(), min, ImVec2(min.x + size.x, min.y + size.y), on, font,
                    ImGui::GetFontSize(), &Ilion::Tr, toneMapColor(ImVec4(0.42f, 0.86f, 0.40f, 1.0f)),
                    toneMapColor(ImVec4(1.0f, 0.66f, 0.14f, 1.0f)), ImVec4(0.020f, 0.045f, 0.025f, 1.0f),
                    ImVec4(0.30f, 0.34f, 0.30f, 1.0f));
}

void MenuCommon::RenderMainMenuFooter(RenderMenuContext& ctx)
{
    ImGui::Separator();
    ImGui::Spacing();

    // Placed by hand instead of using a table or SameLine. Both of them carry the text baseline of the
    // first graph over to the second one, which made it sit a few pixels lower
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float graphWidth = (ImGui::GetContentRegionAvail().x - spacing) * 0.5f;
    const ImVec2 startPos = ImGui::GetCursorPos();

    ImGui::PushItemWidth(graphWidth);

    RenderFrameTimeGraph(ctx);
    const float endY = ImGui::GetCursorPosY();

    // The indent is needed because the cursor goes back to the left edge after every item
    ImGui::SetCursorPosY(startPos.y);
    ImGui::Indent(graphWidth + spacing);
    RenderUpscalerTimeGraph(ctx);
    ImGui::Unindent(graphWidth + spacing);

    ImGui::PopItemWidth();

    ImGui::SetCursorPosY(endY);

    RenderInfoLine(ctx.config);

    RenderMainMenuBottomBar(ctx);
}

void MenuCommon::RenderShaderTimes(RenderMenuContext& ctx)
{
    auto& state = ctx.state;
    auto& currentFeature = ctx.currentFeature;

    if (!IsUpscalerActive(currentFeature) || state.detailedGpuTimes.empty())
        return;

    SectionTitle("Per shader GPU times");

    if (ImGui::BeginTable("ShaderTimes", 2, ImGuiTableFlags_SizingFixedSame))
    {
        bool hasExtra = false;

        for (auto& [name, time, includedInUpscalerTime] : state.detailedGpuTimes)
        {
            if (!includedInUpscalerTime)
            {
                hasExtra = true;
                continue;
            }

            auto formattedTime = StrFmt("%7.2f ms", time);

            ImGui::TableNextColumn();
            ImGui::Text("%s", name.c_str());

            ImGui::TableNextColumn();
            ImGui::Text("%s", formattedTime.c_str());
        }

        if (hasExtra)
        {
            ImGui::TableNextRow();
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextDisabled(Tr("Extra shaders:"));
            ImGui::TableNextColumn();
            ImGui::TextDisabled("");
            for (auto& [name, time, includedInUpscalerTime] : state.detailedGpuTimes)
            {
                if (includedInUpscalerTime)
                    continue;

                auto formattedTime = StrFmt("%7.2f ms", time);

                ImGui::TableNextColumn();
                ImGui::Text("%s", name.c_str());

                ImGui::TableNextColumn();
                ImGui::Text("%s", formattedTime.c_str());
            }
        }

        ImGui::EndTable();
    }
}

void MenuCommon::RenderMainMenuBottomBar(RenderMenuContext& ctx)
{
    auto config = ctx.config;
    auto& io = ctx.io;

    // Where the bottom band joins the rest of the chassis
    IlionTheme::DrawChassisJoint();

    // BOTTOM LINE ---------------
    if (IlionTheme::Button(TrL("Open Wiki")))
    {
        auto pIO = &ImGui::GetPlatformIO();
        auto ctx = ImGui::GetCurrentContext();
        pIO->Platform_OpenInShellFn(ctx, "https://github.com/optiscaler/OptiScaler/wiki");
    }
    ShowTooltip("Click to open the OptiScaler Wiki page\nin your default browser\n\n"
                "Compatibility list with known game issues\nand workarounds, FG options explained\n"
                "and other useful info");

    ImGui::SameLine();

    auto& style = ImGui::GetStyle();
    float rightButtonsWidth =
        ImGui::CalcTextSize(Tr("Save Settings")).x + ImGui::CalcTextSize(Tr("Close")).x +
        style.FramePadding.x * 4.0f + 6.0f;

    float avail = ImGui::GetContentRegionAvail().x;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + avail - rightButtonsWidth);

    if (IlionTheme::Button(TrL("Save Settings")))
        config->SaveIni();

    SameLineIfRoom(ButtonWidth(TrL("Close")), 6.0f);

    if (IlionTheme::Button(TrL("Close")))
    {
        _isVisible = false;
        hasGamepad = (io.BackendFlags | ImGuiBackendFlags_HasGamepad) > 0;
        io.BackendFlags &= 30;
        io.ConfigFlags = ImGuiConfigFlags_NoMouse | ImGuiConfigFlags_NoMouseCursorChange | ImGuiConfigFlags_NoKeyboard;

        _showMipmapCalcWindow = false;
        _showHudlessWindow = false;
        io.MouseDrawCursor = false;
        io.WantCaptureKeyboard = false;
        io.WantCaptureMouse = false;
    }
}

void MenuCommon::RenderMipmapBiasWindow(RenderMenuContext& ctx, ImGuiWindowFlags flags)
{
    auto config = ctx.config;
    auto& io = ctx.io;
    auto& currentFeature = ctx.currentFeature;

    // Metrics window (for debug)
    // ImGui::ShowMetricsWindow();

    // Mipmap calculation window
    if (_showMipmapCalcWindow && currentFeature != nullptr && !currentFeature->IsFrozen() && currentFeature->IsInited())
    {
        auto posX = (io.DisplaySize.x - 450.0f) / 2.0f;
        auto posY = (io.DisplaySize.y - 200.0f) / 2.0f;

        ImGui::SetNextWindowPos(ImVec2 { posX, posY }, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2 { 450.0f, 200.0f }, ImGuiCond_FirstUseEver);

        if (_displayWidth == 0)
        {
            if (config->OutputScalingEnabled.value_or_default())
            {
                _displayWidth = static_cast<uint32_t>(currentFeature->DisplayWidth() *
                                                      config->OutputScalingMultiplier.value_or_default());
            }
            else
            {
                _displayWidth = currentFeature->DisplayWidth();
            }

            _renderWidth = static_cast<uint32_t>(_displayWidth / 3.0f);
            _mipmapUpscalerQuality = 0;
            _mipmapUpscalerRatio = 3.0f;
            _mipBiasCalculated = log2((float) _renderWidth / (float) _displayWidth);
        }

        if (ImGui::Begin("Mipmap Bias", nullptr, flags))
        {
            if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_AnyWindow))
                ImGui::SetWindowFocus();

            if (ImGui::InputScalar("Display Width", ImGuiDataType_U32, &_displayWidth, NULL, NULL, "%u"))
            {
                if (_displayWidth <= 0)
                {
                    if (config->OutputScalingEnabled.value_or_default())
                    {
                        _displayWidth = static_cast<uint32_t>(currentFeature->DisplayWidth() *
                                                              config->OutputScalingMultiplier.value_or_default());
                    }
                    else
                    {
                        _displayWidth = currentFeature->DisplayWidth();
                    }
                }

                _renderWidth = static_cast<uint32_t>(_displayWidth / _mipmapUpscalerRatio);
                _mipBiasCalculated = log2((float) _renderWidth / (float) _displayWidth);
            }

            const char* q[] = { Tr("Ultra Performance"), Tr("Performance"), Tr("Balanced"), Tr("Quality"), Tr("Ultra Quality"), Tr("DLAA") };
            float fr[] = { 3.0f, 2.0f, 1.7f, 1.5f, 1.3f, 1.0f };
            auto configQ = _mipmapUpscalerQuality;

            const char* selectedQ = q[configQ];

            ImGui::BeginDisabled(config->UpscaleRatioOverrideEnabled.value_or_default());

            if (IlionTheme::BeginCombo(TrL("Upscaler Quality"), selectedQ))
            {
                for (int n = 0; n < 6; n++)
                {
                    if (ImGui::Selectable(q[n], (_mipmapUpscalerQuality == n)))
                    {
                        _mipmapUpscalerQuality = n;

                        float ov = -1.0f;

                        if (config->QualityRatioOverrideEnabled.value_or_default())
                        {
                            switch (n)
                            {
                            case 0:
                                ov = config->QualityRatio_UltraPerformance.value_or(-1.0f);
                                break;

                            case 1:
                                ov = config->QualityRatio_Performance.value_or(-1.0f);
                                break;

                            case 2:
                                ov = config->QualityRatio_Balanced.value_or(-1.0f);
                                break;

                            case 3:
                                ov = config->QualityRatio_Quality.value_or(-1.0f);
                                break;

                            case 4:
                                ov = config->QualityRatio_UltraQuality.value_or(-1.0f);
                                break;
                            }
                        }

                        if (ov > 0.0f)
                            _mipmapUpscalerRatio = ov;
                        else
                            _mipmapUpscalerRatio = fr[n];

                        _renderWidth = static_cast<uint32_t>(_displayWidth / _mipmapUpscalerRatio);
                        _mipBiasCalculated = log2((float) _renderWidth / (float) _displayWidth);
                    }
                }

                ImGui::EndCombo();
            }

            ImGui::EndDisabled();

            auto minLimit = config->ExtendedLimits.value_or_default() ? 0.1f : 1.0f;
            auto maxLimit = config->ExtendedLimits.value_or_default() ? 6.0f : 3.0f;
            if (IlionTheme::SliderFloat(TrL("Upscaler Ratio"), &_mipmapUpscalerRatio, minLimit, maxLimit, "%.2f"))
            {
                _renderWidth = static_cast<uint32_t>(_displayWidth / _mipmapUpscalerRatio);
                _mipBiasCalculated = log2((float) _renderWidth / (float) _displayWidth);
            }

            if (ImGui::InputScalar("Render Width", ImGuiDataType_U32, &_renderWidth, NULL, NULL, "%u"))
                _mipBiasCalculated = log2((float) _renderWidth / (float) _displayWidth);

            IlionTheme::SliderFloat(TrL("Mipmap Bias"), &_mipBiasCalculated, -15.0f, 0.0f, "%.6f");

            // BOTTOM LINE
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            ImGui::SameLine();
            ImGui::Spacing();

            constexpr float spacing = 6.0f;
            auto textSize = ImGui::CalcTextSize("Use Value");
            textSize += ImGui::CalcTextSize("Close");
            textSize.x += ImGui::GetStyle().FramePadding.x * 5.0f + spacing; // 2 sides * 2 buttons + 1

            float avail = ImGui::GetContentRegionAvail().x;
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + avail - textSize.x);

            if (IlionTheme::Button(TrL("Use Value")))
            {
                _mipBias = _mipBiasCalculated;
                _showMipmapCalcWindow = false;
            }

            SameLineIfRoom(ButtonWidth(TrL("Close")), spacing);

            if (IlionTheme::Button(TrL("Close")))
                _showMipmapCalcWindow = false;

            ImGui::Spacing();
            ImGui::Separator();

            ImGui::End();
        }
    }
}

void MenuCommon::RenderHudlessResourcesWindow(RenderMenuContext& ctx, ImGuiWindowFlags flags)
{
    auto& state = ctx.state;
    auto config = ctx.config;
    auto& io = ctx.io;

    auto fg = state.currentFG;
    if (_showHudlessWindow && config->FGHUDFix.value_or_default() && fg != nullptr && fg->IsActive())
    {
        auto posX = (io.DisplaySize.x - 400.0f) / 2.0f;
        auto posY = (io.DisplaySize.y - 300.0f) / 2.0f;

        ImGui::SetNextWindowPos(ImVec2 { posX, posY }, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2 { 400.0f, 300.0f });

        if (ImGui::Begin("HUDless Resources", nullptr, flags))
        {
            if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_AnyWindow))
                ImGui::SetWindowFocus();

            int btnCount = 100;

            if (ImGui::BeginTable("HUDlessTable", 2, ImGuiTableFlags_SizingFixedFit))
            {
                ImGui::TableSetupColumn("##1", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("##2", ImGuiTableColumnFlags_WidthFixed);

                ankerl::unordered_dense::map<void*, CapturedHudlessInfo>::iterator it;

                for (it = state.capturedHudlesses.begin(); it != state.capturedHudlesses.end(); it++)
                {
                    ImGui::TableNextRow();

                    ImGui::TableSetColumnIndex(0);

                    ImGui::Text(Tr("%08x, %s->%s, Count: %llu, %s"), (size_t) it->first,
                                GetSourceString(it->second.captureInfo & 0xFF).c_str(),
                                GetDispatchString(it->second.captureInfo & 0xFF00).c_str(), it->second.usageCount,
                                it->second.enabled ? "Active" : "Passive");

                    ImGui::TableSetColumnIndex(1);

                    btnCount++;
                    std::string text;

                    if (it->second.enabled)
                        text = StrFmt("Disable##%d", btnCount);
                    else
                        text = StrFmt("Enable##%d", btnCount);

                    if (IlionTheme::Button(text.c_str()))
                    {
                        LOG_DEBUG("HUDless {:X}: {}", (size_t) it->first,
                                  it->second.enabled ? "Disabling" : "Enabling");
                        it->second.enabled = !it->second.enabled;
                    }
                }

                ImGui::EndTable();
            }

            if (IlionTheme::Button(TrL("Clear##4")))
            {
                LOG_DEBUG("Clearing captured HUDless resources");
                state.clearCapturedHudlesses = true;
            }

            SameLineIfRoom(ButtonWidth(TrL("Close##4")), 8.0f);

            if (IlionTheme::Button(TrL("Close##4")))
                _showHudlessWindow = false;

            ImGui::End();
        }
    }
}

// BENCHMARK -----------------------------
// Manual test of what's in use right now. The user starts it, nothing gets switched by it.
void MenuCommon::RenderBenchmark(RenderMenuContext& ctx)
{
    auto& state = ctx.state;
    auto config = ctx.config;
    auto& currentFeature = ctx.currentFeature;
    auto& menuResScale = ctx.menuResScale;

    SectionTitle("Benchmark");
    ScopedID sectionId { "Benchmark" };

    const auto benchState = IlionBenchmark::GetState();
    const bool running = IlionBenchmark::Running();
    const bool busy = running || benchState == IlionBenchmark::State::Processing;

    // What would be tested, taken from what's really running
    const bool upscalerOn = IsUpscalerActive(currentFeature) && currentFeature->IsInited();
    const bool fsrObservable = upscalerOn && RunsSwitchedFfx(currentBackend);
    const auto active = FfxApiProxy::SrDiagGetActive();

    std::wstring activeDllPath;
    std::string activeFsrName;

    if (fsrObservable && active.module != nullptr)
        activeFsrName = FfxApiProxy::SrDiagDescribeModule(active.module, &activeDllPath);

    // What runs, from the same source as the selector and the overlay
    const std::string upscalerText = upscalerOn ? UpscalerLabel(currentFeature) : std::string(Tr("None"));

    // Technology, implementation, mode and state, recorded with every test. A technology is only recorded as such
    // once it is confirmed: the dll that executed the last upscale (FFX) or accepted evaluations (d4r).
    std::string technology = Tr("None"), provider, model, quality, implementation = "-";
    bool confirmed = false;
    const char* statusText = "No upscaler running";

    // Quality mode from the upscale ratio, as the presets define it
    if (upscalerOn && currentFeature->RenderWidth() > 0)
    {
        const double ratio = (double) currentFeature->TargetWidth() / (double) currentFeature->RenderWidth();
        const auto isRatio = [ratio](double value, double margin) { return std::abs(ratio - value) <= margin; };

        quality = isRatio(1.0, 0.02)     ? "Native AA"
                  : isRatio(1.3, 0.03)   ? "Ultra Quality"
                  : isRatio(1.5, 0.03)   ? "Quality"
                  : isRatio(1.724, 0.04) ? "Balanced"
                  : isRatio(2.0, 0.04)   ? "Performance"
                  : isRatio(3.0, 0.06)   ? "Ultra Performance"
                                      : StrFmt("Custom %.2fx", ratio);
    }

    if (upscalerOn)
    {
        provider = StrFmt("%u.%u.%u", currentFeature->Version().major, currentFeature->Version().minor,
                          currentFeature->Version().patch);

        if (fsrObservable)
        {
            // The dll that executed the last upscale, as the list knows it; the default dll is AMD's FSR
            const auto entry = IlionUpscalers::FindByPath(activeDllPath);
            technology = entry.has_value() ? FamilyName(entry->family) : "FSR";
            implementation = activeFsrName.empty() ? std::string("-") : std::string(Tr(activeFsrName.c_str()));

            if (const auto reported = FfxApiProxy::ActiveSrProvider(); !reported.empty())
                provider = reported;

            const auto diag = FfxApiProxy::SrDiagGetSnapshot();
            confirmed = active.module != nullptr && diag.statusLevel == 0;
            statusText = active.module == nullptr  ? "Waiting for the first upscale"
                         : diag.statusLevel == 1   ? "Switching..."
                         : diag.statusLevel != 0   ? "The DLL that executes is not the selected one"
                                                   : "Active, confirmed by its upscale calls";
        }
        else if (currentBackend == Upscaler::DLSS && state.dlssExternalBackend)
        {
            technology = "DLSS 4";
            implementation = "DLSS 4 (d4r)";

            for (const auto& entry : SharedSrVariants())
            {
                if (entry.kind == IlionUpscalers::Kind::D4r && StatusOf(&entry, currentFeature).loaded)
                    implementation = EntryName(entry);
            }

            // The preset d4r left in the NGX parameters of the last evaluation
            if (const char letter = IlionD4r::PresetLetter(IlionD4r::GetCalls().presetAfter.load()))
                model = std::string(1, letter);

            const auto& calls = IlionD4r::GetCalls();
            confirmed = calls.evaluateOk.load() > 0 && calls.lastResult.load() == 1;
            statusText = confirmed ? "Active, NGX accepted its evaluations" : "Loaded, no evaluation confirmed yet";
        }
        else
        {
            const auto type = currentFeature->GetUpscalerType();
            technology = type == Upscaler::FSR21 || type == Upscaler::FSR22 ? "FSR 2" : currentFeature->ShortName();
            implementation = upscalerText;

            // Its own count of evaluated frames
            confirmed = currentFeature->FrameCount() > 0;
            statusText = confirmed ? "Active, evaluating frames" : "Waiting for the first upscale";
        }
    }

    ImGui::TextWrapped("%s: %s", Tr("Technology"), technology.c_str());
    ImGui::TextWrapped("%s: %s%s%s%s%s", Tr("Implementation"), implementation.c_str(), provider.empty() ? "" : " | ",
                provider.c_str(), model.empty() ? "" : " | ", model.c_str());
    ImGui::TextWrapped("%s: %s", Tr("Upscaling mode"), quality.empty() ? "-" : Tr(quality.c_str()));
    TextWrappedColored(confirmed ? IlionTheme::LampGreen : IlionTheme::LampAmber, "%s: %s", Tr("Status"), Tr(statusText));

    ImGui::BeginDisabled(busy);

    // The selector of the Upscaling tab, the same list, states and switching
    static bool showVersionSelector = false;

    if (IlionTheme::Button(TrL("Change upscaler")))
    {
        showVersionSelector = !showVersionSelector;

        if (showVersionSelector)
            RescanSrVariants();
    }

    ShowTooltip("Opens ILION's upscaler selector here, without leaving Benchmark.\n"
                "It can't be changed while a test is running.");

    if (showVersionSelector)
    {
        ImGui::Indent();
        DrawUpscalerSelector(currentFeature, menuResScale, "benchmark");
        ImGui::Unindent();
    }

    // Duration
    static int benchSeconds = 60;

    ImGui::Spacing();
    ImGui::TextDisabled("%s", Tr("Duration"));
    ImGui::SameLine();

    for (const int seconds : { 30, 60, 120 })
    {
        if (IlionTheme::RadioButton(StrFmt("%d s", seconds).c_str(), benchSeconds == seconds))
            benchSeconds = seconds;

        ImGui::SameLine();
    }

    ImGui::NewLine();
    ImGui::EndDisabled();

    // Start | Stop | Cancel | Save
    ImGui::Spacing();
    ImGui::BeginDisabled(busy);

    if (IlionTheme::LampButton(TrL("Start"), IlionTheme::Lamp::Green))
    {
        IlionBenchmark::Conditions conditions;
        conditions.game = state.gameExe;
        conditions.upscaler = upscalerOn ? upscalerText : std::string("None");
        conditions.upscaleObservable = fsrObservable;
        conditions.frameGeneration = "Off";
        // Never recorded under a technology that wasn't confirmed as the one running
        conditions.technology = confirmed ? technology : technology + " (not confirmed)";
        conditions.provider = provider;
        conditions.model = model;
        conditions.quality = quality;
        conditions.fsrVersion = implementation;

        if (upscalerOn)
        {
            conditions.resolution = StrFmt("%ux%u -> %ux%u", currentFeature->RenderWidth(), currentFeature->RenderHeight(),
                                           currentFeature->TargetWidth(), currentFeature->TargetHeight());
        }

        if (const auto fg = state.currentFG; fg != nullptr && fg->IsActive() && !fg->IsPaused())
            conditions.frameGeneration = StrFmt("%s %ux", fg->Name(), fg->GetInterpolatedFrameCount() + 1);

        if (fsrObservable && active.module != nullptr)
        {
            conditions.dllPath = wstring_to_string(activeDllPath);
            conditions.dllSha256 = FSR4Versions::HashForPath(activeDllPath);

            // Hash is calculated in the background, asked for again when the result gets saved
            if (conditions.dllSha256.empty())
                FSR4Versions::RequestHash(activeDllPath);

            version_t dllVersion;

            if (Util::GetFileVersion(activeDllPath, &dllVersion))
            {
                conditions.dllFileVersion =
                    StrFmt("%u.%u.%u.%u", dllVersion.major, dllVersion.minor, dllVersion.patch, dllVersion.reserved);
            }
        }

        // DLSS: the feature library in use, with its version and hash like the FSR dlls
        else if (upscalerOn && currentBackend == Upscaler::DLSS && state.NVNGX_DLSS_Path.has_value())
        {
            const auto& dlssPath = *state.NVNGX_DLSS_Path;
            conditions.dllPath = wstring_to_string(dlssPath);
            conditions.dllSha256 = FSR4Versions::HashForPath(dlssPath);

            if (conditions.dllSha256.empty())
                FSR4Versions::RequestHash(dlssPath);

            version_t dllVersion;

            if (Util::GetFileVersion(dlssPath, &dllVersion))
            {
                conditions.dllFileVersion =
                    StrFmt("%u.%u.%u.%u", dllVersion.major, dllVersion.minor, dllVersion.patch, dllVersion.reserved);
            }
        }

        LOG_INFO("[BENCHMARK] start: {} s, upscaler: {}, fsr version: {}, dll: {}", benchSeconds, conditions.upscaler,
                 conditions.fsrVersion, conditions.dllPath);

        IlionBenchmark::Start(benchSeconds, ctx.now, conditions, fsrObservable ? active.module : nullptr);
    }

    ShowTooltip("Starts a test of what's in use right now. Recording begins after 2 seconds.\n"
                "The test keeps running with the menu closed, the performance overlay shows its progress.");

    ImGui::EndDisabled();
    IlionTheme::SameLineIfFits(TrL("Stop"));
    ImGui::BeginDisabled(!running);

    if (IlionTheme::LampButton(TrL("Stop"), IlionTheme::Lamp::Red))
        IlionBenchmark::Stop();

    ShowTooltip("Ends the test now and keeps what was recorded");

    IlionTheme::SameLineIfFits(TrL("Cancel"));

    if (IlionTheme::LampButton(TrL("Cancel"), IlionTheme::Lamp::Neutral))
        IlionBenchmark::Cancel();

    ShowTooltip("Ends the test and discards it");

    ImGui::EndDisabled();
    IlionTheme::SameLineIfFits(TrL("Save"));

    static std::string saveError;
    const bool canSave = benchState == IlionBenchmark::State::Finished && IlionBenchmark::GetResult().savedPath.empty();

    ImGui::BeginDisabled(!canSave);

    if (IlionTheme::LampButton(TrL("Save"), IlionTheme::Lamp::Amber))
    {
        // Hash of the dll might have finished after the test started
        if (IlionBenchmark::GetResult().conditions.dllSha256.empty() &&
            !IlionBenchmark::GetResult().conditions.dllPath.empty())
        {
            IlionBenchmark::SetDllSha256(
                FSR4Versions::HashForPath(string_to_wstring(IlionBenchmark::GetResult().conditions.dllPath)));
        }

        saveError.clear();

        const auto folder = Util::ExePath().parent_path() / L"ILION" / L"Benchmarks";

        if (IlionBenchmark::Save(folder, &saveError))
        {
            LOG_INFO("[BENCHMARK] saved: {}", IlionBenchmark::GetResult().savedPath);

            // The comparison window reads the saved tests again
            benchmarkCompareReload = true;
        }
        else
            LOG_ERROR("[BENCHMARK] save failed: {}", saveError);
    }

    ShowTooltip("Writes the result to ILION\\Benchmarks next to the game exe:\n"
                "one json per test, the frame times, and a line in benchmarks.csv.\n"
                "Existing files are never overwritten.");

    ImGui::EndDisabled();

    // Takes the last result off the screen. Saved tests and the upscaler stay as they are.
    IlionTheme::SameLineIfFits(TrL("Clear result"));
    ImGui::BeginDisabled(benchState != IlionBenchmark::State::Finished && benchState != IlionBenchmark::State::Aborted);

    if (IlionTheme::LampButton(TrL("Clear result"), IlionTheme::Lamp::Neutral))
    {
        IlionBenchmark::Dismiss();
        saveError.clear();
    }

    ShowTooltip("Removes the last result from this panel.\nSaved tests are not deleted.");

    ImGui::EndDisabled();

    // Saved tests side by side, in a window of its own
    IlionTheme::SameLineIfFits(TrL("Compare results"), 16.0f * menuResScale);

    if (IlionTheme::LampButton(TrL("Compare results"), IlionTheme::Lamp::Neutral))
    {
        showBenchmarkCompare = true;
        benchmarkCompareReload = true;
    }

    ShowTooltip("Opens the saved tests. Pick two or more to see them side by side,\n"
                "with their differences and an automatic conclusion.");

    // Benchmarks and captures of every dll in one report
    IlionTheme::SameLineIfFits(TrL("Final report"));

    if (IlionTheme::LampButton(TrL("Final report"), IlionTheme::Lamp::Neutral))
    {
        showFinalReport = true;
        finalReportReload = true;
    }

    ShowTooltip("Integrated final report: performance, stability and visual quality of every DLL\n"
                "that has saved tests, with one conclusion.");

    // Status
    ImGui::Spacing();

    if (benchState == IlionBenchmark::State::Stabilizing)
    {
        ImGui::Text("%s  %.1f s", Tr("STABILIZING"), IlionBenchmark::StabilizeLeftMs(ctx.now) / 1000.0);
    }
    else if (benchState == IlionBenchmark::State::Recording)
    {
        const float elapsed = (float) (IlionBenchmark::ElapsedMs() / 1000.0);

        ImGui::Text("%s  %.0f / %d s", Tr("RECORDING"), elapsed, IlionBenchmark::RequestedSeconds());
        ImGui::ProgressBar(elapsed / (float) IlionBenchmark::RequestedSeconds(), ImVec2(-1.0f, 0.0f), "");
    }
    else if (benchState == IlionBenchmark::State::Processing)
    {
        ImGui::TextDisabled("%s", Tr("Calculating..."));
    }
    else if (benchState == IlionBenchmark::State::Aborted)
    {
        ImGui::TextColored(toneMapColor(IlionTheme::Themed(ImVec4(1.f, 0.2f, 0.2f, 1.f), IlionTheme::Role::Bad)), "%s", Tr(IlionBenchmark::AbortReason()));
    }
    else if (benchState == IlionBenchmark::State::Idle)
    {
        ImGui::TextDisabled("%s", Tr("No test yet"));
    }

    // Result
    if (benchState == IlionBenchmark::State::Finished)
    {
        const auto& result = IlionBenchmark::GetResult();
        const std::string notAvailable = HwNotAvailable();

        IlionTheme::SectionPlate(Tr("Result"));

        if (ImGui::BeginTable("benchResult", 2, ImGuiTableFlags_SizingStretchSame))
        {
            const auto row = [&](const char* label, const std::string& value)
            {
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%s", Tr(label));
                ImGui::TableNextColumn();
                ImGui::TextWrapped("%s", value.c_str());
            };

            row("Upscaler", result.conditions.upscaler);

            if (!result.conditions.fsrVersion.empty())
                row("Implementation", result.conditions.fsrVersion);

            row("Test time", StrFmt("%.1f s (%llu %s)%s", result.durationMs / 1000.0, result.frames, Tr("frames"),
                                    result.stoppedEarly ? Tr(", stopped early") : ""));
            row("Average FPS", StrFmt("%.1f", result.avgFps));
            row("Median FPS", StrFmt("%.1f", result.medianFps));
            row("1% low", result.low1Valid ? StrFmt("%.1f", result.low1Fps) : notAvailable);
            row("0.1% low", result.low01Valid ? StrFmt("%.1f", result.low01Fps) : notAvailable);
            row("Frame time mean / median", StrFmt("%.2f / %.2f ms", result.meanMs, result.medianMs));
            row("Frame time P95 / P99", StrFmt("%.2f / %.2f ms", result.p95Ms, result.p99Ms));
            row("Frame time min / max", StrFmt("%.2f / %.2f ms", result.minMs, result.maxMs));
            row("Variability", StrFmt("%.2f ms (%.1f %%)", result.stdevMs, result.variationPercent));
            row("Stutter", StrFmt("%llu (%.1f / min, %.0f ms, max %.1f ms)", result.stutterCount,
                                  result.stutterPerMinute, result.stutterTotalMs, result.stutterLongestMs));

            if (result.conditions.upscaleObservable)
                row("Frames without upscaling", StrFmt("%llu", result.framesWithoutUpscale));

            const auto hardware = [&](const char* label, HwTelemetry::Metric metric, const char* format)
            {
                const auto& stat = result.hardware[metric];

                if (stat.Valid())
                    row(label, StrFmt(format, stat.Average(), stat.max) + "  [" + stat.source + "]");
            };

            hardware("GPU usage", HwTelemetry::GpuUsage, "%.0f / %.0f %%");
            hardware("GPU temperature", HwTelemetry::GpuTemp, "%.0f / %.0f \xC2\xB0""C");
            hardware("GPU hotspot", HwTelemetry::GpuHotspot, "%.0f / %.0f \xC2\xB0""C");
            hardware("VRAM used", HwTelemetry::VramUsed, "%.0f / %.0f MB");
            hardware("CPU usage", HwTelemetry::CpuUsage, "%.0f / %.0f %%");
            hardware("CPU temperature", HwTelemetry::CpuTemp, "%.0f / %.0f \xC2\xB0""C");

            ImGui::EndTable();
        }

        TextWrappedDisabled("%s", Tr("Hardware values: average / maximum during the test"));

        if (result.fgSeen)
        {
            ImGui::TextColored(toneMapColor(IlionTheme::Themed(ImVec4(1.f, 0.8f, 0.f, 1.f), IlionTheme::Role::Warn)), Tr("Frame generation was active (x%d):"),
                               result.fgMultiplier);
            ImGui::TextWrapped("%s", Tr("FPS values are presented frames and include generated ones."));
        }

        if (!result.savedPath.empty())
            ImGui::TextWrapped("%s %s", Tr("Saved:"), result.savedPath.c_str());
        else if (!saveError.empty())
            ImGui::TextColored(toneMapColor(IlionTheme::Themed(ImVec4(1.f, 0.2f, 0.2f, 1.f), IlionTheme::Role::Bad)), "%s", Tr(saveError.c_str()));
    }

    // Captures of the game's image for the visual comparison, one per FSR version of the same scene
    IlionTheme::SectionPlate(Tr("Visual analysis"));

    const auto captureState = IlionCapture::GetState();

    // Not during a test (it costs a frame) and not while the upscaler is being switched
    // Allowed without an upscaler too, a native image is what a reference is made of
    bool captureBlocked = busy || IlionCapture::Busy();
    const char* captureBlockedWhy = nullptr;

    if (fsrObservable)
    {
        const auto diag = FfxApiProxy::SrDiagGetSnapshot();

        if (diag.statusLevel != 0)
        {
            captureBlocked = true;
            captureBlockedWhy = "Wait until the selected FSR version is the one that's active";
        }
    }

    // A capture is tied to a test only this way: taken while that test, already saved, is still the result on
    // screen, and with the same dll, upscaler and resolutions running. Any other capture stays on its own.
    std::string captureTestId;

    if (benchState == IlionBenchmark::State::Finished)
    {
        const auto& shown = IlionBenchmark::GetResult();
        const std::string resolutionNow =
            upscalerOn ? StrFmt("%ux%u -> %ux%u", currentFeature->RenderWidth(), currentFeature->RenderHeight(),
                                currentFeature->TargetWidth(), currentFeature->TargetHeight())
                       : std::string();
        const std::string dllNow = fsrObservable && active.module != nullptr ? wstring_to_string(activeDllPath) : std::string();

        if (!shown.savedPath.empty() && shown.conditions.dllPath == dllNow && shown.conditions.resolution == resolutionNow &&
            shown.conditions.upscaler == (upscalerOn ? upscalerText : std::string("None")))
            captureTestId = std::filesystem::path(shown.savedPath).stem().string();
    }

    ImGui::BeginDisabled(captureBlocked);

    if (IlionTheme::LampButton(TrL("Capture image"), IlionTheme::Lamp::Green))
    {
        IlionCapture::Meta meta;
        meta.game = state.gameExe;
        // Not the translated text, the comparison reads it back
        meta.upscaler = upscalerOn ? upscalerText : std::string("None");
        meta.frameGeneration = "Off";

        if (upscalerOn)
        {
            meta.resolution = StrFmt("%ux%u -> %ux%u", currentFeature->RenderWidth(), currentFeature->RenderHeight(),
                                     currentFeature->TargetWidth(), currentFeature->TargetHeight());
        }

        if (const auto fg = state.currentFG; fg != nullptr && fg->IsActive() && !fg->IsPaused())
            meta.frameGeneration = StrFmt("%s %ux", fg->Name(), fg->GetInterpolatedFrameCount() + 1);

        if (fsrObservable && active.module != nullptr)
        {
            meta.fsrVersion = activeFsrName;
            meta.dllPath = wstring_to_string(activeDllPath);
            meta.dllSha256 = FSR4Versions::HashForPath(activeDllPath);

            version_t dllVersion;

            if (Util::GetFileVersion(activeDllPath, &dllVersion))
            {
                meta.dllFileVersion =
                    StrFmt("%u.%u.%u.%u", dllVersion.major, dllVersion.minor, dllVersion.patch, dllVersion.reserved);
            }
        }

        // Belongs to the saved test that is on screen, when there is one made with what is running now
        meta.benchmarkId = captureTestId;

        IlionCapture::Dismiss();

        if (IlionCapture::Request(meta, CaptureFolder()))
            LOG_INFO("[CAPTURE] requested, fsr version: {}, dll: {}", meta.fsrVersion, meta.dllPath);
    }

    ShowTooltip("Saves the game's image as it is presented, without the ILION menu, as a PNG in ILION\\Captures.\n"
                "Stand still in the same spot, capture, switch the FSR version and capture again.\n"
                "With frame generation on, the captured frame may be a generated one.");

    ImGui::EndDisabled();
    ImGui::SameLine();

    if (captureBlockedWhy != nullptr)
    {
        ImGui::TextDisabled("%s", Tr(captureBlockedWhy));
    }
    else if (captureState == IlionCapture::State::Requested || captureState == IlionCapture::State::WaitingGpu ||
             captureState == IlionCapture::State::Encoding)
    {
        ImGui::TextDisabled("%s", Tr("Capturing..."));
    }
    else if (captureState == IlionCapture::State::Done)
    {
        ImGui::TextWrapped("%s %s", Tr("Saved:"), IlionCapture::LastFile().c_str());
    }
    else if (captureState == IlionCapture::State::Failed)
    {
        ImGui::TextColored(toneMapColor(IlionTheme::Themed(ImVec4(1.f, 0.2f, 0.2f, 1.f), IlionTheme::Role::Bad)), "%s", Tr(IlionCapture::LastError().c_str()));
    }
    else
    {
        ImGui::TextDisabled("%s", Tr("No capture yet"));
    }

    if (!captureTestId.empty())
        TextWrappedDisabled(Tr("A capture made now is linked to the saved test %s."), captureTestId.c_str());
    else
        TextWrappedDisabled("%s", Tr("A capture made now is not linked to any test. To link it, save a test and capture "
                                     "before clearing its result."));

    // Saved captures side by side and measured, in a window of its own
    if (IlionTheme::LampButton(TrL("Compare captures"), IlionTheme::Lamp::Neutral))
    {
        showVisualCompare = true;
        visualCompareReload = true;
    }

    ShowTooltip("Opens the saved captures. Pick two or more to see them side by side,\n"
                "zoom into the same spot of each and measure them against a reference.");
    // Existing tools, the same state as their own controls
    IlionTheme::SectionPlate(Tr("Tools"));

    bool comparisonEnabled = FSR4Comparison::Enabled();

    if (IlionTheme::Checkbox(TrL("Upscaler comparison mode"), &comparisonEnabled))
    {
        FSR4Comparison::SetEnabled(comparisonEnabled);

        if (comparisonEnabled && !config->ShowFps.value_or_default())
            config->ShowFps.set_volatile_value(true);
    }

    SameLineIfRoom(CheckboxWidth(TrL("Performance overlay")), 16.0f * menuResScale);

    bool fpsEnabled = config->ShowFps.value_or_default();

    if (IlionTheme::Checkbox(TrL("Performance overlay"), &fpsEnabled))
        config->ShowFps = fpsEnabled;
}

// BASIC MODE -----------------------------
// One panel made of the section functions of the full menu, called with basicView set so they stop after
// their main controls. Nothing is stored here, both modes read and write the same config and state.
void MenuCommon::RenderBasicPanel(RenderMenuContext& ctx)
{
    auto& state = ctx.state;
    auto config = ctx.config;
    auto& currentFeature = ctx.currentFeature;
    auto& menuResScale = ctx.menuResScale;
    const auto& style = ImGui::GetStyle();

    const float bottomBarHeight = 1.0f + style.ItemSpacing.y * 2.0f + ImGui::GetFrameHeight();

    ImGui::BeginChild("##BasicPanel", ImVec2(0.0f, -bottomBarHeight));
    {
        ScopedCard card;

        basicView = true;
        ScopedCard::SetStrictFilter(true);

        // 1 - Upscaling: technology, model or version, FSR versions
        if (IsUpscalerActive(currentFeature))
        {
            RenderActiveUpscalerSettings(ctx);
        }
        else
        {
            SectionTitle("Upscalers");
            RenderUpscalerStateMessage(ctx);
        }

        // 2 - Frame generation: input and output, then the mode combo of the FG that's in use.
        // Every FG keeps its own checks, the settings function is asked for one card at a time.
        ImGui::Spacing();
        RenderFrameGenerationSelection(ctx);

        for (const char* id : { "fg_fsrfg", "fg_xefg", "fg_dlssg" })
        {
            ScopedCard::SetFilter(id);
            RenderFrameGenerationRuntimeSettings(ctx);
        }

        ScopedCard::SetFilter(nullptr);

        // 3 - Latency
        ImGui::Spacing();
#ifdef LOW_LATENCY_INPUTS
        RenderLowLatencySettings(ctx);
#else
        RenderFakenvapiSettings(ctx);
#endif

        ScopedCard::SetStrictFilter(false);
        basicView = false;

        // 4 - Current status, read only
        ImGui::Spacing();
        SectionTitle("Current Status");

        const auto statusRow = [&](bool on, const char* name, const std::string& value)
        {
            const float size = ImGui::GetTextLineHeight();
            const ImVec2 pos = ImGui::GetCursorScreenPos();
            const ImU32 color = on ? ImGui::ColorConvertFloat4ToU32(toneMapColor(IlionTheme::Themed(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), IlionTheme::Role::Good)))
                                   : ImGui::GetColorU32(ImGuiCol_TextDisabled);

            ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(pos.x + size * 0.5f, pos.y + size * 0.5f),
                                                        size * 0.28f, color);
            ImGui::Dummy(ImVec2(size, size));
            ImGui::SameLine(0.0f, 4.0f * menuResScale);
            ImGui::TextDisabled("%s", on ? "ON " : "OFF");
            ImGui::SameLine(0.0f, 10.0f * menuResScale);
            ImGui::Text("%s: %s", Tr(name), value.c_str());
        };

        const bool upscalerOn = IsUpscalerActive(currentFeature) && currentFeature->IsInited();

        statusRow(upscalerOn, "Upscaler",
                  upscalerOn ? UpscalerLabel(currentFeature)
                             : std::string(Tr("None")));

        // Dll that's really executing the upscaling, from the hot-swap diagnostics
        if (upscalerOn && RunsSwitchedFfx(currentBackend))
        {
            const auto diag = FfxApiProxy::SrDiagGetSnapshot();
            std::string value = Tr(diag.activeName.c_str());

            if (diag.statusLevel != 0)
                value += std::string("  (") + Tr(diag.status.c_str()) + ")";

            statusRow(diag.statusLevel == 0, "Implementation", value);
        }

        // The same sources as the sidebar of the full menu: OptiScaler's own frame generation when it is the
        // one running, otherwise the DLSSG of the game or what stands in for it, by its evaluations. What is
        // only selected or loaded doesn't count as on.
        const auto fg = state.currentFG;
        const bool ownFgOn = fg != nullptr && fg->IsActive() && !fg->IsPaused();
        const auto externalFg = GetExternalFgStatus(state.dlssgDetectedInterpolationCount, state.activeFgNvngx,
                                                    state.swapchainApi == DX12);
        const bool fgOn = ownFgOn || externalFg.kind == ExternalFgStatus::Kind::Active;

        std::string fgValue = Tr("Off");

        if (ownFgOn)
            fgValue = StrFmt("%s %ux", fg->Name(), fg->GetInterpolatedFrameCount() + 1);
        else if (externalFg.kind != ExternalFgStatus::Kind::Off)
            fgValue = externalFg.text;
        else if (const char* loaded = NvngxFgName(state.activeFgNvngx); loaded != nullptr)
            fgValue = StrFmt(Tr("Off (%s loaded, the game isn't generating frames)"), loaded);

        statusRow(fgOn, "Frame Generation", fgValue);

        const auto latencyMode = fakenvapi::getCurrentMode();
        const bool latencyOn = latencyMode != LowLatencyMode::None;

        statusRow(latencyOn, "Latency",
                  latencyOn ? std::string(magic_enum::enum_name(latencyMode)) : std::string(Tr("Off")));
    }
    ImGui::EndChild();

    ImGui::Separator();
    ImGui::Spacing();

    RenderMainMenuBottomBar(ctx);
}

void MenuCommon::RenderMainMenuWindow(RenderMenuContext& ctx)
{
    auto& state = ctx.state;
    auto config = ctx.config;
    auto& frameTime = ctx.frameTime;
    auto& frameRate = ctx.frameRate;
    auto& frameTimesCalculated = ctx.frameTimesCalculated;
    auto& menuResScale = ctx.menuResScale;
    auto& io = ctx.io;

    if (!_isVisible)
        return;

    // Check for GPU support once and reuse the result in all menu sections.
    // DXVK might call Vulkan device creation, which would destroy our objects.
    State::Instance().vulkanSkipHooks = true;
    ctx.primaryGpu =
        std::make_unique<std::decay_t<decltype(IdentifyGpu::getPrimaryGpu())>>(IdentifyGpu::getPrimaryGpu());
    State::Instance().vulkanSkipHooks = false;

    // Overlay font
    if (config->UseHQFont.value_or_default())
        ImGui::PushFontSize(std::round(menuResScale * fontSize));

    // If overlay is not visible frame needs to be inited
    if (!frameTimesCalculated)
    {
        float frameCnt = 0;
        frameTime = 0;
        for (size_t i = 299; i > 199; i--)
        {
            if (state.frameTimes[i] > 0.0)
            {
                frameTime += state.frameTimes[i];
                frameCnt++;
            }
        }

        frameTime /= frameCnt;
        frameRate = 1000.0 / frameTime;
    }

    ImGuiWindowFlags flags = 0;
    flags |= ImGuiWindowFlags_NoSavedSettings;
    flags |= ImGuiWindowFlags_NoCollapse;
    flags |= ImGuiWindowFlags_AlwaysAutoResize;

    if (lastMenuScale != menuResScale)
    {
        lastMenuScale = menuResScale;

        // if UI scale is changed rescale the style
        ImGuiStyle& style = ImGui::GetStyle();
        ImGuiStyle styleold = style; // Backup colors
        style = ImGuiStyle();        // IMPORTANT: ScaleAllSizes will change the original size,
                                     // so we should reset all style config

        ApplyThemeStyle();

        style.ScaleAllSizes(menuResScale);
        style.MouseCursorScale = 1.0f;
        CopyMemory(style.Colors, styleold.Colors, sizeof(style.Colors)); // Restore colors
    }

    // Fixed width, only the tab content scrolls. Keeps the menu from growing or shrinking when tabs change
    // Height can be changed by the user with the resize grip and is stored unscaled in the config
    ImGuiWindowFlags mainFlags = ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse |
                                 ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;

    // ILION: with the ReShade panel open the window grows by the panel to its right (Basic has no tabs, no panel)
    if (config->MenuBasicMode.value_or_default())
    {
        reshadePanelOpen = false;
        reshadePanelAmount = 0.0f;
    }

    AdvanceReShadePanel();
    const float panelExtra = std::floor((ReShadePanelWidth(menuResScale) + ImGui::GetStyle().ItemSpacing.x) * reshadePanelAmount);
    const float menuWidth = std::min(680.0f * menuResScale + panelExtra, io.DisplaySize.x - 20.0f);
    const float maxMenuHeight = io.DisplaySize.y - 20.0f;
    const float minMenuHeight = std::min(300.0f * menuResScale, maxMenuHeight);
    const float menuHeight =
        std::clamp(config->MenuHeight.value_or(815.0f) * menuResScale, minMenuHeight, maxMenuHeight);

    ImGui::SetNextWindowSize(ImVec2(menuWidth, menuHeight), ImGuiCond_Always);
    ImGui::SetNextWindowSizeConstraints(ImVec2(menuWidth, minMenuHeight), ImVec2(menuWidth, maxMenuHeight));

    // Main menu window
    if (windowTitle.empty())
    {
        // Name of this modified build, the OptiScaler build it is based on is shown in the Status tab
        windowTitle = std::string("OptiScaler - ILION v") + BuildInfo::IlionVersion();
    }

    // A chassis has round corners, a wider margin and a taller top bar. Only this window: the overlay and the
    // other windows keep theirs.
    // The font of the theme for the whole main window, title included
    ImFont* themeFont = IlionTheme::SkinFont();

    if (themeFont != nullptr)
        ImGui::PushFont(themeFont, -1.0f);

    if (IlionTheme::industrial)
        IlionTheme::PushChassisStyle();

    const bool mainWindowOpen = ImGui::Begin(windowTitle.c_str(), NULL, mainFlags);

    if (IlionTheme::industrial)
        IlionTheme::PopChassisStyle();

    if (mainWindowOpen)
    {
        // The window as a metal chassis: rim, corner plates and worn paint, behind everything it holds
        IlionTheme::DrawChassis();

        // Interface mode on the left, language on the right, small and above everything else
        RenderModeSelector(config);
        ImGui::SameLine();
        RenderLanguageSelector(config);

        // Update and config notices shown above the tabs.
        RenderMainMenuHeaderMessages(ctx);

        if (config->MenuBasicMode.value_or_default())
        {
            // Compact panel made of the same sections as the tabs below
            RenderBasicPanel(ctx);
        }
        else
        {
            // Essential: the same tabs as Advanced, under a strip of status screens
            if (config->MenuEssentialMode.value_or_default())
                RenderEssentialStrip(ctx);

            // Tabs on the left, tab content on the right.
            RenderMainMenuTabs(ctx);

            // Graphs and buttons, always visible regardless of the selected tab.
            RenderMainMenuFooter(ctx);
        }

        auto winSize = ImGui::GetWindowSize();
        auto winPos = ImGui::GetWindowPos();

        // Menu was resized by the user
        if (std::abs(winSize.y - menuHeight) > 0.5f)
            config->MenuHeight = std::round(winSize.y / menuResScale);

        if (lastPosition.x < -900.0f || (lastPosition.x >= winPos.x - 1.0f && lastPosition.y >= winPos.y - 1.0f &&
                                         lastPosition.x <= winPos.x + 1.0f && lastPosition.y <= winPos.y + 1.0f))
        {
            float posX;
            float posY;

            posX = ((float) io.DisplaySize.x - winSize.x) / 2.0f;
            posY = ((float) io.DisplaySize.y - winSize.y) / 2.0f;

            // don't position menu outside of screen
            if (posX < 0.0 || posY < 0.0)
            {
                posX = 50;
                posY = 50;
            }

            ImGui::SetWindowPos(ImVec2 { posX, posY });
            lastPosition.x = posX;
            lastPosition.y = posY;
        }
        else if (winPos.x + winSize.x > io.DisplaySize.x - 10.0f)
        {
            // ILION: moved by the user and grown (ReShade panel): kept inside the screen, growing to the left instead
            ImGui::SetWindowPos(ImVec2 { std::max(10.0f, io.DisplaySize.x - winSize.x - 10.0f), winPos.y });
        }

        // Blurred game image behind the window, the semi transparent window background tints it
        if (_bgBlurTexture != ImTextureID_Invalid && config->MenuBlur.value_or_default() &&
            ImGui::GetStyleColorVec4(ImGuiCol_WindowBg).w < 1.0f)
        {
            const ImVec2 blurMin = ImGui::GetWindowPos();
            const ImVec2 blurMax = ImVec2(blurMin.x + ImGui::GetWindowWidth(), blurMin.y + ImGui::GetWindowHeight());
            const ImVec2 uvMul = ImVec2(_bgBlurUVScale.x / io.DisplaySize.x, _bgBlurUVScale.y / io.DisplaySize.y);

            ImGui::GetBackgroundDrawList()->AddImageRounded(
                _bgBlurTexture, blurMin, blurMax, ImVec2(blurMin.x * uvMul.x, blurMin.y * uvMul.y),
                ImVec2(blurMax.x * uvMul.x, blurMax.y * uvMul.y), IM_COL32_WHITE, ImGui::GetStyle().WindowRounding);

            _bgBlurUsed = true;
        }

        ImGui::End();
    }

    if (themeFont != nullptr)
        ImGui::PopFont();

    // Detached utility windows owned by the main menu.
    RenderMipmapBiasWindow(ctx, flags);
    RenderHudlessResourcesWindow(ctx, flags);
    DrawBenchmarkCompareWindow(menuResScale, io);
    DrawVisualCompareWindow(menuResScale, io);
    DrawFinalReportWindow(menuResScale, io);

    if (config->UseHQFont.value_or_default())
        ImGui::PopFontSize();
}

void KeyUp(UINT vKey)
{
    inputMenu = vKey == Config::Instance()->ShortcutKey.value_or_default();
    inputFps = vKey == Config::Instance()->FpsShortcutKey.value_or_default();
    inputFG = vKey == Config::Instance()->FGShortcutKey.value_or_default();
    inputFpsCycle = vKey == Config::Instance()->FpsCycleShortcutKey.value_or_default();
}

bool MenuCommon::RenderMenu()
{
    if (!_isInited)
        return false;

    RenderMenuContext ctx { State::Instance(), Config::Instance(), ImGui::GetIO() };
    ctx.now = Util::MillisecondsNow();

    // Menu language follows the config, Spanish unless it says English
    Ilion::SetLang(Ilion::ParseLang(ctx.config->MenuLanguage.value_or_default()));

    UpdateHardwareTelemetry(ctx.config);
    ctx.currentFeature = ctx.state.currentFeature;

    // 1) Collect timing and input state before any ImGui drawing.
    UpdateRenderTiming(ctx);
    UpdateFsr4Comparison(ctx.state, ctx.frameTime, ctx.now);
    UpdateBenchmark(ctx.state, ctx.frameTime, ctx.now);
    UpdateMfgCost(ctx.state, ctx.now);
    UpdateMenuInputMode(ctx);
    HandleMenuShortcuts(ctx);

    // 2) Prepare one-shot notifications and start a new ImGui frame only when needed.
    UpdateVersionAndStartupNotifications(ctx);
    BeginMenuFrameIfNeeded(ctx);
    OptiInput::EndFrame(_isVisible);

    // 3) Draw lightweight overlay windows first, preserving the original order.
    ctx.menuResScale = MenuResolutionScale(ctx.io);
    RenderSplashWindow(ctx);
    RenderNotifications(ctx);
    UpdateFrameTimeAverages(ctx);
    RenderPerformanceOverlay(ctx);

    // 4) Draw the full settings menu last so popups and child windows keep their existing behavior.
    RenderMainMenuWindow(ctx);

    if (ctx.newFrame)
        ImGui::EndFrame();

    return ctx.newFrame;
}

void MenuCommon::Init(HWND InHwnd, bool isUWP)
{
    // Reset shutdown flag in case of re-init
    State::Instance().isShuttingDown = false;

    HWND oldHandle = nullptr;

    if (_handle != nullptr)
    {
        oldHandle = _handle;
        LOG_DEBUG("Old Handle: {:X}, ImGui Handle: {:X}", (size_t) oldHandle,
                  (size_t) ImGui::GetMainViewport()->PlatformHandleRaw);
    }

    _handle = InHwnd;
    _isVisible = false;
    _isUWP = isUWP;
    lastPosition = { -1000.0f, -1000.0f };

    LOG_DEBUG("Handle: {0:X}", (size_t) _handle);

    // In case d3d12 wasn't yet used up to this point, try to update GPU info late here
    IdentifyGpu::updateD3d12Capabilities();

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    ImGuiIO& io = ImGui::GetIO();
    (void) io;

    hasGamepad = (io.BackendFlags | ImGuiBackendFlags_HasGamepad) > 0;
    io.BackendFlags &= 30;
    io.ConfigFlags = ImGuiConfigFlags_NoMouse | ImGuiConfigFlags_NoMouseCursorChange | ImGuiConfigFlags_NoKeyboard;

    io.MouseDrawCursor = _isVisible;
    io.WantCaptureKeyboard = _isVisible;
    io.WantCaptureMouse = _isVisible;
    io.WantSetMousePos = _isVisible;

    io.IniFilename = io.LogFilename = nullptr;

    bool initResult = false;

    if (io.BackendPlatformUserData == nullptr)
    {
        if (!isUWP)
        {
            initResult = ImGui_ImplWin32_Init(InHwnd);
            LOG_DEBUG("ImGui_ImplWin32_Init result: {0}", initResult);
        }
        else
        {
            initResult = ImGui_ImplUwp_Init(InHwnd);
            ImGui_BindUwpKeyUp(KeyUp);
            LOG_DEBUG("ImGui_ImplUwp_Init result: {0}", initResult);
        }
    }

    if (io.Fonts->Fonts.empty() && Config::Instance()->UseHQFont.value_or_default())
    {
        ImFontAtlas* atlas = io.Fonts;
        atlas->Clear();

        // This automatically becomes the next default font
        ImFontConfig fontConfig;

        if (Config::Instance()->FontSize.has_value())
            fontSize = Config::Instance()->FontSize.value();

        if (Config::Instance()->TTFFontPath.has_value())
        {
            io.FontDefault =
                atlas->AddFontFromFileTTF(wstring_to_string(Config::Instance()->TTFFontPath.value()).c_str(), fontSize,
                                          &fontConfig, io.Fonts->GetGlyphRangesDefault());
        }
        else
        {
            io.FontDefault = atlas->AddFontFromMemoryCompressedBase85TTF(hack_compressed_compressed_data_base85,
                                                                         fontSize, &fontConfig);
        }

        // Fonts of the Fallout and Light themes, from the fonts Windows already has: nothing is shipped with ILION.
        // A terminal face for Fallout, a plain sans-serif for Light. Without one the theme keeps the menu's font.
        const auto systemFont = [&](std::initializer_list<const wchar_t*> names) -> ImFont*
        {
            wchar_t windows[MAX_PATH] = {};

            if (GetWindowsDirectoryW(windows, MAX_PATH) == 0)
                return nullptr;

            for (const wchar_t* name : names)
            {
                const auto path = std::filesystem::path(windows) / L"Fonts" / name;
                std::error_code ec;

                if (std::filesystem::exists(path, ec))
                {
                    ImFontConfig themeConfig;
                    return atlas->AddFontFromFileTTF(wstring_to_string(path.wstring()).c_str(), fontSize, &themeConfig);
                }
            }

            return nullptr;
        };

        IlionTheme::terminalFont = systemFont({ L"lucon.ttf", L"consola.ttf" });
        IlionTheme::modernFont = systemFont({ L"bahnschrift.ttf", L"segoeui.ttf", L"arial.ttf" });
        // Fallout 2: its own two fonts (SIL OFL, in the dll), Consolas merged into each for the signs they lack
        const auto withFallback = [&](ImFont* font) -> ImFont*
        {
            wchar_t windows[MAX_PATH] = {};

            if (font != nullptr && GetWindowsDirectoryW(windows, MAX_PATH) != 0)
            {
                const auto path = std::filesystem::path(windows) / L"Fonts" / L"consola.ttf";
                std::error_code ec;

                if (std::filesystem::exists(path, ec))
                {
                    ImFontConfig merge;
                    merge.MergeMode = true;
                    atlas->AddFontFromFileTTF(wstring_to_string(path.wstring()).c_str(), fontSize, &merge);
                }
            }

            return font;
        };

        {
            ImFontConfig textConfig;
            textConfig.RasterizerMultiply = 1.25f; // a thin face, a little brighter reads better on the glass
            IlionTheme::crtFont = withFallback(
                atlas->AddFontFromMemoryCompressedBase85TTF(sharetechmono_compressed_compressed_data_base85, fontSize, &textConfig));
        }
        {
            ImFontConfig titleConfig;
            IlionTheme::crtTitleFont = withFallback(
                atlas->AddFontFromMemoryCompressedBase85TTF(russoone_compressed_compressed_data_base85, fontSize, &titleConfig));
        }

        // Without them the system faces, as before
        if (IlionTheme::crtFont == nullptr)
            IlionTheme::crtFont = systemFont({ L"consola.ttf", L"lucon.ttf" });
    }

    if (!Config::Instance()->OverlayMenu.value_or_default())
    {
        _hdrTonemapApplied = false;
    }

    DWORD hwndPid = 0;
    DWORD hwndTid = GetWindowThreadProcessId(_handle, &hwndPid);

    LOG_DEBUG("HWND: {:X}, IsWindow: {}, HWND PID: {}, Current PID: {}, HWND TID: {}, Current TID: {}",
              (ULONG64) _handle, IsWindow(_handle), hwndPid, GetCurrentProcessId(), hwndTid, GetCurrentThreadId());

    OptiInput::Initialize(_handle, isUWP);

    ApplyThemeStyle();
    _isInited = true;
}

void MenuCommon::Shutdown()
{
    if (!MenuCommon::_isInited)
        return;

    // if (_oWndProc != nullptr)
    //{
    //     auto handle = (HWND) ImGui::GetMainViewport()->PlatformHandleRaw;
    //     SetLastError(0);
    //     auto restoreResult = SetWindowLongPtr(handle, GWLP_WNDPROC, (LONG_PTR) _oWndProc);
    //     auto error = GetLastError();

    //    if (restoreResult == 0 && error != 0)
    //    {
    //        LOG_ERROR("Failed to restore old WndProc. Error: {:X}", error);
    //    }

    //    _oWndProc = nullptr;
    //}

    if (!_isUWP)
        ImGui_ImplWin32_Shutdown();
    else
        ImGui_ImplUwp_Shutdown();

    ImGui::DestroyContext();

    _handle = nullptr;
    _isInited = false;
    _isVisible = false;
}

void MenuCommon::HideMenu()
{
    if (!_isVisible)
        return;

    _isVisible = false;

    ImGuiIO& io = ImGui::GetIO();
    (void) io;

    _showMipmapCalcWindow = false;
    _showHudlessWindow = false;

    io.MouseDrawCursor = _isVisible;
    io.WantCaptureKeyboard = _isVisible;
    io.WantCaptureMouse = _isVisible;
}
