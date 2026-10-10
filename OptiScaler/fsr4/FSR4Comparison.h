#pragma once

#include <windows.h>
#include <cmath>
#include <cctype>
#include <cstdint>
#include <cstring>

// FSR 4 Comparison Mode
//
// Collects frame time statistics per upscaler dll (Official / Community / Lite) while the user switches
// between them. Everything here runs on the thread that renders the menu, once per presented frame, with
// fixed storage. It only reads values that already exist, nothing about the upscaler is changed.
//
// A frame is only counted when an upscale dispatch was executed since the previous frame and the dll that
// executed it is known, so what is selected in the menu never decides where a sample goes.
class FSR4Comparison
{
  public:
    enum class Status : int
    {
        Waiting,
        Stabilizing,
        Recording,
        Saved,
    };

    struct Box
    {
        const char* label = "";
        Status status = Status::Waiting;

        // Frames that were counted and the sum of their frame times
        uint64_t frames = 0;
        double totalMs = 0.0;

        // Shortest and longest single frame time that was counted
        double minMs = 0.0;
        double maxMs = 0.0;

        // A counted frame had frame generation active
        bool fgSeen = false;

        bool HasResult() const { return frames > 0 && totalMs > 0.0; }

        // Counted frames divided by the time they took
        double AvgFps() const { return HasResult() ? (double) frames * 1000.0 / totalMs : 0.0; }

        // Slowest single frame expressed as fps, not a percentile
        double MinFps() const { return HasResult() && maxMs > 0.0 ? 1000.0 / maxMs : 0.0; }

        // Fastest single frame expressed as fps
        double MaxFps() const { return HasResult() && minMs > 0.0 ? 1000.0 / minMs : 0.0; }
    };

    static constexpr int BoxCount = 3;
    static constexpr double StabilizeMs = 2000.0;
    static constexpr double StallMs = 1000.0;

    static bool Enabled() { return _enabled; }

    // Index of the box of the dll that's executing the upscaling, -1 when it isn't one of the three
    static int CurrentBox() { return _currentBox; }

    // A dll is executing the upscaling but it is not Official, Community or Lite
    static bool OtherDllActive() { return _currentModule != nullptr && _currentBox < 0; }

    static const Box& GetBox(int index) { return _boxes[index]; }

    static void SetEnabled(bool enabled)
    {
        if (enabled == _enabled)
            return;

        _enabled = enabled;

        if (enabled)
        {
            // New session
            for (int i = 0; i < BoxCount; i++)
            {
                const char* label = _boxes[i].label;
                _boxes[i] = {};
                _boxes[i].label = label;
            }

            _currentModule = nullptr;
            _currentBox = -1;
            _pendingSwitch = false;
            _stalled = false;
            _syncCounters = true;
            _firstAfterEnable = true;
        }
        else
        {
            // Results stay in memory until the next session starts
            FinishCurrent();
        }
    }

