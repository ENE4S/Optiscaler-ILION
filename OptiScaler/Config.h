#pragma once
#include "SysUtils.h"
#include "State.h"

#include <optional>
#include <filesystem>

enum HasDefaultValue
{
    WithDefault,
    NoDefault,
    SoftDefault // Change always gets saved to the config
};

template <class T, HasDefaultValue defaultState = WithDefault> class CustomOptional : public std::optional<T>
{
  private:
    T _defaultValue;
    std::optional<T> _configIni;
    bool _volatile;

  public:
    CustomOptional(T defaultValue)
        requires(defaultState != NoDefault)
        : std::optional<T>(), _defaultValue(std::move(defaultValue)), _configIni(std::nullopt), _volatile(false)
    {
    }

    CustomOptional()
        requires(defaultState == NoDefault)
        : std::optional<T>(), _defaultValue(T {}), _configIni(std::nullopt), _volatile(false)
    {
    }

    // Prevents a change from being saved to ini
    constexpr void set_volatile_value(const T& value)
    {
        if (!_volatile)
        { // make sure the previously set value is saved
            if (this->has_value())
                _configIni = this->value();
            else
                _configIni = std::nullopt;
        }
        _volatile = true;
        std::optional<T>::operator=(value);
    }

    // Prevents the reset from being saved to ini
    constexpr void reset_volatile()
    {
        if (!_volatile)
        {
            if (this->has_value())
                _configIni = this->value();
            else
                _configIni = std::nullopt;
        }
        _volatile = true;
        std::optional<T>::reset();
    }

    // Use this when first setting a CustomOptional
    constexpr void set_from_config(const std::optional<T>& opt)
    {
        if (!this->has_value())
        {
            _configIni = opt;
            std::optional<T>::operator=(opt);
        }
    }

    constexpr CustomOptional& operator=(const T& value)
    {
        _volatile = false;
        std::optional<T>::operator=(value);
        return *this;
    }

    constexpr CustomOptional& operator=(T&& value)
    {
        _volatile = false;
        std::optional<T>::operator=(std::move(value));
        return *this;
    }

    constexpr CustomOptional& operator=(const std::optional<T>& opt)
    {
        _volatile = false;
        std::optional<T>::operator=(opt);
        return *this;
    }

    constexpr CustomOptional& operator=(std::optional<T>&& opt)
    {
        _volatile = false;
        std::optional<T>::operator=(std::move(opt));
        return *this;
    }

    // Needed for string literals for some reason
    constexpr CustomOptional& operator=(const char* value)
        requires std::same_as<T, std::string>
    {
        _volatile = false;
        std::optional<T>::operator=(T(value));
        return *this;
    }

    constexpr T value_or_default() const&
        requires(defaultState != NoDefault)
    {
        return this->has_value() ? this->value() : _defaultValue;
    }

    constexpr T value_or_default() &&
        requires(defaultState != NoDefault)
    {
        return this->has_value() ? std::move(this->value()) : std::move(_defaultValue);
    }

    constexpr std::optional<T> value_for_config()
        requires(defaultState == WithDefault)
    {
        if (_volatile)
        {
            if (_configIni != _defaultValue)
                return _configIni;

            return std::nullopt;
        }

        if (!this->has_value() || *this == _defaultValue)
            return std::nullopt;

        return this->value();
    }

    constexpr std::optional<T> value_for_config()
        requires(defaultState != WithDefault)
    {
        if (_volatile)
            return _configIni;

        if (this->has_value())
            return this->value();

        return std::nullopt;
    }

    constexpr T value_for_config_or(T other)
    {
        auto option = value_for_config();

        if (option.has_value())
            return option.value();
        else
            return other;
    }
};

constexpr inline int UnboundKey = -1;
constexpr uint32_t NV_PRESET_LATEST = 0x00FFFFFF;

enum FpsOverlayPos : uint32_t
{
    FpsOverlayPos_TopLeft,
    FpsOverlayPos_TopRight,
    FpsOverlayPos_BottomLeft,
    FpsOverlayPos_BottomRight,
    FpsOverlayPos_TopCenter,
    FpsOverlayPos_BottomCenter,
    FpsOverlayPos_Custom, // FpsOverlayPosX/Y
    FpsOverlayPos_COUNT,
};

enum FpsOverlayShadow : uint32_t
{
    FpsOverlayShadow_Off,
    FpsOverlayShadow_Shadow,
    FpsOverlayShadow_Outline,
    FpsOverlayShadow_COUNT,
};

enum FpsOverlay : uint32_t
{
    FpsOverlay_JustFPS,
    FpsOverlay_Simple,
    FpsOverlay_Detailed,
    FpsOverlay_DetailedGraph,
    FpsOverlay_Full,
    FpsOverlay_FullGraph,
    FpsOverlay_ReflexTimings,
    FpsOverlay_LowLatencyTimings = FpsOverlay_ReflexTimings, // upstream's name for the same rows
    FpsOverlay_Latency, // ILION: frame times and both latency meters
    FpsOverlay_Custom,  // ILION: the elements in FpsOverlayElements
    FpsOverlay_COUNT,
};

// What the FPS row shows with the FPS
enum FpsOverlayFpsRow : uint32_t
{
    FpsOverlayFpsRow_JustFps,   // Only the number
    FpsOverlayFpsRow_FrameTime, // The API, frame time, frame generation and upscaler
    FpsOverlayFpsRow_Average,   // The API, average FPS, frame generation and upscaler
    FpsOverlayFpsRow_COUNT,
};

// Output scaling downscaler
enum class Scaler : uint32_t
{
    FSR1 = 0,
    Bicubic = 1,
    CatmullRom = 2,
    Lanczos2 = 3,
    Lanczos3 = 4,
    Kaiser2 = 5,
    Kaiser3 = 6,
    Magic = 7,
    Count
};

enum class ForceReflex : uint32_t
{
    InGame,
    ForceDisable,
    ForceEnable,
    Count
};

enum class LFXMode : uint32_t
{
    Conservative,
    Aggressive,
    ReflexIDs,
    Count
};

enum class LowLatencyInput : uint32_t
{
    None,
    Auto,
    AntiLag2,
    Reflex,
    XeLL,
    UeLowLatency,
    OptiScaler, // OptiScaler's own Streamline (DLSSG output) when the game has no low latency of its own
    _
};

enum class LowLatencyMode : uint32_t
{
    None,
    Auto,
    LatencyFlex,
    AntiLag2,
    XeLL,
    AntiLagVk,
    Reflex,
    FpsLimiter,
    _
};

enum class ReprojectionFill : uint32_t
{
    StrechEdge,
    Dithering,
    Noise,
    Debug,
    SoftEdge,
};

template <> struct EnumConfig<ReprojectionFill>
{
    static constexpr auto default_value = ReprojectionFill::Dithering;

    static constexpr std::pair<ReprojectionFill, std::string_view> mapping[] = {
        { ReprojectionFill::StrechEdge, "strech" },
        { ReprojectionFill::Dithering, "dithering" },
        { ReprojectionFill::Noise, "noise" },
        { ReprojectionFill::Debug, "debug" },
        { ReprojectionFill::SoftEdge, "soft" }
    };
};

