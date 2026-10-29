// ILION adaptation: AMD NR only; optional GI and presentation paths excluded.
// Copyright (c) 2026 3zwr1 (AMDNR). Part of AMDNR (GPL-3.0; see Licenses/AMDNR_NOTICE.txt).
// SPDX-License-Identifier: GPL-3.0-or-later
#include "pch.h"
#include "LmxxfBackend.h"
#include "LmxxfTierPolicy.h" // (0.3.4) the network size tier rules (PlanLmxxfSize, the small-tier policy); pure
#include "LateSubmitGrace.h" // (0.3.4, P3) a job whose list the game submits after the next frame's Evaluate; pure
#include "runtime/LmxxfNrApi.h"
#include "../amd/NrControls.h" // (0.3.4, AUTOMASK) the character mask controls the host sends (ResolveNrControls)
#include "../amd/TemporalStability.h"
#include <wrl/client.h>
#include <d3dcompiler.h>
#include <dxgi1_4.h> // IDXGIAdapter3::QueryVideoMemoryInfo, for the memory telemetry (MemoryNote)
#include <psapi.h>   // GetProcessMemoryInfo (PrivateUsage), the same
#include "../amd/SystemCompiler.h"
#include "../amd/ColorEncoding.h" // 0.3.4 (P7.5): AmdPreSr::EncodingShader, the danielblnc host's sRGB / Gamma 2.2 transfer
#include "../amd/InterleavePacing.h" // 0.3.4 (FB-L5): the pacer's Note / SetStrength, as the danielblnc host calls them
// The RenoDX colour composition's shared HLSL (AmdPreSr::AmdNrComposeHlsl) and the menu note
// (DlssNr::AmdBridge::SetCompositionNote). At global scope: both open their own namespaces.
#include "../amd/NrCompose.h"
#include "../amd/AmdBridge.h"
#include "../amd/ComIdentity.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <vector>

namespace Lmxxf
{
using Microsoft::WRL::ComPtr;
namespace
{
constexpr wchar_t kRuntimeDll[] = L"LmxxfNrRuntime.dll";
// Vulkan bridge, self-healing: live only while a first answer is outstanding - written before
// PrepareFrame (the HIP warm-up), removed when the first answer is consumed or when an attempt
// demonstrably failed without hanging. AmdBridge.cpp reads the same name at the next start and
// does not run lmxxf when it is still there (that session stopped before its first answer).
constexpr wchar_t kVkLaunchMarker[] = L"lmxxf_vk_launch.pending";
// A failed attempt removes the marker; a later attempt writes it again, at most once per this many
// milliseconds (a rate limit, not a cap: an attempt that finally hangs must still leave the file).
constexpr ULONGLONG kVkMarkerRewriteMs = 5000;
// Sessions of queues the neural list left (Submitted), waiting for their lists before api.Destroy.
// Past this many the oldest is left alive (leaked), as every such session was before.
constexpr size_t kMaxRetired = 3;
constexpr UINT kNetMaxW = 1920, kNetMaxH = 1080; // the runtime rejects anything larger
// (0.3.4) The frame size this host sends by default: the 0.3.3.2 layout (ends with output_smooth_threshold, 128
// bytes with the tail padding), not sizeof. The published runtime 1343bbc5 refuses any other size on every frame, so
// the default frame stays 128 when LmxxfNrApi.h grows (the character mask controls); a larger size is sent only with
// the feature that needs it.
constexpr uint32_t kFrameInfo0332Size =
    static_cast<uint32_t>((offsetof(LmxxfNrFrameInfo, output_smooth_threshold) + sizeof(float) + 7u) & ~7u);
static_assert(kFrameInfo0332Size == 128, "the 0.3.3.2 LmxxfNrFrameInfo is 128 bytes");
// (0.3.4, AUTOMASK) The frame that carries the character mask controls: the whole struct, the controls at 124..143. The
// runtime refuses the controls flag with any other size ("PrepareFrame: controls need struct_size 144").
static_assert(sizeof(LmxxfNrFrameInfo) == 144 && offsetof(LmxxfNrFrameInfo, control_tone) == 124,
              "LmxxfNrFrameInfo with the character mask controls is 144 bytes, the controls from 124");
// Full network: set when an LmxxfNrRuntime.dll older than LMXXF_NR_FRAME_FLAG_FULL_NETWORK has
// refused it (Impl::fullNetRefused, the session latch). Process-wide, as the DLL is: the menu
// reads it through FullNetworkRefused() on the present thread for its note.
std::atomic<bool> fullNetworkRefused { false };
// (0.3.4, AUTOMASK) The same for the character mask controls (LMXXF_NR_FRAME_FLAG_CONTROLS + the 144-byte frame): set
// when the LmxxfNrRuntime.dll in use refused them (0.3.3.2's 1343bbc5 and older: "struct_size mismatch"), read by the
// menu through ControlsRefused() for its note. From then on the backend sends the 0.3.3.2 frame (Impl::ctlRefused).
std::atomic<bool> controlsRefused { false };
// (0.3.4, runtime work) The small network tiers: true while the runtime in use took LmxxfNrSetTierPolicy's small-tier
// bit (Impl::tierSmall), and the tier cap the sizing uses (0 = none). Read by the menu (SmallTierPolicy, TierCapInUse).
std::atomic<bool> smallTierPolicy { false };
std::atomic<int> tierCapInUse { 0 };
// (0.3.4, lmxxf Fast mode) [DlssNr] AmdLmxxfFastMode as the last Record applied it (Impl::NoteFastMode): 0 off, -1 on
// with no tier below, else the tier the default sizing feeds (the network runs the next one down). For the menu
// (FastModeFromTier).
std::atomic<int> fastModeFromTier { 0 };
// (0.3.4, P3) Late submission, for the menu (LateSubmitNrShare): -1 while the last stats window saw no neural list
// submitted after the next frame's Record, else the share of that window's frames that got a fresh network answer.
std::atomic<float> lateSubmitNrShare { -1.f };
// The runtime's optional export (AMDNR 0.3.4 runtime; LmxxfNrApi.h once RT-1 merges): int32_t (void* context,
// uint32_t flags); bit 0 = the small tiers 360 / 576; read at the session's next network size resolve (the host calls it
// right after Create, before the first PrepareFrame). Found with GetProcAddress, so an older runtime is never asked.
constexpr char kSetTierPolicyExport[] = "LmxxfNrSetTierPolicy";
constexpr uint32_t kTierPolicySmall = 1u;
// The small tiers on RX 7000 / Radeon 8060S desktop (gfx11, not a handheld) without a cap: reached only when the NR size
// itself is 1024x576 or smaller. Off until the owner's 360 / 576 look check (runtime work OD-C, V8); the key's 360 / 576
// caps reach them on any GPU meanwhile.
constexpr bool kGfx11DesktopSmallTiers = false;
// R3 (0.3.3.2 rebuild): whether the LmxxfNrRuntime.dll in use imports its HIP-shared buffers once per process and reuses
// them across bridge rebuilds (it exports LmxxfNrGetImportPoolStats and DLSS5_IMPORT_POOL is not 0). Process-wide, as
// the DLL is: set by Load, read through ImportPoolState() by the menu and Dynamic NR.
std::atomic<int> importPool { -1 }; // -1 not loaded, 0 older runtime (re-imports per rebuild), 1 reuse

// Three small compute passes, all trivial on purpose.
//
// Exposure: one texel. The game's exposure texture times the NGX scale over the pre-exposure,
// as the danielblnc path computes it, or 1 when the title publishes none. The network's codec
// (native_codec_encode.hlsl) is NVIDIA's captured "mode 1" contract: the colour over a paper
// white, a shoulder above 0.75, then the sRGB curve - and NVIDIA divides that paper white by
// the exposure. The runtime's C ABI has no exposure input, so the multiplication is done here,
// on the copy the network is fed, and undone on its edit. Without it an HDR title's linear
// colour (Forza Horizon 6: scene mean ~1500) saturates to white before the network sees it.
inline constexpr char ExposureShader[] = R"(
Texture2D<float4> src : register(t0);
RWTexture2D<float> dst : register(u0);
cbuffer P : register(b0) { uint w; uint h; uint mode; uint pad; float pre; float scale; float pad2; float pad3; }
[numthreads(1,1,1)] void main(uint3 tid : SV_DispatchThreadID) {
 float e = 1.0, raw = 0.0;
 // Outside 1/256..256 the title's value is not an exposure (zero, a NaN, a debug fill): 1 is
 // used and the raw value is kept in texel 1 so the stats line can show what was refused.
 // Mode 2 (auto-exposure) clamps at the loop's own floor, 1/65536 since 0.3.3 (AutoExposureHlsl.h).
 if (mode == 0u) { float v = src.Load(int3(0,0,0)).r * scale / pre; raw = v; if (isfinite(v) && v > 0.00390625 && v < 256.0) e = v; }
 if (mode == 2u) { float v = src.Load(int3(0,0,0)).r; raw = v; if (isfinite(v) && v > 0.0) e = clamp(v, 1.0 / 65536.0, 16384.0); }
 dst[uint2(0,0)] = e;
 dst[uint2(1,0)] = raw;
}
)";
// AUTO-EXPOSURE, for a title that publishes no exposure texture. danielblnc's runtime never feeds
// the network the colour as is: it measures the encoded picture's mean and moves an exposure
// until that mean sits near 0.5 (its log: "auto-exposure: encoded mean 0.500 -> exposure 89"),
// in every game, HDR or SDR. Fed as is, the lmxxf network sees a picture far from the operating
// point it was trained at - too bright for linear HDR, too dark for a dim SDR scene - and its
// answer drifts in tone and colour; that drift is the "colour difference between daniel and
// lmxxf" (the community workaround, Edit colour 0, throws the drifted chroma away). The same
// loop here: one thread, a 64x36 grid of the render-size copy, the codec's own encode
// (paper white 1, shoulder above 0.75, sRGB curve) on the luminance at the previous exposure,
// then a step toward the target. The encoded mean responds to the exposure roughly as e^(1/2.4),
// so the step is (target / mean)^2.4, capped per feed; the first feed takes the whole step.
// State in the UAV itself: [0] the exposure to feed with, [1] the mean measured, [2] the running
// exposure, [3] a marker that says the state is initialised (uninitialised memory is not).
#include "../amd/AutoExposureHlsl.h"
// Blit: read the game's colour (or motion) through a typed view and write FP16 (RG16 for
// motion) at the target size. PIXEL-EXACT: target pixel p reads source pixel p * (sx, sy),
// in the source texture's own pixel units - never a normalized stretch over the whole
// allocation. Unreal allocates the scene colour larger than the render area, and a stretch
// over the allocation both drags the padding in at the right and bottom edges and resamples
// every pixel, which is a blur. At sx = sy = 1 this is an exact copy. mode bit 0: multiply by
// the exposure texel (the copy the network is fed). The runtime is handed OUR texture, never
// the game's: the game's colour pointer rotates every frame in Unreal and the runtime
// rebinds (and may drain the GPU) on every change. mode bit 1: add k times the edit texture
// (t2, render size) before the exposure - the extra "passes" (see the feed below).
//
// Alpha: carried as is into `full` and `fed` (the newer RenoDX DLSS addon's bridge also passes
// the source alpha to the model; its older v4.7 build forced 1.0). RenoDX by clshortfuse (Carlos
// Lopez Jr.), MIT, https://github.com/clshortfuse/renodx; see Licenses/RenoDX_ATTRIBUTION.txt.
// No RenoDX code here, behaviour reference only. lmxxf CANNOT hand the alpha to the network: the
// pak's encode writes proxy alpha 1 (NVIDIA's captured mode-1 contract) and the prefix reads RGB
// only. The output alpha is `full`'s (TemporalStability writes c.a); the answer's alpha is ignored.
// Non-finite input is cleaned per part: a non-finite rgb is black and keeps its own alpha, a
// non-finite alpha is 1 and keeps its rgb. Before, either blacked the whole pixel: a non-finite
// alpha from the game in `full` (the output), and an FP32 alpha above 65504 - inf once in FP16
// `full` - in `fed` (the network's input). The motion blit compiles this same text (OUT_T
// float2): a 4-channel motion texel whose only non-finite channel is .a now keeps its vector, as
// danielblnc's MotionShader (float2 reads) always did; a non-finite x, y or b still zeroes it, and
// RG motion reads b = 0, a = 1. A finite texel takes exactly the path it took before. lmxxf only:
// danielblnc's copy, proxy, tail, stability and resolve test RGB alone, so a non-finite alpha
// already passes through there without blacking the pixel.
inline constexpr char BlitShader[] = R"(
Texture2D<float4> src : register(t0);
Texture2D<float> expo : register(t1);
Texture2D<float4> edit : register(t2);
RWTexture2D<OUT_T> dst : register(u0);
SamplerState samp : register(s0);
cbuffer P : register(b0) { uint w; uint h; uint mode; float k; float sx; float sy; float srcW; float srcH; }
[numthreads(8,8,1)] void main(uint3 tid : SV_DispatchThreadID) {
 if (tid.x >= w || tid.y >= h) return;
 float2 uv = ((float2(tid.xy) + 0.5) * float2(sx, sy)) / float2(srcW, srcH);
 float4 v = src.SampleLevel(samp, uv, 0);
 if (!all(isfinite(v.rgb))) v.rgb = 0.0;
 if (!isfinite(v.a)) v.a = 1.0;
 if ((mode & 2u) != 0u) { float3 ed = edit.SampleLevel(samp, uv, 0).rgb; if (all(isfinite(ed))) v.rgb = max(v.rgb + k * ed, 0.0); }
 if ((mode & 1u) != 0u) { float e = expo.Load(int3(0,0,0)).r; v.rgb *= e; }
 dst[tid.xy] = (OUT_T) v;
}
)";
// NO-MOTION CARRY (final image mode: the title publishes no motion vectors). Three small
// shaders estimate the motion themselves and carry the network's edit with it:
//   LumaDownShader   - the frame's luma (sqrt space, roughly perceptual) averaged 4x4: the
//                      quarter-resolution picture the matcher works on.
//   BlockMatchShader - per quarter pixel, the offset into a reference picture where a 3x3
//                      patch matches best within +-4 quarter pixels (+-16 full pixels), and how
//                      well it matches (0..1). Flat patches stay put and count as matched.
//   EditWarpShader   - reads the edit through that flow: mode 0 brings the carried edit onto
//                      this frame (faded by match quality); mode 1 brings the old edit into a
//                      fresh answer's space and blends the two (weight k = Temporal stability,
//                      also faded by match quality, so a mismatch takes the fresh edit).
// The edit lives in the space of the frame the network was fed; the answer may be many frames
// old at high frame rates (GTA V: one answer per ~7 frames at 160 fps), which is exactly when
// an in-place carry ghosts and a colour gate cannot tell a moving texture from a still one.
inline constexpr char LumaDownShader[] = R"(
Texture2D<float4> src : register(t0);
RWTexture2D<float> dst : register(u0);
cbuffer P : register(b0) { uint w; uint h; uint mode; float k; float fullW; float fullH; float f2; float f3; }
[numthreads(8,8,1)] void main(uint3 tid : SV_DispatchThreadID) {
 if (tid.x >= w || tid.y >= h) return;
 float s = 0.0;
 [unroll] for (uint y = 0u; y < 4u; ++y) [unroll] for (uint x = 0u; x < 4u; ++x) {
  uint2 p = uint2(min(tid.x * 4u + x, uint(fullW) - 1u), min(tid.y * 4u + y, uint(fullH) - 1u));
  float3 c = src.Load(int3(p, 0)).rgb;
  if (!all(isfinite(c))) c = 0.0;
  s += dot(sqrt(max(c, 0.0)), float3(0.2126, 0.7152, 0.0722));
 }
 dst[tid.xy] = s * (1.0 / 16.0);
}
)";
inline constexpr char BlockMatchShader[] = R"(
Texture2D<float> cur : register(t0);
Texture2D<float> ref : register(t1);
RWTexture2D<float4> dst : register(u0);
cbuffer P : register(b0) { uint w; uint h; uint mode; float k; float thr; float bias; float f2; float f3; }
float Sad(int2 p, int2 d) {
 float s = 0.0;
 int2 lim = int2(int(w) - 1, int(h) - 1);
 [unroll] for (int y = -1; y <= 1; ++y) [unroll] for (int x = -1; x <= 1; ++x) {
  int2 a = clamp(p + int2(x, y), int2(0, 0), lim);
  int2 b = clamp(p + d + int2(x, y), int2(0, 0), lim);
  s += abs(cur.Load(int3(a, 0)) - ref.Load(int3(b, 0)));
 }
 return s;
}
[numthreads(8,8,1)] void main(uint3 tid : SV_DispatchThreadID) {
 if (tid.x >= w || tid.y >= h) return;
 int2 p = int2(tid.xy);
 int2 lim = int2(int(w) - 1, int(h) - 1);
 float c0 = cur.Load(int3(p, 0));
 float contrast = 0.0;
 [unroll] for (int y = -1; y <= 1; ++y) [unroll] for (int x = -1; x <= 1; ++x)
  contrast += abs(cur.Load(int3(clamp(p + int2(x, y), int2(0, 0), lim), 0)) - c0);
 float bestSad = Sad(p, int2(0, 0));
 float bestCost = bestSad;
 int2 bd = int2(0, 0);
 if (contrast > 3.0 * thr) {
  for (int dy = -4; dy <= 4; ++dy) for (int dx = -4; dx <= 4; ++dx) {
   if (dx == 0 && dy == 0) continue;
   float s = Sad(p, int2(dx, dy));
   float cost = s + bias * float(abs(dx) + abs(dy));
   if (cost < bestCost) { bestCost = cost; bestSad = s; bd = int2(dx, dy); }
  }
 }
 float q = 1.0 - saturate((bestSad / 9.0) / (3.0 * thr));
 if (!isfinite(q)) q = 0.0;
 dst[tid.xy] = float4(float2(bd) * 4.0, q, 0.0);
}
)";
inline constexpr char EditWarpShader[] = R"(
Texture2D<float4> hist : register(t0);
Texture2D<float4> flow : register(t1);
Texture2D<float4> fresh : register(t2);
RWTexture2D<float4> dst : register(u0);
SamplerState samp : register(s0);
cbuffer P : register(b0) { uint w; uint h; uint mode; float k; float qw; float qh; float cap; float f3; }
[numthreads(8,8,1)] void main(uint3 tid : SV_DispatchThreadID) {
 if (tid.x >= w || tid.y >= h) return;
 float2 uvq = (float2(tid.xy) + 0.5) / (4.0 * float2(qw, qh));
 float4 fl = flow.SampleLevel(samp, uvq, 0);
 float q = isfinite(fl.z) ? saturate(fl.z) : 0.0;
 float2 off = all(isfinite(fl.xy)) ? fl.xy : float2(0.0, 0.0);
 float2 uv = (float2(tid.xy) + 0.5 + off) / float2(float(w), float(h));
 float3 e = hist.SampleLevel(samp, uv, 0).rgb;
 if (!all(isfinite(e))) e = 0.0;
 if (mode == 1u) {
  float3 f = fresh[tid.xy].rgb;
  if (!all(isfinite(f))) f = 0.0;
  e = lerp(f, e, k * q);
 } else e *= q;
 if (cap > 0.0) e = clamp(e, -cap, cap);
 dst[tid.xy] = float4(e, 1.0);
}
)";
// Depth resample, nearest: after Ray Regeneration the colour is display-sized while the depth
// stays on the render grid; the carry and the edge guard want it on the colour's grid.
inline constexpr char DepthBlitShader[] = R"(
Texture2D<float> src : register(t0);
RWTexture2D<float> dst : register(u0);
cbuffer P : register(b0) { uint w; uint h; uint mode; float k; float sx; float sy; float srcW; float srcH; }
[numthreads(8,8,1)] void main(uint3 tid : SV_DispatchThreadID) {
 if (tid.x >= w || tid.y >= h) return;
 if (mode == 1u) { dst[tid.xy] = 0.5; return; } // flat: every depth test passes
 uint2 sp = uint2(min(uint(float(tid.x) * sx), uint(srcW) - 1u), min(uint(float(tid.y) * sy), uint(srcH) - 1u));
 float d = src.Load(int3(sp, 0));
 dst[tid.xy] = isfinite(d) ? d : 0.0;
}
)";
// Motion chain: the vectors the network's history path reads, at the model's size, in RENDER
// pixels (current -> previous frame). With the network running every frame that is this frame's
// vector; when it ran two frames ago (Model interleave) the history is two frames old, so the
// previous frame's vector is followed from where this one lands and added - the pixel's path
// over both frames. Without this the chain broke on every interleaved frame and the network
// never saw its history at all under interleave.
inline constexpr char MotionChainShader[] = R"(
Texture2D<float4> src : register(t0);
Texture2D<float2> hist : register(t1);
RWTexture2D<float2> dst : register(u0);
SamplerState samp : register(s0);
cbuffer P : register(b0) { uint w; uint h; uint mode; float k; float sx; float sy; float srcW; float srcH; float scaleX; float scaleY; float renderW; float renderH; }
[numthreads(8,8,1)] void main(uint3 tid : SV_DispatchThreadID) {
 if (tid.x >= w || tid.y >= h) return;
 float2 uv = ((float2(tid.xy) + 0.5) * float2(sx, sy)) / float2(srcW, srcH);
 float2 m0 = src.SampleLevel(samp, uv, 0).xy * float2(scaleX, scaleY);
 if (!all(isfinite(m0))) m0 = float2(0.0, 0.0);
 float2 total = m0;
 if ((mode & 1u) != 0u) {
  float2 pr = (float2(tid.xy) + 0.5) * float2(renderW / float(w), renderH / float(h)) + m0;
  float2 m1 = hist.SampleLevel(samp, pr / float2(renderW, renderH), 0);
  if (all(isfinite(m1))) total += m1;
 }
 dst[tid.xy] = total;
}
)";
// (0.3.4, P3) Late edit warp: the answer of a job whose list the game submitted after the next frame's Evaluate (a
// graced job, LateSubmitGrace.h) is consumed one frame later than usual, so its edit is in the space of the frame
// before the previous one. The carry (preset 9 / 11) takes the fresh edit as the previous frame's, so the edit is
// first brought into that frame: each pixel of the previous frame reads the edit where its content was one frame
// earlier, by that frame's vectors (render pixels, current -> previous, as the temporal pass stored them: t1) plus the
// jitter grid's change (k0/k1, the carry's own convention: prev = p + 0.5 + m + jit). Outside the frame: no edit.
inline constexpr char LateEditWarpShader[] = R"(
Texture2D<float4> edit : register(t0);
Texture2D<float2> mv : register(t1);
RWTexture2D<float4> dst : register(u0);
SamplerState samp : register(s0);
cbuffer P : register(b0) { uint w; uint h; uint mode; float k; float jitX; float jitY; float f2; float f3; }
[numthreads(8,8,1)] void main(uint3 tid : SV_DispatchThreadID) {
 if (tid.x >= w || tid.y >= h) return;
 float2 m = mv.Load(int3(tid.xy, 0));
 if (!all(isfinite(m))) m = float2(0.0, 0.0);
 float2 jit = float2(jitX, jitY);
 if (!all(isfinite(jit))) jit = float2(0.0, 0.0);
 float2 at = float2(tid.xy) + 0.5 + m + jit;
 float3 e = float3(0.0, 0.0, 0.0);
 if (at.x >= 0.0 && at.y >= 0.0 && at.x <= float(w) && at.y <= float(h))
  e = edit.SampleLevel(samp, at / float2(float(w), float(h)), 0).rgb;
 if (!all(isfinite(e))) e = float3(0.0, 0.0, 0.0);
 dst[tid.xy] = float4(e, 0.0);
}
)";
// Residual: the network's answer, back in the game's units (over the exposure the copy was
// scaled with), minus the RAW frame it answered for, sampled at render resolution (a bilinear
// lift when the model ran smaller, texel-exact otherwise); or zeros (mode 1) when a history
// must be dropped. Then the EDIT SHAPER, our own controls on the model's edit before anything
// carries it:
//   detail     - gain on the edit's high band (the edit minus its 5-tap cross blur): 1 leaves
//                the model's answer alone, above it the fine re-rendering stands out more,
//                below it only the broad tonal change is kept;
//   saturation - the edit's chroma against its own luminance: 0 = a purely tonal edit;
//   edge guard - where the DEPTH steps (an object against its background) the edit is faded,
//                so the model's re-lighting of one surface cannot bleed onto the other as a
//                halo. Relative depth step, so it works on inverted and on far-range depth.
// AMDNR_DELTA, the RenoDX colour composition's variant (AmdComposition 1; the Classic PSO is
// compiled without it, so its text and code are what they were): t0 is then the compose delta,
// Tail(fed, answer) - fed at the model's size (NrCompose.h, NRCOMPOSE_DELTA), and t4 the answer.
// The edit is delta / e, the MATCHED lift up(composed - fed) / e: at the model's full size it is
// composed / e - raw up to the FP16 rounding of fed; below it (render above 1080p, NR under 100%,
// after RR) it no longer subtracts the raw frame's own high band, as danielblnc's resolve lifts
// (edited - baseline). A pixel the compose pass flagged (alpha != 0: a non-finite fed or answer,
// e.g. an FP16 overflow of raw x e; under the bilinear lift, a flagged neighbour) takes today's
// answer / e - raw instead. The shaper below runs on either unchanged.
//
// HIGHLIGHT CHROMA GUARD ([DlssNr] AmdLmxxfHighlightChromaGuard, on by default in 0.3.3 - tested in Silent
// Hill 2; chromaGuard, the 9th root constant, 1 = on; 0.3.3 colour fix, AMDNR). The codec's shoulder
// (native_codec_encode.hlsl, above 0.75 fed) squashes each channel on its own, so a bright pixel
// reaches the network close
// to white; the decode (native_codec_decode.hlsl Upgrade, then the Colour strength blend) takes
// the colour from the network's answer, so that pixel comes back GREY at the right brightness.
// It happens wherever the feed passes the shoulder: a raised auto-exposure in a dark, foggy
// scene (Silent Hill 2, "the colours suddenly turn grey"), a title exposure that is too high.
// Per pixel the fed max channel mf = max(raw x e) sets w = smoothstep(0.75, 1.5, mf), and the
// answer A becomes lerp(A, rs x Y(A) / Y(rs), w), rs = raw x e in the answer's (fed) units, Y the
// BT.709 luminance: the model keeps the light it gave the pixel, the original gives it its
// colour. w = 0 (mf <= 0.75, or the guard off) skips the step, so such a pixel's edit is exactly
// what it was; a non-finite value, an answer without light or a fed luminance at or below 1e-6
// keeps the answer as it is. RenoDX too: its answer is RenoDX's `upgraded`, which takes the same
// grey chroma from the network, and the tail at Composition colour 1 keeps it (the Highlight
// guard bounds only the light). There the guard runs in the compose pass on the network's answer
// BEFORE the tail (NrCompose.h GuardAnswer, NRCOMPOSE_DELTA only, switched by the compose pass's
// 12th constant): the same w and lerp with o = the fed picture, then the tail composes the guarded
// answer, so Composition colour still acts on those pixels. (A pre-release build ran it here, on
// A = rs + delta AFTER the tail, which handed every pixel fed past 1.5 the original's colour
// whatever Composition colour was set to; with the guard on by default that would have cancelled
// the slider there.) This pass keeps the guard only on the flagged pixels' answer, as in Classic;
// guard off, the RenoDX edit is bit-identical to before. Host-side only: the runtime, its network
// and the probe hash are untouched.
//
// RESIDUAL EDGE FADE (0.3.4, P7.8; [DlssNr] AmdResidualFade, shared with danielblnc, 0..0.25, default 0 = off):
// the cbuffer's spare fourth dword (`pad` before 0.3.4, so the block keeps its size) now carries the fade, the
// width of a band at the frame's border, as a fraction of the frame, over which the finished edit rolls off to
// zero; a corner gets both roll-offs. The same rule as danielblnc's resolve (AmdPreSr.cpp ResolveShader), and
// the same condition: the host sends it only while the edit is lifted (the network fed at a size other than the
// frame) and 0 otherwise, so at 0 the step is skipped and the edit is exactly what it was.
inline constexpr char ResidualShader[] = R"(
Texture2D<float4> net : register(t0);
Texture2D<float4> raw : register(t1);
Texture2D<float> expo : register(t2);
Texture2D<float> depth : register(t3);
#if defined(AMDNR_DELTA)
Texture2D<float4> ans : register(t4);
#endif
RWTexture2D<float4> dst : register(u0);
SamplerState samp : register(s0);
cbuffer P : register(b0) { uint w; uint h; uint mode; float fade; float intensity; float detail; float saturation; float edgeGuard; float chromaGuard; }
// The highlight chroma guard's weight: 0 below the codec's shoulder (and with the guard off).
float GuardWeight(float3 rs) {
 if (!(chromaGuard > 0.5)) return 0.0;
 float wt = smoothstep(0.75, 1.5, max(rs.r, max(rs.g, rs.b)));
 return wt > 0.0 ? wt : 0.0;
}
// The original's colour at the answer's light, rs x Y(a) / Y(rs); false where it is undefined.
bool GuardTarget(float3 a, float3 rs, out float3 g) {
 g = a;
 float ya = dot(a, float3(0.2126, 0.7152, 0.0722));
 float yr = dot(rs, float3(0.2126, 0.7152, 0.0722));
 if (!all(isfinite(rs)) || !isfinite(ya) || !(ya > 0.0) || !(yr > 1e-6)) return false;
 g = rs * (ya / yr);
 return all(isfinite(g));
}
float3 editAt(int2 p, float e) {
 float2 uv = (float2(clamp(p, int2(0,0), int2(int(w)-1, int(h)-1))) + 0.5) / float2(float(w), float(h));
 float3 rw = raw.SampleLevel(samp, uv, 0).rgb;
 float3 rs = rw * e;
 float wt = GuardWeight(rs);
 float3 g;
#if defined(AMDNR_DELTA)
 float4 d = net.SampleLevel(samp, uv, 0);
 float3 r = d.rgb / e; // the compose pass guarded the answer before the tail (NrCompose.h GuardAnswer)
 if (d.a != 0.0) {
  float3 a = ans.SampleLevel(samp, uv, 0).rgb;
  if (wt > 0.0) { if (GuardTarget(a, rs, g)) a = lerp(a, g, wt); }
  r = a / e - rw;
 }
#else
 float3 a = net.SampleLevel(samp, uv, 0).rgb;
 if (wt > 0.0) { if (GuardTarget(a, rs, g)) a = lerp(a, g, wt); }
 float3 r = a / e - rw;
#endif
 return all(isfinite(r)) ? r : float3(0.0, 0.0, 0.0);
}
[numthreads(8,8,1)] void main(uint3 tid : SV_DispatchThreadID) {
 if (tid.x >= w || tid.y >= h) return;
 if (mode != 0u) { dst[tid.xy] = float4(0.0, 0.0, 0.0, 0.0); return; }
 int2 p = int2(tid.xy);
 float e = expo.Load(int3(0,0,0)).r;
 if (!(isfinite(e) && e > 0.0)) e = 1.0;
 float3 c = editAt(p, e);
 float3 r = c;
 if (abs(detail - 1.0) > 0.001) {
  float3 n = editAt(p + int2(0,-1), e), s = editAt(p + int2(0,1), e), wv = editAt(p + int2(-1,0), e), ev = editAt(p + int2(1,0), e);
  float3 blur = (4.0 * c + n + s + wv + ev) * 0.125;
  r = blur + (c - blur) * detail;
 }
 if (abs(saturation - 1.0) > 0.001) {
  float lum = dot(r, float3(0.2126, 0.7152, 0.0722));
  r = lum + (r - lum) * saturation;
 }
 if (edgeGuard > 0.001) {
  float dc = depth.Load(int3(p,0)).r;
  float g = 0.0;
  g = max(g, abs(depth.Load(int3(clamp(p + int2(0,-1), int2(0,0), int2(int(w)-1,int(h)-1)),0)).r - dc));
  g = max(g, abs(depth.Load(int3(clamp(p + int2(0, 1), int2(0,0), int2(int(w)-1,int(h)-1)),0)).r - dc));
  g = max(g, abs(depth.Load(int3(clamp(p + int2(-1,0), int2(0,0), int2(int(w)-1,int(h)-1)),0)).r - dc));
  g = max(g, abs(depth.Load(int3(clamp(p + int2( 1,0), int2(0,0), int2(int(w)-1,int(h)-1)),0)).r - dc));
  float rel = isfinite(g) && isfinite(dc) ? g / max(abs(dc), 1e-5) : 0.0;
  float edge = saturate((rel - 0.01) / 0.05);
  r *= 1.0 - edgeGuard * edge;
 }
 r *= intensity;
 if (!all(isfinite(r))) r = float3(0.0, 0.0, 0.0);
 if (fade > 0.0) { float2 uv = (float2(tid.xy) + 0.5) / float2(float(w), float(h)); float2 b = min(uv, 1.0 - uv) / fade; r *= saturate(min(b.x, b.y)); }
 dst[tid.xy] = float4(r, 0.0);
}
)";
// Stats: what the log needs to answer "it does not work" in any game. Sums, fixed-point, into a
// 12x1 R32_UINT texture. Mode 0 (t0 = the model's fresh edit, t1 = the fed picture): [0] sum of the
// edit's magnitude x256, [1] sum of the fed luminance x16, [3] max edit magnitude x65536. Mode 2
// (t0 = the temporal pass's residual: rgb = the CARRIED edit, alpha = its reprojection validity):
// [2] sum carried x256, [4] sum keep x256, [5] max carried x65536. Mode 3 (t0 = the title's reactive
// mask, f0 = its channel count): [6] sum of the mask x256. Mode 4 (t0 = the vectors the temporal pass
// reads, f0/f1 = the scale to pixels): [7] sum |mv| px x4 (capped 255), [8] count of vectors the
// pass would reject (non-finite or over half the height) x256. Mode 1 clears. Read back every
// kStatsEvery frames. fresh vs carried says whether an edit the model made ever lands; reactive
// and vectors say why not. 0.3.3 colour fix, mode 0 as well: [9] the count of fed pixels whose
// SMALLEST channel is above 1.0 (past paper white in every channel: the blown-feed test of the
// exposure self-heal), [10] the count whose LARGEST channel is above 0.75 (in the codec's
// shoulder: the pixels the highlight chroma guard and the auto-exposure highlight cap act on).
// Mode 1 runs as ONE 8x8 group (Run(..., groups = false)), so it clears by the thread's flat index
// tid.y * 8 + tid.x: all 12 slots. (0.3.4 fix: the test was tid.x < 12 on row 0, which in an 8-wide
// group reached slots 0-7 only, so 8-11 - rejected vectors, blown, shoulder - summed over the whole
// session and a long session's shoulder share passed 100%, which could latch the exposure self-heal.)
inline constexpr char StatsShader[] = R"(
Texture2D<float4> edit : register(t0);
Texture2D<float4> fedTex : register(t1);
RWTexture2D<uint> stats : register(u0);
SamplerState samp : register(s0);
cbuffer P : register(b0) { uint w; uint h; uint mode; float pad; float f0; float f1; float f2; float f3; }
groupshared float gsA[64];
groupshared float gsL[64];
groupshared float gsM[64];
groupshared float gsB[64];
groupshared float gsS[64];
[numthreads(8,8,1)] void main(uint3 tid : SV_DispatchThreadID, uint gi : SV_GroupIndex) {
 if (mode == 1u) { uint i = tid.y * 8u + tid.x; if (i < 12u) stats[uint2(i, 0)] = 0u; return; }
 float a = 0.0, l = 0.0, bl = 0.0, sh = 0.0;
 if (tid.x < w && tid.y < h) {
  float4 r = edit.Load(int3(tid.xy, 0));
  if (mode == 3u) {
   float2 uv = (float2(tid.xy) + 0.5) / float2(float(w), float(h));
   float4 m = fedTex.SampleLevel(samp, uv, 0);
   a = f0 < 1.5 ? m.x : f0 < 2.5 ? max(m.x, m.y) : max(max(m.x, m.y), m.z);
   a = isfinite(a) ? saturate(a) : 0.0;
  } else if (mode == 4u) {
   float2 mv = r.xy * float2(f0, f1);
   bool bad = !all(isfinite(mv)) || max(abs(mv.x), abs(mv.y)) > float(h) * 0.5;
   a = bad ? 0.0 : min(max(abs(mv.x), abs(mv.y)), 255.0);
   l = bad ? 1.0 : 0.0;
  } else {
  a = min(max(abs(r.x), max(abs(r.y), abs(r.z))), 4.0);
  if (mode == 2u) l = clamp(r.a, 0.0, 1.0);
  else {
   float2 uv = (float2(tid.xy) + 0.5) / float2(float(w), float(h));
   float3 c = fedTex.SampleLevel(samp, uv, 0).rgb;
   l = clamp(dot(c, float3(0.2126, 0.7152, 0.0722)), 0.0, 16.0);
   bl = min(c.r, min(c.g, c.b)) > 1.0 ? 1.0 : 0.0;
   sh = max(c.r, max(c.g, c.b)) > 0.75 ? 1.0 : 0.0;
  }
  }
  if (!isfinite(a)) a = 0.0;
  if (!isfinite(l)) l = 0.0;
 }
 gsA[gi] = a; gsL[gi] = l; gsM[gi] = a; gsB[gi] = bl; gsS[gi] = sh;
 GroupMemoryBarrierWithGroupSync();
 for (uint st = 32u; st > 0u; st >>= 1u) {
  if (gi < st) { gsA[gi] += gsA[gi + st]; gsL[gi] += gsL[gi + st]; gsM[gi] = max(gsM[gi], gsM[gi + st]); gsB[gi] += gsB[gi + st]; gsS[gi] += gsS[gi + st]; }
  GroupMemoryBarrierWithGroupSync();
 }
 if (gi == 0u) {
  uint o;
  if (mode == 3u) {
   InterlockedAdd(stats[uint2(6, 0)], uint(gsA[0] * 256.0 + 0.5), o);
  } else if (mode == 4u) {
   InterlockedAdd(stats[uint2(7, 0)], uint(gsA[0] * 4.0 + 0.5), o);
   InterlockedAdd(stats[uint2(8, 0)], uint(gsL[0] * 256.0 + 0.5), o);
  } else if (mode == 2u) {
   InterlockedAdd(stats[uint2(2, 0)], uint(gsA[0] * 256.0 + 0.5), o);
   InterlockedAdd(stats[uint2(4, 0)], uint(gsL[0] * 256.0 + 0.5), o);
   InterlockedMax(stats[uint2(5, 0)], uint(gsM[0] * 65536.0), o);
  } else {
   InterlockedAdd(stats[uint2(0, 0)], uint(gsA[0] * 256.0 + 0.5), o);
   InterlockedAdd(stats[uint2(1, 0)], uint(gsL[0] * 16.0 + 0.5), o);
   InterlockedMax(stats[uint2(3, 0)], uint(gsM[0] * 65536.0), o);
   InterlockedAdd(stats[uint2(9, 0)], uint(gsB[0] + 0.5), o);
   InterlockedAdd(stats[uint2(10, 0)], uint(gsS[0] + 0.5), o);
  }
 }
}
)";

