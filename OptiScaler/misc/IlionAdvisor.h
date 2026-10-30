#pragma once

// ILION dev.65: ILION Advisor. A first configuration suggested from what ILION already knows: the GPU, the screen,
// the game, the technologies usable here and what was measured in this session. Pure logic, no ImGui, no files, no
// side effects: the menu fills Facts and draws Advice. Nothing is applied. No benchmark is needed and no FPS are
// predicted.
//
// Two levels on every answer, never mixed:
//   compatibility  can it run here: decided by the code that already decides it (the upscaler catalogue, the FG
//                  options, the NR bridge, the intelligent activation). The Advisor never checks it again.
//   recommendation is it the advisable choice: Verified only when a measurement of this session settles it, Estimate
//                  when it comes from technical rules, Insufficient when there isn't enough to say.

#include <algorithm>
#include <cmath>
#include <vector>

namespace IlionAdvisor
{
enum class Vendor
{
    Unknown,
    Amd,
    Nvidia,
    Intel
};

enum class Arch
{
    Unknown,
    Rdna2,
    Rdna3,
    Rdna4,
    AmdOther,
    NvidiaDlss, // a GPU OptiScaler identifies as able to run DLSS
    NvidiaOther,
    IntelArc,
    IntelOther
};

// Upscaler families, in the order of the menu's FamilyId (FSR 4, FSR 3.1, FSR 2, XeSS, DLSS, HelixSR, FFX)
enum Family : int
{
    Fsr4,
    Fsr31,
    Fsr2,
    Xess,
    Dlss,
    HelixSR,
    Ffx,
    FamilyCount,
    NoFamily = -1
};

// FG outputs of OptiScaler the Advisor can suggest
enum FgOut : int
{
    XeFG,
    FsrFG,
    Dlssg,
    FgOutCount,
    NoFg = -1
};

enum class Level
{
    Verified,     // settled by a measurement of this session
    Estimate,     // technical rule, no measurement of its own
    Insufficient, // not enough to say
};

// What the FPS of this session say about native rendering
enum class Perf
{
    NativeKnown,    // no OptiScaler upscaler, or a 1.0 ratio: what is observed is native
    ObservedScaled, // an upscaler runs: the FPS are those of that setting, native is unknown
    Unknown,        // no stable samples yet
};

// Where the base FPS (real frames, without generated ones) come from
enum class Base
{
    Measured,       // counters of real game frames while generating, or the presents without FG
    MeasuredBefore, // the reference captured in MFG cost (FG off, earlier in the session)
    Estimated,      // presents divided by a reliable multiplier
    None,
};

enum class Quality
{
    NativeAA,
    Quality,
    Balanced,
    Performance,
};

// Reasons and warnings, drawn as text by the menu
enum class Note
{
    NativeMeetsTarget,
    NativeBelowTarget,
    NativeUnknownScaled,
    NoStableSamples,
    PrudentByResolution,
    ObservedHigh,
    ObservedLow,
    PreferredForArch,
    OnlyUsableFamily,
    NoUsableUpscaler,
    Fsr4Int8Cost,
    FgNotEvaluated,
    FgNoOutput,
    FgNoBase,
    FgBaseTooLow,
    FgBaseBelow60,
    FgBaseOk,
    FgCappedByOutput,
    FgCappedByRefresh,
    FgCappedEstimatedBase,
    FgUnstable,
    FgMaxUnknown,
    FgRefreshUnknown,
    NrNotSupported,
    NrNotReady,
    NrNoBase,
    NrHeadroom,
    NrNoHeadroom,
    LatencyAutomatic,
    LatencyXeFgUsesXeLL,
};

struct Facts
{
    Vendor vendor = Vendor::Unknown;
    Arch arch = Arch::Unknown;
    double vramGb = 0.0;

    int outWidth = 0, outHeight = 0; // what reaches the screen
    double refreshHz = 0.0;          // 0: unknown

    // Upscaling in this session
    bool upscalerRunning = false;
    double renderRatio = 1.0; // output width / render width

    // Frame times of the last seconds
    bool samplesStable = false; // enough recent samples, not a loading screen
    double presentedFps = 0.0;  // what is presented (generated frames included)
    double frametimeCv = 0.0;   // coefficient of variation of the frame times

