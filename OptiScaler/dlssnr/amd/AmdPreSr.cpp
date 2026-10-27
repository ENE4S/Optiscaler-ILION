// ILION adaptation: AMD NR only; optional GI and presentation paths excluded.
// Copyright (c) 2026 3zwr1 (AMDNR)
// SPDX-License-Identifier: GPL-3.0-or-later
#include "AmdPreSr.h"
#include "AmdLayout.h"
#ifdef AMD_RETIRE_DIAGNOSTICS
#include "RetirementDiagnostics.h"
#endif
#include "RuntimeNotification.h"
#include "RuntimeHostLoad.h"
#include "SubmissionState.h"
#include "ColorEncoding.h"
#include "Interleave.h"
#include "InterleavePacing.h"
#include "GraphicsSnapshotHooks.h"
#include <Config.h>
#include <State.h> // (0.3.4, P7.10) State::api: the NR cost readout is n/a on the D3D11 bridge
#include "TemporalStability.h"
#include "DetailColourMix.h"
#include "NrCompose.h"
#include "EditShapeRules.h"
#include "AmdBridge.h" // SetCompositionNote and MemoryTelemetry only (nvsdk_ngx.h is already in through Config.h)
#include "ComIdentity.h" // (0.3.4, P1) SameQueueObject, for Submitting on the device behind a proxy
#include "../DlssNr_Capture.h"
#include <wrl/client.h>
#include <d3dcompiler.h>
#include "SystemCompiler.h"
#include <bcrypt.h>
#include <array>
#include <atomic>
#include <chrono>
#include <algorithm>
#include <fstream>
#include <mutex>
#include <vector>
#include <cctype>
#include <cstring>
#include <cstdlib>
#include <stdexcept>
#include <cmath>

using Microsoft::WRL::ComPtr;
namespace AmdPreSr
{
namespace
{
template <class T> T& At(HMODULE h, size_t rva) { return *reinterpret_cast<T*>(reinterpret_cast<uintptr_t>(h) + rva); }
void Check(HRESULT hr, const char* operation)
{
    if (FAILED(hr))
        throw std::runtime_error(std::string(operation) + " HRESULT=" + std::to_string(static_cast<unsigned>(hr)));
}
void Barrier(ID3D12GraphicsCommandList* c, ID3D12Resource* r, D3D12_RESOURCE_STATES a, D3D12_RESOURCE_STATES b)
{
    if (!r || a == b)
        return;
    D3D12_RESOURCE_BARRIER v {};
    v.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    v.Transition = { r, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, a, b };
    c->ResourceBarrier(1, &v);
}
void ValidateDaniel060(HMODULE module)
{
    const auto base = reinterpret_cast<const unsigned char*>(module);
    { constexpr unsigned char expected[] = { 0x55,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x56,0x57,0x53,0x48,0x81,0xec,0x28 };
      if (std::memcmp(base + 0x2cfb0, expected, sizeof expected) != 0) throw std::runtime_error("Daniel 0.6.0 code/ABI signature mismatch at 2cfb0"); }
    { constexpr unsigned char expected[] = { 0x55,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x56,0x57,0x53,0x48,0x81,0xec,0x78 };
      if (std::memcmp(base + 0x17680, expected, sizeof expected) != 0) throw std::runtime_error("Daniel 0.6.0 code/ABI signature mismatch at 17680"); }
    { constexpr unsigned char expected[] = { 0x41,0x57,0x41,0x56,0x41,0x54,0x56,0x57,0x55,0x53,0x48,0x83,0xec,0x60,0x66,0xf };
      if (std::memcmp(base + 0xb4a0, expected, sizeof expected) != 0) throw std::runtime_error("Daniel 0.6.0 code/ABI signature mismatch at b4a0"); }
    { constexpr unsigned char expected[] = { 0x56,0x48,0x83,0xec,0x30,0x83,0x3d,0xdc,0x35,0xa,0x0,0x0,0xf,0x84,0xac,0x0 };
      if (std::memcmp(base + 0x1b420, expected, sizeof expected) != 0) throw std::runtime_error("Daniel 0.6.0 code/ABI signature mismatch at 1b420"); }
    { constexpr unsigned char expected[] = { 0xf3,0xf,0x10,0x46,0x60 };
      if (std::memcmp(base + 0x1a916, expected, sizeof expected) != 0) throw std::runtime_error("Daniel 0.6.0 code/ABI signature mismatch at 1a916"); }
    { constexpr unsigned char expected[] = { 0xf3,0xf,0x10,0x46,0x60 };
      if (std::memcmp(base + 0x1aef8, expected, sizeof expected) != 0) throw std::runtime_error("Daniel 0.6.0 code/ABI signature mismatch at 1aef8"); }
}
struct Packet
{
    ID3D12GraphicsCommandList* list;
    ID3D12Resource* colour;
    UINT colourState, pad14;
    ID3D12Resource* motion;
    UINT motionState, pad24;
    ID3D12Resource* depth;
    UINT depthState, pad34;
    ID3D12Resource* exposure;
    UINT exposureState;
    float scaleX, scaleY;
    // Native FFX pre mode has different input/output semantics. B supplies its
    // own FP16 staging and uses the existing non-pre packet path (zero).
    uint8_t nativePre;
    uint8_t pad4d[3];
    UINT renderWidth, renderHeight;
    // B does not request native pre-mode reprojection. Explicit zero avoids
    // passing stack data as jitter; this is not a new NGX-to-FFX jitter mapping.
    float jitterX, jitterY;
    // 0.6.0 reads +0x60. Our exposure texture is already normalized by game pre-exposure.
    float preExposure = 1.0f;
    UINT pad64 = 0;
};
static_assert(sizeof(Packet) == 0x68 && offsetof(Packet, scaleX) == 0x44);
static_assert(offsetof(Packet, nativePre) == 0x4c && offsetof(Packet, renderWidth) == 0x50);
static_assert(offsetof(Packet, renderHeight) == 0x54 && offsetof(Packet, jitterX) == 0x58);
static_assert(offsetof(Packet, jitterY) == 0x5c);
static_assert(offsetof(Packet, preExposure) == 0x60);
using InitFn = bool(__fastcall*)(void*, const std::string*);
// Keep a broken HIP dependency from taking the game down during the new 0.6 initialization path.
using HipMallocFn = int (*)(void**, size_t);
using HipFreeFn = int (*)(void*);
int ProbeHipMemory(HipMallocFn alloc, HipFreeFn release, DWORD* fault)
{
    __try {
        void* memory = nullptr;
        const int rc = alloc(&memory, 4096);
        if (rc) return rc;
        return release(memory);
    } __except ((GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION || GetExceptionCode() == EXCEPTION_ILLEGAL_INSTRUCTION)
                ? (*fault = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER) : EXCEPTION_CONTINUE_SEARCH) {
        return -1;
    }
}
bool InitializeDaniel060(InitFn initialize, void* engine, const std::string* weights, DWORD* fault)
{
    __try { return initialize(engine, weights); }
    __except ((GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION || GetExceptionCode() == EXCEPTION_ILLEGAL_INSTRUCTION)
              ? (*fault = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER) : EXCEPTION_CONTINUE_SEARCH) { return false; }
}
using RecordFn = void(__fastcall*)(Packet*);
using NotifyFn = void(__fastcall*)(ID3D12CommandQueue*, UINT, ID3D12CommandList* const*);
using HipSetFn = int (*)(int);
// ALPHA: the source's, carried as is - identity Load at 100%, area-averaged at any other NR
// scale, where the resolve then writes the full-resolution frame's own alpha (ResolveShader).
// The newer RenoDX DLSS addon makes the same input choice: its N2 bridge passes the source
// alpha to the model; its older v4.7 build forced 1.0. RenoDX by clshortfuse (Carlos Lopez
// Jr.), MIT, https://github.com/clshortfuse/renodx; see Licenses/RenoDX_ATTRIBUTION.txt. No
// RenoDX code here, behaviour reference only. The closed runtime's write-back CS stores base.w
// (this texture's own alpha, or with its flag 32 an internal copy's), and its residual is RGB.
// Whether its HIP import reads alpha is not visible. In Classic at 100% no later pass restores
// alpha, so never force it here. The same shader also performs the post-RR write-back at
// identity.
constexpr char CopyShader[] = R"(
Texture2D<float4> src : register(t0);
RWTexture2D<float4> dst : register(u0);
cbuffer Extent : register(b0) { uint w; uint h; uint sourceW; uint sourceH; };
[numthreads(8,8,1)] void main(uint3 p:SV_DispatchThreadID) {
 if(p.x>=w || p.y>=h)return;
 if(w==sourceW && h==sourceH){dst[p.xy]=src.Load(int3(p.xy,0));return;}
 // Integrate the entire source pixel footprint. A single bilinear sample aliases
 // narrow emissive lines when the model runs far below the input resolution.
 float2 lo=float2(p.xy)*float2(sourceW,sourceH)/float2(w,h);
 float2 hi=float2(p.xy+1)*float2(sourceW,sourceH)/float2(w,h);
 int2 first=int2(floor(lo)); float4 sum=0;float total=0;
 [loop]for(int y=first.y;y<int(ceil(hi.y));++y)
 [loop]for(int x=first.x;x<int(ceil(hi.x));++x){
  float2 coverage=max(0,min(hi,float2(x+1,y+1))-max(lo,float2(x,y)));
  float weight=coverage.x*coverage.y;
  sum+=src.Load(int3(clamp(int2(x,y),0,int2(sourceW-1,sourceH-1)),0))*weight;total+=weight;
 }
 dst[p.xy]=sum/max(total,1e-6);
})";
constexpr char ResolveShader[] = R"(
Texture2D<float4> src:register(t0);
Texture2D<float4> baseline:register(t1);
Texture2D<float4> edited:register(t2);
RWTexture2D<float4> dst:register(u0);
cbuffer Extent:register(b0){uint w,h,lowW,lowH;float intensity,limitFrac,fade;};
float3 delta(int2 p){p=clamp(p,0,int2(lowW-1,lowH-1));return edited.Load(int3(p,0)).rgb-baseline.Load(int3(p,0)).rgb;}
[numthreads(8,8,1)] void main(uint3 p:SV_DispatchThreadID){
 if(p.x>=w||p.y>=h)return;
 float2 q=(float2(p.xy)+.5)*float2(lowW,lowH)/float2(w,h)-.5;
 int2 a=int2(floor(q));float2 t=frac(q);
 // CATMULL-ROM, not bilinear. The residual is the only thing carried up from the reduced
 // raster, so how it is resampled decides how much of the model's work survives the trip.
 // Bilinear rounds every edit into a 2x2 average and the finest of them - the detail the
 // model exists to synthesise - is exactly what a 2x2 average destroys first. A cubic
 // kernel keeps it, at four times the taps on a term that is a few percent of the picture.
 float2 t2=t*t, t3=t2*t;
 float wx[4]={-.5*t3.x+t2.x-.5*t.x, 1.5*t3.x-2.5*t2.x+1., -1.5*t3.x+2.*t2.x+.5*t.x, .5*t3.x-.5*t2.x};
 float wy[4]={-.5*t3.y+t2.y-.5*t.y, 1.5*t3.y-2.5*t2.y+1., -1.5*t3.y+2.*t2.y+.5*t.y, .5*t3.y-.5*t2.y};
 float3 d=0;
 float3 dmin=1e30, dmax=-1e30;
 [unroll] for(int yy=0;yy<4;++yy)[unroll] for(int xx=0;xx<4;++xx){
  float3 s=delta(a+int2(xx-1,yy-1));
  d+=s*(wx[xx]*wy[yy]);
  // The inner 2x2 - the samples a bilinear fetch would have used, i.e. the four the
  // output pixel actually sits between.
  if(xx>=1&&xx<=2&&yy>=1&&yy<=2){dmin=min(dmin,s);dmax=max(dmax,s);}
 }
 // RINGING BOUND, and this is what the dark outline beside a character was.
 //
 // Catmull-Rom is not an averaging filter: two of its four taps carry NEGATIVE weight.
 // That is what keeps the fine detail a bilinear 2x2 would destroy, and it is also what
 // makes it overshoot at a hard edge - it reconstructs values outside the range of every
 // sample it read. Across a character's silhouette the edit swings strongly negative just
 // outside the outline, and at a low NR resolution that overshoot is two full-res pixels
 // wide and plainly visible as a dark rim.
 //
 // The `confidence` term below cannot see this. It compares the output pixel against its
 // own bilinear footprint, so it correctly kills the edit ON the silhouette - but a pixel
 // one or two across is road against road, it agrees with its footprint perfectly, and the
 // cubic has still reached into the character to compute it.
 //
 // Clamping the result back into the range of the samples it was built from is the
 // standard answer, and it costs nothing the cubic was chosen for: the reconstruction
 // between samples is kept in full, only the part that was never in any sample is given
 // up. Scaled as one triple by a single scalar, never per channel - a per-channel clamp
 // would let whichever channel overshot most decide the hue of the rim instead of
 // removing it.
 {
  float3 ctr=0.5*(dmax+dmin);
  float3 ext=max(0.5*(dmax-dmin),1e-6);
  float3 v=d-ctr;
  float u=max(abs(v.x/ext.x),max(abs(v.y/ext.y),abs(v.z/ext.z)));
  if(isfinite(u)&&u>1.) d=ctr+v/u;
 }
 d*=intensity;
 float4 c=src.Load(int3(p.xy,0));
 // A reduced neural pixel mixes surfaces and small emitters. Suppress its edit
 // where the original pixel disagrees with that footprint, rather than spreading
 // the edit blindly across high-contrast edges. No previous frame is reused.
 int2 hi=int2(lowW-1,lowH-1);
 float3 b=lerp(lerp(baseline.Load(int3(clamp(a,0,hi),0)).rgb,baseline.Load(int3(clamp(a+int2(1,0),0,hi),0)).rgb,t.x),
 lerp(baseline.Load(int3(clamp(a+int2(0,1),0,hi),0)).rgb,baseline.Load(int3(clamp(a+1,0,hi),0)).rgb,t.x),t.y);
 float3 magnitude=max(max(abs(c.rgb),abs(b)),1e-5);
 float mismatch=max(abs(c.r-b.r)/magnitude.r,max(abs(c.g-b.g)/magnitude.g,abs(c.b-b.b)/magnitude.b));
 float confidence=1-smoothstep(.15,.75,mismatch);
 // Bounded as ONE TRIPLE, not per channel, and that is a correctness fix rather than a
 // preference. Clamping each channel separately means whichever channel hits its wall
 // first decides the colour of the rest - a hue rotation - so an over-large edit came out
 // as a wrong-COLOURED block instead of a smaller version of the right one. It is the same
 // failure that put teal outlines on the taillights in the temporal pass, arrived at
 // independently here. Scaling the whole vector keeps the direction of the edit and gives
 // up only its size, and an edit already inside the bound is untouched.
 float bound=limitFrac*max(max(abs(b.r),abs(c.r)),max(max(abs(b.g),abs(c.g)),max(abs(b.b),abs(c.b))));
 float mag=max(abs(d.r),max(abs(d.g),abs(d.b)));
 if(bound>0.&&mag>bound) d*=bound/mag;
 d*=confidence;
 // The reduced raster has no neighbour outside the frame, and a cubic kernel rings on that
 // missing data. Rolling the edit off over a band at the border removes the ring rather
 // than letting it draw a bright line along the screen edge. A corner gets both rolloffs.
 if(fade>0.){float2 uv=(float2(p.xy)+.5)/float2(w,h);float2 e=min(uv,1.-uv)/fade;d*=saturate(min(e.x,e.y));}
 dst[p.xy]=float4(clamp(c.rgb+d,0,65504),c.a);
})";
// EDIT SHAPER (0.3.3.2 rebuild, option B). NR size not 100% only; at 100% the resolve keeps the A-min dial.
// Runs in place on the slot colour after the RenoDX composition and before the Look: strength and limit act on the
// model's composed edit (answer - pre-model copy), not on the finished look, as lmxxf does at every NR size. The resolve
// then lifts the result at strength 1 with no limit (edge fade kept).
constexpr char EditShapeShader[] = R"(
Texture2D<float4> baseline : register(t0);
RWTexture2D<float4> img : register(u0);
cbuffer P : register(b0) { uint w; uint h; float intensity; float limitFrac; };
[numthreads(8,8,1)] void main(uint3 p : SV_DispatchThreadID) {
 if (p.x >= w || p.y >= h) return;
 float4 o = baseline.Load(int3(p.xy, 0));
 if (!all(isfinite(o.rgb))) return;
 float4 a = img[p.xy];
 if (!all(isfinite(a.rgb))) { img[p.xy] = o; return; }
 float3 d = (a.rgb - o.rgb) * intensity;
 float bound = limitFrac * max(abs(o.r), max(abs(o.g), abs(o.b)));
 float mag = max(abs(d.r), max(abs(d.g), abs(d.b)));
 if (bound > 0.0 && mag > bound) d *= bound / mag;
 // Strength above 1 can push a darkening through zero; never below 0 unless an input already was.
 float3 lo = min(float3(0.0, 0.0, 0.0), min(a.rgb, o.rgb));
 img[p.xy] = float4(clamp(o.rgb + d, lo, 65504.0), a.a);
}
)";
// HYBRID HIGHLIGHT PROXY. Runs in place on the slot colour: once before the model with
// decode = 0, once after it with decode = 1. See the encode site in Record.
constexpr char ProxyShader[] = R"(
RWTexture2D<float4> img : register(u0);
Texture2D<float4> raw : register(t1);
cbuffer P : register(b0) { uint w; uint h; float knee; float range; uint decode; float bound; };
// Encode: identity up to the knee; above it the largest channel m is squeezed into
// (knee, knee + range) by fwd and the scale s = fwd(m)/m is applied to all three channels,
// so a bright saturated colour keeps its hue and no channel can exceed knee + range.
// Decode: DECODED BY THE ORIGINAL - RenoDX's principle (clshortfuse, MIT,
// https://github.com/clshortfuse/renodx): its DLSS addon's N2 colour bridge decodes the model's
// output with n2 = Neutwo(peak)/peak recomputed from the ORIGINAL frame, not from the answer.
// Here s is recomputed from raw (the bit-exact pre-encode copy) by the same instructions as
// the encode, so a model that changes nothing gets the original back (1 FP16 ulp). Near the
// top of the curve the proxy carries almost no magnitude, so there the answer is divided by s.
// Low in the curve it still does: there the model's own answer is decoded through the curve,
// with weight wi = (1 - u)^2 = 1 / inv'(answer), so that term is never steeper than 1, and an
// answer the model moved down to the knee (a sparkle or firefly it removed, dark content moved
// onto a light) comes back as the model returned it. The hand-over is AMDNR's, not RenoDX's.
float fwd(float m) { float t = m - knee; return knee + t / (1.0 + t / range); }
[numthreads(8,8,1)] void main(uint3 p : SV_DispatchThreadID) {
 if (p.x >= w || p.y >= h) return;
 float4 o = raw.Load(int3(p.xy, 0));      // bit-identical to the slot colour the encode reads
 if (!all(isfinite(o.rgb))) return;       // not encoded, so not decoded
 float m = max(o.r, max(o.g, o.b));
 if (m <= knee) return;                   // identity below the knee, both ways
 float s = fwd(m) / m;                    // (0,1]; m > knee >= 0.25
 if (decode == 0u) { img[p.xy] = float4(o.rgb * s, o.a); return; } // source alpha to the model
 float4 c = img[p.xy];                    // the answer; its alpha is kept
 if (!all(isfinite(c.rgb))) return;
 float ma = max(c.r, max(c.g, c.b));
 float g = 1.0;                           // at or below the knee the answer is linear already
 if (ma > knee) {
  float u = saturate((ma - knee) / range);
  float wi = (1.0 - u) * (1.0 - u);
  // wi * knee + range * u * (1 - u) is wi * inv(ma), without inv's division by (1 - u) or its
  // clamp. At u = 1 (the top of the curve, or model overshoot) g = 1 / s: RenoDX's decode.
  g = (wi * knee + range * u * (1.0 - u)) / ma + (1.0 - wi) / s;
 }
 c.rgb *= g;                              // one scalar, so the hue is kept
 // A SAFETY NET NOW, NOT THE DECODE. The old decode inverted the curve on the answer, and that
 // inverse is steep near knee + range: Silent Hill 2 capture 192113 had the overcast sky at its
 // ceiling on every model frame. Any blend of proxy values inside the 3x3 decodes at or below
 // the local raw maximum, so this bites only where the model overshoots `bound` times that - a
 // light cannot be created from nothing - and at FP16's ceiling, in both directions.
 float rm = 0.0;
 int2 mx = int2(int(w) - 1, int(h) - 1);
 [unroll] for (int dy = -1; dy <= 1; ++dy)
  [unroll] for (int dx = -1; dx <= 1; ++dx) {
   float4 r = raw.Load(int3(clamp(int2(p.xy) + int2(dx, dy), int2(0, 0), mx), 0));
   rm = max(rm, max(r.r, max(r.g, r.b)));
  }
 float cap = min(max(rm, knee) * bound + 0.02, 65504.0);
 float md = max(c.r, max(c.g, c.b));
 if (md > cap) c.rgb *= cap / md;
 c.rgb = max(c.rgb, -65504.0);
 img[p.xy] = c;
}
)";
constexpr char DepthShader[] = R"(
Texture2D<float> src : register(t0);
RWTexture2D<float> dst : register(u0);
cbuffer Extent : register(b0) { uint w; uint h; uint sourceW; uint sourceH; };
[numthreads(8,8,1)] void main(uint3 p:SV_DispatchThreadID) {
 if(p.x<w && p.y<h) {
 uint2 q=min(uint2((float2(p.xy)+.5)*float2(sourceW,sourceH)/float2(w,h)),uint2(sourceW-1,sourceH-1));
 dst[p.xy]=src.Load(int3(q,0)); }
})";
constexpr char MotionShader[] = R"(
Texture2D<float2> src : register(t0);
RWTexture2D<float2> dst : register(u0);
cbuffer Extent : register(b0) { uint w; uint h; uint sourceW; uint sourceH; };
[numthreads(8,8,1)] void main(uint3 p:SV_DispatchThreadID) {
 if(p.x>=w || p.y>=h) return;
 uint2 q=min(uint2((float2(p.xy)+0.5)*float2(sourceW,sourceH)/float2(w,h)),uint2(sourceW-1,sourceH-1));
 // Keep the sampled vector unchanged; convert its pixel scale in the packet.
 dst[p.xy]=src.Load(int3(q,0));
})";
constexpr char ExposureShader[] = R"(
Texture2D<float4> src : register(t0);
RWTexture2D<float> dst : register(u0);
cbuffer Extent : register(b0) { uint w; uint h; float preExposure; float exposureScale; };
[numthreads(1,1,1)] void main(uint3 p:SV_DispatchThreadID) {
 float e=src.Load(int3(0,0,0)).r*exposureScale/preExposure;
 dst[uint2(0,0)]=isfinite(e) && e>0 ? e : 1.0;
})";
// The readable SRV format for a depth resource. TYPED depth-stencil formats are here as
// well as the typeless ones: The Last of Us Part I allocates its depth as
// D32_FLOAT_S8X24_UINT, and without these four cases the scaled path refused it with
// "unsupported depth view" - which then latched the backend off for the whole session,
// so "I tick the box and nothing happens". These are the same casts Shader_Dx12.cpp and
// the FSR-RR preprocessor already use for the same resources.
DXGI_FORMAT DepthReadFormat(DXGI_FORMAT f)
{
    switch (f)
    {
    case DXGI_FORMAT_R32_TYPELESS:
    case DXGI_FORMAT_R32_FLOAT:
    case DXGI_FORMAT_D32_FLOAT:
        return DXGI_FORMAT_R32_FLOAT;
    case DXGI_FORMAT_R16_TYPELESS:
    case DXGI_FORMAT_R16_UNORM:
    case DXGI_FORMAT_D16_UNORM:
        return DXGI_FORMAT_R16_UNORM;
    case DXGI_FORMAT_R16_FLOAT:
        return DXGI_FORMAT_R16_FLOAT;
    case DXGI_FORMAT_R24G8_TYPELESS:
    case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:
    case DXGI_FORMAT_D24_UNORM_S8_UINT:
        return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    case DXGI_FORMAT_R32G8X24_TYPELESS:
    case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:
    case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
        return DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
    default:
        return DXGI_FORMAT_UNKNOWN;
    }
}
// Leak audit R4 (0.3.3.2 rebuild): see Record. Returns false (size untouched) when the snap does not apply.
bool SnapNrWorkingSize(UINT inW, UINT inH, float scale, UINT step, UINT& w, UINT& h)
{
    if (!step || scale == 1.f || uint64_t(w) * h <= (1ull << 20)) return false; // <=1 MP: all fit the runtime's reusable 8 MiB buffers
    const bool wide = inW >= inH;
    const UINT sIn = wide ? inH : inW, lIn = wide ? inW : inH, sReq = wide ? h : w;
    auto snap = [step](double v) { return (std::max)(step, UINT(std::lround(v / step)) * step); };
    UINT s = snap(sReq), l = snap(double(s) * lIn / sIn); // short side first, long side from the input's aspect
    const UINT sCap = UINT(std::lround(sIn * 1.5)), lCap = UINT(std::lround(lIn * 1.5));
    if (scale > 1.f)
    {
        if (s > sCap) s -= step;
        l = snap(double(s) * lIn / sIn);
        if (l > lCap) l -= step;
    }
    const bool ok = scale < 1.f ? (s < sIn && l < lIn) : (s > sIn && l > lIn && s <= sCap && l <= lCap);
    if (!ok) return false; // keeps the exact size: never lands on 100%, never beyond 1.5x
    (wide ? h : w) = s;
    (wide ? w : h) = l;
    return true;
}
std::string Layout(ID3D12Resource* resource)
{
    auto d = resource->GetDesc();
    return std::to_string(d.Width) + "x" + std::to_string(d.Height) + " format=" + std::to_string(d.Format) +
           " flags=" + std::to_string(d.Flags) + " samples=" + std::to_string(d.SampleDesc.Count) +
           " array=" + std::to_string(d.DepthOrArraySize) + " dimension=" + std::to_string(d.Dimension);
}
// Set once, when a pass DLL is successfully identified at load. The menu reads THIS
// instead of hashing the file again - see LoadedRuntimeName below for why that matters.
std::atomic<const char*> g_loadedRuntimeName { nullptr };
// (0.3.4, danielblnc support) The loaded layout (the bridge's capability answers) and the runtime's own quality mode as pass 1
// held it right after loading (-1 = not known). Set once per process at load, like g_loadedRuntimeName.
std::atomic<const AmdLayout*> g_loadedRuntimeLayout { nullptr };
std::atomic<int> g_runtimeOwnQuality { -1 };

// (0.3.4, danielblnc support) What the menu and the logs call a known build: its row name when that is a version number, else
// (a build named by its digest prefix) the version its own file carries, read here at run time, else the prefix. Kept
// per row for the process, set once, never changed after (the pointer is handed to the menu thread).
const char* LayoutDisplayName(const AmdLayout* layout, const std::vector<unsigned char>& data)
{
    if (IsVersionName(layout->name))
        return layout->name;
    static std::mutex labelMutex;
    static std::array<std::string, std::size(kAmdLayouts)> labels;
    std::lock_guard lock(labelMutex);
    for (size_t i = 0; i < std::size(kAmdLayouts); ++i)
    {
        if (kAmdLayouts[i] != layout)
            continue;
        if (labels[i].empty())
        {
            const std::string version = RuntimeVersionIn(data.data(), data.size());
            labels[i] = version.empty() ? std::string(layout->name) : version;
        }
        return labels[i].c_str();
    }
    return layout->name;
}

