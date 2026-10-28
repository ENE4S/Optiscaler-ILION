#pragma once

// ILION dev.51-exp: why AMDNR's dynamic NR resolution controller (dlssnr/amd/DynamicNr.h) lowers, raises or keeps the
// NR resolution, read from its own state after its update of the frame. The rules are the controller's, repeated
// here only to name them (it decides nothing): over the target = average above target x 1.08, room = below target x
// 0.85; down after 1.5 s over, up after 5 s with room, up-steps blocked 60 s after a step up that could not hold,
// lmxxf held after its change cap. Pure, so it is tested beside the controller.

#include <dlssnr/amd/DynamicNr.h>

#include <algorithm>

namespace IlionDynamicNr
{
enum class Reason : int
{
    Settling,     // the bridge's settle window: no NR on these frames, the average is not fed
    Measuring,    // no frame time average yet
    MetAtMaximum, // target met at the maximum NR resolution: nothing to lower
    Met,          // target met with the resolution lowered; not enough room to go back up
    Lowering,     // over the target: the next step down comes after 1.5 s over it
    AtMinimum,    // over the target at the lowest step: the network can't get smaller
    WaitingRoom,  // room to spare: a step up comes after 5 s with room
    UpBlocked,    // room to spare, but a step up could not hold a moment ago: up-steps wait 60 s
    Held,         // lmxxf reached its change cap this session: no more changes until the game restarts
    StepAboveFails // room to spare, but the step above was over this target not long ago (dev.51-exp2: 5 min)
};

inline Reason Explain(const DlssNr::DynamicNr::Controller& c, unsigned long long t, bool settling, int targetFps,
                      float ceiling, bool capped)
{
    using Controller = DlssNr::DynamicNr::Controller;

    if (settling)
        return Reason::Settling;

    if (c.emaMs <= 0.0)
        return Reason::Measuring;

    const double target = 1000.0 / (std::max)(targetFps, 1);

    // The step that maps to the ceiling, as the controller finds it
    int first = 0;
    while (first < Controller::kLast && Controller::kSteps[first + 1] >= ceiling - 0.001f)
        ++first;

    const bool over = c.emaMs > target * 1.08;
    const bool room = c.emaMs < target * 0.85;

    if (capped && (over || (room && c.step > first)))
        return Reason::Held;

    if (over)
        return c.step >= Controller::kLast ? Reason::AtMinimum : Reason::Lowering;

    if (room && c.step > first)
    {
        if (c.StepFails(c.step - 1, t, target))
            return Reason::StepAboveFails;
        return t < c.upBlockedUntil ? Reason::UpBlocked : Reason::WaitingRoom;
    }

    return c.step <= first ? Reason::MetAtMaximum : Reason::Met;
}

// English text for the log (the menu translates its own)
inline const char* Name(Reason r)
{
    switch (r)
    {
    case Reason::Settling:
        return "settling, no NR on these frames";
    case Reason::Measuring:
        return "measuring";
    case Reason::MetAtMaximum:
        return "target met at the maximum NR resolution, keeps it";
    case Reason::Met:
        return "target met, keeps the resolution (no 15% room to go up)";
    case Reason::Lowering:
        return "over the target, steps down after 1.5 s over it";
    case Reason::AtMinimum:
        return "over the target at the minimum, can't go lower";
    case Reason::WaitingRoom:
        return "room to spare, steps up after 5 s with room";
    case Reason::UpBlocked:
        return "room to spare, up-steps blocked 60 s after one that could not hold";
    case Reason::Held:
        return "held: lmxxf change cap reached this session";
    case Reason::StepAboveFails:
        return "room to spare, but the step above missed this target less than 5 min ago: stays";
    }
    return "?";
}
} // namespace IlionDynamicNr
