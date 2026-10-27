// Copyright (c) 2026 3zwr1 (AMDNR). Part of AMDNR (GPL-3.0; see Licenses/AMDNR_NOTICE.txt).
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// What each NR runtime supports, one table read by the Neural tab and the runtime chooser (plan item T1,
// the menu-cleanup design note section 3). Header-only; menu thread only.
//
// Menu() is the runtime the tab's controls talk to: the one the process built its backend for, else
// (nothing built yet) the one the bridge will build (detail::MenuIsLmxxfNow; 0.3.3.2's MenuRuntimeIsLmxxf()
// in dlssnr/DlssNr_Menu.cpp asked only "lmxxf chosen and complete", G1 made it follow the bridge). It is NOT the
// "Neural runtime" combo, which after a change in this session names the next start's runtime. The rule is evaluated
// once per ImGui frame and kept for that frame (it checks files while nothing is built), so Get()/Tag() and the
// NeuralUi helpers can ask it per row.
//
// The cells started as the 0.3.3.2 state (plan section 3 "initial support matrix", plus Still-surface steadiness,
// which the plan never saw). A port that lands turns its cell from Planned into Yes here (W3-D reconciles; G2 flipped
// the landed ports 7.2, 7.5, 7.6, 7.7, 7.8 and the 7.9 readout); the menu code reads Get()/Tag() and needs no change
// for it. (0.3.4 menu rework) lmxxf's native character mask has its host step (HX, 7ee9214): Structure, Character
// structure and the AutoMask row are live on lmxxf (a change rebuilds the network, about a second; an older
// LmxxfNrRuntime.dll refuses them, Lmxxf::ControlsRefused()); Tone stays Planned (lmxxf's tone control stays 1 in
// 0.3.4). (0.3.4 MENU match1) danielblnc's own knobs (RuntimeKnobs: Network style, Tone curve, Black lift) are No on
// lmxxf, so its Model strength tree hides and counts them (decision O5) instead of greying them "not in lmxxf yet".
// (0.3.4 G3) danielblnc's highlight colour guard (DANIEL-GUARD) is live: Exposure and highlights' one row writes
// [DlssNr] AmdDanielHighlightGuard there.
// (0.3.4 final menu batch, 2026-09-26) Reconciled with WF landed.md once more: no cell changed. The cells still
// Planned are Tone on lmxxf (its tone control stays 1 in 0.3.4), Edit detail / colour / Edge guard (EditShaper) and
// Output smoothing on danielblnc (ports 7.1 / 7.4 deferred, plan D7); the controls landed since G3 (the
// late-submission grace, the tier cap, the RR-7000 offer) have no Neural-tab row with a cell. No cell belongs to a Ray
// Regeneration row, and none depends on the GPU generation (owner decision 7: the Ray Regeneration section and the RR
// debug view follow RR dispatches only, so RX 7000 gets them whenever RR runs).
// No runtime version appears anywhere in this table (plan N8): the build name of an installed danielblnc runtime comes
// from AmdBridge::RuntimeName() (buildName). Owner decision 2 puts the versions read from the player's own files in the
// Neural runtime combo (NeuralTop.cpp: this buildName for danielblnc, the runtime DLL's version for lmxxf); this table
// still holds no version string.
#include "AmdBridge.h"
#include "../lmxxf/LmxxfBackend.h" // Lmxxf::ImportPoolState (vramPerNewSize)

#include <Config.h>
#include <State.h> // detail::MenuIsLmxxfNow: the Vulkan rule

