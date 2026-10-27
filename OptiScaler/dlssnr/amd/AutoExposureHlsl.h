// Copyright (c) 2026 3zwr1 (AMDNR). Part of AMDNR (GPL-3.0; see Licenses/AMDNR_NOTICE.txt).
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// lmxxf's AUTO-EXPOSURE shader (the loop is described where LmxxfBackend.cpp includes this).
//
// Moved here from LmxxfBackend.cpp so the text lives in one place. A bare literal on purpose -
// no includes, no namespace: LmxxfBackend.cpp includes it INSIDE its anonymous namespace, where
// the literal used to be, and it must stay includable there. lmxxf is its only user: danielblnc's
// estimator (below) has its own text in NrCompose.h, so nothing here reaches that runtime.
//
// 0.3.3 colour fix (lmxxf only, host-side; the runtime and its probe hash are untouched):
//  - FLOOR 1/65536 (was 1/256), in both places below and in LmxxfBackend.cpp's ExposureShader
//    mode 2, which clamps the texel this writes. A title with no exposure texture whose linear HDR
//    colour needs less than 1/256 (Control Resonant, R11G11B10 linear HDR, colour greyed out) sat
//    pinned at the old floor and fed the network a blown-out picture. Above the old floor nothing
//    changes.
//  - HIGHLIGHT CAP (hlCap != 0, [DlssNr] AmdLmxxfAutoExposureHighlightCap, off by default in 0.3.3). The loop
//    drives the MEAN toward encoded 0.5; in a dark, foggy scene that pushes the bright part of the
//    frame past the codec's shoulder (0.75), whose per-channel squash takes the colour out of it,
//    and the Classic decode then takes that part's colour from the network: grey (Silent Hill 2,
//    exposure stepping x1.5 from 3.3 to 25 in one stats window, fed mean 0.89 linear at 16.7). So
//    the same grid also counts the samples whose FED max channel (raw x exposure) is above 0.75:
//    above 8% of the measured samples the exposure is not raised; above 25% it may fall up to x4
//    per feed instead of x1.5. hlCap == 0 is the loop as it was (capDown == cap, the same clamp).
//
// danielblnc's white-point estimator (NrCompose.h, NRCOMPOSE_ESTIMATE) runs the same encode and
// the same (target / mean)^2.4 step, with that runtime's own law instead of this one's: a 0.02
// band, a start at exposure 4 and a 1.5x cap on every update including the first (its log:
// "auto-exposure: encoded mean 0.000 -> exposure 4.0000", then 0.500 held), and its own floor of
// 1/256, which this change leaves alone. Keep the two encodes in step if the one here ever changes.
inline constexpr char AutoExposureShader[] = R"(
Texture2D<float4> src : register(t0);
RWTexture2D<float> st : register(u0);
SamplerState samp : register(s0);
cbuffer P : register(b0) { uint w; uint h; uint mode; float pad; float target; float stepCap; float band; float hlCap; }
float enc(float v) {
 v = max(v, 0.0);
 float sh = 0.75 + 0.25 * (1.0 - exp(-5.770780 * (v - 0.75)));
 v = saturate(v <= 0.75 ? v : sh);
 return v <= 0.0031308 ? v * 12.92 : 1.055 * pow(v, 1.0 / 2.4) - 0.055;
}
[numthreads(1,1,1)] void main(uint3 tid : SV_DispatchThreadID) {
 float prev = st[uint2(2,0)];
 bool first = st[uint2(3,0)] != 1.0 || !(isfinite(prev) && prev >= 1.0 / 65536.0 && prev <= 16384.0);
 if (first) prev = 1.0;
 float sum = 0.0; float n = 0.0; float hot = 0.0;
 for (uint y = 0u; y < 36u; ++y) {
  for (uint x = 0u; x < 64u; ++x) {
   float2 uv = (float2(float(x), float(y)) + 0.5) / float2(64.0, 36.0);
   float3 c = src.SampleLevel(samp, uv, 0).rgb;
   if (!all(isfinite(c))) continue;
   float l = dot(max(c, 0.0), float3(0.2126, 0.7152, 0.0722)) * prev;
   sum += enc(l); n += 1.0;
   // The highlight cap's count: this sample's brightest channel as the network would be fed it.
   float3 fc = max(c, 0.0) * prev;
   if (max(fc.r, max(fc.g, fc.b)) > 0.75) hot += 1.0;
  }
 }
 float mean = n > 0.0 ? sum / n : target;
 // Deadband: a picture already within `band` of the target keeps the exposure it has (a game
 // that feeds the network sensibly as is keeps its look); outside it the step brings it back.
 float step = abs(mean - target) <= band ? 1.0 : pow(target / max(mean, 1e-4), 2.4);
 float cap = first ? 4096.0 : max(stepCap, 1.001);
 float capDown = cap;
 if (hlCap != 0.0 && n > 0.0) {
  float hotFrac = hot / n;
  if (hotFrac > 0.08) step = min(step, 1.0);  // part of the frame is already in the shoulder: never raise
  if (hotFrac > 0.25) capDown = max(cap, 4.0); // much of it: come down faster, up to x4 per feed
 }
 step = clamp(step, 1.0 / capDown, cap);
 float e = clamp(prev * step, 1.0 / 65536.0, 16384.0);
 st[uint2(0,0)] = e; st[uint2(1,0)] = mean; st[uint2(2,0)] = e; st[uint2(3,0)] = 1.0;
}
)";