const AmdLayout* IdentifyRuntime(const std::filesystem::path& file, const char** displayName = nullptr)
{
    std::ifstream in(file, std::ios::binary);
    if (!in)
        return nullptr;
    // Sized read, not an istreambuf_iterator. That iterator pulls the file a character at
    // a time through the stream buffer, and this file is 7.3 MB - on the thread the menu
    // draws from, which is the game's render thread, that is a stall measured in seconds
    // rather than milliseconds. A game with a frame watchdog does not survive it.
    std::error_code ec;
    const auto size = std::filesystem::file_size(file, ec);
    if (ec || size == 0 || size > (64u << 20))
        return nullptr;
    std::vector<unsigned char> data(static_cast<size_t>(size));
    in.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(size));
    if (static_cast<std::uintmax_t>(in.gcount()) != size)
        return nullptr;
    BCRYPT_ALG_HANDLE alg {};
    unsigned char digest[32] {};
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
        return nullptr;
    // `data` can no longer be empty here, and that is the point: an empty vector hands
    // BCryptHash a NULL buffer, which is a documented STATUS_INVALID_PARAMETER - 0xC000000D,
    // the exact code the crash report came back with.
    auto result = BCryptHash(alg, nullptr, 0, data.data(), static_cast<ULONG>(data.size()), digest, 32);
    BCryptCloseAlgorithmProvider(alg, 0);
    if (result < 0)
        return nullptr;
    for (auto layout : kAmdLayouts)
        if (data.size() == layout->size && std::memcmp(digest, layout->sha256.bytes, 32) == 0)
        {
            if (displayName)
                *displayName = LayoutDisplayName(layout, data);
            return layout;
        }
    return nullptr;
}
// A pass DLL this build does not know, described in words the user can act on. The runtime
// carries its version in its own overlay text (RuntimeVersionIn); size and digest
// prefix are for a report. The Skyrim report: every launch "hash mismatch: pass 1", the menu
// reading "pass1?", and the files byte-identical to the release's Runtime.zip - which turned out
// to carry danielblnc's 0.2.16, a build this host has no layout for. Nothing said so.
// versionOut (0.3.3.2): the parsed version, empty when none was found.
std::string DescribeRuntimeFile(const std::filesystem::path& file, std::string* versionOut = nullptr)
{
    if (versionOut)
        versionOut->clear();
    std::ifstream in(file, std::ios::binary);
    std::error_code ec;
    const auto size = std::filesystem::file_size(file, ec);
    if (!in || ec || size == 0 || size > (64u << 20))
        return "unreadable";
    std::vector<unsigned char> data(static_cast<size_t>(size));
    in.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(size));
    if (static_cast<std::uintmax_t>(in.gcount()) != size)
        return "unreadable";
    // (0.3.4, danielblnc support) His newest builds' overlay title has no fixed "(End to close)" tail (the key is configurable),
    // so the version is read after "DLSS-NR on AMD v", with the old tail as the fallback (AmdLayout.h RuntimeVersionIn).
    const std::string version = RuntimeVersionIn(data.data(), data.size());
    if (versionOut)
        *versionOut = version;
    char digest[24] = "?";
    BCRYPT_ALG_HANDLE alg {};
    unsigned char d[32] {};
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0)
    {
        if (BCryptHash(alg, nullptr, 0, data.data(), static_cast<ULONG>(data.size()), d, 32) >= 0)
            std::snprintf(digest, sizeof digest, "%02x%02x%02x%02x%02x%02x%02x%02x", d[0], d[1], d[2], d[3], d[4], d[5], d[6], d[7]);
        BCryptCloseAlgorithmProvider(alg, 0);
    }
    return (version.empty() ? std::string("an unknown build") : "danielblnc's " + version) + " (" +
           std::to_string(size) + " bytes, SHA256 " + digest + "...)";
}
// "0.3.9" against "0.3.10", part by part; a missing part counts as 0.
int CompareRuntimeVersions(const std::string& a, const std::string& b)
{
    size_t i = 0, j = 0;
    while (i < a.size() || j < b.size())
    {
        unsigned long x = 0, y = 0;
        while (i < a.size() && a[i] != '.') x = x * 10 + static_cast<unsigned long>(a[i++] - '0');
        while (j < b.size() && b[j] != '.') y = y * 10 + static_cast<unsigned long>(b[j++] - '0');
        if (x != y)
            return x < y ? -1 : 1;
        if (i < a.size()) ++i;
        if (j < b.size()) ++j;
    }
    return 0;
}
// What to do about a pass DLL this build does not drive, from the version DescribeRuntimeFile
// parsed (0.3.3.2). "Use Runtime.zip" read as "0.4.0 changes nothing" (RE Requiem) and sent the God
// of War tester to danielblnc's own installer. Newest and oldest come from kAmdLayouts, so the
// text follows the table. full = false: the advice alone, for the one-line runtime name in the menu.
//
// Where to send someone is NOT the table: kAmdLayouts also holds builds the AMDNR release never
// shipped (recognised only so a player's own copy keeps working), and naming one of those as the fix
// sent the user after a file they never had. These are the runtime zips the release really carries (danielblnc's builds,
// unmodified, with his permission). Update them with the release.
struct ShippedRuntimeZip
{
    const char* version;
    const char* zip;
};
constexpr ShippedRuntimeZip kShippedRuntimeZips[] = {
    { "0.3.1", "Runtime.zip" }, { "0.3.2", "v0.3.2-Runtime.zip" }, { "0.3.3", "v0.3.3-Runtime.zip" }, { "0.4.0", "v0.4.0-Runtime.zip" }
};
constexpr const char* kNewestShippedRuntimeZip = "v0.4.0-Runtime.zip";
std::string UnsupportedRuntimeAdvice(const std::string& version, bool full = true)
{
    const std::string useShipped = std::string("use ") + kNewestShippedRuntimeZip + " from the AMDNR release";
    const AmdLayout* newest = kAmdLayouts[0];
    const AmdLayout* oldest = kAmdLayouts[0];
    const AmdLayout* same = nullptr;
    std::string known;
    for (auto l : kAmdLayouts)
    {
        // (0.3.4, danielblnc support) Rows named by a digest prefix are builds AMDNR does not ship (a player's own copy keeps
        // working): never compared as versions, never listed to a player whose file is not one of them.
        if (!IsVersionName(l->name))
            continue;
        if (CompareRuntimeVersions(l->name, newest->name) > 0) newest = l;
        if (CompareRuntimeVersions(l->name, oldest->name) < 0) oldest = l;
        if (version == l->name) same = l;
        known += std::string(known.empty() ? "" : ", ") + l->name + " = " + std::to_string(l->size) + " bytes";
    }
    std::string advice;
    if (version.empty())
        advice = "not a danielblnc build this AMDNR knows - " + useShipped;
    else if (same)
    {
        // The same version, other bytes: point at the release's own copy of it when there is one.
        const char* sameZip = nullptr;
        for (const auto& z : kShippedRuntimeZips)
            if (version == z.version)
                sameZip = z.zip;
        advice = "not the " + version + " file this build knows (" + std::to_string(same->size) +
                 " bytes) - danielblnc may have re-published it; update AMDNR, or use " +
                 (sameZip ? sameZip : kNewestShippedRuntimeZip) + " from the AMDNR release";
    }
    else if (CompareRuntimeVersions(version, newest->name) > 0)
        advice = "newer than this AMDNR build knows (it drives up to " + std::string(newest->name) +
                 ") - update AMDNR, or use a supported build (" + kNewestShippedRuntimeZip + " from the AMDNR release)";
    else if (CompareRuntimeVersions(version, oldest->name) < 0)
        advice = "older than supported (the oldest this build drives is " + std::string(oldest->name) + ") - use " +
                 kNewestShippedRuntimeZip + " (or Runtime.zip) from the AMDNR release";
    else
        advice = "a danielblnc version this build has no layout for - " + useShipped;
    if (!full)
        return advice;
    return advice + ". Supported pass1 files: " + known +
           ". Do not install his dlssnr_on_amd_setup.exe or its dxgi.dll / version.dll / winhttp.dll in this folder; "
           "AMDNR runs his runtime as dlssnr_amd_pass1..3.dll, all three from one runtime zip of the AMDNR release (" +
           kNewestShippedRuntimeZip + ")";
}
DXGI_FORMAT ReadFormat(DXGI_FORMAT f)
{
    switch (f)
    {
    case DXGI_FORMAT_R16G16B16A16_TYPELESS:
        return DXGI_FORMAT_R16G16B16A16_FLOAT;
    case DXGI_FORMAT_R32G32B32A32_TYPELESS:
        return DXGI_FORMAT_R32G32B32A32_FLOAT;
    case DXGI_FORMAT_R8G8B8A8_TYPELESS:
        return DXGI_FORMAT_R8G8B8A8_UNORM;
    case DXGI_FORMAT_R10G10B10A2_TYPELESS:
        return DXGI_FORMAT_R10G10B10A2_UNORM;
    default:
        return f;
    }
}
// A typed view the write-back can store through. sRGB views cannot be UAVs, so those go
// through the UNORM twin: the stored bits are what the model returned, which is already in
// the frame's own encoding when the encoding pass ran.
DXGI_FORMAT WriteFormat(DXGI_FORMAT f)
{
    switch (f)
    {
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
        return DXGI_FORMAT_R8G8B8A8_UNORM;
    case DXGI_FORMAT_B8G8R8A8_TYPELESS:
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
        return DXGI_FORMAT_B8G8R8A8_UNORM;
    default:
        return ReadFormat(f);
    }
}
} // namespace
const char* IdentifyRuntimeName(const std::filesystem::path& passDll) { return IdentifyRuntimeName(passDll, nullptr); }
const char* IdentifyRuntimeName(const std::filesystem::path& passDll, const AmdLayout** layoutOut)
{
    const char* display = nullptr;
    auto* layout = IdentifyRuntime(passDll, &display);
    if (layoutOut)
        *layoutOut = layout;
    if (layout)
        return display ? display : layout->name;
    // Not a build this host drives: the menu says which instead of "pass1?" (the bridge asks
    // once and caches the answer).
    static std::string unknown;
    std::string version;
    unknown = DescribeRuntimeFile(passDll, &version);
    unknown += " - not driven by this build: " + UnsupportedRuntimeAdvice(version, false);
    return unknown.c_str();
}
// AmdBridge's standalone check (0.3.3.2). His standalone version.dll is the same binary this host
// drives as a pass DLL, so a known hash is his; an unknown build is his when a dotted version stands
// before his overlay text (OptiScaler.dll carries that literal too, behind other data).
std::string DanielblncBuildOf(const std::filesystem::path& file)
{
    const char* display = nullptr;
    if (const auto* layout = IdentifyRuntime(file, &display))
        return display ? display : layout->name;
    std::string version;
    DescribeRuntimeFile(file, &version);
    if (version.size() >= 3 && std::isdigit(static_cast<unsigned char>(version[0])) &&
        version.find('.') != std::string::npos)
        return version;
    return {};
}

// What the backend actually loaded, or null before it has loaded anything.
//
// The menu used to call IdentifyRuntimeName every frame the Neural section was open, and
// that re-opened, re-read and re-hashed a 7.3 MB DLL from the render thread whenever the
// file's size or write time did not match its cache. The answer was already known - the
// backend identifies each pass when it loads it, a few lines further down - so the work
// was not just slow, it was redundant.
const char* LoadedRuntimeName() { return g_loadedRuntimeName.load(std::memory_order_relaxed); }
const AmdLayout* LoadedRuntimeLayout() { return g_loadedRuntimeLayout.load(std::memory_order_acquire); }
int RuntimeOwnQuality() { return g_runtimeOwnQuality.load(std::memory_order_relaxed); }
// (0.3.4, P3) Late submission (the game submits a frame's list after the next frame's Evaluate, TLOU II): -1 while the
// last window of about 600 frames had no "previous Record still awaits submission" skip, else the share of that
// window's frames NR ran on (Impl::NoteLateWindow). For the menu's attention line; any thread.
std::atomic<float> g_lateSubmitNrShare { -1.f };
float LateSubmitNrShare() { return g_lateSubmitNrShare.load(std::memory_order_relaxed); }
struct Backend::Impl
{
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12Fence> fence;
    ComPtr<ID3D12Resource> scaleBaseline, scaleOutput;
    ComPtr<ID3D12PipelineState> resolvePipeline;
    ComPtr<ID3D12PipelineState> editShapePipeline; bool editShapeFailed = false;
    // The edit shaper's last logged state (0.3.4: "" = off or never logged) and how many lines it has written.
    std::string editShapeLogKey; unsigned editShapeLogLines = 0;
    ComPtr<ID3D12Resource> motionCrop, depthCrop;
    // The slot colour as it was before the highlight proxy encoded it: the decode's original
    // (its scale) and its bound.
    ComPtr<ID3D12Resource> proxyRaw;
    // A shader-readable twin of a depth buffer the title created with
    // D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE (see Record). Same format family, no UAV.
    ComPtr<ID3D12Resource> depthReadable;
    bool loggedDepthReadable = false;
    bool loggedWriteBackRefusal = false;

    bool rtgiFailed = false;
    bool stabilizerFailed = false; // the temporal pass's D3D12 setup or a resize inside it failed: run without it
    std::string failReason;        // why `failed` is set, for the status line (Log() overwrites `status`)
    std::string rtgiStatus;
    std::unique_ptr<TemporalStability> stabilizer;
    // Last observed skip total, so the temporal pass can lean on history for the
    // one frame following a denoise skip (auto behaviour).
    UINT64 stabPrevSkips = 0;
    // Model interleave: picks the model frames and accumulates motion across the
    // skipped ones so the model is handed the total motion since it last ran. See
    // Interleave.h - the fractional accumulator this replaced could not space its
    // skips evenly, and nothing here ever corrected the motion it fed the model.
    std::unique_ptr<Interleave> interleave;
    // On-demand before/after capture for objective flicker/ghosting measurement
    // (see amd-nr-capture.md). "before" is the colour right before temporal
    // stability runs, "after" is the final colour that frame - so the dump is
    // exactly what our own pass changed, isolated from the model's own output.
    // Fires once automatically each session (skips a dark/menu sample and
    // retries) and on a dropped `amd-nr-capture.trigger` file, mirroring the
    // DX12 DlssNr path's DlssNr_Capture.h usage.
    capture::FrameCapture capture;
    UINT64 captureFrameCounter = 0;
    UINT64 captureWriteAtFrame = 0;
    bool autoCaptureDone = false;
    // The FIRST non-dark frame of a session is the moment the world appears out of the
    // loading screen: auto-exposure is still adapting (a real capture showed the scene
    // mean doubling frame to frame there) and our own history is still priming, so it
    // measures a transient rather than driving. After each successful automatic write,
    // take another sample a few seconds later and let it overwrite - the last one
    // written is of settled gameplay, which is what the flicker question is about.
    UINT64 autoCaptureRearmAtFrame = 0;
    // A capture asked for with a delay (the menu's "in 5 s" button): tick to fire at, 0 = none.
    std::atomic<UINT64> captureAtTick { 0 };
    unsigned autoCaptureRuns = 0;
    std::unique_ptr<DetailColourMix> mix;
    // RenoDX colour composition ([DlssNr] AmdComposition 1, NrCompose.h). Built on the first
    // frame that composes; Classic never builds it. A failure to build or record it drops the
    // mode for the session (composeFailed: Classic runs, the log and the menu note say why)
    // instead of latching the neural pass off. compositionState is the last state logged and
    // noted ("" = Classic, the initial state, so a Classic session writes neither), and
    // compositionWhiteSource what Stats reports (kCompositionWhite*); the menu reads it unlocked.
    std::unique_ptr<NrCompose> compose;
    bool composeFailed = false;
    std::string compositionState;
    // (0.3.4, DANIEL-GUARD) [DlssNr] AmdDanielHighlightGuard: a failure to build or record it drops the guard for the
    // session (never NR or the composition); the last logged state ("" = off or never logged) and its line count.
    bool danielGuardFailed = false;
    std::string danielGuardState;
    unsigned danielGuardLogLines = 0;
    // (0.3.4, DANIEL-033-SET) The runtime's own knobs (AmdLayout style / toneCurve / toneLift / useGameExposure),
    // driven by [DlssNr] AmdRuntimeStyle / AmdToneCurve / AmdToneLift / AmdUseGameExposure (-1 = auto = no write).
    // Per pass module and knob: `kept` is the runtime's own value (its default or its own dlssnr_on_amd.ini
    // value), read once right before the host's first write to that global - nothing else writes it in a hosted
    // runtime - and `hostOwned` says the global holds a host value now. A key back at auto writes `kept` back
    // once and the host stops writing, so "auto" is never sticky. Record's thread only, under `lock`.
    struct RuntimeKnobState
    {
        bool captured = false;
        bool hostOwned = false;
        std::uint32_t kept = 0; // the runtime's bits: int32, float, or one byte (AmdLayout)
    };
    // (0.3.4, danielblnc support) The fifth: the quality mode byte (AmdLayout quality), [DlssNr] AmdDanielFastMode.
    static constexpr int kRuntimeKnobs = 5;
    std::array<std::array<RuntimeKnobState, kRuntimeKnobs>, 3> runtimeKnobs {};
    // What the knob lines last said (pass 0): the host bits written, or -1 = the runtime's own value; and how many
    // lines they wrote (set and restore lines apart, 32 each a session: a slider drag writes one per value).
    std::array<std::int64_t, kRuntimeKnobs> knobLogged { -1, -1, -1, -1, -1 };
    unsigned knobSetLines = 0, knobRestoreLines = 0;
    // A key with a value on a runtime that does not map its knob: said once per knob and backend (G2 review).
    std::array<bool, kRuntimeKnobs> knobUnmappedLogged {};
    // (0.3.4, P7.10) NR cost for Stats::nrGpuMs (the menu's readout; nothing else reads it): a timestamp pair on the
    // game's list around the passes' Record calls of a model frame - the game queue's time inside NR, which is the
    // runtime's inline wait for its network - resolved into the slot's readback entry and read when the slot retires
    // (its fence has passed, so the list ran). The mean of the last kGpuTimeWindow answered model frames; -1 until the
    // first window closes, and always on OptiScaler's Vulkan and D3D11 bridges (n/a there). One heap and one 16-byte-
    // per-slot readback, created on the first timed frame, released with the backend; a failure leaves the readout at
    // -1 for the session and changes nothing else. Timed only while something reads the readout (GetStats within the
    // last 2 s: the Neural tab polls it each menu frame), so with the menu closed the game's lists get no extra command
    // at all. The value is live only while the readout is polled and an answered model frame was timed in the last
    // 2 s (gpuTimedAt): otherwise GetStats reports -1, and Record starts a fresh window (-1 until it closes; a slot of
    // the old window that retires later is not counted, gpuTimeWindowId), so NR off, a stopped backend, a loading
    // screen or a reopened menu never shows an old value (G2 review).
    static constexpr UINT kGpuTimeWindow = 60;
    std::atomic<ULONGLONG> statsPolledAt { 0 };
    std::atomic<ULONGLONG> gpuTimedAt { 0 }; // tick of the last answered model frame whose pair was recorded
    UINT gpuTimeWindowId = 0;                // bumped at every window restart; a slot keeps the id it was timed in
    bool gpuTimeActive = false;
    ComPtr<ID3D12QueryHeap> gpuTimeHeap;
    ComPtr<ID3D12Resource> gpuTimeReadback;
    UINT64 gpuTimeFrequency = 0;
    bool gpuTimeFailed = false;
    double gpuTimeSum = 0.0;
    UINT gpuTimeCount = 0;
    std::atomic<float> nrGpuMs { -1.f };
    std::atomic<UINT> compositionWhiteSource { kCompositionWhiteNone };
    // (P3, 0.3.3.2) Stats::modelHistoryInUse: the last value written to the runtime's temporal flag
    // (read unlocked by the menu), and the interleave / preset / history the ghost meter's reading
    // belongs to - a change clears the reading (TemporalStability::ClearModelGhost).
    std::atomic<bool> modelHistoryInUse { false };
    // (0.3.4, LF-C) Stats::modelFrameSeen: the model has answered at least once in this backend's life, so
    // modelHistoryInUse above holds a value the runtime was really given (it is written on model frames only); until
    // then the menu's ghost line waits instead of saying "Model history off". Read unlocked by the menu.
    std::atomic<bool> modelFrameSeen { false };
    int ghostMeterKey = -1;