class Config
{
  public:
    Config();

    // Init flags
    CustomOptional<bool, NoDefault> DepthInverted;
    CustomOptional<bool, NoDefault> AutoExposure;
    CustomOptional<bool, NoDefault> HDR;
    CustomOptional<bool, NoDefault> JitterCancellation;
    CustomOptional<bool, NoDefault> DisplayResolution;
    CustomOptional<bool, NoDefault> DynamicResolution;
    CustomOptional<bool, NoDefault> DisableReactiveMask;
    CustomOptional<float> DlssReactiveMaskBias { 0.45f };

    // Logging
    CustomOptional<bool> LogToFile { false };
    CustomOptional<bool> LogToConsole { false };
    CustomOptional<bool> LogToDebug { false };
    CustomOptional<bool> LogToNGX { false };
    CustomOptional<bool> OpenConsole { false };
    CustomOptional<bool> DebugWait { false }; // not in ini
    CustomOptional<int> LogLevel { 0 };
    CustomOptional<std::wstring> LogFileName { L"OptiScaler.log" };
    CustomOptional<bool> LogSingleFile { true };
    CustomOptional<bool> LogAsync { false };
    CustomOptional<int> LogAsyncThreads { 4 };
    CustomOptional<bool> CreateDumps { false };

    // AMD NR host settings (AMDNR GPL-3.0-or-later).
    CustomOptional<float> AmdColourStrength { 1.0f };
    CustomOptional<float> AmdComposeColour { 1.0f };
    CustomOptional<float> AmdComposeDetail { 1.0f };
    CustomOptional<uint32_t> AmdComposition { 0 };
    CustomOptional<bool, SoftDefault> AmdDanielFastMode { false };
    CustomOptional<bool> AmdDanielHighlightGuard { false };
    CustomOptional<float> AmdDetailStrength { 1.0f };
    CustomOptional<bool> AmdDeviceGate { true };
    CustomOptional<bool> AmdDynamicRes { false };
    CustomOptional<int> AmdDynamicTargetFps { 60 };
    CustomOptional<bool> AmdEditShaper { false };
    CustomOptional<bool> AmdEditShaperCarryCap { false };
    CustomOptional<int> AmdEditShaperLimit { 0 };
    CustomOptional<int> AmdEditShaperScope { 0 };
    CustomOptional<int> AmdEncoding { 0 };
    CustomOptional<bool> AmdEveryFrame { true };
    CustomOptional<bool> AmdGraphicsWaitExperimental { true };
    CustomOptional<int> AmdHighlightProxy { 0 };
    CustomOptional<float> AmdHighlightProxyKnee { 1.0f };
    CustomOptional<float> AmdHighlightProxyRange { 1.0f };
    CustomOptional<float> AmdInterleave { 0.f };
    CustomOptional<bool> AmdInterleaveAdaptive { false };
    CustomOptional<int> AmdInterleaveAdaptiveSensitivity { 1 };
    CustomOptional<int> AmdInterleaveDebug { 0 };
    CustomOptional<int> AmdInterleaveFill { 0 };
    CustomOptional<bool> AmdInterleaveFreshHistory { false };
    CustomOptional<float> AmdInterleaveGhostBound { 0.6f };
    CustomOptional<bool> AmdInterleaveModelHistory { false };
    CustomOptional<float> AmdInterleavePacing { -1.f };
    CustomOptional<int> AmdInterleavePreset { 10 };
    CustomOptional<bool> AmdInterleaveSharp { true };
    CustomOptional<int> AmdJitterSign { 1 };
    CustomOptional<bool> AmdLateSubmitGrace { true };
    CustomOptional<bool> AmdLmxxfAutoExposure { true };
    CustomOptional<bool> AmdLmxxfAutoExposureHighlightCap { true };
    CustomOptional<float> AmdLmxxfEdgeGuard { 0.5f };
    CustomOptional<float> AmdLmxxfEditDetail { 1.0f };
    CustomOptional<float> AmdLmxxfEditSaturation { 1.0f };
    CustomOptional<bool> AmdLmxxfFastMode { false };
    CustomOptional<bool> AmdLmxxfHighlightChromaGuard { true };
    CustomOptional<bool> AmdLmxxfHistory { true };
    CustomOptional<float> AmdLmxxfOutputSmooth { 0.6f };
    CustomOptional<int> AmdLmxxfTierCap { 0 };
    CustomOptional<bool> AmdLmxxfTierSnap { false };
    CustomOptional<bool> AmdNetworkOutput { false };
    CustomOptional<float> AmdNeuralLightingStrength { 1.0f };
    CustomOptional<bool> AmdNeuralListRecovery { true };
    CustomOptional<float> AmdNrScale { 1 };
    CustomOptional<int> AmdNrSizeStep { 64 };
    CustomOptional<bool> AmdOneStreamPerFrame { true };
    CustomOptional<float> AmdResidualFade { 0.0f };
    CustomOptional<float> AmdResidualIntensity { 1.0f };
    CustomOptional<float> AmdResidualLimit { 0.25f };
    CustomOptional<bool> AmdResidualTemporal { false };
    CustomOptional<int> AmdRuntimeStyle { -1 };
    CustomOptional<float> AmdSharpness { 0.0f };
    CustomOptional<bool> AmdSkipUnwrittenFrames { true };
    CustomOptional<int> AmdSlots { 3 };
    CustomOptional<int> AmdSpinDraw { 0 };
    CustomOptional<int> AmdStabilityMode { 0 };
    CustomOptional<bool> AmdStabilityStaticDebug { false };
    CustomOptional<float> AmdStabilityStaticRelax { 0.0f };
    CustomOptional<float> AmdStabilityThreshold { 0.04f };
    CustomOptional<bool> AmdStreamlineDeviceFix { true };
    CustomOptional<float> AmdTemporalStability { 0.6f };
    CustomOptional<int> AmdToneCurve { -1 };
    CustomOptional<float> AmdToneLift { -1.f };
    CustomOptional<int> AmdUseGameExposure { -1 };
    CustomOptional<bool> AmdVkLateCopyWait { false };
    CustomOptional<bool> DlssNrAutoCapture { false };
    CustomOptional<bool> DlssNrAutoMask { true };
    CustomOptional<std::string> DlssNrBackend { std::string() };
    CustomOptional<bool> DlssNrEnabled { false };
    CustomOptional<bool> DlssNrRunBeforeSr { false };
    CustomOptional<float> DlssNrEnvironmentColour { 1.0f };
    CustomOptional<float> DlssNrEnvironmentDetail { 1.0f };
    CustomOptional<float> DlssNrLocalStructure { 1.0f };
    CustomOptional<float> DlssNrMaxRatio { 2.0f };
    CustomOptional<uint32_t> DlssNrPasses { 1 };
    CustomOptional<float> DlssNrSkinColour { 1.0f };
    CustomOptional<float> DlssNrSkinDetail { 1.0f };
    CustomOptional<bool> DlssNrSkinProtection { false };
    CustomOptional<float> DlssNrSkinStructure { -1.0f };
    CustomOptional<bool> DlssNrSkinToneEnabled { true };
    CustomOptional<bool> LmxxfFullNetwork { false };

