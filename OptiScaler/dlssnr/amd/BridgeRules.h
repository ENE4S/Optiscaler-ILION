// Copyright (c) 2026 3zwr1 (AMDNR). Part of AMDNR (GPL-3.0; see Licenses/AMDNR_NOTICE.txt).
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Small rules of the AMD bridge (AmdBridge.cpp), header-only so tests\034\bridge_rules_test.cpp can check them without
// the DLL. No D3D.
#include <windows.h>

namespace DlssNr::BridgeRules
{
// (0.3.4, P10) THE SETTLE. After a settings change Run waits 300 ms with no NR (Run returns before Record) so the
// upscaler finishes its own reconfiguration before the model is rebuilt. That reason holds for a change of the
// upscaler's input size, not for an NR scale change alone (the NR resolution slider commits once on release, a
// Preset or NR style sets it once, Dynamic NR steps are seconds apart): 0.3.3.2 paused NR 300 ms on every one of
// them too (TLOU II 456 settle lines, Hogwarts 223, GTA 126). A scale-only change now keeps the history reset and
// the boundary trace but rebuilds at once, unless it comes within the settle time of the previous change (a burst
// of changes, whatever writes them, still settles as before).
enum class SettingsChange
{
    None,
    ScaleOnly, // the NR scale changed, the input size did not
    Input,     // the upscaler's input size changed (with or without the scale)
};
inline SettingsChange ClassifyChange(unsigned prevWidth, unsigned prevHeight, float prevScale, unsigned width,
                                     unsigned height, float scale)
{
    if (prevWidth != width || prevHeight != height)
        return SettingsChange::Input;
    return prevScale != scale ? SettingsChange::ScaleOnly : SettingsChange::None; // != as 0.3.3.2 compared
}
constexpr unsigned long long kSettleMs = 300;
// Whether `change` restarts the settle. `msSincePrevious` = time since the previous change of either kind (a large
// value when there was none).
inline bool NeedsSettle(SettingsChange change, unsigned long long msSincePrevious)
{
    switch (change)
    {
    case SettingsChange::Input: return true;
    case SettingsChange::ScaleOnly: return msSincePrevious < kSettleMs;
    default: return false;
    }
}

// (0.3.4, P22) Whether `process` is this process: the pseudo-handle GetCurrentProcess() returns, or a real handle to
// it (by its process id, or, for a handle without query access, by the kernel object). Another process's handle and
// null are not.
inline bool IsOwnProcess(HANDLE process)
{
    if (process == GetCurrentProcess())
        return true;
    if (!process)
        return false;
    if (const DWORD pid = GetProcessId(process))
        return pid == GetCurrentProcessId();
    // CompareObjectHandles (Windows 10 1607+) is looked up, not imported: kernel32.lib does not carry it.
    using CompareFn = BOOL(WINAPI*)(HANDLE, HANDLE);
    const auto compare =
        reinterpret_cast<CompareFn>(GetProcAddress(GetModuleHandleW(L"kernelbase.dll"), "CompareObjectHandles"));
    return compare && compare(process, GetCurrentProcess()) != FALSE;
}
// Whether a TerminateProcess call is the game ending itself on purpose, which the running marker records as a
// quit (Logger.cpp, FB-L7 stamps): its own process with exit code 0. A non-zero code (UE's crash exit is 3, crash
// handlers pass the exception code) keeps the "no clean exit" warning of the next start.
inline bool SelfTerminateIsQuit(HANDLE process, UINT exitCode) { return exitCode == 0 && IsOwnProcess(process); }
} // namespace DlssNr::BridgeRules