#include <imgui/imgui.h> // Menu(): the ImGui frame count keys its per-frame answer

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace DlssNr::RuntimeCaps
{
enum class Support : uint8_t
{
    Yes,     // the runtime has it: the row is live
    Always,  // always on inside the runtime: drawn ticked and locked, with Tag()
    Planned, // a port is planned: greyed with Tag()
    No,      // the runtime cannot have it: hidden (counted) while kHideUnsupported, else greyed with Tag();
             // except the caps NeverHidden() lists, which stay on screen
};

enum class Cap : uint8_t
{
    Tone, Structure, CharacterStructure,              // danielblnc runtime inputs
    EditShaper,                                       // Edit detail, Edit colour, Edge guard
    HighlightColourGuard, OutputSmoothing, StabilityMode,
    NetworkHistoryWithoutInterleave, ResidualTemporalSwitch, ResidualEdgeFade,
    ResidualLimitAt100, FullNetwork, AutoExposureSwitch, AutoExposureCap,
    EveryFrame, NrSlots, Encoding, HighlightProxy, NetworkOutput, Rtgi,
    PacingReadout, DebugViewWithoutInterleave, AllPassesOnVulkan, GhostReadout,
    StillSurfaceSteadiness,                           // [DlssNr] AmdStabilityStaticRelax (0.3.3.2), both runtimes
    // (0.3.4 menu rework) The W3-D rows' cells, so their rows gate like the others:
    RuntimeKnobs,        // [DlssNr] AmdRuntimeStyle / AmdToneCurve / AmdToneLift (danielblnc's own knobs, auto = -1)
    UseGameExposure,     // [DlssNr] AmdUseGameExposure (EXPO-PARITY: both halves landed)
    NativeCharacterMask, // [DlssNr] AutoMask (AM-DAN on danielblnc; lmxxf: the AUTOMASK host step, HX)
    EditShaperAB,        // Diagnostics A/B of [DlssNr] AmdEditShaper / Limit / Scope / CarryCap (danielblnc only)
    NrCostReadout,       // AmdPreSr::Stats::nrGpuMs (P7.10, both halves; -1 = hide)
    // (0.3.4, danielblnc support) [DlssNr] AmdDanielFastMode: danielblnc's quality mode (Fast / Reference). Yes on danielblnc,
    // but only a build that has the mode shows the row (AmdBridge::DanielFastModeSupported); No on lmxxf.
    DanielFastMode,
    // (0.3.4, lmxxf Fast mode) [DlssNr] AmdLmxxfFastMode: the network one size tier lower (host only). Yes on lmxxf, No
    // on danielblnc; the row sits where danielblnc's Fast mode sits, and neither is counted as hidden.
    LmxxfFastMode,
    Count
};
inline constexpr size_t kCapCount = static_cast<size_t>(Cap::Count);

// D3 (plan 10.3): options a runtime cannot have ("No") are hidden with a count line. Revert: false = show them
// greyed with their tag.
inline constexpr bool kHideUnsupported = true;

// The exceptions to D3 (plan section 3 and B2/B3/C2): "No" cells whose row stays on screen, so NeuralUi::Hidden()
// never hides them. Full network is greyed on danielblnc (B3: the README points players at it). Residual limit at
// 100% and Debug view without interleave are conditional greys; Neural passes stays live on a danielblnc Vulkan
// game with the tag "1 pass in Vulkan games here" (B2). Those three rows gate on their condition
// (NeuralUi::Gate(off, tag)), not on the cap, which would grey them in every case.
inline constexpr Cap kNeverHidden[] = {
    Cap::ResidualLimitAt100,
    Cap::FullNetwork,
    Cap::DebugViewWithoutInterleave,
    Cap::AllPassesOnVulkan,
};
constexpr bool NeverHidden(Cap cap)
{
    for (Cap c : kNeverHidden)
        if (c == cap)
            return true;
    return false;
}

// One item of the merged Interleave preset combo (plan B5a): the label, the [DlssNr] AmdInterleavePreset value
// it writes, and its one-line item tooltip.
struct FillItem
{
    const char* label;
    int value;
    const char* help;
};

struct RuntimeInfo
{
    AmdBridge::NeuralRuntime id;
    const char* iniValue; // [DlssNr] NrBackend: "daniel" / "lmxxf" (unchanged)
    const char* name;     // "danielblnc" / "lmxxf"
    const char* credit;   // the credit line (plan A3)
    const char* url;      // the author's page
    std::array<Support, kCapCount> caps;
    CustomOptional<bool> Config::*historyKey; // Network history: AmdInterleaveModelHistory / AmdLmxxfHistory
    std::span<const FillItem> fills;          // Interleave preset items (plan B5a), default item first
    // One footnote line each (plan R4), for the tooltips of the rows they name
    const char* noteNrResolution;
    const char* noteResidual;
    const char* noteTemporalStability;
    const char* noteComposition;
    const char* (*vramPerNewSize)(); // what a NEW NR size keeps until restart ("about 75-350 MB per pass")
    unsigned long long slowAbovePixels; // input pixels at 100% NR resolution above which it is slow; 0 = none
    bool (*installed)();                // the runtime's files are all there
    bool (*gpuOk)(const AmdBridge::GpuSupport&);
    std::string (*missing)();  // what is missing, for the attention slot; empty when installed
    const char* (*buildName)(); // the installed build's name (AmdBridge::RuntimeName), nullptr for none
    // Tag() texts (plan R2: at most about 32 characters)
    const char* tagPlanned; // "not in lmxxf yet"
    const char* tagAlways;  // "always on in lmxxf"
    const char* tagNo;      // "not in lmxxf"
};

namespace detail
{
// Both columns side by side, in Cap order (checked below).
struct Cell
{
    Cap cap;
    Support daniel;
    Support lmxxf;
};
inline constexpr Cell kMatrix[] = {
    // cap                                  danielblnc          lmxxf
    { Cap::Tone,                            Support::Yes,       Support::Planned }, // AmdBridge BuildSettings; lmxxf: its tone control stays 1
    { Cap::Structure,                       Support::Yes,       Support::Yes },     // lmxxf: AUTOMASK host step (HX, 7ee9214)
    { Cap::CharacterStructure,              Support::Yes,       Support::Yes },     // lmxxf: AUTOMASK host step (HX, 7ee9214)
    { Cap::EditShaper,                      Support::Planned,   Support::Yes },     // port 7.1; LmxxfBackend residual shader
    { Cap::HighlightColourGuard,            Support::Yes,       Support::Yes },     // port 7.3; danielblnc: DANIEL-GUARD
        // (bd0deb7, [DlssNr] AmdDanielHighlightGuard, default off); NeuralLook.cpp's one row writes the active
        // runtime's key (G3 flip; revert: danielblnc back to Planned greys the row 'not in danielblnc yet')
    { Cap::OutputSmoothing,                 Support::Planned,   Support::Yes },     // port 7.4; lmxxf needs Network history
    { Cap::StabilityMode,                   Support::Yes,       Support::No },      // lmxxf has no picture-space pass
    { Cap::NetworkHistoryWithoutInterleave, Support::Yes,       Support::Yes },     // port 7.2 (landed 0.3.4)
    { Cap::ResidualTemporalSwitch,          Support::Yes,       Support::Always },  // the lmxxf carry is residual temporal
    { Cap::ResidualEdgeFade,                Support::Yes,       Support::Yes },     // port 7.8 (0.3.4): while the edit is lifted
    { Cap::ResidualLimitAt100,              Support::No,        Support::Yes },     // conditional grey on danielblnc (NeverHidden)
    { Cap::FullNetwork,                     Support::No,        Support::Yes },     // shown greyed on danielblnc (plan B3, NeverHidden)
    { Cap::AutoExposureSwitch,              Support::Always,    Support::Yes },     // inside the closed runtime
    { Cap::AutoExposureCap,                 Support::No,        Support::Yes },
    { Cap::EveryFrame,                      Support::Yes,       Support::No },      // lmxxf never waits
    { Cap::NrSlots,                         Support::Yes,       Support::No },      // lmxxf: one job in flight
    { Cap::Encoding,                        Support::Yes,       Support::Yes },     // port 7.5 (0.3.4): upscaler path only
    { Cap::HighlightProxy,                  Support::Yes,       Support::No },
    { Cap::NetworkOutput,                   Support::Yes,       Support::Yes },     // port 7.6 (0.3.4)
    { Cap::Rtgi,                            Support::Yes,       Support::Yes },     // port 7.7 (0.3.4); the inherited
        // screen-space GI (label "Screen-space GI (experimental)", owner decision 6): both backends load its
        // experimental_lighting shaders (OptiScaler-AMD-PreSR lineage, not AMDNR's own) from beside OptiScaler.dll
    { Cap::PacingReadout,                   Support::Yes,       Support::Yes },     // port 7.9 readout (FB-L5, 0.3.4)
    { Cap::DebugViewWithoutInterleave,      Support::No,        Support::Yes },     // conditional grey on danielblnc (NeverHidden)
    { Cap::AllPassesOnVulkan,               Support::No,        Support::Yes },     // danielblnc: 1 pass on Vulkan (NeverHidden)
    { Cap::GhostReadout,                    Support::Yes,       Support::No },      // the readout self-hides
    { Cap::StillSurfaceSteadiness,          Support::Yes,       Support::Yes },     // with conditional greys (M: Quality)
    { Cap::RuntimeKnobs,                    Support::Yes,       Support::No },      // DANIEL-033-SET; a danielblnc build
                                                                                    // without the knobs greys the rows;
                                                                                    // lmxxf: hidden and counted (O5)
    { Cap::UseGameExposure,                 Support::Yes,       Support::Yes },     // EXPO-PARITY (W1-C + W3-A)
    { Cap::NativeCharacterMask,             Support::Yes,       Support::Yes },     // lmxxf: AUTOMASK host step (HX, 7ee9214)
    { Cap::EditShaperAB,                    Support::Yes,       Support::No },      // lmxxf never reads the shaper keys
    { Cap::NrCostReadout,                   Support::Yes,       Support::Yes },     // P7.10 (-1 = the row hides itself)
    { Cap::DanielFastMode,                  Support::Yes,       Support::No },      // danielblnc support: the row shows only for
                                                                                    // a danielblnc build with the mode
    { Cap::LmxxfFastMode,                   Support::No,        Support::Yes },     // lmxxf Fast mode (host only, deccc7e)
};
static_assert(std::size(kMatrix) == kCapCount, "RuntimeCaps: one matrix row per Cap");
constexpr bool MatrixInOrder()
{
    for (size_t i = 0; i < std::size(kMatrix); ++i)
        if (static_cast<size_t>(kMatrix[i].cap) != i)
            return false;
    return true;
}
static_assert(MatrixInOrder(), "RuntimeCaps: matrix rows must follow the Cap order");
constexpr std::array<Support, kCapCount> Column(bool lmxxf)
{
    std::array<Support, kCapCount> c {};
    for (size_t i = 0; i < kCapCount; ++i)
        c[i] = lmxxf ? kMatrix[i].lmxxf : kMatrix[i].daniel;
    return c;
}

// Interleave preset items (plan B5a). Edit accumulation (10) is first on both, so the default sits in the same
// place whichever runtime runs (R10).
inline constexpr FillItem kDanielFills[] = {
    { "Edit accumulation (default)", 10,
      "Each frame is this frame plus the model's last correction, checked against the picture; nothing old is shown." },
    { "Standard", 1, "Carries the last result along the motion vectors." },
    { "Extra temporal", 2, "Standard with more history." },
    { "Guided fill", 5, "Fills from history guided by the new frame." },
    { "Guided fill v2", 6, "Guided fill, steadier in motion." },
    { "Self-tuning (auto)", 8, "Picks the fill per frame by its measured error." },
};
// Classic carry is stored as 1 (D2, plan 10.2; 0.3.3.2 wrote 6). The value written is the menu's
// kLmxxfClassicCarryValue (NeuralPerfQuality.cpp), which is D2's revert switch; the table only lists the item.
// lmxxf reads every stored value below 9 as Classic carry, so an old 6 still reads Classic carry.
inline constexpr FillItem kLmxxfFills[] = {
    { "Edit accumulation (default)", 10,
      "Each frame is this frame plus the model's last correction, checked against the picture; nothing old is shown." },
    { "Classic carry", 1, "lmxxf's original carry, geometry checks only." },
};

inline const char* DanielVram()
{
    return "about 75-350 MB per pass";
}
inline const char* LmxxfVram()
{
    // R3: 0 = an older LmxxfNrRuntime.dll (or DLSS5_IMPORT_POOL=0) that re-imports on every rebuild
    return Lmxxf::ImportPoolState() == 0 ? "about 100 MB" : "about 10-25 MB";
}
inline bool DanielGpuOk(const AmdBridge::GpuSupport& g)
{
    return g.danielOk;
}
inline bool LmxxfGpuOk(const AmdBridge::GpuSupport& g)
{
    return g.lmxxfOk;
}
inline std::string DanielMissing()
{
    if (!AmdBridge::HasFiles())
        return "dlssnr_amd_pass1.dll missing";
    if (!AmdBridge::DanielWeightsPresent())
        return "dlssnr_on_amd_weights.bin missing";
    return {};
}
inline std::string LmxxfMissing()
{
    const bool dll = AmdBridge::LmxxfRuntimePresent();
    const bool assets = AmdBridge::LmxxfAssetsPresent();
    if (!dll && !assets)
        return "LmxxfNrRuntime.dll and LmxxfNrRuntime.pak missing";
    if (!dll)
        return "LmxxfNrRuntime.dll missing";
    if (!assets)
        return "LmxxfNrRuntime.pak (or DLSS5-AMD\\native-game-tiled-assets) missing";
    return {};
}
inline const char* NoBuildName()
{
    return nullptr;
}
} // namespace detail

// Both runtimes, in the order the combo and the chooser list them.
inline std::span<const RuntimeInfo> All()
{
    static const RuntimeInfo rows[] = {
        {
            AmdBridge::NeuralRuntime::Daniel,
            "daniel",
            "danielblnc",
            "DLSS-NR on AMD by Daniel Blanco (danielblnc)",
            "https://github.com/danielblnc/DLSS-NR-on-AMD",
            detail::Column(false),
            &Config::AmdInterleaveModelHistory,
            std::span<const FillItem>(detail::kDanielFills),
            "danielblnc: at exactly 100% edge fade is off and Residual limit only caps the carried edit. At 1440p"
            " and above, 100% is slow; try 70-85%.",
            "danielblnc: at 100% NR resolution it blends the whole result.",
            "danielblnc: smooths the picture, or damps the edit under Edit accumulation.",
            "danielblnc: the runtime composes its answer itself; only the tail runs here, in place on that answer.",
            &detail::DanielVram,
            2560ull * 1440ull, // M:1065-1076: about 45 ms per frame at 1440p input on an RX 9070 XT
            &AmdBridge::HasFiles,
            &detail::DanielGpuOk,
            &detail::DanielMissing,
            &AmdBridge::RuntimeName,
            "not in danielblnc yet",
            "always on in danielblnc",
            "not in danielblnc",
        },
        {
            AmdBridge::NeuralRuntime::Lmxxf,
            "lmxxf",
            "lmxxf",
            "lmxxf runtime by Kien (MIT)",
            "https://github.com/lmxxf/dlss5-on-amd-9070xt-porting",
            detail::Column(true),
            &Config::AmdLmxxfHistory,
            std::span<const FillItem>(detail::kLmxxfFills),
            "lmxxf: 100% feeds the network the frame pixel-exact (sharpest); above 100% it feeds an upscaled copy,"
            " up to 1920x1080.",
            "lmxxf: scales the model's edit before it is carried.",
            "lmxxf: how much of the carried edit is kept each frame.",
            "lmxxf: the runtime's two-branch answer is taken as it is; the tail runs here and the difference is"
            " lifted to the frame.",
            &detail::LmxxfVram,
            0ull,
            &AmdBridge::LmxxfReady,
            &detail::LmxxfGpuOk,
            &detail::LmxxfMissing,
            &detail::NoBuildName,
            "not in lmxxf yet",
            "always on in lmxxf",
            "not in lmxxf",
        },
    };
    return rows;
}

inline const RuntimeInfo& Row(AmdBridge::NeuralRuntime id)
{
    const auto all = All();
    return id == AmdBridge::NeuralRuntime::Lmxxf ? all[1] : all[0];
}

namespace detail
{
// Built already: that one. Not yet: what the bridge will build (AmdBridge LmxxfWanted and the Vulkan
// lmxxf_vk_launch.pending rule in Run): lmxxf when it is complete and chosen, or complete while danielblnc's
// files are missing, and not on a Vulkan title whose last lmxxf session stopped before its first answer;
// otherwise danielblnc. (G1 decision, a bug fix: 0.3.3.2's MenuRuntimeIsLmxxf asked only "chosen and
// complete", so the lmxxf-only package with NrBackend unset showed danielblnc's rows until the first upscaler
// frame.) HipRuntimeVersion() is not asked: its first call loads HIP and runs hipInit on this thread. While
// nothing is built, LmxxfReady() and HasFiles() check files on every call, so only Menu() and the NR toggle
// notice (menu_common.cpp, on the key press) call this.
inline bool MenuIsLmxxfNow()
{
    const auto active = AmdBridge::ActiveRuntime();
    if (active != AmdBridge::NeuralRuntime::Unchosen)
        return active == AmdBridge::NeuralRuntime::Lmxxf;
    return AmdBridge::LmxxfReady() &&
           (AmdBridge::ChosenRuntime() == AmdBridge::NeuralRuntime::Lmxxf || !AmdBridge::HasFiles()) &&
           !(State::Instance().api == API::Vulkan && AmdBridge::LmxxfVkLaunchPending());
}
} // namespace detail

// The runtime the tab's controls talk to (detail::MenuIsLmxxfNow). (G1 review) Evaluated once per ImGui frame
// and kept for the rest of that frame, so every row of one frame sees the same answer and the file checks run
// once per frame however many rows ask (0.3.3.2 asked once or twice per frame). With no ImGui context (never
// in the menu) it is evaluated on every call.
inline const RuntimeInfo& Menu()
{
    static std::atomic<long long> cached { -1 }; // (ImGui frame << 1) | lmxxf; -1 = not asked yet
    bool lmxxf;
    if (ImGui::GetCurrentContext() == nullptr)
    {
        lmxxf = detail::MenuIsLmxxfNow();
    }
    else
    {
        const long long frame = ImGui::GetFrameCount();
        long long c = cached.load(std::memory_order_relaxed);
        if (c < 0 || (c >> 1) != frame)
        {
            c = (frame << 1) | (detail::MenuIsLmxxfNow() ? 1 : 0);
            cached.store(c, std::memory_order_relaxed);
        }
        lmxxf = (c & 1) != 0;
    }
    return Row(lmxxf ? AmdBridge::NeuralRuntime::Lmxxf : AmdBridge::NeuralRuntime::Daniel);
}

inline Support Get(Cap cap)
{
    return Menu().caps[static_cast<size_t>(cap)];
}

// The dim same-line tag of a greyed row: "not in lmxxf yet" / "always on in danielblnc" / "not in lmxxf"; "" for Yes.
inline const char* Tag(Cap cap)
{
    const RuntimeInfo& r = Menu();
    switch (r.caps[static_cast<size_t>(cap)])
    {
    case Support::Always:
        return r.tagAlways;
    case Support::Planned:
        return r.tagPlanned;
    case Support::No:
        return r.tagNo;
    default:
        return "";
    }
}
} // namespace DlssNr::RuntimeCaps