void Check(HRESULT hr, const char* what)
{
    if (FAILED(hr))
        throw std::runtime_error(std::string("lmxxf backend D3D12 error in ") + what + ": " + std::to_string(static_cast<UINT>(hr)));
}
void Barrier(ID3D12GraphicsCommandList* c, ID3D12Resource* r, D3D12_RESOURCE_STATES a, D3D12_RESOURCE_STATES b)
{
    if (a == b || !r) return;
    D3D12_RESOURCE_BARRIER v {};
    v.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    v.Transition = { r, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, a, b };
    c->ResourceBarrier(1, &v);
}
DXGI_FORMAT ColourViewFormat(DXGI_FORMAT f)
{
    switch (f)
    {
    case DXGI_FORMAT_R16G16B16A16_TYPELESS: return DXGI_FORMAT_R16G16B16A16_FLOAT;
    case DXGI_FORMAT_R8G8B8A8_TYPELESS: return DXGI_FORMAT_R8G8B8A8_UNORM;
    case DXGI_FORMAT_B8G8R8A8_TYPELESS: return DXGI_FORMAT_B8G8R8A8_UNORM;
    case DXGI_FORMAT_R10G10B10A2_TYPELESS: return DXGI_FORMAT_R10G10B10A2_UNORM;
    case DXGI_FORMAT_R32G32B32A32_TYPELESS: return DXGI_FORMAT_R32G32B32A32_FLOAT;
    case DXGI_FORMAT_R32G32B32_TYPELESS: return DXGI_FORMAT_R32G32B32_FLOAT;
    default: return f;
    }
}
DXGI_FORMAT MotionViewFormat(DXGI_FORMAT f)
{
    switch (f)
    {
    case DXGI_FORMAT_R16G16_TYPELESS: return DXGI_FORMAT_R16G16_FLOAT;
    case DXGI_FORMAT_R32G32_TYPELESS: return DXGI_FORMAT_R32G32_FLOAT;
    case DXGI_FORMAT_R16G16B16A16_TYPELESS: return DXGI_FORMAT_R16G16B16A16_FLOAT;
    case DXGI_FORMAT_R32G32B32A32_TYPELESS: return DXGI_FORMAT_R32G32B32A32_FLOAT;
    default: return f;
    }
}
DXGI_FORMAT DepthViewFormat(DXGI_FORMAT f)
{
    switch (f)
    {
    case DXGI_FORMAT_R32_TYPELESS: case DXGI_FORMAT_D32_FLOAT: return DXGI_FORMAT_R32_FLOAT;
    case DXGI_FORMAT_R16_TYPELESS: case DXGI_FORMAT_D16_UNORM: return DXGI_FORMAT_R16_UNORM;
    case DXGI_FORMAT_R24G8_TYPELESS: case DXGI_FORMAT_D24_UNORM_S8_UINT: return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    case DXGI_FORMAT_R32G8X24_TYPELESS: case DXGI_FORMAT_D32_FLOAT_S8X24_UINT: return DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
    default: return f;
    }
}
// The title's reactive / bias mask: a readable view format and the channels it really has.
DXGI_FORMAT ReactiveViewFormat(DXGI_FORMAT f)
{
    switch (f)
    {
    case DXGI_FORMAT_R8_TYPELESS: return DXGI_FORMAT_R8_UNORM;
    case DXGI_FORMAT_R16_TYPELESS: return DXGI_FORMAT_R16_FLOAT;
    case DXGI_FORMAT_R32_TYPELESS: return DXGI_FORMAT_R32_FLOAT;
    case DXGI_FORMAT_R8G8B8A8_TYPELESS: return DXGI_FORMAT_R8G8B8A8_UNORM;
    case DXGI_FORMAT_R16G16B16A16_TYPELESS: return DXGI_FORMAT_R16G16B16A16_FLOAT;
    default: return f;
    }
}
unsigned ReactiveChannels(DXGI_FORMAT f)
{
    switch (f)
    {
    case DXGI_FORMAT_R8_TYPELESS: case DXGI_FORMAT_R8_UNORM: case DXGI_FORMAT_R8_UINT: case DXGI_FORMAT_R8_SNORM: case DXGI_FORMAT_R8_SINT:
    case DXGI_FORMAT_R16_TYPELESS: case DXGI_FORMAT_R16_FLOAT: case DXGI_FORMAT_R16_UNORM: case DXGI_FORMAT_R16_UINT: case DXGI_FORMAT_R16_SNORM: case DXGI_FORMAT_R16_SINT:
    case DXGI_FORMAT_R32_TYPELESS: case DXGI_FORMAT_R32_FLOAT: case DXGI_FORMAT_R32_UINT: case DXGI_FORMAT_R32_SINT:
    case DXGI_FORMAT_A8_UNORM: case DXGI_FORMAT_D16_UNORM: case DXGI_FORMAT_D32_FLOAT:
        return 1;
    case DXGI_FORMAT_R8G8_TYPELESS: case DXGI_FORMAT_R8G8_UNORM: case DXGI_FORMAT_R8G8_UINT: case DXGI_FORMAT_R8G8_SNORM: case DXGI_FORMAT_R8G8_SINT:
    case DXGI_FORMAT_R16G16_TYPELESS: case DXGI_FORMAT_R16G16_FLOAT: case DXGI_FORMAT_R16G16_UNORM: case DXGI_FORMAT_R16G16_UINT: case DXGI_FORMAT_R16G16_SNORM: case DXGI_FORMAT_R16G16_SINT:
    case DXGI_FORMAT_R32G32_TYPELESS: case DXGI_FORMAT_R32G32_FLOAT: case DXGI_FORMAT_R32G32_UINT: case DXGI_FORMAT_R32G32_SINT:
        return 2;
    default: return 3;
    }
}
DXGI_FORMAT ExposureViewFormat(DXGI_FORMAT f)
{
    switch (f)
    {
    case DXGI_FORMAT_R32_TYPELESS: return DXGI_FORMAT_R32_FLOAT;
    case DXGI_FORMAT_R16_TYPELESS: return DXGI_FORMAT_R16_FLOAT;
    case DXGI_FORMAT_R32G32B32A32_TYPELESS: return DXGI_FORMAT_R32G32B32A32_FLOAT;
    case DXGI_FORMAT_R16G16B16A16_TYPELESS: return DXGI_FORMAT_R16G16B16A16_FLOAT;
    default: return f;
    }
}
constexpr D3D12_RESOURCE_STATES kSrv = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
constexpr D3D12_RESOURCE_STATES kUav = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
constexpr DXGI_FORMAT kFp16 = DXGI_FORMAT_R16G16B16A16_FLOAT;

class Pass
{
    static constexpr UINT kRegions = 16; // descriptor regions rotated per dispatch
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12RootSignature> root;
    ComPtr<ID3D12PipelineState> pso;
    ComPtr<ID3D12DescriptorHeap> heap;
    UINT srvCount, perRegion, region = 0, stride = 0, constantCount = 12;

  public:
    Pass(ID3D12Device* d, const char* source, size_t size, const char* name, UINT srvs, const D3D_SHADER_MACRO* defines,
         UINT constants = 12)
        : device(d), srvCount(srvs), perRegion(srvs + 1), constantCount(constants)
    {
        D3D12_DESCRIPTOR_RANGE ranges[2] = { { D3D12_DESCRIPTOR_RANGE_TYPE_SRV, srvs, 0, 0, 0 },
                                             { D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0, srvs } };
        D3D12_ROOT_PARAMETER params[2] {};
        params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[0].DescriptorTable = { 2, ranges };
        params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        params[1].Constants = { 0, 0, constants };
        D3D12_STATIC_SAMPLER_DESC samp {};
        samp.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        samp.AddressU = samp.AddressV = samp.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samp.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        D3D12_ROOT_SIGNATURE_DESC rd {};
        rd.NumParameters = 2; rd.pParameters = params; rd.NumStaticSamplers = 1; rd.pStaticSamplers = &samp;
        ComPtr<ID3DBlob> b, e;
        Check(D3D12SerializeRootSignature(&rd, D3D_ROOT_SIGNATURE_VERSION_1, &b, &e), "root signature");
        Check(d->CreateRootSignature(0, b->GetBufferPointer(), b->GetBufferSize(), IID_PPV_ARGS(&root)), "root signature");
        const HRESULT hr = DlssNr::SysCompiler::Compile(source, size, name, defines, nullptr, "main", "cs_5_0", 0, 0, &b, &e);
        if (FAILED(hr))
            throw std::runtime_error(std::string("lmxxf backend shader '") + name + "' failed to compile: " +
                                     (e ? std::string(static_cast<const char*>(e->GetBufferPointer()), e->GetBufferSize()) : std::string("?")));
        D3D12_COMPUTE_PIPELINE_STATE_DESC pd {};
        pd.pRootSignature = root.Get(); pd.CS = { b->GetBufferPointer(), b->GetBufferSize() };
        Check(d->CreateComputePipelineState(&pd, IID_PPV_ARGS(&pso)), "pipeline");
        D3D12_DESCRIPTOR_HEAP_DESC hd {};
        hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV; hd.NumDescriptors = kRegions * perRegion;
        hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        Check(d->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&heap)), "descriptor heap");
        stride = d->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }
    // The caller owns every barrier: sources in NON_PIXEL_SHADER_RESOURCE, target in UNORDERED_ACCESS.
    // Root constants: w, h, mode, k (a float), then four floats; `groups` false dispatches one
    // group (the texel passes).
    void Run(ID3D12GraphicsCommandList* c, ID3D12Resource* const* srv, const DXGI_FORMAT* srvFmt,
             ID3D12Resource* uav, DXGI_FORMAT uavFmt, UINT w, UINT h, UINT mode,
             float f0 = 0.f, float f1 = 0.f, float f2 = 0.f, float f3 = 0.f, bool groups = true, float k = 0.f,
             float g0 = 0.f, float g1 = 0.f, float g2 = 0.f, float g3 = 0.f)
    {
        const UINT base = region * perRegion;
        region = (region + 1) % kRegions;
        auto cpu = heap->GetCPUDescriptorHandleForHeapStart();
        cpu.ptr += static_cast<SIZE_T>(base) * stride;
        for (UINT i = 0; i < srvCount; ++i)
        {
            D3D12_SHADER_RESOURCE_VIEW_DESC s {};
            s.Format = srvFmt[i]; s.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            s.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; s.Texture2D.MipLevels = 1;
            device->CreateShaderResourceView(srv[i], &s, cpu);
            cpu.ptr += stride;
        }
        D3D12_UNORDERED_ACCESS_VIEW_DESC u {};
        u.Format = uavFmt; u.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        device->CreateUnorderedAccessView(uav, nullptr, &u, cpu);
        auto hh = heap.Get();
        c->SetDescriptorHeaps(1, &hh);
        c->SetComputeRootSignature(root.Get());
        c->SetPipelineState(pso.Get());
        auto gpu = heap->GetGPUDescriptorHandleForHeapStart();
        gpu.ptr += static_cast<UINT64>(base) * stride;
        c->SetComputeRootDescriptorTable(0, gpu);
        UINT cb[12] = { w, h, mode, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u };
        std::memcpy(&cb[3], &k, sizeof k);
        const float fl[8] = { f0, f1, f2, f3, g0, g1, g2, g3 };
        std::memcpy(&cb[4], fl, sizeof fl);
        c->SetComputeRoot32BitConstants(1, (std::min)(12u, constantCount), cb, 0);
        if (groups) c->Dispatch((w + 7) / 8, (h + 7) / 8, 1);
        else c->Dispatch(1, 1, 1);
    }
    // The same with the caller's own constant block (the appearance filter's 24 dwords).
    void RunConstants(ID3D12GraphicsCommandList* c, ID3D12Resource* const* srv, const DXGI_FORMAT* srvFmt,
                      ID3D12Resource* uav, DXGI_FORMAT uavFmt, UINT w, UINT h, const void* constants, UINT count)
    {
        const UINT base = region * perRegion;
        region = (region + 1) % kRegions;
        auto cpu = heap->GetCPUDescriptorHandleForHeapStart();
        cpu.ptr += static_cast<SIZE_T>(base) * stride;
        for (UINT i = 0; i < srvCount; ++i)
        {
            D3D12_SHADER_RESOURCE_VIEW_DESC s {};
            s.Format = srvFmt[i]; s.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            s.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; s.Texture2D.MipLevels = 1;
            device->CreateShaderResourceView(srv[i], &s, cpu);
            cpu.ptr += stride;
        }
        D3D12_UNORDERED_ACCESS_VIEW_DESC u {};
        u.Format = uavFmt; u.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        device->CreateUnorderedAccessView(uav, nullptr, &u, cpu);
        auto hh = heap.Get();
        c->SetDescriptorHeaps(1, &hh);
        c->SetComputeRootSignature(root.Get());
        c->SetPipelineState(pso.Get());
        auto gpu = heap->GetGPUDescriptorHandleForHeapStart();
        gpu.ptr += static_cast<UINT64>(base) * stride;
        c->SetComputeRootDescriptorTable(0, gpu);
        c->SetComputeRoot32BitConstants(1, (std::min)(count, constantCount), constants, 0);
        c->Dispatch((w + 7) / 8, (h + 7) / 8, 1);
    }
};

void UavBarrier(ID3D12GraphicsCommandList* c, ID3D12Resource* r)
{
    D3D12_RESOURCE_BARRIER b {};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    b.UAV.pResource = r;
    c->ResourceBarrier(1, &b);
}
ComPtr<ID3D12Resource> ReadbackBuffer(ID3D12Device* d, UINT64 bytes)
{
    D3D12_HEAP_PROPERTIES hp {};
    hp.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC rd {};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    rd.Width = bytes;
    rd.Height = rd.DepthOrArraySize = rd.MipLevels = 1;
    rd.SampleDesc.Count = 1;
    rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> r;
    d->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&r));
    return r;
}
// One row of a small texture into a readback buffer at `offset` (row pitch 256).
void CopyToReadback(ID3D12GraphicsCommandList* c, ID3D12Resource* tex, DXGI_FORMAT fmt, UINT width, ID3D12Resource* buf, UINT64 offset)
{
    D3D12_TEXTURE_COPY_LOCATION dst {};
    dst.pResource = buf;
    dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst.PlacedFootprint.Offset = offset;
    dst.PlacedFootprint.Footprint = { fmt, width, 1u, 1u, 256u };
    D3D12_TEXTURE_COPY_LOCATION src {};
    src.pResource = tex;
    src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    src.SubresourceIndex = 0;
    c->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
}
ComPtr<ID3D12Resource> Texture(ID3D12Device* d, UINT w, UINT h, DXGI_FORMAT fmt)
{
    D3D12_HEAP_PROPERTIES hp {};
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC rd {};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; rd.Width = w; rd.Height = h;
    rd.DepthOrArraySize = 1; rd.MipLevels = 1; rd.Format = fmt; rd.SampleDesc.Count = 1;
    rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    ComPtr<ID3D12Resource> r;
    Check(d->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, kSrv, nullptr, IID_PPV_ARGS(&r)), "texture");
    return r;
}

// The network's working size: the NR resolution setting applied to the render size, then
// fitted into the runtime's 1920x1080 ceiling with the aspect kept. The exact render size
// whenever it fits: the runtime takes any size (1707x960, 1485x835 and the like are
// verified), and an exact size means the copy it is fed is pixel-for-pixel the frame.
void ModelSize(UINT w, UINT h, float scale, UINT& mw, UINT& mh)
{
    const float s = std::clamp(scale, 0.25f, 2.f);
    float fw = w * s, fh = h * s;
    if (fw > kNetMaxW || fh > kNetMaxH)
    {
        const float k = (std::min)(float(kNetMaxW) / fw, float(kNetMaxH) / fh);
        fw *= k; fh *= k;
    }
    mw = std::clamp(static_cast<UINT>(std::lround(fw)), 64u, kNetMaxW);
    mh = std::clamp(static_cast<UINT>(std::lround(fh)), 64u, kNetMaxH);
}

// (0.3.4, FPS-Q1 / plan D30; [DlssNr] AmdLmxxfTierSnap, ini only: unset = on for gfx11, off elsewhere, as
// AmdBridge::LmxxfTierSnapOn hands it over in Settings::lmxxfTierSnap) The runtime runs its network at
// three fixed tiers and picks the smallest one the fed picture fits in (runtime/native_network_geometry.h ForInput:
// up to 1280x720, up to 1600x900, else 1920x1080), and a tier costs the same whatever part of its box the picture
// fills. With the key on, the size is moved to a tier box, aspect kept:
// - nearer the next smaller tier's box than its own (its fill of its own box, the larger side ratio, below the middle
//   of the two boxes: 0.9167 in the 1080 tier, 0.9 in the 900 tier): lowered to that smaller box, a cheaper tier
//   (1080p render: 85% 1632x918 -> 1600x900, 70% 1344x756 -> 1280x720; a 1707x960 render at 100% -> 1600x900);
// - else, when it fills 90% or more of its own box: grown to fill that box, the same cost with more of the frame's
//   detail (80% 1536x864 -> 1600x900, 95% 1824x1026 -> 1920x1080), never past the frame's own size (an exact copy is
//   not upscaled for it) and never past the 1920x1080 ceiling;
// - otherwise kept (a size below 90% of the 720 box: 58% of 1080p).
// Called after ModelSize, only with the key on; off, the sizing is ModelSize's alone, as in 0.3.3.2. Returns the tier
// the asked size lands in, the one it is fed at, and whether it was grown inside its tier.
struct TierSnap
{
    UINT from = 0, to = 0; // tier heights (720 / 900 / 1080); equal when the size stayed in its tier
    bool grown = false;    // stayed in its tier and was grown toward its box
};
TierSnap SnapToNetworkTier(UINT frameW, UINT frameH, UINT& mw, UINT& mh)
{
    struct Box
    {
        UINT w, h;
    };
    static constexpr Box kTiers[3] = { { 1280u, 720u }, { 1600u, 900u }, { 1920u, 1080u } };
    unsigned t = 0;
    while (t < 2 && !(mw <= kTiers[t].w && mh <= kTiers[t].h))
        ++t;
    TierSnap r { kTiers[t].h, kTiers[t].h };
    const double fill = (std::max)(double(mw) / kTiers[t].w, double(mh) / kTiers[t].h);
    if (t > 0 && fill < 0.5 * (1.0 + double(kTiers[t - 1].h) / kTiers[t].h))
    {
        const Box& to = kTiers[t - 1];
        const double k = (std::min)(double(to.w) / mw, double(to.h) / mh);
        mw = (std::clamp)(static_cast<UINT>(std::lround(mw * k)), 64u, to.w);
        mh = (std::clamp)(static_cast<UINT>(std::lround(mh * k)), 64u, to.h);
        r.to = to.h;
        return r;
    }
    if (fill < 0.9)
        return r;
    const Box& box = kTiers[t];
    const double kBox = (std::min)(double(box.w) / mw, double(box.h) / mh);
    const double kFrame = (std::max)(1.0, (std::min)(double(frameW) / mw, double(frameH) / mh));
    const double k = (std::min)(kBox, kFrame);
    const UINT gw = (std::clamp)(static_cast<UINT>(std::lround(mw * k)), mw, box.w);
    const UINT gh = (std::clamp)(static_cast<UINT>(std::lround(mh * k)), mh, box.h);
    r.grown = gw != mw || gh != mh;
    mw = gw;
    mh = gh;
    return r;
}

bool HasWeights(const std::filesystem::path& dir)
{
    std::error_code ec;
    return std::filesystem::exists(dir / L"block0-ffn.f16", ec) || std::filesystem::exists(dir / L"block0-ffn.f32", ec);
}
unsigned ModuleCount(const std::filesystem::path& dir)
{
    std::error_code ec;
    if (!std::filesystem::exists(dir / L"SHA256SUMS", ec))
        return 0;
    unsigned best = 0;
    // (0.3.4) + the handheld builds gfx1103 / gfx1150 (loose asset folders only; the pak is checked by the runtime).
    std::vector<std::filesystem::path> dirs { dir };
    for (const auto& e : std::filesystem::directory_iterator(dir, ec))
        if (e.is_directory(ec)) dirs.push_back(e.path());
    for (const auto& d : dirs)
    {
        unsigned n = 0;
        for (const auto& e : std::filesystem::directory_iterator(d, ec))
            if (e.path().extension().wstring() == L".hsaco") ++n;
        best = (std::max)(best, n);
    }
    return best;
}
// COM identity, for a queue or command list seen through a wrapper (Streamline's interposer and
// the other proxies UE5 titles load): moved to dlssnr/amd/ComIdentity.h (0.3.3.2), shared with the
// bridge's device gate.
using DlssNr::ComIdentity;
using DlssNr::SameComObject;
// One of the runtime's own switches, tested as the runtime tests it (_wgetenv(name) and a value
// of exactly "1": LmxxfNrRuntime.cpp DLSS5_DEBUG_TINT, native_game_codec.h DLSS5_CODEC_SRGB), on
// the process environment its CRT copies. Read only while RenoDX composition is asked for.
bool EnvIsOne(const wchar_t* name)
{
    wchar_t v[4] {};
    return GetEnvironmentVariableW(name, v, 4) == 1 && v[0] == L'1';
}
// The 0.3.3 colour fix's two switches, lmxxf only, both off by default in 0.3.3: [DlssNr]
// AmdLmxxfHighlightChromaGuard (the residual's highlight chroma guard, ResidualShader) and
// AmdLmxxfAutoExposureHighlightCap (the auto-exposure loop's highlight cap, AutoExposureHlsl.h).
// Read straight from the config on the recording thread, as AmdPreSr.cpp reads its own keys, so
// AmdPreSr::Settings and the bridge stay as they are; a menu toggle acts on the next answer / feed.
bool HighlightChromaGuardOn() { return ::Config::Instance()->AmdLmxxfHighlightChromaGuard.value_or_default(); }
bool AutoExposureHighlightCapOn() { return ::Config::Instance()->AmdLmxxfAutoExposureHighlightCap.value_or_default(); }
// (0.3.4, lmxxf Fast mode) [DlssNr] AmdLmxxfFastMode, default off (LmxxfTierPolicy.h PlanLmxxfFastSize): read the same
// way, once per Record; a menu toggle acts on the next Record.
// ILION dev.55: Adaptive NR's Smooth profile asks for it while it has lowered the NR resolution (Lmxxf::SetAdaptiveFast)
std::atomic<bool> adaptiveFast { false };
bool FastModeOn() { return ::Config::Instance()->AmdLmxxfFastMode.value_or_default() || adaptiveFast.load(); }
} // namespace

void SetAdaptiveFast(bool on) { adaptiveFast.store(on); }

// Two layouts are accepted, both next to the game exe:
//   lmxxf's own package:   DLSS5-AMD\native-game-tiled-assets  (weights + HLSL at the root, HIP\gfx1201\*.hsaco)
//   dlss-5-amd-project 1.9.0 (TheAutomatic):  native-game-tiled-assets (weights) + lmxxf-modules + shaders folders
std::filesystem::path AssetsPath(const std::filesystem::path& directory)
{
    std::error_code ec;
    const auto packaged = directory / L"DLSS5-AMD" / L"native-game-tiled-assets";
    if (std::filesystem::is_directory(packaged, ec))
        return packaged;
    const auto beside = directory / L"native-game-tiled-assets";
    if (std::filesystem::is_directory(beside, ec))
        return beside;
    return packaged;
}
std::filesystem::path RuntimePath(const std::filesystem::path& directory)
{
    std::error_code ec;
    const auto beside = directory / kRuntimeDll;
    if (std::filesystem::exists(beside, ec))
        return beside;
    return directory / L"DLSS5-AMD" / kRuntimeDll;
}
// LmxxfNrRuntime.pak: the assets in one encrypted file beside the runtime DLL (the runtime
// mounts it itself; the host only needs to know it is there).
std::filesystem::path PakPath(const std::filesystem::path& directory)
{
    return RuntimePath(directory).parent_path() / L"LmxxfNrRuntime.pak";
}
bool PakPresent(const std::filesystem::path& directory)
{
    std::error_code ec;
    const auto pak = PakPath(directory);
    return std::filesystem::is_regular_file(pak, ec) && std::filesystem::file_size(pak, ec) > (1u << 20);
}
// (AMDNR 0.3.3.2) The assets the runtime is given: LmxxfNrRuntime.pak whenever it is there. A loose
// folder (AssetsPath) is used only when there is no pak, or when the developer switch
// AMDNR_LOOSE_ASSETS=1 asks for it. Before, a DLSS5-AMD\native-game-tiled-assets folder left over from
// an older install won over the pak and silently ran the old kernels (Forza, 0.3.3.2 test:
// c32w=off:nofile). `ignored` gets the loose folder passed over for the pak, else stays empty.
std::filesystem::path UsedAssetsPath(const std::filesystem::path& directory, std::filesystem::path* ignored = nullptr)
{
    std::error_code ec;
    const auto loose = AssetsPath(directory);
    const bool looseThere = std::filesystem::is_directory(loose, ec);
    if (!PakPresent(directory) || (looseThere && EnvIsOne(L"AMDNR_LOOSE_ASSETS")))
        return loose;
    if (looseThere && ignored)
        *ignored = loose;
    return PakPath(directory);
}
bool RuntimePresent(const std::filesystem::path& directory)
{
    std::error_code ec;
    return std::filesystem::exists(RuntimePath(directory), ec);
}
bool AssetsPresent(const std::filesystem::path& directory)
{
    // Cached once found: a directory walk per frame is not free.
    static std::atomic<bool> found { false };
    if (found.load())
        return true;
    if (PakPresent(directory))
    {
        found.store(true);
        return true;
    }
    std::error_code ec;
    const auto assets = AssetsPath(directory);
    if (!std::filesystem::is_directory(assets, ec) || !HasWeights(assets))
        return false;
    const bool hlsl = std::filesystem::exists(assets / L"native_codec_encode.hlsl", ec) ||
                      std::filesystem::exists(assets / L"shaders" / L"native_codec_encode.hlsl", ec) ||
                      std::filesystem::exists(directory / L"shaders" / L"native_codec_encode.hlsl", ec);
    const unsigned modules = (std::max)(ModuleCount(assets / L"HIP"), ModuleCount(directory / L"lmxxf-modules"));
    const bool ok = hlsl && modules >= 24;
    if (ok) found.store(true);
    return ok;
}
bool FullNetworkRefused() { return fullNetworkRefused.load(); }
int ImportPoolState() { return importPool.load(); }
bool ControlsRefused() { return controlsRefused.load(); }
bool SmallTierPolicy() { return smallTierPolicy.load(); }
int TierCapInUse() { return tierCapInUse.load(); }
int FastModeFromTier() { return fastModeFromTier.load(); }
float LateSubmitNrShare() { return lateSubmitNrShare.load(); }