    // XeSS
    CustomOptional<bool> BuildPipelines { true };
    CustomOptional<int32_t> NetworkModel { 0 };
    CustomOptional<bool> CreateHeaps { true };

    // DLSS
    CustomOptional<bool> DLSSEnabled { true };
    CustomOptional<bool> DLSSAllowExternalBackend { false };
    CustomOptional<bool> RenderPresetOverride { false };
    CustomOptional<uint32_t> RenderPresetForAll { 0 };
    CustomOptional<uint32_t> RenderPresetDLAA { 0 };
    CustomOptional<uint32_t> RenderPresetUltraQuality { 0 };
    CustomOptional<uint32_t> RenderPresetQuality { 0 };
    CustomOptional<uint32_t> RenderPresetBalanced { 0 };
    CustomOptional<uint32_t> RenderPresetPerformance { 0 };
    CustomOptional<uint32_t> RenderPresetUltraPerformance { 0 };

    // DLSSD
    CustomOptional<bool> DLSSDRenderPresetOverride { false };
    CustomOptional<uint32_t> DLSSDRenderPresetForAll { 0 };
    CustomOptional<uint32_t> DLSSDRenderPresetDLAA { 0 };
    CustomOptional<uint32_t> DLSSDRenderPresetUltraQuality { 0 };
    CustomOptional<uint32_t> DLSSDRenderPresetQuality { 0 };
    CustomOptional<uint32_t> DLSSDRenderPresetBalanced { 0 };
    CustomOptional<uint32_t> DLSSDRenderPresetPerformance { 0 };
    CustomOptional<uint32_t> DLSSDRenderPresetUltraPerformance { 0 };

    // Nukems
    CustomOptional<bool> NvngxFGMakeDepthCopy { false };

    // Libraries
    CustomOptional<std::wstring, NoDefault> MainDllPath;
    CustomOptional<std::wstring, NoDefault> FfxDx12Path;
    CustomOptional<std::wstring, NoDefault> FfxDx12SRPath;
    CustomOptional<std::wstring, NoDefault> FfxDx12FGPath;
    CustomOptional<std::wstring, NoDefault> FfxDx12RRPath;
    CustomOptional<std::wstring, NoDefault> FfxDx12RCPath;
    CustomOptional<std::wstring, NoDefault> FfxVkPath;
    CustomOptional<std::wstring, NoDefault> FfxVkSRPath;
    CustomOptional<std::wstring, NoDefault> FfxVkFGPath;
    CustomOptional<std::wstring, NoDefault> XeSSLibrary;
    CustomOptional<std::wstring, NoDefault> XeFGLibrary;
    CustomOptional<std::wstring, NoDefault> XeLLLibrary;
    CustomOptional<std::wstring, NoDefault> XeSSDx11Library;
    CustomOptional<std::wstring, NoDefault> NvngxPath;
    CustomOptional<std::wstring, NoDefault> NVNGX_DLSS_Library;
    CustomOptional<std::wstring, NoDefault> DLSSFeaturePath;
    CustomOptional<std::wstring, NoDefault> NvapiDllPath;

    // Sharpness
    CustomOptional<SharpenShader> SharpnessShader { SharpenShader::RCAS };
    CustomOptional<bool> OverrideSharpness { false };
    CustomOptional<float> Sharpness { 0.4f };

    // RCAS
    CustomOptional<bool> RcasEnabled { false };
    CustomOptional<bool> ContrastEnabled { false };
    CustomOptional<float> Contrast { 0.0f };

    // DA Sharpening
    CustomOptional<float, NoDefault> DADepthScale;
    CustomOptional<float, NoDefault> DADepthBias;
    CustomOptional<bool, NoDefault> DAClampOutput;

    // BaBa Neural Sharpening
    CustomOptional<int> BabaNeuralModel { 0 };             // 0 = Model A (standard), 1 = Model B (edge-enhanced)
    CustomOptional<float> BabaNeuralEdgeResponse { 0.0f }; // -1 smooth edges, +1 enhance edges
    CustomOptional<int> BabaNeuralDebugView { 0 };         // 0 = normal, 1 = sharpening map, 2 = residual map

    // OptiSharpen
    // 0 / 1 = Natural / Crisp D10, 2 / 3 the same in fp16 packed (own pipeline)
    CustomOptional<int> OptiSharpenModel { 3 };
    CustomOptional<int> OptiSharpenDebugView { 0 }; // 0 = normal, 1 = sharpening map, 2 = change magnitude

    // MAS
    CustomOptional<bool> MotionSharpnessEnabled { false };
    CustomOptional<bool> MotionSharpnessDebug { false };
    CustomOptional<float> MotionSharpness { 0.2f };
    CustomOptional<float> MotionThreshold { 0.0f };
    CustomOptional<float> MotionScaleLimit { 10.0f };

    // Magnifier
    CustomOptional<bool> MagnifierEnabled { false };
    CustomOptional<float> MagnifierSize { 15.f }; // % of screen Height
    CustomOptional<int> MagnifierZoomFactor { 4 };
    CustomOptional<float> MagnifierBorderSize { 0.3f };   // % of screen Height
    CustomOptional<float> MagnifierCursorOffsetX { 0.f }; // Pixels
    CustomOptional<float> MagnifierCursorOffsetY { 0.f }; // Pixels
    CustomOptional<float, NoDefault> MagnifierStaticPosX; // % of screen Width, static pos enabled if both are defined
    CustomOptional<float, NoDefault> MagnifierStaticPosY; // % of screen Height

    // ILION graphics enhancements: one colour pass after the upscaler, off by default; neutral values skip it
    CustomOptional<bool> GfxEnabled { false };
    CustomOptional<bool> GfxTemperatureEnabled { true };
    CustomOptional<float> GfxTemperature { 0.0f }; // -1 cool .. 1 warm
    CustomOptional<bool> GfxBrightnessEnabled { true };
    CustomOptional<float> GfxBrightness { 0.0f }; // -1..1, one stop each way
    CustomOptional<bool> GfxContrastEnabled { true };
    CustomOptional<float> GfxContrast { 0.0f }; // -1..1
    CustomOptional<bool> GfxSaturationEnabled { true };
    CustomOptional<float> GfxSaturation { 0.0f }; // -1..1
    CustomOptional<bool> GfxVibranceEnabled { true };
    CustomOptional<float> GfxVibrance { 0.0f }; // -1..1
    CustomOptional<bool> GfxLocalContrastEnabled { false }; // experimental
    CustomOptional<float> GfxLocalContrast { 0.0f };         // 0..1, neutral by default

