#pragma once
#include "SysUtils.h"

class FrameLimit
{
    static int timer_sleep(int64_t hundred_ns);
    static int busywait_sleep(int64_t ns);
    static int combined_sleep(int64_t ns);
    // The deadline pacing every limiting place shares (R4); each place keeps its own deadline. Returns ms slept
    static double waitUntilDue(uint64_t& previous_deadline, uint64_t min_interval_us);

  public:
    static bool xeFGDynamicActive();
    // What OptiScaler's own limiter holds for ILION's FG, in final (presented) frames: the target cap when one is
    // active, else the FPS limit
    static float targetFps();
    // What Reflex / XeLL get: the FPS limit only. They pace the game's real frames and don't know every generator's
    // multiplier, so a target cap is held by OptiScaler's limiter instead (targetCapActive)
    static float outsideLimitFps();
    // A target cap is active: OptiScaler's limiter applies it, also where Reflex or XeLL would limit
    static bool targetCapActive();
    // Time the last sleeps of this limiter took, for Dynamic MFG's wait (read and cleared)
    static double takeSleepMs();
    // ILION's FG present (fgActive) or the wrapped swapchain's present (plain FPS limit)
    static void sleep(bool fgActive);
    // The game's DLSSG (DLSS Enabler...) with a target cap: once per real frame (first DLSSG evaluation), with the
    // frames that real frame generates. Returns the time slept, in ms
    static double sleepExternalFg(int framesToGenerate);
};