struct Backend::Impl
{
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> queue; // the queue the runtime is bound to (its fence signals go there)
    std::filesystem::path directory;
    mutable std::mutex lock;
    std::string status = "lmxxf: waiting for the first frame";
    std::string fatalError; // (0.3.3.2) the error that poisoned the runtime session, once (NoteFatal)
    HMODULE dll = nullptr;
    LmxxfNrApi api {};
    int32_t (*poolStats)(LmxxfNrImportPoolStats*) = nullptr; // R3: the runtime's optional export, null on an older runtime
    void* ctx = nullptr;
    bool failed = false, shutdown = false, loggedPost = false, loggedFirst = false, loggedUnsubmitted = false, loggedExposure = false;
    bool loggedStoppedSkip = false; // (0.3.4, P24) Record's once-only line for a poisoned session
    bool loggedCpuWait = false, answerLost = false; // Vulkan bridge: CPU-side wait for the answer (see Record)
    bool vkStageLog = false;   // Vulkan bridge: log the first jobs' stages (Record sets it from cfg)
    unsigned vkJobsLogged = 0; // ... for this many jobs
    // Vulkan bridge (cfg.lmxxfCpuWait): the backend ran over it (sticky; Shutdown skips the drain),
    // launches timed so far, the session/network size whose warm-up line was last checked, the
    // launch marker's state (live: the file is there; first answer: never written again; writes so
    // far), whether the runtime skipped its HIP warm-up (no weights: they upload with the first
    // job), and the once-only notes.
    bool vkBridge = false, vkMarkerLive = false, vkFirstAnswer = false, loggedVkDrainSkip = false;
    bool vkWarmSkipped = false, loggedVkMarkerDone = false;
    unsigned vkMarkerWrites = 0;
    ULONGLONG vkMarkerLastWrite = 0;
    unsigned vkLaunches = 0;
    // UE5 titles: the neural list or its queue seen through a wrapper (same COM identity), noted once.
    bool loggedWrappedList = false, loggedWrappedQueue = false;
    // Queue migration (Submitted): the session of the queue the neural list left, destroyed in a
    // later Record once both host-owned fences show that every list that can reference it is done -
    // the old queue's (earlier frames, and the runtime's own Signal/Wait pairs, which go to the queue
    // it was bound to) and the new queue's (the migration frame's list, which already recorded the
    // old session's consumer and producer when the move was seen). Never waited for on the CPU.
    struct RetiredSession
    {
        void* ctx = nullptr;
        ComPtr<ID3D12CommandQueue> oldQueue, newQueue;
        ComPtr<ID3D12Fence> oldFence, newFence;
        UINT64 value = 0;
    };
    std::vector<RetiredSession> retired;
    void* vkWarmCtx = nullptr;
    UINT vkWarmW = 0, vkWarmH = 0;
    double cpuWaitMax = 0.0;
    std::unique_ptr<Pass> exposurePass, blitColour, blitMotion, motionChain, residual, lookPass, blitDepth;
    std::unique_ptr<Pass> autoExposure; // the loop above, when the title publishes no exposure
    // No-motion mode (final image): the edit in the space of the frame it belongs to (ping-pong),
    // the edit brought onto this frame, the composed output, the raw frame the job in flight
    // was fed and the raw frame the current edit belongs to; quarter-resolution luma of this
    // frame / the fed frame / the edit's frame, and the two flows the matcher produces.
    std::unique_ptr<Pass> lumaDown, blockMatch, editWarp;
    ComPtr<ID3D12Resource> carryHist, carryNext, editFrame, carryOut, baseInFlight, baseOfEdit;
    ComPtr<ID3D12Resource> lumCur, lumFed, lumEdit, flowCB, flowBB;
    UINT qw = 0, qh = 0;
    bool carryZeroed = false;
    double recMaxMs = 0.0; // the longest Record() of the current stats window (CPU)
    // (P6, 0.3.3.2) The network's GPU time over the stats window: the runtime's hip_ms= word (the bridge's
    // LastSpanMs, the last finished job) read once per model frame; -1 (no job finished yet) is not counted.
    double hipMsSum = 0.0, hipMsMax = 0.0;
    unsigned hipMsSamples = 0;
    // (0.3.4, P7.10) The last closed stats window's hip_ms mean, for Stats::nrGpuMs (the menu's NR cost readout);
    // -1 before the first window closes and after a window in which no job reported a time. Readout only.
    float lastWindowHipMs = -1.f;
    void NoteHipMs(const std::string& st)
    {
        const size_t at = st.find("hip_ms=");
        if (at == std::string::npos)
            return;
        const double v = std::strtod(st.c_str() + at + 7, nullptr);
        if (!std::isfinite(v) || v < 0.0)
            return;
        hipMsSum += v;
        hipMsMax = (std::max)(hipMsMax, v);
        ++hipMsSamples;
    }
    std::string HipMsNote() const
    {
        if (!hipMsSamples)
            return "hip_ms n/a (no network time reported)";
        char buf[96];
        std::snprintf(buf, sizeof buf, "hip_ms mean %.2f max %.2f (%u samples)", hipMsSum / hipMsSamples, hipMsMax, hipMsSamples);
        return buf;
    }
    // (0.3.4, FB-L5 / D9 "readout always") The interleave pacer's measurement, on the stats line of a window run
    // with Model interleave on, so lmxxf's model / fill split and whether it paced can be read from the log (the
    // menu's readout is drawn in danielblnc's interleave section). Same numbers as that readout (Pacing::Read).
    static std::string PacingNote(float pacingKey)
    {
        const auto pace = DlssNr::Pacing::Read();
        char buf[200];
        if (pace.modelMs > 0.0 && pace.fillMs > 0.0 && pace.active)
            std::snprintf(buf, sizeof buf, ", pacing: model %.1f ms, fill %.1f ms, paced to %.1f ms, %.0f%% even (AmdInterleavePacing %.2f)",
                          pace.modelMs, pace.fillMs, pace.targetMs, pace.evenness * 100.0, static_cast<double>(pacingKey));
        else if (pace.modelMs > 0.0 && pace.fillMs > 0.0)
            std::snprintf(buf, sizeof buf, ", pacing: model %.1f ms, fill %.1f ms, not pacing (%s)", pace.modelMs, pace.fillMs,
                          !(pacingKey > 0.f) ? "AmdInterleavePacing is not above 0; auto (-1) is off on lmxxf"
                                             : "split under 15%, the model frame costs about what a fill frame does");
        else
            std::snprintf(buf, sizeof buf, ", pacing: measuring");
        return buf;
    }
    ComPtr<ID3D12Fence> drainFence; // resolution change: wait for the queue before textures are freed
    UINT64 drainValue = 0;
    HANDLE drainEvent = nullptr;
    bool loggedDrain = false, loggedDrainTimeout = false;
    // #5 still-surface steadiness: the value and state last logged (-1 = never), so the line follows a change.
    float loggedStaticRelax = -1.f;
    int loggedStaticRelaxState = -1;
    // (0.3.4, P7.8) The residual edge fade as last logged: 0 off (the key at 0; never logged unless it was on),
    // 1 acting (the edit is lifted), 2 set but not used (pixel-exact feed), 3 set but not used (Network output);
    // and the value.
    int loggedFadeState = 0;
    float loggedFadeValue = 0.f;
    static constexpr unsigned kFadeValueLines = 12;
    unsigned fadeValueLines = 0;
    // (0.3.4, P7.6) Network output as last logged (false: nothing is logged while it stays off).
    bool loggedNetworkOutput = false;
    // (0.3.4, P7.5) Encoding ([DlssNr] AmdEncoding 2 sRGB / 3 Gamma 2.2) on lmxxf: the game's colour decoded to linear
    // before the copy everything here works on, the result encoded back at the end - danielblnc's transfer shader
    // (ColorEncoding.h's EncodingShader), run through this file's Pass so its descriptors rotate like every other
    // pass here. Built on first use, so Auto / Linear never create any of it; a pass or texture that cannot be made
    // refuses the conversion for the session (the colour is used as is, as before). The mode as last logged: 0 none
    // (never logged unless it was something else), 2 / 3 converting, 4 set in final image mode (not applied; that mode
    // sets sRGB itself on an 8-bit swapchain, so this line can appear at default settings),
    // 5 refused. encodingFed: the conversion the last frame ran (0 none), so a change restarts the carry.
    std::unique_ptr<Pass> encodingPass;
    ComPtr<ID3D12Resource> decodedColour, encodedOut;
    bool encodingBroken = false;
    unsigned loggedEncodingState = 0, encodingFed = 0;
    // the danielblnc host runs (shaders from the game folder's experimental_lighting\), on the carried frame before
    // the Image look. RtgiNative writes its two dispatches' descriptors at the start of its heap on every Record,
    // which is safe only at a quiescent frame boundary (its header); this host never waits for the GPU between
    // frames, so kRtgiRing instances are used in turn - one Record per frame each, like the rotating regions of
    // this file's Pass - and an instance's descriptors are rewritten only after the lists of kRtgiRing - 1 later
    // frames were recorded. Each holds two render-size FP16 textures (16 bytes a pixel), made on first use; none
    // exist while RTGI stays off. A failure (the assets are missing, an unreadable depth) stops RTGI for the
    // session with one line, never the network. The ring is retired (RetireRtgi: moved to rtgiRetired, freed eight
    // frames later like the graveyard, as recorded lists may still use it) when RTGI stops being applied, when it
    // fails, and before its first Record at a new frame size (an instance is never resized under a list in flight),
    // so its memory is held only while RTGI runs, at the current size.
    static constexpr unsigned kRtgiRing = 4;
    unsigned rtgiNext = 0;
    UINT rtgiW = 0, rtgiH = 0; // the frame size the ring's instances were recorded at (0: none)
    bool rtgiFailed = false, rtgiActive = false;
    std::string rtgiError;
    ComPtr<ID3D12Resource> rtgiDepth; // render-size R32F copy of a depth format RtgiNative does not read (D24S8, D32, ...)
    ComPtr<ID3D12Resource> depthScratch; // after RR: the render-grid depth on the display grid
    // Self-healing, decided from the stats window (see step 9): a title whose vectors are
    // mostly rejected is carried without reprojection; one whose depth test alone kills the
    // carry loses the depth test; one whose exposure texture blacks out or blows out the feed
    // is fed by auto-exposure (exposure 1 with AmdLmxxfAutoExposure off). Each decision is for
    // the session and is logged once.
    bool motionMuted = false, depthMuted = false, exposureMuted = false;
    // 0.3.3 colour fix. The exposure self-heal's blown rule fires on two blown windows in a row:
    // the run so far. The exposure mode of the last feed (0 the title's texture, 1 exposure 1,
    // 2 auto-exposure; ~0u before the first) and the mode behind each stats slot's expoJob copy,
    // so a readback knows what its e / raw are. The once-per-session exposure-source line, and
    // the two colour-fix switches as last logged (-1: not yet).
    unsigned blownWindows = 0;
    unsigned expoModeFed = ~0u, statsExpoMode[2] = { ~0u, ~0u };
    bool loggedExposureSource = false;
    // (0.3.4, EXPO-PARITY) [DlssNr] AmdUseGameExposure = 0 at the last feed while the title published an exposure
    // texture (it was ignored), the same per stats slot. And the key alone (= 0 at the last feed, whether or not a
    // texture was there), per stats slot and as the exposure-source line last said it: that line is logged again
    // only when the key itself moves to or from 0, never when a texture comes and goes (a menu or loading screen
    // without one); with the key at its default it never is.
    bool expoTitleIgnored = false, statsTitleIgnored[2] = { false, false };
    bool expoKeyOff = false, statsKeyOff[2] = { false, false }, loggedKeyOff = false;
    int loggedChromaGuard = -1, loggedHighlightCap = -1;
    // RenoDX composition values as last logged (AMDNR 0.3.3): NoteComposition logs only a change of
    // mode, so a slider move was never visible in a tester's log. Logged on every change of detail,
    // colour, highlight guard, skin edit or the highlight chroma guard while RenoDX composes, at most
    // kComposeValueLines lines per session; the stats line carries the same values in every window.
    static constexpr unsigned kComposeValueLines = 24;
    float loggedComposeDetail = -1.f, loggedComposeColour = -1.f, loggedComposeGuard = -1.f;
    int loggedComposeSkin = -1, loggedComposeChroma = -1;
    unsigned composeValueLines = 0;
    ComPtr<ID3D12Resource> depthFlat; // w x h R32F, 0.5 everywhere, when depthMuted
    // Interleave fill as last logged (-1 not yet, 0 classic carry = preset 9, 1 Edit accumulation =
    // preset 11), and what the menu's Live line reads: whether the fill is Edit accumulation now,
    // and the cadence (the danielblnc host reports both through its own Stats).
    int loggedFillMode = -1;
    bool statAccumulating = false;
    UINT statCadence = 1;
    bool loggedWriteBackRefusal = false;
    ComPtr<ID3D12Resource> lookOut; // the appearance filter's output, render size
    std::string lastHistState; // the runtime's hist= word last logged
    // Full network ([DlssNr] LmxxfFullNetwork -> LMXXF_NR_FRAME_FLAG_FULL_NETWORK): the value the
    // runtime session `fullNetCtx` last prepared its network with (a change rebuilds it there), and
    // the latch for a runtime older than the flag, which refuses it (the default network runs then;
    // mirrored in the process-wide fullNetworkRefused, which the menu reads for its note).
    bool fullNetSent = false, fullNetRefused = false;
    void* fullNetCtx = nullptr;
    // (0.3.4, AUTOMASK host step) lmxxf's native character mask ([DlssNr] AutoMask, Structure intensity, Character
    // structure -> AmdPreSr::ResolveNrControls, dlssnr/amd/NrControls.h). The built-in values (the defaults) go as the
    // 0.3.3.2 frame: 128 bytes, no flag. Any other value goes as the 144-byte frame with LMXXF_NR_FRAME_FLAG_CONTROLS;
    // the runtime folds the values into the network's first layer and rebuilds it on a change, like Full network. As
    // there: `ctlSent` is what the session `ctlCtx` last prepared its network with, and `ctlRefused` the latch for a
    // runtime that refuses the frame (then the built-in values, 128 bytes, from the next frame: NR never stops for it).
    AmdPreSr::NrControls ctlSent {};
    void* ctlCtx = nullptr;
    bool ctlRefused = false;
    // (0.3.4, runtime work B9) The network size tier policy (LmxxfTierPolicy.h), decided once from [DlssNr]
    // AmdLmxxfTierCap and the NR adapter (the handheld APUs), at the first Record: `tierWish.smallTiers` = ask the runtime for
    // the small tiers 360 / 576; `tierSmall` = the runtime has them (the wish until Load asks: so the first frames are
    // already sized for them; false after a runtime without LmxxfNrSetTierPolicy or a refusal). Nothing wished and no
    // cap = 0.3.3.2's sizing (ModelSize, plus SnapToNetworkTier with [DlssNr] AmdLmxxfTierSnap).
    // `tierSmall` is atomic (r1 review 9): Load() writes it from the submit hook, Record reads it on the game's thread.
    bool tierDecided = false;
    Lmxxf::TierPolicy tierWish {};
    std::atomic<bool> tierSmall { false };
    std::string tierWhy; // why the small tiers are wished: for the log
    // (0.3.4, lmxxf Fast mode) The Fast mode state last logged (NoteFastMode: 0 off, -1 on with no tier below, else the
    // default's tier), so its line comes once per change, never per frame.
    int fastLogged = 0;
    UINT64 allocFrame = 0;     // frames at the last (re)allocation: the motion history needs two runs since
    std::unique_ptr<AmdPreSr::TemporalStability> temporal;
    ComPtr<ID3D12Resource> full;      // this frame's colour, FP16, render size, game units: the temporal pass's current frame
    ComPtr<ID3D12Resource> fed;       // what the network is fed: the colour times the exposure, at the model's size
    ComPtr<ID3D12Resource> freshEdit; // the model's edit of the frame it was last fed, render size, game units
    ComPtr<ID3D12Resource> expoJob;   // 1x1: the exposure the job in flight was fed with
    ComPtr<ID3D12Resource> autoExpo;  // 4x1: the auto-exposure loop's state (see AutoExposureShader)
    ComPtr<ID3D12Resource> motionScratch;
    ComPtr<ID3D12Resource> motionFed; // RG16F at the model's size: the vectors the runtime's history path reads
    UINT64 lastFedFrame = ~0ull;      // the frame index the last job was fed on (history needs the one before)
    std::vector<std::pair<UINT64, ComPtr<ID3D12Resource>>> graveyard; // replaced textures, held 8 frames
    // RenoDX colour composition ([DlssNr] AmdComposition 1): a job fed in that mode is sent runtime
    // strengths 1/1 and its answer is composed on the host when it is consumed (ComposeMode; step 1).
    // Built on first use, so Classic never creates any of it: the compose pass (NrCompose.h,
    // NRCOMPOSE_DELTA), the residual's AMDNR_DELTA variant, and the delta, FP16 at the model's size
    // (buried with `fed`).
    std::unique_ptr<Pass> composePass, residualDelta;
    ComPtr<ID3D12Resource> composeDelta;
    bool composeBroken = false; // a compose pass did not build: the mode is refused for the session
    bool jobCompose = false;    // the job in flight was fed in RenoDX mode (latched where RecordInputs succeeds)
    int composeState = 0;       // last noted: 0 Classic, 1 RenoDX, 2.. refused (which reason)
    unsigned composeChanges = 0, statComposed = 0;
    UINT whiteSource = AmdPreSr::kCompositionWhiteNone; // Stats::compositionWhiteSource
    UINT w = 0, h = 0, mw = 0, mh = 0;
    LmxxfNrJob job {};
    ID3D12Resource* netOut = nullptr;
    bool jobRecorded = false;               // RecordInputs is on a list that has not been submitted yet
    bool inFlight = false;                  // the network was launched; its answer is consumed next frame
    // (0.3.4, P3, [DlssNr] AmdLateSubmitGrace; LateSubmitGrace.h) The pending job had its frame of grace: its list was
    // still unsubmitted at the next Record, which fed nothing. Latched until its answer is consumed (one Record later
    // than usual) or the job goes. lateBase: the raw frame that job was fed (a copy of `full` taken on the grace frame,
    // before step 2 refreshes it); lateEdit: its edit before the late warp; both render size, made on the first grace
    // (never in a game that submits on time) and buried with `full` on a size change.
    bool jobGraced = false;
    ComPtr<ID3D12Resource> lateBase, lateEdit;
    std::unique_ptr<Pass> lateWarp;
    bool lateBroken = false;              // the late warp pass or its textures could not be made: no grace this session
    float lateJitX = 0.f, lateJitY = 0.f; // the grace frame's jitter delta (step 4), for the late warp
    // Late submission diagnostics (stats line 'late +N, graced +G'; the menu's LateSubmitNrShare): the list of a job
    // dropped as never submitted with no reset to explain it, and the frame it was fed on; its submission within
    // kLateWindow frames counts as late. Written under `lock`; Submitted's prefilter reads the pointer without it.
    std::atomic<ID3D12CommandList*> droppedList { nullptr };
    UINT64 droppedFedAt = 0;
    unsigned statLate = 0, statGraced = 0, statLateMax = 0;
    bool loggedGrace = false, loggedLateArrival = false;
    // Written under `lock`; atomic (0.3.3.2) because ListReset compares against it from any thread.
    std::atomic<ID3D12CommandList*> pendingList { nullptr };
    // The pending list's COM identity, captured by Record while the list is known to be alive and
    // held by reference: Submitted compares the lists being submitted against these values and never
    // dereferences pendingList, which the game may already have released. The identity path is only
    // armed after two drops no reset explained (a raw pointer that never matched: a wrapper), so the
    // steady-state D3D12 path stays a pointer compare.
    ComPtr<IUnknown> pendingSelfRef, pendingKeyRef;
    std::atomic<IUnknown*> pendingSelf { nullptr }, pendingKey { nullptr };
    std::atomic<bool> identityMatch { false };
    // (0.3.3.2) Bootstrap re-arm (ArmBootstrap): the frame the watched list was armed on while the
    // runtime does not exist, the re-arms in a row, and the once-only notes.
    static constexpr UINT64 kBootRearm = 8;         // frames a watched list may wait for its submission
    static constexpr unsigned kBootStuckNote = 38;  // about 300 frames: say that lmxxf is not coming up
    UINT64 bootArmedAt = 0;
    unsigned bootRearms = 0;
    bool loggedBootRearm = false, loggedBootIdentity = false, loggedBootStuck = false;
    // (0.3.3.2, discard evidence: [DlssNr] AmdNeuralListRecovery) The pending list as ListReset reported
    // it: the game reset it (the bridge's Reset hook) or OptiScaler's own bridge dropped it (`resetCertain`),
    // so what RecordInputs put on it is gone. Set from any thread without the lock; cleared when a new list
    // is armed (SetPending) and consumed by the drop in Record. A reset explains a drop only once a neural
    // list reached ExecuteCommandLists under its own pointer (`rawSubmissions`): behind a wrapper the list
    // ran under another pointer and was reset afterwards, and that drop must still count toward the
    // identity path. Drops nothing explained; the once-only notes.
    std::atomic<ID3D12CommandList*> resetSeen { nullptr };
    std::atomic<bool> resetCertain { false };
    UINT64 rawSubmissions = 0;
    unsigned unexplainedDrops = 0;
    bool loggedResetDrop = false, loggedResetSubmit = false, loggedForeignQueue = false;
    // (0.3.3.2, exit) The thread inside Record or Submitted with `lock` held (0 = none), so Shutdown on that
    // same thread - a crash handler calling ExitProcess from inside our recording - returns at once instead
    // of locking a mutex it already owns; and when the last Record returned, which C1-B's marker rule reads.
    std::atomic<DWORD> lockOwner { 0 };
    ULONGLONG lastRecordReturn = 0;
    void SetPending(ID3D12CommandList* l)
    {
        if (l)
        {
            // a new list generation: an earlier reset is not about it
            resetSeen.store(nullptr);
            resetCertain.store(false);
        }
        pendingList = l;
        pendingSelf.store(nullptr);
        pendingKey.store(nullptr);
        pendingSelfRef.Reset();
        pendingKeyRef.Reset();
        if (l && identityMatch.load())
        {
            const ComIdentity id(l);
            pendingSelfRef = id.Self();
            pendingKeyRef = id.Key();
            pendingSelf.store(pendingSelfRef.Get());
            pendingKey.store(pendingKeyRef.Get());
        }
    }
    // Before the runtime exists it is created at the submission of a list handed to Record (Submitted, on the queue that
    // submits it). The first list can be one the game never submits (a discarded frame) or submits under another pointer
    // (a wrapper), and that pinned the bootstrap for the session: nothing else cleared pendingList while ctx was null, and
    // nothing was logged. So the watched list is replaced by the current one after kBootRearm frames with no submission
    // seen - not every frame: a title whose Record(N+1) comes before ExecuteCommandLists(N) would then never match - and
    // from the second replacement on, submitted lists are also matched by COM identity (captured here while cmd is alive).
    void ArmBootstrap(ID3D12GraphicsCommandList* cmd)
    {
        if (!pendingList)
        {
            SetPending(cmd);
            bootArmedAt = frames;
            return;
        }
        if (frames - bootArmedAt < kBootRearm)
            return;
        ++bootRearms; // also when cmd == pendingList: a reused list behind a wrapper needs the identity path
        if (bootRearms >= 2 && !identityMatch.exchange(true) && !loggedBootIdentity)
        {
            loggedBootIdentity = true;
            Log("lmxxf: no list handed to the neural path was seen at submission for " +
                std::to_string(bootRearms * kBootRearm) +
                " frames before the runtime came up - matching submitted lists by COM identity from now on");
        }
        SetPending(cmd);
        bootArmedAt = frames;
        if (!loggedBootRearm)
        {
            loggedBootRearm = true;
            Log("lmxxf: the list of the first neural frame was not submitted within 8 frames (a discarded frame, or a "
                "wrapper that submits it under another pointer); watching the current frame's list instead (noted once)");
        }
        if (bootRearms == kBootStuckNote && !loggedBootStuck)
        {
            loggedBootStuck = true;
            Log("lmxxf: runtime not loaded after about 300 frames: no list handed to the neural path reached "
                "ExecuteCommandLists, by pointer or by COM identity. lmxxf stays off until one does; send "
                "lmxxf_backend.log and OptiScaler.log, or switch Neural runtime to danielblnc");
        }
    }
    UINT64 frames = 0, modelFrames = 0, skips = 0;
    std::atomic<UINT64> completedForActivity { 0 };
    unsigned framesSinceFresh = 0, modelCounter = 0, prepareFailures = 0;
    std::atomic<bool> resetRequested { false };
    bool primedOnce = false, haveJitter = false;
    // The chain between two model feeds: an answer arrived since the last feed (under Model
    // interleave it arrives on the frame between two feeds, so "fresh this frame" was never
    // true on a feed frame and the history never engaged), and no reset happened since.
    bool answerSinceFeed = false, resetSinceFeed = false;
    unsigned gameResetRun = 0; bool loggedStuckReset = false; // frames in a row the game asserted Reset
    // Stats for the log (every kStatsEvery frames): GPU sums + counters since the last line.
    static constexpr UINT64 kStatsEvery = 600;
    std::unique_ptr<Pass> statsPass;
    ComPtr<ID3D12Resource> statsTex, statsRead[2];
    bool statsWritten[2] = { false, false };
    UINT64 statModelAt = 0, statSkipsAt = 0;
    unsigned statFresh = 0, statResets = 0, statGameResets = 0, statHistOn = 0, statHistReset = 0;
    float lastJitterX = 0.f, lastJitterY = 0.f;

    // (0.3.3.2, [DlssNr] AmdDeviceGate) Whether a queue the neural list was submitted on belongs to the
    // device this backend (and the runtime session) was made on. The runtime's queue contract refuses
    // another device's queue by throwing, which poisons the session; binding to it is never right. Asked
    // only where the runtime would be bound (first sight, migration), never per submission. A queue
    // whose device cannot be read passes, as before.
    bool QueueOnOurDevice(ID3D12CommandQueue* q)
    {
        if (!::Config::Instance()->AmdDeviceGate.value_or_default())
            return true;
        ComPtr<ID3D12Device> qd;
        if (!q || FAILED(q->GetDevice(IID_PPV_ARGS(&qd))) || DlssNr::SameD3D12Device(qd.Get(), device.Get()))
            return true;
        if (!loggedForeignQueue)
        {
            loggedForeignQueue = true;
            Log("lmxxf: the neural list was submitted on a queue of another D3D12 device; the runtime is not bound to "
                "it and the job is dropped (noted once)");
        }
        return false;
    }

