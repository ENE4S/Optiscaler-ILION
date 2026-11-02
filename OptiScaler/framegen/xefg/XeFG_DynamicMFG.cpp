#include "pch.h"
#include "XeFG_DynamicMFG.h"

#include <Config.h>
#include <State.h>
#include <low_latency/input/input_common.h>

XeFG_DynamicMFG::Result XeFG_DynamicMFG::Update(int targetCount, int framesToInterpolate, bool generationOff,
                                                double presentWaitMs, HWND window)
{
    auto config = Config::Instance();
    auto& state = State::Instance();

    Result result;
    result.dynamic = Allowed();
    result.ownChange = result.dynamic && _active;
    result.count = targetCount;

    if (result.dynamic)
    {
        if (!_active)
        {
            // A newly selected Dynamic mode must not spend its settling window generating at the old manual maximum.
            _count.Restart();
            _cap.Reset();
            _capActive = false;
            _unfocused = false;
            state.xefgDynamicCapFps = 0.0f;
            InputCommon::take_sleep_ms();
            _active = true;
            result.ownChange = true; // Apply live; no WAR context reset on entry.
            result.count = 1; // SDK generated count: one generated frame = 2X.
            result.generationOff = false;
            return result;
        }
        const auto now = Util::MillisecondsNow();
        const auto target = TargetFps(now, window);
        const int count1 = framesToInterpolate >= 1 ? std::min(framesToInterpolate, targetCount) : targetCount;
        const int current = generationOff ? 0 : count1;
        const auto refresh = DisplayRefreshFps(now, window);

        // What the game waited in OptiScaler: the low latency / FPS limiter sleep and the output's present path
        const double sleepMs = InputCommon::take_sleep_ms();

        // In the background the game is throttled: keep the count, start over once it is back in front
        const int focus = Focused(window);

        if (focus == 0 || (focus < 0 && _unfocused))
        {
            if (!_unfocused)
                LOG_INFO("Dynamic MFG: the game's window lost focus, keeping {}X", current + 1);

            _unfocused = true;
            _active = true;
            result.generationOff = generationOff;
            result.count = count1;
            return result;
        }

        if (_unfocused)
        {
            LOG_INFO("Dynamic MFG: the game's window is back in front, starting over at {}X", current + 1);
            _unfocused = false;
            _count.Reset();
            _cap.Reset();
            _capActive = false;
        }
        const double waitMs = sleepMs + presentWaitMs;
        const int rounding = std::clamp(config->FGXeFGRoundingDMFG.value_or_default(), 0, 2);
        const int minCount = config->FGXeFGDynamicAllow1X.value_or_default() || targetCount == 1 ? 0 : 1;

        // Use target as cap: every OptiScaler limiter holds the presented frame rate at the target
        // (FrameLimit::targetFps), the target is at most the FPS limit. Without 1X the cap needs 3X to choose from.
        const bool cap = config->FGXeFGTargetAsCapDMFG.value_or_default() && target > 0.0f && targetCount > 1;
        state.xefgDynamicCapFps = cap ? target : 0.0f;

        int count = current;

        // R3: the same cost-aware hysteresis drives target-only and the optional existing cap.
        if (_capActive != cap)
            _count.Restart();
        count = _count.Update({ now, state.lastFGFrameTime, waitMs, target, refresh,
                               cap ? target : config->FramerateLimit.value_or_default(), targetCount, current,
                               cap ? 1 : rounding, cap ? 1 : minCount });

        _capActive = cap;

        if (count != current || now - _logMs >= 5000.0)
        {
            static constexpr const char* reasons[] = { "", "up", "down", "try down", "undo", "no gain, back" };
            static constexpr const char* roundings[] = { "at most", "around", "at least" };
            const auto reason = _count.LastReason();
            const auto change = count != current ? std::format(" -> {}X", count + 1) : std::string();
            const auto why =
                reason != XeFG_DynamicCount::Reason::None ? std::format(" ({})", reasons[(int) reason]) : std::string();

            // Changes at info level, the state every 5 s at debug level
            spdlog::log(count != current ? spdlog::level::info : spdlog::level::debug,
                        __FUNCTION__ " Dynamic MFG R3 {}X{}{}: estimated output {:.1f} fps, target {:.1f} (set {:.1f}, "
                                     "rounding {}, {}, refresh {:.0f}), one step less {:.1f}, waits {:.0f}%, "
                                     "frame cost {:.2f} ms, back-off {:.0f} s",
                        current + 1, change, why, _count.PresentedFps(), _count.TargetFps(), target,
                        roundings[rounding], _count.Uncapped() ? "no cap" : "capped", refresh, _count.LowerFps(),
                        _count.WaitShare() * 100.0, _count.FrameCostMs(), _count.BackoffMs() / 1000.0);

            _logMs = now;
        }

        // 1X: frame generation off, the output keeps its count
        result.generationOff = count == 0;
        result.count = count >= 1 ? count : count1;
    }
    else if (_active)
    {
        _count.Reset();
        _cap.Reset();
        _capActive = false;
    }

    // The cap is set every frame Dynamic runs, also a new FG context mustn't inherit it
    if (!result.dynamic)
    {
        state.xefgDynamicCapFps = 0.0f;
        state.lastFGPresentMs = 0.0;
        InputCommon::take_sleep_ms();
    }

    _active = result.dynamic;
    return result;
}

