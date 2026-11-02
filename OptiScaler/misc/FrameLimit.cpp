#include "pch.h"
#include "FrameLimit.h"

#include "Config.h"
#include <State.h>
#include <framegen/IFGFeature.h>
#include <framegen/xefg/XeFG_DynamicMFG.h>
#include <misc/IlionDynamicMfg.h>
// #include "hooks/D3D11Hooks.h"

// https://learn.microsoft.com/en-us/windows/win32/sync/using-waitable-timer-objects
inline int FrameLimit::timer_sleep(int64_t hundred_ns)
{
    static HANDLE timer = CreateWaitableTimerExW(NULL, NULL, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    LARGE_INTEGER due_time;

    due_time.QuadPart = -hundred_ns;

    if (!timer)
        return 1;

    if (!SetWaitableTimerEx(timer, &due_time, 0, NULL, NULL, NULL, 0))
        return 2;

    if (WaitForSingleObject(timer, 1000) != WAIT_OBJECT_0)
        return 3;

    return 0;
};

inline int FrameLimit::busywait_sleep(int64_t ns)
{
    auto current_time = Util::GetTimestamp();
    auto wait_until = current_time + ns;
    while (current_time < wait_until)
    {
        current_time = Util::GetTimestamp();
    }
    return 0;
}

inline int FrameLimit::combined_sleep(int64_t ns)
{
    constexpr int64_t busywait_threshold = 2'000'000; // 2ms
    int status {};
    auto current_time = Util::GetTimestamp();
    if (ns <= busywait_threshold)
        status = busywait_sleep(ns);
    else
        status = timer_sleep((ns - busywait_threshold) / 100);

    if (int64_t sleep_deviation = ns - (Util::GetTimestamp() - current_time); sleep_deviation > 0 && !status)
        status = busywait_sleep(sleep_deviation);

    return status;
}

bool FrameLimit::xeFGDynamicActive() { return XeFG_DynamicMFG::Allowed(); }

float FrameLimit::targetFps()
{
    const auto fpsLimit = Config::Instance()->FramerateLimit.value_or_default();
    // "Use target as cap" of any FG provider (IlionDynamicMfg): re-checked every call, so a stale cap never survives
    // turning it, Dynamic or the generation off. The FPS limit stays the ceiling.
    if (const float cap = IlionDynamicMfg::CapFps(); cap > 0.0f && (fpsLimit <= 0.0f || cap < fpsLimit))
        return cap;
    return fpsLimit;
}

float FrameLimit::outsideLimitFps() { return Config::Instance()->FramerateLimit.value_or_default(); }

bool FrameLimit::targetCapActive() { return targetFps() != outsideLimitFps(); }

static std::atomic<double> limiterSleepMs { 0.0 };

double FrameLimit::takeSleepMs() { return limiterSleepMs.exchange(0.0); }

void FrameLimit::sleep(bool fgActive)
{
    if (auto fpsCap = targetFps(); fpsCap != 0.0f)
    {
        uint64_t min_interval_us = std::clamp((uint64_t) (1'000'000 / fpsCap), 0ULL, 100'000'000ULL);

        // The game's DLSSG generating (DLSS Enabler...) with a target cap: one real frame per batch of presented
        // frames when this limiter sees only the game's frames, 1 when it sees every presented frame (measured)
        if (!fgActive && targetCapActive() && IlionDynamicMfg::Current() == IlionDynamicMfg::Provider::DlssgGame)
            min_interval_us *= (uint64_t) std::max(IlionDynamicMfg::ExternalLimiterFactor(), 1);

        // The limit counts presented frames, the real frames come once per batch: one real frame per generated
        // frames + 1. ILION used a fixed 2 outside Dynamic, so manual 3X/4X presented above the limit. The real count
        // now holds on D3D12 swapchains; the Vulkan -> D3D12 XeFG bridge keeps the fixed 2 until it is validated.
        // A count of 0 is Dynamic's 1X (generation off); other outputs that report 0 (reprojection) keep 2.
        if (fgActive)
        {
            auto& state = State::Instance();
            auto fg = state.currentFG;
            uint64_t factor = 2;

            if (fg != nullptr && state.swapchainInteropApi != SwapchainInteropApi::VkwDx12)
            {
                const auto generated = fg->GetInterpolatedFrameCount();
                factor = generated >= 1 ? generated + 1 : xeFGDynamicActive() ? 1 : 2;
            }

            min_interval_us *= factor;
        }

        // R4: each frame is due one interval after the previous deadline, not after the previous sleep ended, so the
        // overshoot of each sleep no longer adds up (the real rate stayed a little under the limit). A frame more
        // than one interval late (a hitch, a load, a lower limit) starts over from now, so no burst catches up.
        static uint64_t previous_deadline = 0;
        const uint64_t interval_ns = min_interval_us * 1000;
        const uint64_t current_time = Util::GetTimestamp();
        uint64_t deadline = previous_deadline + interval_ns;

        if (previous_deadline == 0 || current_time > deadline + interval_ns)
        {
            deadline = current_time;
        }
        else if (current_time < deadline)
        {
            if (auto res = combined_sleep(deadline - current_time); res)
                LOG_ERROR("Sleep command failed: {}", res);

            // Dynamic MFG counts this as waiting, as it does the low latency sleep (XeFG_DynamicMFG)
            limiterSleepMs.store(limiterSleepMs.load() + (Util::GetTimestamp() - current_time) / 1'000'000.0);
        }

        previous_deadline = deadline;
    }
}
