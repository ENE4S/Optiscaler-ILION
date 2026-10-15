#pragma once

// ILION: what the performance overlay shows. The overlay type stays the single setting: a predefined type sets its
// elements, Custom takes them from FpsOverlayElements. The predefined types keep OptiScaler's cumulative meaning
// (each one adds to the one before), so a layout drawn for a type looks exactly as before.

#include <cstdint>

#include <Config.h>

namespace IlionOverlay
{
enum Element : uint32_t
{
    FrameTimes = 1 << 0,     // FPS average, frame time and its average
    FrameGraph = 1 << 1,     // graph of frame times
    UpscalerTime = 1 << 2,   // upscaler GPU time and its average
    UpscalerGraph = 1 << 3,  // graph of upscaler times
    ReflexTimings = 1 << 4,  // OptiScaler's Reflex timings
    IlionLatency = 1 << 5,   // ILION estimated latency
};

constexpr uint32_t AllElements = FrameTimes | FrameGraph | UpscalerTime | UpscalerGraph | ReflexTimings | IlionLatency;

inline uint32_t PresetElements(FpsOverlay type)
{
    switch (type)
    {
    case FpsOverlay_Detailed:
        return FrameTimes;
    case FpsOverlay_DetailedGraph:
        return FrameTimes | FrameGraph;
    case FpsOverlay_Full:
        return FrameTimes | FrameGraph | UpscalerTime;
    case FpsOverlay_FullGraph:
        return FrameTimes | FrameGraph | UpscalerTime | UpscalerGraph;
    case FpsOverlay_ReflexTimings:
        return FrameTimes | FrameGraph | UpscalerTime | UpscalerGraph | ReflexTimings;
    case FpsOverlay_Latency:
        return FrameTimes | ReflexTimings | IlionLatency;
    default:
        return 0; // Just FPS, Simple
    }
}

inline uint32_t Elements()
{
    auto config = Config::Instance();
    const auto type = config->FpsOverlayType.value_or_default();
    return type == FpsOverlay_Custom ? (config->FpsOverlayElements.value_or_default() & AllElements) : PresetElements(type);
}

inline bool Shows(Element element) { return (Elements() & element) != 0; }
} // namespace IlionOverlay