    // A resolution change reallocates the temporal pass's textures on the spot (TemporalStability
    // frees the old ones as it makes the new ones) while the previous frames' lists may still be
    // reading them on the GPU - a use-after-free the device answers with a removal, i.e. a crash
    // when the DLSS quality setting changes (GTA V Enhanced / RDR1 reports). Our own textures go
    // to the graveyard for eight frames; the pass's do not, so the queue is drained first, once
    // per change. atExit (C1-A, 0.3.3.2 rebuild): the same 2 s bound replaces the runtime's 30 s drain at process exit,
    // with its own log text.
    void DrainQueue(bool atExit = false)
    {
        if (!queue)
            return;
        // A removed device completes nothing: no Signal, no wait (C2-F, 0.3.3.2 rebuild).
        if (FAILED(device->GetDeviceRemovedReason()))
        {
            if (atExit)
                Log("lmxxf: exit: the D3D12 device was removed - no queue drain");
            return;
        }
        if (!drainFence)
        {
            if (FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&drainFence))))
                return;
            drainEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        }
        const UINT64 v = ++drainValue;
        if (FAILED(queue->Signal(drainFence.Get(), v)))
            return;
        if (drainFence->GetCompletedValue() < v && drainEvent)
        {
            drainFence->SetEventOnCompletion(v, drainEvent);
            if (WaitForSingleObject(drainEvent, 2000) != WAIT_OBJECT_0)
            {
                // Fence hardening: the registration above is still armed and the auto-reset event
                // would be set late, satisfying the NEXT drain's wait before its own value arrives.
                // The old event is left to the fence (leaked on purpose, never closed: a closed
                // handle value can be reused by something else) and a fresh one is used from now on.
                drainEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
                if (atExit)
                    Log("lmxxf: exit: the queue drain timed out after 2 s - the process exits with the runtime's work in "
                        "flight");
                else if (!loggedDrainTimeout)
                {
                    loggedDrainTimeout = true;
                    Log("lmxxf: resolution change: the queue drain timed out after 2 s; its event is left to the fence "
                        "and a new one is used, so a late signal cannot end the next drain early (noted once)");
                }
            }
        }
        if (!loggedDrain && !atExit)
        {
            loggedDrain = true;
            Log("lmxxf: resolution change: the queue was drained before the temporal pass reallocated");
        }
    }
    void Log(const std::string& s)
    {
        status = s;
        std::ofstream out(directory / L"lmxxf_backend.log", std::ios::app);
        out << GetTickCount64() << " " << s << '\n';
    }
    // Memory telemetry (leak audit, 0.3.3), on the stats line and the resize line: this process's
    // use of the adapter's local video memory and its budget, as DXGI reports them
    // (QueryVideoMemoryInfo, LOCAL segment group, node 0, on the adapter our device was made on -
    // found by its LUID), and the process's private bytes (GetProcessMemoryInfo, PrivateUsage).
    // DXGI and not the per-process GPU counter: that counter, like Task Manager's "Dedicated GPU
    // memory" column, counted a HIP-imported buffer about twice in the probe. The adapter is looked
    // up once; a value that cannot be read says n/a. Nothing here touches the GPU or the picture.
    ComPtr<IDXGIAdapter3> memAdapter;
    bool memAdapterTried = false;
    std::string MemoryNote()
    {
        if (!memAdapterTried)
        {
            memAdapterTried = true;
            ComPtr<IDXGIFactory4> factory;
            if (device && SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))) &&
                FAILED(factory->EnumAdapterByLuid(device->GetAdapterLuid(), IID_PPV_ARGS(&memAdapter))))
                memAdapter.Reset();
        }
        std::string s = "mem: vram ";
        DXGI_QUERY_VIDEO_MEMORY_INFO vm {};
        if (memAdapter && SUCCEEDED(memAdapter->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &vm)))
            s += std::to_string(vm.CurrentUsage >> 20) + " MiB used of " + std::to_string(vm.Budget >> 20) + " MiB budget";
        else
            s += "n/a";
        PROCESS_MEMORY_COUNTERS_EX pmc {};
        pmc.cb = sizeof pmc;
        if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof pmc))
            s += ", private " + std::to_string(static_cast<unsigned long long>(pmc.PrivateUsage >> 20)) + " MiB";
        else
            s += ", private n/a";
        return s;
    }
    // Vulkan bridge: the launch marker (kVkLaunchMarker) is there only while a first answer is
    // outstanding. Written before a PrepareFrame attempt (Record), removed at the first answer
    // (ClearVkMarker) or as soon as an attempt demonstrably failed without hanging (DropVkMarker);
    // a later attempt writes it again, at most once per kVkMarkerRewriteMs.
    void WriteVkMarker()
    {
        ++vkMarkerWrites;
        vkMarkerLastWrite = GetTickCount64();
        {
            std::ofstream marker(directory / kVkLaunchMarker, std::ios::out | std::ios::trunc);
            vkMarkerLive = marker.is_open();
        }
        if (!vkMarkerLive)
        {
            Log("lmxxf vk: lmxxf_vk_launch.pending could not be written (no self-healing fallback for this attempt)");
            return;
        }
        if (vkMarkerWrites > 1)
        {
            if (vkMarkerWrites <= 3)
                Log("lmxxf vk: lmxxf_vk_launch.pending written again before PrepareFrame (still no answer; write " +
                    std::to_string(vkMarkerWrites) + (vkMarkerWrites == 3 ? ", not logged again)" : ")"));
            return;
        }
        // What the next start does depends on danielblnc's runtime being installed (the file
        // AmdBridge::HasFiles looks for); the line promises it only when it is there.
        std::error_code ec;
        const bool daniel = std::filesystem::exists(directory / L"dlssnr_amd_pass1.dll", ec);
        Log(std::string("lmxxf vk: lmxxf_vk_launch.pending written before the first PrepareFrame (removed at the first "
                        "answer, or when an attempt fails without hanging; if this session stops before either, the next start ") +
            (daniel ? "runs danielblnc's runtime)" : "will not run lmxxf over the Vulkan bridge)"));
    }
    // An attempt ended without hanging and without an answer outstanding: the marker goes, with the
    // real reason in `line`.
    void DropVkMarker(const std::string& line)
    {
        vkMarkerLive = false;
        std::error_code ec;
        std::filesystem::remove(directory / kVkLaunchMarker, ec);
        if (vkMarkerWrites <= 3)
            Log(line);
        else if (!loggedVkMarkerDone)
        {
            loggedVkMarkerDone = true;
            Log("lmxxf vk: attempts keep failing without hanging - the marker is still re-armed before each attempt "
                "(at most every 5 s); further marker lines are not logged");
        }
    }
    void ClearVkMarker()
    {
        vkFirstAnswer = true;
        DropVkMarker("lmxxf vk: first answer consumed - lmxxf_vk_launch.pending removed");
    }
    // Queue migration (Submitted): fence the session the neural list left on both queues and park it
    // in `retired`; ReleaseRetired destroys it later. No CPU wait here.
    void RetireSession(ID3D12CommandQueue* newQueue)
    {
        RetiredSession r;
        r.ctx = ctx;
        r.oldQueue = queue;
        r.newQueue = newQueue;
        r.value = 1;
        const bool fenced = ctx && queue && newQueue &&
                            SUCCEEDED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&r.oldFence))) &&
                            SUCCEEDED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&r.newFence))) &&
                            SUCCEEDED(queue->Signal(r.oldFence.Get(), r.value)) &&
                            SUCCEEDED(newQueue->Signal(r.newFence.Get(), r.value));
        if (!fenced)
        {
            if (ctx)
                Log("lmxxf: the previous queue's session could not be fenced for release; it is left alive (as before)");
            AbandonRetired(r);
            return;
        }
        retired.push_back(std::move(r));
        if (retired.size() > kMaxRetired)
        {
            Log("lmxxf: " + std::to_string(retired.size()) + " sessions of earlier queues are waiting for their lists; "
                "the oldest is left alive (leaked, as before)");
            AbandonRetired(retired.front());
            retired.erase(retired.begin());
        }
    }
    // A retired session that will not be destroyed: its fences may still have a queued Signal, so
    // they are leaked with it rather than released.
    static void AbandonRetired(RetiredSession& r)
    {
        r.oldFence.Detach();
        r.newFence.Detach();
        r.ctx = nullptr;
    }
    // Destroy every retired session whose two fences have been reached (see Record for when).
    void ReleaseRetired()
    {
        for (auto it = retired.begin(); it != retired.end();)
        {
            if (it->oldFence->GetCompletedValue() < it->value || it->newFence->GetCompletedValue() < it->value)
            {
                ++it;
                continue;
            }
            if (it->ctx && api.Destroy)
                api.Destroy(it->ctx);
            it = retired.erase(it);
            Log("lmxxf: released the session of the previous queue (migration)");
        }
    }
    std::string LastError()
    {
        char e[512] {};
        if (api.GetLastError) api.GetLastError(e, sizeof e);
        return e;
    }
    std::string RuntimeStatus()
    {
        // 512 (0.3.4, AUTOMASK plan A4): the runtime drops words to fit the buffer it is given, and with the character
        // mask's ctl= word a long RDNA 3 status passes 256; 0.3.3.2 read 256.
        char st[512] {};
        if (ctx && api.GetStatus) api.GetStatus(ctx, st, sizeof st);
        return st;
    }
    // For the stats and resize lines (R3): the runtime's HIP import pool - the shared buffers made so far (new ones only
    // when a network needs more than the free ones hold), those in use, their size, and how many bridge buffers were
    // served by reuse. An older runtime has no pool: each rebuild keeps its buffers (about 100 MB at the 1080 tier).
    // (P5, 0.3.3.2) The first frame line runs before Load() binds poolStats: that is "not loaded yet", and
    // "older runtime" is kept for a loaded LmxxfNrRuntime.dll without the LmxxfNrGetImportPoolStats export.
    std::string PoolNote()
    {
        if (!dll)
            return "hip buffers: runtime not loaded yet";
        if (!poolStats)
            return "hip buffers n/a (older runtime)";
        LmxxfNrImportPoolStats ps {};
        ps.struct_size = sizeof ps;
        if (poolStats(&ps) != LMXXF_NR_OK)
            return "hip buffers n/a (runtime did not say)";
        return "hip buffers " + std::to_string(ps.buffers) + " (busy " + std::to_string(ps.busy) + ", " +
               std::to_string(ps.bytes >> 20) + " MiB; imports " + std::to_string(ps.imports) + ", reuses " +
               std::to_string(ps.reuses) + (ps.enabled ? ")" : ", off by DLSS5_IMPORT_POOL=0)");
    }
    // For the stats line: the runtime's idle_syncs= and idle_sync_skips= words (RAM leak fix, 0.3.3), as
    // its status reports them. idle_syncs counts the HIP stream synchronizes, which release the memory
    // HIP keeps per launch; idle_sync_skips counts launches made while the previous job still ran, so
    // there was no synchronize. In a GPU-bound, pipelined game skips climb far faster than syncs
    // (Silent Hill 2: 9035 against 48563); that no longer means memory is held, because the bridge
    // also records an event behind every job (hip_d3d12_bridge.h release_mark), which lets HIP free
    // finished launches without a synchronize. Both count per network build, so a resize or a Full
    // network change restarts them. Reads the status as the every-30-frames status line below does
    // (GetStatus also clears the runtime's LastError, which nothing reads after step 9). The words
    // are missing when the session was poisoned by an error (0.3.3.2: said so, not blamed on the
    // runtime's age), before the first network is built, and from an older runtime ("n/a").
    std::string IdleSyncNote()
    {
        const std::string st = RuntimeStatus();
        if (st.rfind("lmxxf poisoned", 0) == 0)
            return "idle syncs n/a (session stopped by an error)";
        if (st.find(" hip=0 ") != std::string::npos)
            return "idle syncs n/a (no network built yet)";
        std::string note;
        for (const char* key : { "idle_syncs=", "idle_sync_skips=" })
        {
            const size_t at = st.find(key);
            if (at == std::string::npos)
                return "idle syncs n/a (runtime did not say)";
            const size_t end = st.find(' ', at);
            note += (note.empty() ? "" : " ") + st.substr(at, end == std::string::npos ? std::string::npos : end - at);
        }
        // (0.3.4, IDLESYNC) The release marks behind the jobs - what lets HIP free finished launches without a
        // synchronize - read beside the skips they answer for: the runtime's release_marks= word, and
        // release_mark_failures= when a record failed. Optional: an older runtime has neither, and the runtime drops
        // them first when its status would not fit the host's 256 chars, so a missing word adds nothing here.
        for (const char* key : { "release_marks=", "release_mark_failures=" })
        {
            const size_t at = st.find(key);
            if (at == std::string::npos)
                continue;
            const size_t end = st.find(' ', at);
            note += " " + st.substr(at, end == std::string::npos ? std::string::npos : end - at);
        }
        return note;
    }
    // (AMDNR 0.3.3.2) After a fatal error the runtime poisons its session and refuses every later call
    // with "session is poisoned", so the log and the Neural tab never said why lmxxf stopped. Called
    // after a failed call, with its error already read (GetStatus clears the runtime's LastError):
    // once the status says poisoned, keeps the first error - the runtime's own record ("fatal error:
    // <text>", 0.3.3.2 runtime), else the one this call returned - logs it once, and Status() shows it.
    void NoteFatal(const std::string& err)
    {
        if (!fatalError.empty() || !ctx)
            return;
        const std::string st = RuntimeStatus();
        if (st.rfind("lmxxf poisoned", 0) != 0)
            return;
        const std::string tag = "(fatal error: ";
        const size_t at = st.find(tag);
        if (at != std::string::npos)
        {
            fatalError = st.substr(at + tag.size());
            if (!fatalError.empty() && fatalError.back() == ')')
                fatalError.pop_back();
        }
        if (fatalError.empty())
            fatalError = err.empty() ? std::string("the runtime did not say which") : err;
        Log("lmxxf stopped after an error: " + fatalError + " - the runtime refuses every call after it; lmxxf stays off for this session");
    }
    // (0.3.4, runtime work B9) The network size tier policy, once per backend at its first Record (the bridge described
    // the NR adapter before it built this backend, so GpuSupportInfo is that GPU): [DlssNr] AmdLmxxfTierCap (ini only,
    // read at start, never saved) and the GPU class (LmxxfTierPolicy.h DecideLmxxfTierPolicy). lmxxf only: danielblnc's
    // runtime has no network tiers.
    void DecideTierPolicy()
    {
        if (tierDecided)
            return;
        tierDecided = true;
        const auto& gpu = DlssNr::AmdBridge::GpuSupportInfo();
        const bool handheld = gpu.lmxxfOk && Lmxxf::LmxxfHandheldTarget(gpu.target);
        const bool gfx11Desktop = Lmxxf::LmxxfTierSnapDefault(gpu.target) && !Lmxxf::LmxxfHandheldTarget(gpu.target);
        const int key = ::Config::Instance()->AmdLmxxfTierCap.value_or_default();
        tierWish = Lmxxf::DecideLmxxfTierPolicy(key, handheld, gfx11Desktop, kGfx11DesktopSmallTiers);
        tierSmall = tierWish.smallTiers;
        tierWhy = handheld ? "handheld APU " + gpu.name + " (" + gpu.target + ", experimental)"
                  : tierWish.smallTiers && tierWish.cap != 0 ? "[DlssNr] AmdLmxxfTierCap=" + std::to_string(tierWish.cap)
                  : tierWish.smallTiers ? std::string("RDNA 3 desktop") : std::string();
        if (key != 0 && Lmxxf::NormalizeLmxxfTierCap(key) == 0)
            Log("lmxxf: [DlssNr] AmdLmxxfTierCap=" + std::to_string(key) + " is not a network tier (0 auto, 360, 576, 720, 900, 1080) - auto is used");
        PublishTierPolicy();
    }
    // The cap the sizing really uses: a 360 / 576 cap without the small tiers is 720 (PlanLmxxfSize's rule).
    int TierCapUsed() const
    {
        return !tierSmall && (tierWish.cap == 360 || tierWish.cap == 576) ? 720 : tierWish.cap;
    }
    void PublishTierPolicy()
    {
        smallTierPolicy.store(tierSmall && ctx != nullptr);
        tierCapInUse.store(TierCapUsed());
    }
    // Whether the sizing goes through PlanLmxxfSize: small tiers wished (or on), or a cap below the 1080 tier.
    bool TierPlanned() const
    {
        return tierSmall || (tierWish.cap != 0 && tierWish.cap != 1080);
    }
    // The resize line's words for a planned size: " (360 tier, small tiers on, cap 360 auto: the NR size 1280x720
    // capped to 640x360)", and the move inside the tiers as the tier snap words say it.
    std::string TierPlanNote(const Lmxxf::TierPlan& p, UINT frameW, UINT frameH, float scale) const
    {
        const auto size = [](UINT a, UINT b) { return std::to_string(a) + "x" + std::to_string(b); };
        std::string s = " (" + std::to_string(p.to) + " tier, small tiers " + (tierSmall ? "on" : "off") + ", cap " +
                        (tierWish.cap != 0 ? std::to_string(TierCapUsed()) + (tierWish.autoCap ? " auto" : "") : std::string("none"));
        UINT nw = 0, nh = 0;
        ModelSize(frameW, frameH, scale, nw, nh);
        std::string what;
        if (nw != p.askedW || nh != p.askedH)
            what = "the NR size " + size(nw, nh) + " capped to " + size(p.askedW, p.askedH);
        if (p.from != p.to)
            what += (what.empty() ? "" : "; ") + size(p.askedW, p.askedH) + " lands in the " + std::to_string(p.from) +
                    " tier nearer the " + std::to_string(p.to) + " box";
        else if (p.grown)
            what += (what.empty() ? "" : "; ") + size(p.askedW, p.askedH) + " grown to fill the " + std::to_string(p.from) +
                    " tier, the same cost";
        return s + (what.empty() ? "" : ": " + what) + ")";
    }
    // After Create (Load): ask this session for the small tiers when they are wished. Only then: a runtime that does not
    // have them (0.3.3.2's 1343bbc5, the first 0.3.4 builds) is never asked, and without the call every size runs as in
    // 0.3.3.2. A missing export or a refusal leaves them off (a 360 / 576 cap then caps at 720) and says so once.
    void ApplyTierPolicy()
    {
        DecideTierPolicy();
        if (tierWish.smallTiers)
        {
            const auto setPolicy = reinterpret_cast<int32_t (*)(void*, uint32_t)>(GetProcAddress(dll, kSetTierPolicyExport));
            std::string refused;
            if (!setPolicy)
                refused = "this LmxxfNrRuntime.dll has no small network sizes (no LmxxfNrSetTierPolicy: an older runtime)";
            else if (const int32_t rc = setPolicy(ctx, kTierPolicySmall); rc != LMXXF_NR_OK)
                refused = "the runtime refused the small network sizes (LmxxfNrSetTierPolicy " + std::to_string(rc) + ": " + LastError() + ")";
            tierSmall = refused.empty();
            if (tierSmall)
                Log("lmxxf: small network sizes on (the 360 and 576 tiers: 640x360 and 1024x576 boxes) for " + tierWhy +
                    (tierWish.cap != 0 ? ", cap " + std::to_string(tierWish.cap) + (tierWish.autoCap ? " (auto)" : "") : std::string(", no cap")));
            else
                Log("lmxxf: " + refused + " - wanted for " + tierWhy +
                    (tierWish.cap == 360 || tierWish.cap == 576 ? "; the network size is capped at the 720 tier instead" : std::string()) +
                    "; use the LmxxfNrRuntime.dll shipped with this OptiScaler build");
        }
        else if (tierWish.cap != 0 && tierWish.cap != 1080)
            Log("lmxxf: network size capped at the " + std::to_string(tierWish.cap) + " tier ([DlssNr] AmdLmxxfTierCap=" +
                std::to_string(tierWish.cap) + ")");
        PublishTierPolicy();
    }
    // (0.3.4, lmxxf Fast mode) What [DlssNr] AmdLmxxfFastMode did this Record, for the menu (FastModeFromTier) and one
    // log line per change: state 0 = off, -1 = on but the default already feeds the smallest tier this runtime has, else
    // the default's tier (fed defW x defH), lowered to fastW x fastH in the `fastTo` tier. Off from the start logs nothing.
    void NoteFastMode(int state, UINT defW, UINT defH, UINT fastW, UINT fastH, unsigned fastTo)
    {
        fastModeFromTier.store(state, std::memory_order_relaxed);
        if (state == fastLogged)
            return;
        fastLogged = state;
        const auto size = [](UINT a, UINT b) { return std::to_string(a) + "x" + std::to_string(b); };
        if (state == 0)
            Log("lmxxf: Fast mode off ([DlssNr] AmdLmxxfFastMode): the network runs its default size tier again");
        else if (state < 0)
            Log("lmxxf: Fast mode on ([DlssNr] AmdLmxxfFastMode), but the network already runs the smallest size tier this "
                "runtime has (" + size(defW, defH) + "): nothing to lower, no change");
        else
            Log("lmxxf: Fast mode on ([DlssNr] AmdLmxxfFastMode): the network runs one size tier lower, fed " +
                size(fastW, fastH) + " in the " + std::to_string(fastTo) + " tier instead of " + size(defW, defH) + " in the " +
                std::to_string(state) + " tier (on an RX 9070 XT about 29% less network time from the 1080 tier); the only "
                "knob is the fed size: no network block is skipped for it, the runtime and its block list are the default's "
                "(Full network as set)");
    }
    // Bring the runtime up on `queue`: load the DLL once, create a session, bind the queue.
    bool Load()
    {
        fatalError.clear(); // a new session: an earlier stop was the old one's
        if (!dll)
        {
            const auto path = RuntimePath(directory);
            dll = LoadLibraryW(path.c_str());
            if (!dll)
            {
                Log("lmxxf: " + path.string() + " failed to load (Win32 error " + std::to_string(GetLastError()) + ")");
                failed = true;
                return false;
            }
        }
        auto getApi = reinterpret_cast<int32_t (*)(uint32_t, LmxxfNrApi*)>(GetProcAddress(dll, "LmxxfNrGetApi"));
        api = {};
        api.struct_size = sizeof api;
        if (!getApi || getApi(LMXXF_NR_ABI_VERSION, &api) != LMXXF_NR_OK || !api.EnqueueHipAsync || !api.AbandonJob || !api.OutputReady)
        {
            Log("lmxxf: LmxxfNrRuntime.dll is not this build's runtime (LmxxfNrGetApi / EnqueueHipAsync / AbandonJob / OutputReady missing); "
                "use the LmxxfNrRuntime.dll shipped with this OptiScaler build");
            failed = true;
            return false;
        }
        // R3: an optional export, so this host still runs the first 0.3.3.2 runtime (it re-imports on every rebuild).
        // Logged once per process, when the answer is first known.
        poolStats = reinterpret_cast<int32_t (*)(LmxxfNrImportPoolStats*)>(GetProcAddress(dll, "LmxxfNrGetImportPoolStats"));
        {
            LmxxfNrImportPoolStats ps {};
            ps.struct_size = sizeof ps;
            const int pool = poolStats && poolStats(&ps) == LMXXF_NR_OK && ps.enabled ? 1 : 0;
            if (importPool.exchange(pool) != pool)
                Log(pool ? "lmxxf: HIP buffers are made once per network size and reused (AMD HIP import leak worked around)"
                         : poolStats ? "lmxxf: HIP buffer reuse is off (DLSS5_IMPORT_POOL=0): about 100 MB kept per NR size change"
                                     : "lmxxf: this LmxxfNrRuntime.dll re-imports its HIP buffers on every rebuild (about 100 MB kept per NR "
                                       "size change) - use the one shipped with this build");
        }
        LmxxfNrCapabilities caps {};
        caps.struct_size = sizeof caps;
        api.QueryCapabilities(&caps);
        const auto assetsPath = UsedAssetsPath(directory); // the pak first (P1, 0.3.3.2)
        const std::wstring assets = assetsPath.wstring();
        LmxxfNrCreateInfo ci {};
        ci.struct_size = sizeof ci;
        ci.device = device.Get();
        ci.queue = queue.Get();
        ci.assets_directory = assets.c_str();
        void* c = nullptr;
        if (api.Create(&ci, &c) != LMXXF_NR_OK || !c)
        {
            Log("lmxxf Create failed (assets " + assetsPath.string() + "): " + LastError());
            failed = true;
            return false;
        }
        if (api.PrepareSession(c) != LMXXF_NR_OK)
        {
            Log("lmxxf PrepareSession failed: " + LastError());
            api.Destroy(c);
            failed = true;
            return false;
        }
        ctx = c;
        // (0.3.4, runtime work B9) Before the first PrepareFrame, which resolves the network size: only when wished.
        ApplyTierPolicy();
        std::error_code ec;
        // (0.3.4, LF-B) The modules come from the pak whenever the pak is used; the folder names only for loose assets.
        Log("lmxxf runtime up on queue type " + std::to_string(static_cast<UINT>(queue->GetDesc().Type)) + ": " +
            RuntimeStatus() + " (assets " + assetsPath.string() +
            (assetsPath == PakPath(directory)                                    ? ", modules pak"
             : std::filesystem::is_directory(directory / L"lmxxf-modules", ec) ? ", modules lmxxf-modules\\"
                                                                                 : ", modules HIP\\") +
            ", network input ceiling " + std::to_string(caps.max_input_width) + "x" + std::to_string(caps.max_input_height) + ")");
        return true;
    }
    void Zero(ID3D12GraphicsCommandList* cmd, ID3D12Resource* target)
    {
        ID3D12Resource* srv[4] = { full.Get(), full.Get(), expoJob.Get(), full.Get() };
        const DXGI_FORMAT fmts[4] = { kFp16, kFp16, DXGI_FORMAT_R32_FLOAT, kFp16 };
        Barrier(cmd, target, kSrv, kUav);
        residual->Run(cmd, srv, fmts, target, kFp16, w, h, 1u);
        Barrier(cmd, target, kUav, kSrv);
    }
    void Bury(ComPtr<ID3D12Resource>& r)
    {
        if (r) graveyard.emplace_back(frames, r);
        r.Reset();
    }
    // (0.3.4, P3) On a grace frame: the late warp pass and its two render-size textures on first use, then a copy of
    // `full` - still the raw frame the pending job was fed, step 2 refreshes it later in this Record - into lateBase, on
    // this frame's list (it runs after the late list on the queue, before this frame's step 2). False, and no grace for
    // the rest of the session, when they cannot be made: the job is then dropped as in 0.3.3.2.
    bool EnsureLate(ID3D12GraphicsCommandList* cmd)
    {
        if (lateBroken || !full || !temporal)
            return false;
        try
        {
            if (!lateWarp)
                lateWarp = std::make_unique<Pass>(device.Get(), LateEditWarpShader, sizeof(LateEditWarpShader), "lmxxf late edit warp", 2u, nullptr);
            if (!lateBase || !lateEdit || lateBase->GetDesc().Width != w || lateBase->GetDesc().Height != h)
            {
                Bury(lateBase);
                Bury(lateEdit);
                lateBase = Texture(device.Get(), w, h, kFp16);
                lateEdit = Texture(device.Get(), w, h, kFp16);
            }
        }
        catch (const std::exception& e)
        {
            lateBroken = true;
            lateWarp.reset();
            Bury(lateBase);
            Bury(lateEdit);
            Log(std::string("lmxxf: late-submission grace unavailable for this session (") + e.what() +
                "); a job whose list is late is dropped as before");
            return false;
        }
        Barrier(cmd, full.Get(), kSrv, D3D12_RESOURCE_STATE_COPY_SOURCE);
        Barrier(cmd, lateBase.Get(), kSrv, D3D12_RESOURCE_STATE_COPY_DEST);
        cmd->CopyResource(lateBase.Get(), full.Get());
        Barrier(cmd, lateBase.Get(), D3D12_RESOURCE_STATE_COPY_DEST, kSrv);
        Barrier(cmd, full.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, kSrv);
        return true;
    }
    // (0.3.4, P3) Submitted saw the list of a job Record dropped as never submitted: the game submitted it late. Counted
    // for the stats line and the menu within kLateWindow frames (the job is gone; nothing is launched). Takes `lock`.
    void LateArrival(ID3D12CommandList* l)
    {
        std::lock_guard guard(lock);
        const AmdPreSr::LockOwnerMark owned(lockOwner);
        if (failed || shutdown || droppedList.load() != l)
            return;
        droppedList.store(nullptr);
        const UINT64 late = Lmxxf::FramesLate(frames, droppedFedAt);
        if (!Lmxxf::CountsAsLate(late))
            return;
        ++statLate;
        statLateMax = (std::max)(statLateMax, static_cast<unsigned>(late));
        if (!loggedLateArrival)
        {
            loggedLateArrival = true;
            Log("lmxxf: a neural list whose job was dropped as never submitted reached ExecuteCommandLists " + std::to_string(late) +
                " frame(s) later: the game submits frames late (the stats line counts them as late) (noted once)");
        }
    }
    // (0.3.4, P7.7, G2) The RTGI ring out of use: every instance waits eight frames in rtgiRetired, as buried
    // textures do, then is freed; the next Record starts a fresh ring.

    // The composition's controls, sanitised again (the bridge already did): a non-finite value is
    // the neutral one.
    static float Unit(float v, float lo, float hi, float fallback) { return std::isfinite(v) ? std::clamp(v, lo, hi) : fallback; }
    // RenoDX composition: the two passes and the delta texture, made the first time the mode is
    // used (the delta again after a size change). False for the rest of the session when a pass
    // does not build; the compiler's text is logged once.
    bool EnsureCompose()
    {
        if (composeBroken)
            return false;
        try
        {
            if (!composePass)
            {
                // The shared HLSL's lmxxf entry: t0 the answer, t1 fed, u0 the delta, and the same 12
                // root constants Run packs ({w, h, flags, T}, {Cs, G, skin detail, skin colour},
                // {environment detail, environment colour, white, pad}).
                const D3D_SHADER_MACRO delta[] = { { "NRCOMPOSE_DELTA", "1" }, { nullptr, nullptr } };
                composePass = std::make_unique<Pass>(device.Get(), AmdPreSr::AmdNrComposeHlsl, sizeof(AmdPreSr::AmdNrComposeHlsl),
                                                     "lmxxf renodx compose", 2u, delta);
            }
            if (!residualDelta)
            {
                // t0 the delta, t1 raw, t2 the job's exposure, t3 depth, t4 the answer (flagged pixels).
                const D3D_SHADER_MACRO delta[] = { { "AMDNR_DELTA", "1" }, { nullptr, nullptr } };
                residualDelta = std::make_unique<Pass>(device.Get(), ResidualShader, sizeof(ResidualShader), "lmxxf residual (renodx delta)",
                                                       5u, delta);
            }
            if (!composeDelta || composeDelta->GetDesc().Width != mw || composeDelta->GetDesc().Height != mh)
            {
                Bury(composeDelta);
                composeDelta = Texture(device.Get(), mw, mh, kFp16);
            }
            return true;
        }
        catch (const std::exception& e)
        {
            // What failed here was never recorded, but a pass built on an earlier frame may be on a
            // list in flight: everything is kept alive and only the mode is refused from now on.
            composeBroken = true;
            Log(std::string("lmxxf: colour composition RenoDX cannot run in this session: ") + e.what());
            return false;
        }
    }
    // (0.3.4, P7.5) The encoding pass and its two textures (render size, FP16), made on first use; false for the rest
    // of the session when one cannot be made (logged once; nothing of it was recorded, so nothing is in flight).
    bool EnsureEncoding()
    {
        if (encodingBroken)
            return false;
        try
        {
            if (!encodingPass)
                encodingPass = std::make_unique<Pass>(device.Get(), AmdPreSr::EncodingShader, sizeof(AmdPreSr::EncodingShader),
                                                      "lmxxf encoding", 1u, nullptr, 4u);
            if (!decodedColour || decodedColour->GetDesc().Width != w || decodedColour->GetDesc().Height != h)
            {
                Bury(decodedColour);
                decodedColour = Texture(device.Get(), w, h, kFp16);
            }
            if (!encodedOut || encodedOut->GetDesc().Width != w || encodedOut->GetDesc().Height != h)
            {
                Bury(encodedOut);
                encodedOut = Texture(device.Get(), w, h, kFp16);
            }
            return true;
        }
        catch (const std::exception& e)
        {
            encodingBroken = true;
            Log(std::string("lmxxf: AmdEncoding sRGB / Gamma 2.2 cannot run in this session, the colour is used as is: ") + e.what());
            return false;
        }
    }
    // The composition's log line and the menu note (AmdBridge::SetCompositionNote; "" in Classic),
    // on a change of mode only. A title that flips between two answers stops being logged after a
    // dozen lines; the note always follows.
    void NoteComposition(int state, const char* reason, const AmdPreSr::Settings& cfg)
    {
        if (state == composeState)
            return;
        composeState = state;
        if (state != 1)
            whiteSource = AmdPreSr::kCompositionWhiteNone;
        std::string line, note;
        if (state == 0)
            line = "lmxxf: colour composition Classic - the runtime composes at Detail / Colour strength, as before "
                   "(a job already fed keeps the mode it was fed in)";
        else if (state == 1)
        {
            char buf[640];
            std::snprintf(buf, sizeof buf,
                          "lmxxf: colour composition RenoDX (experimental): runtime strengths 1/1 for the jobs fed from now on (the "
                          "runtime's decode then returns RenoDX's upgraded answer, against its own proxy of what the network saw), "
                          "composed on the host against the fed picture - detail %.2f, colour %.2f, highlight guard %.1fx, skin / "
                          "environment edit %s; W from fed units (W = 1, the runtime's paper white; the job's exposure brings the "
                          "edit back to game units); latched per job, the edit shaper and Residual strength / limit still follow",
                          Unit(cfg.composeDetail, 0.f, 2.f, 1.f), Unit(cfg.composeColour, 0.f, 4.f, 1.f), Unit(cfg.maxRatio, 1.f, 8.f, 2.f),
                          cfg.skinProtection ? "on" : "off");
            line = buf;
            // (0.3.4, T10) The menu note carries no runtime tag: the menu's status line names the runtime.
            note = "RenoDX on: white point in fed units (composed against what the network was fed).";
        }
        else
        {
            line = std::string("lmxxf: colour composition RenoDX refused, Classic is used: ") + reason;
            note = std::string("RenoDX refused: ") + reason + ". Classic runs.";
        }
        DlssNr::AmdBridge::SetCompositionNote(note);
        if (composeChanges < 12)
        {
            ++composeChanges;
            Log(line + (composeChanges == 12 ? " (further changes are not logged)" : ""));
        }
    }
    // The colour composition for the job about to be fed ([DlssNr] AmdComposition): true = RenoDX,
    // the runtime is sent strengths 1/1 and the answer is composed at consume. Refused - Classic for
    // this job, one log line and the menu note - where the tail cannot run on what this runtime
    // answers: Network output (nothing of ours after the model, as on danielblnc); display-referred
    // colour (the tail is linear only; AmdEncoding 2/3 is tested as well, since final image mode
    // sets it without the flag); final image mode's no-motion carry; the runtime's two overrides,
    // DLSS5_CODEC_SRGB=1 (its decode linearises an sRGB source, native_codec_decode.hlsl) and
    // DLSS5_DEBUG_TINT=1 (it shows its tint view, LmxxfNrRuntime.cpp); a pass that did not build.
    bool ComposeMode(const AmdPreSr::Frame& f, const AmdPreSr::Settings& cfg, bool noMotion)
    {
        static const char* const kRefused[] = {
            "", "",
            "Network output is on (the model's answer is shown untouched)",
            "the colour is display-referred (no IsHDR create flag, a format that cannot hold linear HDR, or AmdEncoding "
            "sRGB / Gamma 2.2) and the RenoDX tail is linear only",
            "final image mode (the no-motion carry)",
            "DLSS5_CODEC_SRGB=1 (the runtime's codec treats the colour as display-referred sRGB)",
            "DLSS5_DEBUG_TINT=1 (the runtime shows its tint view, not its answer)",
            "the compose pass or its texture could not be made (lmxxf_backend.log says why)",
        };
        int state = 0;
        if (cfg.composition == 1u)
        {
            if (cfg.networkOutput)
                state = 2;
            else if (f.displayReferred || cfg.encoding == 2u || cfg.encoding == 3u)
                state = 3;
            else if (noMotion)
                state = 4;
            else if (EnvIsOne(L"DLSS5_CODEC_SRGB"))
                state = 5;
            else if (EnvIsOne(L"DLSS5_DEBUG_TINT"))
                state = 6;
            else if (!EnsureCompose())
                state = 7;
            else
                state = 1;
        }
        NoteComposition(state, kRefused[state], cfg);
        return state == 1;
    }
    ID3D12Resource* Record(ID3D12GraphicsCommandList* cmd, const AmdPreSr::Frame& f, const AmdPreSr::Settings& cfg);
};

