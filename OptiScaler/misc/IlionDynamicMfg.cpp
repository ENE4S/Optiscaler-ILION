#include "pch.h"
#include "IlionDynamicMfg.h"

#include <Config.h>
#include <State.h>
#include <Util.h>
#include <framegen/IFGFeature.h>
#include <framegen/xefg/XeFG_DynamicMFG.h>
#include <framegen/xefg/XeFG_DynamicCount.h>
#include <hooks/Streamline_Hooks.h>
#include <hooks/Xell_Hooks.h>
#include <misc/FrameLimit.h>
#include <misc/IlionDynamicCapRule.h>
#include <framegen/nvngx/Nvngx_XeMfgXmx.h>
#include <proxies/XeFG_Proxy.h>

#include <algorithm>
#include <cstdlib>

namespace IlionDynamicMfg
{
namespace
{
// The game's DLSSG counts as generating while it evaluated with frames to generate in the last second
constexpr uint64_t kExternalWindowMs = 1000;

std::atomic<uint64_t> wrappedPresents { 0 };
std::atomic<int> measuredPerFrameX100 { 0 }; // wrapped presents per real DLSSG frame x 100, 0 unknown
std::atomic<float> measuredPresentFps { 0.0f }; // wrapped presents per second of the last window
std::atomic<float> measuredRealFps { 0.0f };    // real DLSSG frames per second of the last window
std::atomic<int> capSuspendedAt { 0 };       // asked multiplier the external cap was suspended at, 0 not suspended

// ILION Dynamic for the game's DLSSG: the count it asks for (interpolated frames), a change waiting to be sent through
// slDLSSGSetOptions, and why it stopped. The controller itself runs on one thread only (ExternalRealFrame)
std::atomic<int> ilionCount { 1 };
std::atomic<bool> ilionPending { false };
std::atomic<const char*> ilionRefused { nullptr };
std::atomic<bool> presentsSeen { false }; // a multiplier was measured at the presents since ILION Dynamic started

// The most ILION Dynamic may ask the game's DLSSG for: the user's maximum (Override DLSSG Ratio), within what the
// backend reports
int IlionMaxCount()
{
    auto config = Config::Instance();
    const int limit = std::max(State::Instance().dlssgMfgMax.value_or(0), 1);
    // Read once: the menu can empty the setting between a has_value() and a value() of this thread (game's present)
    const int set = config->FGDLSSGOverrideInterpolationCount.value_or(0);
    const int user = set > 0 ? set : limit;
    return std::clamp(user, 1, limit);
}

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
    case FGNvngxReplacement::XeMfgXmx:
        return "XeMFG XMX";
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
        // ILION Dynamic needs a backend that generates 2 frames or more; the backend's own Dynamic, its support
        if (!StreamlineHooks::isDlssgHooked())
            why = "The game's DLSSG doesn't go through Streamline: ILION can't change its mode";
        else if (state.dlssgMfgMax.value_or(0) < 2 && !state.dlssgGameDMFGSupported)
            why = "The DLSSG backend in use generates one frame at most and doesn't report Dynamic MFG support";
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
        return ExternalDynamicMode() != ExternalMode::Off;
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
        // On: ILION Dynamic when the backend generates 2 frames or more, else the backend's own Dynamic
        SetExternalDynamicMode(!enabled                               ? ExternalMode::Off
                               : state.dlssgMfgMax.value_or(0) >= 2 ? ExternalMode::Ilion
                                                                      : ExternalMode::Native);
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

