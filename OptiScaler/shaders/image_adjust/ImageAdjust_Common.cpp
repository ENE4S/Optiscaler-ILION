#include "pch.h"
#include "ImageAdjust_Common.h"

#include <Config.h>

namespace
{
constexpr float kNeutral = 1e-4f;

float Value(const CustomOptional<bool>& enabled, const CustomOptional<float>& value)
{
    return enabled.value_or_default() ? value.value_or_default() : 0.0f;
}
} // namespace

bool ImageAdjust_Common::ShouldRun()
{
    auto config = Config::Instance();

    if (!config->GfxEnabled.value_or_default() || ImageAdjust::bypass)
        return false;

    for (float v : { Value(config->GfxTemperatureEnabled, config->GfxTemperature),
                     Value(config->GfxBrightnessEnabled, config->GfxBrightness),
                     Value(config->GfxContrastEnabled, config->GfxContrast),
                     Value(config->GfxSaturationEnabled, config->GfxSaturation),
                     Value(config->GfxVibranceEnabled, config->GfxVibrance),
                     Value(config->GfxLocalContrastEnabled, config->GfxLocalContrast) })
    {
        if (std::abs(v) > kNeutral)
            return true;
    }

    return false;
}

void ImageAdjust_Common::FilloutStruct(uint32_t width, uint32_t height, bool isHdr, InternalConstants& c)
{
    auto config = Config::Instance();

    c.Width = (int) width;
    c.Height = (int) height;
    c.IsHdr = isHdr ? 1 : 0;
    c.SplitX = ImageAdjust::compareSplit ? (int) (width / 2) : 0;

    // Brightness: -1..1, one stop each way
    c.Exposure = std::exp2(Value(config->GfxBrightnessEnabled, config->GfxBrightness));

    // Temperature: -1 cool .. 1 warm, red and blue gains, luminance kept
    const float t = Value(config->GfxTemperatureEnabled, config->GfxTemperature);
    float r = 1.0f + 0.15f * t;
    float g = 1.0f + 0.03f * t;
    float b = 1.0f - 0.15f * t;
    const float luma = 0.2126f * r + 0.7152f * g + 0.0722f * b;
    c.GainR = r / luma;
    c.GainG = g / luma;
    c.GainB = b / luma;

    // Contrast: -1..1. SDR: curve power 0.57..1.74. HDR: gentler (0.7..1.3) and at most half a stop per pixel
    const float k = Value(config->GfxContrastEnabled, config->GfxContrast);
    c.ContrastPower = isHdr ? 1.0f + 0.3f * k : std::exp2(0.8f * k);
    c.Pivot = isHdr ? 0.18f : 0.5f;

    // Saturation and vibrance: -1..1
    c.Saturation = 1.0f + Value(config->GfxSaturationEnabled, config->GfxSaturation);
    c.Vibrance = Value(config->GfxVibranceEnabled, config->GfxVibrance);

    // Local contrast: 0..1, neighbourhood about 1/120 of the picture height
    c.LocalContrast = std::max(0.0f, Value(config->GfxLocalContrastEnabled, config->GfxLocalContrast));
    c.LocalRadius = std::max(4.0f, (float) height / 120.0f);
}
