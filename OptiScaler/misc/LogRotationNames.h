// Copyright (c) 2026 3zwr1 (AMDNR). Part of AMDNR (GPL-3.0; see Licenses/AMDNR_NOTICE.txt).
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Names of the kept previous logs ([Log] KeepPreviousLogs, Logger.cpp). Every generation's name matches
// "<stem>.previous*.log", so the report zip (misc/AmdnrReport.h) collects them all. Header-only and free of Windows
// so the unit tests (tests\034) build it without the DLL.
//
// The signatures are fixed (W0-2; a change is a gate request). The bodies are W1-B's: PreviousLogName (FB-L8) and,
// added here at G0 so the unit tests could be written first, SettleLogGate, the FB-L11B log-write dedupe of the
// bridge status.

#include <string>
#include <string_view>

namespace LogRotationNames
{
// The file name (no folder) of previous-log generation `generation`: 0 = the newest previous log, which keeps
// 0.3.3.2's name "<stem>.previous.<writerStem>.log" ("<stem>.previous.log" when `writerStem` is empty). `stem` is
// the log's stem ("OptiScaler"), `writerStem` the stem of the exe that wrote it ("re9"), possibly empty.
// FB-L8 (0.3.4): an older generation n >= 1 is "<stem>.previous-<n>.<writerStem>.log" ("<stem>.previous-<n>.log"),
// e.g. OptiScaler.previous.re9.log, OptiScaler.previous-1.re9.log, OptiScaler.previous-2.re9.log. The '-' after
// "previous" (generation 0 has a '.') keeps every (writer, generation) pair on its own name, so rotating one exe's
// logs never moves another exe's file, whatever dots its name has.
inline std::wstring PreviousLogName(std::wstring_view stem, std::wstring_view writerStem, unsigned generation)
{
    std::wstring name(stem);
    name += L".previous";
    if (generation > 0)
    {
        name += L'-';
        name += std::to_wstring(generation);
    }
    if (!writerStem.empty())
    {
        name += L'.';
        name += writerStem;
    }
    name += L".log";
    return name;
}

// FB-L11B: decides whether a bridge status text (AmdBridge.cpp Message()) is written to amd_bridge.log. Only the log
// write is deduped: Message() keeps storing every text for the menu status exactly as 0.3.3.2 does. Feed every
// Message() text in call order, "" included (the bridge clears the status once per frame before that frame's own
// status). One gate per status; call it under the status mutex. true = write `text` to the log now.
// Contract (W1-B; the tests in tests\034 check it):
//  - "" is never written;
//  - a non-empty text is written when it differs from the last text this gate wrote;
//  - the same text is written again only after a settle ended: two "" calls in a row since that text was last
//    passed in (a whole frame that posted no status).
// Example: "", W, "", W, "", W -> only the first W; then "", "", W -> W again; A, B, A -> all three.
// 0.3.3.2 compared with the previous call's text instead, so the per-frame "" made a settle one line per frame. This
// rule writes a subset of what 0.3.3.2 wrote: a text equal to the previous call's text is never written by either.
struct SettleLogGate
{
    std::string lastText;     // the last text this gate wrote ("" before the first)
    unsigned emptyInARow = 0; // "" calls since a non-empty text was last passed in
    bool settleEnded = false; // two "" in a row since lastText was last passed in: it may be written again

    bool ShouldLog(std::string_view text)
    {
        if (text.empty())
        {
            if (emptyInARow < 2) // saturates: a session of "" frames never wraps it
                ++emptyInARow;
            if (emptyInARow >= 2)
                settleEnded = true;
            return false;
        }

        emptyInARow = 0;
        if (text == lastText && !settleEnded)
            return false; // the same settle (or the same text posted twice in a row): written once already

        lastText.assign(text); // allocates only when a line is written, never on a steady frame
        settleEnded = false;
        return true;
    }
};
} // namespace LogRotationNames
