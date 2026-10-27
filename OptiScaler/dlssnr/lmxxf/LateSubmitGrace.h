// Copyright (c) 2026 3zwr1 (AMDNR). Part of AMDNR (GPL-3.0; see Licenses/AMDNR_NOTICE.txt).
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// AMDNR 0.3.4 (P3, The Last of Us Part II): what lmxxf's Record does with a job whose neural list the game has not
// submitted yet. Pure rules, header-only, standard types only: LmxxfBackend.cpp uses them and
// tests/034/late_submit_grace_test.cpp runs the sequences.
//
// The runtime holds one job at a time. RecordInputs puts the job's input copies on the game's list and the job is
// launched when that list reaches ExecuteCommandLists (Backend::Submitted). Some games submit frame N's list only
// after frame N+1's Evaluate (TLOU II; likely other job-system engines). 0.3.3.2 cancelled such a job at the top of the
// next Record and restarted the carried edit and the network history: in TLOU II 5,860 of 31,704 jobs ran and the
// history restarted about every other frame, on fill frames under Model interleave too.
//
// [DlssNr] AmdLateSubmitGrace (default true; false = exactly 0.3.3.2):
//   - Grace: the Record right after the feed (the list is late by one frame so far) keeps the job and feeds nothing
//     itself (a fill frame, as under Model interleave). The job launches when its list arrives; its answer is consumed
//     one Record later than usual, measured against a copy of the frame it was fed and carried one frame further by
//     that frame's motion (LmxxfBackend.cpp, step 1). So a fill frame never cancels a job that is one frame late.
//   - Still unsubmitted at the Record after that (late by two): dropped exactly as in 0.3.3.2 (cancel, history reset).
//   - A job something explained (the game reset or discarded its list, or OptiScaler's bridge dropped it) and a frame
//     or network size change keep 0.3.3.2's drop at once: that list will not bring our inputs, or the textures the job
//     was fed are being replaced.
//   - A graced job's answer is not used when a reset came between its feed and its consume (a camera cut, NR switched
//     on again, an encoding change): it answers for the picture before it.

namespace Lmxxf
{
enum class PendingJobAction
{
    Grace, // keep the job; this Record feeds nothing
    Drop   // cancel it and restart the history (0.3.3.2)
};

struct PendingJobFacts
{
    bool graceOn = true;                     // [DlssNr] AmdLateSubmitGrace
    bool resetSeen = false;                  // ListReset reported the pending list (reset or discarded by the game)
    bool resetCertain = false;               // OptiScaler's own bridge dropped it
    bool sizeChanges = false;                // this Record reallocates for a new frame or network size
    bool graced = false;                     // the job already had its frame of grace
    unsigned long long recordsSinceFeed = 1; // Records since the one that fed the job: 1 = the next one
};

// The decision at the top of Record for a job that is still recorded but not submitted.
inline PendingJobAction DecidePendingJob(const PendingJobFacts& f)
{
    if (!f.graceOn || f.resetSeen || f.resetCertain || f.sizeChanges || f.graced || f.recordsSinceFeed != 1)
        return PendingJobAction::Drop;
    return PendingJobAction::Grace;
}

// Whether a graced job's answer is used when it is consumed (one Record later than usual).
inline bool UseLateAnswer(bool resetSinceFeed) { return !resetSinceFeed; }

// How late a neural list reached ExecuteCommandLists, in frames: the Records that finished after its own one.
// `recordsDone` counts the Records finished so far, `fedAt` is the index of the Record that fed it (the backend's
// `frames` then), so a list submitted before the next Record is late by 0.
inline unsigned long long FramesLate(unsigned long long recordsDone, unsigned long long fedAt)
{
    return recordsDone > fedAt + 1 ? recordsDone - fedAt - 1 : 0;
}

// A dropped list's submission is counted as late (for the log and the menu) up to this many frames late; after that
// the pointer is forgotten, since the game may reuse the list object for a later frame.
inline constexpr unsigned long long kLateWindow = 2;
inline bool CountsAsLate(unsigned long long framesLate) { return framesLate >= 1 && framesLate <= kLateWindow; }
} // namespace Lmxxf
