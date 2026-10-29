#pragma once

// ILION dev.56: Adaptive NR as a ladder of levels per profile. A level is the NR resolution, the model interval (the
// network runs on 1 of every N frames; 1 = every frame) and lmxxf's Fast network. dev.55 moved only the resolution,
// which costs less than the user expected: in Cyberpunk (RX 7900 XTX) the interval 2 gave far more FPS than any
// resolution step, so the profiles hardly differed. The interval is read every frame (no rebuild), the resolution and
// Fast rebuild the network.
// The stepping rules are AMDNR's dynamic NR ones (dlssnr/amd/DynamicNr.h): down after 1.5 s over target x 1.08, up
// after 5 s under target x 0.85, a step down within 10 s of a step up blocks up-steps 60 s, a level left over this
// target is not tried again for 5 min, lmxxf without its import pool stops after 12 changes. Pure, so it is tested
// outside the game (ILION-V1.2-dev-trabajo\pruebas\perfiles-nr\probar-dev56.cmd).

#include <misc/IlionDynamicNrReason.h> // Reason, Name

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace IlionAdaptiveNr
{
struct Level
{
    float scale = 1.f;  // NR resolution
    int interleave = 1; // the model on 1 of every N frames
    bool fast = false;  // lmxxf's Fast network (one size tier lower)
};

inline bool Same(const Level& a, const Level& b)
{
    return std::fabs(a.scale - b.scale) < 0.001f && a.interleave == b.interleave && a.fast == b.fast;
}

// A rough relative cost, only to keep about the same cost when the ladder changes (a profile switch)
inline double Cost(const Level& l) { return l.scale * l.scale * (l.fast ? 0.8 : 1.0) / (std::max)(l.interleave, 1); }

enum class Profile
{
    Custom,   // a target of the ini or of the Advanced slider: the resolution steps only, as before dev.55
    Quality,  // 40 FPS
    Balanced, // 50 FPS
    Smooth,   // 60 FPS
};

inline Profile ProfileOf(int targetFps)
{
    return targetFps == 40 ? Profile::Quality : targetFps == 50 ? Profile::Balanced : targetFps == 60 ? Profile::Smooth : Profile::Custom;
}

// The profile's levels, from the best image to the cheapest. ceiling: the manual NR resolution (no level above it);
// userInterleave: the menu's own interval (no level below it); lmxxf: Fast exists only on the lmxxf runtime.
// Levels that become equal after that are merged.
inline std::vector<Level> Ladder(Profile p, float ceiling, int userInterleave, bool lmxxf)
{
    std::vector<Level> raw;
    switch (p)
    {
    case Profile::Quality: // image first: resolution 85% at least, the interval only as the last step
        raw = { { 1.f, 1, false }, { 0.85f, 1, false }, { 0.85f, 2, false } };
        break;
    case Profile::Balanced: // the interval first, then the resolution down to 58%
        raw = { { 1.f, 1, false }, { 1.f, 2, false }, { 0.85f, 2, false }, { 0.7f, 2, false }, { 0.58f, 2, false } };
        break;
    case Profile::Smooth: // FPS first: interval 2 then 3, the Fast network, the resolution down to 50%
        raw = { { 1.f, 1, false },  { 1.f, 2, false },  { 0.85f, 2, true },
                { 0.7f, 3, true },  { 0.58f, 3, true }, { 0.5f, 3, true } };
        break;
    default:
        raw = { { 1.f, 1, false }, { 0.85f, 1, false }, { 0.7f, 1, false }, { 0.58f, 1, false }, { 0.5f, 1, false } };
        break;
    }

    std::vector<Level> out;
    for (Level l : raw)
    {
        l.scale = (std::min)(l.scale, ceiling);
        l.interleave = (std::max)(l.interleave, userInterleave);
        l.fast = l.fast && lmxxf;
        if (out.empty() || !Same(out.back(), l))
            out.push_back(l);
    }
    return out;
}

inline std::string Describe(const Level& l)
{
    std::string s = std::to_string((int) std::lround(l.scale * 100.f)) + "%";
    if (l.interleave > 1)
        s += " x" + std::to_string(l.interleave);
    if (l.fast)
        s += " Fast";
    return s;
}

struct Controller
{
    static constexpr unsigned long long kDownDwellMs = 1500, kUpHeadroomMs = 5000, kBounceWindowMs = 10000,
                                        kBounceBlockMs = 60000, kFailMemoryMs = 300000;
    static constexpr unsigned kLmxxfChangeCap = 12;

    std::vector<Level> ladder;
    std::vector<double> failedMs;
    std::vector<unsigned long long> failedAt;
    int step = 0;
    double emaMs = 0;
    unsigned long long lastChange = 0, headroomSince = 0, upBlockedUntil = 0;
    bool lastWasUp = false, capSaid = false;
    int lastTarget = 0;
    unsigned changes = 0;

    Level Current() const { return ladder.empty() ? Level {} : ladder[step]; }
    int Last() const { return ladder.empty() ? 0 : (int) ladder.size() - 1; }
    float Lowest() const { return ladder.empty() ? 1.f : ladder.back().scale; }

    bool StepFails(int s, unsigned long long t, double target) const
    {
        return s >= 0 && s < (int) failedAt.size() && failedAt[s] != 0 && t - failedAt[s] < kFailMemoryMs &&
               failedMs[s] > target * 1.08;
    }

    // A new ladder (a profile switch, the ceiling or the menu's interval changed): continue at the level that costs
    // about the same as the one running, so a switch is one change at most. Returns true when the level changed.
    // held: lmxxf's change cap is reached (an old runtime that keeps memory per size change): nothing changes.
    bool SetLadder(std::vector<Level> next, bool held = false)
    {
        if (held && !ladder.empty())
            return false;
        if (next.empty())
            next.push_back(Level {});

        bool same = next.size() == ladder.size();
        for (size_t i = 0; same && i < next.size(); i++)
            same = Same(next[i], ladder[i]);
        if (same)
            return false;

        const Level before = Current();
        int pick = 0;
        if (!ladder.empty())
        {
            const double cost = Cost(before);
            double best = 1e9;
            for (int i = 0; i < (int) next.size(); i++)
            {
                const double d = std::fabs(Cost(next[i]) - cost);
                if (d < best - 1e-9)
                {
                    best = d;
                    pick = i;
                }
            }
        }

        const bool hadLadder = !ladder.empty();
        ladder = std::move(next);
        failedMs.assign(ladder.size(), 0.0);
        failedAt.assign(ladder.size(), 0);
        step = pick;
        upBlockedUntil = 0;
        headroomSince = 0;
        return hadLadder && !Same(before, Current());
    }

    // t: a millisecond tick; dtMs: this frame's time from a precise clock (0 = unknown); settling: no NR on this
    // frame. Returns the level to run; `log` gets a line when it changes, or once when the change cap holds it.
    Level Update(unsigned long long t, double dtMs, bool settling, int targetFps, bool capChanges, std::string& log)
    {
        if (ladder.empty())
            SetLadder({});
        if (!settling && dtMs > 0 && dtMs < 200) // ignore hitches, pauses and menu time
            emaMs = emaMs > 0 ? emaMs * 0.9 + dtMs * 0.1 : dtMs;
        if (targetFps != lastTarget)
        {
            lastTarget = targetFps;
            upBlockedUntil = 0;
            headroomSince = 0;
        }

        const double target = 1000.0 / (std::max)(targetFps, 1);
        const bool over = emaMs > target * 1.08, room = emaMs > 0 && emaMs < target * 0.85;
        if (!room)
            headroomSince = 0;
        else if (!headroomSince)
            headroomSince = t;

        const bool capped = capChanges && changes >= kLmxxfChangeCap;
        const int before = step;
        if (emaMs > 0 && !settling && !capped && t - lastChange > kDownDwellMs)
        {
            if (over && step < Last())
            {
                if (lastWasUp && t - lastChange < kBounceWindowMs)
                    upBlockedUntil = t + kBounceBlockMs;
                failedMs[step] = emaMs;
                failedAt[step] = t;
                ++step;
                lastWasUp = false;
            }
            else if (room && step > 0 && t - headroomSince >= kUpHeadroomMs && t >= upBlockedUntil &&
                     !StepFails(step - 1, t, target))
            {
                --step;
                lastWasUp = true;
            }
        }

        if (step != before)
        {
            lastChange = t;
            headroomSince = 0;
            ++changes;
            log = "Adaptive NR: " + Describe(ladder[before]) + " -> " + Describe(ladder[step]) + " (average " +
                  std::to_string(emaMs).substr(0, 5) + " ms, target " + std::to_string(target).substr(0, 5) + " ms" +
                  (upBlockedUntil > t ? "; a step up could not hold, up-steps wait 60 s" : "") + ")";
        }
        else if (capped && !capSaid)
        {
            capSaid = true;
            log = "Adaptive NR: held at " + Describe(Current()) + " for this session after " + std::to_string(changes) +
                  " changes - this LmxxfNrRuntime.dll keeps about 100 MB of VRAM and RAM per NR size change until the "
                  "game restarts";
        }
        return Current();
    }
};

// Why it keeps, lowers or raises: the same reasons as AMDNR's controller (misc/IlionDynamicNrReason.h)
inline IlionDynamicNr::Reason Explain(const Controller& c, unsigned long long t, bool settling, int targetFps, bool capped)
{
    using IlionDynamicNr::Reason;
    if (settling)
        return Reason::Settling;
    if (c.emaMs <= 0.0)
        return Reason::Measuring;

    const double target = 1000.0 / (std::max)(targetFps, 1);
    const bool over = c.emaMs > target * 1.08;
    const bool room = c.emaMs < target * 0.85;

    if (capped && (over || (room && c.step > 0)))
        return Reason::Held;
    if (over)
        return c.step >= c.Last() ? Reason::AtMinimum : Reason::Lowering;
    if (room && c.step > 0)
    {
        if (c.StepFails(c.step - 1, t, target))
            return Reason::StepAboveFails;
        return t < c.upBlockedUntil ? Reason::UpBlocked : Reason::WaitingRoom;
    }
    return c.step == 0 ? Reason::MetAtMaximum : Reason::Met;
}
} // namespace IlionAdaptiveNr
