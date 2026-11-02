#pragma once
#include "SysUtils.h"

class FrameLimit
{
    static int timer_sleep(int64_t hundred_ns);
    static int busywait_sleep(int64_t ns);
    static int combined_sleep(int64_t ns);

  public:
    static bool xeFGDynamicActive();
    // What OptiScaler's own limiter holds, in final (presented) frames: the target cap when one is active, else the
    // FPS limit
    static float targetFps();
    // What Reflex / XeLL get: the FPS limit only. They pace the game's real frames and don't know every generator's
    // multiplier, so a target cap is held by OptiScaler's limiter instead (targetCapActive)
    static float outsideLimitFps();
    // A target cap is active: OptiScaler's limiter applies it, also where Reflex or XeLL would limit
    static bool targetCapActive();
    // Time the last sleeps of this limiter took, for Dynamic MFG's wait (read and cleared)
    static double takeSleepMs();
    static void sleep(bool fgActive);
};
