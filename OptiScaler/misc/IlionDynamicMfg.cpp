#include "pch.h"
#include "IlionDynamicMfg.h"

#include <Config.h>
#include <State.h>
#include <Util.h>
#include <framegen/IFGFeature.h>
#include <framegen/xefg/XeFG_DynamicMFG.h>
#include <hooks/Streamline_Hooks.h>
#include <hooks/Xell_Hooks.h>
#include <misc/FrameLimit.h>
#include <proxies/XeFG_Proxy.h>

#include <algorithm>

namespace IlionDynamicMfg
{
namespace
{
// The game's DLSSG counts as generating while it evaluated with frames to generate in the last second
constexpr uint64_t kExternalWindowMs = 1000;

std::atomic<uint64_t> wrappedPresents { 0 };

bool OwnRunning()
{
    auto fg = State::Instance().currentFG;
    return fg != nullptr && fg->IsActive() && !fg->IsPaused();
}

const char* ExternalName(FGNvngxReplacement replacement)
{
    switch (replacement)
    {
    case FGNvngxReplacement::Arturs:
        return "DLSS Enabler";
    case FGNvngxReplacement::Nukems:
        return "Nukem's DLSSG";
    case FGNvngxReplacement::FFX:
        return "FSR FG (Nvngx)";
    case FGNvngxReplacement::Combo:
        return "Combo";
    case FGNvngxReplacement::HelixSR:
        return "NVIDIA FG Lite";
    default:
        return "DLSSG";
    }
}
} // namespace

Provider Current()
{
    const auto& state = State::Instance();

    switch (state.activeFgOutput)
    {
    case FGOutput::XeFG:
        return Provider::XeFG;
    case FGOutput::DLSSG:
        return Provider::DlssgOutput;
    case FGOutput::FSRFG:
    case FGOutput::Reprojection:
        return Provider::FsrFg;
    default:
        break;
    }

    // No output of ILION: the game's DLSSG, through a replacement ILION loads (NvngxFG) or seen evaluating
    if (state.activeFgInput == FGInput::NvngxFG || state.activeFgNvngx != FGNvngxReplacement::None ||
        ExternalGenerating())
        return Provider::DlssgGame;

    return Provider::None;
}

bool DynamicSupported(const char** reason)
{
    const auto& state = State::Instance();
    auto config = Config::Instance();
    const char* why = nullptr;

    switch (Current())
    {
    case Provider::XeFG:
    {
        auto fg = state.currentFG;
        if (fg == nullptr || fg->GetMaxInterpolationCount() < 1)
            why = "XeFG isn't loaded yet";
        else if (XeFGProxy::SetNumInterpolatedFrames() == nullptr || XeFGProxy::SetEnabled() == nullptr)
            why = "This XeFG can't change its multiplier while running";
        else if (state.swapchainInteropApi == SwapchainInteropApi::VkwDx12)
            why = "Dynamic MFG isn't validated on Vulkan yet: the fixed multiplier is used";
        else if (config->FGFollowsGame())
            why = "Frame generation follows the game: choose a multiplier first";
        break;
    }
    case Provider::DlssgGame:
        if (!StreamlineHooks::isDlssgHooked())
            why = "The game's DLSSG doesn't go through Streamline: ILION can't change its mode";
        else if (!state.dlssgGameDMFGSupported)
            why = "The DLSSG backend in use doesn't report Dynamic MFG support: use a fixed multiplier with the "
                  "target as cap";
        break;
    case Provider::DlssgOutput:
        if (state.currentFG == nullptr || !state.currentFG->GetDMFGSupport())
            why = "This DLSSG doesn't report Dynamic MFG support";
        break;
    case Provider::FsrFg:
        why = "FSR FG generates one frame: no Dynamic MFG";
        break;
    default:
        why = "No frame generation is loaded";
        break;
    }

    if (reason != nullptr)
        *reason = why;

    return why == nullptr;
}

bool DynamicEnabled()
{
    auto config = Config::Instance();

    switch (Current())
    {
    case Provider::XeFG:
        return XeFG_DynamicMFG::Allowed();
    case Provider::DlssgGame:
        return config->FGDLSSGOverrideForceDMFG.value_or_default() && State::Instance().dlssgGameDMFGSupported;
    case Provider::DlssgOutput:
        return config->FGDLSSGForceDMFG.value_or_default();
    default:
        return false;
    }
}

void SetDynamicEnabled(bool enabled)
{
    auto config = Config::Instance();
    auto& state = State::Instance();

    switch (Current())
    {
    case Provider::XeFG:
        config->FGXeFGDynamicMFG = enabled;
        if (enabled)
            config->FGEnabled = true; // Dynamic runs on the manual FG path, as R3's own switch did
        break;
    case Provider::DlssgGame:
        config->FGDLSSGOverrideForceDMFG = enabled;
        StreamlineHooks::updateDlssgOptions();
        break;
    case Provider::DlssgOutput:
        config->FGDLSSGForceDMFG = enabled;
        state.fgChanged = true;
        break;
    default:
        break;
    }
}

float TargetFps()
{
    auto config = Config::Instance();

    // One target: the XeFG key, with the older DLSSG key as fallback for an ini that only has that one
    if (const float target = config->FGXeFGFramerateTargetDMFG.value_or_default(); target > 0.0f)
        return target;

    return config->FGDLSSGFramerateTargetDMFG.value_or_default();
}

void SetTargetFps(float fps)
{
    auto config = Config::Instance();
    fps = fps < 10.0f ? 0.0f : std::min(fps, 1000.0f);

    // The DLSSG backends read their own key (DLSSGOptions::dynamicTargetFrameRate): it is kept equal, not a second
    // setting
    config->FGXeFGFramerateTargetDMFG = fps;
    if (fps > 0.0f)
        config->FGDLSSGFramerateTargetDMFG = fps;
    else
        config->FGDLSSGFramerateTargetDMFG.reset();

    if (Current() == Provider::DlssgGame)
        StreamlineHooks::updateDlssgOptions();
}

int MaxCountLimit()
{
    const auto& state = State::Instance();

    switch (Current())
    {
    case Provider::XeFG:
    case Provider::DlssgOutput:
        return state.currentFG != nullptr ? std::max(state.currentFG->GetMaxInterpolationCount(), 0) : 0;
    case Provider::DlssgGame:
        return state.dlssgMfgMax.value_or(0);
    case Provider::FsrFg:
        return 1;
    default:
        return 0;
    }
}

int MaxCount()
{
    auto config = Config::Instance();
    const int limit = std::max(MaxCountLimit(), 1);

    switch (Current())
    {
    case Provider::XeFG:
        return std::clamp(config->FGXeFGInterpolationCount.value_or_default(), 1, limit);
    case Provider::DlssgOutput:
        return std::clamp(config->FGDLSSGInterpolationCount.value_or_default(), 1, limit);
    case Provider::DlssgGame:
    {
        // No override: the game's own choice, as far as ILION saw it
        if (!config->FGDLSSGOverrideInterpolationCount.has_value() || config->FGDLSSGOverrideInterpolationCount.value() <= 0)
            return std::clamp(State::Instance().dlssgDetectedInterpolationCount, 1, limit);
        return std::clamp(config->FGDLSSGOverrideInterpolationCount.value(), 1, limit);
    }
    default:
        return 1;
    }
}

void SetMaxCount(int count)
{
    auto config = Config::Instance();
    count = std::clamp(count, 1, std::max(MaxCountLimit(), 1));

    switch (Current())
    {
    case Provider::XeFG:
        config->FGXeFGInterpolationCount = count;
        break;
    case Provider::DlssgOutput:
        config->FGDLSSGInterpolationCount = count;
        break;
    case Provider::DlssgGame:
        // The same setting as "Override DLSSG Ratio": the frames the game's DLSSG generates (the most, in Dynamic)
        config->FGDLSSGOverrideInterpolationCount = count;
        StreamlineHooks::updateDlssgOptions();
        break;
    default:
        break;
    }
}

bool TargetAsCap() { return Config::Instance()->FGXeFGTargetAsCapDMFG.value_or_default(); }

void SetTargetAsCap(bool cap) { Config::Instance()->FGXeFGTargetAsCapDMFG = cap; }

bool RoundingSupported() { return Current() == Provider::XeFG; }

bool ExternalGenerating(int* multiplier)
{
    const auto& state = State::Instance();
    const uint64_t last = state.dlssgFgLastEvaluateTick.load();
    const bool recent = last != 0 && GetTickCount64() - last < kExternalWindowMs;
    const int count = state.dlssgDetectedInterpolationCount;

    if (multiplier != nullptr)
        *multiplier = recent && count > 0 ? count + 1 : 0;

    return recent && count > 0;
}

void NoteWrappedPresent() { wrappedPresents.fetch_add(1, std::memory_order_relaxed); }

int ExternalLimiterFactor()
{
    int multiplier = 0;
    if (!ExternalGenerating(&multiplier) || multiplier < 2)
        return 1;

    // Present thread only (OptiScaler's limiter at the wrapped swapchain)
    static uint64_t lastPresents = 0;
    static uint64_t lastEvaluations = 0;
    static double lastMs = 0.0;
    static int factor = 1;

    const double now = Util::MillisecondsNow();
    const uint64_t presents = wrappedPresents.load(std::memory_order_relaxed);
    const uint64_t evaluations = State::Instance().dlssgFgEvaluations.load();

    if (lastMs <= 0.0 || now - lastMs > 3000.0)
    {
        lastPresents = presents;
        lastEvaluations = evaluations;
        lastMs = now;
        return factor;
    }

    if (now - lastMs >= 500.0)
    {
        const uint64_t frames = evaluations - lastEvaluations;
        if (frames >= 10)
        {
            // About one present per rendered frame: it sees only the game's frames
            const double perFrame = (double) (presents - lastPresents) / (double) frames;
            const int measured = perFrame < (double) multiplier - 0.5 ? multiplier : 1;
            if (measured != factor)
                LOG_INFO("ILION target cap: {:.2f} wrapped presents per DLSSG frame at {}X, limiter factor {}", perFrame,
                         multiplier, measured);
            factor = measured;
        }

        lastPresents = presents;
        lastEvaluations = evaluations;
        lastMs = now;
    }

    return factor;
}

float CapFps()
{
    if (!TargetAsCap())
        return 0.0f;

    const auto& state = State::Instance();

    // The Vulkan -> D3D12 XeFG bridge keeps its limiter as it was until it is validated
    if (state.swapchainInteropApi == SwapchainInteropApi::VkwDx12)
        return 0.0f;
    const float target = TargetFps();

    switch (Current())
    {
    case Provider::XeFG:
        // R3: Dynamic's controller sets the cap it holds (it accounts for the automatic target and the rounding)
        if (XeFG_DynamicMFG::Allowed())
            return OwnRunning() && state.xefgDynamicCapFps > 0.0f ? state.xefgDynamicCapFps : 0.0f;
        return OwnRunning() && target > 0.0f ? target : 0.0f;
    case Provider::DlssgOutput:
    case Provider::FsrFg:
        return OwnRunning() && target > 0.0f ? target : 0.0f;
    case Provider::DlssgGame:
        return ExternalGenerating() && target > 0.0f ? target : 0.0f;
    default:
        return 0.0f;
    }
}

Status Get()
{
    const auto& state = State::Instance();
    Status s;
    s.provider = Current();
    auto fg = state.currentFG;
    const double now = Util::MillisecondsNow();

    switch (s.provider)
    {
    case Provider::XeFG:
        s.name = "XeFG";
        break;
    case Provider::DlssgOutput:
        s.name = "DLSSG (ILION)";
        break;
    case Provider::FsrFg:
        s.name = fg != nullptr ? fg->Name() : "FSR FG";
        break;
    case Provider::DlssgGame:
        s.name = ExternalName(state.activeFgNvngx);
        break;
    default:
        s.name = "";
        break;
    }

    if (s.provider != Provider::DlssgGame && OwnRunning())
    {
        s.own = true;
        s.multiplier = (int) fg->GetInterpolatedFrameCount() + 1;
        s.gen = Gen::Detected;

        if (state.lastFGFrameTime > 0.5)
        {
            s.baseFps = (float) (1000.0 / state.lastFGFrameTime);
            s.baseMeasured = true;
        }

        // XeFG reports what it sent to its presenter: generation confirmed, both rates measured
        if (s.provider == Provider::XeFG)
        {
            const auto measured = state.xefgPresentTelemetry.Read(now);
            if (measured.outputValid)
            {
                s.gen = Gen::Confirmed;
                s.finalFps = (float) measured.outputFps;
                s.finalMeasured = true;
            }
            if (measured.baseValid)
                s.baseFps = (float) measured.baseFps;
        }
    }
    else if (s.provider == Provider::DlssgGame)
    {
        int multiplier = 0;
        if (ExternalGenerating(&multiplier))
        {
            s.gen = Gen::Detected;
            s.multiplier = multiplier;

            // The backend's own count of presented frames, reported to the game in the last second
            const uint64_t tick = state.dlssgFramesPresentedTick.load();
            if (state.dlssgFramesActuallyPresented.load() > 1 && tick != 0 && GetTickCount64() - tick < kExternalWindowMs)
                s.gen = Gen::Confirmed;
        }
        else
        {
            s.gen = Gen::Unknown;
        }

        // Real frames: the game's DLSSG evaluates once per rendered frame
        static uint64_t lastCount = 0;
        static double lastMs = 0.0;
        static float rate = 0.0f;
        const uint64_t count = state.dlssgFgEvaluations.load();
        if (lastMs <= 0.0 || now - lastMs > 2000.0)
        {
            lastCount = count;
            lastMs = now;
            rate = 0.0f;
        }
        else if (now - lastMs >= 500.0)
        {
            rate = (float) ((count - lastCount) * 1000.0 / (now - lastMs));
            lastCount = count;
            lastMs = now;
        }

        if (s.gen != Gen::Unknown && rate > 0.0f)
        {
            s.baseFps = rate;
            s.baseMeasured = true;
        }
    }

    if (!s.finalMeasured && s.baseFps > 0.0f && s.multiplier > 0)
        s.finalFps = s.baseFps * (float) s.multiplier;

    // The limiter FG_Hooks / the wrapped swapchain choose: a target cap is always OptiScaler's own (it knows the
    // multiplier); else Reflex, else XeLL, else OptiScaler's
    s.limitFps = FrameLimit::targetFps();
    if (FrameLimit::targetCapActive())
        s.limiter = Limiter::OptiScaler;
    else if (state.reflexLimitsFps)
        s.limiter = Limiter::Reflex;
    else if (XellHooks::canLimit())
        s.limiter = Limiter::XeLL;
    else if (s.limitFps > 0.0f)
        s.limiter = Limiter::OptiScaler;

    return s;
}
} // namespace IlionDynamicMfg
