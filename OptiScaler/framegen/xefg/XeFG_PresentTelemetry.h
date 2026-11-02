#pragma once
#include <cstdint>
#include <mutex>
#include <cmath>

// Measured successful game Present cadence and frames the SDK says it sent for presentation.
// No multiplication by the requested count; neither value claims scanout or raster-only GPU throughput.
class XeFG_PresentTelemetry
{
  public:
    struct Sample { double baseFps=0.0, outputFps=0.0, sampledMs=0.0; bool baseValid=false, outputValid=false; };
    void Reset()
    {
        std::lock_guard lock(_mutex);
        Clear();
    }
    void Record(double nowMs, bool presentSucceeded, bool sdkStatusValid, uint32_t submittedFrames)
    {
        std::lock_guard lock(_mutex);
        if (!std::isfinite(nowMs) || nowMs < _lastMs || (_lastMs > 0.0 && nowMs - _lastMs > 2500.0))
            Clear();
        if (!std::isfinite(nowMs)) return;
        _lastMs = nowMs;
        if (_startMs < 0.0) { _startMs=nowMs; return; }
        if (!presentSucceeded)
        {
            // A failed Present is not evidence of output, and invalidates this reporting interval.
            _sample = {};
            _startMs=nowMs; _base=0; _outputs=0; _allSdk=true;
            return;
        }
        ++_base;
        _allSdk = _allSdk && sdkStatusValid && submittedFrames >= 1;
        if (sdkStatusValid) _outputs += submittedFrames;
        const double elapsed = nowMs-_startMs;
        if (elapsed >= 1000.0)
        {
            _sample = {1000.0*_base/elapsed, 1000.0*_outputs/elapsed, nowMs, _base>=8, _base>=8 && _allSdk};
            _startMs=nowMs; _base=0; _outputs=0; _allSdk=true;
        }
    }
    Sample Read(double nowMs) const
    {
        std::lock_guard lock(_mutex);
        return !std::isfinite(nowMs) || nowMs < _sample.sampledMs || nowMs-_sample.sampledMs>2000.0 ? Sample{} : _sample;
    }
  private:
    void Clear() { _sample={}; _startMs=-1.0; _lastMs=0.0; _base=0; _outputs=0; _allSdk=true; }
    mutable std::mutex _mutex;
    Sample _sample;
    double _startMs=-1.0, _lastMs=0.0;
    uint64_t _base=0, _outputs=0;
    bool _allSdk=true;
};