ID3D12Resource* Backend::Impl::Record(ID3D12GraphicsCommandList* cmd, const AmdPreSr::Frame& f, const AmdPreSr::Settings& cfg)
{
    if (failed || shutdown)
        return nullptr;
    // A removed device (a TDR): stop cleanly and say so, as danielblnc does (C2-F, 0.3.3.2 rebuild). Without it the
    // status read "lmxxf stopped after an error: <the HIP or D3D12 text that followed>". Nothing is released or waited
    // for; a Vulkan launch marker stays, as for any stop that may have been ours.
    // (0.3.4, P24 review) Checked before P24's stop below, as 0.3.3.2 did every frame: a HIP error that a device removal
    // caused poisons the session first, and this line is what tells a TDR apart from a runtime fault in the log.
    if (const HRESULT removed = device->GetDeviceRemovedReason(); FAILED(removed))
    {
        failed = true;
        char hr[16];
        std::snprintf(hr, sizeof hr, "0x%08X", static_cast<unsigned>(removed));
        Log(std::string("lmxxf stopped: the D3D12 device was removed (HRESULT ") + hr +
            ") - nothing is released or waited for");
        return nullptr;
    }
    // (0.3.4, P24) A session the runtime poisoned (NoteFatal) refuses every call after it: no more PrepareFrame per frame
    // (each failed, every 300th logged), no job, nothing recorded; the frame goes to the upscaler untouched. One line.
    if (!fatalError.empty())
    {
        if (!loggedStoppedSkip)
        {
            loggedStoppedSkip = true;
            Log("lmxxf: no more runtime calls in this session (stopped after the error above); the upscaler gets the game's frame");
        }
        return nullptr;
    }
    // After Ray Regeneration (f.writeBack) the colour is the finished output at display
    // resolution and there is no upscaler after it to hand a replacement to. The pipeline below
    // runs unchanged on the display grid (the network is fed a copy fitted into its 1920x1080
    // ceiling, the edit is lifted back), the render-grid depth is resampled onto that grid,
    // the jitter is ignored (the output is resolved), and the result is written back into the
    // output at the end of the frame. One frame late, like the pre-SR placement.
    if (!cmd || !f.colour || !f.motion || !f.depth || !f.width || !f.height)
    {
        ++skips;
        return nullptr;
    }
    if (!temporal)
        temporal = std::make_unique<AmdPreSr::TemporalStability>(device.Get());
    if (!blitColour)
    {
        const D3D_SHADER_MACRO m4[] = { { "OUT_T", "float4" }, { nullptr, nullptr } };
        const D3D_SHADER_MACRO m2[] = { { "OUT_T", "float2" }, { nullptr, nullptr } };
        exposurePass = std::make_unique<Pass>(device.Get(), ExposureShader, sizeof(ExposureShader), "lmxxf exposure", 1u, nullptr);
        blitColour = std::make_unique<Pass>(device.Get(), BlitShader, sizeof(BlitShader), "lmxxf blit", 3u, m4);
        blitMotion = std::make_unique<Pass>(device.Get(), BlitShader, sizeof(BlitShader), "lmxxf motion blit", 3u, m2);
        motionChain = std::make_unique<Pass>(device.Get(), MotionChainShader, sizeof(MotionChainShader), "lmxxf motion chain", 2u, nullptr);
        residual = std::make_unique<Pass>(device.Get(), ResidualShader, sizeof(ResidualShader), "lmxxf residual", 4u, nullptr);
        blitDepth = std::make_unique<Pass>(device.Get(), DepthBlitShader, sizeof(DepthBlitShader), "lmxxf depth blit", 1u, nullptr);
        lumaDown = std::make_unique<Pass>(device.Get(), LumaDownShader, sizeof(LumaDownShader), "lmxxf luma down", 1u, nullptr);
        blockMatch = std::make_unique<Pass>(device.Get(), BlockMatchShader, sizeof(BlockMatchShader), "lmxxf block match", 2u, nullptr);
        editWarp = std::make_unique<Pass>(device.Get(), EditWarpShader, sizeof(EditWarpShader), "lmxxf edit warp", 3u, nullptr);
        // The appearance filter ("Image look"), the same shader the danielblnc path runs, on the
        // frame after the carry: 1 SRV, 24 root constants.
        expoJob = Texture(device.Get(), 2, 1, DXGI_FORMAT_R32_FLOAT); // [0] effective exposure, [1] the title's raw value
        autoExposure = std::make_unique<Pass>(device.Get(), AutoExposureShader, sizeof(AutoExposureShader), "lmxxf auto-exposure", 1u, nullptr);
        autoExpo = Texture(device.Get(), 4, 1, DXGI_FORMAT_R32_FLOAT);
        try
        {
            statsPass = std::make_unique<Pass>(device.Get(), StatsShader, sizeof(StatsShader), "lmxxf stats", 2u, nullptr);
            statsTex = Texture(device.Get(), 12, 1, DXGI_FORMAT_R32_UINT);
            statsRead[0] = ReadbackBuffer(device.Get(), 512);
            statsRead[1] = ReadbackBuffer(device.Get(), 512);
        }
        catch (...)
        {
            statsPass.reset(); // stats are a diagnostic; the backend runs without them
        }
    }
    // (0.3.4, P3) A job still recorded on a list the game has not submitted is handled below, once the frame and
    // network size of this Record are known (the grace needs them): kept one frame, or dropped as in 0.3.3.2.
    // NR resolution is the user's: at 100% the copy is pixel-exact; below it the edit is lifted
    // back to the frame; above it the network is fed an upscaled copy (fitted into 1920x1080),
    // which costs more and resamples the edit both ways. The menu says so; the choice stays.
    UINT wantW = 0, wantH = 0;
    UINT askedW = 0, askedH = 0;
    TierSnap tierSnap {};
    // (0.3.4, runtime work B9) The network size tier policy (LmxxfTierPolicy.h): with the small tiers (handheld APUs,
    // [DlssNr] AmdLmxxfTierCap=360 / 576) or a cap below 1080 the size is PlanLmxxfSize's. Neither (every desktop by
    // default): the 0.3.3.2 expression below, unchanged.
    DecideTierPolicy();
    const bool tierPlanned = TierPlanned();
    Lmxxf::TierPlan tierPlan {};
    if (!tierPlanned)
    {
        ModelSize(f.width, f.height, cfg.modelScale, wantW, wantH);
        // (0.3.4, FPS-Q1) [DlssNr] AmdLmxxfTierSnap: the size moved to a network tier box (SnapToNetworkTier: lowered to
        // the tier below when nearer its box, else grown to fill its own). Off: not called, the size is ModelSize's.
        askedW = wantW;
        askedH = wantH;
        tierSnap = cfg.lmxxfTierSnap ? SnapToNetworkTier(f.width, f.height, wantW, wantH) : TierSnap {};
    }
    else
    {
        tierPlan = Lmxxf::PlanLmxxfSize(f.width, f.height, cfg.modelScale, tierWish.cap, tierSmall, cfg.lmxxfTierSnap);
        askedW = tierPlan.askedW;
        askedH = tierPlan.askedH;
        wantW = tierPlan.w;
        wantH = tierPlan.h;
        tierSnap = { tierPlan.from, tierPlan.to, tierPlan.grown };
    }
    // (0.3.4, lmxxf Fast mode) [DlssNr] AmdLmxxfFastMode (default off): the network one size tier lower than the size
    // above (1080 -> 900, 900 -> 720; 720 -> 576 and 576 -> 360 with the small tiers), host only (LmxxfTierPolicy.h
    // PlanLmxxfFastSize: the frame planned again with the cap one tier lower). A change is a new fed size: handled below
    // as an NR resolution change (the job in flight abandoned, the textures remade, the history restarted). Off: the
    // size above is kept untouched, so the runtime gets exactly the default's inputs.
    Lmxxf::TierPlan fastPlan {};
    bool fastApplied = false;
    int fastState = 0;
    const UINT defaultW = wantW, defaultH = wantH;
    if (FastModeOn())
    {
        const unsigned defaultTier = static_cast<unsigned>(Lmxxf::LmxxfNetworkTier(wantW, wantH, tierSmall));
        fastApplied = Lmxxf::PlanLmxxfFastSize(f.width, f.height, cfg.modelScale, defaultTier, tierSmall, cfg.lmxxfTierSnap, fastPlan);
        fastState = fastApplied ? static_cast<int>(defaultTier) : -1;
        if (fastApplied)
        {
            wantW = fastPlan.w;
            wantH = fastPlan.h;
        }
    }
    NoteFastMode(fastState, defaultW, defaultH, wantW, wantH, fastPlan.to);
    const bool resized = f.width != w || f.height != h || wantW != mw || wantH != mh;
    // A job recorded on a list the game never submitted (it happens on a discarded frame) must
    // be cancelled before the runtime will prepare another.
    // (0.3.4, P3, [DlssNr] AmdLateSubmitGrace; LateSubmitGrace.h) ... unless the list is only late: some games submit
    // frame N's list after frame N+1's Evaluate (TLOU II). On the Record right after the feed, with no reset or discard
    // reported and no size change, the job is kept and this frame feeds nothing (graceFill); it launches when its list
    // arrives and its answer is consumed one Record later than usual (step 1: lateBase, the late warp). Still pending at
    // the Record after that: dropped as below. Evaluated here (was at the top of Record) because the size decides it; no
    // step between the two places reads the job's state.
    bool graceFill = false;
    if (jobRecorded)
    {
        Lmxxf::PendingJobFacts facts {};
        facts.graceOn = Config::Instance()->AmdLateSubmitGrace.value_or_default() && !lateBroken;
        facts.resetSeen = resetSeen.load() != nullptr;
        facts.resetCertain = resetCertain.load();
        facts.sizeChanges = resized;
        facts.graced = jobGraced;
        facts.recordsSinceFeed = lastFedFrame == ~0ull ? 0 : frames - lastFedFrame;
        if (Lmxxf::DecidePendingJob(facts) == Lmxxf::PendingJobAction::Grace && EnsureLate(cmd))
        {
            graceFill = true;
            jobGraced = true;
            ++statGraced;
            if (!loggedGrace)
            {
                loggedGrace = true;
                Log("lmxxf: the game had not submitted the previous frame's list by this frame's Evaluate (late submission); its "
                    "job is kept one frame instead of dropped - this frame feeds nothing, the answer comes one frame later "
                    "([DlssNr] AmdLateSubmitGrace; the stats line counts late / graced) (noted once)");
            }
        }
    }
    if (jobRecorded && !graceFill)
    {
        // (0.3.3.2) Explained when OptiScaler's bridge dropped the list, or the game reset it once a neural
        // list has reached ExecuteCommandLists under its own pointer (ListReset, rawSubmissions): the game
        // discarded it, and a later submission of the same object carries none of our copies (Submitted then
        // did not launch the job). Only a drop nothing explained can be a wrapper.
        const bool seen = resetSeen.exchange(nullptr) != nullptr;
        const bool certain = resetCertain.exchange(false);
        const bool explained = seen && (certain || rawSubmissions > 0);
        bool& said = explained ? loggedResetDrop : loggedUnsubmitted;
        if (!said)
        {
            said = true;
            Log(explained ? "lmxxf: the game reset the neural list before submitting it (a discarded frame); its job is dropped (noted once)"
                          : "lmxxf: the previous neural list was never submitted; its job is dropped (noted once)");
        }
        // (0.3.4, P3) Remembered so a late submission of it is counted (Submitted, 'late +N'); not after a reset.
        if (!seen && !certain)
        {
            droppedList.store(pendingList.load(), std::memory_order_release);
            droppedFedAt = lastFedFrame;
        }
        api.CancelUnsubmitted(ctx, job.handle);
        jobRecorded = false;
        jobGraced = false;
        // (0.3.3.2) A dropped list is a lost frame: the temporal pass, the carried edit and the network
        // history restart here instead of carrying on from a job that never ran.
        resetRequested.store(true);
        // The raw pointer never matched a submission, twice, with no reset to explain it: arm the COM
        // identity match (a wrapper between Evaluate and ExecuteCommandLists, as UE5 titles load them).
        // The second unexplained drop, not the first (0.3.3.2): one discarded frame whose list the game
        // released must not leave a QueryInterface per submitted list for the rest of the session, and a
        // wrapper drops every frame, so it waits one frame longer.
        if (!explained && ++unexplainedDrops >= 2 && !identityMatch.exchange(true))
            Log("lmxxf: the neural list's pointer was not seen at submission - matching submitted lists by COM identity from now on");
        SetPending(nullptr);
        ++skips;
    }
    // (0.3.4, P3) A dropped list not seen at submission within the window: forgotten (the object may be reused).
    if (droppedList.load(std::memory_order_relaxed) && Lmxxf::FramesLate(frames, droppedFedAt) > Lmxxf::kLateWindow)
        droppedList.store(nullptr, std::memory_order_relaxed);
    if (resized)
    {
        const bool reallocating = w != 0; // not the first allocation: the GPU may still read the old textures
        // The job in flight was fed the old textures. It is ABANDONED, not consumed: recording
        // its decode into this list and then letting PrepareFrame rebuild the runtime's codec for
        // the new size would free the resources this not-yet-submitted list still references (a
        // GPU use-after-free: the crash on every NR-resolution change). AbandonJob waits for the
        // HIP work on the CPU and puts the runtime back to Ready without touching any list; our
        // own textures stay alive in the graveyard until the earlier lists are long done.
        if (inFlight)
        {
            api.AbandonJob(ctx, job.handle);
            inFlight = false;
            netOut = nullptr;
        }
        Bury(fed);
        Bury(composeDelta); // RenoDX mode's delta, at the model's size like fed; remade on its next use (never made in Classic)
        Bury(full);
        Bury(freshEdit);
        Bury(motionScratch);
        Bury(motionFed);
        Bury(lookOut);
        Bury(decodedColour); Bury(encodedOut); // P7.5, remade at the new size on their next use (never made at Auto / Linear)
        Bury(rtgiDepth);                       // P7.7, the same
        Bury(lateBase); Bury(lateEdit);        // P3, remade at the new size on the next grace (never made in a game that submits on time)
        Bury(carryHist); Bury(carryNext); Bury(editFrame); Bury(carryOut); Bury(baseInFlight); Bury(baseOfEdit);
        Bury(lumCur); Bury(lumFed); Bury(lumEdit); Bury(flowCB); Bury(flowBB);
        carryZeroed = false;
        w = f.width; h = f.height; mw = wantW; mh = wantH;
        full = Texture(device.Get(), w, h, kFp16);
        fed = Texture(device.Get(), mw, mh, kFp16);
        freshEdit = Texture(device.Get(), w, h, kFp16);
        lookOut = Texture(device.Get(), w, h, kFp16);
        motionFed = Texture(device.Get(), mw, mh, DXGI_FORMAT_R16G16_FLOAT);
        lastFedFrame = ~0ull;
        allocFrame = frames;
        if (reallocating)
        {
            // Vulkan bridge: the drain's Signal would sit behind this frame's queue Wait on the game's
            // next vkQueueSubmit (2 s lost for nothing); the bridge already waited on the CPU for the
            // previous frame's D3D12 work at the top of this Evaluate, which is what the drain is for.
            if (cfg.lmxxfCpuWait)
            {
                if (!loggedVkDrainSkip)
                {
                    loggedVkDrainSkip = true;
                    Log("lmxxf vk: resolution change - queue drain skipped (the bridge already waited for the previous "
                        "frame's D3D12 work on the CPU; a queue signal now would wait for the game's next submit)");
                }
            }
            else
                DrainQueue();
        }
        temporal->Prepare(w, h);
        const auto cd = f.colour->GetDesc();
        // The memory reading is taken after our own textures were remade and before the runtime
        // rebuilds its chain for the new size (the next PrepareFrame): the runtime's share of this
        // change shows on the next resize or stats line.
        Log("lmxxf: frame " + std::to_string(w) + "x" + std::to_string(h) + " (colour allocation " +
            std::to_string(static_cast<unsigned>(cd.Width)) + "x" + std::to_string(cd.Height) + "), network fed " +
            std::to_string(mw) + "x" + std::to_string(mh) + (mw != w || mh != h ? " (edit lifted to the frame)" : " (pixel-exact)") +
            (fastApplied ? " (Fast mode: one tier lower than the default's " + std::to_string(defaultW) + "x" + std::to_string(defaultH) +
                               " in the " + std::to_string(fastState) + " tier, fed in the " + std::to_string(fastPlan.to) + " tier)"
             : tierPlanned ? TierPlanNote(tierPlan, f.width, f.height, cfg.modelScale)
             : !cfg.lmxxfTierSnap ? std::string()
             : tierSnap.from != tierSnap.to
                 ? " (tier snap: " + std::to_string(askedW) + "x" + std::to_string(askedH) + " lands in the " + std::to_string(tierSnap.from) +
                       " tier nearer the " + std::to_string(tierSnap.to) + " box, fed in the " + std::to_string(tierSnap.to) + " tier)"
             : tierSnap.grown
                 ? " (tier snap: " + std::to_string(askedW) + "x" + std::to_string(askedH) + " grown to fill the " +
                       std::to_string(tierSnap.from) + " tier, the same cost)"
                 : " (tier snap on: kept, the " + std::to_string(tierSnap.from) + " tier)") +
            (f.writeBack ? " | after Ray Regeneration: depth " + std::to_string(f.guideWidth) + "x" + std::to_string(f.guideHeight) +
                               " resampled to the output, result written back into the output" : "") +
            " | " + PoolNote() + " | " + MemoryNote());
        // What the title hands us, once: the formats and conventions every carry decision rests on.
        {
            const auto md = f.motion->GetDesc();
            const auto dd = f.depth->GetDesc();
            std::string line = "lmxxf inputs: colour fmt " + std::to_string(static_cast<int>(cd.Format)) + " state " + std::to_string(static_cast<int>(f.colourState)) +
                               " | motion fmt " + std::to_string(static_cast<int>(md.Format)) + " " + std::to_string(static_cast<unsigned>(md.Width)) + "x" + std::to_string(md.Height) +
                               " active " + std::to_string(f.motionWidth) + "x" + std::to_string(f.motionHeight) + " scale (" + std::to_string(f.motionScaleX) + ", " + std::to_string(f.motionScaleY) + ")" +
                               " | depth fmt " + std::to_string(static_cast<int>(dd.Format)) + (f.depthInverted ? " inverted" : " normal") +
                               " | reactive ";
            if (f.reactive)
            {
                const auto rd = f.reactive->GetDesc();
                line += "fmt " + std::to_string(static_cast<int>(rd.Format)) + " " + std::to_string(static_cast<unsigned>(rd.Width)) + "x" + std::to_string(rd.Height) +
                        " (" + std::to_string(ReactiveChannels(rd.Format)) + " channel(s))";
            }
            else
                line += "none";
            line += std::string(" | exposure ") + (f.exposure ? "texture" : "none") + " pre " + std::to_string(f.preExposure) + " scale " + std::to_string(f.exposureScale) +
                    " | jitter " + std::to_string(f.jitterX) + ", " + std::to_string(f.jitterY);
            Log(line);
        }
    }
    // Full network toggled ([DlssNr] LmxxfFullNetwork): the next PrepareFrame rebuilds the runtime's
    // network and, with it, the codec chain the job in flight would be decoded through. As on a size
    // change (above), that job is ABANDONED, not consumed into this list - the rebuild would leave
    // the list reading freed resources. The carried edit goes on until the rebuilt network's first
    // answer; the network's history restarts with it (no answer since the last feed).
    const bool wantFullNetwork = cfg.lmxxfFullNetwork && !fullNetRefused;
    if (inFlight && ctx && ctx == fullNetCtx && wantFullNetwork != fullNetSent)
    {
        api.AbandonJob(ctx, job.handle);
        inFlight = false;
        netOut = nullptr;
    }
    // (0.3.4, AUTOMASK host step) The character mask controls ([DlssNr] AutoMask, Structure intensity, Character
    // structure; NrControls.h): a change rebuilds the runtime's network at the next PrepareFrame, exactly as a Full
    // network change does, so the job in flight is abandoned the same way. The built-in values (all the defaults) and a
    // runtime that refused the controls send nothing new: the 0.3.3.2 frame.
    const AmdPreSr::NrControls wantCtl =
        ctlRefused ? AmdPreSr::NrControls {} : AmdPreSr::ResolveNrControls(cfg.autoMask, cfg.structure, cfg.skin);
    if (inFlight && ctx && ctx == ctlCtx && wantCtl != ctlSent)
    {
        api.AbandonJob(ctx, job.handle);
        inFlight = false;
        netOut = nullptr;
    }
    // A game that holds the Reset flag up (some titles send it every frame) would erase the edit
    // every frame; a real cut lasts one frame. After 8 frames in a row the flag is ignored.
    gameResetRun = f.reset ? gameResetRun + 1 : 0;
    const bool gameReset = f.reset && gameResetRun <= 8;
    if (gameResetRun == 9 && !loggedStuckReset)
    {
        loggedStuckReset = true;
        Log("lmxxf: the game has asserted Reset for 9 frames in a row; ignoring the flag while it stays up");
    }
    // (0.3.4, P7.5) AmdEncoding sRGB (2) / Gamma 2.2 (3): decode before the copy (step 2), encode the result (step 8c).
    // Upscaler path only: final image mode sets sRGB itself on an 8-bit swapchain (PresentExperimental.h), and lmxxf's
    // no-motion carry keeps its 0.3.3.2 feed there. A change of the conversion restarts the carried edit and the
    // network's history (the edit changes units), as a change of encoding restarts danielblnc's. Auto / Linear: false,
    // nothing is made, dispatched or reset.
    const bool wantEncoding = (cfg.encoding == 2u || cfg.encoding == 3u) && !cfg.lmxxfNoMotion;
    const bool convertEncoding = wantEncoding && EnsureEncoding();
    const unsigned encodingNow = convertEncoding ? cfg.encoding : 0u;
    const bool encodingChanged = encodingNow != encodingFed;
    encodingFed = encodingNow;
    {
        const unsigned encodingState = convertEncoding ? cfg.encoding
                                       : (cfg.encoding == 2u || cfg.encoding == 3u) ? (cfg.lmxxfNoMotion ? 4u : 5u)
                                                                                    : 0u;
        if (encodingState != loggedEncodingState)
        {
            loggedEncodingState = encodingState;
            Log(encodingState == 2u   ? "lmxxf: AmdEncoding sRGB - the game's colour is decoded from sRGB to linear before the network "
                                        "and the carry, and the result is encoded back to sRGB"
                : encodingState == 3u ? "lmxxf: AmdEncoding Gamma 2.2 - the game's colour is decoded (power 2.2) to linear before the "
                                        "network and the carry, and the result is encoded back"
                : encodingState == 4u ? "lmxxf: final image mode - its sRGB / Gamma 2.2 conversion (final image mode picks sRGB itself "
                                        "on an 8-bit swapchain, or AmdEncoding asks for it) is not applied; lmxxf keeps the feed it "
                                        "always had there (the no-motion carry)"
                : encodingState == 5u ? "lmxxf: AmdEncoding sRGB / Gamma 2.2 refused for this session (see above); the colour is used as is"
                                      : "lmxxf: AmdEncoding Auto / Linear - the colour is used as is");
        }
    }
    const bool reset = gameReset || resetRequested.exchange(false) || resized || !primedOnce || encodingChanged;
    resetSinceFeed = resetSinceFeed || reset;
    if (reset) ++statResets;
    if (f.reset) ++statGameResets;

    // The depth every carry decision reads: the title's, or after RR its resample onto the
    // display grid (nearest; the depth is a guide, not a picture).
    ID3D12Resource* depth = f.depth;
    D3D12_RESOURCE_STATES depthState = f.depthState;
    if (f.writeBack && blitDepth)
    {
        const auto dd = f.depth->GetDesc();
        const UINT gw = f.guideWidth ? f.guideWidth : static_cast<UINT>(dd.Width);
        const UINT gh = f.guideHeight ? f.guideHeight : dd.Height;
        if (!depthScratch || depthScratch->GetDesc().Width != w || depthScratch->GetDesc().Height != h)
        {
            Bury(depthScratch);
            depthScratch = Texture(device.Get(), w, h, DXGI_FORMAT_R32_FLOAT);
        }
        ID3D12Resource* srvd[1] = { f.depth };
        const DXGI_FORMAT fmtsd[1] = { DepthViewFormat(dd.Format) };
        Barrier(cmd, f.depth, f.depthState, kSrv);
        Barrier(cmd, depthScratch.Get(), kSrv, kUav);
        blitDepth->Run(cmd, srvd, fmtsd, depthScratch.Get(), DXGI_FORMAT_R32_FLOAT, w, h, 0u,
                       float(gw) / float(w), float(gh) / float(h), float(dd.Width), float(dd.Height));
        Barrier(cmd, depthScratch.Get(), kUav, kSrv);
        Barrier(cmd, f.depth, kSrv, f.depthState);
        depth = depthScratch.Get();
        depthState = kSrv;
    }
    if (depthMuted && blitDepth)
    {
        if (!depthFlat || depthFlat->GetDesc().Width != w || depthFlat->GetDesc().Height != h)
        {
            Bury(depthFlat);
            depthFlat = Texture(device.Get(), w, h, DXGI_FORMAT_R32_FLOAT);
            ID3D12Resource* srvd[1] = { full.Get() }; // unread in mode 1
            const DXGI_FORMAT fmtsd[1] = { kFp16 };
            Barrier(cmd, depthFlat.Get(), kSrv, kUav);
            blitDepth->Run(cmd, srvd, fmtsd, depthFlat.Get(), DXGI_FORMAT_R32_FLOAT, w, h, 1u, 1.f, 1.f, float(w), float(h));
            Barrier(cmd, depthFlat.Get(), kUav, kSrv);
        }
        depth = depthFlat.Get();
        depthState = kSrv;
    }

    // The 0.3.3 colour fix's switches (see HighlightChromaGuardOn), read once per frame and logged
    // at the first frame and on every change, so an on/off comparison can be read from the log.
    const bool chromaGuard = HighlightChromaGuardOn();
    const bool highlightCap = AutoExposureHighlightCapOn();
    if (int(chromaGuard) != loggedChromaGuard || int(highlightCap) != loggedHighlightCap)
    {
        loggedChromaGuard = int(chromaGuard);
        loggedHighlightCap = int(highlightCap);
        Log(std::string("lmxxf: highlight chroma guard ") +
            (chromaGuard ? "on (pixels fed past the codec's shoulder - max channel above 0.75 - keep the game's colour at the "
                           "model's light, fully from 1.5; Classic and RenoDX)"
                         : "off ([DlssNr] AmdLmxxfHighlightChromaGuard=false: the answer's colour is used as is)") +
            " | auto-exposure highlight cap " +
            (highlightCap ? "on (no raise while over 8% of the measured samples are fed past 0.75, down up to x4 per feed "
                            "over 25%; acts only while auto-exposure feeds)"
                          : "off ([DlssNr] AmdLmxxfAutoExposureHighlightCap=false: the mean alone steers it)"));
    }
    // (0.3.4, P7.6) Network output ([DlssNr] AmdNetworkOutput) on lmxxf: the model's answer, carried onto the frame
    // untouched. As danielblnc's Network output skips every pass of ours after its model, every control between
    // the runtime and the output is set to its neutral value or skipped: the runtime's Detail / Colour strengths
    // 1/1 and no output smoothing (the fork's in-runtime blend toward the previous answer); the edit shaper
    // (detail 1, colour 1, edge guard 0), Residual strength 1, Residual limit off (1x with extra passes, as at
    // limit 0), Residual edge fade 0 and the highlight chroma guard off; the carry at Temporal stability 0 (a
    // fresh answer replaces the carried edit; the still-surface relax off); no Image look, no sharpening. The
    // carry itself still runs - it is how an lmxxf answer reaches the frame, one frame late - and so do the
    // interleave fill, the debug views and the exposure the network is fed with. The RenoDX composition stays
    // refused under it, as on danielblnc (its tail is ours). Off, the default: every value is what it was.
    // One line per change (none while it stays off).
    const bool rawNetwork = cfg.networkOutput;
    if (rawNetwork != loggedNetworkOutput)
    {
        loggedNetworkOutput = rawNetwork;
        char buf[480];
        if (rawNetwork)
            std::snprintf(buf, sizeof buf,
                          "lmxxf: network output ON - the model's answer is carried onto the frame untouched: runtime strengths "
                          "1/1, output smoothing off, edit shaper, Residual strength / limit / edge fade and the highlight chroma "
                          "guard neutral, Temporal stability 0, no Image look or sharpening (set: look=%d stability=%.2f "
                          "sharpness=%.2f strength=%.2f limit=%.2f detail/colour %.2f/%.2f interleave=%.1f)",
                          cfg.look.enabled ? 1 : 0, static_cast<double>(cfg.stability), static_cast<double>(cfg.sharpness),
                          static_cast<double>(cfg.residualIntensity), static_cast<double>(cfg.residualLimit),
                          static_cast<double>(cfg.detail), static_cast<double>(cfg.colour), static_cast<double>(cfg.interleave));
        else
            std::snprintf(buf, sizeof buf, "lmxxf: network output off - the controls act on the answer again");
        Log(buf);
    }
    // (0.3.4, P7.8) Residual edge fade ([DlssNr] AmdResidualFade, ResidualShader's `fade`): sent only while the edit
    // is lifted - the network fed at a size other than the frame's - as danielblnc applies it only away from its
    // input size; otherwise 0, which skips the step (the default, 0, never sends anything else). One line when the
    // value or whether it acts changes; none while the key stays at 0.
    const float fadeKey = Unit(cfg.residualFade, 0.f, 0.25f, 0.f);
    const bool editLifted = mw != w || mh != h;
    const float residualFade = editLifted && !rawNetwork ? fadeKey : 0.f;
    {
        const int fadeState = !(fadeKey > 0.f) ? 0 : rawNetwork ? 3 : editLifted ? 1 : 2;
        // A value-only change (a slider moving) is logged at most kFadeValueLines times a session; a change of state always.
        if (fadeState != loggedFadeState || (fadeState != 0 && fadeKey != loggedFadeValue && fadeValueLines < kFadeValueLines))
        {
            if (fadeState == loggedFadeState)
                ++fadeValueLines;
            loggedFadeState = fadeState;
            loggedFadeValue = fadeKey;
            char buf[288];
            if (fadeState == 0)
                std::snprintf(buf, sizeof buf, "lmxxf: residual edge fade off (AmdResidualFade 0)");
            else if (fadeState == 1)
                std::snprintf(buf, sizeof buf,
                              "lmxxf: residual edge fade %.3f - the model's edit rolls off to zero over that share of the frame "
                              "at its border (the network is fed %ux%u, the edit is lifted to %ux%u)",
                              static_cast<double>(fadeKey), mw, mh, w, h);
            else if (fadeState == 3)
                std::snprintf(buf, sizeof buf,
                              "lmxxf: residual edge fade %.3f not used - Network output is on (the answer is shown untouched)",
                              static_cast<double>(fadeKey));
            else
                std::snprintf(buf, sizeof buf,
                              "lmxxf: residual edge fade %.3f not used - the network is fed at the frame's own size (%ux%u, "
                              "pixel-exact); it acts only while the edit is lifted, as on danielblnc away from 100%%",
                              static_cast<double>(fadeKey), w, h);
            Log(buf);
        }
    }
    // The residual's edit shaper, Residual strength and highlight chroma guard for the answer consumed below: the
    // settings (clamped as always), or neutral under Network output (P7.6).
    const float shapeIntensity = rawNetwork ? 1.f : std::clamp(cfg.residualIntensity, 0.f, 2.f);
    const float shapeDetail = rawNetwork ? 1.f : std::clamp(cfg.lmxxfEditDetail, 0.f, 2.f);
    const float shapeSaturation = rawNetwork ? 1.f : std::clamp(cfg.lmxxfEditSaturation, 0.f, 2.f);
    const float shapeEdgeGuard = rawNetwork ? 0.f : std::clamp(cfg.lmxxfEdgeGuard, 0.f, 1.f);
    const bool shapeChromaGuard = chromaGuard && !rawNetwork;

    // 1. Frame N-1's answer becomes the model's fresh edit, which the temporal pass smooths
    //    against the carried one and carries onto this frame.
    bool fresh = false;
    // No-motion mode: the textures it carries with, allocated on demand and zeroed once.
    const bool noMotion = cfg.lmxxfNoMotion && lumaDown && blockMatch && editWarp;
    // EDIT ACCUMULATION (interleave preset 10; the temporal pass runs it as 11 - TemporalStability.h
    // has the full note): this frame's raw + the carried edit on every frame, validated raw against
    // raw, each answer spread over two frames, the stored edit never multiplied by `keep`. With
    // interleave off, or in no-motion mode (its own carry), the classic carry (9) runs as before.
    const bool accumulate = cfg.interleave >= 2.f && cfg.interleavePreset == 10 && !noMotion;
    if (int(accumulate) != loggedFillMode)
    {
        loggedFillMode = int(accumulate);
        Log(accumulate ? "lmxxf interleave fill: Edit accumulation (preset 11) - raw + carried edit every frame, raw-against-raw "
                         "test, answer spread over two frames, no keep compounding, edge guard on the fed frame's depth, "
                         "no restart on a switch from or to Classic carry"
                       : "lmxxf interleave fill: Classic carry (preset 9)");
    }
    statAccumulating = accumulate;
    statCadence = cfg.interleave >= 2.f ? static_cast<UINT>(cfg.interleave) : 1u;
    // The mode's parameters: EMA weight of the carried edit, the matcher's tolerance (sqrt-luma
    // units per tap) and the bound on the edit - the same ones the reprojecting path uses.
    // (0.3.4, P7.6) Network output: no EMA and no Residual limit here either (1x with extra passes, as at limit 0).
    const float nmKeep = rawNetwork ? 0.f : std::clamp(cfg.stability, 0.f, 0.95f);
    const float nmThr = std::isfinite(cfg.stabilityThreshold) ? std::clamp(cfg.stabilityThreshold, 0.01f, 0.5f) : 0.04f;
    const float nmCap = cfg.residualLimit > 0.f && !rawNetwork ? std::clamp(cfg.residualLimit, 0.f, 2.f)
                                                               : (std::clamp(cfg.passes, 1u, 3u) > 1 ? 1.f : 0.f);
    const DXGI_FORMAT kLum = DXGI_FORMAT_R16_FLOAT;
    if (noMotion && (!carryHist || carryHist->GetDesc().Width != w || carryHist->GetDesc().Height != h))
    {
        Bury(carryHist); Bury(carryNext); Bury(editFrame); Bury(carryOut); Bury(baseInFlight); Bury(baseOfEdit);
        Bury(lumCur); Bury(lumFed); Bury(lumEdit); Bury(flowCB); Bury(flowBB);
        qw = (w + 3u) / 4u; qh = (h + 3u) / 4u;
        carryHist = Texture(device.Get(), w, h, kFp16);
        carryNext = Texture(device.Get(), w, h, kFp16);
        editFrame = Texture(device.Get(), w, h, kFp16);
        carryOut = Texture(device.Get(), w, h, kFp16);
        baseInFlight = Texture(device.Get(), w, h, kFp16);
        baseOfEdit = Texture(device.Get(), w, h, kFp16);
        lumCur = Texture(device.Get(), qw, qh, kLum);
        lumFed = Texture(device.Get(), qw, qh, kLum);
        lumEdit = Texture(device.Get(), qw, qh, kLum);
        flowCB = Texture(device.Get(), qw, qh, kFp16);
        flowBB = Texture(device.Get(), qw, qh, kFp16);
        carryZeroed = false;
        Log("lmxxf: no-motion carry (final image mode): the answer is consumed when the runtime reports it done; "
            "the motion is estimated by block matching at " + std::to_string(qw) + "x" + std::to_string(qh) +
            " (+-16 px) and the edit is carried with it, faded where nothing matches; EMA " + std::to_string(nmKeep) +
            ", match tolerance " + std::to_string(nmThr) + ", feed rest " + std::to_string(cfg.interleave >= 2.f ? unsigned(cfg.interleave) : 1u) + " frame(s)");
    }
    if (noMotion && !carryZeroed)
    {
        Zero(cmd, carryHist.Get());
        carryZeroed = true;
    }
    // No-motion mode: is the answer there? If not, keep carrying and do not feed (one job at a
    // time); the frame never waits for the network. RecordOutputs would otherwise put a queue
    // wait on the HIP fence and the whole frame - at Stray's 41 fps, every frame - behind it.
    bool answerPending = false;
    if (inFlight && noMotion && api.OutputReady)
    {
        uint32_t ready = 1u;
        if (api.OutputReady(ctx, job.handle, &ready) == LMXXF_NR_OK && ready == 0u)
            answerPending = true;
    }
    // Vulkan-on-D3D12 bridge (cfg.lmxxfCpuWait): the answer is waited for HERE, on the CPU, so
    // RecordOutputs' deferred queue wait finds the HIP fence already signalled. That bridge records
    // and executes its D3D12 list inside the game's Evaluate, after a queue Wait on a fence that
    // only the game's next vkQueueSubmit signals, and at the next Evaluate waits on the CPU for that
    // frame's D3D12 work. What froze Indiana Jones and the Great Circle right after the first job
    // (lmxxf_backend.log ends at "first frame prepared") was the runtime's lazy weight upload: a
    // hipStreamSynchronize inside EnqueueHipAsync, behind that Wait. The runtime now warms the
    // network before any such wait (LMXXF_NR_FRAME_FLAG_VULKAN_BRIDGE), and this wait is acyclic:
    // the job's input signal was queued before the frame fence the bridge retired before this
    // Record. The frame pays the network's time, as under danielblnc's inline mode; Model
    // interleave halves it. Past the bound the backend stops with a line; Shutdown skips the
    // runtime's drain over this bridge.
    vkStageLog = cfg.lmxxfCpuWait;
    if (cfg.lmxxfCpuWait)
        vkBridge = true;
    if (inFlight && !noMotion && cfg.lmxxfCpuWait && api.OutputReady)
    {
        if (vkJobsLogged < 3)
            Log("lmxxf vk: consume - waiting on the CPU for the HIP fence of the job in flight");
        const auto tw = std::chrono::steady_clock::now();
        uint32_t ready = 1u;
        while (api.OutputReady(ctx, job.handle, &ready) == LMXXF_NR_OK && ready == 0u)
        {
            const double waited = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - tw).count();
            if (waited > 1000.0)
            {
                answerLost = true;
                throw std::runtime_error("over the Vulkan bridge the network's answer did not arrive within 1000 ms (the HIP "
                                         "fence never signalled); the pass is off for this session, the game goes on");
            }
            Sleep(0);
        }
        const double waited = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - tw).count();
        if (waited > cpuWaitMax) cpuWaitMax = waited;
        if (!loggedCpuWait || vkJobsLogged < 3)
        {
            loggedCpuWait = true;
            ++vkJobsLogged;
            Log("lmxxf: Vulkan bridge - answer " + std::to_string(vkJobsLogged) + " arrived after " + std::to_string(waited).substr(0, 6) +
                " ms of CPU wait (the frame waits for the network here, bounded at 1000 ms)");
        }
    }
    if (inFlight && !answerPending)
    {
        const int32_t rc = api.RecordOutputs(ctx, job.handle, cmd);
        // (0.3.3.2) read before Retire, which sets the runtime's LastError again
        const std::string outErr = rc != LMXXF_NR_OK ? LastError() : std::string();
        api.Retire(ctx, job.handle);
        inFlight = false;
        // (0.3.4, P3) A graced job (its list reached ExecuteCommandLists after the next Record): its answer is for the
        // frame before the previous one. The edit is measured against that frame's copy (lateBase) into lateEdit and
        // warped into the previous frame's space below, where the carry expects a fresh edit; not used after a reset
        // since its feed. No-motion mode needs neither: its carry refers an answer of any age to the frame it was fed
        // (baseInFlight) and matches it by block matching.
        const bool lateAnswer = jobGraced && !noMotion;
        const bool lateUsable = !lateAnswer || (lateBase && lateEdit && lateWarp && Lmxxf::UseLateAnswer(resetSinceFeed));
        jobGraced = false;
        // Vulkan bridge: the first answer is in (after the CPU wait above, or in no-motion mode
        // once OutputReady said so) - this session did not freeze; the launch marker goes.
        if (vkMarkerLive && rc == LMXXF_NR_OK)
            ClearVkMarker();
        if (rc != LMXXF_NR_OK)
        {
            Log("lmxxf RecordOutputs failed: " + outErr);
            NoteFatal(outErr);
        }
        else if (!reset && netOut && lateUsable)
        {
            // `full` still holds the RAW frame the answer is for: it is refreshed in step 2. The
            // edit shaper (detail / colour / edge guard) runs here, on the fresh edit. Its edge
            // guard reads this frame's depth under Classic carry (9) - the frame the edit is about
            // to be carried onto - and frame N-1's under Edit accumulation (11), see below.
            // No-motion mode: the answer may be several frames old, so the residual is measured
            // against the copy of the frame it was fed (`full` is only that frame when the job was
            // fed on the previous one).
            ID3D12Resource* rawForEdit = noMotion && baseInFlight ? baseInFlight.Get() : lateAnswer ? lateBase.Get() : full.Get();
            // (0.3.4, P3) Where the residual writes: freshEdit, or for a late answer lateEdit (warped into freshEdit below).
            ID3D12Resource* const editDst = lateAnswer ? lateEdit.Get() : freshEdit.Get();
            // The edge guard's depth. The edit is in frame N-1's pixel space - `full` is still that
            // frame's raw - so under Edit accumulation the guard reads frame N-1's depth, which the
            // temporal pass kept (R32_FLOAT, already in kSrv between its Runs): this frame's depth
            // put the guard's edges where the geometry is now, a frame away from the edit's own.
            // Classic carry keeps this frame's depth, so the A/B between the two stays clean.
            const bool edgeDepthHist = accumulate && temporal && temporal->Primed();
            ID3D12Resource* edgeDepth = edgeDepthHist ? temporal->DepthHistory() : depth;
            const DXGI_FORMAT edgeDepthFmt = edgeDepthHist ? DXGI_FORMAT_R32_FLOAT : DepthViewFormat(depth->GetDesc().Format);
            if (jobCompose && composePass && residualDelta && composeDelta)
            {
                // RENODX COLOUR COMPOSITION (AmdComposition 1; this job was fed in that mode, so the
                // runtime decoded it at strengths 1/1 and netOut is RenoDX's `upgraded` in fed units).
                // Composed in frame N-1's space at the model's size: `fed` still holds what this job
                // was fed and expoJob its exposure (both are rewritten in step 6, after this), so
                // W = 1 exactly - the runtime's paper white - whatever set the exposure (the title's
                // texture, auto-exposure, a self-heal). composeDelta = Tail(fed, answer) - fed, with
                // the non-finite pixels flagged; the residual's delta variant lifts it to the frame
                // as delta / e (the matched lift) and runs the edit shaper and Residual strength on
                // it exactly as on Classic's edit. The guard binds the model's answer against fed(N-1)
                // before those controls; preset 9 then carries the edit onto this frame, bounded by
                // Residual limit as today. One frame late, as every lmxxf edit is.
                //
                // CREDITS (NrCompose.h has the full list). The two-branch luminance ratio and the hue
                // step that make `upgraded` are RenoDX's UpgradeToneMap (clshortfuse, MIT,
                // https://github.com/clshortfuse/renodx), computed here by lmxxf's codec against the
                // proxy the network saw - lmxxf's (MIT) reconstruction of NVIDIA's decode, whose
                // luminance-only / full-colour blend is the Colour strength blend of the tail. The
                // tail is dlssnr.hlsl's (OptiScaler -> Dagherbou -> wilsjo2 lineage; wilsjo2's
                // SkinColourWeight and skin / environment edit), RenoDX's neutral-axis compression
                // and Bjorn Ottosson's OkLab. AMDNR (3zwr1) only wires it: the 1/1 feed, the per-job
                // latch, W in fed units and this matched lift.
                {
                    const float cd = Unit(cfg.composeDetail, 0.f, 2.f, 1.f), cc = Unit(cfg.composeColour, 0.f, 4.f, 1.f),
                                cg = Unit(cfg.maxRatio, 1.f, 8.f, 2.f);
                    const int cs = cfg.skinProtection ? 1 : 0, ch = chromaGuard ? 1 : 0;
                    if ((cd != loggedComposeDetail || cc != loggedComposeColour || cg != loggedComposeGuard ||
                         cs != loggedComposeSkin || ch != loggedComposeChroma) && composeValueLines < kComposeValueLines)
                    {
                        loggedComposeDetail = cd;
                        loggedComposeColour = cc;
                        loggedComposeGuard = cg;
                        loggedComposeSkin = cs;
                        loggedComposeChroma = ch;
                        char buf[320];
                        std::snprintf(buf, sizeof buf,
                                      "lmxxf: RenoDX composition values - detail %.2f, colour %.2f, highlight guard %.1fx, skin / "
                                      "environment edit %s, highlight chroma guard %s (on the answer before the composition)%s",
                                      cd, cc, cg, cs ? "on" : "off", ch ? "on" : "off",
                                      ++composeValueLines == kComposeValueLines ? " - last such line this session" : "");
                        Log(buf);
                    }
                }
                ID3D12Resource* srvc[2] = { netOut, fed.Get() };
                const DXGI_FORMAT fmtsc[2] = { kFp16, kFp16 };
                Barrier(cmd, composeDelta.Get(), kSrv, kUav);
                composePass->Run(cmd, srvc, fmtsc, composeDelta.Get(), kFp16, mw, mh, cfg.skinProtection ? 1u : 0u,
                                 Unit(cfg.composeColour, 0.f, 4.f, 1.f), Unit(cfg.maxRatio, 1.f, 8.f, 2.f),
                                 Unit(cfg.skinDetail, 0.f, 1.f, 1.f), Unit(cfg.skinColour, 0.f, 1.f, 1.f), true,
                                 Unit(cfg.composeDetail, 0.f, 2.f, 1.f), Unit(cfg.envDetail, 0.f, 1.f, 1.f),
                                 Unit(cfg.envColour, 0.f, 1.f, 1.f), /*white: fed units*/ 1.f,
                                 /*highlight chroma guard, on the answer before the tail*/ chromaGuard ? 1.f : 0.f);
                Barrier(cmd, composeDelta.Get(), kUav, kSrv);
                ID3D12Resource* srv[5] = { composeDelta.Get(), rawForEdit, expoJob.Get(), edgeDepth, netOut };
                const DXGI_FORMAT fmts[5] = { kFp16, kFp16, DXGI_FORMAT_R32_FLOAT, edgeDepthFmt, kFp16 };
                if (!edgeDepthHist) Barrier(cmd, depth, depthState, kSrv);
                Barrier(cmd, editDst, kSrv, kUav);
                residualDelta->Run(cmd, srv, fmts, editDst, kFp16, w, h, 0u, shapeIntensity, shapeDetail, shapeSaturation,
                                   shapeEdgeGuard, true, /*fade (P7.8)*/ residualFade, /*chromaGuard*/ shapeChromaGuard ? 1.f : 0.f);
                Barrier(cmd, editDst, kUav, kSrv);
                if (!edgeDepthHist) Barrier(cmd, depth, kSrv, depthState);
                whiteSource = AmdPreSr::kCompositionWhiteFed;
                ++statComposed;
            }
            else
            {
                // Classic: the runtime composed at Detail / Colour strength; the edit is its answer
                // over the exposure minus the raw frame (where the feed passed the codec's shoulder,
                // the highlight chroma guard first gives that answer the raw frame's colour).
                ID3D12Resource* srv[4] = { netOut, rawForEdit, expoJob.Get(), edgeDepth };
                const DXGI_FORMAT fmts[4] = { kFp16, kFp16, DXGI_FORMAT_R32_FLOAT, edgeDepthFmt };
                if (!edgeDepthHist) Barrier(cmd, depth, depthState, kSrv);
                Barrier(cmd, editDst, kSrv, kUav);
                residual->Run(cmd, srv, fmts, editDst, kFp16, w, h, 0u, shapeIntensity, shapeDetail, shapeSaturation,
                              shapeEdgeGuard, true, /*fade (P7.8)*/ residualFade, /*chromaGuard*/ shapeChromaGuard ? 1.f : 0.f);
                Barrier(cmd, editDst, kUav, kSrv);
                if (!edgeDepthHist) Barrier(cmd, depth, kSrv, depthState);
                whiteSource = AmdPreSr::kCompositionWhiteNone;
            }
            if (lateAnswer)
            {
                // (0.3.4, P3) The late answer's edit, in the space of the frame it was fed, brought into the previous
                // frame's by that frame's vectors (the temporal pass's motion history, not flipped yet this frame) and
                // the grace frame's jitter change; the carry then takes it on as any fresh edit.
                ID3D12Resource* srvl[2] = { lateEdit.Get(), temporal->MotionHistory() };
                const DXGI_FORMAT fmtsl[2] = { kFp16, DXGI_FORMAT_R16G16_FLOAT };
                Barrier(cmd, freshEdit.Get(), kSrv, kUav);
                lateWarp->Run(cmd, srvl, fmtsl, freshEdit.Get(), kFp16, w, h, 0u, lateJitX, lateJitY);
                Barrier(cmd, freshEdit.Get(), kUav, kSrv);
            }
            fresh = true;
            answerSinceFeed = true;
            ++statFresh;
            framesSinceFresh = 0;
            if (noMotion)
            {
                // The fresh edit belongs to the frame that was in flight. The old edit is brought
                // into that frame's space through the flow between the two fed frames and blended
                // in where it matches; then the in-flight copies become the edit's reference.
                ID3D12Resource* srvm[2] = { lumFed.Get(), lumEdit.Get() };
                const DXGI_FORMAT fmtsm[2] = { kLum, kLum };
                Barrier(cmd, flowBB.Get(), kSrv, kUav);
                blockMatch->Run(cmd, srvm, fmtsm, flowBB.Get(), kFp16, qw, qh, 0u, nmThr, 0.25f * nmThr);
                Barrier(cmd, flowBB.Get(), kUav, kSrv);
                ID3D12Resource* srvw[3] = { carryHist.Get(), flowBB.Get(), freshEdit.Get() };
                const DXGI_FORMAT fmtsw[3] = { kFp16, kFp16, kFp16 };
                Barrier(cmd, carryNext.Get(), kSrv, kUav);
                editWarp->Run(cmd, srvw, fmtsw, carryNext.Get(), kFp16, w, h, 1u, float(qw), float(qh), nmCap, 0.f, true, nmKeep);
                Barrier(cmd, carryNext.Get(), kUav, kSrv);
                std::swap(carryHist, carryNext);
                std::swap(lumEdit, lumFed);
                std::swap(baseOfEdit, baseInFlight);
            }
        }
    }
    // Sessions of queues the neural list left (queue migration, see Submitted) are destroyed here,
    // before this frame's new work, once both of their fences are reached - and only while no job of
    // the current session is waiting to be consumed: the old network's teardown frees HIP memory,
    // which can synchronize the whole device, and over the Vulkan bridge that is bounded only once
    // the current job's answer was taken above. Nothing to do (one empty() test) without a migration.
    if (!retired.empty() && !inFlight)
        ReleaseRetired();
    // The model has been silent for a while (idle, failing): the last edit is stale, drop it
    // rather than carry it forever. In no-motion mode an answer is legitimately several frames
    // apart (the network runs at its own pace), so the limit is wider there.
    const UINT nmRest = cfg.interleave >= 2.f ? static_cast<UINT>(cfg.interleave) : 1u;
    if (!fresh && ++framesSinceFresh == (noMotion ? (std::max)(30u, 4u * nmRest + 16u) : 9u))
        Zero(cmd, noMotion ? carryHist.Get() : temporal->ResidualHistory());
    if (reset)
    {
        Zero(cmd, temporal->ResidualHistory());
        Zero(cmd, temporal->ResidualNext());
        temporal->ResetHistory();
        if (noMotion)
            Zero(cmd, carryHist.Get());
    }

    // 2. This frame's colour, FP16, at render size in the game's units: pixel-exact from the
    //    render area of the game's allocation.
    if (convertEncoding)
    {
        // (0.3.4, P7.5) The render area decoded to linear (the pass reads texel p of the allocation for p inside w x h,
        // the area the blit below reads pixel-exact), through a view of the raw channel values: an _SRGB view would
        // decode in hardware first (ColorEncoding.h does the same). Then the usual copy, from the decoded texture.
        const auto cd = f.colour->GetDesc();
        DXGI_FORMAT rawView = ColourViewFormat(cd.Format);
        if (rawView == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB)
            rawView = DXGI_FORMAT_R8G8B8A8_UNORM;
        if (rawView == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB)
            rawView = DXGI_FORMAT_B8G8R8A8_UNORM;
        const UINT ec[4] = { w, h, cfg.encoding, 0u }; // EncodingShader: w, h, mode, inverse (0 = decode)
        ID3D12Resource* srvE[1] = { f.colour };
        const DXGI_FORMAT fmtsE[1] = { rawView };
        Barrier(cmd, f.colour, f.colourState, kSrv);
        Barrier(cmd, decodedColour.Get(), kSrv, kUav);
        encodingPass->RunConstants(cmd, srvE, fmtsE, decodedColour.Get(), kFp16, w, h, ec, 4u);
        Barrier(cmd, decodedColour.Get(), kUav, kSrv);
        Barrier(cmd, f.colour, kSrv, f.colourState);
        ID3D12Resource* srv[3] = { decodedColour.Get(), expoJob.Get(), decodedColour.Get() };
        const DXGI_FORMAT fmts[3] = { kFp16, DXGI_FORMAT_R32_FLOAT, kFp16 };
        Barrier(cmd, full.Get(), kSrv, kUav);
        blitColour->Run(cmd, srv, fmts, full.Get(), kFp16, w, h, 0u, 1.f, 1.f, float(w), float(h));
        Barrier(cmd, full.Get(), kUav, kSrv);
    }
    else
    {
        const auto cd = f.colour->GetDesc();
        ID3D12Resource* srv[3] = { f.colour, expoJob.Get(), f.colour };
        const DXGI_FORMAT fmts[3] = { ColourViewFormat(cd.Format), DXGI_FORMAT_R32_FLOAT, ColourViewFormat(cd.Format) };
        Barrier(cmd, f.colour, f.colourState, kSrv);
        Barrier(cmd, full.Get(), kSrv, kUav);
        blitColour->Run(cmd, srv, fmts, full.Get(), kFp16, w, h, 0u, 1.f, 1.f, float(cd.Width), float(cd.Height));
        Barrier(cmd, full.Get(), kUav, kSrv);
        Barrier(cmd, f.colour, kSrv, f.colourState);
    }

    // 3. Motion at render resolution, as the temporal pass reads it: pixel-exact from the
    //    active extent when the vectors are display-sized.
    ID3D12Resource* motion = f.motion;
    D3D12_RESOURCE_STATES motionState = f.motionState;
    float sx = f.motionScaleX, sy = f.motionScaleY;
    const UINT mvW = f.motionWidth ? f.motionWidth : w, mvH = f.motionHeight ? f.motionHeight : h;
    if (mvW != w || mvH != h)
    {
        if (!motionScratch)
            motionScratch = Texture(device.Get(), w, h, DXGI_FORMAT_R16G16_FLOAT);
        const auto md = f.motion->GetDesc();
        ID3D12Resource* srv[3] = { f.motion, expoJob.Get(), f.motion };
        const DXGI_FORMAT fmts[3] = { MotionViewFormat(md.Format), DXGI_FORMAT_R32_FLOAT, MotionViewFormat(md.Format) };
        Barrier(cmd, f.motion, f.motionState, kSrv);
        Barrier(cmd, motionScratch.Get(), kSrv, kUav);
        blitMotion->Run(cmd, srv, fmts, motionScratch.Get(), DXGI_FORMAT_R16G16_FLOAT, w, h, 0u,
                        float(mvW) / float(w), float(mvH) / float(h), float(md.Width), float(md.Height));
        Barrier(cmd, motionScratch.Get(), kUav, kSrv);
        Barrier(cmd, f.motion, kSrv, f.motionState);
        sx *= float(w) / float(mvW);
        sy *= float(h) / float(mvH);
        motion = motionScratch.Get();
        motionState = kSrv;
    }
    // The scale the carry and the network history use: zero when the title's vectors were
    // judged unusable (self-healing) - the edit is then carried in place, one frame late.
    const float csx = motionMuted ? 0.f : sx, csy = motionMuted ? 0.f : sy;

    // 4. Jitter delta, the same convention as the danielblnc path (see AmdPreSr.cpp).
    float jitterDx = 0.f, jitterDy = 0.f;
    if (cfg.jitterSign != 0 && haveJitter && !reset && !f.writeBack)
    {
        jitterDx = float(cfg.jitterSign) * (lastJitterX - f.jitterX);
        jitterDy = float(cfg.jitterSign) * (lastJitterY - f.jitterY);
        if (!std::isfinite(jitterDx) || !std::isfinite(jitterDy) || std::fabs(jitterDx) > 4.f || std::fabs(jitterDy) > 4.f)
            jitterDx = jitterDy = 0.f;
    }
    lastJitterX = f.jitterX; lastJitterY = f.jitterY; haveJitter = true;
    if (graceFill)
    {
        // (0.3.4, P3) The grid change from the late job's frame to this one, for its answer's warp next frame.
        lateJitX = jitterDx;
        lateJitY = jitterDy;
    }

    // 5. Carry the edit onto this frame (TemporalStability.h, preset 9; preset 11 under Edit
    //    accumulation - same inputs, same alpha, cap, cadence and drop). Temporal stability is
    //    the weight of the carried edit against a fresh one; Residual limit bounds the edit.
    //    alpha 0 would be the pass's own bypass, so the floor keeps the carry running. A switch
    //    between 9 and 11 does NOT restart the pass: both store the same linear edit, so 11 -> 9
    //    goes straight on and 9 -> 11 trusts the geometry alone for the one frame the raw
    //    history 9 never wrote is missing (TemporalStability::Run, reset constant 2).
    //    (0.3.4, P7.6) Network output: Temporal stability 0 (the floor), so a fresh answer replaces the carried edit.
    const float alpha = rawNetwork ? 0.001f : (std::max)(std::clamp(cfg.stability, 0.f, 1.f), 0.001f);
    // With extra passes the network is fed its own edit and answers with more of it, a loop
    // that only the bound closes; so "Residual limit = 0 (off)" still bounds at 1x under them.
    const UINT passesForCap = std::clamp(cfg.passes, 1u, 3u);
    // (0.3.4, P7.6) Network output: Residual limit off, i.e. what limit 0 gives (the 1x bound under extra passes stays).
    const float residualCap = cfg.residualLimit > 0.f && !rawNetwork ? std::clamp(cfg.residualLimit, 0.f, 2.f) : (passesForCap > 1 ? 1.f : 0.f);
    // #5 still-surface steadiness, on the classic carry's agreement clamp (preset 9). Off while the self-heal zeroes the
    // vectors or flattens depth (every pixel would read as still), at stability 0 (the carry still runs at alpha 0.001
    // and must stay identical) and under Edit accumulation (preset 11 keeps its own rules; its pipeline never reads it).
    // (0.3.4, P7.6) And off under Network output (4), where the carry runs at the floor as at stability 0.
    const int staticState = accumulate ? 3 : (motionMuted || depthMuted) ? 2 : rawNetwork ? 4 : !(cfg.stability > 0.f) ? 1 : 0;
    const float staticRelax = staticState == 0 ? cfg.stabilityStaticRelax : 0.f;
    if (!noMotion && (cfg.stabilityStaticRelax != loggedStaticRelax || staticState != loggedStaticRelaxState))
    {
        loggedStaticRelax = cfg.stabilityStaticRelax;
        loggedStaticRelaxState = staticState;
        char buf[160];
        std::snprintf(buf, sizeof buf, "lmxxf: still-surface steadiness %.2f (%s)%s", static_cast<double>(cfg.stabilityStaticRelax),
                      staticState == 3   ? "not used under Edit accumulation"
                      : staticState == 2 ? "off (self-heal)"
                      : staticState == 1 ? "off (Temporal stability 0)"
                      : staticState == 4 ? "off (Network output)"
                                         : "carried-edit clamp",
                      cfg.stabilityStaticDebug && staticState != 3 ? ", overlay on" : "");
        Log(buf);
    }
    ID3D12Resource* out = nullptr;
    if (noMotion)
    {
        // This frame's quarter luma, the flow from it to the edit's frame, the edit brought
        // through that flow (faded where nothing matches), and the frame plus that edit.
        {
            ID3D12Resource* srvl[1] = { full.Get() };
            const DXGI_FORMAT fmtsl[1] = { kFp16 };
            Barrier(cmd, lumCur.Get(), kSrv, kUav);
            lumaDown->Run(cmd, srvl, fmtsl, lumCur.Get(), kLum, qw, qh, 0u, float(w), float(h));
            Barrier(cmd, lumCur.Get(), kUav, kSrv);
        }
        {
            ID3D12Resource* srvm[2] = { lumCur.Get(), lumEdit.Get() };
            const DXGI_FORMAT fmtsm[2] = { kLum, kLum };
            Barrier(cmd, flowCB.Get(), kSrv, kUav);
            blockMatch->Run(cmd, srvm, fmtsm, flowCB.Get(), kFp16, qw, qh, 0u, nmThr, 0.25f * nmThr);
            Barrier(cmd, flowCB.Get(), kUav, kSrv);
        }
        {
            ID3D12Resource* srvw[3] = { carryHist.Get(), flowCB.Get(), freshEdit.Get() };
            const DXGI_FORMAT fmtsw[3] = { kFp16, kFp16, kFp16 };
            Barrier(cmd, editFrame.Get(), kSrv, kUav);
            editWarp->Run(cmd, srvw, fmtsw, editFrame.Get(), kFp16, w, h, 0u, float(qw), float(qh), nmCap, 0.f, true, nmKeep);
            Barrier(cmd, editFrame.Get(), kUav, kSrv);
        }
        ID3D12Resource* srvo[3] = { full.Get(), expoJob.Get(), editFrame.Get() };
        const DXGI_FORMAT fmtso[3] = { kFp16, DXGI_FORMAT_R32_FLOAT, kFp16 };
        Barrier(cmd, carryOut.Get(), kSrv, kUav);
        blitColour->Run(cmd, srvo, fmtso, carryOut.Get(), kFp16, w, h, 2u, 1.f, 1.f, float(w), float(h), true, 1.f);
        Barrier(cmd, carryOut.Get(), kUav, kSrv);
        out = carryOut.Get();
    }
    else
    out = temporal->Run(
        cmd, full.Get(), kSrv, motion, motionState, depth, depthState, w, h, csx, csy,
        alpha, reset, /*ghostReject*/ 1.f, /*mode*/ 0u, /*threshold*/ 0.08f, /*interleaved*/ 1.f,
        /*detailScale*/ 1.f, /*modelFrame*/ 0.f, /*sharpFill*/ 0.f, float(cfg.interleaveDebug),
        /*extraTemporal: 11 Edit accumulation, 9 classic carry*/ accumulate ? 11.f : 9.f,
        /*baseline*/ freshEdit.Get(), /*residualTemporal*/ fresh ? 1.f : 0.f,
        residualCap,
        f.depthInverted ? 1.f : 0.f, /*heldGhostBound*/ 0.f, f.reactive, jitterDx, jitterDy, /*sharpGain*/ 0.f,
        staticRelax, (cfg.stabilityStaticDebug && !accumulate) ? 1.f : 0.f);
    // Debug view 5 (lmxxf): the model's fresh edit added to this frame directly, no reprojection,
    // no carry. One frame late and unwarped, so it smears in motion - but it shows the edit
    // whatever the reprojection thinks. If this view shows an effect and the normal view does
    // not, the carry (motion vectors, depth, reactive mask) is refusing the edit in this title.
    if (cfg.interleaveDebug == 5u && lookOut && out)
    {
        ID3D12Resource* srvd[3] = { full.Get(), expoJob.Get(), freshEdit.Get() };
        const DXGI_FORMAT fmtsd[3] = { kFp16, DXGI_FORMAT_R32_FLOAT, kFp16 };
        Barrier(cmd, lookOut.Get(), kSrv, kUav);
        blitColour->Run(cmd, srvd, fmtsd, lookOut.Get(), kFp16, w, h, 2u, 1.f, 1.f, float(w), float(h), true, 1.f);
        Barrier(cmd, lookOut.Get(), kUav, kSrv);
        out = lookOut.Get();
    }

    // 6. Feed the network: every frame, or every Nth under Model interleave. The copy it gets
    //    is at the model's size, times the exposure; the exposure texel is kept beside the job
    //    so the edit can be brought back into the game's units next frame.
    //
    //    "Neural passes": real launches. The runtime runs the network P times on its HIP stream,
    //    each extra pass fed the previous answer converted back to the input layout by one
    //    small kernel - the danielblnc runtime's sequential layers, at P times the model cost.
    //    The residual is measured against the raw frame, so the whole compounded edit comes
    //    back and Residual strength / limit bound it like any other.
    const UINT cadence = cfg.interleave >= 2.f ? static_cast<UINT>(cfg.interleave) : 1u;
    const UINT passes = std::clamp(cfg.passes, 1u, 3u);
    // No-motion mode: one job at a time, and at least `cadence` frames between two feeds (Model
    // interleave = the network's rest, i.e. how much of the GPU it may take beside the game).
    const bool restDone = lastFedFrame == ~0ull || frames - lastFedFrame >= cadence;
    const bool cadenceModel = answerPending ? false : noMotion ? (!inFlight && restDone) : (modelCounter++ % cadence) == 0u;
    // (0.3.4, P3) A grace frame feeds nothing: the runtime still holds the late job (one job at a time).
    const bool wantModel = cadenceModel && !graceFill;
    // Colour composition for a job fed now ([DlssNr] AmdComposition; decided every frame, so the
    // log line and the menu note follow the setting). Classic: false, and nothing below changes.
    // RenoDX: the runtime is asked for strengths 1/1 - at T = C = 1 its decode returns RenoDX's
    // `upgraded` unchanged (native_codec_decode.hlsl), with the ratio taken against the runtime's
    // own proxy of fed, exactly what the network saw - and the host composes it at consume. The
    // strengths are per job and act only in the decode; the network's history is taken before the
    // decode, so the mode never disturbs the temporal loop.
    const bool composeFeed = ComposeMode(f, cfg, noMotion);
    if (wantModel && ctx && out)
    {
        {
            const bool haveExposure = f.exposure != nullptr;
            const float pre = std::isfinite(f.preExposure) && f.preExposure > 0.f ? f.preExposure : 1.f;
            const float scale = std::isfinite(f.exposureScale) && f.exposureScale > 0.f ? f.exposureScale : 1.f;
            // (0.3.4, EXPO-PARITY) [DlssNr] AmdUseGameExposure, shared with danielblnc: 0 ignores the title's exposure
            // texture and the network is fed as for a title that publishes none (auto-exposure, or exposure 1 with
            // AmdLmxxfAutoExposure off); -1 (auto, the default) and 1 use it, exactly as before.
            const bool useTitleExposure = cfg.useGameExposure != 0;
            if (!loggedExposure)
            {
                loggedExposure = true;
                Log(haveExposure && useTitleExposure
                        ? "lmxxf: exposure from the title's exposure texture (x scale " + std::to_string(scale) +
                              " / pre-exposure " + std::to_string(pre) + ")"
                    : haveExposure
                        ? std::string("lmxxf: the title's exposure texture is ignored (AmdUseGameExposure=0); ") +
                              (cfg.lmxxfAutoExposure ? "auto-exposure sets the network's operating point"
                                                     : "the network is fed the colour as is (AmdLmxxfAutoExposure is off)")
                    : cfg.lmxxfAutoExposure
                        ? "lmxxf: the title publishes no exposure texture; auto-exposure sets the network's operating point "
                          "(the encoded picture's mean toward 0.5, as danielblnc's runtime does in every game; "
                          "AmdLmxxfAutoExposure=false feeds the colour as is)"
                        : "lmxxf: the title publishes no exposure texture; the network is fed the colour as is "
                          "(right for SDR-range colour, too bright for linear HDR)");
            }
            const bool titleExposure = haveExposure && !exposureMuted && useTitleExposure;
            expoTitleIgnored = haveExposure && !useTitleExposure; // for the stats readback (the source line, the self-heal)
            expoKeyOff = !useTitleExposure;                       // and the key alone: when the source line is re-logged
            const bool autoOn = !titleExposure && cfg.lmxxfAutoExposure && autoExposure && autoExpo;
            if (autoOn)
            {
                // Measure the render-size copy at the previous exposure and step; the state lives
                // in the UAV (read-modify-write), so no second texture and no readback. The last
                // constant is the highlight cap (0.3.3; 0 = the loop as before).
                ID3D12Resource* srvA[1] = { full.Get() };
                const DXGI_FORMAT fmtsA[1] = { kFp16 };
                Barrier(cmd, autoExpo.Get(), kSrv, kUav);
                autoExposure->Run(cmd, srvA, fmtsA, autoExpo.Get(), DXGI_FORMAT_R32_FLOAT, 1u, 1u, 0u, 0.5f, 1.5f, 0.25f,
                                  highlightCap ? 1.f : 0.f, false);
                Barrier(cmd, autoExpo.Get(), kUav, kSrv);
            }
            ID3D12Resource* srv[1] = { titleExposure ? f.exposure : autoOn ? autoExpo.Get() : full.Get() };
            const DXGI_FORMAT fmts[1] = { titleExposure ? ExposureViewFormat(f.exposure->GetDesc().Format) : autoOn ? DXGI_FORMAT_R32_FLOAT : kFp16 };
            if (haveExposure) Barrier(cmd, f.exposure, f.exposureState, kSrv);
            Barrier(cmd, expoJob.Get(), kSrv, kUav);
            expoModeFed = titleExposure ? 0u : autoOn ? 2u : 1u; // what expoJob holds from now on (the stats readback)
            exposurePass->Run(cmd, srv, fmts, expoJob.Get(), DXGI_FORMAT_R32_FLOAT, 1u, 1u, expoModeFed, pre, scale, 0.f, 0.f, false);
            Barrier(cmd, expoJob.Get(), kUav, kSrv);
            if (haveExposure) Barrier(cmd, f.exposure, kSrv, f.exposureState);
            // The network is fed the raw frame; "passes" are real launches inside the runtime now
            // (the answer re-fed on the HIP stream), so nothing of ours goes into the copy.
            ID3D12Resource* srv2[3] = { full.Get(), expoJob.Get(), full.Get() };
            const DXGI_FORMAT fmts2[3] = { kFp16, DXGI_FORMAT_R32_FLOAT, kFp16 };
            Barrier(cmd, fed.Get(), kSrv, kUav);
            blitColour->Run(cmd, srv2, fmts2, fed.Get(), kFp16, mw, mh, 1u,
                            float(w) / float(mw), float(h) / float(mh), float(w), float(h));
            Barrier(cmd, fed.Get(), kUav, kSrv);
        }
        // The network's temporal history (upstream's path): the vectors at the model's size, raw
        // texture units, pixel-exact from the active extent; the scale turns them into pixels of
        // the fed grid. History is valid only when the previous job was fed on the previous
        // frame and its answer arrived (fresh): a skipped frame, a reset or a size change breaks
        // the chain and the network runs from scratch once.
        const bool wantHistory = cfg.lmxxfHistory;
        // The previous answer is `gap` frames old: 1 with the network on every frame, 2 under
        // Model interleave. Its vectors must cover that many frames.
        const UINT64 gap = lastFedFrame == ~0ull ? 0 : frames - lastFedFrame;
        const bool chainTwo = gap == 2 && frames >= allocFrame + 2;
        const bool historyChain = answerSinceFeed && !resetSinceFeed && (gap == 1 || chainTwo);
        if (wantHistory)
        {
            const auto md = f.motion->GetDesc();
            ID3D12Resource* srv[2] = { f.motion, temporal->MotionBeforeThis() };
            const DXGI_FORMAT fmts[2] = { MotionViewFormat(md.Format), DXGI_FORMAT_R16G16_FLOAT };
            Barrier(cmd, f.motion, f.motionState, kSrv);
            Barrier(cmd, motionFed.Get(), kSrv, kUav);
            motionChain->Run(cmd, srv, fmts, motionFed.Get(), DXGI_FORMAT_R16G16_FLOAT, mw, mh, chainTwo ? 1u : 0u,
                             float(mvW) / float(mw), float(mvH) / float(mh), float(md.Width), float(md.Height), true, 0.f,
                             csx, csy, float(w), float(h));
            Barrier(cmd, motionFed.Get(), kUav, kSrv);
            Barrier(cmd, f.motion, kSrv, f.motionState);
        }
        LmxxfNrFrameInfo fi {};
        fi.struct_size = kFrameInfo0332Size; // 128, the size every runtime since 0.3.3.2 accepts (see the constant)
        fi.session_id = 1;
        fi.frame_id = frames + 1;
        fi.list_generation = frames + 1;
        fi.command_list = cmd;
        fi.color_width = mw;
        fi.color_height = mh;
        fi.color = fed.Get();
        fi.color_state = kSrv;
        fi.flags = LMXXF_NR_FRAME_FLAG_STRENGTH | (wantHistory ? LMXXF_NR_FRAME_FLAG_TEMPORAL : 0u) |
                   (cfg.lmxxfCpuWait ? LMXXF_NR_FRAME_FLAG_VULKAN_BRIDGE : 0u) | // warm-up, no queue drains
                   (wantFullNetwork ? LMXXF_NR_FRAME_FLAG_FULL_NETWORK : 0u); // only when on: older runtimes refuse it
        // RenoDX mode: 1/1 (the host composes; see composeFeed). Classic: Detail / Colour strength.
        // The clamp stays - the runtime refuses a strength above 1 (PrepareFrame fails).
        // (0.3.4, P7.6) Network output: 1/1 as well - the model's whole answer (danielblnc skips its Detail / Colour mix).
        fi.transfer_strength = composeFeed || rawNetwork ? 1.f : std::clamp(cfg.detail, 0.f, 1.f);
        fi.color_strength = composeFeed || rawNetwork ? 1.f : std::clamp(cfg.colour, 0.f, 1.f);
        fi.model_scale = 1.f;
        fi.motion = motionFed.Get();
        fi.motion_state = kSrv;
        fi.motion_width = mw;
        fi.motion_height = mh;
        // motionFed holds render pixels already (the chain shader applied the NGX scale).
        fi.motion_scale_x = float(mw) / float(w);
        fi.motion_scale_y = float(mh) / float(h);
        fi.history_reset = historyChain ? 0u : 1u;
        fi.passes = passes;
        // Upstream's output-side smoothing inside the runtime: only meaningful with a history.
        // (0.3.4, P7.6) Off under Network output: it blends the answer toward the previous one.
        fi.output_smooth = wantHistory && !rawNetwork ? std::clamp(cfg.lmxxfOutputSmooth, 0.f, 1.f) : 0.f;
        fi.output_smooth_threshold = 6.f / 255.f;
        // (0.3.4, AUTOMASK host step) Values other than lmxxf's built-in ones (1, 1, 1, 1): the whole 144-byte struct,
        // the flag and the five controls (style stays 1). The built-in values keep the frame above byte for byte
        // (128 bytes, no flag, bytes 124..127 zero), which every runtime since 0.3.3.2 takes.
        const bool sendCtl = !wantCtl.BuiltIn();
        if (sendCtl)
        {
            fi.struct_size = sizeof fi;
            fi.flags |= LMXXF_NR_FRAME_FLAG_CONTROLS;
            AmdPreSr::WriteNrControls(fi, wantCtl);
        }
        job = {};
        job.struct_size = sizeof job;
        // Vulkan bridge: the first PrepareFrame creates the HIP bridge and runs the warm-up, the
        // first point where this session could freeze; the marker goes down before it, and again
        // before a later attempt when an earlier one failed (and removed it) with no answer yet.
        if (cfg.lmxxfCpuWait && !vkFirstAnswer && !vkMarkerLive &&
            (vkMarkerWrites == 0 || GetTickCount64() - vkMarkerLastWrite >= kVkMarkerRewriteMs))
            WriteVkMarker();
        const auto t0 = std::chrono::steady_clock::now();
        int32_t rc = api.PrepareFrame(ctx, &fi, &job);
        if (rc != LMXXF_NR_OK)
        {
            ++skips;
            // (0.3.4, AUTOMASK host step) A runtime older than the character mask controls refuses the frame that
            // carries them: 0.3.3.2's 1343bbc5 and older the size ("PrepareFrame: struct_size mismatch"); one that took
            // the size but not the flag would say "unknown flags" (a runtime that knows the controls knows every older
            // flag, so the controls are the unknown one); the 0.3.4 runtime "controls need struct_size 144" / "control
            // out of range". lmxxf's built-in mask runs from the next model frame (the 0.3.3.2 frame) rather than no
            // network at all, once, with a line saying which file is old; the menu reads ControlsRefused().
            bool ctlRefusedNow = false; // this attempt was only the controls' refusal: the next frame retries at once
            if (sendCtl && !ctlRefused)
            {
                const std::string err = LastError();
                if (err.find("struct_size mismatch") != std::string::npos || err.find("unknown flags") != std::string::npos ||
                    err.find("controls need struct_size") != std::string::npos || err.find("control out of range") != std::string::npos)
                {
                    ctlRefused = true;
                    ctlRefusedNow = true;
                    controlsRefused.store(true);
                    Log("lmxxf: this LmxxfNrRuntime.dll refused the character mask controls (" + err + "; an older runtime) - "
                        "[DlssNr] AutoMask=false, Structure intensity and Character structure do not act on lmxxf with it; lmxxf's "
                        "built-in character mask runs from the next frame. Use the LmxxfNrRuntime.dll shipped with this OptiScaler build");
                }
            }
            // A runtime older than the Full network flag refuses it ("unknown flags"). The default
            // network runs from the next model frame on rather than none at all, and the log says
            // which file is old. (0.3.4) Not read while the frame carried the controls: then the controls are the
            // unknown flag (above), and the next frame, without them, tells whether Full network is known.
            if (wantFullNetwork && !sendCtl && LastError().find("unknown flags") != std::string::npos)
            {
                fullNetRefused = true;
                fullNetworkRefused.store(true); // the menu's note (FullNetworkRefused)
                Log("lmxxf: this LmxxfNrRuntime.dll refused Full network ([DlssNr] LmxxfFullNetwork: unknown flag, an older "
                    "runtime) - the default network runs instead (blocks 42, 43, 46 skipped); use the LmxxfNrRuntime.dll "
                    "shipped with this OptiScaler build");
            }
            const bool logFailure = ++prepareFailures <= 5 || prepareFailures % 300 == 0;
            if (logFailure || vkMarkerLive || fatalError.empty())
            {
                const std::string err = LastError();
                if (logFailure)
                    Log("lmxxf PrepareFrame failed (" + std::to_string(prepareFailures) + "): " + err +
                        (cfg.lmxxfCpuWait && !wantFullNetwork && !sendCtl && err.find("unknown flags") != std::string::npos
                             ? " - this LmxxfNrRuntime.dll predates the Vulkan-bridge flag; use the one shipped with this OptiScaler build"
                             : ""));
                // Vulkan bridge: PrepareFrame (or the HIP warm-up inside it) returned, so this
                // attempt did not hang, and no job exists: nothing is outstanding. (0.3.4) Not after the controls'
                // refusal, an argument check before any HIP work: the retry with the 0.3.3.2 frame comes on the next
                // frame, sooner than the marker's rewrite limit, and its warm-up must still be covered by the marker.
                if (vkMarkerLive && !ctlRefusedNow)
                    DropVkMarker("lmxxf vk: PrepareFrame failed (" + err + ") - marker removed, the next start retries lmxxf");
                NoteFatal(err);
            }
        }
        else
        {
            // Vulkan bridge: the runtime reports its HIP warm-up (and a skipped queue drain) through
            // LastError after a successful PrepareFrame; read here, before RecordInputs clears it.
            // A new session or a new network size means this PrepareFrame created a bridge.
            if (cfg.lmxxfCpuWait)
            {
                const std::string note = LastError();
                const bool created = ctx != vkWarmCtx || mw != vkWarmW || mh != vkWarmH;
                if (note.find("HIP warm-up skipped") != std::string::npos)
                    vkWarmSkipped = true; // no weights: nothing was uploaded ahead of the first job
                if (note.find("HIP warm-up") != std::string::npos || (!created && !note.empty()))
                    Log(note);
                else if (created)
                    Log("lmxxf vk: warm-up line missing: " + (note.empty() ? std::string("(the runtime reported nothing)") : note));
                vkWarmCtx = ctx;
                vkWarmW = mw;
                vkWarmH = mh;
            }
            // Full network: one line when this session's network is first built and on every change,
            // with the runtime's own skip= word so the line says what really runs. (After the Vulkan
            // note above: GetStatus clears the runtime's LastError.)
            if (ctx != fullNetCtx || wantFullNetwork != fullNetSent)
            {
                const bool live = ctx == fullNetCtx;
                const std::string st = RuntimeStatus();
                const size_t at = st.find("skip=");
                const size_t end = at == std::string::npos ? std::string::npos : st.find(' ', at);
                const std::string skip = at == std::string::npos ? std::string("skip=? (runtime did not say)")
                                                                 : st.substr(at, end == std::string::npos ? std::string::npos : end - at);
                Log(std::string("lmxxf: full network ") + (wantFullNetwork ? "on (all 71 blocks run)" : "off (blocks 42, 43, 46 skipped, about 0.5 ms faster at 1080p)") +
                    " - runtime " + skip + (live ? " - applied live: the runtime rebuilt the network, its history restarts" : ""));
                fullNetCtx = ctx;
                fullNetSent = wantFullNetwork;
            }
            // (0.3.4, AUTOMASK host step) One line when a session's network is first built with controls other than the
            // built-in ones, and on every change, with the runtime's own ctl= word (printed only while they are not
            // built-in), so the line says what really runs. Nothing at the defaults.
            if ((ctx != ctlCtx && sendCtl) || (ctx == ctlCtx && wantCtl != ctlSent))
            {
                const bool live = ctx == ctlCtx;
                const std::string st = RuntimeStatus();
                const size_t at = st.find(" ctl=");
                const size_t end = at == std::string::npos ? std::string::npos : st.find(' ', at + 1);
                const std::string word = at != std::string::npos ? st.substr(at + 1, end == std::string::npos ? std::string::npos : end - at - 1)
                                         : sendCtl ? std::string("ctl=? (runtime did not say)")
                                                   : std::string("no ctl= (built-in)");
                char values[96] {};
                std::snprintf(values, sizeof values, "tone %g, structure %g, skin %g, other %g", wantCtl.tone, wantCtl.structure,
                              wantCtl.skin, wantCtl.other);
                Log(std::string("lmxxf: character mask ") + (cfg.autoMask ? "on" : "off") + " ([DlssNr] AutoMask), controls " + values +
                    (wantCtl.BuiltIn() ? " (lmxxf's built-in values)" : "") + " - runtime " + word +
                    (live ? " - applied live: the runtime rebuilt the network, its history restarts" : ""));
            }
            ctlCtx = ctx;
            ctlSent = wantCtl;
            rc = api.RecordInputs(ctx, job.handle, cmd);
            if (rc != LMXXF_NR_OK)
            {
                const std::string err = LastError();
                Log("lmxxf RecordInputs failed: " + err);
                NoteFatal(err);
                api.CancelUnsubmitted(ctx, job.handle);
                ++skips;
                // Vulkan bridge: the job is cancelled before any launch; nothing is outstanding.
                if (vkMarkerLive)
                    DropVkMarker("lmxxf vk: RecordInputs failed (" + err + ") - marker removed, the next start retries lmxxf");
            }
            else
            {
                netOut = static_cast<ID3D12Resource*>(job.private_output);
                // The per-job latch: this job's answer is composed at consume only if it was fed at
                // 1/1, whatever the setting says by then - a toggle never mixes the two modes.
                jobCompose = composeFeed;
                jobRecorded = true;
                jobGraced = false; // (0.3.4, P3) a new job: no grace yet
                SetPending(cmd);
                lastFedFrame = frames;
                if (noMotion && baseInFlight)
                {
                    // The raw frame this job was fed: the residual and the gate refer to it later.
                    Barrier(cmd, full.Get(), kSrv, D3D12_RESOURCE_STATE_COPY_SOURCE);
                    Barrier(cmd, baseInFlight.Get(), kSrv, D3D12_RESOURCE_STATE_COPY_DEST);
                    cmd->CopyResource(baseInFlight.Get(), full.Get());
                    Barrier(cmd, baseInFlight.Get(), D3D12_RESOURCE_STATE_COPY_DEST, kSrv);
                    Barrier(cmd, full.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, kSrv);
                    Barrier(cmd, lumCur.Get(), kSrv, D3D12_RESOURCE_STATE_COPY_SOURCE);
                    Barrier(cmd, lumFed.Get(), kSrv, D3D12_RESOURCE_STATE_COPY_DEST);
                    cmd->CopyResource(lumFed.Get(), lumCur.Get());
                    Barrier(cmd, lumFed.Get(), D3D12_RESOURCE_STATE_COPY_DEST, kSrv);
                    Barrier(cmd, lumCur.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, kSrv);
                }
                ++modelFrames;
                completedForActivity.fetch_add(1, std::memory_order_release);
                answerSinceFeed = false;
                resetSinceFeed = false;
                // P6: one status read per model frame, for the stats line's hip_ms and the history word below.
                const std::string modelStatus = RuntimeStatus();
                NoteHipMs(modelStatus);
                if (wantHistory) { if (historyChain) ++statHistOn; else ++statHistReset; }
                if (wantHistory)
                {
                    // Log the history state the runtime reports (on / reset / off, guard) whenever
                    // it changes: "I did not feel a difference" needs this line to be answerable.
                    const std::string& st = modelStatus;
                    const size_t at = st.find("hist=");
                    std::string word = at == std::string::npos ? std::string("hist=?") : st.substr(at, st.find(' ', at) == std::string::npos ? std::string::npos : st.find(' ', at) - at);
                    {
                        // drop the running answer count ("/123") so only real state changes log
                        const size_t sl = word.find('/');
                        if (sl != std::string::npos)
                        {
                            size_t e2 = sl + 1;
                            while (e2 < word.size() && word[e2] >= '0' && word[e2] <= '9') ++e2;
                            word.erase(sl, e2 - sl);
                        }
                    }
                    if (word != lastHistState)
                    {
                        lastHistState = word;
                        Log("lmxxf: network history " + word + " (gap " + std::to_string(gap) + " frame(s)" + (chainTwo ? ", vectors chained over two frames" : "") + ")");
                    }
                }
                if (!loggedFirst)
                {
                    loggedFirst = true;
                    const auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
                    // The weights are uploaded by the warm-up over the Vulkan bridge, else by the first job
                    // (also over the bridge when the runtime reported "HIP warm-up skipped").
                    Log(std::string("lmxxf: first frame prepared (HIP modules and shaders loaded; ") +
                        (cfg.lmxxfCpuWait && !vkWarmSkipped ? "weights uploaded by the HIP warm-up" : "weights upload with the first job") + ") in " +
                        std::to_string(static_cast<int>(ms)) + " ms: " + RuntimeStatus());
                }
            }
        }
    }
    else if (wantModel)
        ++skips; // the runtime comes up at this list's submission; see Submitted
    if (!ctx)
        ArmBootstrap(cmd); // observe a list's queue: that is the queue the runtime binds to
    // 6b. Interleave pacing (0.3.4, FB-L5 / plan D9): the pacer (dlssnr/amd/InterleavePacing.h, run at present)
    //     is told which frames fed the network, as the danielblnc host does, so the menu's model / fill
    //     readout measures lmxxf too. A "model" frame is one that fed a job (lastFedFrame set above). The pacer
    //     paces only at a strength above 0; on lmxxf that is an explicit AmdInterleavePacing 0..1. Auto (-1,
    //     the default) is 0 here, so lmxxf's frame timing stays as it was (strength 0 measures and never
    //     sleeps). To give lmxxf danielblnc's auto rule, pass cfg.interleavePacing as is on the next line.
    const float lmxxfPacing = cfg.interleavePacing >= 0.f ? cfg.interleavePacing : 0.f;
    DlssNr::Pacing::SetStrength(cadence > 1u ? lmxxfPacing : 0.f);
    DlssNr::Pacing::Note(lastFedFrame == frames);

    // 6c. (0.3.4, P7.7) RTGI, on the carried frame before the Image look, gated like the look (not under Network
    //     output, as on danielblnc, nor in debug view 5). Its depth is the title's (after Ray Regeneration the
    //     resample onto the output grid), never the self-heal's flat stand-in, copied to R32F when RtgiNative cannot
    //     read its format. Off (the default): nothing is made or recorded.
    // 8c. (0.3.4, P7.5) AmdEncoding sRGB / Gamma 2.2: the result encoded back into the game's transfer, last, as the
    //     danielblnc host encodes its final colour (before its write-back too).
    if (convertEncoding && out)
    {
        const UINT ec[4] = { w, h, cfg.encoding, 1u }; // inverse 1 = encode
        ID3D12Resource* srvE[1] = { out };
        const DXGI_FORMAT fmtsE[1] = { kFp16 };
        Barrier(cmd, encodedOut.Get(), kSrv, kUav);
        encodingPass->RunConstants(cmd, srvE, fmtsE, encodedOut.Get(), kFp16, w, h, ec, 4u);
        Barrier(cmd, encodedOut.Get(), kUav, kSrv);
        out = encodedOut.Get();
    }
    // 8b. After Ray Regeneration: the result goes back into the output the title will present.
    //     Half-float outputs are copied; other UAV-capable formats are written through a typed
    //     view (the blit at identity); anything else is refused once in the log.
    if ((f.writeBack || f.inputWriteBack) && out)
    {
        const auto td = f.colour->GetDesc();
        const bool copyable = td.Format == DXGI_FORMAT_R16G16B16A16_FLOAT || td.Format == DXGI_FORMAT_R16G16B16A16_TYPELESS;
        if (copyable)
        {
            Barrier(cmd, out, kSrv, D3D12_RESOURCE_STATE_COPY_SOURCE);
            Barrier(cmd, f.colour, f.colourState, D3D12_RESOURCE_STATE_COPY_DEST);
            D3D12_TEXTURE_COPY_LOCATION from {}, to {};
            from.pResource = out;
            from.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            to.pResource = f.colour;
            to.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            const D3D12_BOX box { 0, 0, 0, w, h, 1 };
            cmd->CopyTextureRegion(&to, 0, 0, 0, &from, &box);
            Barrier(cmd, f.colour, D3D12_RESOURCE_STATE_COPY_DEST, f.colourState);
            Barrier(cmd, out, D3D12_RESOURCE_STATE_COPY_SOURCE, kSrv);
        }
        else if (td.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS)
        {
            ID3D12Resource* srvb[3] = { out, expoJob.Get(), out };
            const DXGI_FORMAT fmtsb[3] = { kFp16, DXGI_FORMAT_R32_FLOAT, kFp16 };
            Barrier(cmd, f.colour, f.colourState, kUav);
            if (f.colourState == kUav)
                UavBarrier(cmd, f.colour); // order our store after the upscaler's own
            blitColour->Run(cmd, srvb, fmtsb, f.colour, ColourViewFormat(td.Format), w, h, 0u, 1.f, 1.f, float(w), float(h));
            Barrier(cmd, f.colour, kUav, f.colourState);
        }
        else
        {
            if (!loggedWriteBackRefusal)

        {
            loggedWriteBackRefusal = true;
            Log("lmxxf after RR: the output format " + std::to_string(static_cast<int>(td.Format)) +
                " is neither half-float nor UAV-capable; the result was not written back");
            }
            return nullptr;
        }
    }
    // 9. Every kStatsEvery frames: the exposure the job is fed with, the fed picture's mean
    //    brightness, the model's edit magnitude (mean, max), the counters since the last line,
    //    the runtime's idle_syncs / idle_sync_skips (IdleSyncNote, totals for the current network
    //    build) and the process's video memory / budget and private bytes (MemoryNote, read now).
    //    The GPU numbers are read back one window later (no stall).
    //    On the D3D11 bridge (cfg.d3d11Bridge) only the CPU half runs: the GPU half, recorded on
    //    IFeature_Dx11wDx12's list, made that list's Close return E_INVALIDARG on its first window
    //    (frame 600), and the bridge never recovered - every later Reset of the list failed too, so
    //    the picture stayed broken for the rest of the session. With the GPU half out, the line keeps
    //    the counters and says the GPU values are n/a, as the NR cost readout does on this bridge.
    if (statsPass && statsTex && freshEdit && fed && frames % kStatsEvery == kStatsEvery - 1)
    {
        const unsigned slot = static_cast<unsigned>((frames / kStatsEvery) & 1u);
        const bool gpuStats = !cfg.d3d11Bridge;
        if (gpuStats)
        {
            ID3D12Resource* srv[2] = { freshEdit.Get(), fed.Get() };
            const DXGI_FORMAT fmts[2] = { kFp16, kFp16 };
            Barrier(cmd, statsTex.Get(), kSrv, kUav);
            statsPass->Run(cmd, srv, fmts, statsTex.Get(), DXGI_FORMAT_R32_UINT, w, h, 1u, 0.f, 0.f, 0.f, 0.f, false);
            UavBarrier(cmd, statsTex.Get());
            statsPass->Run(cmd, srv, fmts, statsTex.Get(), DXGI_FORMAT_R32_UINT, w, h, 0u);
            if (ID3D12Resource* carried = noMotion ? carryHist.Get() : temporal->ResidualHistory())
            {
                UavBarrier(cmd, statsTex.Get());
                ID3D12Resource* srv2[2] = { carried, fed.Get() };
                statsPass->Run(cmd, srv2, fmts, statsTex.Get(), DXGI_FORMAT_R32_UINT, w, h, 2u);
            }
            if (f.reactive)
            {
                const auto rd = f.reactive->GetDesc();
                UavBarrier(cmd, statsTex.Get());
                ID3D12Resource* srv3[2] = { freshEdit.Get(), f.reactive };
                const DXGI_FORMAT fmts3[2] = { kFp16, ReactiveViewFormat(rd.Format) };
                statsPass->Run(cmd, srv3, fmts3, statsTex.Get(), DXGI_FORMAT_R32_UINT, w, h, 3u, float(ReactiveChannels(rd.Format)));
            }
            if (motion)
            {
                const auto md = motion->GetDesc();
                UavBarrier(cmd, statsTex.Get());
                ID3D12Resource* srv4[2] = { motion, fed.Get() };
                const DXGI_FORMAT fmts4[2] = { motion == motionScratch.Get() ? DXGI_FORMAT_R16G16_FLOAT : MotionViewFormat(md.Format), kFp16 };
                Barrier(cmd, motion, motionState, kSrv);
                statsPass->Run(cmd, srv4, fmts4, statsTex.Get(), DXGI_FORMAT_R32_UINT, w, h, 4u, sx, sy);
                Barrier(cmd, motion, kSrv, motionState);
            }
            Barrier(cmd, statsTex.Get(), kUav, D3D12_RESOURCE_STATE_COPY_SOURCE);
            Barrier(cmd, expoJob.Get(), kSrv, D3D12_RESOURCE_STATE_COPY_SOURCE);
            CopyToReadback(cmd, statsTex.Get(), DXGI_FORMAT_R32_UINT, 12, statsRead[slot].Get(), 0);
            CopyToReadback(cmd, expoJob.Get(), DXGI_FORMAT_R32_FLOAT, 2, statsRead[slot].Get(), 256);
            statsExpoMode[slot] = expoModeFed; // the mode that wrote the expoJob just copied
            statsTitleIgnored[slot] = expoTitleIgnored; // and whether AmdUseGameExposure=0 set the title's texture aside
            statsKeyOff[slot] = expoKeyOff;             // and whether that key was 0 at all
            Barrier(cmd, statsTex.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, kSrv);
            Barrier(cmd, expoJob.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, kSrv);
        }
        const unsigned prev = slot ^ 1u;
        std::string gpu = gpuStats ? "GPU values pending" : "GPU values n/a on the D3D11 bridge";
        if (statsWritten[prev])
        {
            void* mapped = nullptr;
            const D3D12_RANGE range { 0, 260 };
            if (SUCCEEDED(statsRead[prev]->Map(0, &range, &mapped)) && mapped)
            {
                UINT vals[12] {};
                float e = 0.f, eraw = 0.f;
                std::memcpy(vals, mapped, sizeof vals);
                std::memcpy(&e, static_cast<const char*>(mapped) + 256, sizeof e);
                std::memcpy(&eraw, static_cast<const char*>(mapped) + 260, sizeof eraw);
                const D3D12_RANGE none { 0, 0 };
                statsRead[prev]->Unmap(0, &none);
                const double px = double(w) * double(h);
                const double fedMean = vals[1] / 16.0 / px, freshMean = vals[0] / 256.0 / px, keepMean = vals[4] / 256.0 / px;
                const double reactiveMean = vals[6] / 256.0 / px, rejected = vals[8] / 256.0 / px;
                // 0.3.3 colour fix: the fed pixels past paper white in every channel (blown) and in the
                // codec's shoulder (max channel above 0.75), as fractions; what fed the job whose
                // exposure was copied (texture / auto / 1); and whether a texture value was refused
                // (the ExposureShader's own test: 1/256 < v < 256). Exposure and raw are printed with %g,
                // so a value below 1e-4 (auto-exposure's floor is 1/65536) shows as itself, not 0.0000.
                const double blownFrac = vals[9] / px, shoulderFrac = vals[10] / px;
                const unsigned fedMode = statsExpoMode[prev];
                const bool titleIgnored = statsTitleIgnored[prev]; // (0.3.4) AmdUseGameExposure=0 in that window
                const bool keyOff = statsKeyOff[prev];             // the key alone (the texture may have been absent)
                const bool refused = fedMode == 0u && !(std::isfinite(eraw) && eraw > 0.00390625f && eraw < 256.f);
                const char* const fedFrom = fedMode == 0u ? (refused ? "title texture, refused" : "title texture")
                                            : fedMode == 2u ? (titleIgnored ? "auto-exposure, title texture ignored" : "auto-exposure")
                                            : fedMode == 1u ? (titleIgnored ? "fixed 1, title texture ignored" : "fixed 1")
                                                            : "no feed yet";
                char buf[640];
                std::snprintf(buf, sizeof buf, "exposure %.6g (raw %.6g, %s) | fed mean %.4f, shoulder %.1f%%, blown %.1f%% | fresh edit mean %.5f max %.4f | carried edit mean %.5f max %.4f | keep mean %.3f | reactive mean %.3f%s | mv mean %.2f px, rejected %.1f%% (GPU values from %llu frames earlier)",
                              e, eraw, fedFrom, fedMean, shoulderFrac * 100.0, blownFrac * 100.0, freshMean, vals[3] / 65536.0,
                              vals[2] / 256.0 / px, vals[5] / 65536.0, keepMean,
                              reactiveMean, f.reactive ? "" : " (no mask)", vals[7] / 4.0 / px, rejected * 100.0,
                              static_cast<unsigned long long>(kStatsEvery));
                gpu = buf;
                // The exposure source, once per session (0.3.3), with the raw value: which of the
                // colour report's states a title is in - the texture accepted (too high: a fed mean
                // well above 0.75 with a high shoulder share), accepted at exactly 1, refused (fed at
                // 1), or no texture (auto-exposure; its floor is 1/65536 since 0.3.3). (0.3.4, EXPO-PARITY) It
                // names the source that window was really fed from, AmdUseGameExposure=0 included, and is
                // logged again only when that key itself moves to or from 0 (G1 review: a title's texture
                // coming and going with the key held at 0 is not a key change and logs nothing).
                if (fedMode != ~0u && (!loggedExposureSource || keyOff != loggedKeyOff))
                {
                    const char* const when = !loggedExposureSource ? "once per session"
                                             : keyOff              ? "AmdUseGameExposure changed to 0"
                                                                   : "AmdUseGameExposure changed from 0";
                    loggedExposureSource = true;
                    loggedKeyOff = keyOff;
                    // Ignored: the texture was there at the feed and set aside, or the key is 0 and one is there
                    // now (it arrived after the feed; the self-heal cannot fire at 0, so it is not the heal's).
                    const bool srcIgnored = titleIgnored || (keyOff && f.exposure);
                    std::string src;
                    if (fedMode == 0u)
                    {
                        char t[224];
                        std::snprintf(t, sizeof t, "the title's exposure texture, value %s (texel x scale %g / pre-exposure %g)",
                                      refused ? (eraw > 1e-6f && eraw <= 0.00390625f
                                                     ? "REFUSED - fed at exposure 1 (1e-6 < raw <= 1/256: a lower bound of 1e-6 would accept it)"
                                                     : "REFUSED - outside 1/256..256, fed at exposure 1")
                                              : "accepted",
                                      static_cast<double>(std::isfinite(f.exposureScale) && f.exposureScale > 0.f ? f.exposureScale : 1.f),
                                      static_cast<double>(std::isfinite(f.preExposure) && f.preExposure > 0.f ? f.preExposure : 1.f));
                        src = t;
                    }
                    else if (fedMode == 2u)
                        src = srcIgnored ? "auto-exposure (the title's exposure texture is ignored: AmdUseGameExposure=0)"
                              : f.exposure ? "auto-exposure (the title's texture was set aside by the self-heal)"
                                           : "none (the title publishes no exposure texture) - auto-exposure";
                    else
                        src = srcIgnored ? "exposure 1 (the title's exposure texture is ignored: AmdUseGameExposure=0; AmdLmxxfAutoExposure is off)"
                              : f.exposure ? "exposure 1 (the title's texture was set aside by the self-heal; AmdLmxxfAutoExposure is off)"
                                           : "none (the title publishes no exposure texture) - exposure 1 (AmdLmxxfAutoExposure is off)";
                    char m[640];
                    std::snprintf(m, sizeof m, "lmxxf exposure source (%s): %s | raw %.6g, e %.6g | fed mean %.4f, %.1f%% of fed pixels in the codec's shoulder (max channel > 0.75), %.1f%% past paper white in every channel",
                                  when, src.c_str(), static_cast<double>(eraw), static_cast<double>(e), fedMean, shoulderFrac * 100.0, blownFrac * 100.0);
                    Log(m);
                }
                // Self-healing: three rules on the window just read. Each fires once per session,
                // only on a clear fault, and says so in the log; a title inside the normal ranges
                // is never touched.
                if (!motionMuted && rejected > 0.5)
                {
                    motionMuted = true;
                    char m[256];
                    std::snprintf(m, sizeof m, "lmxxf self-heal: %.0f%% of the title's motion vectors were rejected (non-finite or longer than half the frame, scale %.3f x %.3f); the edit is carried without reprojection from now on",
                                  rejected * 100.0, f.motionScaleX, f.motionScaleY);
                    Log(m);
                }
                if (!depthMuted && !motionMuted && statFresh > 0 && keepMean < 0.05 && rejected < 0.1 && reactiveMean < 0.5 && freshMean > 0.005)
                {
                    depthMuted = true;
                    char m[256];
                    std::snprintf(m, sizeof m, "lmxxf self-heal: the carry kept %.1f%% of the edit with sound vectors and no covering mask, so the depth test (format %d, %s) is refusing it; the depth test is off from now on",
                                  keepMean * 100.0, static_cast<int>(f.depth->GetDesc().Format), f.depthInverted ? "inverted" : "normal");
                    Log(m);
                }
                // A black fed picture is the exposure's fault only when the scene itself is not black:
                // on a loading screen the raw mean is ~0 as well (RE Requiem: muted at 0.1253 on a
                // black frame, then fed at exposure 1 with a mean of 2.69 for the rest of the session -
                // the "lmxxf colours differ" report). Once muted, the auto-exposure loop feeds the
                // network instead of a bare exposure 1.
                //
                // 0.3.3 colour fix. BLOWN is measured directly: more than half the fed pixels past
                // paper white in EVERY channel (stats slot 9), or a fed mean above 12 (each pixel's
                // luminance is clamped at 16 there), in TWO windows in a row fed with the title's
                // exposure - a flash, a white fade or a lagging eye adaptation in a healthy title
                // lasts one window, and the heal is a whole-session latch. The black rule is as it
                // was. The heal now also fires at e == 1 while auto-exposure is on: a value refused
                // (outside 1/256..256, fed at 1) or a texture holding exactly 1 on a colour that is
                // not pre-exposed blows the feed out as surely as a value accepted too high, and
                // with auto-exposure off a heal to exposure 1 would change nothing. A healthy feed
                // (SDR-range colour, or linear HDR at a sane exposure) meets neither rule.
                const double rawMean = e > 0.f ? fedMean / e : fedMean;
                const bool blackWindow = fedMean < 0.002 && rawMean > 0.002;
                // (0.3.4, EXPO-PARITY) Only while the title's exposure is in use: with AmdUseGameExposure=0 now, or in
                // the window read, nothing here is the title exposure's fault, so neither rule counts or fires. At the
                // key's default (-1) and at 1 this is true and both rules are exactly as before.
                const bool titleInUse = cfg.useGameExposure != 0 && !titleIgnored;
                const bool titleWindow = f.exposure && !exposureMuted && fedMode == 0u && titleInUse;
                blownWindows = titleWindow && (blownFrac > 0.5 || fedMean > 12.0) ? blownWindows + 1u : 0u;
                if (!exposureMuted && f.exposure && titleInUse && (e != 1.f || cfg.lmxxfAutoExposure) && (blackWindow || blownWindows >= 2u))
                {
                    exposureMuted = true;
                    blownWindows = 0;
                    char m[560];
                    std::snprintf(m, sizeof m, "lmxxf self-heal: with the title's exposure (e %.6g, raw %.6g%s) the fed picture is %s (mean %.4f, %.1f%% of fed pixels past paper white in every channel, %.1f%% in the codec's shoulder%s); the network is fed by %s from now on",
                                  static_cast<double>(e), static_cast<double>(eraw), refused ? ", refused: fed at 1" : "",
                                  blackWindow ? "black" : "blown out", fedMean, blownFrac * 100.0, shoulderFrac * 100.0,
                                  blackWindow ? "" : "; two windows in a row",
                                  cfg.lmxxfAutoExposure ? "auto-exposure" : "exposure 1 (AmdLmxxfAutoExposure is off)");
                    Log(m);
                }
            }
        }
        statsWritten[slot] = gpuStats; // nothing was copied into statsRead[slot] without the GPU half
        // RenoDX windows carry the composition values in force now (AMDNR 0.3.3), so a tester's log
        // shows that a slider move arrived even after the change lines above have run out.
        std::string composedNote;
        if (statComposed)
        {
            char buf[200];
            std::snprintf(buf, sizeof buf, " | RenoDX composed +%u (W fed units; detail %.2f, colour %.2f, guard %.1fx, skin %s, chroma guard %s)",
                          statComposed, Unit(cfg.composeDetail, 0.f, 2.f, 1.f), Unit(cfg.composeColour, 0.f, 4.f, 1.f),
                          Unit(cfg.maxRatio, 1.f, 8.f, 2.f), cfg.skinProtection ? "on" : "off", chromaGuard ? "on" : "off");
            composedNote = buf;
        }
        // Which fill carried the edit this window, and under Edit accumulation what it did: the
        // share of the picture wearing the tone curve (the carry was not valid there) and the share
        // the raw-against-raw test alone refused. The temporal pass's readings, three frames old.
        std::string fillNote = " | fill classic";
        if (accumulate && temporal)
        {
            char buf[160];
            std::snprintf(buf, sizeof buf, " | fill edit-accum: tone curve %.1f%%, raw test refused %.1f%%",
                          100.0 * temporal->ChangeFraction(), 100.0 * temporal->GateRefusedFraction());
            fillNote = buf;
        }
        // (0.3.4, P3) late: neural lists that reached ExecuteCommandLists after the next frame's Record (a graced job's,
        // launched; or a dropped job's, too late), with the largest lateness in frames; graced: jobs kept one frame for it.
        const std::string lateNote = ", late +" + std::to_string(statLate) +
                                     (statLate ? " (up to " + std::to_string(statLateMax) + " frame(s))" : std::string()) +
                                     ", graced +" + std::to_string(statGraced);
        Log("lmxxf stats @" + std::to_string(frames + 1) + ": " + gpu + " | model +" + std::to_string(modelFrames - statModelAt) +
            " (fresh +" + std::to_string(statFresh) + ", skips +" + std::to_string(skips - statSkipsAt) + lateNote + ") | resets +" +
            std::to_string(statResets) + " (game +" + std::to_string(statGameResets) + ") | history on +" + std::to_string(statHistOn) +
            " reset +" + std::to_string(statHistReset) + (cfg.lmxxfHistory ? "" : " (history off)") +
            " | record cpu max " + std::to_string(recMaxMs).substr(0, 5) + " ms | " + HipMsNote() +
            (cadence > 1 ? " | interleave 1/" + std::to_string(cadence) + PacingNote(cfg.interleavePacing) : "") +
            (passes > 1 ? " | passes " + std::to_string(passes) : "") +
            (cfg.lmxxfCpuWait ? " | cpu wait max " + std::to_string(cpuWaitMax).substr(0, 5) + " ms (Vulkan bridge)" : "") +
            composedNote + fillNote + " | " + IdleSyncNote() + " | " + PoolNote() +
            " | " + MemoryNote());
        // (0.3.4, P3) The menu's late-submission line: while a list came late in this window, the share of its frames
        // with a fresh answer, times the cadence (a fill frame under Model interleave is not a miss); else -1.
        lateSubmitNrShare.store(statLate ? (std::min)(1.f, float(statFresh) * float(cadence) / float(kStatsEvery)) : -1.f);
        statLate = statGraced = statLateMax = 0;
        statComposed = 0;
        cpuWaitMax = 0.0;
        statModelAt = modelFrames;
        statSkipsAt = skips;
        statFresh = statResets = statGameResets = statHistOn = statHistReset = 0;
        recMaxMs = 0.0;
        lastWindowHipMs = hipMsSamples ? static_cast<float>(hipMsSum / hipMsSamples) : -1.f; // P7.10, before the reset
        hipMsSum = hipMsMax = 0.0;
        hipMsSamples = 0;
    }
    primedOnce = true;
    ++frames;
    while (!graveyard.empty() && graveyard.front().first + 8 < frames)
        graveyard.erase(graveyard.begin());

    if (frames % 30 == 1)
        status = "lmxxf: " + RuntimeStatus() + " | frames " + std::to_string(frames) + ", model " +
                 std::to_string(modelFrames) + ", skips " + std::to_string(skips) +
                 (cadence > 1 ? " | interleave 1/" + std::to_string(cadence) : "") +
                 (passes > 1 ? " | passes " + std::to_string(passes) : "") +
                 (motionMuted ? " | self-heal: no reprojection (vectors rejected)" : "") +
                 (depthMuted ? " | self-heal: depth test off" : "") +
                 (exposureMuted ? (cfg.lmxxfAutoExposure ? " | self-heal: auto-exposure" : " | self-heal: exposure 1") : "") +
                 (ctx ? std::string()
                  : bootRearms >= kBootStuckNote
                      ? std::string(" | runtime not loaded: no neural list reached ExecuteCommandLists (lmxxf_backend.log)")
                      : std::string(" | runtime not up yet: waiting for the game to submit a neural list"));
    return out;
}

