// Copyright (c) 2026 3zwr1 (AMDNR)
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <d3d12.h>
#include <atomic>
#include <filesystem>
#include <string>

namespace AmdPreSr
{
// (0.3.3.2, exit) Which thread holds a backend's recording lock for Record or Submitted: set after the
// lock is taken, cleared before it is released (declare it after the guard). Shutdown compares it with
// its own thread, so an ExitProcess from inside our own recording (a crash handler) returns at once
// instead of locking a mutex that thread already owns.
struct LockOwnerMark
{
    std::atomic<DWORD>& owner;
    explicit LockOwnerMark(std::atomic<DWORD>& o) : owner(o) { owner.store(GetCurrentThreadId(), std::memory_order_relaxed); }
    ~LockOwnerMark() { owner.store(0, std::memory_order_relaxed); }
    LockOwnerMark(const LockOwnerMark&) = delete;
    LockOwnerMark& operator=(const LockOwnerMark&) = delete;
};
// Identify pass DLL by SHA+size. Returns kAmdLayouts[].name or nullptr.
// (0.3.4, danielblnc support) For a build named by its digest prefix (AmdLayout.h IsVersionName) the name returned is the
// version the file itself carries (AmdLayout.h RuntimeVersionIn), else that prefix; an unknown build returns a
// description. layoutOut (optional) receives the identified layout, nullptr for an unknown build.
const char* IdentifyRuntimeName(const std::filesystem::path& passDll);
struct AmdLayout;
const char* IdentifyRuntimeName(const std::filesystem::path& passDll, const AmdLayout** layoutOut);
// The name of the runtime the backend has loaded, or null if it has not loaded one.
// Free to call from anywhere, including per-frame UI code: it touches no files.
const char* LoadedRuntimeName();
// (0.3.4, danielblnc support) The layout of the runtime the backend has loaded, or null. Any thread, no file access.
const AmdLayout* LoadedRuntimeLayout();
// (0.3.4, danielblnc support) The loaded runtime's own quality mode, read from pass 1 right after it loaded (its DllMain has
// parsed its dlssnr_on_amd.ini), before the host writes anything there: 1 Fast, 0 Reference, -1 not known (nothing
// loaded yet, or a build without the mode). Any thread.
int RuntimeOwnQuality();
float LateSubmitNrShare(); // (0.3.4, P3) -1: nothing late in the last ~600 frames, else the share NR ran on; any thread
struct Frame
{
    ID3D12Resource *colour = nullptr, *motion = nullptr, *depth = nullptr, *exposure = nullptr;
    UINT width = 0, height = 0;
    float motionScaleX = 1, motionScaleY = 1;
    // Active display extent, excluding allocation padding; zero means render-resolution vectors.
    UINT motionWidth = 0, motionHeight = 0;
    float preExposure = 1, exposureScale = 1;
    // The title's TAA jitter for this frame, in render pixels (NGX Jitter_Offset). The raw
    // is rendered on a grid shifted by it; consecutive frames differ by the delta even when
    // nothing moves, and the temporal pass must reproject by that delta as well as by motion.
    float jitterX = 0, jitterY = 0;
    bool reset = false, depthInverted = false;
    D3D12_RESOURCE_STATES colourState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    D3D12_RESOURCE_STATES motionState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    D3D12_RESOURCE_STATES depthState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    D3D12_RESOURCE_STATES exposureState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    // The title's reactive / bias mask, when it publishes one. Marks pixels whose
    // colour cannot be predicted from the previous frame - particles, transparencies,
    // anything composited after the velocity pass. Null in titles that publish none.
    ID3D12Resource* reactive = nullptr;
    D3D12_RESOURCE_STATES reactiveState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    // Post-RR placement. `colour` is then the finished Ray Regeneration output at display
    // resolution while depth (and the reactive mask) stay at render resolution: guideWidth/
    // Height is their active extent (0 = the colour extent, the pre-SR case; motion declares
    // its own with motionWidth/Height). writeBack asks Record to write its result back into
    // `colour` instead of handing a replacement to the caller; the returned pointer is then
    // only a success flag.
    UINT guideWidth = 0, guideHeight = 0;
    bool writeBack = false;
    bool inputWriteBack = false; // Before placement: preserve pre-SR guide/jitter semantics, write into the original input.
    // The colour is display-referred (already tone mapped / encoded), not linear light: the game
    // created its upscaler without IsHDR, or the colour format cannot hold linear HDR (only float
    // formats can), or AmdEncoding says sRGB / Gamma 2.2. Set by AmdBridge::Run with the same test
    // the NVIDIA path uses for passthrough (DlssNr_Dx12.cpp FormatCanHoldLinearHdr). Used only to
    // refuse the RenoDX composition (AmdComposition 1) on both runtimes: its tail is linear only.
    bool displayReferred = false;
};
struct LookSettings
{
    bool enabled = false;
    UINT appearance = 2, inspect = 0;
    bool detectSkin = true;
    float mix = 1, materialDetail = 1.15f, shapeDefinition = 1.2f, localLighting = 1.15f;
    float skinDetail = 1.1f, skinSoftness = .486f, specularControl = .58f, highlightRollOff = .9f;
    float colourSeparation = 0, shadowDepth = .2f, antiHalo = .901f, flatAreaProtection = 0;
    float tone = 0, exposureEV = 1, contrast = 1, saturation = 1, highlightCompression = 0;
};
struct RtgiSettings {
    bool enabled = false;
    UINT quality = 2, denoiser = 1, inspect = 0;
    float mix = 1, lighting = 5, occlusion = 1, ambient = 1;
    float thickness = .1f, smoothness = .5f, fade = .3f, fov = 60, farPlane = 600;
    float contact = 0, saturation = 1, radius = 1;
    bool operator==(const RtgiSettings&) const = default;
};
struct Settings
{
    UINT encoding = 0; // Auto, Linear, sRGB, Gamma 2.2
    bool everyFrame = false;
    // How many frames may be in flight at the NR stage. Too few and a frame that
    // finds every buffer busy carries no NR at all. In one YYSLS AB session the
    // counter rose by about 1200-1440 per two-slot segment and stayed flat with
    // three; the log and PresentMon windows were not aligned, so this is not a
    // skip rate. Onimusha had no skips at either two or three.
    // See AmdPreSr.cpp for what the bound costs in memory.
    UINT slots = 3;
    bool toneChannels = false;
    float modelScale = 1;
    UINT sizeStep = 64; // leak audit R4 (0.3.3.2 rebuild): danielblnc working-size snap, see AmdPreSr.cpp
    // Temporal stabilisation of the final NR colour (0 off = byte-identical,
    // 1 max history). Motion-reprojected, neighbourhood-clamped feedback run on
    // the render queue before Super Resolution. See TemporalStability.h.
    float stability = 0;
    // 0 = variance clamp (TAA-style), 1 = difference-gated (blend only where the
    // reprojected history already matches the frame; ghost-free, after lmxxf).
    UINT stabilityMode = 0;
    float stabilityThreshold = 0.08f; // difference-gated sensitivity (tonemapped units)
    // #5 still-surface steadiness (0.3.3.2 rebuild): 0 = off (byte-identical). See TemporalStability.h.
    float stabilityStaticRelax = 0.f;
    bool stabilityStaticDebug = false;
    // Blend the denoised result back toward the pre-denoise colour. 1/1 = full
    // model (byte-identical, pass skipped). detail = brightness amount; colour =
    // model hue (1) vs original hue at the model's brightness (0). See DetailColourMix.h.
    float detail = 1, colour = 1;
    // Contrast Adaptive Sharpening on the denoised colour, 0..1. 0 = off (skipped).
    float sharpness = 0;
    // Experimental interleave: run the model every Nth frame and fill the others
    // by temporal reprojection. 0 = off (every frame); >1 is the average cadence
    // and need not be an integer (1.4 = skip ~2 frames in 5). Forces the temporal
    // stabilizer on. Big FPS win, but disrupts model temporal state.
    float interleave = 0.f;
    // Interleave fill frame source: 0 = pure reprojected history (cannot flicker, can
    // ghost), 1 = smart fill mixing the blurred un-denoised current frame (breaks the
    // ghost, but makes the fill frame differ from the model frame). See TemporalStability.h.
    UINT interleaveFill = 0;
    // Take a skipped frame's detail from the reprojected (denoised) history instead of
    // scaling the un-denoised current frame's. Full sharpness and no grain on both
    // frame types; the risk is the sub-pixel reprojection error showing up instead.
    bool interleaveSharp = true;
    // Diagnostic overlay: 0 off, 1 trust, 2 frame type, 3 clamp activity.
    UINT interleaveDebug = 0;
    // Interleave preset: 0 = standard, 1 = extra temporal on filled frames. 10 = Edit
    // accumulation on both runtimes (the lmxxf host hands it to the temporal pass as 11); any
    // other value is lmxxf's classic carry (9). See Config.h AmdInterleavePreset.
    UINT interleavePreset = 0;
    // How far to even out the model/fill frame-time sawtooth, 0 = off, 1 = fully even
    // (and fully even costs the whole interleave gain). See InterleavePacing.h.
    float interleavePacing = 0.f;
    // Hybrid highlight proxy (experimental, off; danielblnc only): the frame handed to the
    // model is compressed above `proxyKnee` with a reversible curve and the model's answer is
    // divided by the encode's scale recomputed from the original pixel (RenoDX's principle),
    // handed over to the curve's own decode where the answer sits low in the curve. Identity
    // below the knee. See AmdPreSr.cpp, ProxyShader.
    bool proxy = false;
    float proxyKnee = 1.f;
    float proxyRange = 1.f;
    // Held frame's ghost bound, 0 = off (nothing gated, as before), 1 = full clip to
    // the current neighbourhood. Catches the ghost a geometric test cannot: content
    // with no motion vectors of its own, like smoke. See TemporalStability.h.
    float interleaveGhostBound = 0.f;
    // Clear the MODEL's own internal temporal history on every model frame while
    // interleaving. Costs the model its noise accumulation; removes the smear it
    // builds internally around content its motion vectors do not describe.
    bool interleaveFreshHistory = false;
    // Whether the MODEL keeps its own temporal history across skipped frames. Off: every
    // model frame is computed on its own, exactly as every-frame mode does with interleave
    // off. On: the network's temporal path runs on a history two frames old - cleaner
    // where nothing moves, and the origin of a ghost that lives in the model frames
    // themselves where something does. See the temporal flag in AmdPreSr.cpp.
    bool interleaveModelHistory = false;
    // lmxxf only: feed the network its own previous answer, reprojected by the motion vectors,
    // as temporal history (upstream's temporal path; off = every answer from scratch).
    bool lmxxfHistory = true;
    // lmxxf only, "Full network" ([DlssNr] LmxxfFullNetwork): run all 71 blocks instead of the
    // runtime's production schedule (blocks 42, 43, 46 skipped; about 1 ms at 1080p). A change
    // rebuilds the runtime's network at the next model frame (LMXXF_NR_FRAME_FLAG_FULL_NETWORK).
    bool lmxxfFullNetwork = false;
    // lmxxf only, the edit shaper (see LmxxfBackend.cpp's residual shader) and upstream's
    // output-side smoothing inside the runtime (0 = off, strength of the blend toward the
    // motion-warped previous answer where the two differ little).
    float lmxxfEditDetail = 1.f, lmxxfEditSaturation = 1.f, lmxxfEdgeGuard = 0.5f, lmxxfOutputSmooth = 0.6f;
    // lmxxf only: when the title publishes no exposure texture, feed the network at an exposure
    // that brings the encoded picture's mean toward 0.5 (danielblnc's runtime does this in every
    // game) instead of the colour as is. See LmxxfBackend.cpp, AutoExposureShader.
    bool lmxxfAutoExposure = true;
    // lmxxf only: the frame runs over OptiScaler's Vulkan-on-D3D12 bridge (a Vulkan title; the name
    // is historical). That bridge records and executes its D3D12 list inside Evaluate, after a queue
    // Wait on a fence that only the game's next vkQueueSubmit signals. So the lmxxf host asks the
    // runtime to warm the network before that first wait (LMXXF_NR_FRAME_FLAG_VULKAN_BRIDGE), waits
    // for the answer on the CPU with a bound, and skips queue drains that would sit behind the Wait.
    bool lmxxfCpuWait = false;
    // Both hosts: the frame runs over OptiScaler's Vulkan-on-D3D12 bridge (a Vulkan title). The
    // bridge sets it to the same value as lmxxfCpuWait, which keeps its lmxxf-only meaning above.
    // danielblnc reads it to skip the post-submit job wait, to run one Neural pass, and to log a
    // gap between two Records (AmdPreSr.cpp: WaitAfterSubmitIfEveryFrame, Record).
    bool vulkanBridge = false;
    // lmxxf only: the frame runs over OptiScaler's D3D11-on-D3D12 bridge (a DirectX 11 title,
    // IFeature_Dx11wDx12). The GPU half of the every-kStatsEvery stats is left out there: recorded
    // on that bridge's list it makes Close return E_INVALIDARG (LmxxfBackend.cpp, step 9), the same
    // bridge the danielblnc NR cost readout already skips (AmdPreSr.cpp, timeNr).
    bool d3d11Bridge = false;
    // lmxxf only, final image mode: the title has no motion vectors. The answer is consumed only
    // when the runtime reports it finished (the frame never waits for the network; the next feed
    // waits for the answer instead), and the edit is carried in place by an EMA (`stability`)
    // gated on colour change against the frame the edit belongs to (`stabilityThreshold`),
    // instead of the reprojecting temporal pass.
    bool lmxxfNoMotion = false;
    // Adaptive interleave: the model runs every frame while the picture is changing and
    // interleaves only when it stands still (see Interleave::NoteChange).
    bool interleaveAdaptive = true;
    // Jitter compensation sign for the temporal pass: +1 = prev = p + m + (jPrev - jCur),
    // -1 = the opposite convention, 0 = off. Verified with a static capture (cap_jitter.py).
    int jitterSign = 1;
    UINT interleaveAdaptiveSensitivity = 1; // 0 Low, 1 Medium, 2 High
    // Deliver the model result untouched: every pass of ours after it is skipped.
    bool networkOutput = false;
    // Residual composition, used whenever the model runs away from the input resolution: the
    // model's CHANGE is carried back and added to the full-resolution frame, so the frame's
    // own detail survives instead of being replaced by a resampled picture. danielblnc: at
    // exactly 100% strength scales the whole NR result against the pre-model frame and limit /
    // fade do not act; at any other NR size strength and limit act on the model's edit before
    // the Look / stability / sharpening (EditShapeShader) and fade rolls the lifted edit off at
    // the border. lmxxf: strength and limit act on the edit at every size; fade acts while the
    // edit is lifted (0.3.4).
    float residualIntensity = 1.f, residualLimit = 0.5f, residualFade = 0.f;
    // Smooth the model's edit over time instead of the picture. Works with interleave off.
    bool residualTemporal = false;
    // SJ-1 (0.3.3.2 rebuild, danielblnc): the edit shaper away from 100% NR; false = the whole-result dial of the
    // first 0.3.3.2 build ([DlssNr] AmdEditShaper, opt-in; off by default after the Forza highlight test).
    // See EditShapeShader in AmdPreSr.cpp.
    bool editShaper = false;
    UINT passes = 1;
    // Legacy preference; the host currently supports compute (0) only.
    int spinDraw = 0;
    // Experimental: the 0.3.1 graphics wait (1-pixel draws). Unavailable in this build: its
    // admission can never pass (the graphics root signature is never tracked), so Record never
    // installs the state hooks nor asks for admission and the compute wait runs. Here it only
    // picks a one-time log line; the key itself still sets SpinDraw before the runtime's Init
    // (InitPass reads it directly), unchanged.
    bool graphicsWait = false;
    float tone = 0, structure = 1, skin = 1;
    // Colour composition ([DlssNr] AmdComposition): 0 Classic (today's picture on both runtimes,
    // byte-identical; everything below is ignored), 1 RenoDX (experimental): the composition tail
    // of dlssnr.hlsl run on the host after the model (NrCompose.h), bounding the answer against
    // the original before the usual residual controls. The bridge sanitises every field:
    //   composeDetail  T, 0..2 ([DlssNr] AmdComposeDetail; above 1 the luminance ratio is raised
    //                  to the power T, still bounded by the guard)
    //   composeColour  Cs, 0..4 ([DlssNr] AmdComposeColour; above 1 OkLab chroma is scaled and
    //                  pulled back into gamut toward neutral)
    //   maxRatio       the Highlight guard G, 1..8, two-sided ([DlssNr] MaxRatio, shared with
    //                  the NVIDIA path)
    //   skinProtection the skin / environment final edit with the colour-based mask ([DlssNr]
    //                  SkinProtection); the four strengths 0..1 ([DlssNr] SkinDetail, SkinColour,
    //                  EnvironmentDetail, EnvironmentColour), skinColour 0 when SkinToneEnabled
    //                  is off - the same keys and the same sanitising as the NVIDIA path.
    // Classic keeps detail / colour above (0..1) and never reads these.
    UINT composition = 0;
    float composeDetail = 1, composeColour = 1, maxRatio = 2;
    bool skinProtection = false;
    float skinDetail = 1, skinColour = 1, envDetail = 1, envColour = 1;
    // AMDNR 0.3.4 (plan section 3). Filled by AmdBridge BuildSettings from the keys of the same name (Config.h); every
    // default below is the 0.3.3.2 behaviour.
    //   editShaperLimit     [DlssNr] AmdEditShaperLimit 0..2: 0 literal, 1 F1, 2 F2 (EditShapeRules.h; danielblnc)
    //   editShaperBelowOnly [DlssNr] AmdEditShaperScope == 1: the shaper acts below 100% NR only
    //   editShaperCarryCap  [DlssNr] AmdEditShaperCarryCap: Edit accumulation's cap = the shaper's effective limit
    //   danielHighlightGuard [DlssNr] AmdDanielHighlightGuard: danielblnc highlight colour guard
    //   autoMask            [DlssNr] AutoMask (true): the runtimes' character mask
    //   runtimeStyle, toneCurve, useGameExposure: [DlssNr] AmdRuntimeStyle -1..2, AmdToneCurve -1..1,
    //                       AmdUseGameExposure -1..1; -1 = auto (the host writes nothing)
    //   toneLift            [DlssNr] AmdToneLift: -1 auto, else 0..0.25
    //   lmxxfTierSnap       [DlssNr] AmdLmxxfTierSnap: lmxxf NR size snapped to the network's tiers
    int editShaperLimit = 0;
    bool editShaperBelowOnly = false;
    bool editShaperCarryCap = false;
    bool danielHighlightGuard = false;
    bool autoMask = true;
    int runtimeStyle = -1, toneCurve = -1, useGameExposure = -1;
    float toneLift = -1.f;
    // (0.3.4, danielblnc support) [DlssNr] AmdDanielFastMode: -1 unset = auto (the host writes nothing: the runtime keeps its
    // own mode, Fast unless its dlssnr_on_amd.ini says otherwise), 1 true = Fast, 0 false = Reference. danielblnc
    // only, and only builds whose layout maps the byte (AmdLayout.h QualityMapped); live, no history reset.
    int danielQuality = -1;
    bool lmxxfTierSnap = false;
    LookSettings look;
    RtgiSettings rtgi;
};
// A snapshot of what the backend has been doing, for the menu's live readout.
// Deliberately raw monotonic counters plus the tick they were read at, rather than
// rates: whoever displays them knows over what interval they want to average, and
// keeping no averaging state in the backend means this costs nothing to sample.
struct Stats
{
    unsigned long long tick = 0;         // GetTickCount64 when this was taken
    unsigned long long recorded = 0;     // frames the pass recorded work for
    unsigned long long completedModelFrames = 0;
    unsigned long long modelFrames = 0;  // of those, frames the model actually ran on
    unsigned long long skips = 0;        // frames that found no free slot and carried no NR
    UINT width = 0, height = 0;          // the model's working resolution
    bool interleaving = false;
    // Adaptive interleave readout: the last measured fractions and whether the model is
    // currently forced onto every frame; boostFrames counts the frames it was forced on.
    float changeFraction = 0.f, motionFraction = 0.f;
    bool boosting = false;
    unsigned long long boostFrames = 0;
    // Model-frame ghost meter (TemporalStability.h): of the moving pixels on the last
    // measured model frame whose old content differs from the new raw, the fraction where
    // the model's answer resembles the OLD picture at that position more than the raw.
    float modelGhostFraction = 0.f;
    unsigned modelGhostSamples = 0;
    // (P3, 0.3.3.2) Whether the model really has a history: the last value handed to the runtime's
    // temporal flag (not the config: AmdEveryFrame=false turns it on too). Off, there is nothing for
    // the model to ghost from and the menu shows no meter. danielblnc only (lmxxf leaves it false).
    bool modelHistoryInUse = false;
    // Self-tuning: the frame-mean error the learner currently measures per candidate
    // (reprojected picture, carried edit, luminance curve).
    float learnedError[3] = { 0.f, 0.f, 0.f };
    bool learnedValid = false;
    // Edit accumulation (interleave preset 10 on danielblnc, 11 on lmxxf): whether it runs; the
    // share of the picture wearing the tone curve (the carried edit was not valid there); the
    // share refused by the raw-against-raw test alone; how much of the model's local detail the
    // danielblnc fit keeps (-1 = not measured yet / lmxxf); model calls the runtime declined.
    bool accumulating = false;
    float carryTone = 0.f, carryRawRefused = 0.f, carryDetailKept = -1.f;
    unsigned long long refusedCalls = 0;
    // Where the RenoDX composition took its white point W on the last composed frame (no
    // readback of the value itself): kCompositionWhite* below.
    UINT compositionWhiteSource = 0;
    // AMDNR 0.3.4 (plan section 3). modelFrameSeen: the model has answered at least once since NR started (the menu's
    // ghost meter says "waiting for the first model frame" until then). nrGpuMs: the NR cost of a model frame in ms,
    // -1 = not measured (no readout).
    bool modelFrameSeen = false;
    float nrGpuMs = -1.f;
};
// Stats::compositionWhiteSource. None: not composing (Classic, refused, or no model frame yet).
// Title: W = 1/e from the title's exposure texture (danielblnc). Estimate: W = 1/e from NrCompose's
// white-point estimator (danielblnc, no exposure texture). Fed: lmxxf's fed units, W = 1.
inline constexpr UINT kCompositionWhiteNone = 0, kCompositionWhiteTitle = 1, kCompositionWhiteEstimate = 2,
                      kCompositionWhiteFed = 3;

// What the bridge (AmdBridge.cpp) needs from a neural backend. Two implement it: this file's
// Backend, hosting danielblnc's closed runtime, and Lmxxf::Backend (dlssnr/lmxxf), hosting the
// open-source lmxxf runtime. The bridge builds one of them from the [DlssNr] NrBackend choice
// and never looks past this interface again.
class NeuralBackend
{
  public:
    virtual ~NeuralBackend() = default;
    // Records pre-SR work. Returns a FP16 input for the upscaler, or nullptr on skip/failure.
    virtual ID3D12Resource* Record(ID3D12GraphicsCommandList*, const Frame&, const Settings&) = 0;
    // Cheap snapshot for the menu; safe to call every frame.
    virtual Stats GetStats() const = 0;
    // Which of the lists about to be executed is the backend's own (for isolation), or -1.
    virtual int PendingListIndex(UINT, ID3D12CommandList* const*) const = 0;
    // Called BEFORE and AFTER every real queue submission, including non-upscale lists.
    virtual void Submitting(ID3D12CommandQueue*, UINT, ID3D12CommandList* const*) = 0;
    virtual void Submitted(ID3D12CommandQueue*, UINT, ID3D12CommandList* const*) = 0;
    // (0.3.3.2) Discard evidence ([DlssNr] AmdNeuralListRecovery): `list` was reset by the game (the
    // bridge's ID3D12GraphicsCommandList::Reset hook, only for the list NR last recorded into) or, with
    // `ownBridge`, dropped by one of OptiScaler's own bridges without being executed. Any thread, including
    // from inside the runtimes' own recording (they reset their own lists while the backend's lock is
    // held), so it never takes that lock: atomics only. The backend acts on it at its next Record. A reset
    // proves a discard only where the game's neural lists are known to reach ExecuteCommandLists under the
    // pointer they were recorded with: behind a wrapper (UE5 titles) the list may have run under another
    // pointer and been reset afterwards, so each backend checks that first. A bridge's own drop is certain.
    virtual void ListReset(ID3D12CommandList*, bool ownBridge) = 0;
    // Diagnostic snapshot only; does not flush, cancel, or destroy GPU work. Never waits for the
    // recording lock (0.3.3.2): a busy backend writes the reason without the snapshot.
    virtual void TraceBoundary(const std::string&) = 0;
    virtual bool Ready() = 0;
    virtual bool Shutdown() = 0; // call before loader-lock teardown, after all submissions
    virtual void InvalidateHistory() = 0; // applied at the next safe recording boundary
    virtual void RequestCapture(unsigned delayMs = 0) = 0;
    virtual std::string Status() const = 0;
    virtual UINT64 RecordedFrames() const = 0;
    // (0.3.3.2) One line in the backend's own log, from the menu thread (the NR on/off note). Unlike
    // TraceBoundary it never touches the status line (a stopped runtime keeps saying why) and never
    // waits on the recording path.
    virtual void NoteLine(const std::string&) = 0;
    // (0.3.3.2) The runtime stopped for this session: refused, failed to load or create, or poisoned.
    virtual bool Stopped() const = 0;
};

// Process lifetime owner: intentionally not destroyed/unloaded while HIP threads exist.
class Backend : public NeuralBackend
{
    struct Impl;
    Impl* p;