    ComPtr<ID3D12Resource> lookColour;
    ComPtr<ID3D12PipelineState> lookPipeline;
    ComPtr<ID3D12PipelineState> proxyPipeline;
    bool proxyLogged = false;
    ComPtr<ID3D12DescriptorHeap> heap;
    ComPtr<ID3D12RootSignature> root;
    ComPtr<ID3D12PipelineState> pipeline;
    ComPtr<ID3D12PipelineState> depthPipeline;
    ComPtr<ID3D12PipelineState> motionPipeline, exposurePipeline;
    UINT lastMotionWidth = 0, lastMotionHeight = 0;
    UINT lastGuideWidth = 0, lastGuideHeight = 0;
    // Previous frame's TAA jitter (render pixels), for the temporal pass's reprojection.
    float lastJitterX = 0.f, lastJitterY = 0.f;
    bool haveJitter = false;
    UINT lastInputWidth=0,lastInputHeight=0;
    bool hadExposure = false;
    std::array<HMODULE, 3> runtime {};
    std::array<UINT, 3> observedTimeouts {};
    std::filesystem::path directory;
    std::string status = "AMD pre-SR: not initialized";
    std::atomic<bool> failed { false };
    std::atomic<bool> resetRequested { true };
    Settings lastSettings {};
    bool haveSettings = false;
    bool loggedGhostBound = false;
    bool loggedGuidedFill = false;
    // #5 still-surface steadiness: the value and path last logged (-1 = never), so the line follows a change.
    float loggedStaticRelax = -1.f;
    int loggedStaticRelaxPath = -1;
    // Edit accumulation (interleave preset 10): the one-shot "active" line, and the periodic
    // line's counters - stabiliser runs under the preset, of those with an answer / without one
    // since the last line, and where the refused-call and skip totals stood at the last line.
    bool loggedAccumulation = false;
    UINT64 accumRuns = 0, accumLogAt = 0, accumModel = 0, accumFilled = 0, accumRefusedAt = 0, accumSkipsAt = 0;
    // Model frames whose Record the runtime declined (nothing answered): filled frames to the pass.
    UINT64 refusedCalls = 0;
    // On OptiScaler's Vulkan-on-D3D12 bridge (Settings::vulkanBridge), each logged once: the
    // post-submit job wait is skipped (WaitAfterSubmitIfEveryFrame), and Neural passes 2-3 run as 1
    // (Record).
    bool loggedVkSubmitWait = false;
    bool loggedVkPassClamp = false;
    // Vulkan bridge gap diagnostic (Record): the tick at which the previous Record returned (0 =
    // none yet), and how many gaps of more than a second there have been. The first 10 are logged,
    // then every 10th.
    ULONGLONG lastRecordTick = 0;
    UINT64 vkRecordGaps = 0;
    // Latch failures that invalidate the shared completion timeline. Such work
    // remains pinned rather than being retired using an untrustworthy fence.
    bool completionOrderValid = true;
    bool graphicsFallbackReported = false;
    bool graphicsUnavailableReported = false; // "graphics wait unavailable in this build", once
    // Set by a resize, cleared by the first job completed after it, which logs memory again.
    bool memoryAfterResize = false;
    // Last admission verdict, so the log records the transitions rather than a line a frame.
    bool graphicsAdmitPrev = false;
    bool rawNetworkPrev = false;
    UINT64 frames = 0, serial = 0;
    UINT64 lastSubmitted = 0, lastCompleted = 0, completedFrames = 0;
    std::atomic<UINT64> completedForActivity { 0 };
    UINT64 pendingSkips = 0, fenceSkips = 0, fenceRecoveries = 0;
    // Frames the model actually ran on. With interleave off this tracks `frames`;
    // with it on the ratio between them is the cadence the user is really getting.
    UINT64 modelFrames = 0;
    UINT64 boostFrames = 0; // model frames the adaptive interleave forced onto skipped slots
    UINT64 retryAfter = 0, timeoutEvents = 0;
    bool resetAfterTimeout = false;
    // Per-job state and the GPU resources that job borrows. The original runtime is a single worker
    // on one HIP stream, so slots never run concurrently: an extra slot only
    // lets the CPU record the next frame while the previous job is still
    // retiring, instead of blocking the render thread in Submitted.
    //
    // Too few slots and a frame that finds every buffer busy is recorded with no
    // NR at all. Slot count therefore changes denoise coverage as well as frame
    // timing; the available aggregate logs do not establish a causal feedback
    // direction between skips and later capture waits.
    // Measured with the count flipped mid-run at one standing position:
    //
    //   Onimusha  2/3 slots: 0 skips; no frame-time difference detected
    //   YYSLS AB  2 slots: about 1200-1440 counter increments/segment; 3: 0
    //   YYSLS     separate 60 s sweep: 1800 at 2 slots; 0 at 3, 4 and 5
    //
    // The YYSLS "win" at two slots is frames that carried no NR at all. The
    // count is the runtime's own skip counter. The AB log segments and the
    // 45-second PresentMon windows do not share boundaries, so no skip rate is
    // derived from them and the separate sweep is not compared numerically.
    static constexpr UINT kMaxSlots = 5;
    // kDefaultSlots == 1 reproduces the original one-frame-outstanding behaviour
    // exactly, which is what the control build is for. The macro used to work the
    // other way round, so every build that forgot to define it silently produced
    // a lower-throughput single-slot build - a trap that caught this project once.
    // AMD_SINGLESLOT now only moves the default; the option can still raise it.
#ifdef AMD_SINGLESLOT
    static constexpr UINT kDefaultSlots = 1;
#else
    static constexpr UINT kDefaultSlots = 3;
#endif
    struct Slot
    {
        std::atomic<ID3D12CommandList*> pending { nullptr };
        std::atomic<UINT64> completion { 0 };
        std::array<UINT, 3> jobs {};
        // Passes recorded into THIS slot. Global activePasses is only the
        // config for the next Record; a still-in-flight slot must retire and
        // notify against the count it was recorded with (hot 1↔2 pass change).
        // UINT_MAX = never recorded this slot. 0 is a real value (the runtime refused).
        static constexpr UINT kPassUnset = 0xffffffffu;
        UINT passCount = kPassUnset;
        SubmissionState submission;
        ComPtr<ID3D12CommandQueue> submissionQueue;
        std::unique_ptr<ColorEncoding> decode, encode;
        // The original runtime reads this and writes its correction back into it (in place), so no
        // two outstanding jobs may share one.
        ComPtr<ID3D12Resource> colour;
        ComPtr<ID3D12Resource> exposureCopy;
        // (0.3.3.2, [DlssNr] AmdNeuralListRecovery) The game's list this slot's work was recorded into,
        // held until the slot retires: its address cannot come back as another list meanwhile, and a
        // list only this reference still holds is one the game released. `resetSeen` is the list
        // ListReset reported (any thread, no lock), `resetCertain` whether OptiScaler's own bridge
        // dropped it. `discarded`: the job was completed here without the game's list (RecoverDiscarded,
        // `notified` passes so far, since `discardedAt`); it retires without counting as a frame.
        // `blockedBase`: unsubmittedSkips when this slot was recorded.
        ComPtr<ID3D12CommandList> listRef;
        std::atomic<ID3D12CommandList*> resetSeen { nullptr };
        std::atomic<bool> resetCertain { false };
        bool discarded = false;
        UINT notified = 0;
        UINT64 discardedAt = 0, blockedBase = 0;
        // (0.3.4, P7.10) This generation's list carries an answered model frame's NR timestamp pair (query entries
        // 2k, 2k+1, resolved to readback bytes 16k..16k+15); read once when the slot retires. gpuTimeBegin: the begin
        // stamp read last time, so an entry the GPU did not rewrite is never counted twice.
        bool gpuTimed = false;
        UINT64 gpuTimeBegin = 0;
        UINT gpuTimeWindow = 0; // Impl::gpuTimeWindowId when the pair was recorded
    };
    // Every slot gets its own copy of the whole descriptor block. A single
    // shared block aliases across slots: Record repoints descriptor 1 at the
    // active slot's colour, so a list still executing for the other slot would
    // read and write the wrong texture. Design section 3.3 forbids that, and
    // section 3.2 already asked for 14 per slot - this is that.
    // 16: pairs at +0 (model colour), +2, +4 (motion), +6 (exposure), +8 (look), +10/+12
    // (resolve), +14 (highlight proxy, in place on the slot colour).
    // +16: the post-RR write-back (FP16 result -> the game's output, typed view).
    // +18/+19: the raw copy the highlight proxy's encode and decode take their scale from and
    // the decode is bounded by (t1, t2).
    // +20/+21: the edit shaper (t0 = pre-model copy, u0 = slot colour, in place).
    static constexpr UINT kDescriptorsPerSlot = 22;
    static constexpr UINT kDescriptors = kDescriptorsPerSlot * kMaxSlots;
    std::array<Slot, kMaxSlots> slots;
    // What the option asks for this frame, and how many buffers actually exist.
    // They differ for one frame at most: wantSlots is read straight from the
    // settings so a change takes effect immediately, and liveSlots trails it
    // until the buffers have been brought into line. Slot structs above
    // liveSlots hold no texture, so asking for three reserves memory for three.
    UINT wantSlots = kDefaultSlots;
    UINT liveSlots = 0;
    UINT activeSlot = 0;
    UINT skipWaits = 0;
    UINT recordCalls = 0;
    UINT unsubmittedSkips = 0;
    // (0.3.4, P3) Late-submission window: frames NR ran on vs frames skipped because the previous frame's list was not
    // submitted yet, over about 600; one log line per window that had such a skip (danielblnc's runtime holds one
    // unsubmitted job, so those frames get no NR; the full fix is 0.3.5).
    UINT lateWindowRan = 0, lateWindowLate = 0;
    void NoteLateWindow(bool late)
    {
        ++(late ? lateWindowLate : lateWindowRan);
        const UINT total = lateWindowRan + lateWindowLate;
        if (total < 600)
            return;
        g_lateSubmitNrShare.store(lateWindowLate ? float(lateWindowRan) / float(total) : -1.f, std::memory_order_relaxed);
        if (lateWindowLate)
        {
            char line[400];
            std::snprintf(line, sizeof line,
                          "AMD late submission: NR ran on %.1f%% of the last %u frames; on the other %u the game had not yet "
                          "submitted the previous frame's list (it submits after the next frame's Evaluate) and danielblnc's "
                          "runtime holds one unsubmitted job, so they got no NR (lmxxf gives such a job one frame of grace)",
                          100.0 * double(lateWindowRan) / double(total), total, lateWindowLate);
            AppendLog(line);
        }
        lateWindowRan = lateWindowLate = 0;
    }
    // The original runtime joins its workers and clears the abort buffer while it rebuilds staging,
    // which it does after a resize, a re-created upscaler context or an INI
    // change. While that is in flight the extra slot must not be used to skip
    // the Submitted wait - doing so hung the game.
    //
    // A timer cannot guard this: the rebuild happens on whichever later Record
    // The original runtime chooses, so any window simply expires first and the crash follows. It
    // publishes its own decision as a sticky byte instead - set when it detects
    // the change, cleared only after it has drained the queue and joined its
    // workers - and a rebuild happens on exactly those calls that read 1 at
    // entry. Reading it is therefore the real guard.
    bool NativeRebuilding() const
    {
        if (!L || !L->recreate) return true; // unknown layout: assume the worst
        for (UINT i = 0; i < runtime.size(); ++i)
            if (auto h = runtime[i])
                if (At<volatile uint8_t>(h, L->recreate) != 0) return true;
        return false;
    }
    // True while any slot still owns the resources its job borrowed.
    bool AnySlotBusy() const
    {
        for (size_t k = 0; k < slots.size(); ++k)
            if (slots[k].pending.load(std::memory_order_acquire)) return true;
        return false;
    }
    bool HasUnsubmitted() const
    {
        for (const auto& sl : slots)
            if (sl.pending.load(std::memory_order_acquire) && sl.submission.BlocksRecord())
                return true;
        return false;
    }
    // Prefilter: collect every slot whose recorded list appears in `lists`.
    // Only the atomic pending pointer is read here. Callers (Submitting/Submitted)
    // must NOT hold p->lock yet — Record holds that lock across the runtime
    // Record call, and taking it first deadlocks on same-thread re-entry.
    //
    // This must not stop at the first match. The next frame can reuse the very
    // same command-list pointer while an older slot holding that pointer is
    // still retiring, so one batch can match two slots. Returning the older,
    // already-submitted one made the caller's lock-held check fail and return,
    // and the newer slot was then never submitted: it stayed occupied until the
    // 5s abandon and every Record in between was skipped. `submitted` is a plain
    // bool written under p->lock, so it cannot be read here; hand every
    // candidate to the caller, which holds the lock and can choose.
    UINT FindPendingCandidates(UINT n, ID3D12CommandList* const* lists,
                               std::array<UINT, kMaxSlots>& outSlots,
                               std::array<ID3D12CommandList*, kMaxSlots>& outPending) const
    {
        UINT count = 0;
        for (size_t k = 0; k < slots.size(); ++k)
        {
            auto candidate = slots[k].pending.load(std::memory_order_acquire);
            if (!candidate)
                continue;
            for (UINT i = 0; i < n; ++i)
            {
                if (lists[i] != candidate)
                    continue;
                outSlots[count] = static_cast<UINT>(k);
                outPending[count] = candidate;
                ++count;
                break;
            }
        }
        return count;
    }
    // Requires p->lock. Picks the first candidate that is still that slot's
    // pending list and has not been submitted yet.
    bool PickUnsubmitted(const std::array<UINT, kMaxSlots>& candSlots,
                         const std::array<ID3D12CommandList*, kMaxSlots>& candPending, UINT count,
                         UINT& outSlot, ID3D12CommandList*& outPending) const
    {
        for (UINT c = 0; c < count; ++c)
        {
            const auto& sl = slots[candSlots[c]];
            // (0.3.3.2) A discarded slot is RecoverDiscarded's alone: the list object came back with
            // other work on it, and its job's passes are (being) notified there.
            if (sl.pending.load() == candPending[c] && !sl.submission.submitted && !sl.discarded)
            {
                outSlot = candSlots[c];
                outPending = candPending[c];
                return true;
            }
        }
        return false;
    }
    // Highest fence value any slot is still waiting on.
    UINT64 LatestCompletion() const
    {
        UINT64 value = 0;
        for (size_t k = 0; k < slots.size(); ++k)
            value = (std::max)(value, slots[k].completion.load());
        return value;
    }
    bool deviceLostReported = false;
    UINT width = 0, height = 0, activePasses = 0, lastPasses = 0;
    HipSetFn hipSet = nullptr;
    int hipDevice = -1;
    const AmdLayout* L = nullptr;
    std::mutex lock;
    // (0.3.3.2, exit) The thread inside Record, Submitting or Submitted with `lock` held (LockOwnerMark,
    // AmdPreSr.h), for Shutdown on that same thread.
    std::atomic<DWORD> lockOwner { 0 };
    // (0.3.3.2) Discarded lists recovered this session (RecoverDiscarded), the Records that found NR paused
    // behind one, and the once-only note that a recovered job did not retire in time.
    UINT64 discardedLists = 0, discardPausedSkips = 0;
    bool loggedDiscardOverdue = false;
    // (0.3.3.2) Neural lists Submitted found under the pointer they were recorded with. Until one has, a
    // reset or released list proves nothing: behind a wrapper (UE5 titles) every list runs under another
    // pointer and never matches, and recovering it would publish a job for a list that already ran.
    UINT64 submittedByPointer = 0;
    // The last status line, and a lock of its own. Separate from `lock` on purpose: this
    // one is held for a string copy and nothing else, so the menu can always get an answer
    // without waiting on whatever the recording path is doing.
    mutable std::mutex statusTextLock;
    mutable std::string statusText;
#ifdef AMD_RETIRE_DIAGNOSTICS
    RetirementDiagnostics diagnostics;
#endif
    // The temporal pass is optional; the neural pass is not. Its setup failing used to latch the
    // whole backend off ("backend stopped": the Where Winds Meet report, E_FAIL at the first Run
    // on every launch while 0.1.0 ran). Created here, once; a failure costs the pass for the
    // session and the log names the call that failed.
    bool EnsureStabilizer()
    {
        if (stabilizerFailed)
            return false;
        if (stabilizer)
            return true;
        try
        {
            stabilizer = std::make_unique<TemporalStability>(device.Get());
            return true;
        }
        catch (const std::exception& e)
        {
            stabilizerFailed = true;
            Log(std::string("AMD temporal stability unavailable for this session (the neural pass runs without it): ") + e.what());
            return false;
        }
    }
    void Log(const std::string& s)
    {
        status = s;
        AppendLog(s);
    }
    // Log's file half, without the status line (0.3.3.2, Backend::NoteLine: an NR toggle note from the
    // menu overwrote a stopped backend's "AMD idle: stopped - <why>" for the rest of the session).
    void AppendLog(const std::string& s)
    {
        std::ofstream out(directory / L"amd_presr.log", std::ios::app);
        // The first line this process writes is the session header (0.3.3.2): the build, exe and pid
        // behind the lines below had to be guessed from the wording of the refusal text.
        static std::atomic<bool> headerWritten { false };
        if (out && !headerWritten.exchange(true))
            out << GetTickCount64() << " " << DlssNr::AmdBridge::SessionHeader() << '\n';
        out << GetTickCount64() << " " << s << '\n';
    }
    void TraceBoundary(const std::string& reason)
    {
        // A diagnostic snapshot, not a state (0.3.3.2): a backend that had already stopped keeps its
        // "AMD idle: stopped - <why>" on the status line. Record writes that line once per cause, so a
        // context release after a refusal replaced the reason for the rest of the session.
        const bool wasFailed = failed.load();
        const std::string keptStatus = wasFailed ? status : std::string();
        struct RestoreStatus
        {
            Impl* impl;
            bool on;
            const std::string& kept;
            ~RestoreStatus()
            {
                if (on)
                    impl->status = kept;
            }
        } restoreStatus { this, wasFailed, keptStatus };
        const auto gpu = fence ? fence->GetCompletedValue() : 0;
        const auto removed = device->GetDeviceRemovedReason();
          // Report the first slot with work outstanding; with one slot that
          // is the only one, so the line keeps the original format.
          const Slot* traced = &slots[0];
          for (size_t k = 0; k < slots.size(); ++k)
              if (slots[k].pending.load(std::memory_order_acquire)) { traced = &slots[k]; break; }
        Log("AMD boundary: " + reason + " pending=" +
            std::to_string(reinterpret_cast<uintptr_t>(traced->pending.load())) +
            " submitted=" + std::to_string(traced->submission.submitted) +
            " recordedAt=" + std::to_string(traced->submission.recordedAt) +
            " submittedAt=" + std::to_string(traced->submission.submittedAt) +
            " fence=" + std::to_string(gpu) + "/" + std::to_string(traced->completion.load()) +
            " deviceHR=" + std::to_string(static_cast<UINT>(removed)) +
            " NR=" + std::to_string(width) + "x" + std::to_string(height));
        for (UINT i = 0; i < runtime.size(); ++i)
            if (auto h = runtime[i])
                Log("AMD boundary pass " + std::to_string(i + 1) + " native=" +
                    std::to_string(At<UINT>(h, L->jobDone)) + "/" + std::to_string(traced->jobs[i]) +
                    " nativePending=" + std::to_string(reinterpret_cast<uintptr_t>(At<void*>(h, L->pendingList))) +
                    " timeouts=" + std::to_string(At<UINT>(h, L->timeoutCount)));
        if (FAILED(removed) && !deviceLostReported)
        {
            deviceLostReported = true;
            failed = true;
            // Read whatever DRED the game/OS collected. Do not change device
            // creation settings or globally enable a debug layer in the game.
            ComPtr<ID3D12DeviceRemovedExtendedData1> dred;
            if (SUCCEEDED(device.As(&dred)))
            {
                D3D12_DRED_PAGE_FAULT_OUTPUT1 fault {};
                const auto hr = dred->GetPageFaultAllocationOutput1(&fault);
                if (SUCCEEDED(hr))
                    Log("AMD DRED page fault: VA=" + std::to_string(fault.PageFaultVA));
                else
                    // 0x887a0004: driver did not report a page fault. Do not
                    // print "page fault" when the API said there was none.
                    Log("AMD DRED page fault: not available hr=" +
                        std::to_string(static_cast<UINT>(hr)));
                D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT1 breadcrumbs {};
                const auto bh = dred->GetAutoBreadcrumbsOutput1(&breadcrumbs);
                if (SUCCEEDED(bh))
                {
                    Log("AMD DRED breadcrumbs: available");
                    UINT count = 0;
                    for (auto node = breadcrumbs.pHeadAutoBreadcrumbNode; node && count++ < 16;
                         node = node->pNext)
                        Log("AMD DRED list=" +
                            std::to_string(reinterpret_cast<uintptr_t>(node->pCommandList)) +
                            " progress=" +
                            std::to_string(node->pLastBreadcrumbValue ? *node->pLastBreadcrumbValue : 0) +
                            "/" + std::to_string(node->BreadcrumbCount));
                }
                else
                    Log("AMD DRED breadcrumbs: not available hr=" +
                        std::to_string(static_cast<UINT>(bh)));
            }
        }
    }
    // Diagnostic dump of every slot. Used when slots refuse to retire (yysls
    // skip/stall) so the next field run can show which state is stuck.
    void LogSlotSnapshot(const char* reason)
    {
        const auto gpuDone = fence ? fence->GetCompletedValue() : UINT64_MAX;
        Log(std::string("AMD slot-snap: ") + reason + " fence=" + std::to_string(gpuDone) +
            " lastSubmitted=" + std::to_string(lastSubmitted) + " completedFrames=" +
            std::to_string(completedFrames) + " pendingSkips=" + std::to_string(pendingSkips) +
            " nativeRebuild=" + std::to_string(NativeRebuilding() ? 1 : 0) +
            " failed=" + std::to_string(failed ? 1 : 0));
        for (size_t k = 0; k < slots.size(); ++k)
        {
            const auto& sl = slots[k];
            const auto pending = sl.pending.load(std::memory_order_acquire);
            const auto target = sl.completion.load();
            const UINT passCount = (sl.passCount == Slot::kPassUnset) ? 0u : sl.passCount;
            std::string jobs;
            std::string dones;
            for (UINT i = 0; i < static_cast<UINT>(sl.jobs.size()); ++i)
            {
                if (i) { jobs += ","; dones += ","; }
                jobs += std::to_string(sl.jobs[i]);
                UINT done = 0;
                if (L && i < runtime.size() && runtime[i])
                    done = static_cast<UINT>(InterlockedCompareExchange(
                        reinterpret_cast<volatile LONG*>(&At<UINT>(runtime[i], L->jobDone)), 0, 0));
                dones += std::to_string(done);
            }
            Log("AMD slot-snap k=" + std::to_string(k) +
                " pending=" + std::to_string(reinterpret_cast<uintptr_t>(pending)) +
                " submitted=" + std::to_string(sl.submission.submitted ? 1 : 0) +
                " recordedAt=" + std::to_string(sl.submission.recordedAt) +
                " submittedAt=" + std::to_string(sl.submission.submittedAt) +
                " passCount=" + std::to_string(passCount) +
                " jobs=[" + jobs + "] jobDone=[" + dones + "]" +
                " completion=" + std::to_string(target) +
                " fenceOk=" + std::to_string(gpuDone != UINT64_MAX && target != 0 && gpuDone >= target ? 1 : 0));
        }
    }
    // Retire one slot if its job has finished. Called with `lock` held. Keep
    // every borrowed resource alive until BOTH native inference and the actual
    // D3D12 submission have retired.
    void RetireSlot(UINT k, bool waitForGpu, const char* source
#ifdef AMD_RETIRE_DIAGNOSTICS
                    , RetirementDiagnostics::Event* sample
#endif
                    )
    {
        Slot& sl = slots[k];
        if (!sl.pending.load(std::memory_order_acquire))
            return;
        if (!completionOrderValid)
            return;
        bool nativeDone = true;
        bool timedOut = false;
        // Use this slot's recorded pass count, not the global config.
        // 0 is valid (the runtime refused); only kPassUnset means "never recorded".
        const UINT passCount = (sl.passCount == Slot::kPassUnset) ? 0u : sl.passCount;
        for (UINT i = 0; i < passCount; ++i)
        {
            const auto done = static_cast<UINT>(InterlockedCompareExchange(
                reinterpret_cast<volatile LONG*>(&At<UINT>(runtime[i], L->jobDone)), 0, 0));
            nativeDone &= sl.jobs[i] != 0 && done >= sl.jobs[i];
#ifdef AMD_RETIRE_DIAGNOSTICS
            // sample is null for every slot except the one RetireSubmission
            // chose to instrument. Writing through it crashed a two-slot build as soon
            // as two slots were pending (s13/s14).
            if (sample)
                sample->done[i] = done;
#endif
            timedOut |= At<UINT>(runtime[i], L->timeoutCount) > observedTimeouts[i];
        }
        auto gpuDone = fence->GetCompletedValue();
        if (gpuDone == UINT64_MAX && !deviceLostReported)
            TraceBoundary("device removed while retiring");
        const auto target = sl.completion.load();
#ifdef AMD_RETIRE_DIAGNOSTICS
        if (sample)
        {
            sample->nativeDone = nativeDone; // Exactly the value passed to CanRetire.
            sample->gpuBefore = gpuDone;
            sample->target = target;
        }
#endif
        // Preserve the existing short recording-thread wait, but never block
        // on a list that the game has not submitted yet, or from Status().
        if (waitForGpu && sl.submission.submitted && nativeDone && gpuDone < target)
        {
#ifdef AMD_RETIRE_DIAGNOSTICS
            const auto waitStart = RetirementDiagnostics::Clock();
#endif
            const auto start = GetTickCount64();
            while (gpuDone < target && GetTickCount64() - start < 16)
            {
                Sleep(1);
                gpuDone = fence->GetCompletedValue();
            }
#ifdef AMD_RETIRE_DIAGNOSTICS
            if (sample)
                sample->waitMs = diagnostics.Milliseconds(RetirementDiagnostics::Clock() - waitStart);
#endif
        }
#ifdef AMD_RETIRE_DIAGNOSTICS
        if (sample)
        {
            sample->gpuAfter = gpuDone;
            sample->retired = sl.submission.CanRetire(nativeDone, gpuDone, target);
        }
#endif
        if (sl.submission.CanRetire(nativeDone, gpuDone, target))
        {
            sl.pending.store(nullptr, std::memory_order_release);
            sl.passCount = Slot::kPassUnset;
            sl.submission = {};
            sl.submissionQueue.Reset();
            // Clear the fence target. A later Record must not inherit a stale
            // completion from a previous generation (it made fenceOk look true
            // for a list that was never submitted).
            sl.completion.store(0, std::memory_order_release);
            lastSubmitted = GetTickCount64();
            // (0.3.3.2) A job completed without the game's list (RecoverDiscarded) answered nothing any
            // frame used: said, and not counted as a completed frame.
            const bool wasDiscarded = sl.discarded;
            if (wasDiscarded)
                Log("AMD neural: the discarded list's job retired " + std::to_string(lastSubmitted - sl.discardedAt) +
                    " ms after it was completed without the list; NR resumes with fresh history");
            sl.discarded = false;
            sl.notified = 0;
            sl.listRef.Reset();
            sl.resetSeen.store(nullptr, std::memory_order_relaxed);
            sl.resetCertain.store(false, std::memory_order_relaxed);
            if (!failed && passCount && !timedOut && !wasDiscarded)
            {
                ReadGpuTime(sl, k); // (0.3.4, P7.10) the list ran: its NR timestamp pair, if it has one
                ++completedFrames;
                completedForActivity.fetch_add(1, std::memory_order_release);
                lastCompleted = lastSubmitted;
                status = "Completed AMD pre-SR passes=" + std::to_string(passCount) + " at " +
                         std::to_string(width) + "x" + std::to_string(height);
                if (completedFrames <= 3 || completedFrames % 120 == 0)
                    Log(status);
                // HEARTBEAT, for the report this log could not answer.
                //
                // A tester reported neural rendering in RDR2 "doing less and less over a few
                // minutes until it stops changing the image". The logs that came with it turned
                // out to be a tuning session - every change in passes and resolution in them is
                // the tester's own hand on a slider - so there was nothing in the file that could
                // have shown a drift, and nothing worth guessing from.
                //
                // This prints the quantities that would tell the two candidate stories apart, on
                // a slow enough cadence to be free: if the geometry and pass count hold steady
                // while the picture stops changing, the model is converging or our composition
                // is; if the model's call time collapses or the runtime's completed count stops
                // advancing, the pass is no longer running at all. One log from a degraded
                // session now decides it without another round trip.
                //
                // Memory rides along (AmdBridge::MemoryTelemetry, the bridge's so that both hosts
                // can print the same figures): at fixed settings danielblnc should hold flat, so a
                // climb between two heartbeats with no resize line between them is a leak.
                if (completedFrames % 1800 == 0)
                {
                    std::string native;
                    for (UINT i = 0; i < runtime.size(); ++i)
                        if (auto rh = runtime[i])
                            native += " p" + std::to_string(i + 1) + "done=" +
                                      std::to_string(At<UINT>(rh, L->jobDone)) + " p" +
                                      std::to_string(i + 1) +
                                      "to=" + std::to_string(At<UINT>(rh, L->timeoutCount));
                    Log("AMD heartbeat: completed=" + std::to_string(completedFrames) +
                        " recorded=" + std::to_string(frames) + " passes=" + std::to_string(passCount) +
                        " at " + std::to_string(width) + "x" + std::to_string(height) +
                        " timeoutEvents=" + std::to_string(timeoutEvents) + native +
                        DlssNr::AmdBridge::MemoryTelemetry(device.Get()));
                }
                // The "after" of a resize line: the first job completed at the new size ran on
                // staging the runtime rebuilt for it, so the difference is what the size cost.
                if (memoryAfterResize)
                {
                    memoryAfterResize = false;
                    Log("AMD memory after the rebuild at " + std::to_string(width) + "x" + std::to_string(height) +
                        " passes=" + std::to_string(passCount) + ":" + DlssNr::AmdBridge::MemoryTelemetry(device.Get()));
                }
            }
            return;
        }
        if (sl.submission.ReportStall(GetTickCount64()))
        {
            Log("AMD submission stalled >5s; retaining list/resources until completion. submitted=" +
                std::to_string(sl.submission.submitted) + " nativeDone=" + std::to_string(nativeDone) +
                " passes=" + std::to_string(passCount) + " fence=" + std::to_string(gpuDone) +
                "/" + std::to_string(target));
            LogSlotSnapshot("stall");
        }
    }
    void RetireSubmission(bool waitForGpu = false, const char* source = "Unknown"
#ifdef AMD_RETIRE_DIAGNOSTICS
                          , RetirementDiagnostics::Event* recordEvent = nullptr
#endif
                          )
    {
#ifdef AMD_RETIRE_DIAGNOSTICS
        RetirementDiagnostics::Scope timing(diagnostics, directory, L ? L->name : "uninitialized", source, recordEvent);
        auto& sample = timing.event;
        sample.passes = activePasses;
        sample.width = width;
        sample.height = height;
        sample.everyFrame = haveSettings && lastSettings.everyFrame;
        // Sample the first slot with work outstanding. In single-slot mode that is
        // the only slot, so the recorded diagnostic matches the original.
        UINT sampled = kMaxSlots;
        for (UINT k = 0; k < kMaxSlots; ++k)
            if (slots[k].pending.load(std::memory_order_acquire))
            {
                sampled = k;
                break;
            }
        if (sampled < kMaxSlots)
        {
            sample.pending = reinterpret_cast<uintptr_t>(slots[sampled].pending.load(std::memory_order_acquire));
            sample.submitted = slots[sampled].submission.submitted;
            sample.recordedAt = slots[sampled].submission.recordedAt;
            sample.submittedAt = slots[sampled].submission.submittedAt;
            sample.jobs = slots[sampled].jobs;
            sample.target = slots[sampled].completion.load();
            for (UINT k = 0; k < kMaxSlots; ++k)
                RetireSlot(k, waitForGpu, source, k == sampled ? &sample : nullptr);
        }
#else
        for (UINT k = 0; k < kMaxSlots; ++k)
            RetireSlot(k, waitForGpu, source);
#endif
    }
    // (0.3.3.2, [DlssNr] AmdNeuralListRecovery) A NEURAL LIST THE GAME DISCARDED. The runtime owns one
    // not-yet-notified job, and Notify is the only way it lets go of it: a list recorded here and never
    // executed kept HasUnsubmitted() true, and every Record after it was skipped for the rest of the session
    // ("previous Record still awaits submission"). Discarded is decided on evidence only:
    //   - one of OptiScaler's bridges dropped the list after Close (certain);
    //   - ListReset saw the game reset it (what was recorded on it can never run), once this game's neural
    //     lists are known to reach ExecuteCommandLists under the pointer they were recorded with
    //     (submittedByPointer): behind a wrapper the list ran under another pointer and a reset proves
    //     nothing, so such a title stays as it was (NR pinned after its first list);
    //   - or the list is released: after 8 skipped Records and a second, only listRef still holds it
    //     (the same condition).
    // A late submission is never guessed at: a list the game still holds and has not reset keeps waiting,
    // as before. The job is then completed the way a list whose capture never lands already is in the
    // field (dlssnr_on_amd.log: "its capture never landed ... the game did not execute the command list",
    // the job done about 3.5 s later, no timeout, the runtime carrying on): the same per-pass Notify
    // Submitted makes, on the bound queue, one pass at a time as Submitted publishes them, then a fence
    // Signal so the slot can retire. Nothing that job answers reaches a frame (its list is gone), and
    // until it retires Record records nothing (DiscardOutstanding), so no new job queues behind its wait
    // and the game's queue never waits on it. History restarts. Requires `lock`.
    void RecoverDiscarded()
    {
        const auto now = GetTickCount64();
        for (UINT k = 0; k < kMaxSlots; ++k)
        {
            Slot& sl = slots[k];
            ID3D12CommandList* const pend = sl.pending.load(std::memory_order_acquire);
            if (!pend)
                continue;
            if (!sl.discarded)
            {
                ID3D12CommandList* const seen = sl.resetSeen.exchange(nullptr, std::memory_order_acq_rel);
                const bool certain = sl.resetCertain.exchange(false, std::memory_order_acq_rel);
                if (sl.submission.submitted)
                    continue; // reset after its submission: the game reusing the list, as every frame
                const char* how = seen != pend                    ? nullptr
                                  : certain                       ? "was dropped by OptiScaler's bridge without being executed"
                                  : submittedByPointer > 0        ? "was reset by the game before it was executed"
                                                                  : nullptr;
                if (!how && sl.listRef && submittedByPointer > 0 && unsubmittedSkips - sl.blockedBase >= 8 &&
                    now - sl.submission.recordedAt >= 1000)
                {
                    sl.listRef->AddRef();
                    if (sl.listRef->Release() == 1)
                        how = "was released by the game before it was executed";
                }
                if (!how)
                    continue;
                sl.discarded = true;
                sl.notified = 0;
                sl.discardedAt = now;
                ++discardedLists;
                resetRequested = true;
                Log(std::string("AMD neural: the list this frame's neural work was recorded into ") + how + " (slot " +
                    std::to_string(k) + ", recorded " + std::to_string(now - sl.submission.recordedAt) +
                    " ms ago); its job is completed without it and NR pauses until it retires (discarded lists " +
                    std::to_string(discardedLists) + ")");
            }
            if (sl.submission.submitted)
                continue; // completed already; RetireSlot frees it
            const UINT passCount = (sl.passCount == Slot::kPassUnset) ? 0u : sl.passCount;
            ID3D12CommandQueue* const q = queue.Get();
            while (sl.notified < passCount)
            {
                const UINT i = sl.notified;
                // Submitted's rule: the next pass is published only once the previous worker finished.
                if (i > 0 && static_cast<UINT>(InterlockedCompareExchange(
                                 reinterpret_cast<volatile LONG*>(&At<UINT>(runtime[i - 1], L->jobDone)), 0, 0)) < sl.jobs[i - 1])
                    break;
                auto h = runtime[i];
                const bool matched = At<ID3D12CommandList*>(h, L->pendingList) == pend;
                if (matched)
                    reinterpret_cast<NotifyFn>(reinterpret_cast<uintptr_t>(h) + L->notify)(q, 1, &pend);
                if (!matched || At<ID3D12CommandList*>(h, L->pendingList) != nullptr)
                {
                    failed = true;
                    completionOrderValid = false;
                    Log("AMD Notify did not consume the discarded list's job; stopping NR and retaining resources, pass=" +
                        std::to_string(i + 1));
                    return;
                }
                ++sl.notified;
            }
            if (sl.notified < passCount)
                continue; // the next Record publishes the next pass
            const UINT64 value = ++serial;
            if (FAILED(q->Signal(fence.Get(), value)))
            {
                failed = true;
                Log("D3D12 completion Signal failed for a discarded list's job; resources retained");
                return;
            }
            sl.submissionQueue = q;
            sl.completion.store(value);
            sl.submission.Submit(now);
        }
    }
    // Whether a job completed by RecoverDiscarded has not retired yet: Record records nothing meanwhile.
    // Bounded: after 10 s (the field's wait is about 3.5 s) Record goes on and the slot retires whenever
    // it does, as any stalled slot.
    bool DiscardOutstanding()
    {
        const auto now = GetTickCount64();
        for (const auto& sl : slots)
            if (sl.discarded && sl.pending.load(std::memory_order_acquire))
            {
                if (now - sl.discardedAt < 10000)
                    return true;
                if (!loggedDiscardOverdue)
                {
                    loggedDiscardOverdue = true;
                    Log("AMD neural: a discarded list's job has not retired after 10 s; recording again without waiting for it (noted once)");
                }
            }
        return false;
    }
    // Execute has already happened. Wait only for HIP job-done, not the D3D12
    // fence: that fence covers FSR and the rest of the batch and was stalling
    // ExecuteCommandLists down to ~30 FPS. The original runtime's GPU inline still serializes NR
    // before FSR on the list. Record may still skip if the fence is in flight.
    void WaitAfterSubmitIfEveryFrame(UINT k)
    {
        if (!haveSettings || !lastSettings.everyFrame)
            return;
        // On OptiScaler's Vulkan-on-D3D12 bridge this frame's job cannot finish before the game
        // ends and submits the command buffer it is recording: the bridge's D3D12 list (ours
        // included) is queued behind a Wait on a fence that only that vkQueueSubmit signals, and
        // this runs inside the game's Evaluate, while it is still recording. The loop below
        // therefore always runs out its 80 ms budget and retires nothing, so skipping it changes
        // no slot or retire state. It used to cost 80 ms on the runtime's rebuild frames, and on
        // every frame with AmdSlots=1. Checked before the rebuild test on purpose: here the wait
        // cannot do what it is load-bearing for on D3D12.
        if (lastSettings.vulkanBridge)
        {
            if (!loggedVkSubmitWait)
            {
                loggedVkSubmitWait = true;
                Log("AMD vk: post-submit job wait skipped - on the Vulkan bridge this frame's job cannot finish before "
                    "the game ends and submits the command buffer it is recording, so the wait would always run out "
                    "its 80 ms and retire nothing (said once)");
            }
            return;
        }
        // The wait existed for one reason: with a single slot, the next Record
        // would skip unless this frame's job had already retired. An extra slot
        // is exactly what removes that need, so with two slots the render thread
        // must not block here - blocking is the cost this whole change removes.
        // Not while the original runtime is rebuilding, though: that is when the wait is load-bearing.
        if (wantSlots > 1 && !NativeRebuilding())
        {
            // Throttled trace of the fast path, so a run shows whether it was
            // taken and how far the native counter had progressed.
            if (++skipWaits <= 3 || skipWaits % 300 == 0)
                Log("AMD wait skipped (not rebuilding); count=" + std::to_string(skipWaits) +
                    " nativeDone=" + std::to_string(L && runtime[0] ? At<UINT>(runtime[0], L->jobDone) : 0) +
                    " job=" + std::to_string(slots[k].jobs[0]));
            return;
        }
#ifdef AMD_RETIRE_DIAGNOSTICS
        // Observational only: no wait behaviour is changed here.
        RetirementDiagnostics::Scope timing(diagnostics, directory, L ? L->name : "uninitialized", "EfWaitLoop");
        auto& sample = timing.event;
        sample.everyFrame = true;
        sample.passes = (slots[k].passCount == Slot::kPassUnset) ? 0u : slots[k].passCount;
        sample.width = width;
        sample.height = height;
        sample.jobs = slots[k].jobs;
        sample.target = slots[k].completion.load();
        const auto waitEntry = RetirementDiagnostics::Clock();
#endif
        // Use THIS slot's recorded pass count, not the global activePasses
        // (the next Record may already have rewritten it).
        const UINT slotPasses = (slots[k].passCount == Slot::kPassUnset) ? 0u : slots[k].passCount;
        unsigned iterations = 0;
#ifdef AMD_RETIRE_DIAGNOSTICS
        bool nativeAtEntry = true;   // first poll result: did we wait at all?
#endif
        const auto start = GetTickCount64();
        while (GetTickCount64() - start < 80)
        {
            bool nativeDone = true;
            for (UINT i = 0; i < slotPasses; ++i)
            {
                if (!runtime[i] || slots[k].jobs[i] == 0)
                {
                    nativeDone = false;
                    break;
                }
                const auto done = static_cast<UINT>(InterlockedCompareExchange(
                    reinterpret_cast<volatile LONG*>(&At<UINT>(runtime[i], L->jobDone)), 0, 0));
                if (done < slots[k].jobs[i])
                {
                    nativeDone = false;
                    break;
                }
            }
#ifdef AMD_RETIRE_DIAGNOSTICS
            if (iterations == 0)
                nativeAtEntry = nativeDone;
#endif
            if (nativeDone)
            {
                RetireSubmission(false, "EveryFrameWait");
#ifdef AMD_RETIRE_DIAGNOSTICS
                sample.outcome = "done";
#endif
                break;
            }
            ++iterations;
            Sleep(1);
        }
#ifdef AMD_RETIRE_DIAGNOSTICS
        if (sample.outcome == std::string_view("poll"))
            sample.outcome = "budget";   // fell out of the 80 ms loop without finishing
        sample.waitIterations = iterations;
        sample.waitedBeforeDone = !nativeAtEntry;
        sample.gpuAfter = fence ? fence->GetCompletedValue() : 0;
        sample.waitMs = diagnostics.Milliseconds(RetirementDiagnostics::Clock() - waitEntry);
#endif
    }
    void InitHip()
    {
        if (hipSet)
            return;
        HMODULE hip = LoadLibraryExW(L"amdhip64_7.dll", nullptr, LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if (!hip)
            throw std::runtime_error("Cannot load amdhip64_7.dll; Windows error=" + std::to_string(GetLastError()) +
                                     ". Install the compatible AMD HIP 7 runtime; HIP 6 alone is insufficient.");
        wchar_t hipPath[MAX_PATH] {};
        GetModuleFileNameW(hip, hipPath, MAX_PATH);
        Log("HIP runtime: " + std::filesystem::path(hipPath).string());
        auto count = reinterpret_cast<int (*)(int*)>(GetProcAddress(hip, "hipGetDeviceCount"));
        auto props = reinterpret_cast<int (*)(void*, int)>(GetProcAddress(hip, "hipGetDevicePropertiesR0600"));
        hipSet = reinterpret_cast<HipSetFn>(GetProcAddress(hip, "hipSetDevice"));
        if (!count || !props || !hipSet)
            throw std::runtime_error("HIP R0600 API unavailable");
        int n = 0;
        int countResult = count(&n);
        if (countResult != 0 || n == 0)
            throw std::runtime_error("HIP device enumeration failed: code=" + std::to_string(countResult) +
                                     " devices=" + std::to_string(n));
        auto luid = device->GetAdapterLuid();
        for (int i = 0; i < n; ++i)
        {
            // R0600 prefix: name[256], uuid[16], luid[8]. Oversized aligned storage.
            alignas(16) std::array<unsigned char, 8192> p {};
            int propResult = props(p.data(), i);
            Log("HIP candidate " + std::to_string(i) + " code=" + std::to_string(propResult) +
                " name=" + std::string(reinterpret_cast<char*>(p.data())));
            if (propResult == 0 && std::memcmp(p.data() + 272, &luid, 8) == 0)
            {
                hipDevice = i;
                Log("HIP adapter: " + std::string(reinterpret_cast<char*>(p.data())));
                break;
            }
        }
        if (hipDevice < 0 || hipSet(hipDevice) != 0)
            throw std::runtime_error("No HIP adapter matches D3D12 LUID");
    }
    // One line appended to dlssnr_on_amd.log before pass i loads (0.3.3.2). Shared with the runtime and
    // with a standalone copy that may hold the file; written once, any failure ignored.
    void MarkHostedLoad(UINT i)
    {
        wchar_t exeW[MAX_PATH] {};
        const DWORD n = GetModuleFileNameW(nullptr, exeW, MAX_PATH);
        const std::wstring exeName = std::filesystem::path(std::wstring(exeW, n < MAX_PATH ? n : 0)).filename().wstring();
        std::string exe(exeName.size() * 3 + 1, '\0');
        const int bytes = WideCharToMultiByte(CP_UTF8, 0, exeName.c_str(), static_cast<int>(exeName.size()), exe.data(),
                                              static_cast<int>(exe.size()), nullptr, nullptr);
        exe.resize(bytes > 0 ? static_cast<size_t>(bytes) : 0);
        const std::string line = std::string("--- ") + DlssNr::AmdBridge::ProductName() + " hosts dlssnr_amd_pass" +
                                 std::to_string(i + 1) + ".dll = danielblnc " + (LoadedRuntimeName() ? LoadedRuntimeName() : "?") + ", " +
                                 (exe.empty() ? std::string("?") : exe) + " pid " + std::to_string(GetCurrentProcessId()) +
                                 " tick " + std::to_string(GetTickCount64()) +
                                 "; the next \"loaded into ... as <name>.dll\" line is this hosted copy ---\r\n";
        HANDLE file = CreateFileW((directory / L"dlssnr_on_amd.log").c_str(), FILE_APPEND_DATA,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_ALWAYS,
                                  FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
            return;
        DWORD written = 0;
        WriteFile(file, line.data(), static_cast<DWORD>(line.size()), &written, nullptr);
        CloseHandle(file);
    }
    // dlssnr_on_amd.ini keys the hosted runtime reads in its DllMain and AMDNR does not set, logged once
    // per process (0.3.3.2, GTA V: the ini beside the game belonged to danielblnc's standalone install).
    // Text only, so it works for every layout.
    void LogRuntimeIni()
    {
        static bool logged = false;
        if (logged)
            return;
        logged = true;
        static const char* const kSet[] = { "enabled",   "temporal",      "usefsrinputs",  "usedepth",   "tonemap",
                                            "interop",   "async",         "localtone",     "localstructure",
                                            "skinstructure", "useautomask", "tonechannels", "spindraw" };
        const std::string setList = "Enabled, Temporal, UseFsrInputs, UseDepth, Tonemap, Interop, Async, LocalTone, "
                                    "LocalStructure, SkinStructure, UseAutoMask, ToneChannels, SpinDraw";
        std::ifstream in(directory / L"dlssnr_on_amd.ini", std::ios::binary);
        if (!in)
        {
            Log("AMD runtime ini: no dlssnr_on_amd.ini (runtime defaults; AMDNR sets " + setList + ")");
            return;
        }
        std::string text(8192, '\0');
        in.read(text.data(), static_cast<std::streamsize>(text.size()));
        text.resize(static_cast<size_t>(in.gcount()));
        if (text.rfind("\xEF\xBB\xBF", 0) == 0)
            text.erase(0, 3); // UTF-8 BOM
        const auto trim = [](std::string s) {
            const auto b = s.find_first_not_of(" \t\r\n");
            if (b == std::string::npos)
                return std::string();
            return s.substr(b, s.find_last_not_of(" \t\r\n") - b + 1);
        };
        const auto lower = [](std::string s) {
            for (auto& c : s)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return s;
        };
        const auto number = [](const std::string& v, float& out) {
            char* end = nullptr;
            out = std::strtof(v.c_str(), &end);
            return end != v.c_str();
        };
        std::string keys, warn;
        size_t pos = 0;
        while (pos < text.size())
        {
            const auto eol = text.find('\n', pos);
            const std::string raw = text.substr(pos, eol == std::string::npos ? std::string::npos : eol - pos);
            pos = eol == std::string::npos ? text.size() : eol + 1;
            const std::string lineText = trim(raw);
            const auto eq = lineText.find('=');
            if (lineText.empty() || lineText[0] == ';' || lineText[0] == '#' || lineText[0] == '[' || eq == std::string::npos)
                continue;
            const std::string key = trim(lineText.substr(0, eq)), value = trim(lineText.substr(eq + 1));
            const std::string k = lower(key);
            if (std::find(std::begin(kSet), std::end(kSet), k) != std::end(kSet))
                continue;
            keys += (keys.empty() ? "" : ", ") + key + "=" + value;
            float f = 0.f;
            const bool isNumber = number(value, f);
            if ((k == "residual" && isNumber && f == 0.f) || (k == "scale" && (!isNumber || std::fabs(f - 0.03125f) > 1e-6f)) ||
                (k == "style" && (!isNumber || f != 0.f)) || (k == "tonecurve" && lower(value) == "aces") ||
                (k == "tonelift" && isNumber && f > 0.f) || (k == "usegameexposure" && isNumber && f == 0.f))
                warn += (warn.empty() ? "" : ", ") + key + "=" + value;
        }
        Log("AMD runtime ini (read once at load; AMDNR sets " + setList + "): " +
            (keys.empty() ? std::string("no other keys") : keys));
        if (!warn.empty())
            Log("AMD runtime ini WARNING: " + warn +
                " in dlssnr_on_amd.ini changes the picture and AMDNR does not set it (Residual=0 replaces the frame; "
                "the file may belong to danielblnc's standalone install) - remove these keys, or the file" +
                (L && L->style ? std::string("; Style, ToneCurve, ToneLift and UseGameExposure are overridden while "
                                             "[DlssNr] AmdRuntimeStyle / AmdToneCurve / AmdToneLift / AmdUseGameExposure "
                                             "hold a value")
                               : std::string()));
    }
    // (0.3.4, DANIEL-033-SET) Per model frame and pass, before Record: the runtime knobs this runtime maps. A key with a
    // value writes it (the runtime reads these globals on every job or frame, so it acts on this frame, as tone /
    // structure / skin do); ToneLift acts once, on pass 0 (the other passes get 0, as tone's do). Before the host's
    // first write to a global its own value is kept; a key back at auto writes that value back once, then nothing.
    // With every key at auto from the start no global is ever read or written.
    // (0.3.4, danielblnc support) The quality mode (AmdDanielFastMode) is the fifth, on every pass (each pass module has its own
    // byte), and acts on the job it precedes like the others; it is not a model input, so no history reset.
    void ApplyRuntimeKnobs(UINT pass, HMODULE r, const Settings& cfg)
    {
        if (!L || !r || pass >= runtimeKnobs.size())
            return;
        static constexpr const char* kName[kRuntimeKnobs] = { "Style", "ToneCurve", "ToneLift", "UseGameExposure",
                                                              "Quality" };
        static constexpr const char* kKey[kRuntimeKnobs] = { "AmdRuntimeStyle", "AmdToneCurve", "AmdToneLift",
                                                             "AmdUseGameExposure", "AmdDanielFastMode" };
        const std::uint32_t rva[kRuntimeKnobs] = { L->style, L->toneCurve, L->toneLift, L->useGameExposure, L->quality };
        const bool set[kRuntimeKnobs] = { cfg.runtimeStyle >= 0 && cfg.runtimeStyle <= 2,
                                          cfg.toneCurve >= 0 && cfg.toneCurve <= 1,
                                          cfg.toneLift >= 0.f && cfg.toneLift <= 0.25f,
                                          cfg.useGameExposure >= 0 && cfg.useGameExposure <= 1,
                                          cfg.danielQuality >= 0 && cfg.danielQuality <= 1 };
        const float lift = pass == 0 ? cfg.toneLift : 0.f;
        std::uint32_t liftBits = 0;
        std::memcpy(&liftBits, &lift, sizeof liftBits);
        const std::uint32_t bits[kRuntimeKnobs] = { static_cast<std::uint32_t>(cfg.runtimeStyle),
                                                    static_cast<std::uint32_t>(cfg.toneCurve), liftBits,
                                                    static_cast<std::uint32_t>(cfg.useGameExposure),
                                                    static_cast<std::uint32_t>(cfg.danielQuality) };
        constexpr int kUseGameExposure = 3; // one byte in the runtime; the first three are 4 bytes
        constexpr int kQuality = 4;         // one byte in the runtime (1 Fast, 0 Reference)
        const auto describe = [](int k, std::uint32_t v) {
            if (k == 2)
            {
                float f = 0.f;
                std::memcpy(&f, &v, sizeof f);
                return std::to_string(f);
            }
            std::string text = std::to_string(v);
            if (k == 0)
                text += v == 0 ? " (Default)" : v == 1 ? " (Natural)" : v == 2 ? " (Cinematic)" : "";
            else if (k == 1)
                text += v == 1 ? " (ACES)" : v == 0 ? " (Reinhard)" : "";
            else if (k == kUseGameExposure)
                text += v ? " (the title's exposure)" : " (the runtime's auto-exposure)";
            else
                text += v ? " (Fast)" : " (Reference)";
            return text;
        };
        for (int k = 0; k < kRuntimeKnobs; ++k)
        {
            if (!rva[k])
            {
                // Not mapped for this runtime: never read, never written. A key that holds a value says so once, so a
                // log shows why it did nothing (no version named here: the "AMD runtime <name>" line has the loaded build).
                if (set[k] && pass == 0 && !knobUnmappedLogged[k])
                {
                    knobUnmappedLogged[k] = true;
                    AppendLog(std::string("AMD runtime knob: ") + kName[k] + " from [DlssNr] " + kKey[k] +
                              " is not available with this runtime build (ignored)");
                }
                continue;
            }
            auto& state = runtimeKnobs[pass][k];
            const bool byteWide = k == kUseGameExposure || k == kQuality;
            if (set[k])
            {
                if (!state.captured)
                {
                    state.kept = byteWide ? static_cast<std::uint32_t>(At<std::uint8_t>(r, rva[k]))
                                          : At<std::uint32_t>(r, rva[k]);
                    state.captured = true;
                }
                if (byteWide)
                    At<std::uint8_t>(r, rva[k]) = static_cast<std::uint8_t>(bits[k]);
                else
                    At<std::uint32_t>(r, rva[k]) = bits[k];
                state.hostOwned = true;
                // One line per value the key takes (pass 1 speaks for all), not per frame.
                if (pass == 0 && knobLogged[k] != static_cast<std::int64_t>(bits[k]))
                {
                    knobLogged[k] = bits[k];
                    if (knobSetLines < 32)
                    {
                        std::string line = std::string("AMD runtime knob: ") + kName[k] + " = " + describe(k, bits[k]) +
                                           " from [DlssNr] " + kKey[k] + (k == 2 ? " (first pass only; later passes 0)" : "") +
                                           "; the runtime's own value " + describe(k, state.kept) + " comes back at auto";
                        if (++knobSetLines == 32)
                            line += " (further knob values are not logged)";
                        AppendLog(line);
                    }
                }
            }
            else if (state.hostOwned)
            {
                if (byteWide)
                    At<std::uint8_t>(r, rva[k]) = static_cast<std::uint8_t>(state.kept);
                else
                    At<std::uint32_t>(r, rva[k]) = state.kept;
                state.hostOwned = false;
                if (pass == 0)
                {
                    knobLogged[k] = -1;
                    if (knobRestoreLines < 32)
                    {
                        std::string line = std::string("AMD runtime knob: restored ") + kName[k] + " = " +
                                           describe(k, state.kept) + ", the runtime's own value ([DlssNr] " + kKey[k] +
                                           " back at auto)";
                        if (++knobRestoreLines == 32)
                            line += " (further restores are not logged)";
                        AppendLog(line);
                    }
                }
            }
        }
    }
    // (0.3.4, P7.10) The NR cost timer's heap and readback, created on the first frame that is timed. False (no
    // timestamps recorded) after a failure, for the rest of the session.
    bool EnsureGpuTimer()
    {
        if (gpuTimeHeap && gpuTimeReadback && gpuTimeFrequency)
            return true;
        if (gpuTimeFailed)
            return false;
        try
        {
            UINT64 frequency = 0;
            Check(queue->GetTimestampFrequency(&frequency), "NR cost timestamp frequency");
            if (!frequency)
                throw std::runtime_error("NR cost timestamp frequency is 0");
            D3D12_QUERY_HEAP_DESC qd {};
            qd.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
            qd.Count = 2 * kMaxSlots;
            Check(device->CreateQueryHeap(&qd, IID_PPV_ARGS(&gpuTimeHeap)), "NR cost timestamp heap");
            D3D12_HEAP_PROPERTIES hp {};
            hp.Type = D3D12_HEAP_TYPE_READBACK;
            D3D12_RESOURCE_DESC rd {};
            rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            rd.Width = 16ull * kMaxSlots;
            rd.Height = 1;
            rd.DepthOrArraySize = 1;
            rd.MipLevels = 1;
            rd.SampleDesc.Count = 1;
            rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            Check(device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                  IID_PPV_ARGS(&gpuTimeReadback)),
                  "NR cost readback buffer");
            gpuTimeFrequency = frequency;
            AppendLog("AMD NR cost readout on: a timestamp pair on the game's list around the passes of each answered model "
                      "frame (the queue's time inside NR), mean of " + std::to_string(kGpuTimeWindow) + " frames");
            return true;
        }
        catch (const std::exception& e)
        {
            gpuTimeFailed = true;
            gpuTimeHeap.Reset();
            gpuTimeReadback.Reset();
            gpuTimeFrequency = 0;
            AppendLog(std::string("AMD NR cost readout unavailable for this session (NR is unaffected): ") + e.what());
            return false;
        }
    }
    // (0.3.4, P7.10) At the retire of a slot whose list ran (its fence passed) with an answered model frame's pair.
    void ReadGpuTime(Slot& sl, UINT k)
    {
        if (!sl.gpuTimed)
            return;
        sl.gpuTimed = false;
        if (!gpuTimeReadback || !gpuTimeFrequency)
            return;
        const D3D12_RANGE readRange { 16ull * k, 16ull * k + 16ull };
        void* mapped = nullptr;
        if (FAILED(gpuTimeReadback->Map(0, &readRange, &mapped)) || !mapped)
            return;
        UINT64 stamp[2] {};
        std::memcpy(stamp, static_cast<const unsigned char*>(mapped) + 16ull * k, sizeof stamp);
        const D3D12_RANGE nothingWritten { 0, 0 };
        gpuTimeReadback->Unmap(0, &nothingWritten);
        if (stamp[1] <= stamp[0] || stamp[0] == sl.gpuTimeBegin)
            return; // not a pair this list wrote
        sl.gpuTimeBegin = stamp[0];
        if (sl.gpuTimeWindow != gpuTimeWindowId)
            return; // timed before the window restarted: not part of this one
        const double ms = static_cast<double>(stamp[1] - stamp[0]) * 1000.0 / static_cast<double>(gpuTimeFrequency);
        if (!(ms < 1000.0))
            return;
        gpuTimeSum += ms;
        if (++gpuTimeCount >= kGpuTimeWindow)
        {
            nrGpuMs.store(static_cast<float>(gpuTimeSum / gpuTimeCount), std::memory_order_relaxed);
            gpuTimeSum = 0.0;
            gpuTimeCount = 0;
        }
    }
    void InitPass(UINT i)
    {
        if (runtime[i])
            return;
        InitHip();
        auto path = directory / (L"dlssnr_amd_pass" + std::to_wstring(i + 1) + L".dll");
        // A missing pass DLL is not a foreign build (it read "is unreadable, which this build does not
        // drive" with pass1 sizes when pass 2 was the one missing). HasFiles checks pass 1 only.
        std::error_code existsEc;
        if (!std::filesystem::exists(path, existsEc))
            throw std::runtime_error("dlssnr_amd_pass" + std::to_string(i + 1) +
                                     ".dll is missing - copy all three dlssnr_amd_pass1..3.dll from the same runtime zip "
                                     "of the AMDNR release (" + kNewestShippedRuntimeZip + ")");
        const char* display = nullptr;
        auto identified = IdentifyRuntime(path, &display);
        if (!identified)
        {
            // The supported list and the advice come from kAmdLayouts (0.3.3.2), so they follow the table.
            std::string version;
            const std::string what = DescribeRuntimeFile(path, &version);
            throw std::runtime_error("dlssnr_amd_pass" + std::to_string(i + 1) + ".dll is " + what +
                                     ", which this build does not drive: " + UnsupportedRuntimeAdvice(version));
        }
        if (L && L != identified)
            throw std::runtime_error("Mixed AMD runtime versions across passes");
        L = identified;
        if (L == &kAmd060) {
            const auto hip = GetModuleHandleW(L"amdhip64_7.dll");
            auto alloc = reinterpret_cast<HipMallocFn>(GetProcAddress(hip, "hipMalloc"));
            auto release = reinterpret_cast<HipFreeFn>(GetProcAddress(hip, "hipFree"));
            DWORD fault = 0;
            if (!alloc || !release) throw std::runtime_error("Daniel 0.6.0: HIP allocation API unavailable");
            const int rc = ProbeHipMemory(alloc, release, &fault);
            if (rc) throw std::runtime_error("Daniel 0.6.0: HIP allocation preflight failed, code=" +
                std::to_string(rc) + ", exception=" + std::to_string(fault) + "; NR stopped before loading the runtime");
        }
        // (0.3.4, danielblnc support) A build named by its digest prefix shows the version its file carries (read at run time);
        // the log names both, so a report still says which row drove it.
        const char* shown = display ? display : L->name;
        g_loadedRuntimeName.store(shown, std::memory_order_relaxed);
        g_loadedRuntimeLayout.store(L, std::memory_order_release);
        Log(std::string("AMD runtime ") + shown +
            (std::strcmp(shown, L->name) != 0 ? std::string(" (layout ") + L->name + ")" : std::string()));
        auto weights = directory / L"dlssnr_on_amd_weights.bin";
        if (!std::filesystem::exists(weights))
            throw std::runtime_error("dlssnr_on_amd_weights.bin is required");
        // Host marker in dlssnr_on_amd.log (0.3.3.2): the hosted copy's banner says "loaded into <exe>
        // as version.dll" as his standalone does, so the two looked the same in a tester's log.
        MarkHostedLoad(i);
        // A's standalone DllMain normally creates its own hook thread. Isolate
        // the pinned bootstrap BEFORE it can run; a post-LoadLibrary patch races it.
        HMODULE h = RuntimeHostLoad::Load(path.c_str(), L);
        // Retain module even on failure: CRT registered HIP kernels; no unsafe unloading.
        runtime[i] = h;
        if (L == &kAmd060) ValidateDaniel060(h);
        auto isolated = std::string("AMD runtime bootstrap isolated: host owns submission and configuration");
        if (RuntimeHostLoad::LastPathFailures())
            isolated += " (module path spelling differed from load path)";
        Log(isolated);
        LogRuntimeIni();
        // (0.3.4, danielblnc support) The runtime's own quality mode, as its DllMain set it from its dlssnr_on_amd.ini (default
        // Fast), read once from pass 1 before the host could write there (ApplyRuntimeKnobs runs in Record). Read only;
        // builds without the mode are not touched.
        if (i == 0 && QualityMapped(*L) && g_runtimeOwnQuality.load(std::memory_order_relaxed) < 0)
        {
            const int own = At<std::uint8_t>(h, L->quality) != 0 ? 1 : 0;
            g_runtimeOwnQuality.store(own, std::memory_order_relaxed);
            Log(std::string("AMD runtime quality mode (its own setting: dlssnr_on_amd.ini Quality, default fast): ") +
                (own ? "Fast" : "Reference") + "; [DlssNr] AmdDanielFastMode true / false overrides it per frame, unset "
                "keeps it");
        }
        // The hash above fixes this private module's import layout. Older games
        // ship a 2013 D3DCompiler that rejects the FP16 typed UAV load shader.
        // Bind only this module's compiler import; leave the game's DLL intact.
        static HMODULE systemCompiler = [] {
            wchar_t systemPath[MAX_PATH] {};
            auto length = GetSystemDirectoryW(systemPath, MAX_PATH);
            if (!length || length >= MAX_PATH) return HMODULE(nullptr);
            auto path = std::filesystem::path(systemPath) / L"d3dcompiler_47.dll";
            return LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        }();
        auto compile = systemCompiler ? GetProcAddress(systemCompiler, "D3DCompile") : nullptr;
        if (!compile) throw std::runtime_error("System D3DCompile unavailable for AMD neural shaders");
        if (L->d3dCompileIat)
        {
            auto import = reinterpret_cast<void**>(reinterpret_cast<uintptr_t>(h) + L->d3dCompileIat);
            DWORD previousProtection = 0;
            if (!VirtualProtect(import, sizeof(void*), PAGE_READWRITE, &previousProtection))
                throw std::runtime_error("Could not bind private AMD shader compiler");
            InterlockedExchangePointer(import, reinterpret_cast<void*>(compile));
            DWORD unused = 0;
            if (!VirtualProtect(import, sizeof(void*), previousProtection, &unused))
                throw std::runtime_error("Could not restore private AMD import protection");
            Log("Private AMD shaders use System32 D3DCompiler; game compiler preserved");
        }
        else
            Log("AMD runtime has no D3DCompile import; using engine default");
        // All passes notify after the bridge's single real submission.
        At<NotifyFn>(h, L->trampoline) = AlreadySubmitted;

        At<ID3D12Device*>(h, L->device) = device.Get();
        device->AddRef();
        At<ID3D12CommandQueue*>(h, L->queue) = queue.Get();
        queue->AddRef();
        At<int>(h, L->hipOrdinal) = hipDevice;
        At<uint8_t>(h, L->configuredInline) = 1;
        At<uint8_t>(h, L->interop) = 1;
        At<uint8_t>(h, L->enabled) = 1;
        At<uint8_t>(h, L->fsrInputs) = 1;
        At<uint8_t>(h, L->depthPresent) = 1;
        At<int>(h, L->tonemap) = -1;
        // SpinDraw is set BEFORE Init, and the order is the whole point.
        //
        // It used to be written just after, with the note that "the init value here does
        // not gate the feature" because Record raises it per frame. That was wrong, and it
        // is why turning the graphics wait on did nothing: the runtime builds its wait
        // path - the graphics root signature and the one-pixel-draw PSO - during Init,
        // from the value of SpinDraw AT THAT MOMENT. Init saw the default, built only the
        // compute path, and every later write to SpinDraw asked it to draw with a pipeline
        // that had never been created. The per-frame write is still needed to choose the
        // wait frame by frame; it cannot conjure the objects that choice selects between.
        //
        // So the flag is declared here, before Init, and the runtime builds both paths.
        // Choosing between them stays where it belongs, in Record.
        if (L->spinDraw)
        {
            const bool wantGraphics = Config::Instance()->AmdGraphicsWaitExperimental.value_or_default();
            At<int>(h, L->spinDraw) = wantGraphics ? 1 : 0;
            Log(std::string("AMD runtime: SpinDraw=") + (wantGraphics ? "1" : "0") +
                " BEFORE init (the runtime builds its wait pipeline from this value)");
        }
        std::string file = weights.string();
        if (hipSet(hipDevice) != 0) throw std::runtime_error("AMD HIP device selection failed");
        DWORD initFault = 0;
        const auto initialize = reinterpret_cast<InitFn>(reinterpret_cast<uintptr_t>(h) + L->init);
        const auto engine = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(h) + L->engine);
        const bool initialized = L == &kAmd060 ? InitializeDaniel060(initialize, engine, &file, &initFault)
                                              : initialize(engine, &file);
        if (!initialized) throw std::runtime_error(initFault ?
            "Daniel 0.6.0: initialization fault in the external runtime/HIP, exception=" + std::to_string(initFault) :
            "AMD engine initialization failed");
        // Whatever Init built, the host starts on the compute wait. Record raises it only
        // for a command list GraphicsSnapshotHooks has actually admitted - none in this build
        // (kGraphicsAdmissionPossible in Record), so it stays 0.
        if (L->spinDraw)
            At<int>(h, L->spinDraw) = 0;
        At<uint8_t>(h, L->initDone) = 1;
        Log("Initialized independent AMD pass " + std::to_string(i + 1));
    }
    void InitShader()
    {
        if (root)
            return;
        D3D12_DESCRIPTOR_RANGE ranges[2] {};
        ranges[0] = { D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, 0 };
        ranges[1] = { D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0, 1 };
        D3D12_ROOT_PARAMETER params[3] {};
        params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[0].DescriptorTable = { 2, ranges };
        params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        params[1].Constants = { 0, 0, 24 };
        D3D12_DESCRIPTOR_RANGE residualRange { D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 1, 0, 0 };
        params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[2].DescriptorTable = { 1, &residualRange };
        D3D12_ROOT_SIGNATURE_DESC desc { 3, params, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE };
        ComPtr<ID3DBlob> blob, error;
        Check(D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error),
              "Root signature serialize");
        Check(device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&root)),
              "Root signature create");
        Check(DlssNr::SysCompiler::Compile(CopyShader, sizeof(CopyShader), "AMD active crop", nullptr, nullptr, "main", "cs_5_0",
                         D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &blob, &error),
              "Crop shader compile");
        D3D12_COMPUTE_PIPELINE_STATE_DESC ps {};
        ps.pRootSignature = root.Get();
        ps.CS = { blob->GetBufferPointer(), blob->GetBufferSize() };
        Check(device->CreateComputePipelineState(&ps, IID_PPV_ARGS(&pipeline)), "Crop pipeline");
        Check(DlssNr::SysCompiler::Compile(DepthShader, sizeof(DepthShader), "AMD depth conversion", nullptr, nullptr, "main", "cs_5_0",
                         D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &blob, &error),
              "Depth shader compile");
        ps.CS = { blob->GetBufferPointer(), blob->GetBufferSize() };
        Check(device->CreateComputePipelineState(&ps, IID_PPV_ARGS(&depthPipeline)), "Depth pipeline");
        Check(DlssNr::SysCompiler::Compile(MotionShader, sizeof(MotionShader), "AMD motion resample", nullptr, nullptr, "main", "cs_5_0",
                         D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &blob, &error), "Motion shader compile");
        ps.CS = { blob->GetBufferPointer(), blob->GetBufferSize() };
        Check(device->CreateComputePipelineState(&ps, IID_PPV_ARGS(&motionPipeline)), "Motion pipeline");
        Check(DlssNr::SysCompiler::Compile(ExposureShader, sizeof(ExposureShader), "AMD exposure conversion", nullptr, nullptr, "main", "cs_5_0",
                         D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &blob, &error), "Exposure shader compile");
        ps.CS = { blob->GetBufferPointer(), blob->GetBufferSize() };
        Check(device->CreateComputePipelineState(&ps, IID_PPV_ARGS(&exposurePipeline)), "Exposure pipeline");
        Check(DlssNr::SysCompiler::Compile(ResolveShader, sizeof(ResolveShader), "AMD residual resolve", nullptr, nullptr, "main", "cs_5_0",
                         D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &blob, &error), "Resolve compile");
        ps.CS = { blob->GetBufferPointer(), blob->GetBufferSize() };
        Check(device->CreateComputePipelineState(&ps, IID_PPV_ARGS(&resolvePipeline)), "Resolve pipeline");
        D3D12_DESCRIPTOR_HEAP_DESC hd { D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, kDescriptors,
                                        D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 0 };
        Check(device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&heap)), "Crop heap");
    }
};
Backend::Backend(ID3D12Device* d, ID3D12CommandQueue* q, const std::filesystem::path& dir) : p(new Impl)
{
    p->device = d;
    p->queue = q;
    p->directory = dir;
    // The tail of this line identifies the build. Four earlier rounds were
    // analysed without it and the logs could not be told apart.
#ifdef AMD_SINGLESLOT
    static constexpr const char* kBuildTag = " [r27-contract default=1 control]";
#else
    static constexpr const char* kBuildTag = " [r27-contract default=3 cap=5]";
#endif
    // The build tag names the default, not the count in force: the option can
    // change it while the game runs, and the change logs its own line when it
    // lands. Three earlier rounds were analysed without a tag and the logs could
    // not be told apart.
    p->Log("AMD submission revision 20260916-r27: path-tolerant bootstrap isolation, pending admission, retained late lists, ordered queue migration" +
           std::string(kBuildTag));
    try
    {
        Check(d->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&p->fence)), "Completion fence");
    }
    catch (const std::exception& e)
    {
        p->failed = true;
        p->failReason = e.what();
        p->Log(e.what());
    }
}
Stats Backend::GetStats() const
{
    Stats s {};
    if (!p)
        return s;
    s.tick = GetTickCount64();
    s.recorded = p->frames;
    s.modelFrames = p->modelFrames;
    s.completedModelFrames = p->completedForActivity.load(std::memory_order_acquire);
    s.skips = p->pendingSkips + p->fenceSkips;
    s.width = p->width;
    s.height = p->height;
    s.interleaving = p->haveSettings && p->lastSettings.interleave > 1.f;
    s.modelFrameSeen = p->modelFrameSeen.load(std::memory_order_acquire); // first: it publishes modelHistoryInUse (LF-C)
    s.modelHistoryInUse = p->modelHistoryInUse.load(std::memory_order_relaxed);
    if (p->stabilizer)
    {
        s.changeFraction = p->stabilizer->ChangeFraction();
        s.motionFraction = p->stabilizer->MotionFraction();
        s.modelGhostFraction = p->stabilizer->ModelGhostFraction();
        s.modelGhostSamples = p->stabilizer->ModelGhostSamples();
        s.learnedValid = p->stabilizer->LearnedValid();
        for (int k = 0; k < 3; ++k) s.learnedError[k] = p->stabilizer->LearnedError(k);
        s.carryTone = p->stabilizer->ChangeFraction();
        s.carryRawRefused = p->stabilizer->GateRefusedFraction();
        s.carryDetailKept = p->stabilizer->DetailKept();
    }
    s.accumulating = s.interleaving && p->lastSettings.interleavePreset == 10;
    s.refusedCalls = p->refusedCalls;
    s.boosting = p->interleave && p->interleave->Boost();
    s.boostFrames = p->boostFrames;
    s.compositionWhiteSource = p->compositionWhiteSource.load(std::memory_order_relaxed);
    // (0.3.4, P7.10) -1 = not measured (yet, or n/a). Someone reads the readout: Record times NR for the next 2 s. A
    // readout that lapsed (not polled for 2 s) or saw no answered model frame timed in the last 2 s (NR off, backend
    // stopped, loading screen) reads -1, so the value of an earlier menu session is never shown (G2 review).
    const ULONGLONG prevPoll = p->statsPolledAt.exchange(s.tick, std::memory_order_relaxed);
    const ULONGLONG timedAt = p->gpuTimedAt.load(std::memory_order_relaxed);
    const bool readoutLive = prevPoll != 0 && s.tick < prevPoll + 2000 && timedAt != 0 && s.tick < timedAt + 2000;
    s.nrGpuMs = readoutLive ? p->nrGpuMs.load(std::memory_order_relaxed) : -1.f;
    return s;
}