    // Menu
    CustomOptional<float, NoDefault> MenuScale;
    CustomOptional<float, NoDefault> MenuHeight; // Unscaled, no value means default height
    CustomOptional<bool, SoftDefault> OverlayMenu { true };
    CustomOptional<int> ShortcutKey { VK_INSERT };
    CustomOptional<bool> ExtendedLimits { false };
    CustomOptional<bool> ShowFps { false };

    // Hardware readings of the overlay, from HWiNFO or MSI Afterburner
    CustomOptional<bool> HwGpuUsage { false };
    CustomOptional<bool> HwGpuTemp { false };
    CustomOptional<bool> HwGpuHotspot { false };
    CustomOptional<bool> HwVram { false };
    CustomOptional<bool> HwCpuUsage { false };
    CustomOptional<bool> HwCpuTemp { false };
    CustomOptional<bool> HwGpuPower { false };
    CustomOptional<bool> HwCpuPower { false };
    CustomOptional<bool> HwRam { false };
    CustomOptional<bool> FpsOverlayLow1 { false };
    CustomOptional<int> HwSource { 0 }; // 0 = auto, 1 = HWiNFO, 2 = MSI Afterburner
    /// 0 Top Left, 1 Top Right, 2 Bottom Left, 3 Bottom Right
    CustomOptional<FpsOverlayPos> FpsOverlayPosition { FpsOverlayPos_TopLeft };
    /// 0 Only FPS, 1 +Avg FPS & Upscaler info 2 +Frame Time,
    /// 3 +Upscaler Time, 4 +Frame Time Graph, 5 +Upscaler Time Graph
    /// 6 +Reflex timings
    CustomOptional<FpsOverlay> FpsOverlayType { FpsOverlay_JustFPS };
    CustomOptional<uint32_t> FpsOverlayElements { 1 }; // ILION: elements of the Custom type (misc/IlionOverlay.h)
    CustomOptional<int> FpsShortcutKey { VK_PRIOR };
    CustomOptional<int> FpsCycleShortcutKey { VK_NEXT };
    CustomOptional<bool> FpsOverlayHorizontal { false };
    CustomOptional<bool> FpsOverlayShowRatio { false };
    CustomOptional<bool> FpsOverlayCompactLabels { false };
    CustomOptional<bool> FpsOverlayHideAverages { false };
    // Rows of the Custom overlay type in their order, comma separated keys, hidden ones start with -.
    // No value means the default overlay type's rows
    CustomOptional<std::string, NoDefault> FpsOverlayRows;
    CustomOptional<FpsOverlayFpsRow> FpsOverlayFpsRowType { FpsOverlayFpsRow_Average }; // Of the Custom type
    CustomOptional<float, NoDefault> FpsOverlayRowSpacing; // No value means the default spacing
    // Milliseconds the values that change every frame are kept, 0 updates them every frame
    CustomOptional<float> FpsOverlayUpdateInterval { 0.0f };
    CustomOptional<float> FpsOverlayAlpha { 0.45f };
    CustomOptional<float, NoDefault> FpsScale; // No value means same as MenuScale
    CustomOptional<float> FpsOverlayGraphHeight { 1.0f };
    // Position for FpsOverlayPos_Custom, 0.0 left/top to 1.0 right/bottom
    CustomOptional<float> FpsOverlayPosX { 0.0f };
    CustomOptional<float> FpsOverlayPosY { 0.0f };
    // Font file in the Windows fonts folder or a full path, no value means the menu font
    CustomOptional<std::string, NoDefault> FpsOverlayFont;
    CustomOptional<float, NoDefault> FpsOverlayFontSize; // No value means the menu font size
    CustomOptional<float> FpsOverlayLetterSpacing { 0.0f };
    // Labels are the names like FPS:, values their numbers
    CustomOptional<float> FpsOverlayLabelScale { 1.0f };
    CustomOptional<float> FpsOverlayValueScale { 1.0f };
    // 0xRRGGBB, used when the overlays don't use the theme colors
    CustomOptional<uint32_t> FpsOverlayLabelColor { 0xEAEAEA };
    CustomOptional<uint32_t> FpsOverlayValueColor { 0xFFFFFF };
    CustomOptional<uint32_t> FpsOverlayGraphColor { 0x00FF00 };
    CustomOptional<uint32_t> FpsOverlayBGColor { 0x000000 };
    CustomOptional<FpsOverlayShadow> FpsOverlayShadowType { FpsOverlayShadow_Shadow };
    CustomOptional<float> FpsOverlayShadowStrength { 0.4f };
    CustomOptional<uint32_t> FpsOverlayShadowColor { 0x000000 };
    CustomOptional<float> FpsOverlayRounding { 2.0f };
    CustomOptional<float> MenuRounding { 2.0f };
    CustomOptional<bool> UseHQFont { true };
    CustomOptional<bool> DisableSplash { false };
    CustomOptional<float> FontSize { 14.0f };
    CustomOptional<std::wstring, NoDefault> TTFFontPath;
    CustomOptional<int> FGShortcutKey { VK_END };
    CustomOptional<bool> LightTheme { false };
    CustomOptional<bool> IndustrialTheme { true };
    CustomOptional<int> IlionPalette { 0 }; // ILION theme colours: 0 = original, 2 = light, 3 = Fallout (1, the first Fallout, is read as 3)
    CustomOptional<bool> OverlaysUseTheme { false };
    CustomOptional<float> MenuAccentColorR { 0.00f };
    CustomOptional<float> MenuAccentColorG { 0.40f };
    CustomOptional<float> MenuAccentColorB { 0.77f };
    CustomOptional<float> MenuBGColorR { 0.0f };
    CustomOptional<float> MenuBGColorG { 0.0f };
    CustomOptional<float> MenuBGColorB { 0.0f };
    CustomOptional<float> MenuBGColorA { 0.93f };
    CustomOptional<bool> MenuBlur { true };
    CustomOptional<float> MenuBlurStrength { 1.0f };
    CustomOptional<bool> CustomTabEnabled { true };
    CustomOptional<std::string> MenuLanguage { "es" };
    CustomOptional<bool> MenuBasicMode { false };
    CustomOptional<bool> MenuClassicLayout { false }; // ILION dev.57: the menu as it was before the new interface
    CustomOptional<std::string> MenuUiTheme { "industrial" }; // ILION dev.70: industrial, olive or traditional (ClassicLayout=true is traditional)
    CustomOptional<std::string> MenuUiPresentation { "full" }; // ILION dev.73: full or compact, for Industrial and Clean Olive
    CustomOptional<bool> MenuSidebarWide { false };   // ILION dev.57: the sidebar of the new interface with names
    CustomOptional<bool> MenuEssentialMode { false }; // ILION: Essential mode, the full tabs under a strip of status screens. BasicMode wins
    CustomOptional<bool> IlionInfoLine { true };      // ILION: INFO line at the bottom of the menu
    CustomOptional<std::string> CustomTabCards {
        "upscaler,sharpness,fg_selection,fg_fsrfg,fg_xefg,fg_dlssg,fg_flipmeter,"
        "fg_reprojection,fg_optifg,fg_nvngx,fg_fsrfg_inputs,fg_sl_inputs,framerate"
    }; // Comma separated ids of the boxes

