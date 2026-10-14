#pragma once

#include "pch.h"

// ILION graphics enhancements: colour, brightness, contrast and local contrast of the upscaled picture in one pass
// after the upscaler and the sharpening (shader: precompile/ImageAdjust.hlsl). Off by default, and left out of the
// chain whenever every control is neutral, so the picture is untouched then.
class ImageAdjust_Common
{
  protected:
    // Only 4 byte members, in the order of the shader's cbuffer
    struct alignas(256) InternalConstants
    {
        int Width;
        int Height;
        int IsHdr;
        int SplitX;

        float Exposure;
        float GainR;
        float GainG;
        float GainB;

        float ContrastPower;
        float Pivot;
        float Saturation;
        float Vibrance;

        float LocalContrast;
        float LocalRadius;
        float Pad0;
        float Pad1;
    };

    static void FilloutStruct(uint32_t width, uint32_t height, bool isHdr, InternalConstants& constants);

  public:
    static bool ShouldRun();
};

namespace ImageAdjust
{
// Comparison, while the menu runs; not saved
inline bool compareSplit = false; // left half of the picture without the adjustments
inline bool bypass = false;       // the whole picture without them
} // namespace ImageAdjust