    // Frame generation
    bool fgGenerating = false;
    int fgEffectiveMultiplier = 1;   // 2 = one generated frame per real one
    bool fgMultiplierReliable = false; // OptiScaler's output or DLSSG with a detected count, no dynamic MFG
    double realBaseFps = 0.0;        // MFG cost: real frames counted while generating, 0 = none
    double referenceBaseFps = 0.0;   // MFG cost: the captured reference (FG off), 0 = none
    int fgLoaded = NoFg;             // the output loaded now

    bool fgEvaluated = false;            // the FG options were worked out in this session
    bool fgUsable[FgOutCount] = {};      // usable here (the module's own conditions)
    int fgMaxMultiplier[FgOutCount] = {}; // 0 = unknown (known only once loaded)

    // Upscalers usable here (the catalogue's own conditions)
    bool familyUsable[FamilyCount] = {};

    // Neural Rendering
    bool nrGpuOk = false;  // the bridge can run a runtime on this GPU
    bool nrReady = false;  // a runtime is ready
    bool nrRestart = false; // another runtime is loaded: needs a restart

    // Latency
    bool fgXeFgChosen = false; // XeFG chosen or loaded: XeLL owns latency
};

struct Advice
{
    Perf perf = Perf::Unknown;
    Base base = Base::None;
    double baseFps = 0.0;
    double targetFps = 60.0;

    bool scaling = false;
    Level scalingLevel = Level::Insufficient;
    bool scalingPrudent = false; // an initial, prudent suggestion

    int family = NoFamily;
    Quality quality = Quality::Quality;
    Level techLevel = Level::Insufficient;

    bool fg = false;
    int fgOut = NoFg;
    int multiplier = 1;
    Level fgLevel = Level::Insufficient;
    bool fgPrudent = false;

    bool nr = false;
    Level nrLevel = Level::Insufficient;