    // Hooks
    CustomOptional<bool> HookOriginalNvngxOnly { false };
    CustomOptional<bool> EarlyHooking { false };
    CustomOptional<bool, SoftDefault> UseNtdllHooks { true };

    // Upscale Ratio Override
    CustomOptional<bool> UpscaleRatioOverrideEnabled { false };
    CustomOptional<float> UpscaleRatioOverrideValue { 1.3f };

    // DRS
    CustomOptional<bool> DrsMinOverrideEnabled { false };
    CustomOptional<bool> DrsMaxOverrideEnabled { false };

    // Quality Overrides
    CustomOptional<bool> QualityRatioOverrideEnabled { false };
    CustomOptional<float> QualityRatio_DLAA { 1.0f };
    CustomOptional<float> QualityRatio_UltraQuality { 1.3f };
    CustomOptional<float> QualityRatio_Quality { 1.5f };
    CustomOptional<float> QualityRatio_Balanced { 1.7f };
    CustomOptional<float> QualityRatio_Performance { 2.0f };
    CustomOptional<float> QualityRatio_UltraPerformance { 3.0f };

    // ProcessFilter
    CustomOptional<std::wstring, NoDefault> TargetProcess;
    CustomOptional<std::wstring> ProcessExclusionList = {
        L"crashpad_handler.exe|crashreport.exe|crashreporter.exe|crs-handler.exe|crs-uploader.exe|crs-video.exe|"
        L"unitycrashhandler64.exe|idtechlauncher.exe|cefviewwing.exe|ace-setup64.exe|ace-service64.exe|"
        L"qtwebengineprocess.exe|platformprocess.exe|bugsplathd64.exe|bssndrpt64.exe|pspcsdkappmgr.exe|pspcsdkcore.exe|"
        L"pspcsdkstttts.exe|pspcsdktelemetry.exe|pspcsdkui.exe|pspcsdkupdatechecker.exe|pspcsdkvoicechat.exe|"
        L"pspcsdkwebview.exe|windhawk.exe|vscodium.exe|crash_reporter.exe|steamerrorreporter64.exe|crashreportclient."
        L"exe|edcefcrashpadprocess.exe|edcefrenderprocess.exe|unicrashreporter.exe"
    };

    // Hotfixes
    CustomOptional<bool> CheckForUpdate { true };
    CustomOptional<bool, SoftDefault> DisableOverlays { false };

    CustomOptional<bool> SimulateWaitableObject { false };

    CustomOptional<float, NoDefault> MipmapBiasOverride; // disabled by default
    CustomOptional<bool> MipmapBiasFixedOverride { false };
    CustomOptional<bool> MipmapBiasScaleOverride { false };
    CustomOptional<bool> MipmapBiasOverrideAll { false };

    CustomOptional<int, NoDefault> AnisotropyOverride; // disabled by default
    CustomOptional<bool> OverrideShaderSampler { true };
    CustomOptional<bool> AnisotropyModifyComp { true };
    CustomOptional<bool> AnisotropyModifyMinMax { true };
    CustomOptional<bool> AnisotropySkipPointFilter { true };

    CustomOptional<int, NoDefault> RoundInternalResolution; // disabled by default

    CustomOptional<int, NoDefault> SkipFirstFrames; // disabled by default
    CustomOptional<bool, SoftDefault> RestoreComputeSignature { false };
    CustomOptional<bool, SoftDefault> RestoreGraphicSignature { false };
    CustomOptional<bool, SoftDefault> ExtendedStateRestore { false };

    CustomOptional<bool> UsePrecompiledShaders { true };

    CustomOptional<bool> UseGenericAppIdWithDlss { false };
    CustomOptional<bool> PreferDedicatedGpu { true };
    CustomOptional<bool> PreferFirstDedicatedGpu { false };

    CustomOptional<int32_t, NoDefault> ColorResourceBarrier;    // disabled by default
    CustomOptional<int32_t, NoDefault> MVResourceBarrier;       // disabled by default
    CustomOptional<int32_t, NoDefault> DepthResourceBarrier;    // disabled by default
    CustomOptional<int32_t, NoDefault> ExposureResourceBarrier; // disabled by default
    CustomOptional<int32_t, NoDefault> MaskResourceBarrier;     // disabled by default
    CustomOptional<int32_t, NoDefault> OutputResourceBarrier;   // disabled by default

    CustomOptional<bool, SoftDefault> CreateD3D12DeviceForLuma { false };

    // Upscalers
    CustomOptional<Upscaler, SoftDefault> Dx11Upscaler { Upscaler::FSR22 };
    CustomOptional<Upscaler, SoftDefault> Dx12Upscaler { Upscaler::XeSS };
    CustomOptional<Upscaler, SoftDefault> VulkanUpscaler { Upscaler::FSR22 };

    // Output Scaling
    CustomOptional<bool> OutputScalingEnabled { false };
    CustomOptional<float> OutputScalingMultiplier { 1.5f };
    CustomOptional<Scaler> OutputScalingDownscaler { Scaler::FSR1 };

    // FSR
    CustomOptional<bool> FsrDebugView { false };
    CustomOptional<int> FfxUpscalerIndex { 0 };
    // ILION: index of the Vulkan native FFX list (FSR 3.1 of amd_fidelityfx_vk.dll, the AMD Vulkan driver's FSR 4);
    // kept apart from the DX12 list's, which the Vulkan to DX12 interop also uses
    CustomOptional<int> FfxUpscalerIndexVk { 0 };
    // ILION: upscaling backend of FFX in Vulkan games: 0 automatic (native Vulkan where the implementation has it,
    // the DX12 interop for dlls that need it), 1 native Vulkan only, 2 DX12 interop
    CustomOptional<int> IlionVulkanBackend { 0 };
    // Custom upscaler name (misc/CustomProviders.h) of each API, stored in the API's upscaler setting in place of
    // the upscaler it runs as, empty for the regular upscalers
    CustomOptional<std::string, NoDefault> CustomUpscalerDx11;
    CustomOptional<std::string, NoDefault> CustomUpscaler;
    CustomOptional<std::string, NoDefault> CustomUpscalerVk;
    // Folder in custom with a dlssd for the game's ray reconstruction, RRUpscaler in the ini, empty for auto
    CustomOptional<std::string, NoDefault> CustomRRUpscaler;
    // Regular upscaler running the game's ray reconstruction without denoising, RRUpscaler in the ini, empty for DLSSD
    CustomOptional<Upscaler, NoDefault> RRRegularUpscaler;
    // Regular upscalers can be picked for ray reconstruction, which is then offered to the game without a DLSSD
    CustomOptional<bool> RRAllowRegularUpscalers { false };