Backend::Backend(ID3D12Device* d, ID3D12CommandQueue* q, const std::filesystem::path& dir) : p(new Impl)
{
    p->device = d;
    p->queue = q;
    p->directory = dir;
    // The assets actually used (the pak first, UsedAssetsPath), not the folder that might be there.
    std::filesystem::path ignored;
    const auto assets = UsedAssetsPath(dir, &ignored);
    std::error_code ec;
    const char* source = assets == PakPath(dir)                    ? " (pak)"
                         : std::filesystem::is_directory(assets, ec) ? " (loose folder)"
                                                                     : " (not found)";
    p->Log("lmxxf backend created: runtime " + RuntimePath(dir).string() + ", assets " + assets.string() + source);
    // One WARN per process naming the loose folder the pak won over. (0.3.4, LF-A) Worded per layout (AssetsPath):
    // DLSS5-AMD\native-game-tiled-assets is the lmxxf package layout, left there by an older install, so renaming or
    // deleting it is the advice. A native-game-tiled-assets folder beside the exe is the dlss-5-amd-project layout and
    // may belong to a separately installed mod: the line says it is not used and how to use it, never to delete it.
    static std::atomic<bool> warnedLoose { false };
    if (!ignored.empty() && !warnedLoose.exchange(true))
    {
        const bool packageLayout = ignored == dir / L"DLSS5-AMD" / L"native-game-tiled-assets";
        const std::string w =
            packageLayout ? "WARN: lmxxf: ignoring the loose asset folder " + ignored.string() +
                                " - LmxxfNrRuntime.pak is used instead (a folder left from an older install runs older kernels); "
                                "rename or delete the folder, or set AMDNR_LOOSE_ASSETS=1 to use it"
                          : "WARN: lmxxf: the asset folder " + ignored.string() +
                                " is not used - LmxxfNrRuntime.pak is used instead; the folder may belong to another mod "
                                "(dlss-5-amd-project layout) and is left as it is; set AMDNR_LOOSE_ASSETS=1 to use it";
        p->Log(w);
        LOG_WARN("{}", w);
    }
}
Backend::~Backend() {} // process lifetime, like the danielblnc host: HIP threads may outlive us

