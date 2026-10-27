// Copyright (c) 2026 3zwr1 (AMDNR) - the AMD integration. Part of AMDNR (GPL-3.0; see Licenses/AMDNR_NOTICE.txt).
// SPDX-License-Identifier: GPL-3.0-or-later
// Portions: see below (CREDITS). The composition tail and its helpers are ported from the dlssnr.hlsl
// lineage (Dagherbou -> wilsjo2, GPL-3.0) and from RenoDX by clshortfuse (MIT); those portions stay theirs.
#pragma once
#include <d3d12.h>
#include <d3dcompiler.h>
#include "SystemCompiler.h"
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <string>
namespace AmdPreSr {
// RENODX COLOUR COMPOSITION ON THE AMD PATH ([DlssNr] AmdComposition = 1): the proxy-free tail of
// the composition, one HLSL string for both runtimes. danielblnc runs it through class NrCompose
// below (in place on its answer); lmxxf through its own Pass with NRCOMPOSE_DELTA (a delta its
// residual lifts). AmdComposition 0 (Classic) never reaches this file.
//
// CREDITS. The maths here is not AMDNR's, and this says whose it is.
//  - RenoDX by clshortfuse (MIT, https://github.com/clshortfuse/renodx): the two-branch luminance
//    ratio and the hue step (renodx::tonemap::config::UpgradeToneMap and renodx::color::correct::Hue,
//    src/shaders/tonemap.hlsl) that make the answer this pass receives - on lmxxf the runtime's
//    codec computes them against the exact proxy the network saw, on danielblnc the closed runtime
//    composes its own - and the D65 / Hunt-Pointer-Estevez neutral-axis gamut compression used
//    below (ClampAp1, from RenoDX's DLSS 5 addon). See Licenses/RenoDX_ATTRIBUTION.txt.
//  - lmxxf (MIT): the reconstruction of NVIDIA's "mode 1" codec, including the blend between a
//    luminance-only result and the model's full colour (native_codec_decode.hlsl:153-155), which is
//    the Colour strength blend below. On lmxxf the head runs inside that codec.
//  - The dlssnr.hlsl lineage this tail is ported from (shaders/dlssnr/precompile/dlssnr.hlsl:894-1034;
//    OptiScaler -> Dagherbou -> wilsjo2 -> this build): the 1/512 ratio floor, the power amplification
//    above strength 1, the two-sided guard, the OkLab chroma boost, and wilsjo2's SkinColourWeight
//    with the skin / environment final edit. The helpers are copied from that file verbatim.
//  - OkLab: Bjorn Ottosson's published constants.
//  - AMDNR (3zwr1): only the AMD integration - this shared pass and its use on both runtimes, the
//    white-point handling (where W enters, the estimator), the lmxxf matched lift - and the
//    RDNA 3 backend.
//
// WHAT IT DOES. O is the pre-model original and A the runtime's answer, both linear BT.709 in the
// SAME source units: on danielblnc what the runtime was handed (NGX colour, or the decoded copy)
// and the closed runtime's own composed picture; on lmxxf the fed copy (raw x e, paper white 1)
// and RenoDX's `upgraded` from the runtime at strengths 1/1. W is white in those units. The tail
// of dlssnr.hlsl then runs on them: Detail strength T lerps toward the answer and above 1 raises
// the luminance ratio to the power T; that ratio against the original, floored by W/512 on both
// sides, is bounded two-sided by the Highlight guard G; Colour strength Cs blends between the
// original's hue at the bounded light and the answer's colour, and above 1 scales OkLab chroma by
// Cs with the neutral-axis compression pulling it back into gamut; the optional skin /
// environment edit attenuates the result. This bounds the model's answer against the original
// BEFORE each runtime's usual residual controls, which still run after it as they do today
// (residual strength and limit, lmxxf's edit shaper, the Look, stability, sharpen): the guard is
// not the final bound on the picture.
//
// NO DIVIDE BY W, EXCEPT AROUND ClampAp1. Every step is homogeneous of degree 1 in (O, A) -
// ratios, lerps, a clamp of a ratio, OkLab(kx) = k^(1/3) OkLab(x) with FromOkLab(k^(1/3) y) =
// k FromOkLab(y), a neutral that scales with Y - so dlssnr.hlsl's divide by the white point and
// multiply back (hlsl:777, 1012) is replaced by W in three places: the ratio floor W/512
// (hlsl:948), the empty-answer threshold 1e-5 W (hlsl:897) and the skin mask's input
// sRGB(saturate(O/W)) (hlsl:1018). ClampAp1 is the exception: its tests (luminance > 1e-8,
// SafeDivide's 1e-8) are absolute, and on a colour with a negative channel and a luminance near 0
// one side of them clips the channel while the other pulls the colour to a near-black neutral (a
// dark blue going black; measured at W = 89 and 4096). So the two calls dlssnr.hlsl makes in its
// normalised units - on the answer (hlsl:222) and after the chroma boost (hlsl:1000) - run here on
// colour / W and multiply back (W >= 1/16384, and exact at W = 1); the skin block's (hlsl:1031)
// runs in source units in both files. Apart from that, a wrong W cannot shift tone or colour: it
// moves the dark floor and what the skin mask selects.
//
// PIXEL RULES. The ratio, the blend and the mask are computed on O+ = max(O, 0), as NVIDIA
// composes max(frame, 0) (hlsl:671) and lmxxf's decode max(source, 0); the signed O is kept only
// as the skin block's base chroma and its detail-0 / colour-0 endpoint, where dlssnr.hlsl keeps
// originalSample. Only a non-finite O or A leaves a pixel uncomposed: the default entry keeps the
// runtime's answer exactly there, NRCOMPOSE_DELTA flags it (alpha 1) so lmxxf's residual falls
// back to today's net/e - raw for that pixel (an FP16 overflow of fed = raw x e lands here).
//
// EXACT AT NEUTRAL. pow(x, 1.0) compiles to exp2(log2 x) and lerp(a, b, 1) to mad(1, b - a, a);
// neither is exact, and dlssnr.hlsl's "bit-identical at strength 1" (hlsl:959-961, 985-986)
// relies on them being so. Here each such step sits behind an explicit branch (T < 1, T > 1,
// b != rho, Cs < 1, Cs > 1, skin), so at T = 1, Cs = 1, skin off and rho inside [1/G, G] the
// answer comes back bit for bit (a finite, non-negative answer inside the FP16 range).
//
// ROOT CONSTANTS, 12 dwords at b0 - the same packing as lmxxf's Pass::Run {w, h, mode, k, f0..f3,
// g0..g3}: { uint w, h, flags; float T; float Cs, G, skinDetail, skinColour; float envDetail,
// envColour, whiteConst, pad }. flags: 1 SKIN (the skin / environment final edit), 2 reserved (a
// mask preview, not in this change), 4 USE_TEXEL (default entry: W = 1/e from t1's texel (0,0),
// whiteConst when that is not a usable exposure), 8 RESET (estimator: start again from 4).
// pad: lmxxf's highlight chroma guard switch, read only by NRCOMPOSE_DELTA (GuardAnswer; 1 = on,
// AMDNR 0.3.3). danielblnc's default entry and the estimator never read it. danielblnc's own guard
// (AMDNR 0.3.4, [DlssNr] AmdDanielHighlightGuard, off by default) reads it only in its separate
// permutation, AmdNrComposeDanielGuardHlsl below, which is appended to this text at compile time.
//
// ENTRIES. One root signature: a table {SRV t0, SRV t1, UAV u0}, the 12 constants, s0 linear clamp.
//   default            danielblnc, in place. t0 Org = the pre-model copy (FP16, w x h), t1 Expo =
//                      the exposure texel (R32_FLOAT), u0 Dst = the answer (FP16, w x h), read and
//                      written by the same thread. Writes Tail(O, A) with the ORIGINAL's alpha, as
//                      NVIDIA (hlsl:1049) and lmxxf's decode write.
//   NRCOMPOSE_DELTA    lmxxf. t0 Ans = the network's answer, t1 Org = fed, u0 Dst = an FP16 delta,
//                      all at the model size; W = whiteConst (1: fed units). Writes (Tail - o, 0),
//                      or (0, 0, 0, 1) where the pixel was not composed.
//   NRCOMPOSE_ESTIMATE danielblnc's white point in a title that publishes no exposure texture. One
//                      thread; t0 Org, u0 St = 4x1 R32_FLOAT: [0] e, [1] the mean measured, [2] the
//                      running e, [3] an initialised marker. W = 1/St[0] through the default entry's
//                      USE_TEXEL. The same encode and step as lmxxf's AutoExposureShader
//                      (AutoExposureHlsl.h) with danielblnc's own law - it starts at 4 and holds the
//                      encoded mean at 0.500 (its log: "encoded mean 0.000 -> exposure 4.0000", then
//                      means 0.499-0.526): band 0.02, a 1.5x cap on every update, e in [1/256, 16384],
//                      and an empty (black) measurement holds. It measures what the runtime is handed
//                      (the pre-model copy), not the proxy-encoded slot. Because the tail is
//                      homogeneous, this W only moves the dark floor (W/512) and the skin mask.
inline constexpr char AmdNrComposeHlsl[] = "\ncbuffer P : register(b0) { uint w; uint h; uint flags; float T; float Cs; float G; float skinDetail; float skinColour; float envDetail; float envColour; float whiteConst; float pad; }\nSamplerState samp : register(s0);\n#if defined(NRCOMPOSE_ESTIMATE)\nTexture2D<float4> Org : register(t0);\nRWTexture2D<float> St : register(u0);\n#elif defined(NRCOMPOSE_DELTA)\nTexture2D<float4> Ans : register(t0);\nTexture2D<float4> Org : register(t1);\nRWTexture2D<float4> Dst : register(u0);\n#else\nTexture2D<float4> Org : register(t0);\nTexture2D<float> Expo : register(t1);\nRWTexture2D<float4> Dst : register(u0);\n#endif\n\n#if defined(NRCOMPOSE_ESTIMATE)\n// danielblnc's own law (the header comment): target 0.5, band 0.02, start 4, 1.5x per update.\nstatic const float kTarget = 0.5, kBand = 0.02, kStepCap = 1.5, kStartExposure = 4.0;\n// lmxxf's AutoExposureShader encode, unchanged: paper white 1, shoulder above 0.75, sRGB curve.\nfloat enc(float v) {\n v = max(v, 0.0);\n float sh = 0.75 + 0.25 * (1.0 - exp(-5.770780 * (v - 0.75)));\n v = saturate(v <= 0.75 ? v : sh);\n return v <= 0.0031308 ? v * 12.92 : 1.055 * pow(v, 1.0 / 2.4) - 0.055;\n}\n[numthreads(1,1,1)] void main(uint3 tid : SV_DispatchThreadID) {\n float prev = St[uint2(2,0)];\n bool first = (flags & 8u) != 0u || St[uint2(3,0)] != 1.0 || !(isfinite(prev) && prev >= 1.0 / 256.0 && prev <= 16384.0);\n if (first) prev = kStartExposure;\n float sum = 0.0; float n = 0.0;\n for (uint y = 0u; y < 36u; ++y) {\n  for (uint x = 0u; x < 64u; ++x) {\n   float2 uv = (float2(float(x), float(y)) + 0.5) / float2(64.0, 36.0);\n   float3 c = Org.SampleLevel(samp, uv, 0).rgb;\n   if (!all(isfinite(c))) continue;\n   float l = dot(max(c, 0.0), float3(0.2126, 0.7152, 0.0722)) * prev;\n   sum += enc(l); n += 1.0;\n  }\n }\n // Nothing measured (no finite sample, or a black frame: a load screen) holds the exposure, as\n // danielblnc's runtime does; otherwise the capped step toward the target outside the band.\n float mean = n > 0.0 ? sum / n : 0.0;\n float step = 1.0;\n if (mean > 0.0 && abs(mean - kTarget) > kBand) step = clamp(pow(kTarget / max(mean, 1e-4), 2.4), 1.0 / kStepCap, kStepCap);\n float e = clamp(prev * step, 1.0 / 256.0, 16384.0);\n St[uint2(0,0)] = e; St[uint2(1,0)] = mean; St[uint2(2,0)] = e; St[uint2(3,0)] = 1.0;\n}\n#else\n// ---- helpers, verbatim from shaders/dlssnr/precompile/dlssnr.hlsl (59-152, 166-189, 286, 290-294)\nfloat SanitizeFinite(float v, float fallback) { return isfinite(v) ? v : fallback; }\n\n// Approximate skin-colour selection, not a face/skin segmentation network. Warm\n// materials may be selected and coloured lighting can hide skin.\nfloat SkinColourWeight(float3 rgb)\n{\n    rgb = saturate(rgb);\n    float y = dot(rgb, float3(0.299, 0.587, 0.114));\n    float cb = (rgb.b - y) * 0.564 + 0.5;\n    float cr = (rgb.r - y) * 0.713 + 0.5;\n    float2 distance = (float2(cb, cr) - float2(0.405, 0.600)) / float2(0.090, 0.110);\n    float chroma = max(rgb.r, max(rgb.g, rgb.b)) - min(rgb.r, min(rgb.g, rgb.b));\n    return (1.0 - smoothstep(0.55, 1.35, length(distance))) * smoothstep(0.02, 0.10, chroma);\n}\n\nfloat3 SanitizeFinite3(float3 v, float3 fallback)\n{\n    return float3(SanitizeFinite(v.x, fallback.x), SanitizeFinite(v.y, fallback.y),\n                  SanitizeFinite(v.z, fallback.z));\n}\n\nfloat SafeDivide(float numerator, float denominator, float fallback)\n{\n    return abs(denominator) > 1e-8 ? numerator / denominator : fallback;\n}\n\n// Hunt-Pointer-Estevez LMS over linear BT.709, carrying the fixed D65 adaptation state the\n// compression is defined against. The signal itself never leaves BT.709.\nfloat3 LMSToBT709(float3 color)\n{\n    const float3x3 m = { 5.62059812, -4.57145756, 0.15577924,\n                         -1.15555585, 2.25800438, -0.15415806,\n                         0.03059913, -0.19018011, 1.06820532 };\n    return mul(m, color);\n}\n\nfloat3 BT709ToLMS(float3 color)\n{\n    const float3x3 m = { 0.30569589, 0.62271286, 0.04528636,\n                         0.15776262, 0.76968599, 0.08807030,\n                         0.01933082, 0.11919478, 0.95053215 };\n    return mul(m, color);\n}\n\n// The neutral colour of the same luminance as what is being compressed -- the point everything is\n// pulled toward, so that pulling changes saturation and not hue.\nfloat3 D65NeutralBT709(float3 adaptiveStateLms, float luminance)\n{\n    float3 d65 = LMSToBT709(max(adaptiveStateLms, 1e-8));\n    float d65Y = max(dot(d65, float3(0.2126, 0.7152, 0.0722)), 1e-8);\n    return d65 * (luminance / d65Y);\n}\n\n// The largest scale toward the neutral axis that leaves no channel negative. One for a colour that\n// was already representable, which is why this is safe to run on every pixel.\nfloat GamutCompressionScale(float3 color, float3 adaptiveStateLms)\n{\n    color = SanitizeFinite3(color, float3(0.0, 0.0, 0.0));\n\n    const float y = dot(color, float3(0.2126, 0.7152, 0.0722));\n\n    if (!(y > 1e-8))\n        return 1.0;\n\n    const float3 neutral = D65NeutralBT709(adaptiveStateLms, y);\n    float scale = 1.0;\n\n    if (color.r < 0.0 && neutral.r > color.r)\n        scale = min(scale, SafeDivide(neutral.r, neutral.r - color.r, 1.0));\n\n    if (color.g < 0.0 && neutral.g > color.g)\n        scale = min(scale, SafeDivide(neutral.g, neutral.g - color.g, 1.0));\n\n    if (color.b < 0.0 && neutral.b > color.b)\n        scale = min(scale, SafeDivide(neutral.b, neutral.b - color.b, 1.0));\n\n    return saturate(SanitizeFinite(scale, 1.0));\n}\n\nfloat3 ClampAp1(float3 color)\n{\n    const float3 adaptiveStateLms = BT709ToLMS(float3(0.18, 0.18, 0.18));\n    const float scale = GamutCompressionScale(color, adaptiveStateLms);\n\n    // Nothing was out of gamut. Leave the colour exactly as it arrived.\n    if (scale >= 1.0)\n        return color;\n\n    const float y = dot(color, float3(0.2126, 0.7152, 0.0722));\n    const float3 neutral = D65NeutralBT709(adaptiveStateLms, y);\n\n    return SanitizeFinite3(neutral + (color - neutral) * scale, max(neutral, 0.0));\n}\n\nfloat3 CbrtSigned(float3 v) { return sign(v) * pow(abs(v), 1.0 / 3.0); }\n\nfloat3 ToOkLab(float3 color)\n{\n    const float3x3 rgb_to_lms = { 0.4122214708, 0.5363325363, 0.0514459929,\n                                  0.2119034982, 0.6806995451, 0.1073969566,\n                                  0.0883024619, 0.2817188376, 0.6299787005 };\n    const float3x3 lms_to_lab = { 0.2104542553, 0.7936177850, -0.0040720468,\n                                  1.9779984951, -2.4285922050, 0.4505937099,\n                                  0.0259040371, 0.7827717662, -0.8086757660 };\n    return mul(lms_to_lab, CbrtSigned(mul(rgb_to_lms, color)));\n}\n\nfloat3 FromOkLab(float3 lab)\n{\n    const float3x3 lab_to_lms = { 1.0, 0.3963377774, 0.2158037573,\n                                  1.0, -0.1055613458, -0.0638541728,\n                                  1.0, -0.0894841775, -1.2914855480 };\n    const float3x3 lms_to_rgb = { 4.0767416621, -3.3077115913, 0.2309699292,\n                                  -1.2684380046, 2.6097574011, -0.3413193965,\n                                  -0.0041960863, -0.7034186147, 1.7076147010 };\n    float3 lms = mul(lab_to_lms, lab);\n    return mul(lms_to_rgb, lms * lms * lms);\n}\n\nstatic const float3 kLuma = float3(0.2126, 0.7152, 0.0722);\n\nfloat3 LinearToSrgb(float3 v)\n{\n    v = saturate(v);\n    return lerp(v * 12.92, 1.055 * pow(max(v, 1e-8), 1.0 / 2.4) - 0.055, step(0.0031308, v));\n}\n// ---- the tail (dlssnr.hlsl:894-1034 in source units; the header comment explains W and the branches)\nbool Tail(float3 Og, float3 Ag, float W, out float3 R)\n{\n    R = Ag;\n    if (!all(isfinite(Og)) || !all(isfinite(Ag)))\n        return false;\n    const float3 O = max(Og, 0.0);                        // O+: ratio, blend and mask\n    // The answer as `upgraded` (hlsl:222), clamped in dlssnr's units (colour / W, the header's NO\n    // DIVIDE BY W). Skipped with no negative channel: ClampAp1 returns such a colour unchanged, and\n    // Ag / W * W would not, so the neutral identity stays bit-exact.\n    const float3 A = any(Ag < 0.0) ? ClampAp1(Ag / W) * W : Ag;\n    const float original"
"Luma = dot(O, kLuma);\n    const float ratioFloor = W * (1.0 / 512.0);           // hlsl:948\n    float3 upgraded = dot(A, kLuma) <= 1e-5 * W ? O : A;  // an empty answer hands the frame back (hlsl:897-902)\n    if (T < 1.0)\n        upgraded = lerp(O, upgraded, saturate(T));        // hlsl:929\n    const float lumaRatio = (dot(upgraded, kLuma) + ratioFloor) / (originalLuma + ratioFloor); // hlsl:949\n    const float amplified = T > 1.0 ? pow(max(lumaRatio, 1e-6), T) : lumaRatio;                // hlsl:962\n    const float guard = max(G, 1.0);\n    const float boundedRatio = clamp(amplified, 1.0 / guard, guard);                           // hlsl:982-983\n    if (boundedRatio != lumaRatio)\n        upgraded *= boundedRatio / max(lumaRatio, 1e-6);  // hlsl:987\n    float3 result = Cs >= 1.0 ? upgraded : lerp(O * boundedRatio, upgraded, saturate(Cs));     // hlsl:997\n    if (Cs > 1.0) // hlsl:999-1000, in dlssnr's units (colour / W) for ClampAp1's absolute tests\n        result = ClampAp1(FromOkLab(float3(1.0, Cs, Cs) * ToOkLab(max(result, 0.0) / W))) * W;\n    if ((flags & 1u) != 0u)\n    {\n        // hlsl:1014-1031, already in source units there. The mask classifies the untouched frame\n        // as the network was shown it.\n        const float mask = SkinColourWeight(LinearToSrgb(saturate(O / W)));\n        const float detail = lerp(envDetail, skinDetail, mask);\n        const float colour = lerp(envColour, skinColour, mask);\n        const float baseY = dot(O, kLuma);\n        const float editedY = dot(max(result, 0.0), kLuma);\n        const float wantedY = lerp(baseY, editedY, detail);\n        // dlssnr.hlsl's originalSample is max(frame, 0) (hlsl:671-674), so the base chroma is taken from O+ as\n        // well: with the signed original a negative channel pulled the pixel below wantedY.\n        const float3 baseChroma = O / max(baseY, 1e-6);\n        const float3 editedChroma = result / max(editedY, 1e-6);\n        if (detail == 0.0 && colour == 0.0)\n            result = Og;\n        else if (detail != 1.0 || colour != 1.0)\n            result = ClampAp1(lerp(baseChroma, editedChroma, colour) * wantedY);\n    }\n    if (!all(isfinite(result)))\n        return false;\n    R = clamp(result, 0.0, 65504.0);                      // hlsl:1049's max(., 0), and the FP16 range\n    return true;\n}\n\n#if defined(NRCOMPOSE_DELTA)\n// lmxxf's highlight chroma guard ([DlssNr] AmdLmxxfHighlightChromaGuard; the 12th constant, `pad`,\n// is 1 when it is on), run on the network's ANSWER before the tail: where fed passes the codec's\n// shoulder (max channel from 0.75, fully at 1.5) the answer takes fed's colour at its own light, and\n// the tail then composes that - so Composition detail / colour still act on those pixels.\nfloat3 GuardAnswer(float3 a, float3 o)\n{\n    if (!(pad > 0.5))\n        return a;\n    const float wt = smoothstep(0.75, 1.5, max(o.r, max(o.g, o.b)));\n    const float ya = dot(a, kLuma), yo = dot(o, kLuma);\n    if (!(wt > 0.0) || !all(isfinite(a)) || !all(isfinite(o)) || !(ya > 0.0) || !(yo > 1e-6))\n        return a;\n    const float3 g = o * (ya / yo);\n    return all(isfinite(g)) ? lerp(a, g, wt) : a;\n}\n[numthreads(8,8,1)] void main(uint3 tid : SV_DispatchThreadID) {\n if (tid.x >= w || tid.y >= h) return;\n const int2 p = int2(tid.xy);\n const float3 o = Org.Load(int3(p, 0)).rgb;\n float3 r;\n if (!Tail(o, GuardAnswer(Ans.Load(int3(p, 0)).rgb, o), max(whiteConst, 1e-4), r)) { Dst[p] = float4(0.0, 0.0, 0.0, 1.0); return; }\n const float3 d = r - o;\n Dst[p] = all(isfinite(d)) ? float4(d, 0.0) : float4(0.0, 0.0, 0.0, 1.0);\n}\n#else\nfloat WhiteOf() {\n float white = max(whiteConst, 1e-4);\n if ((flags & 4u) != 0u) {\n  const float e = Expo.Load(int3(0, 0, 0)).r;\n  if (isfinite(e) && e > 0.0) white = 1.0 / clamp(e, 1.0 / 4096.0, 16384.0);\n }\n return white;\n}\n[numthreads(8,8,1)] void main(uint3 tid : SV_DispatchThreadID) {\n if (tid.x >= w || tid.y >= h) return;\n const int2 p = int2(tid.xy);\n const float4 o = Org.Load(int3(p, 0));\n float3 r;\n if (!Tail(o.rgb, Dst[p].rgb, WhiteOf(), r)) return; // uncomposed: the runtime's answer stays exactly as it is\n Dst[p] = float4(r, o.a);\n}\n#endif\n#endif\n";
// DANIELBLNC HIGHLIGHT CHROMA GUARD ([DlssNr] AmdDanielHighlightGuard, default off; AMDNR 0.3.4, own code). The
// same idea as lmxxf's guard above (GuardAnswer, and LmxxfBackend.cpp's ResidualShader): where the original passes
// a codec shoulder, the answer keeps the light the runtime gave the pixel and takes the original's colour. On
// danielblnc the original is the pre-model copy in the units the runtime was handed and W = 1/e is the white of
// those units (WhiteOf: the title's exposure texel, or NrCompose::Estimate's), so the weight is
// smoothstep(0.75, 1.5, max(O+) / W) = smoothstep(0.75, 1.5, max(O+) x e) and the target O+ x Y(A) / Y(O+). E is a
// proxy: danielblnc's own codec is closed, which is why this is an A/B key and not a default.
// Two entries: compose_guard (RenoDX: the default entry's work on the guarded answer) and guard_only (Classic: the
// guard alone, in place, before the edit shaper). This text is appended to AmdNrComposeHlsl and compiled only with
// NRCOMPOSE_DANIEL_GUARD, on first use and only with the key on (NrCompose::EnsureGuard), so the default entry, the
// estimator and lmxxf's NRCOMPOSE_DELTA still compile from AmdNrComposeHlsl alone, byte for byte as before.
inline constexpr char AmdNrComposeDanielGuardHlsl[] = R"(
#if defined(NRCOMPOSE_DANIEL_GUARD) && !defined(NRCOMPOSE_DELTA) && !defined(NRCOMPOSE_ESTIMATE)
// a = the runtime's answer, o = the pre-model copy, both in source units; W = their white (1/e). The answer
// unchanged wherever the weight is 0, a value is not finite, the answer has no light or the original none to give.
float3 DanielGuardAnswer(float3 a, float3 o, float W) {
 if (!(pad > 0.5) || !all(isfinite(a)) || !all(isfinite(o)) || !(W > 0.0)) return a;
 const float3 op = max(o, 0.0);
 const float wt = smoothstep(0.75, 1.5, max(op.r, max(op.g, op.b)) / W);
 const float ya = dot(a, kLuma), yo = dot(op, kLuma);
 if (!(wt > 0.0) || !(ya > 0.0) || !(yo > 1e-6 * W)) return a;
 const float3 g = min(lerp(a, op * (ya / yo), wt), 65504.0);
 return all(isfinite(g)) ? g : a;
}
// RenoDX with the guard: the default entry, composing the guarded answer.
[numthreads(8,8,1)] void compose_guard(uint3 tid : SV_DispatchThreadID) {
 if (tid.x >= w || tid.y >= h) return;
 const int2 p = int2(tid.xy);
 const float4 o = Org.Load(int3(p, 0));
 const float white = WhiteOf();
 float3 r;
 if (!Tail(o.rgb, DanielGuardAnswer(Dst[p].rgb, o.rgb, white), white, r)) return; // uncomposed: the answer stays
 Dst[p] = float4(r, o.a);
}
// Classic with the guard: the guard alone, in place on the answer (its alpha kept).
[numthreads(8,8,1)] void guard_only(uint3 tid : SV_DispatchThreadID) {
 if (tid.x >= w || tid.y >= h) return;
 const int2 p = int2(tid.xy);
 const float4 a = Dst[p];
 const float3 g = DanielGuardAnswer(a.rgb, Org.Load(int3(p, 0)).rgb, WhiteOf());
 if (any(g != a.rgb)) Dst[p] = float4(g, a.a);
}
#endif
)";
// The danielblnc side: the compose and estimate dispatches, self-contained like DetailColourMix
// (own root signature, heap and PSOs), recorded on the render queue. Two dispatches per Record,
// so 16 descriptor regions (DetailColourMix's 5 assume one) cover 8 frames in flight.
class NrCompose {
 using Res = Microsoft::WRL::ComPtr<ID3D12Resource>;
 static constexpr UINT kRegions = 16;     // descriptor regions rotated per dispatch
 static constexpr UINT kPerRegion = 3;    // t0, t1, u0
 Microsoft::WRL::ComPtr<ID3D12Device> device;
 Microsoft::WRL::ComPtr<ID3D12RootSignature> root;
 Microsoft::WRL::ComPtr<ID3D12PipelineState> composePipeline, estimatePipeline;
 // (0.3.4) danielblnc's highlight chroma guard: built by EnsureGuard only, never with the key off.
 Microsoft::WRL::ComPtr<ID3D12PipelineState> composeGuardPipeline, guardOnlyPipeline;
 Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> heap;
 Res state; // the estimator's 4x1 R32_FLOAT state; rests in NON_PIXEL_SHADER_RESOURCE
 UINT region = 0, stride = 0;
 static void Check(HRESULT hr, const char* what) {
  if (FAILED(hr)) throw std::runtime_error(std::string("AMD compose D3D12 error (") + what + ") " + std::to_string((UINT)hr));
 }
 static void Barrier(ID3D12GraphicsCommandList* c, ID3D12Resource* r, D3D12_RESOURCE_STATES a, D3D12_RESOURCE_STATES b) {
  if (a == b || !r) return;
  D3D12_RESOURCE_BARRIER v {}; v.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  v.Transition = { r, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, a, b }; c->ResourceBarrier(1, &v);
 }
 // A caller whose answer is already a UAV gets UAV barriers instead of transitions, so a write
 // before this pass and a read after it are still ordered against the in-place dispatch.
 static void UavBarrier(ID3D12GraphicsCommandList* c, ID3D12Resource* r) {
  D3D12_RESOURCE_BARRIER v {}; v.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV; v.UAV.pResource = r; c->ResourceBarrier(1, &v);
 }
 void Build(const D3D_SHADER_MACRO* defines, const char* name, Microsoft::WRL::ComPtr<ID3D12PipelineState>& pso) {
  Microsoft::WRL::ComPtr<ID3DBlob> b, e;
  const HRESULT hr = DlssNr::SysCompiler::Compile(AmdNrComposeHlsl, sizeof(AmdNrComposeHlsl), name, defines, nullptr,
                                                  "main", "cs_5_0", 0, 0, &b, &e);
  if (FAILED(hr))
   throw std::runtime_error(std::string("AMD shader '") + name + "' failed to compile: " +
                            (e ? std::string(static_cast<const char*>(e->GetBufferPointer()), e->GetBufferSize()) : std::string("?")));
  D3D12_COMPUTE_PIPELINE_STATE_DESC pd {}; pd.pRootSignature = root.Get(); pd.CS = { b->GetBufferPointer(), b->GetBufferSize() };
  Check(device->CreateComputePipelineState(&pd, IID_PPV_ARGS(&pso)), name);
 }
 // (0.3.4) Build's twin for a source other than AmdNrComposeHlsl alone and an entry other than main.
 void BuildEntry(const std::string& source, const D3D_SHADER_MACRO* defines, const char* name, const char* entry,
                 Microsoft::WRL::ComPtr<ID3D12PipelineState>& pso) {
  Microsoft::WRL::ComPtr<ID3DBlob> b, e;
  const HRESULT hr = DlssNr::SysCompiler::Compile(source.data(), source.size(), name, defines, nullptr,
                                                  entry, "cs_5_0", 0, 0, &b, &e);
  if (FAILED(hr))
   throw std::runtime_error(std::string("AMD shader '") + name + "' failed to compile: " +
                            (e ? std::string(static_cast<const char*>(e->GetBufferPointer()), e->GetBufferSize()) : std::string("?")));
  D3D12_COMPUTE_PIPELINE_STATE_DESC pd {}; pd.pRootSignature = root.Get(); pd.CS = { b->GetBufferPointer(), b->GetBufferSize() };
  Check(device->CreateComputePipelineState(&pd, IID_PPV_ARGS(&pso)), name);
 }
 // One region: t0, t1 (null when absent), u0. Returns its GPU handle.
 D3D12_GPU_DESCRIPTOR_HANDLE Bind(ID3D12Resource* t0, DXGI_FORMAT f0, ID3D12Resource* t1, DXGI_FORMAT f1, ID3D12Resource* u0, DXGI_FORMAT fu) {
  const UINT base = region * kPerRegion;
  region = (region + 1) % kRegions;
  auto cpu = heap->GetCPUDescriptorHandleForHeapStart();
  cpu.ptr += static_cast<SIZE_T>(base) * stride;
  auto addSrv = [&](ID3D12Resource* r, DXGI_FORMAT f) {
   D3D12_SHADER_RESOURCE_VIEW_DESC s {}; s.Format = f; s.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
   s.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; s.Texture2D.MipLevels = 1;
   device->CreateShaderResourceView(r, &s, cpu); cpu.ptr += stride;
  };
  addSrv(t0, f0);
  addSrv(t1, f1);
  { D3D12_UNORDERED_ACCESS_VIEW_DESC u {}; u.Format = fu; u.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    device->CreateUnorderedAccessView(u0, nullptr, &u, cpu); }
  auto gpu = heap->GetGPUDescriptorHandleForHeapStart(); gpu.ptr += static_cast<UINT64>(base) * stride;
  return gpu;
 }
 public:
 // Flag bits of the constants' third dword (see the comment above AmdNrComposeHlsl).
 static constexpr UINT kFlagSkin = 1, kFlagUseTexel = 4, kFlagReset = 8;
 // The 12 root constants, in the shader's order (lmxxf's Pass::Run packs the same).
 struct Constants { UINT w, h, flags; float T, Cs, G, skinDetail, skinColour, envDetail, envColour, whiteConst, pad; };
 static_assert(sizeof(Constants) == 12 * sizeof(UINT), "12 root constants");
 // What Apply composes with. Sanitised again in Apply (the bridge already clamps them).
 struct Params {
  float detail = 1.f;      // T, 0..2 (Settings::composeDetail)
  float colour = 1.f;      // Cs, 0..4 (Settings::composeColour)
  float guard = 2.f;       // G, 1..8 (Settings::maxRatio)
  bool skin = false;       // the skin / environment final edit (Settings::skinProtection)
  float skinDetail = 1.f, skinColour = 1.f, envDetail = 1.f, envColour = 1.f; // 0..1 each
  float whiteConst = 1.f;  // W when no exposure texel is given (or it is not a usable exposure)
  bool highlightGuard = false; // (0.3.4) danielblnc's highlight chroma guard before the tail; needs EnsureGuard
 };
 explicit NrCompose(ID3D12Device* d) : device(d) {
  D3D12_DESCRIPTOR_RANGE ranges[2] = { { D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 0, 0, 0 },
                                       { D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0, 2 } };
  D3D12_ROOT_PARAMETER params[2] {};
  params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; params[0].DescriptorTable = { 2, ranges };
  params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS; params[1].Constants = { 0, 0, 12 };
  D3D12_STATIC_SAMPLER_DESC samp {};
  samp.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
  samp.AddressU = samp.AddressV = samp.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  samp.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  D3D12_ROOT_SIGNATURE_DESC rd {}; rd.NumParameters = 2; rd.pParameters = params; rd.NumStaticSamplers = 1; rd.pStaticSamplers = &samp;
  Microsoft::WRL::ComPtr<ID3DBlob> b, e;
  Check(D3D12SerializeRootSignature(&rd, D3D_ROOT_SIGNATURE_VERSION_1, &b, &e), "root signature");
  Check(d->CreateRootSignature(0, b->GetBufferPointer(), b->GetBufferSize(), IID_PPV_ARGS(&root)), "root signature");
  Build(nullptr, "AMD NR compose", composePipeline);
  const D3D_SHADER_MACRO estimate[] = { { "NRCOMPOSE_ESTIMATE", "1" }, { nullptr, nullptr } };
  Build(estimate, "AMD NR compose white estimate", estimatePipeline);
  D3D12_DESCRIPTOR_HEAP_DESC hd {}; hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV; hd.NumDescriptors = kRegions * kPerRegion;
  hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  Check(d->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&heap)), "descriptor heap");
  stride = d->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
 }
 // Compose the runtime's answer IN PLACE. `answer` is the FP16 w x h texture the runtime wrote
 // (danielblnc's slot colour): answerState -> UNORDERED_ACCESS -> answerState. `original` is the
 // FP16 w x h pre-model copy in the same units (the scale baseline), and `exposure` the R32_FLOAT
 // texel whose (0,0) is e with W = 1/e (the slot's exposure copy, or Estimate's texture) or null
 // for W = whiteConst; both must already be in NON_PIXEL_SHADER_RESOURCE and are left there.
 // Sets its own descriptor heap, root signature and pipeline: the caller rebinds its own before
 // its next dispatch. Returns false when nothing was recorded.
 bool Apply(ID3D12GraphicsCommandList* c, ID3D12Resource* answer, D3D12_RESOURCE_STATES answerState,
            ID3D12Resource* original, ID3D12Resource* exposure, UINT w, UINT h, const Params& k) {
  if (!c || !answer || !original || !w || !h) return false;
  const auto unit = [](float v, float lo, float hi, float fallback) { return std::isfinite(v) ? std::clamp(v, lo, hi) : fallback; };
  Constants cb {};
  cb.w = w; cb.h = h;
  cb.flags = (k.skin ? kFlagSkin : 0u) | (exposure ? kFlagUseTexel : 0u);
  cb.T = unit(k.detail, 0.f, 2.f, 1.f);
  cb.Cs = unit(k.colour, 0.f, 4.f, 1.f);
  cb.G = unit(k.guard, 1.f, 8.f, 2.f);
  cb.skinDetail = unit(k.skinDetail, 0.f, 1.f, 1.f);
  cb.skinColour = unit(k.skinColour, 0.f, 1.f, 1.f);
  cb.envDetail = unit(k.envDetail, 0.f, 1.f, 1.f);
  cb.envColour = unit(k.envColour, 0.f, 1.f, 1.f);
  cb.whiteConst = std::isfinite(k.whiteConst) && k.whiteConst > 0.f ? k.whiteConst : 1.f;
  // (0.3.4) The guard permutation only when asked for and built; otherwise exactly the 0.3.3.2 dispatch (pad 0).
  const bool guard = k.highlightGuard && composeGuardPipeline;
  if (guard) cb.pad = 1.f;
  const auto table = Bind(original, DXGI_FORMAT_R16G16B16A16_FLOAT, exposure, DXGI_FORMAT_R32_FLOAT,
                          answer, DXGI_FORMAT_R16G16B16A16_FLOAT);
  const bool alreadyUav = answerState == D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
  if (alreadyUav) UavBarrier(c, answer);
  else Barrier(c, answer, answerState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  auto hh = heap.Get(); c->SetDescriptorHeaps(1, &hh);
  c->SetComputeRootSignature(root.Get()); c->SetPipelineState(guard ? composeGuardPipeline.Get() : composePipeline.Get());
  c->SetComputeRootDescriptorTable(0, table);
  c->SetComputeRoot32BitConstants(1, 12, &cb, 0);
  c->Dispatch((w + 7) / 8, (h + 7) / 8, 1);
  if (alreadyUav) UavBarrier(c, answer);
  else Barrier(c, answer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, answerState);
  return true;
 }
 // (0.3.4) Compile danielblnc's highlight chroma guard (both entries of AmdNrComposeDanielGuardHlsl) once. Called
 // only while [DlssNr] AmdDanielHighlightGuard is on; throws on failure (the caller drops the guard for the
 // session and keeps everything else). Cheap once built.
 void EnsureGuard() {
  if (composeGuardPipeline && guardOnlyPipeline) return;
  const std::string source = std::string(AmdNrComposeHlsl) + AmdNrComposeDanielGuardHlsl;
  const D3D_SHADER_MACRO guard[] = { { "NRCOMPOSE_DANIEL_GUARD", "1" }, { nullptr, nullptr } };
  if (!composeGuardPipeline) BuildEntry(source, guard, "AMD NR compose with highlight guard", "compose_guard", composeGuardPipeline);
  if (!guardOnlyPipeline) BuildEntry(source, guard, "AMD NR highlight guard", "guard_only", guardOnlyPipeline);
 }
 // (0.3.4) Classic with the guard: the guard alone, IN PLACE on the runtime's answer, with Apply's bindings and
 // barriers (answer, original and exposure as there; exposure null = W from whiteConst). Returns false when nothing
 // was recorded (EnsureGuard has not built it).
 bool Guard(ID3D12GraphicsCommandList* c, ID3D12Resource* answer, D3D12_RESOURCE_STATES answerState,
            ID3D12Resource* original, ID3D12Resource* exposure, UINT w, UINT h, float whiteConst = 1.f) {
  if (!c || !answer || !original || !w || !h || !guardOnlyPipeline) return false;
  Constants cb {};
  cb.w = w; cb.h = h;
  cb.flags = exposure ? kFlagUseTexel : 0u;
  cb.whiteConst = std::isfinite(whiteConst) && whiteConst > 0.f ? whiteConst : 1.f;
  cb.pad = 1.f;
  const auto table = Bind(original, DXGI_FORMAT_R16G16B16A16_FLOAT, exposure, DXGI_FORMAT_R32_FLOAT,
                          answer, DXGI_FORMAT_R16G16B16A16_FLOAT);
  const bool alreadyUav = answerState == D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
  if (alreadyUav) UavBarrier(c, answer);
  else Barrier(c, answer, answerState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  auto hh = heap.Get(); c->SetDescriptorHeaps(1, &hh);
  c->SetComputeRootSignature(root.Get()); c->SetPipelineState(guardOnlyPipeline.Get());
  c->SetComputeRootDescriptorTable(0, table);
  c->SetComputeRoot32BitConstants(1, 12, &cb, 0);
  c->Dispatch((w + 7) / 8, (h + 7) / 8, 1);
  if (alreadyUav) UavBarrier(c, answer);
  else Barrier(c, answer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, answerState);
  return true;
 }
 // Estimate the white point the runtime composes at, for a title that publishes no exposure
 // texture: one thread over a 64x36 grid of `original` (FP16, in NON_PIXEL_SHADER_RESOURCE, left
 // there). Returns the state texture in NON_PIXEL_SHADER_RESOURCE, texel (0,0) = e, ready to be
 // Apply's `exposure`; null when nothing was recorded. `reset` starts the loop again from 4 (the
 // texture's own marker already does that the first time).
 ID3D12Resource* Estimate(ID3D12GraphicsCommandList* c, ID3D12Resource* original, bool reset = false) {
  if (!c || !original) return nullptr;
  if (!state) {
   D3D12_HEAP_PROPERTIES hp {}; hp.Type = D3D12_HEAP_TYPE_DEFAULT;
   D3D12_RESOURCE_DESC rd {}; rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; rd.Width = 4; rd.Height = 1;
   rd.DepthOrArraySize = 1; rd.MipLevels = 1; rd.Format = DXGI_FORMAT_R32_FLOAT; rd.SampleDesc.Count = 1;
   rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
   Check(device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                                         nullptr, IID_PPV_ARGS(&state)), "estimate state");
  }
  Constants cb {};
  cb.w = 1; cb.h = 1; cb.flags = reset ? kFlagReset : 0u;
  const auto table = Bind(original, DXGI_FORMAT_R16G16B16A16_FLOAT, nullptr, DXGI_FORMAT_R32_FLOAT,
                          state.Get(), DXGI_FORMAT_R32_FLOAT);
  Barrier(c, state.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  auto hh = heap.Get(); c->SetDescriptorHeaps(1, &hh);
  c->SetComputeRootSignature(root.Get()); c->SetPipelineState(estimatePipeline.Get());
  c->SetComputeRootDescriptorTable(0, table);
  c->SetComputeRoot32BitConstants(1, 12, &cb, 0);
  c->Dispatch(1, 1, 1);
  Barrier(c, state.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  return state.Get();
 }
};
}