    // UpscalerIndex of [FSR] for the regular FFX upscaler, of [<name>] for a custom one
    CustomOptional<int>& FfxUpscalerIndexFor(const std::string& custom)
    {
        if (custom.empty())
            return FfxUpscalerIndex;

        // Entries never move, the reference stays valid outside the lock
        std::scoped_lock lock(_customIndexMutex);
        return _ffxCustomUpscalerIndex.try_emplace(custom, 0).first->second;
    }
    CustomOptional<int> FfxFGIndex { 0 };
    CustomOptional<bool> FsrUseMaskForTransparency { true };
    CustomOptional<bool> FsrNonLinearColorSpace { false };
    CustomOptional<bool> FsrNonLinearSRGB { false };
    CustomOptional<bool> FsrNonLinearPQ { false };
    CustomOptional<bool> FsrAgilitySDKUpgrade { false };

    // Stability preset values
    CustomOptional<float> FsrVelocity { 0.5f };
    CustomOptional<float> FsrReactiveScale { 0.333f };
    CustomOptional<float> FsrShadingScale { 0.5f };
    CustomOptional<float> FsrAccAddPerFrame { 0.333f };
    CustomOptional<float> FsrMinDisOccAcc { 0.0f };

    // FSR4
    CustomOptional<FSR4Model, NoDefault> Fsr4Model; // FSR 4 network (FSR4Models), unset = automatic
    // Overrides the FSR 4 support detected for the GPU
    CustomOptional<FSR4Support> Fsr4GpuSupport { FSR4Support::None };
    CustomOptional<uint32_t, NoDefault> Fsr4Preset;
    CustomOptional<std::wstring, NoDefault> Fsr4SrVariant;
    CustomOptional<bool> Fsr4EnableWatermark { false };
    CustomOptional<bool, SoftDefault> Fsr4DoNotLoadAmdxc64 { false };

    // FSR Common
    CustomOptional<float> FsrVerticalFov { 60.0f };
    CustomOptional<float> FsrHorizontalFov { 0.0f }; // off by default
    CustomOptional<float> FsrCameraNear { 0.1f };
    CustomOptional<float> FsrCameraFar { 100000.0f };
    CustomOptional<bool, SoftDefault> FsrUseFsrInputValues { true };

    // dx11wdx12
    CustomOptional<bool> Dx11DelayedInit { false };
    CustomOptional<bool, SoftDefault> DontUseNTShared { true };

    // vulkanwdx12
    CustomOptional<bool> VulkanUseCopyForInputs { false };
    CustomOptional<bool> VulkanUseCopyForOutput { false };

    // NVAPI Override
    CustomOptional<bool> DisableFlipMetering { false };
    CustomOptional<bool> DisableOTA { false };

    // Spoofing
    CustomOptional<bool, SoftDefault> DxgiSpoofing { true };
    CustomOptional<bool> DxgiFactoryWrapping { false };
    CustomOptional<bool, SoftDefault> StreamlineSpoofing { true };
    CustomOptional<std::string, NoDefault> DxgiBlacklist; // disabled by default
    CustomOptional<int, NoDefault> DxgiVRAM;              // disabled by default
    CustomOptional<bool, SoftDefault> VulkanSpoofing { false };
    CustomOptional<bool, SoftDefault> VulkanExtensionSpoofing { false };
    CustomOptional<int, NoDefault> VulkanVRAM; // disabled by default
    CustomOptional<bool> SpoofHAGS { false };
    CustomOptional<bool> SpoofFeatureLevel { false };
    CustomOptional<uint32_t> SpoofedVendorId { VendorId::Nvidia };
    CustomOptional<uint32_t> SpoofedDeviceId { 0x2684 };
    CustomOptional<uint32_t, NoDefault> TargetVendorId;
    CustomOptional<uint32_t, NoDefault> TargetDeviceId;
    CustomOptional<std::wstring> SpoofedGPUName { L"NVIDIA GeForce RTX 4090" };
    CustomOptional<bool> UESpoofIntelAtomics64 { false };
    CustomOptional<bool> SpoofRegistry { false };
    CustomOptional<bool> SpoofUser32 { false };
    CustomOptional<std::wstring> SpoofedDriver { L"32.0.15.9155" };

    // Plugins
    CustomOptional<std::wstring, NoDefault> PluginPath;
    CustomOptional<std::wstring, NoDefault> XeMfgVariant; // ILION: folder of plugins/XeMFG loaded as the XeMFG variant, empty = none
    // ILION dev.50: the XeMFG Vulkan bridge (with_dx12/vk_with_dx12_sc) is suspended; it runs only when this is true,
    // never because an FGOutput=XeFG came with the ini
    CustomOptional<bool> XeMfgVulkanExperimental { true }; // ILION 1.5.1: on, as the 1.4.1.2 ini had it (auto read as off in 1.5.0 hid XeFG on Vulkan)
    // ILION dev.53: back buffers of the bridge's D3D12 FG swapchain (Vulkan games), 3-16. XeMFG's flip queue paces a
    // batch of generated frames only when the chain has room for it (MFG Unlocker: "native_chain_too_small" with 3)
    CustomOptional<int> XeMfgVulkanBuffers { 8 };
    CustomOptional<bool> LoadSpecialK { false };
    CustomOptional<bool> LoadReShade { false };
    CustomOptional<std::string, NoDefault> ReShadeAddedEffects; // ILION: effects added to the ReShade panel, "file|technique;..."
    CustomOptional<bool> LoadCustomAmdxc64OnRdna2 { false };
    CustomOptional<bool> LoadAsiPlugins { true }; // ILION 1.5.1: on, as the 1.4.1.2 ini had it (XeMFG variants, MFG-Unlocker, OptiPatcher)
    // ILION v1.4.1.3: NVIDIA's DLSS FG stays at 2X on RTX 40/30/20 (Streamline reports one generated frame), and so do the
    // DLSSG replacements (XMX, DLSSG-XeFG, ANR FG). MFG-Unlocker (dashdogy, its XeMFG variant folder) lifts that limit.
    // Unset (auto): loaded on RTX 40 only; true: also RTX 30/20 (its author calls that route experimental); false: never.
    // Never with ILION's XeFG output, in Vulkan games, beside SM86 or Artur's enabler, or when it's already loaded
    CustomOptional<bool> NvidiaMfgUnlock { false };
    CustomOptional<int> LateAsiPluginsDelay { 30 };