ID3D12Resource* Backend::Record(ID3D12GraphicsCommandList* cmd, const AmdPreSr::Frame& f, const AmdPreSr::Settings& cfg)
{
    std::lock_guard guard(p->lock);
    const AmdPreSr::LockOwnerMark owned(p->lockOwner); // (0.3.3.2) for Shutdown on this thread
    try
    {
        const auto t0 = std::chrono::steady_clock::now();
        ID3D12Resource* r = p->Record(cmd, f, cfg);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        if (ms > p->recMaxMs) p->recMaxMs = ms;
        p->lastRecordReturn = GetTickCount64(); // frames were flowing (Shutdown's marker rule)
        return r;
    }
    catch (const std::exception& e)
    {
        p->failed = true;
        p->Log(std::string("lmxxf backend stopped: ") + e.what());
        // Vulkan bridge, before the first answer: a lost answer (the HIP fence never signalled within
        // the bound) keeps the marker - lmxxf did not deliver on this bridge. Any other stop returned
        // here, so it did not hang; the marker goes with the real reason.
        if (p->vkMarkerLive)
        {
            if (p->answerLost)
                p->Log("lmxxf vk: the first answer never arrived - lmxxf_vk_launch.pending stays, so the next start does "
                       "not run lmxxf over the Vulkan bridge");
            else
                p->DropVkMarker(std::string("lmxxf vk: the backend stopped (") + e.what() +
                                ") - marker removed, the next start retries lmxxf");
        }
        return nullptr;
    }
}
AmdPreSr::Stats Backend::GetStats() const
{
    std::lock_guard guard(p->lock);
    AmdPreSr::Stats s {};
    s.tick = GetTickCount64();
    s.recorded = p->frames;
    s.modelFrames = p->modelFrames;
    s.completedModelFrames = p->completedForActivity.load(std::memory_order_acquire);
    s.skips = p->skips;
    s.width = p->mw;
    s.height = p->mh;
    s.compositionWhiteSource = p->whiteSource; // Fed after a RenoDX-composed answer, else None
    s.nrGpuMs = p->lastWindowHipMs; // (0.3.4, P7.10) the network's GPU ms per job, last stats window's mean; -1 none
    // Edit accumulation, for the menu's Live line (parity with danielblnc's Stats; no model-frame
    // fit on lmxxf, so detail kept stays -1 and no call is ever refused the danielblnc way).
    s.interleaving = p->statCadence > 1;
    s.accumulating = p->statAccumulating;
    if (p->statAccumulating && p->temporal)
    {
        s.carryTone = p->temporal->ChangeFraction();
        s.carryRawRefused = p->temporal->GateRefusedFraction();
    }
    return s;
}
int Backend::PendingListIndex(UINT, ID3D12CommandList* const*) const { return -1; }
void Backend::Submitting(ID3D12CommandQueue*, UINT, ID3D12CommandList* const*) {}
void Backend::Submitted(ID3D12CommandQueue* q, UINT n, ID3D12CommandList* const* lists)
{
    if (!q || !lists)
        return;
    ID3D12CommandList* const pending = p->pendingList; // cheap prefilter, confirmed under the lock
    // (0.3.4, P3) A dropped job's list arriving late (raw pointer only; one atomic load per ExecuteCommandLists otherwise).
    if (ID3D12CommandList* const dropped = p->droppedList.load(std::memory_order_acquire); dropped && dropped != pending)
    {
        for (UINT i = 0; i < n; ++i)
        {
            if (lists[i] == dropped)
            {
                p->LateArrival(dropped);
                break;
            }
        }
    }
    if (!pending)
        return;
    bool hit = false;
    for (UINT i = 0; i < n && !hit; ++i)
        hit = lists[i] == pending;
    bool wrappedList = false;
    if (!hit)
    {
        // UE5 titles: the list handed to Evaluate can reach ExecuteCommandLists through a wrapper
        // (Streamline's interposer and other proxies), or the other way round - the same list under
        // another pointer. Armed only after a frame whose raw compare never matched; the pending
        // list's identity was captured by Record (held by reference), so nothing here dereferences
        // `pending`. Only the lists being submitted - alive by definition - are queried.
        IUnknown* const wantSelf = p->pendingSelf.load();
        IUnknown* const wantKey = p->pendingKey.load();
        if (!wantSelf && !wantKey)
            return;
        for (UINT i = 0; i < n && !hit; ++i)
        {
            if (!lists[i])
                continue;
            const ComIdentity id(lists[i]);
            hit = (wantSelf && id.Self() == wantSelf) || (wantKey && id.Key() == wantKey);
        }
        if (!hit)
            return;
        wrappedList = true;
    }
    std::lock_guard guard(p->lock);
    const AmdPreSr::LockOwnerMark owned(p->lockOwner); // (0.3.3.2) for Shutdown on this thread
    if (p->pendingList != pending || p->failed || p->shutdown)
        return;
    // (0.3.3.2) The game reset this object after RecordInputs and records and submits it again: what
    // this list carries now is the game's, not our input copies. SetPending(nullptr) keeps the evidence
    // for the drop in the next Record; only a new list clears it.
    const bool resetBeforeSubmit = p->resetSeen.load() == pending;
    if (!wrappedList)
        ++p->rawSubmissions; // this game's neural lists reach ExecuteCommandLists under their own pointer
    p->SetPending(nullptr);
    if (wrappedList && !p->loggedWrappedList)
    {
        p->loggedWrappedList = true;
        p->Log("lmxxf: the neural list was submitted through a wrapper (another pointer, the same COM object); "
               "treated as the list itself (noted once)");
    }
    try
    {
        if (!p->ctx)
        {
            // First sight of the queue the game really submits the neural list on: bind the runtime
            // to it (the hint the bridge gave at creation can be a present or proxy queue). Never to
            // a queue of another D3D12 device (0.3.3.2): the next Record watches a new list.
            if (!p->QueueOnOurDevice(q))
                return;
            p->queue = q;
            if (p->vkStageLog)
                p->Log("lmxxf vk: submit - first sight of the submit queue; loading the runtime on it");
            p->Load();
            if (p->vkStageLog)
                p->Log("lmxxf vk: submit - runtime loaded");
            return;
        }
        if (!p->jobRecorded)
            return;
        if (resetBeforeSubmit)
        {
            // Launching would run the network on inputs that were never copied (the previous feed) and
            // hand its answer to the next frame. jobRecorded stays set, so the next Record drops the job
            // as a lost frame, with its history reset, exactly as for a list that never came back.
            if (!p->loggedResetSubmit)
            {
                p->loggedResetSubmit = true;
                p->Log("lmxxf: the neural list was reset and submitted again without our inputs (a discarded frame); "
                       "its job is not launched (noted once)");
            }
            return;
        }
        p->jobRecorded = false;
        // The queue the job is launched against: this one, or the bound one when this is the bound
        // queue through a wrapper (the runtime's queue contract compares COM identity, not proxies).
        ID3D12CommandQueue* launchQueue = q;
        if (q != p->queue.Get())
        {
            if (SameComObject(q, p->queue.Get()))
            {
                // The same queue under another pointer (a wrapper, as UE5 titles load them): not a
                // migration. The runtime stays bound and the launch names the bound queue.
                launchQueue = p->queue.Get();
                if (!p->loggedWrappedQueue)
                {
                    p->loggedWrappedQueue = true;
                    p->Log("lmxxf: the neural list arrived on the bound queue through a wrapper (the same COM object); "
                           "no rebind, the launch uses the bound queue (noted once)");
                }
            }
            else
            {
                // The runtime signals its fence on the queue it was created with; a list on another
                // queue would let HIP start before its input was copied. Rebind, drop this frame's job.
                // The old session is not destroyed here - this list and the old queue may still use
                // its resources - but fenced on both queues and released by a later Record
                // (RetireSession / ReleaseRetired), with no CPU wait in this hook. Never to a queue of
                // another D3D12 device (0.3.3.2): its session keeps the bound queue and the job is dropped.
                if (!p->QueueOnOurDevice(q))
                {
                    p->api.CancelUnsubmitted(p->ctx, p->job.handle);
                    ++p->skips;
                    return;
                }
                p->Log("lmxxf: the neural list moved to another queue; the runtime is rebound to it and this frame's model output is dropped");
                p->api.CancelUnsubmitted(p->ctx, p->job.handle);
                p->RetireSession(q);
                p->ctx = nullptr;
                p->queue = q;
                p->Load();
                ++p->skips;
                // Vulkan bridge, before the first answer: the next PrepareFrame warms the new session,
                // so the marker stays - unless the runtime could not come up on the new queue.
                if (p->failed && p->vkMarkerLive)
                    p->DropVkMarker("lmxxf vk: the runtime failed on the new queue (" + p->status +
                                    ") - marker removed, the next start retries lmxxf");
                return;
            }
        }
        if (p->vkStageLog && p->vkJobsLogged < 3)
            p->Log("lmxxf vk: submit - the neural list is on the queue; launching the HIP job (EnqueueHipAsync)");
        const auto te = std::chrono::steady_clock::now();
        const int32_t rc = p->api.EnqueueHipAsync(p->ctx, p->job.handle, launchQueue);
        if (p->vkStageLog)
        {
            // Over the bridge this runs inside its ExecuteCommandLists, inside the game's Evaluate:
            // a slow return means a HIP call synchronised there. Logged for the first 3 launches and
            // for any launch over 50 ms.
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - te).count();
            const bool early = p->vkLaunches < 3;
            if (early)
                ++p->vkLaunches;
            if (early || ms > 50.0)
            {
                char line[192];
                std::snprintf(line, sizeof line, "lmxxf vk: submit - EnqueueHipAsync returned %d in %.2f ms%s", static_cast<int>(rc), ms,
                              ms > 50.0 ? " (over 50 ms: a HIP call blocked inside the bridge's ExecuteCommandLists)" : "");
                p->Log(line);
            }
        }
        if (rc != LMXXF_NR_OK)
        {
            const std::string err = p->LastError();
            p->Log("lmxxf EnqueueHipAsync failed: " + err);
            p->NoteFatal(err);
            p->api.CancelUnsubmitted(p->ctx, p->job.handle);
            ++p->skips;
            // Vulkan bridge: the launch returned without a job in flight; nothing is outstanding.
            if (p->vkMarkerLive)
                p->DropVkMarker("lmxxf vk: EnqueueHipAsync failed (" + err + ") - marker removed, the next start retries lmxxf");
            return;
        }
        p->inFlight = true;
        if (p->jobGraced)
        {
            // (0.3.4, P3) A graced job's list, after the next Record: late, and launched thanks to the grace.
            ++p->statLate;
            p->statLateMax = (std::max)(p->statLateMax, static_cast<unsigned>(Lmxxf::FramesLate(p->frames, p->lastFedFrame)));
        }
    }
    catch (const std::exception& e)
    {
        p->failed = true;
        p->Log(std::string("lmxxf backend stopped at submission: ") + e.what());
        if (p->vkMarkerLive)
            p->DropVkMarker(std::string("lmxxf vk: the backend stopped at submission (") + e.what() +
                            ") - marker removed, the next start retries lmxxf");
    }
}
void Backend::ListReset(ID3D12CommandList* list, bool ownBridge)
{
    // Lock-free (see AmdPreSr.h): the runtime resets lists of its own while `lock` is held. Only the
    // pending list counts; Submitted and the drop in Record read the evidence under the lock.
    if (!list || list != p->pendingList.load(std::memory_order_acquire))
        return;
    if (ownBridge)
        p->resetCertain.store(true, std::memory_order_relaxed);
    p->resetSeen.store(list, std::memory_order_release);
}
void Backend::TraceBoundary(const std::string& reason)
{
    // (0.3.3.2) Never waits for the recording lock: this comes from the game's context release, which can
    // be on another thread (or at exit) while Record or Submitted hold it. Busy: the reason alone.
    std::unique_lock<std::mutex> guard(p->lock, std::try_to_lock);
    std::ofstream out(p->directory / L"lmxxf_backend.log", std::ios::app);
    if (!guard.owns_lock())
    {
        out << GetTickCount64() << " boundary: " << reason << " (backend busy, no snapshot)\n";
        return;
    }
    out << GetTickCount64() << " boundary: " << reason << " (frames " << p->frames << ", in flight " << (p->inFlight ? 1 : 0) << ")\n";
}
bool Backend::Ready()
{
    std::lock_guard guard(p->lock);
    return p->ctx != nullptr && !p->failed;
}
bool Backend::Shutdown()
{
    // (0.3.3.2) Exit on the thread that holds `lock` inside Record or Submitted (a crash handler calling
    // ExitProcess from inside our recording): locking again would throw or deadlock. Left to the process.
    if (p->lockOwner.load(std::memory_order_relaxed) == GetCurrentThreadId())
        return false;
    // Process exit (C1-A, 0.3.3.2 rebuild): at most about 2 s for the lock, then the backend is left to the process.
    // A Record or Submitted holding it (a stalled queue, a crash handler exiting on the recording thread) no longer
    // holds the exit up; MSVC's try_lock also answers false, not a throw, when this thread owns the lock.
    std::unique_lock<std::mutex> guard(p->lock, std::defer_lock);
    for (const ULONGLONG start = GetTickCount64(); !guard.try_lock();)
    {
        if (GetTickCount64() - start >= 2000)
            return false;
        Sleep(1);
    }
    if (p->shutdown)
        return true;
    p->shutdown = true;
    // (0.3.3.2, C1-B) Vulkan bridge: a clean quit before the first answer. Frames were still flowing (the
    // last Record returned under 2 s ago) and the answer was not lost, so this session did not freeze in
    // the warm-up - the marker would only turn lmxxf off at the next start. Kept when no Record came for
    // 2 s (a render thread stuck in the bridge's previous-frame wait) or the lock could not be had; Task
    // Manager's end-task never reaches this hook, so a real freeze still leaves it.
    if (p->vkMarkerLive && !p->answerLost && p->lastRecordReturn != 0 && GetTickCount64() - p->lastRecordReturn < 2000)
        p->DropVkMarker("lmxxf vk: the game exited cleanly before the first answer - lmxxf_vk_launch.pending removed");
    // Over the Vulkan bridge the runtime's drain Signal would sit behind a queue Wait on a fence only
    // the game's next vkQueueSubmit signals (30 s at exit): skipped, and the session is left to the
    // process, whose lifetime it has anyway. Sessions retired by a queue migration and not released
    // yet are left alone too: the process is exiting. On D3D12 the host's own queue drain runs, bounded
    // at 2 s (C1-A), not the runtime's Drain (up to 30 s); the session is not destroyed at exit.
    if (p->ctx && !p->answerLost && !p->vkBridge)
        p->DrainQueue(/*atExit*/ true);
    return true;
}
void Backend::InvalidateHistory() { p->resetRequested.store(true); }
void Backend::RequestCapture(unsigned) {}
std::string Backend::Status() const
{
    std::lock_guard guard(p->lock);
    // (0.3.3.2) a poisoned session: why it stopped, not the last "session is poisoned" line
    if (!p->fatalError.empty())
        return "lmxxf stopped after an error: " + p->fatalError;
    return p->status;
}
UINT64 Backend::RecordedFrames() const
{
    std::lock_guard guard(p->lock);
    return p->frames;
}
void Backend::NoteLine(const std::string& line)
{
    // The menu thread (NR on/off note, 0.3.3.2): a plain append, never the status line, and no wait on
    // the recording path - under the lock when it is free so the line cannot split a Record line.
    std::unique_lock<std::mutex> guard(p->lock, std::try_to_lock);
    std::ofstream out(p->directory / L"lmxxf_backend.log", std::ios::app);
    out << GetTickCount64() << " " << line << '\n';
}
bool Backend::Stopped() const
{
    // Refused or failed to load/create (failed), or the runtime session poisoned by a fatal error
    std::lock_guard guard(p->lock);
    return p->failed || !p->fatalError.empty();
}
} // namespace Lmxxf
