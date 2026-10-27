// Copyright (c) 2026 3zwr1 (AMDNR). Part of AMDNR (GPL-3.0; see Licenses/AMDNR_NOTICE.txt).
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Dynamic NR resolution controller (0.3.3.2 rebuild, #33). Steps down after 1.5 s over the target, up only after 5 s
// with room to spare; a step down within 10 s of a step up is a bounce and blocks up-steps for 60 s. Frame time from a
// precise clock (the caller), settle-window frames left out. With capChanges (lmxxf while its runtime still re-imports
// on every rebuild) it holds after kLmxxfChangeCap changes. No D3D, so it can be simulated.
//
// What it replaces (AmdBridge.cpp, 0.3.3.2 first build): a symmetric 1.5 s dwell with the same 1.08x / 0.85x thresholds
// ping-ponged whenever the model cost more than about 80% of the frame; GetTickCount64 (10-16 ms resolution) read at
// least 15.6 ms above about 64 FPS, so 90/120/144 FPS targets walked down to 50%; the settle frames (no NR, cheaper) fed
// the average; and steps below the ceiling were counted as changes that did nothing.
#include <algorithm>
#include <cmath>
#include <string>

namespace DlssNr::DynamicNr
{
struct Controller
{
    static constexpr float kSteps[5] = { 1.0f, 0.85f, 0.7f, 0.58f, 0.5f };
    static constexpr int kLast = 4;
    static constexpr unsigned long long kDownDwellMs = 1500, kUpHeadroomMs = 5000, kBounceWindowMs = 10000,
                                        kBounceBlockMs = 60000;
    static constexpr unsigned kLmxxfChangeCap = 12; // only while lmxxf leaks per rebuild (no import pool)
    int step = 0;
    double emaMs = 0;
    unsigned long long lastChange = 0, headroomSince = 0, upBlockedUntil = 0;
    bool lastWasUp = false, capSaid = false;
    int lastTarget = 0;
    unsigned changes = 0;

    // t: a millisecond tick (any clock; only differences are used). dtMs: this frame's time from a precise clock, 0 when
    // unknown. settling: the bridge's settle window (no NR on the frame). ceiling: the manual NR resolution. Returns the
    // NR scale for this frame; `log` gets a line when the step changes, or once when the change cap holds it.
    float Update(unsigned long long t, double dtMs, bool settling, int targetFps, float ceiling, bool capChanges,
                 std::string& log)
    {
        if (!settling && dtMs > 0 && dtMs < 200) // ignore hitches, pauses and menu time
            emaMs = emaMs > 0 ? emaMs * 0.9 + dtMs * 0.1 : dtMs;
        if (targetFps != lastTarget)
        {
            lastTarget = targetFps;
            upBlockedUntil = 0;
            headroomSince = 0;
        }
        const double target = 1000.0 / (std::max)(targetFps, 1);
        // steps[first] maps to the ceiling: every step above it would return the same scale (phantom steps).
        int first = 0;
        while (first < kLast && kSteps[first + 1] >= ceiling - 0.001f)
            ++first;
        if (step < first)
            step = first;
        const bool over = emaMs > target * 1.08, room = emaMs > 0 && emaMs < target * 0.85;
        if (!room)
            headroomSince = 0;
        else if (!headroomSince)
            headroomSince = t;
        const bool capped = capChanges && changes >= kLmxxfChangeCap;
        const int before = step;
        if (emaMs > 0 && !settling && !capped && t - lastChange > kDownDwellMs)
        {
            if (over && step < kLast)
            {
                // A step down soon after a step up: the up-step could not hold. Block up-steps for a while.
                if (lastWasUp && t - lastChange < kBounceWindowMs)
                    upBlockedUntil = t + kBounceBlockMs;
                ++step;
                lastWasUp = false;
            }
            else if (room && step > first && t - headroomSince >= kUpHeadroomMs && t >= upBlockedUntil)
            {
                --step;
                lastWasUp = true;
            }
        }
        auto pct = [&](int i) { return std::to_string(int(std::lround(100.0 * (std::min)(kSteps[i], ceiling)))); };
        if (step != before)
        {
            lastChange = t;
            headroomSince = 0;
            ++changes;
            log = "Dynamic NR: " + pct(before) + "% -> " + pct(step) + "% (average " + std::to_string(emaMs).substr(0, 5) +
                  " ms, target " + std::to_string(target).substr(0, 5) + " ms" +
                  (upBlockedUntil > t ? "; a step up could not hold, up-steps wait 60 s" : "") + ")";
        }
        else if (capped && !capSaid)
        {
            capSaid = true;
            log = "Dynamic NR: held at " + pct(step) + "% for this session after " + std::to_string(changes) +
                  " changes - this LmxxfNrRuntime.dll keeps about 100 MB of VRAM and RAM per NR size change until the game "
                  "restarts";
        }
        return (std::min)(kSteps[step], ceiling);
    }
};
} // namespace DlssNr::DynamicNr