    // Frame Generation
    CustomOptional<FGInput> FGInput { FGInput::NoFG };
    CustomOptional<FGOutput> FGOutput { FGOutput::NoFG };
    CustomOptional<FGNvngxReplacement> FGNvngxReplacement { FGNvngxReplacement::Nukems };
    CustomOptional<bool> FGDrawUIOverFG { false };
    CustomOptional<bool> FGUIPremultipliedAlpha { true };
    CustomOptional<bool, SoftDefault> FGDisableHudless { false };
    CustomOptional<bool> FGDisableUI { false };
    CustomOptional<bool> FGSkipReset { false };
    CustomOptional<int> FGAllowedFrameAhead { 1 };
    CustomOptional<bool> FGDepthValidNow { false };
    CustomOptional<bool> FGVelocityValidNow { false };
    CustomOptional<bool> FGHudlessValidNow { false };
    CustomOptional<bool> FGOnlyAcceptFirstHudless { false };
    CustomOptional<bool, SoftDefault> FGPreserveSwapChain { true };
    CustomOptional<bool, SoftDefault> FGSkipResizeBuffers { false };
    CustomOptional<bool> FGModifyBufferState { false };
    CustomOptional<bool> FGModifySCIndex { false };
    CustomOptional<float> FGHudCutoff { 0.0f };
    CustomOptional<FrameTimeSource> FTInput { FrameTimeSource::Input };
    CustomOptional<bool> FGReprojectionEnabled { false }; // WIP Not saved or loaded

    // OptiFG
    // Unset (auto) follows the game's DLSSG state when FG Input is DLSSG, use IsFGEnabled() to read
    CustomOptional<bool, SoftDefault> FGEnabled { false };
    CustomOptional<bool> FGUseMutexForSwapchain { true };
    CustomOptional<bool> FGMakeMVCopy { true };
    CustomOptional<bool> FGMakeDepthCopy { true };
    CustomOptional<bool> FGResourceFlip { false };
    CustomOptional<bool> FGResourceFlipOffset { false };
    CustomOptional<bool> FGHudlessFlip { false };
    CustomOptional<bool, SoftDefault> FGAlwaysCaptureFSRFGSwapchain { false };

    CustomOptional<int, NoDefault> FGRectLeft;
    CustomOptional<int, NoDefault> FGRectTop;
    CustomOptional<int, NoDefault> FGRectWidth;
    CustomOptional<int, NoDefault> FGRectHeight;

    // OptiFG - Hudfix
    CustomOptional<bool, SoftDefault> FGDisableHUDFix { false };
    CustomOptional<bool> FGHUDFix { false };
    CustomOptional<int> FGHUDLimit { 1 };
    CustomOptional<bool> FGHUDFixExtended { false };
    CustomOptional<bool> FGHUDFixUITexture { true };
    CustomOptional<bool, NoDefault> FGHUDFixUIUnblend;
    CustomOptional<bool> FGImmediateCapture { false };
    CustomOptional<bool> FGHudfixPersistentBindings { true };
    CustomOptional<bool> FGDontUseSwapchainBuffers { false };
    CustomOptional<bool> FGRelaxedResolutionCheck { false };
    CustomOptional<bool> FGHudfixDisableRTV { false };
    CustomOptional<bool> FGHudfixDisableSRV { false };
    CustomOptional<bool> FGHudfixDisableUAV { false };
    CustomOptional<bool> FGHudfixDisableOM { false };
    CustomOptional<bool> FGHudfixDisableDispatch { false };
    CustomOptional<bool> FGHudfixDisableDI { false };
    CustomOptional<bool> FGHudfixDisableDII { false };
    CustomOptional<bool> FGHudfixDisableSCR { true };
    CustomOptional<bool> FGHudfixDisableSGR { true };

    // OptiFG - Resource Tracking
    CustomOptional<bool> FGAlwaysTrackHeaps { false };
    CustomOptional<bool> FGResourceBlocking { false };
    CustomOptional<bool> FGUseShards { false };

    // OptiFG - DLSS-D Depth scale
    CustomOptional<bool> FGEnableDepthScale { false };
    CustomOptional<float> FGDepthScaleMax { 10000.0f };

    // FSR-FG
    CustomOptional<bool> FGDebugView { false };
    CustomOptional<bool> FGDebugResetLines { false };
    CustomOptional<bool> FGDebugTearLines { false };
    CustomOptional<bool> FGDebugPacingLines { false };
    CustomOptional<bool> FGAsync { false };
    CustomOptional<bool> FGFramePacingTuning { true };
    CustomOptional<float> FGFPTSafetyMarginInMs { 0.01f };
    CustomOptional<float> FGFPTVarianceFactor { 0.3f };
    CustomOptional<bool> FGFPTAllowHybridSpin { false };
    CustomOptional<int> FGFPTHybridSpinTime { 2 };
    CustomOptional<bool> FGFPTAllowWaitForSingleObjectOnFence { false };
    CustomOptional<bool> FSRFGDisableFlipMetering { false };

    CustomOptional<bool> FSRFGSkipConfigForHudless { false };
    CustomOptional<bool> FSRFGSkipDispatchForHudless { false };
    CustomOptional<bool> FSRFGEnableWatermark { false };
    // Custom FFX FG name (misc/CustomProviders.h) used by FSR FG, empty for the regular one
    CustomOptional<std::string, NoDefault> FSRFGProvider;

    // XeFG
    CustomOptional<bool> FGXeFGIgnoreInitChecks { false };
    CustomOptional<int> FGXeFGInterpolationCount { 1 };
    CustomOptional<bool> FGXeFGDynamicMFG { false };          // Count picked up to FGXeFGInterpolationCount
    CustomOptional<float> FGXeFGFramerateTargetDMFG { 0.0f }; // 0 = the FPS limit, else just under the refresh rate
    CustomOptional<int> FGXeFGRoundingDMFG { 1 };             // 0 at most, 1 around, 2 at least the target
    CustomOptional<bool> FGXeFGDynamicAllow1X { true };       // Dynamic MFG may turn frame generation off (1X)
    CustomOptional<bool> FGXeFGTargetAsCapDMFG { false };     // The Dynamic MFG target is also the FPS limit
    CustomOptional<int, NoDefault> FGXeFGOverrideInterpolationCount; // Game's own XeFG (XeFG input), 0 = off
    CustomOptional<bool> FGXeFGUIComposition { false };
    CustomOptional<bool> FGXeFGDepthInverted { true };
    CustomOptional<bool> FGXeFGJitteredMV { false };
    CustomOptional<bool> FGXeFGHighResMV { false };
    CustomOptional<bool> FGXeFGDebugView { false };
    CustomOptional<bool, SoftDefault> FGXeFGForceBorderless { false };

    // DLSSG
    CustomOptional<int> FGDLSSGInterpolationCount { 1 }; // For Opti's own SL instance
    CustomOptional<bool> FGDLSSGUseGamesReflexMarkers { true };
    CustomOptional<int, NoDefault>
        FGDLSSGOverrideInterpolationCount; // For overriding game's value sent to SL, could be Nvngx FG, could be noFG
                                           // but someone just uses real DLSSG
    CustomOptional<bool> FGDLSSGOverrideForceDMFG { false };   // Overrides game's DLSSG mode to Dynamic
    CustomOptional<bool> FGDLSSGForceDMFG { false };           // Overrides Opti's DLSSG mode to Dynamic
    // ILION Dynamic for the game's DLSSG (experimental): a fixed mode (eOn) whose count ILION's own controller picks up
    // to OverrideInterpolationCount, instead of the backend's eDynamic (OverrideForceDMFG)
    CustomOptional<bool> FGDLSSGIlionDynamic { false };
    CustomOptional<float> FGDLSSGFramerateTargetDMFG { 0.0f }; // 0.0 means auto-detects the display refresh rate