ID3D12Resource* Backend::Record(ID3D12GraphicsCommandList* cmd, const Frame& incoming, const Settings& cfg)
{
    std::lock_guard guard(p->lock);
    const LockOwnerMark owned(p->lockOwner); // (0.3.3.2) for Shutdown on this thread
    // Unconditional per-call bookkeeping for the before/after capture tool, run
    // before any of the early returns below so it fires on every Record() call
    // regardless of whether this particular frame goes on to record anything.
    ++p->captureFrameCounter;
    if (p->captureWriteAtFrame != 0 && p->captureFrameCounter >= p->captureWriteAtFrame)
    {
        // The write (Map + disk I/O) happens a few frames after the last copy
        // was recorded, once the GPU is certainly past it - this path has no
        // fence of its own to wait on instead, same reasoning as the DX12
        // DlssNr capture this mirrors.
        p->captureWriteAtFrame = 0;
        // One folder per capture, time-stamped: a later capture must never overwrite an
        // earlier one (a session's automatic samples are the before/after of whatever the
        // user changed in between, and that pair is the measurement).
        wchar_t stamp[32] {};
        {
            const time_t now = time(nullptr);
            tm local {};
            localtime_s(&local, &now);
            wcsftime(stamp, 32, L"%Y%m%d-%H%M%S", &local);
        }
        const auto written = p->capture.write(p->directory / (std::wstring(L"amd-nr-capture-") + stamp));
        if (!written.empty())
        {
            p->Log("AMD capture: wrote matched before/after frames to " + written);
            if (p->autoCaptureRuns < 4 && Config::Instance()->DlssNrAutoCapture.value_or_default())
            {
                ++p->autoCaptureRuns;
                p->autoCaptureRearmAtFrame = p->captureFrameCounter + 1800; // ~30s at 60fps; the last run wins
            }
        }
        else if (p->capture.isActive())
            p->Log("AMD capture: sample was a dark/menu frame, discarded and re-armed");
        else
            p->Log("AMD capture: sample was dark too many times in a row, giving up until asked again");
    }
    if ((p->captureFrameCounter % 60) == 0)
    {
        std::error_code ec;
        const auto trigger = p->directory / L"amd-nr-capture.trigger";
        if (std::filesystem::exists(trigger, ec))
        {
            std::filesystem::remove(trigger, ec);
            p->capture.request(capture::kMaxFrames);
            p->Log("AMD capture requested by trigger file");
        }
    }
    if (const UINT64 at = p->captureAtTick.load(); at != 0 && GetTickCount64() >= at)
    {
        p->captureAtTick.store(0);
        p->capture.request(capture::kMaxFrames);
        p->Log("AMD capture: delayed request fired");
    }
    if (p->autoCaptureRearmAtFrame != 0 && p->captureFrameCounter >= p->autoCaptureRearmAtFrame)
    {
        p->autoCaptureRearmAtFrame = 0;
        p->capture.request(capture::kMaxFrames);
        p->Log("AMD capture: follow-up sample requested (run " + std::to_string(p->autoCaptureRuns) + ")");
    }
    if (!p->autoCaptureDone && p->captureFrameCounter >= 180)
    {
        // One capture happens on its own each session, so there is always a
        // fresh sample without anyone having to remember to ask - IF the ini asks for
        // it. This never consulted DlssNrAutoCapture; the NVIDIA path did, and this one
        // ran on every AMD install regardless. See the note on the option in Config.h
        // for what that cost in a dark game.
        p->autoCaptureDone = true;
        if (Config::Instance()->DlssNrAutoCapture.value_or_default())
        {
            p->capture.request(capture::kMaxFrames);
            p->Log("AMD capture: automatic session sample requested");
        }
    }
    // wantSlots is plain state guarded by `lock`, like liveSlots and activeSlot.
    // Widening the choice of buffer here only lets the pick below take a slot
    // whose texture does not exist yet; the rebuild pass further down runs in
    // this same call and creates it before anything is recorded into it.
    p->wantSlots = (std::clamp)(cfg.slots, 1u, Impl::kMaxSlots);
    Frame f=incoming;
#ifdef AMD_RETIRE_DIAGNOSTICS
    p->diagnostics.BeginRecord(p->frames != 0);
    RetirementDiagnostics::Scope timing(p->diagnostics, p->directory, p->L ? p->L->name : "uninitialized", "Record");
    timing.event.outcome = "other_skip";
    p->RetireSubmission(true, "Record", &timing.event);
#else
    p->RetireSubmission(true);
#endif
    // Vulkan bridge gap diagnostic. The runtime's SPIKE lines in dlssnr_on_amd.log ("its capture
    // never landed", 3.6-4.0 s in Indiana Jones) say a job's list ran seconds late, and two causes
    // fit: the game did not reach Evaluate for that long, or the bridge's CPU wait for the previous
    // frame held it (OptiScaler.log then says "has not completed after"). This line puts the host's
    // tick next to the runtime's clock. The gap is counted from the previous Record's RETURN, so the
    // long first Record of a session (about 5 s on the bridge) is not reported as one.
    if (cfg.vulkanBridge)
    {
        const ULONGLONG now = GetTickCount64();
        if (p->lastRecordTick != 0 && now - p->lastRecordTick > 1000)
        {
            ++p->vkRecordGaps;
            if (p->vkRecordGaps <= 10 || p->vkRecordGaps % 10 == 0)
            {
                p->Log("AMD vk: no Record for " + std::to_string(now - p->lastRecordTick) +
                       " ms - either the game did not reach Evaluate or the bridge's previous-frame wait held it "
                       "(see OptiScaler.log 'has not completed after'); gap " + std::to_string(p->vkRecordGaps));
                p->LogSlotSnapshot("gap");
            }
        }
    }
    // Stamps lastRecordTick on every way out of this call, early returns and exceptions included,
    // still under p->lock (declared after the guard, so destroyed before it).
    struct VkRecordExit
    {
        Impl* impl;
        bool on;
        ~VkRecordExit()
        {
            if (on)
                impl->lastRecordTick = GetTickCount64();
        }
    } vkRecordExit { p, cfg.vulkanBridge };
    if (p->failed || !cmd || !f.colour || !f.motion || !f.depth)
    {
        // This return used to be silent, and "model idle" with a clean status line (a Where
        // Winds Meet report with XeSS input, after an in-game settings change) is exactly what
        // it looks like from the menu. Say which input is missing, once per change of cause.
        static std::string lastWhy;
        const std::string why = p->failed ? (p->failReason.empty() ? std::string("backend stopped") : "stopped - " + p->failReason)
                                : !cmd ? "no command list"
                                : !f.colour ? "no colour input"
                                : !f.motion ? "no motion vectors" : "no depth";
        if (why != lastWhy)
        {
            lastWhy = why;
            // Log() is also the line the menu shows, so a stopped backend says why there. The
            // Skyrim report read "the upscaler's NGX parameters lack it" for a pass DLL this
            // build does not drive - the sentence was about the other cases.
            p->Log(p->failed ? "AMD idle: " + why
                             : "AMD idle: " + why + " (the upscaler's NGX parameters lack it; nothing recorded until it returns)");
        }
        return nullptr;
    }
    const auto listType = cmd->GetType();
    if (listType != D3D12_COMMAND_LIST_TYPE_DIRECT && listType != D3D12_COMMAND_LIST_TYPE_COMPUTE)
    {
        static bool said = false;
        if (!said)
        {
            said = true;
            p->Log("AMD idle: the upscaler records on a command list of type " + std::to_string(static_cast<int>(listType)) +
                   ", not DIRECT or COMPUTE; nothing recorded");
        }
        return nullptr;
    }
    // (0.3.3.2, [DlssNr] AmdNeuralListRecovery) A list the game discarded is completed without it, and
    // nothing is recorded until that job retires (Impl::RecoverDiscarded). Off: exactly as before.
    if (DlssNr::AmdBridge::ListRecoveryOn())
    {
        p->RecoverDiscarded();
        if (p->failed)
            return nullptr;
        if (p->DiscardOutstanding())
        {
            if (++p->discardPausedSkips <= 3 || p->discardPausedSkips % 120 == 0)
                p->Log("AMD paused: a discarded list's job is still finishing; count=" + std::to_string(p->discardPausedSkips));
            return nullptr;
        }
    }
    // A owns only one not-yet-notified list/job. Submitted slots remain free
    // to overlap; do not overwrite that singleton while waiting for Execute.
    if (p->HasUnsubmitted())
    {
#ifdef AMD_RETIRE_DIAGNOSTICS
        timing.event.outcome = "unsubmitted_skip";
#endif
        if (++p->unsubmittedSkips <= 3 || p->unsubmittedSkips % 120 == 0)
            p->Log("AMD skipped: previous Record still awaits submission; count=" +
                   std::to_string(p->unsubmittedSkips));
        p->NoteLateWindow(true); // (0.3.4, P3)
        return nullptr;
    }
    if (cfg.spinDraw != 0 && !cfg.graphicsWait && !p->graphicsFallbackReported)
    {
        p->graphicsFallbackReported = true;
        p->Log("AMD AmdSpinDraw setting ignored: graphics state restoration is incomplete; using compute spin");
    }
    // Once per session, whenever the key is on (it no longer glitches anything: see the
    // admission block below, which never runs).
    if (cfg.graphicsWait && !p->graphicsUnavailableReported)
    {
        p->graphicsUnavailableReported = true;
        p->Log("AMD graphics wait unavailable in this build; compute wait used "
               "(AmdGraphicsWaitExperimental is on, but its state admission cannot pass, so no hooks are installed)");
    }
    const auto deviceStatus = p->device->GetDeviceRemovedReason();
    if (FAILED(deviceStatus))
    {
        p->failed = true;
        p->Log("AMD stopped: D3D12 device lost, HRESULT=" + std::to_string(static_cast<UINT>(deviceStatus)));
        p->TraceBoundary("Record device removed");
        return nullptr;
    }
    // Pick the slot for this frame. With one slot this is the original
    // behaviour: that slot must have retired or the frame is skipped. With two,
    // the second slot lets the CPU keep recording while the previous job is
    // still retiring, instead of blocking the render thread in Submitted.
    //
    // Only slots below wantSlots are handed out, so lowering the option takes
    // effect on this frame; the buffers above it are released a frame or two
    // later, once whatever is still using them has retired.
    Impl::Slot* sl = nullptr;
    for (UINT k = 0; k < p->wantSlots; ++k)
        if (!p->slots[k].pending.load(std::memory_order_acquire))
        {
            sl = &p->slots[k];
            p->activeSlot = static_cast<UINT>(k);
            break;
        }
    if (!sl)
    {
#ifdef AMD_RETIRE_DIAGNOSTICS
        timing.event.outcome = "pending_skip";
#endif
        // Do not wait here. Execute/Submitted needs this lock to Notify HIP.
        // Every-frame waits after Execute in Submitted instead.
        if (++p->pendingSkips <= 3 || p->pendingSkips % 120 == 0)
        {
            p->Log("AMD skipped: no free neural slot; count=" + std::to_string(p->pendingSkips));
            p->LogSlotSnapshot("skip");
        }
        return nullptr;
    }
    const auto completion = sl->completion.load();
#ifdef AMD_RETIRE_DIAGNOSTICS
    const auto extraGpuBefore = p->fence->GetCompletedValue();
    if (extraGpuBefore < completion)
#else
    if (p->fence->GetCompletedValue() < completion)
#endif
    {
#ifdef AMD_RETIRE_DIAGNOSTICS
        timing.event.extraWaited = true;
        timing.event.extraGpuBefore = extraGpuBefore;
        timing.event.extraTarget = completion;
        const auto waitStart = RetirementDiagnostics::Clock();
#endif
        // Only wait for already submitted GPU work. Never wait here for an
        // unsubmitted list: its submission may depend on the recording thread.
        // A short scheduling delay used to bypass the effect for a whole frame.
        const auto start = GetTickCount64();
        while (p->fence->GetCompletedValue() < completion && GetTickCount64() - start < 16)
            Sleep(1);
#ifdef AMD_RETIRE_DIAGNOSTICS
        timing.event.extraWaitMs = p->diagnostics.Milliseconds(RetirementDiagnostics::Clock() - waitStart);
        const auto extraGpuAfter = p->fence->GetCompletedValue();
        timing.event.extraGpuAfter = extraGpuAfter;
        if (extraGpuAfter < completion)
#else
        if (p->fence->GetCompletedValue() < completion)
#endif
        {
#ifdef AMD_RETIRE_DIAGNOSTICS
            timing.event.outcome = "fence_skip";
#endif
            if (++p->fenceSkips)
                p->Log("AMD skipped: submitted GPU work still in flight after 16 ms; count=" + std::to_string(p->fenceSkips));
            return nullptr;
        }
        if (++p->fenceRecoveries <= 3 || p->fenceRecoveries % 120 == 0)
            p->Log("AMD continuity: prior GPU work retired after short wait; count=" + std::to_string(p->fenceRecoveries));
    }
    bool timedOut = false;
    for (UINT i = 0; i < p->runtime.size(); ++i)
        if (auto h = p->runtime[i])
        {
            UINT count = static_cast<UINT>(
                InterlockedCompareExchange(reinterpret_cast<volatile LONG*>(&At<UINT>(h, p->L->timeoutCount)), 0, 0));
            if (count > p->observedTimeouts[i])
            {
                p->timeoutEvents += count - p->observedTimeouts[i];
                timedOut = true;
            }
            // The native count resets when staging is recreated.
            p->observedTimeouts[i] = count;
        }
    if (timedOut)
    {
        p->retryAfter = GetTickCount64() + 1000;
        p->resetAfterTimeout = true;
        p->Log("AMD timeout: native fallback may reuse the previous residual; retry in 1s with fresh history. Events=" +
               std::to_string(p->timeoutEvents));
    }
    if (GetTickCount64() < p->retryAfter)
        return nullptr;
    try
    {
        // Reject transient/dummy guides before any GPU commands or native jobs.
        // A later valid frame must be allowed to recover without restarting.
        const auto cd=f.colour->GetDesc();
        const UINT iw=f.width?f.width:UINT(cd.Width), ih=f.height?f.height:cd.Height;
        // Post-RR: the guides stay at render resolution under a display-sized colour, so
        // each is held to the extent the caller declared for it, not to the colour's.
        const UINT mvNeedW = f.writeBack && f.motionWidth ? f.motionWidth : iw;
        const UINT mvNeedH = f.writeBack && f.motionHeight ? f.motionHeight : ih;
        const UINT gNeedW = f.writeBack && f.guideWidth ? f.guideWidth : iw;
        const UINT gNeedH = f.writeBack && f.guideHeight ? f.guideHeight : ih;
        for(auto guide : {f.motion,f.depth}) {
            auto gd=guide->GetDesc();
            const UINT needW = guide == f.motion ? mvNeedW : gNeedW, needH = guide == f.motion ? mvNeedH : gNeedH;
            if(gd.Width<needW || gd.Height<needH || gd.SampleDesc.Count!=1 || gd.DepthOrArraySize!=1 ||
               gd.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D) {
                const std::string reason="AMD neural: waiting for valid full-size guides; received "+Layout(guide);
                if(p->status!=reason)p->Log(reason);
                p->resetRequested=true;
                return nullptr;
            }
        }
        auto desc = f.colour->GetDesc();
        UINT w = f.width ? f.width : static_cast<UINT>(desc.Width), h = f.height ? f.height : desc.Height;
        if (p->frames == 0 || p->lastInputWidth != w || p->lastInputHeight != h)
        {
            p->Log("Input active=" + std::to_string(w) + "x" + std::to_string(h) + " colour=" + Layout(f.colour));
            p->Log("Input motion=" + Layout(f.motion) + " depth=" + Layout(f.depth));
        }
        if (!w || !h || w > desc.Width || h > desc.Height || desc.SampleDesc.Count != 1 || desc.DepthOrArraySize != 1 ||
            desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D)
            throw std::runtime_error("Unsupported active colour extent/layout");
        // Display-resolution vectors are resampled, never cropped as if they
        // belonged to the render-resolution pixel grid.
        for (auto guide : { f.motion, f.depth })
        {
            auto gd = guide->GetDesc();
            const UINT needW = guide == f.motion ? mvNeedW : gNeedW, needH = guide == f.motion ? mvNeedH : gNeedH;
            if (gd.Width < needW || gd.Height < needH || gd.SampleDesc.Count != 1 || gd.DepthOrArraySize != 1 ||
                gd.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D)
                throw std::runtime_error(std::string("Unsupported AMD pre-SR ") +
                                         (guide == f.motion ? "motion: " : "depth: ") + Layout(guide));
        }
        const UINT inputW=w, inputH=h;
        // Ceiling raised to 1.5, and the min() that pinned the result to the input size
        // is gone. Those two lines were the whole of what blocked supersampling; the rest
        // of the path never cared about the direction of the ratio:
        //
        //   colour  - a compute shader that maps input -> w x h, any ratio
        //   depth   - the same, whenever the size differs at all (convertDepth == scaled)
        //   motion  - resampleMotion, already keyed on the size differing
        //   residual resolve - a cubic that reads low -> writes out, ratio-agnostic
        //
        // So the model can be asked to work ABOVE the frame it was handed, and its answer
        // comes back down through the residual path. Cost grows with the square: 1.5 is
        // 2.25x the model time, which is why it is the ceiling and not the default.
        float scale=std::isfinite(cfg.modelScale)?std::clamp(cfg.modelScale,.25f,1.5f):1.f;
        // A DEPTH THE SCALED PATH CANNOT READ DEGRADES THE SCALE, NOT THE FEATURE.
        //
        // These two conditions used to throw once the size had been decided, and a throw
        // out of Record latches `failed` - so a title with an unreadable depth guide and
        // an NR resolution other than 100% got no neural rendering at all, silently, for
        // the rest of the session. The message even said "use 100%". So use 100%: the
        // model then takes the frame as it is and needs no depth conversion, which is
        // what the message was asking the user to do by hand. Logged once per reason.
        {
            const auto dd = f.depth->GetDesc();
            const char* why = nullptr;
            if (dd.Flags & D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE)
                why = "NR resolution forced to 100%: the depth guide is not shader readable";
            else if (DepthReadFormat(dd.Format) == DXGI_FORMAT_UNKNOWN)
                why = "NR resolution forced to 100%: no readable view for this depth format";
            if (why && scale != 1.f)
            {
                scale = 1.f;
                const std::string reason = std::string(why) + " (" + Layout(f.depth) + ")";
                if (p->status != reason) { p->status = reason; p->Log(reason); }
            }
        }
        w=(std::max)(32u,UINT(std::lround(inputW*scale)));
        h=(std::max)(32u,UINT(std::lround(inputH*scale)));
        // MEMORY AT EVERY NEW SIZE (leak audit R4, 0.3.3.2 rebuild). danielblnc's runtime keeps the interop buffers of
        // every working size above about 1 MP until exit, per pass, reused only on an exact size; pixel-exact sizes made
        // every DRS step, DLSS mode or Dynamic NR step a new 75-354 MB. Away from 100% the short side is snapped to
        // cfg.sizeStep and the long side follows the input's aspect (stretched by at most half a step: <= 2.4% above
        // 1 MP). Every later stage uses per-axis factors (motion, jitter, the resolve's rc[], guides, dims[]), so only
        // the model's raster changes. The forced-100% depth fallback above sets scale = 1 and never snaps.
        const UINT exactW = w, exactH = h;
        const bool snapped = SnapNrWorkingSize(inputW, inputH, scale, cfg.sizeStep, w, h);
        const bool scaled=w!=inputW||h!=inputH;
        // The residual resolve is no longer tied to running below input resolution.
        //
        // Below 100% it does what it always did: carry only the model's CHANGE up to the
        // full-resolution frame. At 100% there is nothing to carry up - the picture the
        // model saw and the picture it would be composed onto are the same one - so the
        // sum is an identity and the pass was skipped.
        //
        // But an identity is only an identity at strength 1. At any other strength the same
        // arithmetic becomes the effect-strength dial the AMD path has never had:
        //
        //     out = baseline + strength * (edited - baseline)
        //
        // 0 gives the frame the model was handed, 1 gives exactly what it returned, and
        // ABOVE 1 amplifies the model's own edit. DLSSNR.Intensity is not mapped in the AMD
        // layout so the network cannot be asked to push harder from the inside; scaling its
        // residual from the outside is the closest honest equivalent, and unlike a contrast
        // or saturation control it leaves alone whatever the chain did not change.
        // (0.3.3.2 rebuild) At exactly 100% the resolve is the A-min dial on the whole result: strength against the
        // pre-model copy, no limit, no fade, so strength -> 1 converges on the skip. At any other NR size strength and
        // limit act on the model's composed edit in EditShapeShader, before the Look, stability and sharpening, and the
        // resolve is a pure lift (strength 1, no limit, edge fade kept). lmxxf composes this way at every size.
        //
        // It composes against `baseline` rather than the game's own colour when not scaled,
        // deliberately: both are then in the model's working space, so no encoding or
        // exposure difference can leak into the difference being scaled.
        const bool residualDial = cfg.residualIntensity != 1.f;
        const bool wantResolve = scaled || residualDial;
        // Option B away from 100% (O-2: also above 100%; strictly below would be `scaled && w < inputW`).
        // [DlssNr] AmdEditShaper=true opts in (off by default since the Forza highlight test); off keeps the first
        // 0.3.3.2 build's whole-result dial.
        // (0.3.4, A/B keys read at load, never saved) AmdEditShaperScope=1 keeps the shaper to NR sizes below 100%
        // (the requested scale, which the depth fallback above has already forced to 1 where it applies), so above
        // 100% the whole-result dial with its limit stays. AmdEditShaperLimit picks the shaper's limit
        // (EditShapeRules.h): 0 literal = the Residual limit (0.3.3.2), 1 F1 = no cap, 2 F2 = no cap at 100% rising
        // to the Residual limit at 50%. At their defaults (scope 0, limit 0) these are 0.3.3.2's two expressions.
        bool shapeMode = scaled && cfg.editShaper && (!cfg.editShaperBelowOnly || scale < 1.f) && !cfg.networkOutput &&
                         !p->editShapeFailed;
        const float shapeLimit = EditShapeLimit(cfg.editShaperLimit, scale, cfg.residualLimit);
        const bool shapeEdit = shapeMode && (cfg.residualIntensity != 1.f || shapeLimit > 0.f);
        // AmdEditShaperCarryCap's value for Edit accumulation's bound (used only with that key on): the shaper's limit,
        // never looser than the 4x sanity ceiling it tightens (F2's limit / t grows without bound near 100%).
        const float carryCapLimit = (std::min)(shapeLimit, 4.f);
        // The Residual temporal interleave preset needs the pre-model copy too - it is the
        // `base` the residual is measured against - so the capture is driven by whichever
        // of the two wants it. The resolve pass itself still runs only for wantResolve.
        // Either route needs the pre-model copy: the interleave preset uses it as the base
        // for the frames the model skipped, the standalone switch uses it every frame.
        // Presets 4 (Residual temporal) and 5 (Guided fill) both need the pre-model copy
        // bound as `base`: 4 measures its residual against it, 5 filters it.
        // Preset 10 (Edit accumulation) reads the pre-model copy on BOTH frame types: it is the
        // raw every frame is rebuilt from, and on a model frame the side the model's answer is
        // fitted against.
        const bool residualTemporal = (cfg.interleave > 1.f &&
                                       (cfg.interleavePreset == 4 || cfg.interleavePreset == 5 ||
                                        cfg.interleavePreset == 6 || cfg.interleavePreset == 8 ||
                                        cfg.interleavePreset == 10)) ||
                                      (cfg.interleave <= 1.f && cfg.residualTemporal);
        // EDIT ACCUMULATION (interleave preset 10; TemporalStability.h has the full note): every
        // frame is this frame's raw plus the model's carried local gain/slope, fitted on model
        // frames. Decided once here because four places below act on it: what motion the model
        // is fed, the damping, the modelFrame flag and the sanity bound.
        const bool accumulate = cfg.interleave > 1.f && cfg.interleavePreset == 10;
        // The RenoDX composition wants the pre-model copy as well: wantBaseline, below the
        // composition gate, adds it. The resolve's output buffer stays tied to these two.
        const bool resolveBaseline = wantResolve || residualTemporal;
        const UINT mvW=f.motionWidth?f.motionWidth:inputW, mvH=f.motionHeight?f.motionHeight:inputH;
        const auto depthDesc = f.depth->GetDesc();
        // The private AMD runtime already accepts typeless/depth-stencil guides
        // and stages only the colour-sized active region. Preparing another
        // crop/conversion on the game's command list duplicates that work and
        // invalidates some UE 4.26 command lists (Stellar Blade reports
        // E_INVALIDARG from Close). Pass the original guides through instead.
        // Post-RR hands a render-sized depth under a display-sized colour: a size difference
        // like any other, and the same conversion maps it onto the model's grid.
        const UINT gW = f.guideWidth ? f.guideWidth : inputW, gH = f.guideHeight ? f.guideHeight : inputH;
        const bool guideResample = gW != inputW || gH != inputH;
        const bool convertDepth = scaled || guideResample;
        // Both refusals are handled above by forcing the scale to 1, so `scaled` can only
        // be true here with a readable depth. Kept as a hard check rather than removed.
        if (scaled && (depthDesc.Flags & D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE))
            throw std::runtime_error("NR scale: depth is not shader readable; use 100%");
        if (scaled && DepthReadFormat(depthDesc.Format)==DXGI_FORMAT_UNKNOWN)
            throw std::runtime_error("NR scale: unsupported depth view; use 100%");
        const bool resampleMotion = mvW != w || mvH != h;
        const auto motionDesc = f.motion->GetDesc();
        if (resampleMotion && (f.motionWidth > motionDesc.Width || f.motionHeight > motionDesc.Height))
            throw std::runtime_error("Display motion extent exceeds its allocation");
        if (resampleMotion && motionDesc.Format != DXGI_FORMAT_R16G16_FLOAT &&
            motionDesc.Format != DXGI_FORMAT_R32G32_FLOAT && motionDesc.Format != DXGI_FORMAT_R16G16_SNORM &&
            motionDesc.Format != DXGI_FORMAT_R16G16B16A16_FLOAT && motionDesc.Format != DXGI_FORMAT_R32G32B32A32_FLOAT)
            throw std::runtime_error("Unsupported display motion format: " + Layout(f.motion));
        ID3D12Resource* exposureSource = nullptr;
        if (f.exposure)
        {
            const auto ed = f.exposure->GetDesc();
            if (ed.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D && ed.SampleDesc.Count == 1 &&
                ed.DepthOrArraySize == 1 && !(ed.Flags & D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE) &&
                (ed.Format == DXGI_FORMAT_R32_FLOAT || ed.Format == DXGI_FORMAT_R32G32_FLOAT ||
                 ed.Format == DXGI_FORMAT_R32G32B32A32_FLOAT || ed.Format == DXGI_FORMAT_R16_FLOAT ||
                 ed.Format == DXGI_FORMAT_R16G16B16A16_FLOAT))
                exposureSource = f.exposure;
        }
        if (f.motion->GetDesc().Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL)
            throw std::runtime_error("Unsupported depth-stencil motion buffer: " + Layout(f.motion));
        // ---- RenoDX colour composition gate ([DlssNr] AmdComposition; NrCompose.h) ----------
        // Classic (0, the default) is today's picture byte for byte: composeOn stays false, so
        // nothing below allocates, copies, dispatches, transitions or sets a constant it did not
        // before, and DetailColourMix keeps Detail / Colour strength as it always has. RenoDX (1)
        // composes the runtime's answer in place against the pre-model copy, after the last pass
        // and before the Look (the compose site below Record's runtime calls).
        //
        // REFUSED - Classic runs, one log line, one menu note - where the linear-only tail has no
        // honest input: Network output (the model's answer is to be shown untouched), and a
        // display-referred colour. That is f.displayReferred (the bridge's test, the NVIDIA path's
        // passthrough rule: no IsHDR create flag, a format that cannot hold linear light, or
        // AmdEncoding sRGB / Gamma 2.2) OR the encoding itself, because final image mode fills its
        // own Frame and says so only through s.encoding (PresentExperimental.h). So no W = 1
        // display-referred case exists here. lmxxf refuses the same cases the same way.
        std::string composeRefusal; // short: it is the menu note's reason as well as the log's
        if (cfg.composition == 1)
        {
            if (cfg.networkOutput)
                composeRefusal = "Network output is on (the model's answer is shown untouched)";
            else if (cfg.encoding == 2 || cfg.encoding == 3)
                composeRefusal = std::string("AmdEncoding ") + (cfg.encoding == 2 ? "sRGB" : "Gamma 2.2") +
                                 " - the colour is display-referred, the composition is linear only";
            else if (f.displayReferred)
                composeRefusal = "the colour is display-referred (no IsHDR flag, or a format without linear HDR), "
                                 "the composition is linear only";
            else if (!p->compose && !p->composeFailed)
            {
                // Built once, here, before anything is recorded: a failure costs the mode for
                // the session, not the neural pass (a throw out of Record would latch `failed`).
                try { p->compose = std::make_unique<NrCompose>(p->device.Get()); }
                catch (const std::exception& e)
                {
                    p->composeFailed = true;
                    p->Log(std::string("AMD colour composition: the RenoDX pass could not be built: ") + e.what());
                }
            }
            if (composeRefusal.empty() && (!p->compose || p->composeFailed))
                composeRefusal = "the composition pass failed this session (amd_presr.log has the error)";
        }
        const bool composeOn = cfg.composition == 1 && composeRefusal.empty();
        // Logged and noted once per change of state - the mode, the refusal, where W comes from -
        // and cleared in Classic. The strengths are named in the line but do not start one (a
        // slider drag would log a line a frame). No readback of W: the source is logged, not its
        // value. A frame-to-frame flip (two upscaler contexts, one of them display-referred)
        // stops being logged after a few dozen lines; the note keeps following it.
        {
            std::string state, note;
            if (!composeRefusal.empty())
            {
                state = "refused: " + composeRefusal;
                note = "RenoDX refused: " + composeRefusal + ". Classic runs.";
            }
            else if (composeOn)
            {
                state = exposureSource ? "on: title" : "on: estimate";
                note = exposureSource ? "RenoDX on: white point from the title's exposure texture."
                                      : "RenoDX on: white point estimated (the title publishes no exposure texture).";
            }
            if (state != p->compositionState)
            {
                p->compositionState = state;
                static unsigned changes = 0;
                if (changes < 32)
                {
                    ++changes;
                    std::string line = "AMD colour composition: ";
                    if (!composeRefusal.empty())
                        line += "RenoDX refused - " + composeRefusal + "; Classic runs";
                    else if (composeOn)
                        line += std::string("RenoDX (experimental) - the runtime's answer is composed on the host against "
                                            "the pre-model copy, W from ") +
                                (exposureSource ? "the title's exposure texture (W = 1/e, the texel the runtime is handed)"
                                                : "an estimate of the runtime's own auto-exposure (W = 1/e, measured on the "
                                                  "pre-model copy)") +
                                "; detail " + std::to_string(cfg.composeDetail) + ", colour " +
                                std::to_string(cfg.composeColour) + ", guard " + std::to_string(cfg.maxRatio) +
                                "x, skin/environment edit " + (cfg.skinProtection ? "on" : "off") + " (as of this change)";
                    else
                        line += "Classic";
                    if (changes == 32)
                        line += " (further changes are not logged)";
                    p->Log(line);
                }
                DlssNr::AmdBridge::SetCompositionNote(note);
            }
        }
        if (!composeOn)
            p->compositionWhiteSource.store(kCompositionWhiteNone, std::memory_order_relaxed);
        // DANIELBLNC HIGHLIGHT CHROMA GUARD (0.3.4, [DlssNr] AmdDanielHighlightGuard; NrCompose.h). Off (the default)
        // nothing below builds, compiles, allocates, dispatches or logs anything for it, so the picture and the
        // command list are 0.3.3.2's. On: the runtime's answer takes the original's colour at its own light where the
        // original passes the shoulder at the runtime's exposure, before the edit shaper and the Look - in RenoDX
        // inside the composition (its guard permutation), in Classic as a guard-only pass at the same place. Not with
        // Network output (the model's answer is shown untouched). Its pipelines are compiled on first use; a failure
        // drops the guard for the session and keeps everything else. It needs NrCompose's estimator in Classic too,
        // so the guard builds that object when RenoDX has not.
        bool danielGuard = cfg.danielHighlightGuard && !cfg.networkOutput && !p->danielGuardFailed;
        if (danielGuard)
        {
            try
            {
                if (!p->compose)
                    p->compose = std::make_unique<NrCompose>(p->device.Get());
                p->compose->EnsureGuard();
            }
            catch (const std::exception& e)
            {
                p->danielGuardFailed = true;
                danielGuard = false;
                p->Log(std::string("AMD highlight guard (danielblnc) unavailable this session: ") + e.what());
            }
        }
        // The pre-model copy: the resolve's and the residual presets' baseline, and the
        // composition's original. Only the copy is added for the composition, not the resolve's
        // full-resolution output (that stays with resolveBaseline, as before). The highlight guard's
        // original too (0.3.4, only with its key on).
        const bool wantBaseline = resolveBaseline || composeOn || shapeEdit || danielGuard;
        // One pass on OptiScaler's Vulkan-on-D3D12 bridge. Submitted publishes pass i+1 only after
        // pass i's job is done, polling for it inside the bridge's ExecuteCommandLists; there the
        // list sits behind a Wait that only the game's next vkQueueSubmit signals, which cannot
        // happen while the game is still in Evaluate. Each extra pass would stall 5 s and then stop
        // NR for the session. D3D12 and D3D11 titles are unaffected; lmxxf runs all its passes
        // inside one runtime job, so it needs no clamp.
        const UINT askedPasses = std::clamp(cfg.passes, 1u, 3u);
        p->activePasses = cfg.vulkanBridge ? 1u : askedPasses;
        if (cfg.vulkanBridge && askedPasses > 1 && !p->loggedVkPassClamp)
        {
            p->loggedVkPassClamp = true;
            p->Log("AMD vk: Neural passes " + std::to_string(askedPasses) +
                   " -> 1 on this Vulkan title (danielblnc): pass 2 is published only after pass 1 finishes, inside "
                   "the bridge's submission, which cannot happen before the game submits (5 s per pass, then NR off). "
                   "lmxxf runs all passes here (said once)");
        }
        bool passChange = p->lastPasses != p->activePasses;
        p->lastPasses = p->activePasses;
        for (UINT i = 0; i < p->activePasses; ++i)
            p->InitPass(i);
        const AmdLayout* L = p->L;
        if (!L)
            return nullptr;
        p->InitShader();
        const bool resize = p->width != w || p->height != h;
        const bool countChange = p->wantSlots != p->liveSlots;
        if (resize || countChange)
        {
            // Slot selection above only guarantees that the slot we picked is
            // idle, but this rebuild releases slot colours. Releasing or
            // rewriting a texture a still-in-flight list references is a
            // use-after-free, so defer to a frame where the buffers being
            // touched have retired. Nothing has been recorded into cmd yet at
            // this point, so returning here costs one frame of NR and nothing
            // else.
            //
            // A resize rewrites every buffer; a shrink releases the ones above
            // the new count. A grow creates only new buffers and touches none of
            // the live ones, so it needs no drain at all - which matters because
            // on a game that keeps every slot busy, a frame with nothing
            // outstanding can be a long wait, and the option would look stuck.
            if (resize || p->wantSlots < p->liveSlots)
            {
                const UINT first = resize ? 0u : p->wantSlots;
                for (UINT k = first; k < p->liveSlots; ++k)
                    if (p->slots[k].pending.load(std::memory_order_acquire))
                        return nullptr;
            }
            // Every live slot needs its own FP16 target, not just whichever one
            // is active on the frame the count changes. A slot with a null
            // colour is refused by the runtime outright: the call returns in well under a
            // microsecond, having logged nothing, advanced no counter and set no
            // state - which is exactly the refusal that took three rounds to pin
            // down. Buffers above the count are released instead, so asking for
            // three reserves memory for three: one of these is w*h*8 bytes, where
            // w,h is the RENDER extent (f.width is the DLSS render subrect, not
            // the output). That is 16.6 MB at a 1080p render and 29.5 MB at 1440p,
            // so a 4K output at DLSS Quality reserves ~29 MB per slot, not 66.
            D3D12_HEAP_PROPERTIES hp {};
            hp.Type = D3D12_HEAP_TYPE_DEFAULT;
            D3D12_RESOURCE_DESC rd {};
            rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            rd.Width = w;
            rd.Height = h;
            rd.DepthOrArraySize = 1;
            rd.MipLevels = 1;
            rd.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
            rd.SampleDesc.Count = 1;
            rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
            for (UINT k = 0; k < Impl::kMaxSlots; ++k)
            {
                auto& slot = p->slots[k];
                if (k >= p->wantSlots)
                {
                    if (slot.colour) slot.colour.Reset();
                    if (slot.exposureCopy) slot.exposureCopy.Reset();
                    slot.decode.reset();
                    slot.encode.reset();
                    continue;
                }
                const auto desc = slot.colour ? slot.colour->GetDesc() : D3D12_RESOURCE_DESC {};
                if (!slot.colour || desc.Width != w || desc.Height != h)
                {
                    slot.colour.Reset();
                    Check(p->device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
                                                             D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, nullptr,
                                                             IID_PPV_ARGS(&slot.colour)),
                          "Active FP16 texture");
                }
            }
            if (countChange)
                p->Log("AMD slots: " + std::to_string(p->wantSlots) + " (buffers " +
                       std::to_string(p->wantSlots) + ", cap " + std::to_string(Impl::kMaxSlots) + ")");
            // MEMORY AT EVERY NEW SIZE (leak audit R4). The runtime keeps the interop buffers it
            // creates for each new NR size above about 1 MP in a reuse pool it never trims (75-354
            // MB per pass, read from its disassembly, never measured in a game), and the host
            // cannot free them: HIP does not give a freed import's VRAM back. Away from 100% the
            // size is now snapped (0.3.3.2 rebuild, see the size decision above), so DRS and Dynamic
            // NR revisit a few sizes instead of making new ones. This line is the "before", taken
            // before the runtime has rebuilt anything; the first job completed at the new size logs
            // the "after" (memoryAfterResize). Going back to a size already used should cost
            // nothing, and the two lines are what show whether it does.
            if (resize)
            {
                p->Log("AMD resize: NR " + std::to_string(p->width) + "x" + std::to_string(p->height) + " -> " +
                       std::to_string(w) + "x" + std::to_string(h) + " passes=" + std::to_string(p->activePasses) +
                       (snapped ? " (snapped from " + std::to_string(exactW) + "x" + std::to_string(exactH) + " to " +
                                      std::to_string(cfg.sizeStep) + " px)"
                                : "") +
                       ":" + DlssNr::AmdBridge::MemoryTelemetry(p->device.Get()));
                p->memoryAfterResize = true;
            }
            p->liveSlots = p->wantSlots;
            p->width = w;
            p->height = h;
        }
        const bool convertEncoding = cfg.encoding == 2 || cfg.encoding == 3;
        if (convertEncoding) {
            if(!sl->decode) sl->decode=std::make_unique<ColorEncoding>(p->device.Get());
            if(!sl->encode) sl->encode=std::make_unique<ColorEncoding>(p->device.Get());
            f.colour=sl->decode->Run(cmd,f.colour,f.colourState,inputW,inputH,cfg.encoding,false);
            f.colourState=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        }
        auto prepareGuide = [&](ID3D12Resource* source, ComPtr<ID3D12Resource>& crop)
        {
            auto rd = source->GetDesc();
            if (rd.Width == w && rd.Height == h)
                return source;
            if (!crop || crop->GetDesc().Width != w || crop->GetDesc().Height != h ||
                crop->GetDesc().Format != rd.Format)
            {
                crop.Reset();
                rd.Width = w;
                rd.Height = h;
                rd.MipLevels = 1;
                rd.Flags = D3D12_RESOURCE_FLAG_NONE;
                D3D12_HEAP_PROPERTIES hp {};
                hp.Type = D3D12_HEAP_TYPE_DEFAULT;
                Check(p->device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
                                                         D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, nullptr,
                                                         IID_PPV_ARGS(&crop)),
                      "Guide crop");
            }
            return crop.Get();
        };
        auto motion = f.motion;
        auto depth = f.depth;
        const auto& look = cfg.look;
        // Network output skips every pass of ours that follows the model. Each stage is
        // gated separately rather than by an early return, so the model still records
        // normally and only the presentation changes - which is what makes the comparison
        // worth anything.
        const bool rawNetwork = cfg.networkOutput;
        // Reported three times as doing nothing - no change in picture AND none in frame
        // rate. The frame rate part is the interesting half: skipping two real compute
        // dispatches has to show up somewhere. So log the state transition and exactly
        // which passes were live when it flipped. If this line never appears, the setting
        // is not reaching the backend and the argument is over; if it appears and lists
        // passes that were already off, the answer is that there was nothing to skip.
        if (rawNetwork != p->rawNetworkPrev)
        {
            p->rawNetworkPrev = rawNetwork;
            p->Log(std::string("AMD network output ") + (rawNetwork ? "ON" : "off") +
                   ": look=" + std::to_string(look.enabled ? 1 : 0) +
                   " rtgi=" + std::to_string(cfg.rtgi.enabled ? 1 : 0) +
                   " mix=" + std::to_string((cfg.detail < 1.f || cfg.colour < 1.f) ? 1 : 0) +
                   " stability=" + std::to_string(cfg.stability) +
                   " sharpness=" + std::to_string(cfg.sharpness) +
                   " interleave=" + std::to_string(cfg.interleave));
        }
        const bool applyLook = !rawNetwork && look.enabled && (look.mix > 0 || look.tone > 0 || look.inspect != 0);
        auto createScratch = [&](ComPtr<ID3D12Resource>& resource, UINT sw, UINT sh, DXGI_FORMAT format)
        {
            if (resource && resource->GetDesc().Width == sw && resource->GetDesc().Height == sh) return;
            resource.Reset();
            D3D12_HEAP_PROPERTIES hp {};
            hp.Type = D3D12_HEAP_TYPE_DEFAULT;
            D3D12_RESOURCE_DESC rd {};
            rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            rd.Width = sw; rd.Height = sh; rd.DepthOrArraySize = 1; rd.MipLevels = 1;
            rd.Format = format; rd.SampleDesc.Count = 1;
            rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
            Check(p->device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, nullptr, IID_PPV_ARGS(&resource)), "Guide scratch");
        };
        if (wantBaseline) {
            createScratch(p->scaleBaseline,w,h,DXGI_FORMAT_R16G16B16A16_FLOAT);
            // Not for the composition alone: it never writes this (66 MB at 4K after RR).
            if (resolveBaseline) createScratch(p->scaleOutput,inputW,inputH,DXGI_FORMAT_R16G16B16A16_FLOAT);
        }
        // What the conversion, or the model at 100%, reads: the title's depth or its readable twin.
        ID3D12Resource* depthSource = f.depth;
        if (!scaled)
        {
            // A DEPTH THAT FORBIDS SHADER VIEWS IS COPIED, NOT HANDED ON. At 100% the
            // title's depth used to go straight to the runtime and to the stability pass,
            // and both create a shader resource view of it. On a buffer created with
            // DENY_SHADER_RESOURCE that view is an invalid call and the device is removed -
            // the game's own dialog then says DXGI_ERROR_INVALID_CALL (The Last of Us Part
            // II report). The scaled path already refused such a depth; the 100% path let
            // it through. A whole-resource copy into a typeless twin of the same format
            // family is legal from any depth buffer, and every reader here already knows
            // how to view the typeless form.
            const auto dd = f.depth->GetDesc();
            if ((dd.Flags & D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE) && dd.SampleDesc.Count == 1 &&
                dd.DepthOrArraySize == 1 && dd.MipLevels == 1)
            {
                DXGI_FORMAT twin = DXGI_FORMAT_UNKNOWN;
                switch (dd.Format)
                {
                case DXGI_FORMAT_D32_FLOAT_S8X24_UINT: case DXGI_FORMAT_R32G8X24_TYPELESS:
                case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS: twin = DXGI_FORMAT_R32G8X24_TYPELESS; break;
                case DXGI_FORMAT_D32_FLOAT: case DXGI_FORMAT_R32_TYPELESS: twin = DXGI_FORMAT_R32_TYPELESS; break;
                case DXGI_FORMAT_D24_UNORM_S8_UINT: case DXGI_FORMAT_R24G8_TYPELESS:
                case DXGI_FORMAT_R24_UNORM_X8_TYPELESS: twin = DXGI_FORMAT_R24G8_TYPELESS; break;
                case DXGI_FORMAT_D16_UNORM: case DXGI_FORMAT_R16_TYPELESS: twin = DXGI_FORMAT_R16_TYPELESS; break;
                default: break;
                }
                if (twin != DXGI_FORMAT_UNKNOWN)
                {
                    auto& twinRes = p->depthReadable;
                    if (!twinRes || twinRes->GetDesc().Width != dd.Width || twinRes->GetDesc().Height != dd.Height ||
                        twinRes->GetDesc().Format != twin)
                    {
                        twinRes.Reset();
                        D3D12_HEAP_PROPERTIES hp {};
                        hp.Type = D3D12_HEAP_TYPE_DEFAULT;
                        D3D12_RESOURCE_DESC rd {};
                        rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
                        rd.Width = dd.Width; rd.Height = dd.Height; rd.DepthOrArraySize = 1; rd.MipLevels = 1;
                        rd.Format = twin; rd.SampleDesc.Count = 1;
                        rd.Flags = D3D12_RESOURCE_FLAG_NONE;
                        Check(p->device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
                            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, nullptr, IID_PPV_ARGS(&twinRes)),
                            "Readable depth twin");
                        if (!p->loggedDepthReadable)
                        {
                            p->loggedDepthReadable = true;
                            p->Log("AMD depth guide forbids shader views (format " + std::to_string(dd.Format) +
                                   "): copied each frame into a readable twin (format " + std::to_string(twin) + ")");
                        }
                    }
                    Barrier(cmd, f.depth, f.depthState, D3D12_RESOURCE_STATE_COPY_SOURCE);
                    Barrier(cmd, twinRes.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
                    cmd->CopyResource(twinRes.Get(), f.depth);
                    Barrier(cmd, twinRes.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                    Barrier(cmd, f.depth, D3D12_RESOURCE_STATE_COPY_SOURCE, f.depthState);
                    depthSource = twinRes.Get();
                }
            }
        }
        depth = depthSource;
        if (convertDepth)
        {
            const auto sd = depthSource->GetDesc();
            if ((sd.Flags & D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE) || DepthReadFormat(sd.Format) == DXGI_FORMAT_UNKNOWN)
                throw std::runtime_error("Post-RR: no readable view for the depth guide: " + Layout(f.depth));
            createScratch(p->depthCrop,w,h,DXGI_FORMAT_R32_FLOAT);
            depth=p->depthCrop.Get();
        }
        if (resampleMotion)
        {
            createScratch(p->motionCrop, w, h, DXGI_FORMAT_R16G16_FLOAT);
            motion = p->motionCrop.Get();
        }
        if (exposureSource) createScratch(sl->exposureCopy, 1, 1, DXGI_FORMAT_R32_FLOAT);

        const bool guideChange = p->lastInputWidth != inputW || p->lastInputHeight != inputH ||
                                 p->lastMotionWidth != f.motionWidth || p->lastMotionHeight != f.motionHeight ||
                                 p->lastGuideWidth != gW || p->lastGuideHeight != gH ||
                                 p->hadExposure != (exposureSource != nullptr);
        if (resize || guideChange || p->frames == 0)
            p->Log("Guide mapping: motion=" + std::to_string(f.motionWidth) + "x" + std::to_string(f.motionHeight) +
                   " resampled=" + std::to_string(resampleMotion) + " exposure=" +
                   (exposureSource ? Layout(exposureSource) : "auto") +
                   " preExposure=" + std::to_string(f.preExposure) + " tone=" + std::to_string(cfg.tone));
        const UINT slotBase = p->activeSlot * Impl::kDescriptorsPerSlot;
        const UINT descriptorStride = p->device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        auto cpu = p->heap->GetCPUDescriptorHandleForHeapStart();
        cpu.ptr += static_cast<SIZE_T>(slotBase) * descriptorStride;
        D3D12_SHADER_RESOURCE_VIEW_DESC srv {};
        srv.Format = ReadFormat(f.colour->GetDesc().Format);
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.Texture2D.MipLevels = 1;
        p->device->CreateShaderResourceView(f.colour, &srv, cpu);
        cpu.ptr += p->device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        D3D12_UNORDERED_ACCESS_VIEW_DESC uav {};
        uav.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        p->device->CreateUnorderedAccessView(sl->colour.Get(), nullptr, &uav, cpu);
        auto guideDescriptors = [&](UINT slot, ID3D12Resource* source, ID3D12Resource* target, DXGI_FORMAT format)
        {
            auto handle = p->heap->GetCPUDescriptorHandleForHeapStart();
            auto stride = p->device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            handle.ptr += slot * stride;
            auto guideSrv = srv;
            guideSrv.Format = ReadFormat(source->GetDesc().Format);
            p->device->CreateShaderResourceView(source, &guideSrv, handle);
            handle.ptr += stride;
            auto guideUav = uav; guideUav.Format = format;
            p->device->CreateUnorderedAccessView(target, nullptr, &guideUav, handle);
        };
        // Indices are absolute, so each caller adds the slot's block base.
        if (resampleMotion) guideDescriptors(slotBase + 4, f.motion, motion, DXGI_FORMAT_R16G16_FLOAT);
        if (exposureSource) guideDescriptors(slotBase + 6, exposureSource, sl->exposureCopy.Get(), DXGI_FORMAT_R32_FLOAT);
        if (convertDepth)
        {
            // Distinct descriptor slots: overwriting the colour descriptors here
            // would change the earlier dispatch when the GPU consumes the list.
            cpu.ptr += p->device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            srv.Format = DepthReadFormat(depthSource->GetDesc().Format);
            p->device->CreateShaderResourceView(depthSource, &srv, cpu);
            cpu.ptr += p->device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            uav.Format = DXGI_FORMAT_R32_FLOAT;
            p->device->CreateUnorderedAccessView(depth, nullptr, &uav, cpu);
        }
        Barrier(cmd, f.colour, f.colourState, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        Barrier(cmd, sl->colour.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        cmd->SetComputeRootSignature(p->root.Get());
        cmd->SetPipelineState(p->pipeline.Get());
        auto heap = p->heap.Get();
        cmd->SetDescriptorHeaps(1, &heap);
        { auto t0 = p->heap->GetGPUDescriptorHandleForHeapStart();
          t0.ptr += static_cast<SIZE_T>(slotBase) * descriptorStride;
          cmd->SetComputeRootDescriptorTable(0, t0); }
        UINT dims[] { w, h, inputW, inputH };
        cmd->SetComputeRoot32BitConstants(1, 4, dims, 0);
        cmd->Dispatch((w + 7) / 8, (h + 7) / 8, 1);
        Barrier(cmd, sl->colour.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        Barrier(cmd, f.colour, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, f.colourState);
        Barrier(cmd, f.motion, f.motionState, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        Barrier(cmd, f.depth, f.depthState, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        Barrier(cmd, exposureSource, f.exposureState, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        auto copyGuide = [&](ID3D12Resource* source, ID3D12Resource* dest)
        {
            if (source == dest)
                return;
            Barrier(cmd, source, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_SOURCE);
            Barrier(cmd, dest, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
            D3D12_TEXTURE_COPY_LOCATION from {}, to {};
            from.pResource = source;
            to.pResource = dest;
            D3D12_BOX box { 0, 0, 0, w, h, 1 };
            cmd->CopyTextureRegion(&to, 0, 0, 0, &from, &box);
            Barrier(cmd, dest, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            Barrier(cmd, source, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        };
        if (resampleMotion)
        {
            Barrier(cmd, motion, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            cmd->SetPipelineState(p->motionPipeline.Get());
            auto table = p->heap->GetGPUDescriptorHandleForHeapStart();
            table.ptr += static_cast<SIZE_T>(slotBase + 4) * descriptorStride;
            cmd->SetComputeRootDescriptorTable(0, table);
            UINT motionDims[] { w, h, mvW, mvH };
            cmd->SetComputeRoot32BitConstants(1, 4, motionDims, 0);
            cmd->Dispatch((w + 7) / 8, (h + 7) / 8, 1);
            Barrier(cmd, motion, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        }
        if (convertDepth)
        {
            Barrier(cmd, depth, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            cmd->SetPipelineState(p->depthPipeline.Get());
            UINT depthDims[] { w, h, gW, gH };
            cmd->SetComputeRoot32BitConstants(1,4,depthDims,0);
            auto table = p->heap->GetGPUDescriptorHandleForHeapStart();
            table.ptr += static_cast<SIZE_T>(slotBase + 2) * descriptorStride;
            cmd->SetComputeRootDescriptorTable(0, table);
            cmd->Dispatch((w + 7) / 8, (h + 7) / 8, 1);
            Barrier(cmd, depth, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        }
        else
            copyGuide(depthSource, depth);
        if (exposureSource)
        {
            auto exposure = sl->exposureCopy.Get();
            Barrier(cmd, exposure, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            cmd->SetPipelineState(p->exposurePipeline.Get());
            auto table = p->heap->GetGPUDescriptorHandleForHeapStart();
            table.ptr += static_cast<SIZE_T>(slotBase + 6) * descriptorStride;
            cmd->SetComputeRootDescriptorTable(0, table);
            struct { UINT w, h; float preExposure, exposureScale; } constants {
                1, 1, std::isfinite(f.preExposure) && f.preExposure > 0 ? f.preExposure : 1,
                std::isfinite(f.exposureScale) && f.exposureScale > 0 ? f.exposureScale : 1 };
            cmd->SetComputeRoot32BitConstants(1, 4, &constants, 0);
            cmd->Dispatch(1, 1, 1);
            Barrier(cmd, exposure, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        }
        UINT accepted = 0;
        if (wantBaseline) copyGuide(sl->colour.Get(),p->scaleBaseline.Get());
        // Detail/Colour strength: snapshot the pre-denoise colour so the mix pass
        // can blend the model's answer back toward it. Skipped at full strength.
        // Off in RenoDX mode: Detail and Colour strength are the composition's T and Cs there
        // (their own keys), applied before the Look instead of after it.
        const bool wantMix = !cfg.networkOutput && (cfg.detail < 1.f || cfg.colour < 1.f) && !composeOn;
        if (wantMix)
        {
            if (!p->mix) p->mix = std::make_unique<DetailColourMix>(p->device.Get());
            p->mix->Capture(cmd, sl->colour.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, w, h);
        }
        // NAME the setting that changed, not just the fact that one did.
        //
        // A log came back with ten history resets in one session, every one of them
        // "settings=1", and only two of them next to a scale change that was visible in the
        // log. Eight had no explanation at all. That matters more than it sounds: a history
        // reset throws away the entire temporal history, so the next filled frame has
        // nothing to reproject and the model starts from scratch - a guaranteed artefact,
        // fired repeatedly during normal play for reasons nobody could see.
        //
        // "settings changed" is not a diagnosis. Which one is.
        // (0.3.4, DANIEL-033-SET) The runtime knobs are model inputs like tone/structure/skin, so a change resets the
        // history too - only where this runtime maps the knob (elsewhere the key writes nothing). All four at auto (-1,
        // the default) never differ, so 0.3.3.2's resets are unchanged.
        const bool styleChanged = p->haveSettings && L->style && cfg.runtimeStyle != p->lastSettings.runtimeStyle;
        const bool toneCurveChanged = p->haveSettings && L->toneCurve && cfg.toneCurve != p->lastSettings.toneCurve;
        const bool toneLiftChanged = p->haveSettings && L->toneLift && cfg.toneLift != p->lastSettings.toneLift;
        const bool useGameExposureChanged =
            p->haveSettings && L->useGameExposure && cfg.useGameExposure != p->lastSettings.useGameExposure;
        std::string changedNames;
        if (p->haveSettings)
        {
            auto note = [&](bool differs, const char* name) {
                if (differs) changedNames += (changedNames.empty() ? "" : ",") + std::string(name);
            };
            note(cfg.encoding != p->lastSettings.encoding, "encoding");
            note(cfg.toneChannels != p->lastSettings.toneChannels, "toneChannels");
            note(cfg.modelScale != p->lastSettings.modelScale, "modelScale");
            note(cfg.tone != p->lastSettings.tone, "tone");
            note(cfg.structure != p->lastSettings.structure, "structure");
            note(cfg.skin != p->lastSettings.skin, "skin");
            note(cfg.everyFrame != p->lastSettings.everyFrame, "everyFrame");
            note(cfg.interleave != p->lastSettings.interleave, "interleave");
            note(cfg.proxy != p->lastSettings.proxy, "proxy");
            note(cfg.proxyKnee != p->lastSettings.proxyKnee || cfg.proxyRange != p->lastSettings.proxyRange, "proxyCurve");
            note(cfg.autoMask != p->lastSettings.autoMask, "autoMask");
            note(styleChanged, "runtimeStyle");
            note(toneCurveChanged, "toneCurve");
            note(toneLiftChanged, "toneLift");
            note(useGameExposureChanged, "useGameExposure");
        }
        else
            changedNames = "first-frame";
        const bool settingsChanged = cfg.encoding != p->lastSettings.encoding || cfg.toneChannels != p->lastSettings.toneChannels || cfg.modelScale != p->lastSettings.modelScale || !p->haveSettings || cfg.tone != p->lastSettings.tone ||
                                     cfg.structure != p->lastSettings.structure || cfg.skin != p->lastSettings.skin ||
                                     cfg.everyFrame != p->lastSettings.everyFrame || cfg.interleave != p->lastSettings.interleave ||
                                     cfg.proxy != p->lastSettings.proxy || cfg.proxyKnee != p->lastSettings.proxyKnee ||
                                     cfg.proxyRange != p->lastSettings.proxyRange ||
                                     cfg.autoMask != p->lastSettings.autoMask || // (0.3.4) AM-DAN: the charMask input, as tone/structure/skin
                                     styleChanged || toneCurveChanged || toneLiftChanged || useGameExposureChanged; // (0.3.4) knobs
        const bool explicitReset = p->resetRequested.exchange(false);
        const bool gap = p->lastSubmitted && GetTickCount64() - p->lastSubmitted > 250;
        if (f.reset || resize || guideChange || passChange || p->resetAfterTimeout || settingsChanged || explicitReset || gap)
        {
            p->Log("AMD history reset: frame=" + std::to_string(p->frames) +
                   " game=" + std::to_string(f.reset) + " resize=" + std::to_string(resize) +
                   " guides=" + std::to_string(guideChange) + " passes=" + std::to_string(passChange) +
                   " timeout=" + std::to_string(p->resetAfterTimeout) + " settings=" + std::to_string(settingsChanged) +
                   " explicit=" + std::to_string(explicitReset) + " gap=" + std::to_string(gap) +
                   (settingsChanged ? " changed=[" + changedNames + "]" : ""));
        }
        // Model interleave. Everything about which frames the model runs on, and about
        // the motion it is told about when it does, lives in Interleave.h.
        const bool interleaving = cfg.interleave > 1.f;
        // (P3, 0.3.3.2) The model-frame ghost reading belongs to the interleave, preset and model
        // history it was taken under: a change of any clears it (with the readbacks in flight), so the
        // menu never shows a reading from an earlier setting. Menu only; the picture is untouched.
        {
            const int ghostKey = (interleaving ? 1 : 0) | (p->modelHistoryInUse.load(std::memory_order_relaxed) ? 2 : 0) |
                                 (static_cast<int>(cfg.interleavePreset) << 2);
            if (ghostKey != p->ghostMeterKey)
            {
                p->ghostMeterKey = ghostKey;
                if (p->stabilizer)
                    p->stabilizer->ClearModelGhost();
            }
        }
        bool runModel = true;
        ID3D12Resource* modelMotion = motion;
        if (interleaving)
        {
            if (!p->interleave) p->interleave = std::make_unique<Interleave>(p->device.Get());
            p->interleave->SetCadence(cfg.interleave);
            // A preset switch restarts the cadence too, so the frame the temporal pass restarts on
            // (TemporalStability::Run sees the new preset and drops its history) is a MODEL frame
            // and the new preset starts from an answer, not from its prior. Deliberately not part
            // of settingsChanged: that would also clear the runtime's own history, which on some
            // drivers means a HIP allocation mid-frame, for a change that is ours alone.
            const bool presetSwitch = p->haveSettings && cfg.interleavePreset != p->lastSettings.interleavePreset;
            if (presetSwitch)
                p->Log("AMD interleave preset " + std::to_string(p->lastSettings.interleavePreset) + " -> " +
                       std::to_string(cfg.interleavePreset) + ": temporal pass restarted (runtime history untouched)");
            if (f.reset || resize || guideChange || passChange || p->resetAfterTimeout ||
                settingsChanged || explicitReset || gap || presetSwitch)
                p->interleave->Reset();
            runModel = p->interleave->BeginFrame();
            // ADAPTIVE INTERLEAVE. The cadence above skips every other frame regardless of
            // what is on screen; the temporal pass measures what was on screen (how much of
            // the last filled frame fell back, how much of the picture is moving) and, while
            // either is high, the skip is overruled and the model runs. Interleave then costs
            // nothing it can be seen to cost: its artefacts live on skipped frames, and a frame
            // is only skipped while a skipped frame would look like a model frame.
            if (cfg.interleaveAdaptive && p->stabilizer)
            {
                p->interleave->SetAdaptive(cfg.interleaveAdaptiveSensitivity);
                p->interleave->NoteChange(p->stabilizer->ChangeFraction(), p->stabilizer->MeasuredFilled(),
                                          p->stabilizer->MotionFraction());
                if (!runModel && p->interleave->Boost())
                {
                    runModel = true;
                    ++p->boostFrames;
                }
            }
            // counted before the model records, so a refusal still shows as a model frame
            // attempted rather than silently vanishing from the ratio.
            // THE POINT OF THIS MODULE. The model keeps its own history and reprojects
            // it with the vectors it is handed - vectors that describe ONE frame. Under
            // interleave the model last ran `cadence` frames ago, so one frame of motion
            // reprojects its history by a fraction of the distance the camera actually
            // moved, and every model call therefore starts from a misaligned history.
            // That damage happens inside the model, before this pass sees anything, and
            // no filtering afterwards can undo it. Hand it the accumulated total instead.
            // The state is NON_PIXEL_SHADER_RESOURCE whichever resource `motion` is: the game's
            // vectors were moved there before the guide copies above and stay there until they
            // are handed back after the runtime calls. This passed f.motionState when `motion`
            // was the game's own resource, so on a title whose vectors live in any other state
            // interleave recorded a transition from a state the resource was not in, and back.
            // Edit accumulation with the runtime's history off hands the model this frame's own
            // vectors (packet.motion below: `accumulate && !historyInUse`, and historyInUse is
            // exactly `!(everyFrame && !interleaveModelHistory)` while interleaving), so the chain
            // would be a full-screen pass whose result nothing reads. Skipped; the next frame that
            // needs it starts the chain afresh.
            if (accumulate && cfg.everyFrame && !cfg.interleaveModelHistory)
            {
                p->interleave->SkipMotion();
            }
            else
            {
                modelMotion = p->interleave->Accumulate(
                    cmd, motion, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                    w, h, f.motionScaleX * (resampleMotion ? float(w) / mvW : 1.0f),
                    f.motionScaleY * (resampleMotion ? float(h) / mvH : 1.0f), runModel,
                    f.reset || resize || guideChange || passChange || p->resetAfterTimeout ||
                        settingsChanged || explicitReset || gap);
            }
        }
        else if (p->interleave)
        {
            p->interleave->Reset();
        }
        // Tell the pacer which kind of frame this is, so the present path can measure the
        // two costs apart. Unconditional: with interleave off every frame is a model frame,
        // the two measurements converge, the split test fails and the pacer disengages on
        // its own. See InterleavePacing.h for why the pacing lives at present and not here.
        DlssNr::Pacing::SetStrength(interleaving ? cfg.interleavePacing : 0.f);
        DlssNr::Pacing::Note(runModel);
        // ---- graphics wait admission -------------------------------------------------
        // The runtime's graphics wait spends its idle time on one-pixel draws instead of
        // a compute spin. Draws disturb rasteriser and output-merger state, and until now
        // nothing recorded that state, so the feature shipped disabled behind a note
        // saying restoration was incomplete. GraphicsSnapshotHooks.h records it; this is
        // where the recording is turned into a decision.
        //
        // The rule is conservative on purpose: the wait runs only on a list watched from
        // a Reset onward, with viewport, scissor, topology, render targets and predication
        // all in a KNOWN state. Anything less and `reason` says which one was missing, and
        // the compute wait - which needs no restoration at all - is used instead. A wrong
        // viewport restored from a guess would send the game's next draw somewhere else
        // with nothing in any log to explain it; refusing is always the cheaper error.
        //
        // NOT IN THIS BUILD. CanAdmitGraphics refuses while the graphics root signature is
        // Unknown (GraphicsSnapshot.h), and nothing ever makes it known: SetSignature has no
        // caller outside the unit test and the hooks do not cover SetGraphicsRootSignature or
        // SetPipelineState. So admission could never pass - no ADMITTED line in any tester log -
        // while Install still detoured twelve command-list functions mid-frame and put a global
        // lock on every RS/OM/topology/query call from every thread for the rest of the session,
        // for nothing. Neither happens now: the compute wait every session actually ran is the
        // one that runs, and Record says so once (graphicsUnavailableReported). The SpinDraw
        // write before Init (InitPass) is left exactly as it was: the key alone still decides
        // what the runtime builds at Init. This can only come back with the signature tracked.
        constexpr bool kGraphicsAdmissionPossible = false;
        GraphicsSnap::GraphicsSnapshot gfxSnapshot {};
        bool gfxAdmitted = false;
        if (kGraphicsAdmissionPossible && cfg.graphicsWait && L->spinDraw)
        {
            GraphicsSnap::Install(cmd);
            const char* reason = "not_attempted";
            gfxAdmitted = GraphicsSnap::Admit(cmd, gfxSnapshot, reason);
            if (gfxAdmitted != p->graphicsAdmitPrev || p->recordCalls <= 120)
            {
                p->graphicsAdmitPrev = gfxAdmitted;
                p->Log(std::string("AMD graphics wait ") + (gfxAdmitted ? "ADMITTED" : "refused") +
                       ": " + reason);
            }
        }
        // HYBRID HIGHLIGHT PROXY (experimental, off by default). What the model is shown.
        //
        // The Forza capture measured the model returning flat bright content at 0.5-0.58x
        // the raw brightness: shown linear HDR, it compresses everything above its comfort
        // range. Every tonal fault under interleave descends from that gap between raw and
        // model. So, with the proxy on, the slot colour is squeezed above the knee with a
        // reversible curve (identity below it) right before the model runs - after the
        // pre-model copy was taken, so every comparison downstream is still against the
        // true raw - and the model's answer is decoded right after the last pass by the
        // original's own scale (RenoDX's bridge principle) near the top of the curve, and
        // through the curve where the model's answer still resolves magnitude: exact where
        // the model changes nothing (1 FP16 ulp), and a sparkle the model removed stays
        // removed. The decode runs even when the runtime refused the frame: an encoded frame
        // with no model on it decodes back to the original.
        //
        // p->proxyRaw is shared across slots, as the bound always was, and relies on the same
        // in-order execution on one queue between each list's copy and its decode. The stake is
        // larger now - its scale drives the whole decode, not only the cap - and createScratch
        // re-creates it on a size change without a fence (pre-existing, as for the encode copy).
        const bool proxyOn = cfg.proxy && runModel && !rawNetwork;
        auto runProxy = [&](bool decode)
        {
            if (!p->proxyPipeline)
            {
                ComPtr<ID3DBlob> blob, error;
                auto hr = DlssNr::SysCompiler::Compile(ProxyShader, sizeof(ProxyShader), "AMD highlight proxy", nullptr, nullptr,
                                     "main", "cs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &blob, &error);
                if (FAILED(hr) && error) p->Log(static_cast<const char*>(error->GetBufferPointer()));
                Check(hr, "Highlight proxy shader compile");
                D3D12_COMPUTE_PIPELINE_STATE_DESC ps {};
                ps.pRootSignature = p->root.Get();
                ps.CS = { blob->GetBufferPointer(), blob->GetBufferSize() };
                Check(p->device->CreateComputePipelineState(&ps, IID_PPV_ARGS(&p->proxyPipeline)), "Highlight proxy pipeline");
            }
            guideDescriptors(slotBase + 14, sl->colour.Get(), sl->colour.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT);
            createScratch(p->proxyRaw, w, h, DXGI_FORMAT_R16G16B16A16_FLOAT);
            if (!decode)
            {
                // Keep the raw before it is encoded: the decode recomputes the encode's scale
                // from it and bounds by it.
                Barrier(cmd, sl->colour.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_SOURCE);
                Barrier(cmd, p->proxyRaw.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
                cmd->CopyResource(p->proxyRaw.Get(), sl->colour.Get());
                Barrier(cmd, p->proxyRaw.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                Barrier(cmd, sl->colour.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            }
            {
                // Root table 2 (t1, t2): the raw copy twice; the shader reads t1 only.
                auto handle = p->heap->GetCPUDescriptorHandleForHeapStart();
                handle.ptr += static_cast<SIZE_T>(slotBase + 18) * descriptorStride;
                auto rawSrv = srv;
                rawSrv.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
                p->device->CreateShaderResourceView(p->proxyRaw.Get(), &rawSrv, handle);
                handle.ptr += descriptorStride;
                p->device->CreateShaderResourceView(p->proxyRaw.Get(), &rawSrv, handle);
            }
            Barrier(cmd, sl->colour.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            cmd->SetComputeRootSignature(p->root.Get());
            cmd->SetPipelineState(p->proxyPipeline.Get());
            cmd->SetDescriptorHeaps(1, &heap);
            auto table = p->heap->GetGPUDescriptorHandleForHeapStart();
            table.ptr += static_cast<SIZE_T>(slotBase + 14) * descriptorStride;
            cmd->SetComputeRootDescriptorTable(0, table);
            auto rawTable = p->heap->GetGPUDescriptorHandleForHeapStart();
            rawTable.ptr += static_cast<SIZE_T>(slotBase + 18) * descriptorStride;
            cmd->SetComputeRootDescriptorTable(2, rawTable);
            // bound: safety net against model overshoot only - the decoded value may not exceed
            // this multiple of the local raw maximum (never fires at identity, or under any
            // blend of proxy values inside the 3x3).
            struct { UINT w, h; float knee, range; UINT decode; float bound; } pc {
                w, h, cfg.proxyKnee, cfg.proxyRange, decode ? 1u : 0u, 2.0f };
            cmd->SetComputeRoot32BitConstants(1, 6, &pc, 0);
            cmd->Dispatch((w + 7) / 8, (h + 7) / 8, 1);
            Barrier(cmd, sl->colour.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        };
        if (proxyOn)
        {
            if (!p->proxyLogged)
            {
                p->proxyLogged = true;
                p->Log("AMD highlight proxy ON (hybrid): knee " + std::to_string(cfg.proxyKnee) + ", range " +
                       std::to_string(cfg.proxyRange) + " - the model is shown compressed highlights, its answer is decoded "
                       "by the original's scale (RenoDX-style), through the curve where the answer still resolves it");
            }
            runProxy(false);
        }
        else if (p->proxyLogged && !cfg.proxy)
        {
            p->proxyLogged = false;
            p->Log("AMD highlight proxy OFF");
        }
        // (0.3.4, P7.10) NR cost readout: a timestamp pair around the passes of a model frame on the game's list, only
        // while the readout is being read (menu open; n/a on OptiScaler's Vulkan and D3D11 bridges). Measurement only:
        // two timestamp writes and one 16-byte resolve into our own readback; no resource of the frame is touched, so
        // the picture is 0.3.3.2's, and with the menu closed nothing at all is recorded.
        const UINT slotIndex = static_cast<UINT>(sl - &p->slots[0]);
        sl->gpuTimed = false;
        const ULONGLONG statsPolledAt = p->statsPolledAt.load(std::memory_order_relaxed);
        const ULONGLONG timingNow = GetTickCount64();
        const bool readoutWanted = statsPolledAt != 0 && timingNow < statsPolledAt + 2000;
        // A fresh window when the readout turns on or off, and while it is on but no answered model frame was timed
        // in the last 2 s (NR was off or the backend idle, possibly with the menu open throughout): the old window's
        // partial sum and value are dropped, and its slots that retire later are not counted (gpuTimeWindowId).
        const ULONGLONG lastTimed = p->gpuTimedAt.load(std::memory_order_relaxed);
        const bool timingStale = lastTimed == 0 || timingNow >= lastTimed + 2000;
        if (readoutWanted != p->gpuTimeActive || (readoutWanted && timingStale))
        {
            p->gpuTimeActive = readoutWanted;
            p->gpuTimeSum = 0.0;
            p->gpuTimeCount = 0;
            ++p->gpuTimeWindowId;
            p->nrGpuMs.store(-1.f, std::memory_order_relaxed);
        }
        const bool timeNr = runModel && readoutWanted && !cfg.vulkanBridge && State::Instance().api != API::DX11 &&
                            p->EnsureGpuTimer();
        if (timeNr)
            cmd->EndQuery(p->gpuTimeHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 2 * slotIndex);
        if (runModel)
        for (UINT i = 0; i < p->activePasses; ++i)
        {
            auto r = p->runtime[i];
            // Compute spin. The experimental graphics wait (0.3.1's 1-pixel draws) needs
            // a list admitted above, and none is in this build, so this writes 0 every
            // model frame, exactly as every tester run did - see AmdGraphicsWaitExperimental.
            if (L->spinDraw)
                At<int>(r, L->spinDraw) = gfxAdmitted ? 1 : 0;
            // 0x8d9bd is Temporal in the original 0.2.17. Default on (skip-frame path).
            // Every-frame mode matches author 0.3: skip history inputs, do not
            // clear history-valid (0x8d018) each frame.
            // Interleave used to FORCE the temporal path on, on the assumption that a model
            // run intermittently needs its history. Every-frame mode takes no history inputs
            // at all, so running it every other frame changes nothing for it - while the
            // forced temporal path reprojected a history two frames old, and the ghost it
            // built there sat in the MODEL frames, ground truth to every fill preset and
            // beyond the reach of every fill-side fix that was tried against it. So the
            // model's history under interleave is a switch (AmdInterleaveModelHistory), off
            // by default: interleave then shows exactly the model that interleave-off shows.
            // (0.3.4, P7.2) One Network history switch for both runtimes: AmdInterleaveModelHistory
            // now keeps the network's history with interleave off too, as AmdLmxxfHistory does on
            // lmxxf. With the key false (the default) this is 0.3.3.2's value in every case
            // (everyFrame ? 0 : 1); with it true the only change is interleave off -> 1 (was 0).
            At<uint8_t>(r, L->temporal) =
                (cfg.everyFrame && !cfg.interleaveModelHistory) ? 0 : 1;
            p->modelHistoryInUse.store(At<uint8_t>(r, L->temporal) != 0, std::memory_order_relaxed); // Stats (P3)
            // Engine +0x120 is the history-valid flag, +0x118 is the current
            // borrowed history view. Clear only at a quiescent frame boundary.
            // The model keeps its OWN temporal history and reprojects it with the
            // vectors it is handed. Under interleave that history is a whole cadence
            // old, and anything it contains that the motion field does not describe -
            // a taillight's glow is the standing example - gets dragged inside the
            // network, before this pass ever sees the picture. Measured on a capture:
            // the red trail is present in the model's output and our pass only reduces
            // it, which is why no amount of filtering here ever removed it.
            //
            // Clearing the history each model frame denies the smear anywhere to
            // accumulate. The model then denoises each call from scratch, so it loses
            // the temporal averaging it uses to suppress noise - a real cost, which is
            // why this is a switch and not the default.
            const bool freshModelHistory =
                interleaving && runModel && cfg.interleaveFreshHistory;
            // A GAME-REQUESTED reset (the NGX Reset flag: a camera cut, a menu, a loading
            // screen) used to clear the runtime's history like every other reset. With the
            // runtime in every-frame mode (temporal = 0: the default, and since the Model
            // history switch the interleave default too) it takes no history inputs, so the
            // clear buys nothing - and it is not free: nulling historyView makes the runtime
            // build a new history resource inside HIP, mid-frame, on the stream the game's
            // queue is spinning on. Alone in the Dark (DX11 through the D3D12 bridge) lost
            // the device with DEVICE_HUNG seven times in one log, every one of them on the
            // frame right after a game reset (73 game resets, 7 hangs, no other pattern).
            // So a game reset clears the runtime history only while the runtime's temporal
            // path is actually on; the structural resets below are unchanged.
            const bool historyInUse = At<uint8_t>(r, L->temporal) != 0;
            if ((f.reset && historyInUse) || resize || guideChange || passChange || p->resetAfterTimeout ||
                settingsChanged || explicitReset || gap || freshModelHistory)
            {
                At<uint8_t>(r, L->historyValid) = 0;
                At<void*>(r, L->historyView) = nullptr;
            }
            At<UINT>(r, L->depthInverted) = f.depthInverted;
            At<uint8_t>(r, L->explicitDepth) = 1; // explicit depth convention, no heuristic
            // Pass 0 only, and this is a REVERT.
            //
            // It was briefly `cfg.tone` on every pass, on the theory that a second pass
            // running with tone strength zero was why two passes barely differed from one.
            // That theory cost a regression: with passes=2 and the default raised to 1.0 in
            // the same build, the tonal lift went from being applied once at half strength
            // to twice at full - four times as much - and interleave came back with heavy
            // flicker and ghosting. Which follows: interleave's whole difficulty is the gap
            // between a model frame and a filled one, and every increase in the model's
            // effect widens exactly that gap.
            //
            // So the original `i == 0` was load-bearing, not an oversight. The raised
            // ceiling and the 1.0 default stay - those come from RenoDX's documentation of
            // the same parameter and are defensible on their own - but stacking the lift
            // per pass does not.
            At<float>(r, L->tone) = i == 0 ? cfg.tone : 0;
            At<float>(r, L->structure) = cfg.structure;
            At<float>(r, L->skin) = cfg.skin;
            At<UINT>(r, L->toneChannels)=cfg.toneChannels?1u:0u;
            // The native semantic character-mask channel, on every pass (0.3.4, AM-DAN): [DlssNr] AutoMask (default
            // true = 1, 0.3.3.2's constant) now reaches danielblnc as well; a change resets the history (settingsChanged).
            // The NVIDIA path's Pass2AutoMask / Pass3AutoMask are not read on AMD: danielblnc's passes share one Settings.
            At<UINT>(r, L->charMask) = cfg.autoMask ? 1u : 0u;
            // (0.3.4, DANIEL-033-SET) Style / ToneCurve / ToneLift / UseGameExposure: written only while their key holds a
            // value and this runtime maps them (0.3.3, 0.4.0); at auto nothing is read or written, as in 0.3.3.2.
            p->ApplyRuntimeKnobs(i, r, cfg);
            // The old shader ceiling expired at high render resolutions even
            // when inference finished well inside the native host watchdog.
            // Scale the spin allowance with pixels, but retain a hard ceiling
            // in the private shader if notification is lost. This is an
            // iteration allowance, not a portable millisecond conversion.
            At<UINT>(r, L->watchdog) = static_cast<UINT>(std::clamp<UINT64>(
                262144 + (UINT64(w) * h + 1) / 2, 262144, 2097152));
            Packet packet {};
            packet.list = cmd;
            packet.colour = sl->colour.Get();
            packet.colourState = 4;
            // Edit accumulation with the runtime's history off (the default): the model is handed
            // this frame's own vectors, so it sees exactly what it sees with interleave off. The
            // accumulated total is for a network that reprojects its OWN history across the skip;
            // with that history off the total only told it a wrong story about this frame.
            packet.motion = (accumulate && !historyInUse) ? motion : modelMotion;
            packet.motionState = 4;
            packet.depth = depth;
            packet.depthState = 4;
            packet.exposure = exposureSource ? sl->exposureCopy.Get() : nullptr;
            packet.exposureState = 4;
            packet.scaleX = f.motionScaleX * (resampleMotion ? float(w) / mvW : 1.0f);
            packet.scaleY = f.motionScaleY * (resampleMotion ? float(h) / mvH : 1.0f);
            packet.renderWidth = w;
            packet.renderHeight = h;
            // Snapshot everything that can explain a refusal, and time the call.
            // jobId is diagnostic; the pending list is the publication contract.
            // Notify consumes it, not the worker.
            const void* pendingBefore = At<ID3D12CommandList*>(r, L->pendingList);
            const unsigned recreateBefore = L->recreate ? At<volatile uint8_t>(r, L->recreate) : 0;
            const UINT jobBefore = At<UINT>(r, L->jobId);
            const UINT doneBefore = At<UINT>(r, L->jobDone);
            // 0.3.1 uses a blocking mutex. +0x4c is its ownership/recursion
            // count, not a waiter count or a measure of worker saturation.
            const UINT lockBefore = L->recordLock ? At<UINT>(r, L->recordLock + 0x4c) : 0;
            const int gate4c = L->gate4c ? At<int>(r, L->gate4c) : 0;
            const int gate68 = L->gate68 ? At<int>(r, L->gate68) : 0;
            const UINT count78 = L->counter78 ? At<UINT>(r, L->counter78) : 0;
            // Written BEFORE the call, so a process that dies inside Record
            // leaves this as the last line - which is itself the answer.
            // Every call for the first 120 frames, then a sparse heartbeat. The
            // earlier limit of 5 hid exactly the frames these runs die on, and
            // cost a round to a wrong conclusion drawn from a missing line.
            if (++p->recordCalls <= 120 || p->recordCalls % 300 == 0)
                p->Log("AMD Record enter: n=" + std::to_string(p->recordCalls) +
                       " jobBefore=" + std::to_string(jobBefore) +
                       " doneBefore=" + std::to_string(doneBefore) +
                       " listBefore=" + std::to_string(reinterpret_cast<uintptr_t>(pendingBefore)) +
                       " recreate=" + std::to_string(recreateBefore) +
                       " lockCount=" + std::to_string(lockBefore) +
                       " gate4c=" + std::to_string(gate4c) +
                       " gate68=" + std::to_string(gate68) +
                       " count78=" + std::to_string(count78));
            const auto callStart = std::chrono::steady_clock::now();
            reinterpret_cast<RecordFn>(reinterpret_cast<uintptr_t>(r) + L->record)(&packet);
            const auto callMicros = std::chrono::duration_cast<std::chrono::microseconds>(
                                        std::chrono::steady_clock::now() - callStart)
                                        .count();
            sl->jobs[i] = At<UINT>(r, L->jobId);
            // Staging recreation resets the native job counter. After a resize,
            // job 1 can follow job 1, so counter equality does not mean rejection.
            // The native pending-list pointer is the actual submission contract.
            const bool recorded = At<ID3D12CommandList*>(r, L->pendingList) == cmd;
            if (recorded)
            {
                // Runtime 0.2.17 owns the abort word in its HIP flags buffer.
                // Native staging rebuilds join workers and clear that buffer.
                ++accepted;
            }
            if (At<uint8_t>(r, L->nativeFailure))
            {
                p->failed = true;
                p->Log("AMD pass native failure: " + std::to_string(i + 1) + " job=" + std::to_string(sl->jobs[i]));
                break;
            }
            if (!recorded)
            {
                // No matching pending list means we must not claim publication.
                // Counters alone cannot establish why Record declined.
                p->Log("AMD Record refused: jobBefore=" + std::to_string(jobBefore) +
                       " jobAfter=" + std::to_string(At<UINT>(r, L->jobId)) +
                       " doneBefore=" + std::to_string(doneBefore) +
                       " listBefore=" + std::to_string(reinterpret_cast<uintptr_t>(pendingBefore)) +
                       " listAfter=" + std::to_string(reinterpret_cast<uintptr_t>(At<ID3D12CommandList*>(r, L->pendingList))) +
                       " recreate_before=" + std::to_string(recreateBefore) +
                       " lockCount=" + std::to_string(lockBefore) +
                       " gate4c=" + std::to_string(gate4c) +
                       " gate68=" + std::to_string(gate68) +
                       " count78=" + std::to_string(count78) +
                       " count78_after=" + std::to_string(L->counter78 ? At<UINT>(r, L->counter78) : 0) +
                       " call_us=" + std::to_string(callMicros) +
                       " slot=" + std::to_string(static_cast<UINT>(sl - &p->slots[0])) +
                       " busy=" + std::to_string(p->AnySlotBusy() ? 1 : 0));
                break;
            }
            // Healthy calls are logged sparsely so the log still shows whether
            // the runtime ever blocks, which is what decides if admission control is viable.
            if (p->recordCalls <= 120 || p->recordCalls % 300 == 0)
                p->Log("AMD Record ok: n=" + std::to_string(p->recordCalls) +
                       " jobAfter=" + std::to_string(At<UINT>(r, L->jobId)) +
                       " call_us=" + std::to_string(callMicros) +
                       " slot=" + std::to_string(static_cast<UINT>(sl - &p->slots[0])));
        }
        if (timeNr)
        {
            cmd->EndQuery(p->gpuTimeHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 2 * slotIndex + 1);
            cmd->ResolveQueryData(p->gpuTimeHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 2 * slotIndex, 2,
                                  p->gpuTimeReadback.Get(), 16ull * slotIndex);
            sl->gpuTimed = accepted > 0; // an answered model frame; a refused one measured no network
            if (sl->gpuTimed)
            {
                sl->gpuTimeWindow = p->gpuTimeWindowId;
                p->gpuTimedAt.store(timingNow, std::memory_order_relaxed);
            }
        }
        // Put back what the wait's draws overwrote, while the list is still ours. Only
        // the RS/IA/OM state - ScopedNrStateEnvelope around this call owns the root
        // signatures, root arguments, PSO and descriptor heaps, and two owners for one
        // piece of state is how restores start fighting each other. Never taken in this build.
        if (gfxAdmitted)
            GraphicsSnap::Restore(cmd, p->device.Get(), gfxSnapshot);
        Barrier(cmd, f.motion, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, f.motionState);
        Barrier(cmd, f.depth, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, f.depthState);
        Barrier(cmd, exposureSource, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, f.exposureState);
        p->activePasses = accepted;
        // Bind this slot's notify/retire pass count to what was actually recorded.
        sl->passCount = accepted;
        if (proxyOn) runProxy(true);
#ifdef AMD_RETIRE_DIAGNOSTICS
        timing.event.accepted = accepted;
#endif
        if (accepted)
        {
            ++p->frames;
            p->NoteLateWindow(false); // (0.3.4, P3)
            if (runModel)
            {
                ++p->modelFrames;
                p->modelFrameSeen.store(true, std::memory_order_release); // after the temporal flag's store (LF-C)
            }
        }
        // A model frame the runtime declined: nothing answered, so the temporal pass is told it
        // is a filled frame (the modelFrame flag below) and it is counted for the log and menu.
        else if (runModel && !p->failed)
            ++p->refusedCalls;
        // Even accepted == 0 has B's copy/conversion/barrier commands recorded.
        // Track that list until its D3D fence completes before reusing resources.
        // (0.3.3.2) A new generation: no discard evidence yet, and with list recovery on, a reference to
        // the list until the slot retires (see Slot::listRef).
        sl->resetSeen.store(nullptr, std::memory_order_relaxed);
        sl->resetCertain.store(false, std::memory_order_relaxed);
        sl->discarded = false;
        sl->notified = 0;
        sl->blockedBase = p->unsubmittedSkips;
        if (DlssNr::AmdBridge::ListRecoveryOn())
            sl->listRef = cmd;
        else
            sl->listRef.Reset();
        sl->submission.Record(GetTickCount64());
        sl->pending.store(cmd, std::memory_order_release);
        if (p->failed)
            return nullptr;
        // RENODX COLOUR COMPOSITION (composeOn: the gate above; Classic never enters). Here the
        // slot colour holds the final pass's answer - after the proxy decode, after every
        // compounding pass (the runtime writes in place) - in NON_PIXEL_SHADER_RESOURCE, and
        // scaleBaseline the pre-model copy of the same w x h frame, taken after the crop and
        // before the proxy encode: what the runtime was handed, linear (display-referred input
        // is refused above), FP16, in the answer's units. Composed IN PLACE, so everything after
        // this acts on the composed picture with no descriptor re-pointed: the Look (its SRV is
        // the slot colour), RTGI, stability, sharpen, the resolve (whose finalColour - baseline
        // then lifts C - O onto the frame), the post-RR write-back. Each keeps its own controls:
        // the guard bounds the model's answer against the original before them, not the final
        // picture. Skipped frames (runModel false: the slot holds the raw) and refused ones
        // (accepted 0: nothing answered) are not composed; the fills carry composed model frames.
        // The network's input is unchanged, so a toggle resets no history. The runtime's own
        // composition is the head here (its curve and proxy are closed): only the tail runs on
        // top of it. Two dispatches at most per frame against NrCompose's 16 descriptor regions:
        // the 5 slots that can be in flight never reach a region still being read.
        if (composeOn && runModel && accepted > 0)
        {
            try
            {
                // W, the white of these units. A title that publishes an exposure texture: the
                // texel the runtime is handed (e = tex x scale / pre, ExposureShader), W = 1/e.
                // The runtime runs no auto-exposure of its own then (Resident Evil Requiem,
                // the captured tester logs: "exposure yes" and not one "auto-exposure:" line).
                // Otherwise NrCompose's estimate of that hidden auto-exposure, measured on the
                // pre-model copy with danielblnc's own law (it starts at 4 once; the runtime's
                // exposure carries on across its staging rebuilds, so neither does this). The
                // tail is homogeneous of degree 1, so W moves only the dark floor (W/512) and
                // what the skin mask selects, never overall tone or colour.
                ID3D12Resource* white = exposureSource ? sl->exposureCopy.Get()
                                                       : p->compose->Estimate(cmd, p->scaleBaseline.Get());
                NrCompose::Params k;
                k.detail = cfg.composeDetail;
                k.colour = cfg.composeColour;
                k.guard = cfg.maxRatio;
                k.skin = cfg.skinProtection;
                k.skinDetail = cfg.skinDetail;
                k.skinColour = cfg.skinColour;
                k.envDetail = cfg.envDetail;
                k.envColour = cfg.envColour;
                k.whiteConst = 1.f; // only if the texel is not a usable exposure
                k.highlightGuard = danielGuard; // (0.3.4) the guard permutation; false = the 0.3.3.2 dispatch
                if (p->compose->Apply(cmd, sl->colour.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                                      p->scaleBaseline.Get(), white, w, h, k))
                    p->compositionWhiteSource.store(exposureSource ? kCompositionWhiteTitle : kCompositionWhiteEstimate,
                                                    std::memory_order_relaxed);
                // NrCompose bound its own heap, root signature and pipeline. Every pass below binds
                // its own before it dispatches, and the caller's state envelope restores the game's.
            }
            catch (const std::exception& e)
            {
                // Only the estimator's first allocation can throw, before it records anything.
                // The mode goes for the session (the next frame's gate notes it); this frame
                // shows the runtime's answer uncomposed.
                p->composeFailed = true;
                p->Log(std::string("AMD colour composition: the RenoDX pass stopped for this session: ") + e.what());
            }
        }
        // (0.3.4) Classic with the highlight guard: the guard alone, in place on the answer, with the composition's
        // W (the title's exposure texel, or the estimate of the runtime's own auto-exposure). Same frames as the
        // composition: model frames the runtime answered. At most two NrCompose dispatches a frame, as above.
        else if (danielGuard && runModel && accepted > 0)
        {
            try
            {
                ID3D12Resource* white = exposureSource ? sl->exposureCopy.Get()
                                                       : p->compose->Estimate(cmd, p->scaleBaseline.Get());
                p->compose->Guard(cmd, sl->colour.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                                  p->scaleBaseline.Get(), white, w, h);
            }
            catch (const std::exception& e)
            {
                // As the composition: only the estimator's first allocation can throw, before it records anything.
                p->danielGuardFailed = true;
                danielGuard = false;
                p->Log(std::string("AMD highlight guard (danielblnc) stopped for this session: ") + e.what());
            }
        }
        // The guard's line in amd_presr.log: when it starts acting, when its mode (RenoDX / Classic) or where its E
        // comes from changes, and when it stops after a line said it was on. Answered model frames only; with the key
        // off and never on, no line at all. 32 lines a session.
        if (runModel && accepted > 0)
        {
            const std::string state = danielGuard ? std::string(composeOn ? "RenoDX" : "Classic") + "/" +
                                                        (exposureSource ? "title" : "estimate")
                                                  : std::string();
            if (state != p->danielGuardState && p->danielGuardLogLines < 32)
            {
                std::string line;
                if (danielGuard)
                    line = std::string("AMD highlight guard (danielblnc) on: ") +
                           (composeOn ? "inside the RenoDX composition, on the answer before the tail"
                                      : "Classic, a guard-only pass on the answer before the edit shaper and the Look") +
                           "; E from " +
                           (exposureSource ? "the title's exposure texture (the texel the runtime is handed)"
                                           : "an estimate of the runtime's own auto-exposure (measured on the pre-model copy)") +
                           "; weight smoothstep(0.75, 1.5, max(original) x E)";
                else
                    line = "AMD highlight guard (danielblnc) off";
                if (++p->danielGuardLogLines == 32)
                    line += " (further changes are not logged)";
                p->AppendLog(line);
                p->danielGuardState = state;
            }
        }
        // EDIT SHAPER (option B, see EditShapeShader): in place on the slot colour, so the Look (its SRV is the slot
        // colour), RTGI, mix, stability, sharpen, the resolve and the post-RR write-back all act on the shaped picture
        // with no descriptor re-pointed. Skipped interleave frames (the slot holds the raw) and refused frames get none;
        // the fills carry the shaped model frames. Compiled on first use and never with Check at init: a failure keeps
        // 0.3.3.2 behaviour (whole-result dial) and NR running.
        if (shapeEdit && runModel && accepted > 0)
        {
            if (!p->editShapePipeline && !p->editShapeFailed)
            {
                try
                {
                    ComPtr<ID3DBlob> blob, error;
                    auto hr = DlssNr::SysCompiler::Compile(EditShapeShader, sizeof(EditShapeShader), "AMD edit shaper", nullptr, nullptr,
                                                           "main", "cs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &blob, &error);
                    if (FAILED(hr) && error)
                        p->Log(static_cast<const char*>(error->GetBufferPointer()));
                    Check(hr, "Edit shaper compile");
                    D3D12_COMPUTE_PIPELINE_STATE_DESC ps {};
                    ps.pRootSignature = p->root.Get();
                    ps.CS = { blob->GetBufferPointer(), blob->GetBufferSize() };
                    Check(p->device->CreateComputePipelineState(&ps, IID_PPV_ARGS(&p->editShapePipeline)), "Edit shaper pipeline");
                }
                catch (const std::exception& e)
                {
                    p->editShapeFailed = true;
                    shapeMode = false;
                    p->Log(std::string("AMD edit shaper unavailable this session: Residual strength and limit act on the whole NR "
                                       "result below 100% (0.3.3.2 behaviour): ") + e.what());
                }
            }
            if (p->editShapePipeline)
            {
                guideDescriptors(slotBase + 20, p->scaleBaseline.Get(), sl->colour.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT);
                Barrier(cmd, sl->colour.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                cmd->SetComputeRootSignature(p->root.Get());
                cmd->SetDescriptorHeaps(1, &heap);
                cmd->SetPipelineState(p->editShapePipeline.Get());
                auto table = p->heap->GetGPUDescriptorHandleForHeapStart();
                table.ptr += static_cast<SIZE_T>(slotBase + 20) * descriptorStride;
                cmd->SetComputeRootDescriptorTable(0, table);
                struct { UINT w, h; float intensity, limit; } shape { w, h, cfg.residualIntensity, shapeLimit };
                cmd->SetComputeRoot32BitConstants(1, 4, &shape, 0);
                cmd->Dispatch((w + 7) / 8, (h + 7) / 8, 1);
                Barrier(cmd, sl->colour.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            }
        }
        // The shaper's line in amd_presr.log (0.3.4: was once a session). Written when shape mode starts and again
        // when its limit mode, scope or carry cap changes, or under F2 the NR size (F2's effective limit follows the
        // size; literal and F1 do not, so a Dynamic NR / DRS step there writes nothing and keeps the budget for the
        // A/B). With AmdEditShaper=true but the shaper not acting (100%, scope 1 above 100%, Network output) an
        // "off" line with its reason is written on the first model frame, and one when it stops after an "on" line
        // - so every shot of an A/B has its state in the log. AmdEditShaper=false (default) writes nothing unless a
        // line said it was on. Checked on answered model frames only (the frames the shaper acts on); slider values
        // are not part of the key (a drag would write a line a frame; the bridge's settings diff logs them).
        // 32 lines a session.
        if (runModel && accepted > 0)
        {
            const bool scopeOut = cfg.editShaperBelowOnly && !(scale < 1.f);
            std::string key;
            if (shapeMode)
                key = "on/" + std::to_string(cfg.editShaperLimit) + "/" + (cfg.editShaperBelowOnly ? "1" : "0") + "/" +
                      (cfg.editShaperCarryCap ? "1" : "0") +
                      (cfg.editShaperLimit == kEditShapeF2 ? "/" + std::to_string(w) + "x" + std::to_string(h)
                                                           : std::string());
            else if (cfg.editShaper)
                key = std::string("off/") + (!scaled ? "100" : cfg.networkOutput ? "network" : scopeOut ? "scope" : "dial");
            if (key != p->editShapeLogKey && p->editShapeLogLines < 32)
            {
                const std::string nrSize = std::to_string(w) + "x" + std::to_string(h) + " of " + std::to_string(inputW) +
                                           "x" + std::to_string(inputH);
                std::string line;
                if (shapeMode)
                {
                    const char* modeName = cfg.editShaperLimit == kEditShapeF1   ? "F1 (no cap)"
                                           : cfg.editShaperLimit == kEditShapeF2 ? "F2 (ramped: none at 100%, the limit at 50%)"
                                                                                 : "literal";
                    line = "AMD edit shaper on (NR " + nrSize + "; mode " + modeName + ", scope " +
                           (cfg.editShaperBelowOnly ? "below 100% only" : "both sides of 100%") + ", effective limit " +
                           (shapeLimit > 0.f ? std::to_string(shapeLimit) : std::string("none")) +
                           "): Residual strength " + std::to_string(cfg.residualIntensity) + " and limit " +
                           std::to_string(cfg.residualLimit) +
                           " act on the model's edit before the Look/stability/sharpening; the resolve lifts at 1, "
                           "no limit, edge fade " + std::to_string(cfg.residualFade) +
                           (accumulate ? "; Edit accumulation's cap " +
                                             (cfg.editShaperCarryCap && shapeLimit > 0.f ? std::to_string(carryCapLimit)
                                                                                         : std::string("4x")) +
                                             (cfg.editShaperCarryCap ? " (carry cap on)" : "")
                                       : std::string());
                }
                else if (!scaled)
                    line = "AMD edit shaper off (NR " + nrSize + "): at 100% Residual strength acts on the whole result "
                           "with no limit";
                else if (cfg.networkOutput)
                    line = "AMD edit shaper off (NR " + nrSize + "): Network output is on (the model's answer is shown "
                           "untouched)";
                else
                    line = "AMD edit shaper off (NR " + nrSize +
                           (cfg.editShaper && scopeOut ? "; scope below 100% only" : "") +
                           "): Residual strength and limit act on the whole NR result (whole-result dial)";
                if (++p->editShapeLogLines == 32)
                    line += " (further changes are not logged)";
                p->AppendLog(line);
                p->editShapeLogKey = key;
            }
        }

        auto finalColour = sl->colour.Get();


        // Detail/Colour strength: blend the denoised colour back toward the
        // captured pre-denoise baseline. Inert (returns finalColour) at full strength.
        if (wantMix && p->mix)
            finalColour = p->mix->Apply(cmd, finalColour, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                                        w, h, cfg.detail, cfg.colour);
        // Snapshot for the before/after capture tool: everything from here down
        // (temporal stability, sharpen) is OUR pass, isolated from whatever the
        // model itself did. Only read if a capture is actually running.
        ID3D12Resource* capturedBefore = finalColour;
        // Temporal stabilisation of the final colour (default off = byte-identical).
        // Runs at model-work size with the same motion resource/scale the model
        // received; a camera cut or any history-reset condition passes through.
        // Interleave keeps the stability pass even here, and has to: on a skipped frame
        // there IS no network output, and what would be shown instead is the raw
        // un-denoised frame. That is not the model's work, so showing it would answer the
        // wrong question. Turn interleave off for a clean comparison.
        if ((!rawNetwork || interleaving) && (cfg.stability > 0.f || interleaving || cfg.residualTemporal) &&
            p->EnsureStabilizer())
        {
            const bool historyReset = f.reset || resize || guideChange || passChange ||
                                      p->resetAfterTimeout || explicitReset || gap;
            const auto motionState = (motion == f.motion)
                                         ? f.motionState
                                         : D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
            // Same restored-state convention as motion above: depth was handed to
            // the model/RTGI earlier and restored to its original state by the
            // callee - f.depthState if it is still the caller's own resource,
            // or NON_PIXEL_SHADER_RESOURCE if it is our own converted scratch copy.
            const auto depthStateForStability = (depth == f.depth)
                                         ? f.depthState
                                         : D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
            // Auto: a frame that finds every slot busy carries no NR and is shown
            // raw. On the first frame after such a skip, lean harder on history so
            // the on/off flashing reads as steadier. Base strength otherwise.
            float alpha = std::clamp(cfg.stability, 0.f, 1.f);
            const UINT64 skips = p->pendingSkips + p->fenceSkips;
            const bool skippedRecently = skips != p->stabPrevSkips;
            if (skippedRecently)
                p->stabPrevSkips = skips;
            float ghostReject = 1.f;
            if (interleaving)
            {
                // Model frame: a fresh clean denoise is available, so keep MOST of
                // it (low history) - this refreshes the history and clears ghosts.
                // Filled frame: the current buffer is UN-denoised (noisy). Balance
                // the two failure modes - full history smears ghosts (bright lights),
                // full rejection pulls noise in. A partial reject + slightly lower
                // history weight keeps both tolerable while the model frame refreshes.
                // Model frame alpha raised again, from 0.30 to 0.60: 0.30 still left
                // static/slow-changing pixels (street/building lights) at 70% fresh
                // model weight per call, and the model re-decides emissive/specular
                // brightness independently each time it runs, so that 70% carried the
                // model's own per-call jitter through almost unfiltered - visible as
                // light flicker at the interleave cadence, worse at 1.5 (more model
                // calls/sec) and worst in low-key scenes (dawn/night) where emissive
                // content dominates the visible contrast. ghostReject stays 1 on model
                // frames (unchanged below), so a pixel whose reprojected history
                // actually falls outside the current clamp box (real motion/
                // disocclusion) still has its history weight zeroed regardless of
                // alpha - raising this ceiling only adds damping to pixels already
                // agreeing with history (static/slow lights), not moving ones.
                // Model-frame alpha raised 0.60 -> 0.85 now that the shader gates ghost
                // rejection on motion (see TemporalStability.h's `ghostScale`). Until that
                // gate existed this number could not do anything on the pixels that
                // actually flicker: `a2 = alpha * (1 - ghost)` and `ghost` was saturated
                // there, so 0.15, 0.30 and 0.60 all multiplied out to the same 0 - three
                // earlier rounds spent raising a value that was being cancelled. With the
                // gate, a STATIC pixel now genuinely gets this much history weight, which
                // is what damps the model's per-call brightness jitter, and a MOVING pixel
                // still has its history rejected in full, so ghosting is unaffected. The
                // only thing a high value costs is response time on a static pixel whose
                // lighting genuinely changes - which is rare and gradual.
                // Both frame types carry the same weight: a difference between them is
                // itself something the eye sees at the cadence. 0.85 was set when the
                // motion-gated ghost rejection was assumed to fully protect moving pixels;
                // it does not, and that much history weight showed up as the ghosting
                // reported alongside it. 0.70 still damps the model's per-call jitter hard
                // (the thing 0.15/0.30/0.60 never could, because they were multiplied by a
                // saturated ghost term) without holding a trail behind moving objects.
                alpha = 0.70f;
                if (!runModel) ghostReject = 0.5f;
                // Edit accumulation damps the model's answers in transform space by Temporal
                // stability itself - the rule lmxxf's carry has always used - so the one slider
                // means the same thing on both runtimes. The 0.70 above was never read by the
                // Held-picture presets anyway. The floor keeps the pass out of its own bypass.
                if (accumulate)
                {
                    alpha = (std::max)(std::clamp(cfg.stability, 0.f, 1.f), 0.001f);
                    ghostReject = 1.f;
                }
            }
            else if (skippedRecently)
            {
                alpha = (std::max)(alpha, 0.85f);
            }
            // Interleave: on a model frame use the user's chosen mode (it is a fresh
            // denoise, so difference-gated works there and refreshes the history); on
            // a fill frame use the smart fill (mode 3) that keeps clean history where
            // it agrees with the blurred current and breaks the ghost where it does
            // not. Non-interleave uses the user's chosen mode directly.
            // Fill frame source is the user's call: mode 2 keeps the reprojected history
            // untouched (a fill frame is then identical in kind to the model frame it
            // follows, so it cannot introduce a cadence difference - it can only ghost),
            // mode 3 mixes in the blurred un-denoised current frame to break that ghost
            // and pays for it with exactly such a difference.
            // Interleave now has ONE path in the shader, taken by both frame types, so
            // nothing here switches on whether the model ran this frame - that switching
            // was itself the cadence. `interleaved` below is what selects it.
            const UINT stabMode = cfg.stabilityMode;
            // How much of the current frame's fine detail the interleave path keeps.
            // It only scales the high band, so it trades sharpness against grain and
            // cannot bring the cadence back. 0..3 -> 0.25 / 0.50 / 0.75 / 1.00.
            const float detailScale = 0.25f * float(std::clamp<UINT>(cfg.interleaveFill, 0u, 3u) + 1u);
            (void) detailScale;
            // JITTER-COMPENSATED REPROJECTION. The title renders every frame on a grid shifted
            // by its TAA jitter; the motion vectors do not carry that shift. Standing still in
            // Silent Hill 2 the raw moves by 0.2-0.8 px between consecutive frames, and the
            // filled frames measured 0.2-0.6 px off the raw they were shown with: the held
            // picture sat where the PREVIOUS frame's grid was. That is a sub-pixel wobble at
            // half the frame rate, on every edge and texture, worse in motion - the shimmer
            // no per-pixel test could have removed because it was in the geometry of the
            // reprojection itself. Every TAA and every upscaler does this; this pass did not.
            float jitterDx = 0.f, jitterDy = 0.f;
            // Not after Ray Reconstruction (writeBack): RR's output is already resolved, it carries no
            // jitter, so shifting the history by the jitter delta misregistered it by up to a display
            // pixel per frame - post-RR shimmer. lmxxf skips it the same way (LmxxfBackend.cpp).
            if (cfg.jitterSign != 0 && p->haveJitter && !historyReset && !f.writeBack)
            {
                jitterDx = float(cfg.jitterSign) * (p->lastJitterX - f.jitterX) * (float(w) / float(inputW));
                jitterDy = float(cfg.jitterSign) * (p->lastJitterY - f.jitterY) * (float(h) / float(inputH));
                if (!std::isfinite(jitterDx) || !std::isfinite(jitterDy) || std::fabs(jitterDx) > 4.f || std::fabs(jitterDy) > 4.f)
                    jitterDx = jitterDy = 0.f;
            }
            if (p->capture.isActive())
                p->Log("AMD capture frame " + std::to_string(p->capture.progress()) + ": model=" +
                       std::to_string(runModel ? 1 : 0) + " jitter=(" + std::to_string(f.jitterX) + "," +
                       std::to_string(f.jitterY) + ") delta=(" + std::to_string(jitterDx) + "," +
                       std::to_string(jitterDy) + ")");
            // Self-tuning (preset 8): the model's sharpening gain, measured on the last model
            // frame as the ratio of its high band to the raw's, reproduced on skipped frames.
            const float sharpGain = std::clamp(p->stabilizer->SharpRatio() - 1.f, 0.f, 1.5f);
            // A MODEL frame is one the model ANSWERED. A Record the runtime refused leaves the raw
            // frame in the slot, and handing that to the pass as "the model's answer" made every
            // preset store raw as the model's edit (or, under Edit accumulation, fit the model as
            // the identity) and show it as such. It is a filled frame, and is now told so.
            const bool answered = runModel && accepted > 0;
            // Edit accumulation's bound is a sanity ceiling only (4x the pixel): the Residual limit
            // is by design inactive at 100% NR on danielblnc (Config.h), and applying it here
            // would cut the model's legitimate lift on lights and sky, on some frames only.
            // (0.3.4, [DlssNr] AmdEditShaperCarryCap=true, A/B only) In shape mode - never at 100%, where shapeMode is
            // false - the shaper's own finite limit replaces the 4x ceiling: the shaper removed the resolve's final cap,
            // and the fitted gain/slope of the carried edit could re-expand a capped edit past it (Forza, preset 10).
            // Never above the 4x ceiling (carryCapLimit): F2's limit can exceed it near 100%.
            const bool carryCap = shapeMode && cfg.editShaperCarryCap && shapeLimit > 0.f;
            const float stabResidualCap = accumulate ? (carryCap ? carryCapLimit : 4.f) : cfg.residualLimit;
            try
            {
                finalColour = p->stabilizer->Run(
                    cmd, finalColour, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, motion, motionState,
                    depth, depthStateForStability, w, h,
                    f.motionScaleX * (resampleMotion ? float(w) / mvW : 1.0f),
                    f.motionScaleY * (resampleMotion ? float(h) / mvH : 1.0f),
                    alpha, historyReset, ghostReject, stabMode, cfg.stabilityThreshold,
                    interleaving ? 1.f : 0.f, detailScale, answered ? 1.f : 0.f,
                    cfg.interleaveSharp ? 1.f : 0.f, float(cfg.interleaveDebug),
                    float(cfg.interleavePreset),
                    residualTemporal ? p->scaleBaseline.Get() : nullptr,
                    (cfg.interleave <= 1.f && cfg.residualTemporal) ? 1.f : 0.f,
                    stabResidualCap,
                    f.depthInverted ? 1.f : 0.f,
                    cfg.interleaveGhostBound,
                    f.reactive, jitterDx, jitterDy, sharpGain,
                    // #5 still-surface steadiness: the every-frame clamp and the Residual temporal edit clamp only;
                    // Model interleave's presets keep their own rules (0 = off, byte-identical).
                    interleaving ? 0.f : cfg.stabilityStaticRelax,
                    (!interleaving && cfg.stabilityStaticDebug) ? 1.f : 0.f);
                if (accumulate)
                {
                    ++p->accumRuns;
                    ++(answered ? p->accumModel : p->accumFilled);
                }
            }
            catch (const std::exception& e)
            {
                // A reallocation inside the pass (resolution change) can fail the way its setup
                // can. The pass is dropped for the session; its objects stay (recorded lists may
                // still reference them) and the frame goes on with the unstabilised colour.
                p->stabilizerFailed = true;
                p->Log(std::string("AMD temporal stability stopped for this session (the neural pass runs without it): ") + e.what());
            }
            // Same reason as the pacer's line: "I cannot see a difference" and "it never
            // ran" look identical from the outside, and the first time each of these was
            // reported the answer turned out to be the second one.
            // Preset 3 only: v2 (6) replaces the bound's verdict with its raw-vs-raw test, so the
            // strength never acted there and this line claimed a control that did nothing.
            {
                // 0 = the every-frame clamp, 1 = the Residual temporal edit clamp (it returns before the clamp), 2 = not used
                const int path = interleaving ? 2 : (cfg.residualTemporal ? 1 : 0);
                if (cfg.stabilityStaticRelax != p->loggedStaticRelax || path != p->loggedStaticRelaxPath)
                {
                    p->loggedStaticRelax = cfg.stabilityStaticRelax;
                    p->loggedStaticRelaxPath = path;
                    char buf[192];
                    std::snprintf(buf, sizeof buf, "Still-surface steadiness %.2f (%s)%s",
                                  static_cast<double>(cfg.stabilityStaticRelax),
                                  path == 2 ? "not used while Model interleave is on"
                                  : path == 1 ? "Residual temporal edit clamp"
                                              : "every-frame clamp",
                                  cfg.stabilityStaticDebug && path != 2 ? ", overlay on" : "");
                    p->AppendLog(buf);
                }
            }
            if (!p->loggedGhostBound && interleaving && cfg.interleavePreset == 3)
            {
                p->loggedGhostBound = true;
                p->Log("Held frame ghost bound active: strength " +
                       std::to_string(cfg.interleaveGhostBound) + " (0 = off)");
            }
            if (!p->loggedGuidedFill && interleaving && cfg.interleavePreset == 6)
            {
                p->loggedGuidedFill = true;
                p->Log("Guided fill v2 active: Held picture; filled-frame fallbacks wear the model's tone (LUT); ghost "
                       "bound compares raw against raw (baseline bound: " +
                       std::string(p->scaleBaseline ? "yes" : "NO") + ")");
            }
            if (!p->loggedAccumulation && accumulate)
            {
                p->loggedAccumulation = true;
                char buf[640];
                std::snprintf(buf, sizeof buf,
                              "Edit accumulation active (interleave preset 10): every frame = this frame's raw + the model's "
                              "carried gain/slope, fitted on model frames, damped at Temporal stability %.2f, spread over two "
                              "frames; where refused, the model's tone curve + detail ratio; own pipeline; refused calls "
                              "count as filled frames; model fed per-frame motion (history %s); baseline bound: %s",
                              static_cast<double>(alpha), cfg.interleaveModelHistory ? "on" : "off",
                              p->scaleBaseline ? "yes" : "NO");
                p->Log(buf);
            }
            else if (!accumulate)
                p->loggedAccumulation = false;
            // The periodic proof that it runs and what it does: the frame types it saw, how much of
            // the picture wore the tone curve (the carry was not valid there), how much of that the
            // raw test alone refused, and how much of the model's local detail the fit keeps. The
            // readings are the temporal pass's, three frames old; no stall.
            if (accumulate && p->accumRuns >= p->accumLogAt + 1800)
            {
                p->accumLogAt = p->accumRuns;
                const UINT64 skipsNow = p->pendingSkips + p->fenceSkips;
                const float kept = p->stabilizer->DetailKept();
                const std::string keptText = kept >= -0.5f ? std::to_string(int(std::lround(100.0 * kept))) + "%" : "n/a";
                char buf[512];
                std::snprintf(buf, sizeof buf,
                              "AMD edit accumulation: model +%llu / filled +%llu (refused +%llu, skipped +%llu) | tone curve "
                              "%.1f%% | raw test refused %.1f%% | model detail kept %s | stability %.2f | lut %s | "
                              "jitter sign %d",
                              static_cast<unsigned long long>(p->accumModel),
                              static_cast<unsigned long long>(p->accumFilled),
                              static_cast<unsigned long long>(p->refusedCalls - p->accumRefusedAt),
                              static_cast<unsigned long long>(skipsNow - p->accumSkipsAt),
                              100.0 * p->stabilizer->ChangeFraction(), 100.0 * p->stabilizer->GateRefusedFraction(),
                              keptText.c_str(),
                              static_cast<double>(alpha), p->stabilizer->LutValid() ? "yes" : "no", cfg.jitterSign);
                p->Log(buf);
                p->accumModel = p->accumFilled = 0;
                p->accumRefusedAt = p->refusedCalls;
                p->accumSkipsAt = skipsNow;
            }
            if (!p->loggedGuidedFill && interleaving && cfg.interleavePreset == 5)
            {
                p->loggedGuidedFill = true;
                p->Log("Guided fill active: filled frames are a history-guided filter of the current "
                       "frame plus the carried edit (baseline bound: " +
                       std::string(p->scaleBaseline ? "yes" : "NO") + ")");
            }
        }
        else if (p->stabilizer)
            p->stabilizer->ResetHistory();
        // Contrast adaptive sharpening on the denoised colour (default off = skipped).

        if (p->capture.isActive())
        {
            p->capture.record(cmd, p->device.Get(), capturedBefore, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                              finalColour, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            if (p->capture.readyToWrite() && p->captureWriteAtFrame == 0)
                p->captureWriteAtFrame = p->captureFrameCounter + 8;
        }
        if (wantResolve) {
            // Scaled: compose onto the game's full-resolution colour, which is the point.
            // Not scaled: compose onto the pre-model copy, so both sides of the difference
            // come from the same buffer in the same space and only `strength` decides the
            // result. Using f.colour there would drag any encoding difference into the sum.
            guideDescriptors(slotBase + 10,scaled ? f.colour : p->scaleBaseline.Get(),
                             p->scaleOutput.Get(),DXGI_FORMAT_R16G16B16A16_FLOAT);
            auto handle=p->heap->GetCPUDescriptorHandleForHeapStart();
            auto stride=p->device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            handle.ptr += static_cast<SIZE_T>(slotBase + 12) * stride;
            auto v=srv;v.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;
            p->device->CreateShaderResourceView(p->scaleBaseline.Get(),&v,handle);
            handle.ptr+=stride;p->device->CreateShaderResourceView(finalColour,&v,handle);
            Barrier(cmd,f.colour,f.colourState,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            Barrier(cmd,p->scaleOutput.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            cmd->SetComputeRootSignature(p->root.Get());cmd->SetDescriptorHeaps(1,&heap);
            cmd->SetPipelineState(p->resolvePipeline.Get());
            auto table=heap->GetGPUDescriptorHandleForHeapStart();table.ptr+=static_cast<SIZE_T>(slotBase+10)*stride;cmd->SetComputeRootDescriptorTable(0,table);
            // Root table 2 names the (baseline, finalColour) pair written just above at
            // slotBase+12/+13, so it is two descriptors along from table 0. This used to
            // add (slotBase+2) on top of the already-advanced handle, which resolves to
            // 2*slotBase+12: correct for slot 0 and wrong for every other slot - slot 1
            // read another slot's pair, and from slot 3 it pointed past the end of the
            // kDescriptors heap outright. Only reachable on the `scaled` path, which is
            // why it survived: every measurement so far ran at NR resolution 100%.
            table.ptr+=static_cast<SIZE_T>(2)*stride;cmd->SetComputeRootDescriptorTable(2,table);
            UINT rc[7]{inputW,inputH,w,h,0,0,0};
            // Network output: the model's answer as it is, lifted at strength 1, unclamped and unfaded. At exactly 100%
            // (0.3.3.2 A-min) the resolve is the strength dial on the whole result: no limit, no fade. Away from 100% with
            // the edit shaper (shapeMode) strength and limit were applied to the model's edit already, so the resolve is a
            // pure lift with the edge fade kept; if the shaper failed to compile, 0.3.3.2's whole-result dial applies.
            const float rIntensity = (rawNetwork || shapeMode) ? 1.f : cfg.residualIntensity;
            const float rLimit = rawNetwork ? 1e6f : (shapeMode || !scaled) ? 0.f : cfg.residualLimit;
            const float rFade = (rawNetwork || !scaled) ? 0.f : cfg.residualFade;
            std::memcpy(rc+4,&rIntensity,4);std::memcpy(rc+5,&rLimit,4);std::memcpy(rc+6,&rFade,4);
            cmd->SetComputeRoot32BitConstants(1,7,rc,0);
            cmd->Dispatch((inputW+7)/8,(inputH+7)/8,1);
            Barrier(cmd,p->scaleOutput.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            Barrier(cmd,f.colour,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,f.colourState);
            finalColour=p->scaleOutput.Get();
        }
        p->resetAfterTimeout = false;
        p->lastSettings = cfg;
        p->haveSettings = true;
        p->lastInputWidth=inputW;p->lastInputHeight=inputH;
        p->lastMotionWidth = f.motionWidth;
        p->lastMotionHeight = f.motionHeight;
        p->lastGuideWidth = gW;
        p->lastGuideHeight = gH;
        p->lastJitterX = f.jitterX;
        p->lastJitterY = f.jitterY;
        p->haveJitter = true;
        p->hadExposure = exposureSource != nullptr;
        if (p->frames <= 120 || resize)
            p->Log(std::string(f.writeBack ? "Recorded post-RR " : "Recorded pre-SR ") + std::to_string(w) + "x" +
                   std::to_string(h) + " passes=" + std::to_string(p->activePasses) +
                   (f.writeBack ? " (output " + std::to_string(inputW) + "x" + std::to_string(inputH) +
                                      ", guides " + std::to_string(gW) + "x" + std::to_string(gH) + ")"
                                : std::string()));
        if(convertEncoding) finalColour=sl->encode->Run(cmd,finalColour,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,inputW,inputH,cfg.encoding,true);
        // POST-RR: THE RESULT GOES BACK INTO THE TEXTURE IT CAME FROM. There is no upscaler
        // after this call to hand a replacement to; the caller's output is the frame the game
        // presents. A half-float output takes a region copy; anything else goes through the
        // crop shader at identity size with a typed view of the output, which needs the UAV
        // flag every DLSS output already carries. Every read of f.colour above is ordered
        // before this by its own transitions.
        if ((f.writeBack || f.inputWriteBack) && finalColour)
        {
            const auto td = f.colour->GetDesc();
            const bool copyable = td.Format == DXGI_FORMAT_R16G16B16A16_FLOAT ||
                                  td.Format == DXGI_FORMAT_R16G16B16A16_TYPELESS;
            if (copyable)
            {
                Barrier(cmd, finalColour, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_SOURCE);
                Barrier(cmd, f.colour, f.colourState, D3D12_RESOURCE_STATE_COPY_DEST);
                D3D12_TEXTURE_COPY_LOCATION from {}, to {};
                from.pResource = finalColour;
                to.pResource = f.colour;
                D3D12_BOX box { 0, 0, 0, inputW, inputH, 1 };
                cmd->CopyTextureRegion(&to, 0, 0, 0, &from, &box);
                Barrier(cmd, f.colour, D3D12_RESOURCE_STATE_COPY_DEST, f.colourState);
                Barrier(cmd, finalColour, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            }
            else if (td.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS)
            {
                guideDescriptors(slotBase + 16, finalColour, f.colour, WriteFormat(td.Format));
                Barrier(cmd, f.colour, f.colourState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                if (f.colourState == D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
                {
                    // No transition happened, so order our store after the upscaler's own.
                    D3D12_RESOURCE_BARRIER uavBarrier {};
                    uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
                    uavBarrier.UAV.pResource = f.colour;
                    cmd->ResourceBarrier(1, &uavBarrier);
                }
                cmd->SetComputeRootSignature(p->root.Get());
                cmd->SetPipelineState(p->pipeline.Get());
                auto heapPtr = p->heap.Get();
                cmd->SetDescriptorHeaps(1, &heapPtr);
                auto table = p->heap->GetGPUDescriptorHandleForHeapStart();
                table.ptr += static_cast<SIZE_T>(slotBase + 16) * descriptorStride;
                cmd->SetComputeRootDescriptorTable(0, table);
                UINT backDims[] { inputW, inputH, inputW, inputH };
                cmd->SetComputeRoot32BitConstants(1, 4, backDims, 0);
                cmd->Dispatch((inputW + 7) / 8, (inputH + 7) / 8, 1);
                Barrier(cmd, f.colour, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, f.colourState);
            }
            else
            {
                if (!p->loggedWriteBackRefusal)

            {
                p->loggedWriteBackRefusal = true;
                p->Log("AMD post-RR: output " + Layout(f.colour) +
                       " is neither half-float nor UAV-capable; the result was not written back");
                }
                return nullptr;
            }
        }
#ifdef AMD_RETIRE_DIAGNOSTICS
        timing.event.outcome = "recorded";
#endif
        return finalColour;
    }
    catch (const std::exception& e)
    {
        // This latches the backend off for the session. The reason used to go to the log
        // only, so from the menu it looked like the checkbox did nothing; now the status
        // line carries it, which is where a user looks first.
        p->failed = true;
        p->failReason = e.what();
        p->status = std::string("AMD pre-SR stopped: ") + e.what();
        p->Log(e.what());
        return nullptr;
    }
}
int Backend::PendingListIndex(UINT count, ID3D12CommandList* const* lists) const
{
    if (!lists) return -1;
    for (size_t s = 0; s < p->slots.size(); ++s)
    {
        auto pending = p->slots[s].pending.load(std::memory_order_acquire);
        if (!pending) continue;
        for (UINT i = 0; i < count; ++i)
            if (lists[i] == pending) return static_cast<int>(i);
    }
    return -1;
}
void Backend::ListReset(ID3D12CommandList* list, bool ownBridge)
{
    // Lock-free (see AmdPreSr.h): this runtime resets lists of its own inside Record, under `lock`. Every
    // slot holding this list is marked; RecoverDiscarded, under the lock, tells a submitted one (the game
    // reusing the list, as it does every frame) from one that never was.
    if (!list)
        return;
    for (auto& sl : p->slots)
        if (sl.pending.load(std::memory_order_acquire) == list)
        {
            if (ownBridge)
                sl.resetCertain.store(true, std::memory_order_relaxed);
            sl.resetSeen.store(list, std::memory_order_release);
        }
}
void Backend::TraceBoundary(const std::string& reason)
{
    // (0.3.3.2) Never waits for the recording lock: this comes from the game's context release, which can
    // be on another thread (or at exit) while Submitted holds it for seconds. Busy: the reason alone, in
    // the file only (the status line and the slot state belong to whoever holds the lock).
    std::unique_lock<std::mutex> guard(p->lock, std::try_to_lock);
    if (!guard.owns_lock())
    {
        p->AppendLog("AMD boundary: " + reason + " (backend busy, no snapshot)");
        return;
    }
    p->TraceBoundary(reason);
}
void Backend::Submitting(ID3D12CommandQueue* queue, UINT n, ID3D12CommandList* const* lists)
{
    if (!queue)
        return;
    // Atomic-only prefilter. Do not take p->lock here: Record may already hold it.
    std::array<UINT, Impl::kMaxSlots> candSlots {};
    std::array<ID3D12CommandList*, Impl::kMaxSlots> candPending {};
    const UINT cands = p->FindPendingCandidates(n, lists, candSlots, candPending);
    if (!cands)
        return;
    std::lock_guard guard(p->lock);
    const LockOwnerMark owned(p->lockOwner); // (0.3.3.2) for Shutdown on this thread
    const AmdLayout* L = p->L;
    if (!L)
        return;
    // Choose under the lock. submitted is a plain bool, and a newer slot can
    // hold the same list pointer as an older one that is already submitted.
    UINT slot = 0;
    ID3D12CommandList* pending = nullptr;
    if (!p->PickUnsubmitted(candSlots, candPending, cands, slot, pending))
        return;
    auto& sl = p->slots[slot];
    if (p->frames <= 120)
        p->Log("Neural submission: lists=" + std::to_string(n) + " queueType=" +
               std::to_string(static_cast<UINT>(queue->GetDesc().Type)));
    // Match the recorded list, not the swapchain's presentation queue. FG can
    // replace the latter, and the renderer may also migrate between queues.
    // Preserve a single ordered fence timeline across queue migration. A high
    // signal on a different queue is otherwise no proof that older work ended.
    // This is a GPU dependency inserted BEFORE Execute, not a CPU/HIP wait.
    //
    // (0.3.4, P1 Marvel's Midnight Suns, [DlssNr] AmdStreamlineDeviceFix) Not a migration: a backend built on the
    // device behind the game's proxy device (AmdBridge::NrOnDeviceBehindProxy) holds the game's native queue, and the
    // game submits through its proxy of that same queue. Moving L->queue to the proxy would send the runtime's own
    // native lists through the proxy's ExecuteCommandLists; the queue stays, with no Wait (one queue, one timeline).
    // lmxxf's wrapper branch does the same (LmxxfBackend.cpp, "no rebind"). Any other queue migrates as before.
    if (queue != p->queue.Get() && DlssNr::AmdBridge::NrOnDeviceBehindProxy() &&
        DlssNr::SameQueueObject(queue, p->queue.Get()))
    {
        static bool saidSameQueue = false; // under p->lock; one backend per process
        if (!saidSameQueue)
        {
            saidSameQueue = true;
            p->AppendLog("Render submission arrives on the game's queue through its proxy (the same queue object); no "
                         "migration, the runtime keeps the queue behind it (noted once)");
        }
    }
    else if (queue != p->queue.Get())
    {
        // (0.3.4, P1 review) A real migration in a backend built on the device behind the game's proxy device binds
        // the queue behind the new proxy queue when it answers Streamline's GUID (ComIdentity.h, QueueBehindProxy), so
        // the runtime's own native lists keep going to a native queue; the next submission through that proxy is then
        // the rule above. A queue that does not answer is bound as before. Every other backend: unchanged.
        ComPtr<ID3D12CommandQueue> behind;
        if (DlssNr::AmdBridge::NrOnDeviceBehindProxy())
            behind.Attach(DlssNr::QueueBehindProxy(queue));
        ID3D12CommandQueue* const bind = behind ? behind.Get() : queue;
        if (p->serial && FAILED(queue->Wait(p->fence.Get(), p->serial)))
        {
            p->failed = true;
            p->completionOrderValid = false;
            p->Log("Render queue migration Wait failed; stopping NR and retaining outstanding resources");
        }
        for (auto h : p->runtime)
            if (h)
            {
                auto old = At<ID3D12CommandQueue*>(h, L->queue);
                bind->AddRef();
                At<ID3D12CommandQueue*>(h, L->queue) = bind;
                if (old)
                    old->Release();
            }
        p->queue = bind;
        p->Log("Render submission queue changed; dependency on completion=" + std::to_string(p->serial) +
               (behind ? " (bound the queue behind the game's proxy queue)" : ""));
    }
    sl.submissionQueue = queue;
    // Bind the real queue here, but only wake HIP after ExecuteCommandLists.
    // A capture-wait kernel launched before D3D12 submission can occupy the GPU
    // while the capture it depends on is still queued on the CPU.

}
void Backend::Submitted(ID3D12CommandQueue* queue, UINT n, ID3D12CommandList* const* lists)
{
    // Every caller must pair Submitting -> real Execute -> Submitted. A queue
    // dependency inserted here would be too late to protect this list's work.
    if (!queue)
        return;
    // Same lock-after-match order as Submitting. Do not lock first: Record can
    // hold p->lock while this thread re-enters via the Execute hook.
    std::array<UINT, Impl::kMaxSlots> candSlots {};
    std::array<ID3D12CommandList*, Impl::kMaxSlots> candPending {};
    const UINT cands = p->FindPendingCandidates(n, lists, candSlots, candPending);
    if (!cands)
        return;
    std::lock_guard guard(p->lock);
    const LockOwnerMark owned(p->lockOwner); // (0.3.3.2) for Shutdown on this thread
    const AmdLayout* L = p->L;
    if (!L)
        return;
    // A reused command-list pointer can also match an older submitted slot;
    // only the current unsubmitted generation may be published here.
    UINT slot = 0;
    ID3D12CommandList* pending = nullptr;
    if (!p->PickUnsubmitted(candSlots, candPending, cands, slot, pending))
        return;
    ++p->submittedByPointer; // (0.3.3.2) this game's neural lists reach ExecuteCommandLists as recorded
    auto& sl = p->slots[slot];
    if (sl.submissionQueue.Get() != queue)
    {
        p->failed = true;
        p->completionOrderValid = false;
        p->Log("AMD submission was not prepared on this queue; retaining resources");
    }
    // Notify uses this slot's recorded pass count. 0 = the runtime refused (no HIP job).
    const UINT passCount = (sl.passCount == Impl::Slot::kPassUnset) ? 0u : sl.passCount;
    const auto notifyPass = [&](UINT i) {
        auto h = p->runtime[i];
        const bool matched = At<ID3D12CommandList*>(h, L->pendingList) == pending;
        if (matched)
            reinterpret_cast<NotifyFn>(reinterpret_cast<uintptr_t>(h) + L->notify)(queue, n, lists);
        if (!matched || At<ID3D12CommandList*>(h, L->pendingList) != nullptr)
        {
            p->failed = true;
            p->completionOrderValid = false;
            p->Log("AMD Notify did not consume the expected pending list; retaining resources, pass=" +
                   std::to_string(i + 1));
            return false;
        }
        return true;
    };
    if (passCount == 1)
    {
        notifyPass(0);
        auto value = ++p->serial;
        if (FAILED(queue->Signal(p->fence.Get(), value)))
        {
            p->failed = true;
            p->Log("D3D12 completion Signal failed; resources retained");
            return;
        }
        sl.completion.store(value);
        sl.submission.Submit(GetTickCount64());
        p->WaitAfterSubmitIfEveryFrame(slot);
        return;
    }
    for (UINT i = 0; i < passCount; ++i)
    {
        auto h = p->runtime[i];
        if (!notifyPass(i))
            break;
        // All runtimes use HIP stream 0. Publish the next pass only once the previous
        // worker finished; otherwise its capture-wait kernel could block the first pass.
        auto start = GetTickCount64();
        while (static_cast<UINT>(InterlockedCompareExchange(reinterpret_cast<volatile LONG*>(&At<UINT>(h, L->jobDone)), 0,
                                                            0)) < sl.jobs[i])
        {
            if (GetTickCount64() - start > 5000)
            {
                p->failed = true;
                p->Log("HIP completion timeout pass " + std::to_string(i + 1));
                break;
            }
            Sleep(1);
        }
    }
    UINT64 value = ++p->serial;
    if (FAILED(queue->Signal(p->fence.Get(), value)))
    {
        p->failed = true;
        p->Log("D3D12 completion Signal failed");
        return;
    }
    sl.completion.store(value);
    sl.submission.Submit(GetTickCount64());
    p->WaitAfterSubmitIfEveryFrame(slot);
    p->RetireSubmission(false, "Submitted");
}
std::string Backend::Status() const
{
    // This runs from the MENU, on the game's render thread, every frame the Neural section
    // is open. Two things it used to do were wrong for that.
    //
    // It took `lock` and waited. The runtime here runs its inline wait - the log says
    // "mode inline (same-frame, game waits for the network)" and measures the game's queue
    // spinning 17-20 ms a frame on it - so that thread is already at its limit before the
    // menu asks for anything. Adding a blocking acquire of the backend's main lock on top
    // is how it stops answering, and a game with a frame watchdog kills it for that. One
    // did: Neverness to Everness, with NR running perfectly and the crash arriving the
    // moment this section was expanded, reported from the game's own crash SDK.
    //
    // And it called RetireSubmission, which is not a read. Drawing a line of text was
    // retiring GPU submissions - real state, mutated from the UI thread, racing whatever
    // the worker was doing with the same objects. A status function has no business
    // advancing anything; the recording path retires its own work and always did.
    //
    // So: never block, never mutate. If the lock is busy, hand back the last line built -
    // it is at most a frame or two old, which is invisible in text that reports counters.
    std::unique_lock<std::mutex> guard(p->lock, std::try_to_lock);
    if (!guard.owns_lock())
    {
        std::lock_guard<std::mutex> t(p->statusTextLock);
        return p->statusText.empty() ? std::string("AMD pre-SR: busy") : p->statusText;
    }
    const AmdLayout* L = p->L;
    auto reportedTimeouts = p->timeoutEvents;
    for (UINT i = 0; L && i < p->runtime.size(); ++i)
        if (p->runtime[i])
        {
            auto count = At<UINT>(p->runtime[i], L->timeoutCount);
            if (count > p->observedTimeouts[i])
                reportedTimeouts += count - p->observedTimeouts[i];
        }
    // Menu / Status must name the runtime that was actually identified —
    // 0.3.0 and 0.3.1 are both valid, and the user cannot tell them apart
    // from pass DLL filenames alone.
    // (0.3.4, danielblnc support) The shown name: a build named by its digest prefix reads as the version its file carries.
    const char* shownName = LoadedRuntimeName();
    const std::string runtimeTag = L ? (std::string("AMD runtime ") + (shownName ? shownName : L->name) + " | ") : std::string();
    std::string built;
    if (!p->failed && p->lastSubmitted)
        built = runtimeTag + p->status + (p->rtgiStatus.empty() ? "" : " | " + p->rtgiStatus) + " | completed frames=" + std::to_string(p->completedFrames) +
                (p->lastCompleted ? " last completion " + std::to_string((GetTickCount64() - p->lastCompleted) / 1000) + "s ago" : " no successful completion") +
                " | timeout events=" + std::to_string(reportedTimeouts) +
                " | skipped pending/GPU=" + std::to_string(p->pendingSkips) + "/" + std::to_string(p->fenceSkips);
    else
        built = runtimeTag + p->status;
    {
        std::lock_guard<std::mutex> t(p->statusTextLock);
        p->statusText = built;
    }
    return built;
}
UINT64 Backend::RecordedFrames() const { return p->frames; }
void Backend::NoteLine(const std::string& line)
{
    // The menu thread, like Status(): never wait on p->lock (a frame watchdog killed NTE for a blocking
    // acquire here). Under the lock when it is free, so the line cannot land inside a Record line;
    // otherwise without it - one short append. Never Log(): that would replace the status line.
    std::unique_lock<std::mutex> guard(p->lock, std::try_to_lock);
    p->AppendLog(line);
}
bool Backend::Stopped() const { return p->failed.load(); }
void Backend::InvalidateHistory() { p->resetRequested.store(true); }
void Backend::RequestCapture(unsigned delayMs)
{
    if (delayMs == 0)
    {
        p->capture.request(capture::kMaxFrames);
        p->Log("AMD capture requested (key or menu)");
        return;
    }
    p->captureAtTick.store(GetTickCount64() + delayMs);
    p->Log("AMD capture requested in " + std::to_string(delayMs) + " ms");
}
bool Backend::Ready()
{
    std::lock_guard guard(p->lock);
    if (!p->fence) return false;
    p->RetireSubmission(false, "Ready");
    const auto completed = p->fence->GetCompletedValue();
    return !p->failed && !p->AnySlotBusy() && completed != UINT64_MAX && completed >= p->LatestCompletion();
}
bool Backend::Shutdown()
{
    // (0.3.3.2) Exit on the thread that holds `lock` inside Record or Submitted (a crash handler calling
    // ExitProcess from inside our recording): locking again would throw or deadlock, and the runtime is
    // mid-call. Left to the process, like the busy case below.
    if (p->lockOwner.load(std::memory_order_relaxed) == GetCurrentThreadId())
    {
        p->failed = true;
        p->AppendLog("AMD exit on the recording thread while it holds the backend (a crash handler?): workers left to the process");
        return false;
    }
    // Process exit (C1-A, 0.3.3.2 rebuild): at most about 2 s for the lock (Submitted can hold it 5 s per pass); on a
    // timeout, or with a job still busy, the runtime is left to the process and nothing more is recorded (`failed`,
    // atomic). MSVC's try_lock answers false, not a throw, when this thread owns the lock.
    std::unique_lock<std::mutex> guard(p->lock, std::defer_lock);
    for (const ULONGLONG start = GetTickCount64(); !guard.try_lock();)
    {
        if (GetTickCount64() - start >= 2000)
        {
            p->failed = true;
            return false;
        }
        Sleep(1);
    }
    const AmdLayout* L = p->L;
    if (!p->fence) return false;
    p->RetireSubmission(false, "Shutdown");
#ifdef AMD_RETIRE_DIAGNOSTICS
    p->diagnostics.Flush(p->directory, L ? L->name : "uninitialized", "shutdown");
#endif
    const auto completed = p->fence->GetCompletedValue();
    if (p->AnySlotBusy() || completed == UINT64_MAX || completed < p->LatestCompletion())
    {
        p->failed = true;
        return false;
    }
    for (auto h : p->runtime)
        if (h && L)
        {
            if (p->hipSet)
                p->hipSet(p->hipDevice);
            reinterpret_cast<void (*)()>(reinterpret_cast<uintptr_t>(h) + L->shutdown)();
        }
    p->failed = true;
    p->Log("Workers stopped outside loader lock");
    return true;
}
} // namespace AmdPreSr
