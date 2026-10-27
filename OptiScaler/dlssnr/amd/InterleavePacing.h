// Copyright (c) 2026 3zwr1 (AMDNR). Part of AMDNR (GPL-3.0; see Licenses/AMDNR_NOTICE.txt).
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Interleave pacing.
//
// THE PROBLEM THIS EXISTS FOR, stated precisely, because the obvious reading of it
// is wrong and leads to the wrong fix.
//
// With model interleave at cadence 2 the frames alternate expensive/cheap: the model
// runs on one and the other is filled by reprojection. Measured in Forza that is
// roughly 33 ms and 13 ms. The first instinct is that uneven intervals are themselves
// the judder - they are not. A game that simulates 33 ms of motion and shows it 33 ms
// later is correct; uneven intervals with matching content read as smooth.
//
// What actually breaks is the game's own delta time. Engines step the simulation with
// the PREVIOUS frame's duration, so an oscillating frame time feeds an oscillating dt
// into a frame that will be displayed for the other duration: the 33 ms camera step is
// shown 13 ms later and the 13 ms step is shown 33 ms later. Every frame's motion is
// wrong by the difference, alternating in sign. That is textbook frame-time-oscillation
// judder, and it is why the friend's report was "the image is quite smooth" and "it
// feels really choppy" at the same time. Both halves of that sentence are true, and
// neither is about image quality - which is why nothing in TemporalStability.h could
// ever have fixed it.
//
// WHAT CAN AND CANNOT BE DONE ABOUT IT. A 33 ms frame cannot be made shorter here. The
// only lever is making the cheap frame longer, so the trade is real and continuous:
//
//   target = average work  ->  no pacing possible; the work already takes that long
//   target = model  work   ->  perfectly even, and the frame rate is exactly what you
//                              get with interleave switched off. The entire gain, gone.
//
// So this is a dial, not a switch, and it is off by default. `strength` picks a point
// between those two ends. The useful part is that the win is not geometric: stabilising
// the interval stabilises the engine's dt estimate, which breaks the oscillation the
// judder comes from - and that loop settles well before the fully-even end. Partial
// pacing buys most of the smoothness for a fraction of the frame rate. The menu shows
// the measured numbers live so the trade is visible instead of guessed at.
//
// THREADING. Note() runs on the render thread inside Record; Pace() runs on the present
// thread. Nothing here locks: the present path must never be able to block behind the
// recording path, and a pacer that stalls is worse than no pacer. Everything crossing
// the two is an atomic, and the pacer's own state is touched by Pace() alone.
#include <atomic>
#include <cstdint>
#include <algorithm>


namespace DlssNr::Pacing
{

struct Snapshot
{
    bool active = false;   // pacing is engaged (enabled, and a real split was measured)
    double modelMs = 0;    // measured work time of a frame the model ran on
    double fillMs = 0;     // measured work time of a filled frame
    double targetMs = 0;   // the interval being paced to
    double evenness = 0;   // 0 = untouched sawtooth, 1 = fully even
};

namespace detail
{
// Set by the render thread, read by the present thread.
inline std::atomic<unsigned> g_noteSeq { 0 };     // bumped once per recorded frame
inline std::atomic<bool> g_noteModel { false };   // was the model run on that frame
inline std::atomic<float> g_strength { 0.f };     // 0 = off, 1 = fully even

// Published for the menu. Written by Pace(), read by the UI; plain atomics rather
// than a lock because a torn readout costs a wrong digit for one frame and a lock
// on this path costs a frame.
inline std::atomic<double> g_showModelMs { 0 };
inline std::atomic<double> g_showFillMs { 0 };
inline std::atomic<double> g_showTargetMs { 0 };
inline std::atomic<bool> g_showActive { false };

// Owned by Pace() only.
struct State
{
    uint64_t lastExitNs = 0;    // when the previous Pace() returned, i.e. the frame's start
    uint64_t lastNoteNs = 0;    // when a fresh note last arrived
    double deadlineNs = 0;      // where this frame's present was supposed to land
    double modelNs = 0;         // EMA of model-frame work
    double fillNs = 0;          // EMA of filled-frame work
    unsigned lastSeq = 0;       // the note this pacer has already consumed
    bool logged = false;        // one-shot "this actually engaged" line
};

// How long without a note before the deadline is abandoned. Time, not a count of
// presents: with frame generation there are two presents per rendered frame (more with
// multi-frame generation), so a count would call a perfectly healthy FG session starved
// and keep resetting the deadline it is supposed to be carrying.
constexpr uint64_t kStarveNs = 500'000'000; // 500 ms
inline State& state()
{
    static State s;
    return s;
}

// An EMA slow enough that one hitched frame does not move the target, and fast enough
// to follow a genuine load change within about a second at 60 fps.
constexpr double kEmaAlpha = 0.03;
inline void ema(double& acc, double sample)
{
    acc = acc == 0.0 ? sample : acc + kEmaAlpha * (sample - acc);
}
} // namespace detail

// Called once per recorded frame from the render thread, with whether the model ran.
inline void Note(bool modelRan)
{
    detail::g_noteModel.store(modelRan, std::memory_order_relaxed);
    detail::g_noteSeq.fetch_add(1, std::memory_order_release);
}

// 0 disables. Called from the frame setup that already reads the config.
inline void SetStrength(float strength)
{
    // -1 = automatic (see Pace), 0 = off, 0..1 = manual.
    detail::g_strength.store(std::clamp(strength, -1.f, 1.f), std::memory_order_relaxed);
}

// For the menu's live readout. Free to call every frame.
inline Snapshot Read()
{
    using namespace detail;
    Snapshot s;
    s.active = g_showActive.load(std::memory_order_relaxed);
    s.modelMs = g_showModelMs.load(std::memory_order_relaxed);
    s.fillMs = g_showFillMs.load(std::memory_order_relaxed);
    s.targetMs = g_showTargetMs.load(std::memory_order_relaxed);
    if (s.modelMs > s.fillMs && s.fillMs > 0.0)
    {
        const double avg = 0.5 * (s.modelMs + s.fillMs);
        s.evenness = std::clamp((s.targetMs - avg) / (s.modelMs - avg), 0.0, 1.0);
    }
    return s;
}

// Called from the present path, immediately after Present returns. Safe to call
// unconditionally: it does nothing unless interleave is running and the user asked
// for pacing, and it never sleeps past a deadline another limiter already met.


} // namespace DlssNr::Pacing