    // Flip Meter
    CustomOptional<int> FGFlipMeterInterpolationCount { 1 };
    // Reprojection
    CustomOptional<ReprojectionFill> ReprojectionFillMode { ReprojectionFill::Dithering };
    CustomOptional<float> ReprojectionDepthCutoff { 0.1f };
    CustomOptional<uint32_t> ReprojectionCutoffExpand { 1 };
    CustomOptional<bool> ReprojectionLateLatch { true };
    CustomOptional<bool> ReprojectionDirectionCache { true };
    CustomOptional<bool> ReprojectionParallaxAuto { true };   // estimated from the motion vectors
    CustomOptional<float> ReprojectionParallaxScale { 0.0f }; // when not auto, 0 = rotation only
    CustomOptional<bool> ReprojectionThirdPersonDetection { true };

    // As per
    // https://github.com/artur-graniszewski/dlss-enabler-main/blob/a92464d468eb0d91ae17befa66c6bf6229f20b9f/Utils/DlssgProxy.cpp#L1033
    CustomOptional<uint32_t> NvngxFGDispatchFlags { 0x10000000 }; // IGNORE_UI_TEXTURE
    CustomOptional<bool> NvngxFGShowDebug { false };

    // fakenvapi
    CustomOptional<bool, SoftDefault> UseFakenvapi { true };
    CustomOptional<bool> ForceXeLL { false };
    CustomOptional<bool> FN_ForceLatencyFlex { false };
    CustomOptional<LFXMode> FN_LatencyFlexMode { LFXMode::Conservative };
    CustomOptional<ForceReflex> FN_ForceReflex { ForceReflex::InGame };
    // ILION Intelligent Activation: 0 automatic, 1 manual, 2 off. Not set: derived from the options above
    CustomOptional<int, NoDefault> IlionLatencyMode;
    CustomOptional<LowLatencyInput> LowLatencyInput { LowLatencyInput::Auto }; // TODO: no reading/saving to config
    CustomOptional<LowLatencyMode> LowLatencyOutput { LowLatencyMode::Auto };

    // Inputs
    CustomOptional<bool> EnableDlssInputs { true };
    CustomOptional<bool> EnableXeSSInputs { true };
    CustomOptional<bool, SoftDefault> UseFsr2Inputs { true };
    CustomOptional<bool, SoftDefault> UseFsr2Dx11Inputs { false };
    CustomOptional<bool, SoftDefault> UseFsr2VulkanInputs { false };
    CustomOptional<bool, SoftDefault> Fsr2Pattern { false };
    CustomOptional<bool, SoftDefault> UseFsr3Inputs { true };
    CustomOptional<bool, SoftDefault> Fsr3Pattern { false };
    CustomOptional<bool, SoftDefault> UseFfxInputs { true };
    CustomOptional<bool> EnableHotSwapping { false };
    CustomOptional<bool, SoftDefault> EnableFsr2Inputs { true };
    CustomOptional<bool, SoftDefault> EnableFsr3Inputs { true };
    CustomOptional<bool, SoftDefault> EnableFfxInputs { true };

    // Framerate
    CustomOptional<float> FramerateLimit { 0.0f };
    // ILION dev.51: latency regulator (misc/IlionRegulator.h): 0 off, 1 balanced (2 frames in flight), 2 maximum (1)
    CustomOptional<int> IlionLatencyRegulator { 0 };

    // HDR
    CustomOptional<bool> ForceHDR { false };
    CustomOptional<bool> UseHDR10 { false };
    CustomOptional<bool> SkipColorSpace { false };

    // V-Sync
    CustomOptional<bool, SoftDefault> OverrideVsync { false };
    CustomOptional<bool, NoDefault> ForceVsync;
    CustomOptional<UINT> VsyncInterval { 0 };
    CustomOptional<int, NoDefault> VulkanPresentMode; // VkPresentModeKHR

    // Old configs for compat reasons
    CustomOptional<bool, NoDefault> _DONTUSE_Fsr4ForceEnableInt8;

    bool LoadFromPath(const wchar_t* InPath);
    bool SaveIni();
    bool SaveXeFG();
    bool SaveMenuPreferences();

    // Writes only the settings of the upscaler chosen in ILION's selector, at once, like SaveMenuPreferences
    bool SaveUpscalerChoice();
    bool SaveXeMfgVariant();

    // ILION v1.4.0: values of the ini as they are, for the plugin API ([Plugin.<name>] sections, OptiScaler
    // v10). SaveRawValues writes only those sections to the file, the rest of it stays as it is
    std::optional<std::string> GetRawValue(const char* section, const char* key);
    void SetRawValue(const char* section, const char* key, const char* value);
    void DeleteRawValue(const char* section, const char* key);
    bool SaveRawValues();


    void CheckUpscalerFiles();

    // FG on/off and interpolation count, following the game's DLSSG state when FGEnabled is unset
    static bool FGCanFollowGame();
    bool FGFollowsGame() const;
    bool IsFGEnabled() const;
    int FGInterpolationCount(int outputCount) const;

    std::vector<std::string> GetConfigLog();

    // The FPS overlay's settings that differ from the defaults as a code, to keep them outside the ini and share
    // them without a file
    std::string ExportOverlaySettings();
    bool ImportOverlaySettings(std::string_view code);

    // A code's settings as text, also a preset: Key=Value separated by ';', keys without the FpsOverlay prefix.
    // What it doesn't list goes back to the default.
    bool ApplyOverlaySettings(std::string_view text);

    static Config* Instance();

  private:
    inline static Config* _config;
    inline static std::vector<std::string> _log;

    std::filesystem::path absoluteFileName;
    std::wstring fileName = L"OptiScaler.ini";

    // Ini sections aren't case sensitive, the folder names neither
    struct NoCaseLess
    {
        bool operator()(const std::string& a, const std::string& b) const { return _stricmp(a.c_str(), b.c_str()) < 0; }
    };

    std::map<std::string, CustomOptional<int>, NoCaseLess> _ffxCustomUpscalerIndex;
    std::mutex _customIndexMutex;

    bool Reload(std::filesystem::path iniPath);

    // The FPS overlay's keys of the Menu section: read when loading the ini, or assigned for a pasted code
    void ReadOverlaySettings(bool assign);
    void WriteOverlaySettings();

    std::optional<std::string> readString(std::string section, std::string key, bool lowercase = false);
    std::optional<std::wstring> readWString(std::string section, std::string key, bool lowercase = false);
    std::optional<float> readFloat(std::string section, std::string key);
    std::optional<int> readInt(std::string section, std::string key);
    std::optional<uint32_t> readUInt(std::string section, std::string key);
    std::optional<bool> readBool(std::string section, std::string key);

    template <typename Enum> std::optional<Enum> readEnum(std::string section, std::string key);
};