  public:
    Backend(ID3D12Device*, ID3D12CommandQueue*, const std::filesystem::path& directory);
    // Records pre-SR work. Returns a FP16 input for the upscaler, or nullptr on skip/failure.
    ID3D12Resource* Record(ID3D12GraphicsCommandList*, const Frame&, const Settings&) override;
    // Cheap snapshot for the menu; safe to call every frame.
    Stats GetStats() const override;
    // Bind the render queue and enqueue a migration dependency BEFORE Execute.
    // Required for every submission; does not publish HIP work yet.
    int PendingListIndex(UINT, ID3D12CommandList* const*) const override;
    void Submitting(ID3D12CommandQueue*, UINT, ID3D12CommandList* const*) override;
    // Diagnostic snapshot only; does not flush, cancel, or destroy GPU work.
    void TraceBoundary(const std::string&) override;
    // Must run immediately AFTER real queue submission, including non-upscale lists.
    void Submitted(ID3D12CommandQueue*, UINT, ID3D12CommandList* const*) override;
    void ListReset(ID3D12CommandList*, bool ownBridge) override;
    bool Ready() override;
    bool Shutdown() override; // call before loader-lock teardown, after all submissions
    void InvalidateHistory() override; // applied at the next safe recording boundary
    // The next 8 frames, like the trigger file: now, or after `delayMs` (menu closed, moving).
    void RequestCapture(unsigned delayMs = 0) override;
    std::string Status() const override;
    UINT64 RecordedFrames() const override;
    void NoteLine(const std::string&) override;
    bool Stopped() const override;
};
} // namespace AmdPreSr