    // Call once per presented frame.
    // frameTimeMs: time between the last two presents as measured by the menu
    // module / name: dll that executed the most recent upscale dispatch
    // dispatchCount / errorCount: totals of that dll's dispatch function
    // requestTick: changes when a dll switch gets requested
    static void OnFrame(double frameTimeMs, double nowMs, HMODULE module, const char* name, int slot,
                        uint64_t dispatchCount, uint64_t errorCount, uint64_t requestTick, bool fgActive)
    {
        if (!_enabled)
            return;

        if (_syncCounters)
        {
            _syncCounters = false;
            _lastRequestTick = requestTick;

            // Start from the current totals so only dispatches made from now on are seen
            _lastSlot = slot;
            _lastDispatchCount = dispatchCount;
            _lastErrorCount = errorCount;
            _lastDispatchMs = nowMs;
        }

        // A switch was requested, the running measurement ends before the transition starts
        if (requestTick != _lastRequestTick)
        {
            _lastRequestTick = requestTick;
            FinishCurrent();
            _pendingSwitch = true;
        }

        const bool dispatched = slot >= 0 && (slot != _lastSlot || dispatchCount != _lastDispatchCount);

        if (!dispatched)
        {
            // No upscaling for this frame (switch in progress, pause menu, other upscaler), not counted
            if (nowMs - _lastDispatchMs > StallMs)
                _stalled = true;

            return;
        }

        const bool failed = slot == _lastSlot ? errorCount != _lastErrorCount : errorCount != 0;

        _lastSlot = slot;
        _lastDispatchCount = dispatchCount;
        _lastErrorCount = errorCount;
        _lastDispatchMs = nowMs;

        if (module != _currentModule || _pendingSwitch)
        {
            // The dll that executes the upscaling changed, or dispatches came back after a requested switch.
            // When a switch fails the same dll shows up here again and simply gets measured again.
            FinishCurrent();

            _currentModule = module;
            _currentBox = Classify(module, name);
            _pendingSwitch = false;
            _stalled = false;

            if (_currentBox >= 0)
            {
                _boxes[_currentBox].status = Status::Stabilizing;
                _holdUntilMs = _firstAfterEnable ? nowMs : nowMs + StabilizeMs;
                _resetOnStart = true;
            }

            _firstAfterEnable = false;
        }
        else if (_stalled)
        {
            // Upscaling was away for a while, skip the frames around it but keep what was measured
            _stalled = false;

            if (_currentBox >= 0)
            {
                _boxes[_currentBox].status = Status::Stabilizing;
                _holdUntilMs = nowMs + StabilizeMs;
                _resetOnStart = false;
            }
        }

        if (_currentBox < 0 || failed)
            return;

        auto& box = _boxes[_currentBox];

        if (box.status == Status::Stabilizing)
        {
            if (nowMs < _holdUntilMs)
                return;

            if (_resetOnStart)
            {
                box.frames = 0;
                box.totalMs = 0.0;
                box.minMs = 0.0;
                box.maxMs = 0.0;
                box.fgSeen = false;
            }

            // This frame's time still belongs to the stabilizing period
            box.status = Status::Recording;
            return;
        }

        if (box.status != Status::Recording || !(frameTimeMs > 0.0) || !std::isfinite(frameTimeMs))
            return;

        box.frames++;
        box.totalMs += frameTimeMs;

        if (box.minMs <= 0.0 || frameTimeMs < box.minMs)
            box.minMs = frameTimeMs;

        if (frameTimeMs > box.maxMs)
            box.maxMs = frameTimeMs;

        if (fgActive)
            box.fgSeen = true;
    }

    static const char* StatusName(Status status)
    {
        switch (status)
        {
        case Status::Stabilizing:
            return "STABILIZING";
        case Status::Recording:
            return "RECORDING";
        case Status::Saved:
            return "SAVED";
        default:
            return "WAITING";
        }
    }

  private:
    inline static bool _enabled = false;
    inline static Box _boxes[BoxCount] = { { "OFFICIAL" }, { "COMMUNITY" }, { "LITE" } };

    inline static HMODULE _currentModule = nullptr;
    inline static int _currentBox = -1;
    inline static bool _pendingSwitch = false;
    inline static bool _stalled = false;
    inline static bool _syncCounters = true;
    inline static bool _firstAfterEnable = true;
    inline static bool _resetOnStart = true;
    inline static double _holdUntilMs = 0.0;

    inline static uint64_t _lastRequestTick = 0;
    inline static int _lastSlot = -1;
    inline static uint64_t _lastDispatchCount = 0;
    inline static uint64_t _lastErrorCount = 0;
    inline static double _lastDispatchMs = 0.0;

    static void FinishCurrent()
    {
        if (_currentBox < 0)
            return;

        auto& box = _boxes[_currentBox];

        // A result from an earlier measurement stays when the new one didn't get to start
        box.status = box.HasResult() ? Status::Saved : Status::Waiting;
    }

    static bool ContainsNoCase(const char* text, const char* lowerNeedle)
    {
        const size_t needleLength = std::strlen(lowerNeedle);

        for (const char* start = text; *start != 0; start++)
        {
            size_t i = 0;

            while (i < needleLength && start[i] != 0 && (char) std::tolower((unsigned char) start[i]) == lowerNeedle[i])
                i++;

            if (i == needleLength)
                return true;
        }

        return false;
    }

    // Same names as the visual verification markers, from the folder of the dll
    static int Classify(HMODULE module, const char* name)
    {
        if (module == nullptr || name == nullptr)
            return -1;

        if (ContainsNoCase(name, "official"))
            return 0;

        if (ContainsNoCase(name, "community"))
            return 1;

        if (ContainsNoCase(name, "lite"))
            return 2;

        return -1;
    }
};