    // The game's DLSSG reads the target only in Dynamic: re-sending its options otherwise could change its count
    if (Current() == Provider::DlssgGame && DynamicEnabled())
        StreamlineHooks::requestDlssgOptionsUpdate("Dynamic MFG target");
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
        // ILION Dynamic: the maximum its controller uses (IlionMaxCount)
        if (config->FGDLSSGIlionDynamic.value_or_default())
            return std::clamp(IlionMaxCount(), 1, limit);

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
        StreamlineHooks::requestDlssgOptionsUpdate("multiplier / Dynamic MFG maximum");
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

void NoteWrappedPresent()
{
    const uint64_t presents = wrappedPresents.fetch_add(1, std::memory_order_relaxed) + 1;

    // Present thread: every 500 ms, the presents the wrapped swapchain made per real frame the game's DLSSG evaluated
    // (State::dlssgFgEvaluations counts MultiFrameIndex 1 only). With the generated frames going through it, that is
    // the multiplier the backend really applies (DLSS Enabler can generate another count than the one asked for);
    // about 1 means it sees only the game's frames, more than the backend's maximum means it sees other presents too
    static uint64_t lastPresents = 0;
    static uint64_t lastEvaluations = 0;
    static double lastMs = 0.0;
    static int candidate = 0;
    static int unsettled = 0; // windows in a row that matched no whole multiplier

    const double now = Util::MillisecondsNow();
    const uint64_t evaluations = State::Instance().dlssgFgEvaluations.load();

    if (lastMs <= 0.0 || now - lastMs > 3000.0 || !ExternalGenerating())
    {
        lastPresents = presents;
        lastEvaluations = evaluations;
        lastMs = now;
        candidate = 0;
        unsettled = 0;
        if (!ExternalGenerating())
        {
            measuredPerFrameX100.store(0);
            measuredPresentFps.store(0.0f);
            measuredRealFps.store(0.0f);
        }
        return;
    }

    if (now - lastMs < 500.0)
        return;

    const uint64_t frames = evaluations - lastEvaluations;
    measuredPresentFps.store((float) ((presents - lastPresents) * 1000.0 / (now - lastMs)));
    measuredRealFps.store((float) (frames * 1000.0 / (now - lastMs)));

    if (frames >= 10)
    {
        // A count is taken only when the window lands within 0.3 of a whole number, and only after two windows in a
        // row agree: a hitch or a menu can't make the cap jump between multipliers. A window between two counts (a
        // change in the middle of it) keeps the last one, three in a row drop it: the backend keeps changing. A window
        // that is clearly not a batch of frames (under 1.5, or above the most the backend can make) clears it
        const int perFrameX100 = (int) ((presents - lastPresents) * 100 / frames);
        const int rounded = (perFrameX100 + 50) / 100;
        const bool whole = std::abs(perFrameX100 - rounded * 100) <= 30;
        const int most = State::Instance().dlssgMfgMax.value_or(5) + 1;

        if (perFrameX100 < 150 || rounded > most)
        {
            candidate = 0;
            unsettled = 0;
            if (measuredPerFrameX100.exchange(0) != 0)
                LOG_INFO("ILION external FG: {:.2f} presents per real DLSSG frame, the applied multiplier can't be "
                         "measured",
                         perFrameX100 / 100.0);
        }
        else if (whole && rounded == candidate)
        {
            unsettled = 0;
            if (measuredPerFrameX100.exchange(rounded * 100) != rounded * 100)
                LOG_INFO("ILION external FG: applies {}X (asked {}X), {:.2f} presents per real DLSSG frame", rounded,
                         State::Instance().dlssgDetectedInterpolationCount + 1, perFrameX100 / 100.0);
        }
        else
        {
            candidate = whole ? rounded : 0;
            if (!whole && ++unsettled >= 3 && measuredPerFrameX100.exchange(0) != 0)
                LOG_INFO("ILION external FG: {:.2f} presents per real DLSSG frame for 1.5 s, no stable multiplier",
                         perFrameX100 / 100.0);
        }
    }

    lastPresents = presents;
    lastEvaluations = evaluations;
    lastMs = now;
}

float ExternalRealFps() { return ExternalGenerating() ? measuredRealFps.load() : 0.0f; }

int ExternalReported()
{
    const auto& state = State::Instance();
    const uint64_t tick = state.dlssgFramesPresentedTick.load();
    const uint32_t reported = state.dlssgFramesActuallyPresented.load();
    return ExternalGenerating() && tick != 0 && GetTickCount64() - tick < kExternalWindowMs ? (int) reported : 0;
}

int ExternalApplied(int* requested, AppliedSource* source)
{
    int asked = 0;
    ExternalGenerating(&asked);

    if (requested != nullptr)
        *requested = asked;
    if (source != nullptr)
        *source = AppliedSource::None;
    if (asked == 0)
        return 0;

    // Measured from the presents: the count really applied
    if (const int perFrameX100 = measuredPerFrameX100.load(); perFrameX100 >= 150)
    {
        if (source != nullptr)
            *source = AppliedSource::Measured;
        return (perFrameX100 + 50) / 100;
    }

    // Only what was asked for: not confirmed. Streamline's numFramesActuallyPresented counts the frames since the
    // game's last slDLSSGGetState, a multiplier only if the game asks once per frame: shown apart (ExternalReported)
    if (source != nullptr)
        *source = AppliedSource::Requested;
    return asked;
}

float ExternalCapRealFps(int framesToGenerate)
{
    if (Current() != Provider::DlssgGame)
        return 0.0f;

    const float cap = CapFps();

    // ILION Dynamic: the frames this real frame generates, which the backend was checked to take
    // (ExternalRealFrame), once the presents showed the generated frames; until then the cap waits
    if (IlionDrivesExternal())
    {
        const int count = framesToGenerate >= 0 ? framesToGenerate : ilionCount.load();
        return cap > 0.0f && presentsSeen.load() && count >= 1 ? cap / (float) (count + 1) : 0.0f;
    }

    // Safe fallback: only a multiplier measured at the presents sets the real-frame cap. With only the asked count the
    // cap waits: a wrong guess would hold the game below the target
    AppliedSource source = AppliedSource::None;
    const int applied = ExternalApplied(nullptr, &source);
    const bool confirmed = source == AppliedSource::Measured;
    return cap > 0.0f && confirmed && applied >= 1 ? cap / (float) applied : 0.0f;
}

bool BackendDynamic()
{
    return Current() == Provider::DlssgGame && State::Instance().dlssgLastSetMode == sl::DLSSGMode::eDynamic;
}

bool ExternalCapSuspended()
{
    int requested = 0;
    AppliedSource source = AppliedSource::None;
    const int applied = ExternalApplied(&requested, &source);
    const bool confirmed = source == AppliedSource::Measured;

    // Another asked count (or the generation stopped): the cap may be tried again
    if (int held = capSuspendedAt.load(); held != 0 && held != requested &&
                                          capSuspendedAt.compare_exchange_strong(held, 0))
        LOG_INFO("ILION external FG: target cap resumed, {}X asked now", requested);

    // More frames than asked while ILION lowers the real frames: the backend's own controller answers the cap by
    // raising the multiplier, the cap would chase it down to its maximum. Kept off until the asked count changes
    int expected = 0;
    if (confirmed && requested > 0 && applied > requested && capSuspendedAt.compare_exchange_strong(expected, requested))
        LOG_WARN("ILION external FG: target cap suspended, the backend applies {}X although {}X was asked", applied,
                 requested);

    return capSuspendedAt.load() != 0;
}

float CapFps()
{
    // ILION v1.4.0 R3: the cap is Dynamic MFG's. It holds the frames only while Dynamic MFG runs for the provider in
    // use (IlionDynamicCapRule); with a fixed multiplier it never applies, whatever the saved "Use target as cap" says.
    // R2 capped fixed XeFG, ILION's DLSSG, FSR FG and the game's DLSSG (real frames held at target / multiplier).
    // Re-evaluated on every call (FrameLimit::targetFps): switching Dynamic off removes the cap at the next frame
    const auto& state = State::Instance();
    const Provider provider = Current();

    IlionDynamicCapRule::Inputs in;
    in.targetAsCap = TargetAsCap();
    in.vulkanBridge = state.swapchainInteropApi == SwapchainInteropApi::VkwDx12;
    in.target = TargetFps();
    in.ownRunning = provider != Provider::DlssgGame && OwnRunning();
    in.xefgDynamic = provider == Provider::XeFG && XeFG_DynamicMFG::Allowed();
    in.xefgControllerCap = state.xefgDynamicCapFps;
    in.dlssgOutputDynamic = provider == Provider::DlssgOutput && Config::Instance()->FGDLSSGForceDMFG.value_or_default();
    in.ilionDrivesExternal = provider == Provider::DlssgGame && IlionDrivesExternal();
    in.externalGenerating = provider == Provider::DlssgGame && in.ilionDrivesExternal && ExternalGenerating();

    const auto ruleProvider = provider == Provider::XeFG          ? IlionDynamicCapRule::Provider::XeFG
                              : provider == Provider::DlssgGame   ? IlionDynamicCapRule::Provider::DlssgGame
                              : provider == Provider::DlssgOutput ? IlionDynamicCapRule::Provider::DlssgOutput
                              : provider == Provider::FsrFg       ? IlionDynamicCapRule::Provider::FsrFg
                                                                  : IlionDynamicCapRule::Provider::None;
    const float cap = IlionDynamicCapRule::Cap(ruleProvider, in);

    // No cap: the fixed-multiplier loop guard has nothing to keep (no residual state for the next time it applies)
    if (cap <= 0.0f)
        capSuspendedAt.store(0);

    return cap;
}

ExternalMode ExternalDynamicMode()
{
    auto config = Config::Instance();
    if (config->FGDLSSGIlionDynamic.value_or_default())
        return ExternalMode::Ilion;
    if (config->FGDLSSGOverrideForceDMFG.value_or_default() && State::Instance().dlssgGameDMFGSupported)
        return ExternalMode::Native;
    return ExternalMode::Off;
}

void SetExternalDynamicMode(ExternalMode mode)
{
    auto config = Config::Instance();
    config->FGDLSSGIlionDynamic = mode == ExternalMode::Ilion;
    config->FGDLSSGOverrideForceDMFG = mode == ExternalMode::Native;
    ilionRefused.store(nullptr);
    StreamlineHooks::requestDlssgOptionsUpdate(mode == ExternalMode::Ilion    ? "Dynamic MFG: ILION"
                                               : mode == ExternalMode::Native ? "Dynamic MFG: automatic"
                                                                              : "Dynamic MFG: off");
}

bool IlionDrivesExternal()
{
    return Config::Instance()->FGDLSSGIlionDynamic.value_or_default() && ilionRefused.load() == nullptr &&
           Current() == Provider::DlssgGame && StreamlineHooks::isDlssgHooked();
}

int IlionExternalCount() { return std::clamp(ilionCount.load(), 1, IlionMaxCount()); }

const char* IlionExternalRefused()
{
    return Config::Instance()->FGDLSSGIlionDynamic.value_or_default() ? ilionRefused.load() : nullptr;
}

namespace
{
void Refuse(const char* why)
{
    const char* none = nullptr;
    if (ilionRefused.compare_exchange_strong(none, why))
    {
        LOG_WARN("ILION Dynamic (game's DLSSG) stopped: {}", why);
        ilionPending.store(true); // the game's own options go back to the backend (fixed count, the maximum)
    }
}
} // namespace

void ExternalRealFrame(int framesToGenerate, double sleptMs)
{
    // The R3 controller, an instance of its own: XeFG's R3 (XeFG_DynamicMFG) is not touched
    static XeFG_DynamicCount controller;
    static bool active = false;
    static bool lastCap = false;
    static double lastFrameMs = 0.0;
    static double askedAtMs = 0.0; // when the count in ilionCount was asked for; 0 once the backend took it
    static double logMs = 0.0;

    const double now = Util::MillisecondsNow();
    const double frameMs = lastFrameMs > 0.0 ? now - lastFrameMs : 0.0;
    lastFrameMs = now;

    if (!IlionDrivesExternal())
    {
        if (active)
        {
            LOG_INFO("ILION Dynamic (game's DLSSG): off");
            controller.Reset();
            active = false;
            presentsSeen.store(false);
        }
        return;
    }

    const int maxCount = IlionMaxCount();

    if (!active)
    {
        // As R3: start at 2X, the settling window is never spent at the old fixed maximum
        controller.Restart();
        active = true;
        lastCap = false;
        presentsSeen.store(false);
        ilionCount.store(1);
        ilionPending.store(true);
        askedAtMs = now;
        LOG_INFO("ILION Dynamic (game's DLSSG): on, starting at 2X, maximum {}X", maxCount + 1);
        return;
    }

    if (measuredPerFrameX100.load() >= 150)
        presentsSeen.store(true);

    int asked = ilionCount.load();

    // A lower maximum is followed at once
    if (asked > maxCount)
    {
        ilionCount.store(maxCount);
        ilionPending.store(true);
        askedAtMs = now;
        return;
    }

    // The game's DLSSG generating nothing (its menu, a pause) or a long gap (a load): nothing to judge, a pending
    // check starts over
    if (framesToGenerate <= 0 || frameMs > 1000.0)
    {
        if (askedAtMs > 0.0)
            askedAtMs = now;
        return;
    }

    // The backend has to take the count asked for: this frame's MultiFrameCount shows it. Frames still at the old
    // count are not judged; the count is sent once more after 0.5 s, 2 s without it means the backend doesn't follow
    // ILION's count
    static bool resent = false;
    if (framesToGenerate != asked)
    {
        if (askedAtMs <= 0.0)
            askedAtMs = now; // the game changed the count itself: ILION's goes back
        if (!resent && now - askedAtMs > 500.0)
        {
            resent = true;
            ilionPending.store(true);
        }
        if (now - askedAtMs > 2000.0)
            Refuse("The backend didn't take the multiplier ILION asked for within 2 s");
        return;
    }
    resent = false;

    if (askedAtMs > 0.0)
    {
        LOG_INFO("ILION Dynamic (game's DLSSG): the backend took {}X after {:.0f} ms", asked + 1, now - askedAtMs);
        askedAtMs = 0.0;
    }

    // The presents must never show more frames than the maximum: the backend would be ignoring it
    if (const int perFrameX100 = measuredPerFrameX100.load();
        perFrameX100 >= 150 && (perFrameX100 + 50) / 100 > maxCount + 1)
    {
        Refuse("The backend presented more frames than the maximum multiplier");
        return;
    }

    // A hitch or a load is no sample; without a target there is nothing to aim at, the count stays
    const float target = TargetFps();
    if (frameMs <= 0.0 || frameMs > 1000.0 || target <= 0.0f)
        return;

    // The same feed as R3 with "Use target as cap": the real frame interval and the part of it ILION's limiter slept
    // (never counted as render time), the target, the cap or the FPS limit, the maximum and the count in use. No 1X:
    // eOn with no frames to generate is not the game's DLSSG off
    auto config = Config::Instance();
    const bool cap = TargetAsCap();
    if (cap != lastCap)
        controller.Restart();
    lastCap = cap;

    int next = controller.Update({ now, frameMs, sleptMs, (double) target, 0.0,
                                   cap ? (double) target : (double) config->FramerateLimit.value_or_default(), maxCount,
                                   asked, 1, 1 });
    next = std::clamp(next, 1, maxCount);

    if (next != asked || now - logMs >= 5000.0)
    {
        static constexpr const char* reasons[] = { "", "up", "down", "try down", "undo", "no gain, back" };
        const auto reason = controller.LastReason();
        spdlog::log(next != asked ? spdlog::level::info : spdlog::level::debug,
                    __FUNCTION__ " ILION Dynamic (game's DLSSG) {}X{}{}: estimated output {:.1f} fps, target {:.1f} "
                                 "({}), slept {:.2f} ms of {:.2f} ms, max {}X",
                    asked + 1, next != asked ? std::format(" -> {}X", next + 1) : std::string(),
                    reason != XeFG_DynamicCount::Reason::None ? std::format(" ({})", reasons[(int) reason])
                                                              : std::string(),
                    controller.PresentedFps(), (double) target, cap ? "cap" : "no cap", sleptMs, frameMs,
                    maxCount + 1);
        logMs = now;
    }

    if (next != asked)
    {
        ilionCount.store(next);
        ilionPending.store(true);
        askedAtMs = now;
    }
}

void ApplyPendingExternalCount()
{
    // Outside the NGX evaluation: slDLSSGSetOptions with the game's last options, Streamline_Hooks puts in the count
    if (ilionPending.load() && ilionPending.exchange(false) && Current() == Provider::DlssgGame)
        StreamlineHooks::updateDlssgOptions();
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
        s.requested = s.multiplier;
        s.appliedSource = AppliedSource::Own;
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
        // Asked for (DLSSG.MultiFrameCount), measured from the presents, and the backend's own count (reported, not a
        // multiplier for sure); only a measured count confirms the generation
        int requested = 0;
        AppliedSource source = AppliedSource::None;
        const int applied = ExternalApplied(&requested, &source);
        if (requested > 0)
        {
            s.requested = requested;
            s.multiplier = applied;
            s.appliedSource = source;
            s.gen = source == AppliedSource::Measured ? Gen::Confirmed : Gen::Detected;
            s.reported = ExternalReported();

            // ILION v1.4.0 fase 2: XeMFG XMX whose last XeFG context failed (its log) generates nothing: presents counted
            // per real frame are Streamline's slots, not generated frames
            if (state.activeFgNvngx == FGNvngxReplacement::XeMfgXmx && s.gen == Gen::Confirmed &&
                Nvngx_XeMfgXmx::ReadBackend().ContextFailed())
                s.gen = Gen::Detected;
        }
        else
        {
            s.gen = Gen::Unknown;
        }

        // Real frames: the first DLSSG evaluation of each rendered frame (MultiFrameIndex 1)
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

    // Measured multiplier of the game's DLSSG: the presents are the final frames, their rate is the final rate.
    // Otherwise the final rate is base x the multiplier asked for, an estimate
    if (const float presentFps = measuredPresentFps.load();
        s.provider == Provider::DlssgGame && s.appliedSource == AppliedSource::Measured && presentFps > 0.0f)
    {
        s.finalFps = presentFps;
        s.finalMeasured = true;
    }
    else if (s.baseFps > 0.0f && s.multiplier > 0 && (s.provider == Provider::DlssgGame || !s.finalMeasured))
    {
        s.finalFps = s.baseFps * (float) s.multiplier;
    }

    // The game's DLSSG: whose the target is. In eDynamic the backend's own controller; with a fixed multiplier ILION's
    // cap, unless the loop guard suspended it
    if (s.provider == Provider::DlssgGame)
    {
        s.backendDynamic = BackendDynamic();
        s.ilionDrives = IlionDrivesExternal();
        s.ilionRefused = IlionExternalRefused();
        if (TargetAsCap() && TargetFps() > 0.0f && DynamicEnabled())
        {
            s.capByBackend = s.backendDynamic;
            s.capSuspended = !s.backendDynamic && !s.ilionDrives && ExternalCapSuspended();
        }
    }

    // The limiter FG_Hooks / the wrapped swapchain choose: a target cap is always OptiScaler's own (it knows the
    // multiplier); else Reflex, else XeLL, else OptiScaler's
    s.limitFps = FrameLimit::targetFps();
    const bool externalCapWaits = s.provider == Provider::DlssgGame && FrameLimit::targetCapActive() &&
                                  ExternalCapRealFps() <= 0.0f;
    if (externalCapWaits)
    {
        // The cap waits for a confirmed multiplier (ExternalCapRealFps): what limits now is the plain FPS limit
        s.limitFps = FrameLimit::outsideLimitFps();
        s.capWaiting = true;
        if (state.reflexLimitsFps)
            s.limiter = Limiter::Reflex;
        else if (XellHooks::canLimit())
            s.limiter = Limiter::XeLL;
        else if (s.limitFps > 0.0f)
            s.limiter = Limiter::OptiScaler;
    }
    else if (FrameLimit::targetCapActive())
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