bool XeFG_DynamicMFG::Allowed()
{
    auto config = Config::Instance();
    auto& state = State::Instance();
    return state.activeFgOutput == FGOutput::XeFG && config->FGXeFGDynamicMFG.value_or_default() &&
           !config->FGFollowsGame() && state.swapchainInteropApi != SwapchainInteropApi::VkwDx12;
}

void XeFG_DynamicMFG::Restart()
{
    _active = false;
    _count.Restart();
    _cap.Reset();
}

int XeFG_DynamicMFG::Focused(HWND window)
{
    // Unknown window: nothing to tell, keep deciding
    if (window == nullptr || !IsWindow(window))
        return 1;

    if (IsIconic(GetAncestor(window, GA_ROOT)))
        return 0;

    const auto foreground = GetForegroundWindow();

    if (foreground == nullptr)
        return -1;

    if (GetAncestor(foreground, GA_ROOT) == GetAncestor(window, GA_ROOT))
        return 1;

    // Any window of the game's process counts, a game can present to one window while another has the focus
    DWORD pid = 0;
    GetWindowThreadProcessId(foreground, &pid);
    return pid == GetCurrentProcessId() ? 1 : 0;
}

float XeFG_DynamicMFG::DisplayRefreshFps(double nowMs, HWND window)
{
    // The window can move to another display, look again every 2 seconds
    if (window != nullptr && (_refreshRate <= 0 || nowMs - _refreshCheckedMs >= 2000.0))
    {
        _refreshCheckedMs = nowMs;
        _refreshRate = Util::GetActiveRefreshRate(window);
    }

    return (float) std::max(_refreshRate, 0);
}

float XeFG_DynamicMFG::TargetFps(double nowMs, HWND window)
{
    // OptiScaler's FPS limit counts the presented frames, a target above it can't be reached
    const auto limit = Config::Instance()->FramerateLimit.value_or_default();

    if (auto target = Config::Instance()->FGXeFGFramerateTargetDMFG.value_or_default(); target > 0.0f)
        return limit > 0.0f ? std::min(target, limit) : target;

    if (limit > 0.0f)
        return limit;

    // Just under the refresh rate (116 at 120 Hz), where a VRR display's frame cap sits
    const auto refresh = DisplayRefreshFps(nowMs, window);
    return refresh > 0.0f ? std::floor(refresh - refresh * refresh / 3600.0f) : 0.0f;
}