    std::vector<Note> scalingNotes, techNotes, fgNotes, nrNotes, latencyNotes;
};

// The FPS to aim at: the refresh rate, between 60 and 144 (a higher refresh doesn't make 144 FPS unplayable)
inline double TargetFps(double refreshHz) { return refreshHz > 0.0 ? std::clamp(refreshHz * 0.9, 60.0, 144.0) : 60.0; }

// Families in order of preference for an architecture; the first usable one is suggested
inline std::vector<int> FamilyPreference(Arch arch)
{
    switch (arch)
    {
    case Arch::Rdna4:
        return { Fsr4, Fsr31, Xess, Fsr2 };
    case Arch::Rdna3:
        return { Fsr4, Fsr31, Xess, Fsr2 };
    case Arch::NvidiaDlss:
        return { Dlss, Fsr4, Xess, Fsr31, Fsr2 };
    case Arch::IntelArc:
        return { Xess, Fsr31, Fsr2 };
    default:
        return { Fsr31, Xess, Fsr2 };
    }
}

// FG outputs in order of preference: the loaded one first (the user's choice), then the vendor's own
inline std::vector<int> FgPreference(const Facts& f)
{
    std::vector<int> order;
    if (f.fgLoaded != NoFg)
        order.push_back(f.fgLoaded);
    const std::vector<int> vendor = f.vendor == Vendor::Nvidia ? std::vector<int> { Dlssg, XeFG, FsrFG }
                                    : f.vendor == Vendor::Amd   ? std::vector<int> { FsrFG, XeFG, Dlssg }
                                                                : std::vector<int> { XeFG, FsrFG, Dlssg };
    for (int o : vendor)
        if (std::find(order.begin(), order.end(), o) == order.end())
            order.push_back(o);
    return order;
}

inline Quality Step(Quality q, int by)
{
    return (Quality) std::clamp((int) q + by, (int) Quality::NativeAA, (int) Quality::Performance);
}

inline Advice Evaluate(const Facts& f)
{
    Advice a;
    a.targetFps = TargetFps(f.refreshHz);

    // ---- Base FPS: real frames first, the division only as an estimate ------------------------------------------
    if (f.fgGenerating)
    {
        if (f.realBaseFps > 0.0)
        {
            a.base = Base::Measured;
            a.baseFps = f.realBaseFps;
        }
        else if (f.referenceBaseFps > 0.0)
        {
            a.base = Base::MeasuredBefore;
            a.baseFps = f.referenceBaseFps;
        }
        else if (f.fgMultiplierReliable && f.fgEffectiveMultiplier > 1 && f.presentedFps > 0.0 && f.samplesStable)
        {
            a.base = Base::Estimated;
            a.baseFps = f.presentedFps / f.fgEffectiveMultiplier;
        }
    }
    else if (f.samplesStable && f.presentedFps > 0.0)
    {
        a.base = Base::Measured;
        a.baseFps = f.presentedFps;
    }

    // ---- What the FPS say about native ----------------------------------------------------------------------------
    if (a.base == Base::None)
        a.perf = Perf::Unknown;
    else if (!f.upscalerRunning || f.renderRatio <= 1.01)
        a.perf = Perf::NativeKnown;
    else
        a.perf = Perf::ObservedScaled;

    // ---- Scaling on or off, and its quality mode ---------------------------------------------------------------
    const Quality byResolution = f.outHeight >= 2000 ? Quality::Performance : f.outHeight >= 1400 ? Quality::Quality : Quality::Quality;

    if (a.perf == Perf::NativeKnown && a.base != Base::Estimated)
    {
        if (a.baseFps >= a.targetFps)
        {
            a.scaling = false;
            a.scalingLevel = Level::Verified;
            a.scalingNotes.push_back(Note::NativeMeetsTarget);
            a.quality = Quality::NativeAA;
        }
        else
        {
            a.scaling = true;
            a.scalingLevel = Level::Verified;
            a.scalingNotes.push_back(Note::NativeBelowTarget);
            // The further below the target, the lower the mode (a rule: no FPS are predicted)
            const double ratio = a.baseFps / a.targetFps;
            a.quality = ratio >= 0.8 ? Quality::Quality : ratio >= 0.6 ? Quality::Balanced : Quality::Performance;
        }
    }
    else if (a.perf == Perf::ObservedScaled)
    {
        a.scaling = true;
        a.scalingLevel = Level::Estimate;
        a.scalingNotes.push_back(Note::NativeUnknownScaled);
        a.quality = byResolution;
        const double observed = a.baseFps;
        if (observed >= a.targetFps * 1.25)
        {
            a.quality = Step(byResolution, -1);
            a.scalingNotes.push_back(Note::ObservedHigh);
        }
        else if (observed < a.targetFps * 0.8)
        {
            a.quality = Step(byResolution, +1);
            a.scalingNotes.push_back(Note::ObservedLow);
        }
    }
    else
    {
        // Nothing measured yet: a prudent start by output resolution
        a.scaling = f.outHeight >= 1400;
        a.scalingLevel = Level::Insufficient;
        a.scalingPrudent = true;
        a.quality = a.scaling ? byResolution : Quality::NativeAA;
        a.scalingNotes.push_back(Note::NoStableSamples);
        a.scalingNotes.push_back(Note::PrudentByResolution);
    }

    // ---- Technology: the first usable one in the architecture's order -------------------------------------------
    if (a.scaling || a.quality == Quality::NativeAA)
    {
        int usable = 0;
        for (int fam = 0; fam < FamilyCount; fam++)
            usable += f.familyUsable[fam] ? 1 : 0;

        for (int fam : FamilyPreference(f.arch))
        {
            if (f.familyUsable[fam])
            {
                a.family = fam;
                break;
            }
        }

        if (a.family == NoFamily)
        {
            a.techLevel = Level::Insufficient;
            a.techNotes.push_back(Note::NoUsableUpscaler);
            if (a.scaling)
                a.scaling = false;
        }
        else
        {
            a.techLevel = Level::Estimate; // compatibility is verified by the catalogue, the choice is a rule
            a.techNotes.push_back(usable == 1 ? Note::OnlyUsableFamily : Note::PreferredForArch);
            if (a.family == Fsr4 && f.arch == Arch::Rdna3)
                a.techNotes.push_back(Note::Fsr4Int8Cost);
        }
    }

    // ---- Frame generation: base FPS and latency first, the refresh rate only as a ceiling ------------------------
    if (!f.fgEvaluated)
    {
        a.fgLevel = Level::Insufficient;
        a.fgNotes.push_back(Note::FgNotEvaluated);
    }
    else
    {
        for (int o : FgPreference(f))
        {
            if (o >= 0 && o < FgOutCount && f.fgUsable[o])
            {
                a.fgOut = o;
                break;
            }
        }

        if (a.fgOut == NoFg)
        {
            a.fgLevel = Level::Verified;
            a.fgNotes.push_back(Note::FgNoOutput);
        }
        else if (a.base == Base::None)
        {
            a.fg = false;
            a.fgPrudent = true;
            a.fgLevel = Level::Insufficient;
            a.fgNotes.push_back(Note::FgNoBase);
        }
        else if (a.baseFps < 45.0)
        {
            a.fg = false;
            a.fgLevel = a.base == Base::Estimated ? Level::Estimate : Level::Verified;
            a.fgNotes.push_back(Note::FgBaseTooLow);
        }
        else if (a.baseFps < 60.0)
        {
            a.fg = false;
            a.fgPrudent = true;
            a.fgLevel = Level::Estimate;
            a.fgNotes.push_back(Note::FgBaseBelow60);
        }
        else
        {
            a.fg = true;
            a.fgLevel = Level::Estimate;
            a.fgNotes.push_back(Note::FgBaseOk);

            const bool measured = a.base == Base::Measured || a.base == Base::MeasuredBefore;
            const bool stable = f.frametimeCv < 0.15;
            int m = 2;
            if (measured && stable && a.baseFps >= 70.0)
                m = 3;
            if (measured && stable && a.baseFps >= 85.0)
                m = 4;

            if (!stable && f.frametimeCv >= 0.20)
            {
                m = 2;
                a.fgNotes.push_back(Note::FgUnstable);
            }
            if (!measured && m > 2)
                m = 2;
            if (!measured)
                a.fgNotes.push_back(Note::FgCappedEstimatedBase);

            // The output's own maximum: known once it is loaded; unknown, no more than 2x
            const int maxMultiplier = f.fgMaxMultiplier[a.fgOut];
            if (maxMultiplier <= 0)
            {
                if (m > 2)
                {
                    m = 2;
                    a.fgNotes.push_back(Note::FgMaxUnknown);
                }
            }
            else if (m > maxMultiplier)
            {
                m = maxMultiplier;
                a.fgNotes.push_back(Note::FgCappedByOutput);
            }

            // The refresh rate: a ceiling of reference, never a reason to go higher
            if (f.refreshHz > 0.0)
            {
                bool capped = false;
                while (m > 2 && a.baseFps * m > f.refreshHz * 1.1)
                {
                    m--;
                    capped = true;
                }
                if (capped)
                    a.fgNotes.push_back(Note::FgCappedByRefresh);
            }
            else
                a.fgNotes.push_back(Note::FgRefreshUnknown);

            a.multiplier = std::max(2, m);
        }
    }

    // ---- Neural Rendering: only with a runtime ready and room in the frame time ---------------------------------
    if (!f.nrGpuOk)
    {
        a.nr = false;
        a.nrLevel = Level::Verified;
        a.nrNotes.push_back(Note::NrNotSupported);
    }
    else if (!f.nrReady || f.nrRestart)
    {
        a.nr = false;
        a.nrLevel = Level::Verified;
        a.nrNotes.push_back(Note::NrNotReady);
    }
    else if (a.base == Base::None)
    {
        a.nr = false;
        a.nrLevel = Level::Insufficient;
        a.nrNotes.push_back(Note::NrNoBase);
    }
    else if (a.baseFps >= 75.0)
    {
        a.nr = true;
        a.nrLevel = Level::Estimate;
        a.nrNotes.push_back(Note::NrHeadroom);
    }
    else
    {
        a.nr = false;
        a.nrLevel = Level::Estimate;
        a.nrNotes.push_back(Note::NrNoHeadroom);
    }

    // ---- Latency: ILION's intelligent activation, which already knows what is compatible --------------------------
    a.latencyNotes.push_back(Note::LatencyAutomatic);
    if (f.fgXeFgChosen || (a.fg && a.fgOut == XeFG))
        a.latencyNotes.push_back(Note::LatencyXeFgUsesXeLL);

    return a;
}

// A suggestion shown changes only when the new one has held for this long (seconds)
inline constexpr double kHoldSeconds = 5.0;

// The short form compared for the hold: what the banner line says
inline int Signature(const Advice& a)
{
    return (a.scaling ? 1 : 0) | ((a.family + 1) << 1) | ((int) a.quality << 5) | ((a.fg ? 1 : 0) << 8) | ((a.fgOut + 1) << 9) |
           (a.multiplier << 12) | ((a.nr ? 1 : 0) << 16);
}
} // namespace IlionAdvisor
