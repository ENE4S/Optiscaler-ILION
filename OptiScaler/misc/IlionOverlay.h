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
    RayReconstruction = 1 << 6, // ILION 1.5.0: OpenRR's network time, mode and resolution (only while the game uses RR)
};

constexpr uint32_t AllElements = FrameTimes | FrameGraph | UpscalerTime | UpscalerGraph | ReflexTimings | IlionLatency | RayReconstruction;

inline uint32_t PresetElements(FpsOverlay type)
{
    switch (type)
    {
    case FpsOverlay_Detailed:
        return FrameTimes | RayReconstruction;
    case FpsOverlay_DetailedGraph:
        return FrameTimes | FrameGraph | RayReconstruction;
    case FpsOverlay_Full:
        return FrameTimes | FrameGraph | UpscalerTime | RayReconstruction;
    case FpsOverlay_FullGraph:
        return FrameTimes | FrameGraph | UpscalerTime | UpscalerGraph | RayReconstruction;
    case FpsOverlay_ReflexTimings:
        return FrameTimes | FrameGraph | UpscalerTime | UpscalerGraph | ReflexTimings | RayReconstruction;
    case FpsOverlay_Latency:
        return FrameTimes | ReflexTimings | IlionLatency | RayReconstruction;
    default:
        return 0; // Just FPS, Simple
    }
}

// ILION 1.5.0: in the saved Custom list Ray Reconstruction is a "hidden" bit, so lists saved before it existed show it
constexpr uint32_t RayReconstructionHidden = 1u << 7;

// The elements shown, from the saved Custom list
inline uint32_t FromStored(uint32_t stored)
{
    uint32_t shown = stored & (AllElements & ~(uint32_t) RayReconstruction);
    if ((stored & RayReconstructionHidden) == 0)
        shown |= RayReconstruction;
    return shown;
}

// The Custom list to save for the elements shown
inline uint32_t ToStored(uint32_t shown)
{
    return (shown & (AllElements & ~(uint32_t) RayReconstruction)) | ((shown & RayReconstruction) != 0 ? 0u : RayReconstructionHidden);
}

inline uint32_t Elements()
{
    auto config = Config::Instance();
    const auto type = config->FpsOverlayType.value_or_default();
    return type == FpsOverlay_Custom ? FromStored(config->FpsOverlayElements.value_or_default()) : PresetElements(type);
}

inline bool Shows(Element element) { return (Elements() & element) != 0; }
} // namespace IlionOverlay
