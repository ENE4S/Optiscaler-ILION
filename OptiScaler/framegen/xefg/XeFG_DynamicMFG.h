#pragma once

#include "XeFG_DynamicCount.h"
#include "XeFG_DynamicCap.h"

#include <Windows.h>

// Dynamic MFG for an XeFG output: it passes the wait it measured and applies the result its own way
class XeFG_DynamicMFG
{
  public:
    struct Result
    {
        bool dynamic = false;       // Dynamic MFG runs this frame
        bool ownChange = false;     // it ran last frame too, a count change is its own and not the user's
        int count = 1;              // interpolated frames to use, kept while generation is off
        bool generationOff = false; // 1X: no frames generated
    };

    // Dynamic MFG may run: XeFG output, Dynamic chosen, not following the game, and not on the Vulkan -> D3D12 XeFG
    // bridge (Dynamic is validated on D3D12 swapchains only; there the manual multiplier is used). The one check
    // shared by the controller, XeFG_Dx12, FrameLimit and the menu.
    static bool Allowed();

    // presentWaitMs: what the game waited in the output's present path this frame
    Result Update(int targetCount, int framesToInterpolate, bool generationOff, double presentWaitMs, HWND window);

    // FG back on: the averages start over, what was learned stays
    void Restart();

    // The output kept another count than the one asked for (XeFG rejected it): averages and the pending check start
    // over on the count in use, without the 2X restart of Restart()
    void Resync() { _count.Restart(); }

  private:
    XeFG_DynamicCount _count;
    XeFG_DynamicCap _cap; // with "Use target as cap": holds the target, no rounding band, no 1X
    bool _active = false;
    bool _capActive = false;
    double _logMs = 0.0;
    int _refreshRate = 0;
    double _refreshCheckedMs = 0.0;
    bool _unfocused = false; // the game's window was in the background last frame

    // 1: the game is in front, 0: in the background, -1: no foreground window
    static int Focused(HWND window);
    float DisplayRefreshFps(double nowMs, HWND window);
    float TargetFps(double nowMs, HWND window);
};
