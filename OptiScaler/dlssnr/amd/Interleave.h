// Copyright (c) 2026 3zwr1 (AMDNR). Part of AMDNR (GPL-3.0; see Licenses/AMDNR_NOTICE.txt).
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <d3d12.h>
#include <d3dcompiler.h>
#include "SystemCompiler.h"
#include <wrl/client.h>
#include <stdexcept>
#include <string>

namespace AmdPreSr {

// Model interleave: run the neural model on only some frames and carry the rest.
//
// This file owns the two pieces that decide WHICH frames the model runs on and WHAT
// MOTION it is told about when it does. The third piece - what a carried frame shows -
// lives in TemporalStability.h under its MODEL INTERLEAVE marker.
//
// ---------------------------------------------------------------------------------
// The motion problem, which is the reason this file exists
// ---------------------------------------------------------------------------------
// The model is a TEMPORAL denoiser: it keeps its own history internally and reprojects
// it with the motion vectors it is handed. Those vectors describe one frame of motion,
// which is correct only if the model ran on the previous frame.
//
// Under interleave it did not. At cadence 2 the model last ran TWO frames ago, so its
// internal history is two frames old - but it was still being handed one frame of
// motion, so it reprojected that history by half the distance the camera actually
// travelled. Every single model call started from a misaligned history. That is damage
// done INSIDE the model, before our pass ever sees the picture, and no amount of
// filtering afterwards can undo it: it shows up as the model's output changing shape
// and brightness from call to call, and as smearing that looks exactly like ghosting.
//
// The fix is to hand the model the TOTAL motion since the last frame it actually ran
// on. That cannot be read from any single frame's vectors - it has to be accumulated
// across the skipped frames by chaining them:
//
//     A_n(p) = mv_n(p) + A_(n-1)( p + mv_n(p) )
//
// mv_n(p) walks pixel p back to where it was one frame ago; looking A_(n-1) up AT that
// position walks it the rest of the way back to the last model frame. After a model
// frame consumes the total, the accumulator resets to zero and the next cycle starts.
//
// Kept in the motion resource's own units (not pixels), so the scale factors the host
// already computes for the model apply to the accumulated vector unchanged.
inline constexpr char MotionAccumulateShader[] = R"(
Texture2D<float4> mvTex     : register(t0);
Texture2D<float2> accumPrev : register(t1);
RWTexture2D<float2> accumNext : register(u0);
RWTexture2D<float2> total     : register(u1);
SamplerState samp : register(s0);
cbuffer P : register(b0) { uint w; uint h; float scaleX; float scaleY; uint modelFrame; uint resetAll; }
[numthreads(8,8,1)] void main(uint3 tid : SV_DispatchThreadID) {
 if (tid.x>=w || tid.y>=h) return;
 int2 p = int2(tid.xy);
 float2 mvNow = mvTex.Load(int3(p,0)).xy;
 if (!all(isfinite(mvNow))) mvNow = float2(0.0, 0.0);
 float2 carried = float2(0.0, 0.0);
 if (resetAll == 0) {
  // Walk back one frame, in pixels, and pick up how far THAT pixel still had to go.
  // Sampled (not Loaded) because the step is sub-pixel, and clamped into the frame
  // rather than discarded: a vector leaving the frame still has a best answer at the
  // edge, and dropping it would make the accumulated motion wrong in a band around
  // the border - the model would then reproject that band by the wrong distance.
  float2 prevPx = float2(p) + 0.5 + mvNow * float2(scaleX, scaleY);
  float2 uv = clamp(prevPx, float2(0.5, 0.5), float2(float(w), float(h)) - 0.5)
            / float2(float(w), float(h));
  float2 a = accumPrev.SampleLevel(samp, uv, 0);
  if (all(isfinite(a))) carried = a;
 }
 float2 sum = mvNow + carried;
 total[p] = sum;
 // A model frame consumes `total` and starts the next cycle from nothing.
 accumNext[p] = modelFrame != 0 ? float2(0.0, 0.0) : sum;
}
)";

// Chooses model frames and accumulates motion across the skipped ones.
class Interleave
{
    using Res = Microsoft::WRL::ComPtr<ID3D12Resource>;
    static constexpr UINT kRegions = 5;   // same in-flight rule as TemporalStability
    static constexpr UINT kPerRegion = 4; // t0 mv, t1 accumPrev, u0 accumNext, u1 total

    Microsoft::WRL::ComPtr<ID3D12Device> device;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> root;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipeline;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> heap;
    Res accum[2];
    Res total;
    UINT width = 0, height = 0, index = 0, region = 0, stride = 0;
    bool primed = false;

    // --- cadence ---
    // Whole numbers only. A fractional cadence cannot space its skipped frames evenly:
    // 1.5 produces M F M M F M M F, a period of three with uneven gaps, which beats at
    // 20 Hz on a 60 fps frame - close to the eye's worst case - while 2 gives an even
    // M F M F at 30 Hz. This was the single value users reported as clearly wrong, and
    // it is not tunable, so the type is an integer and nothing fractional can be set.
    UINT period = 0;       // 0 = off, else run the model once every `period` frames
    UINT phase = 0;        // frames since the last model frame

    // --- adaptive cadence ---
    // The model runs every frame for kBoostFrames after a filled frame had to fall back on
    // more than kChangeEnter of the (sampled) picture, or after any frame had more than
    // kMotionEnter of it moving by over a pixel. A skipped frame is indistinguishable from a
    // model frame only when the picture stands still; these two numbers are what "stands
    // still" means. The change fraction is smoothed over the filled frames it is measured
    // on, so one noisy reading neither starts nor ends a boost by itself.
    // Set from the sensitivity setting (SetAdaptive); the defaults are "Medium".
    float kChangeEnter = 0.03f;
    float kMotionEnter = 0.30f;
    UINT kBoostFrames = 8;
    UINT boostLeft = 0;
    float changeEma = 0.f;
    unsigned long long boosts = 0;

    static void Check(HRESULT hr)
    {
        if (FAILED(hr))
            throw std::runtime_error("AMD interleave D3D12 error " + std::to_string((UINT) hr));
    }

    static void Barrier(ID3D12GraphicsCommandList* c, ID3D12Resource* r, D3D12_RESOURCE_STATES a,
                        D3D12_RESOURCE_STATES b)
    {
        if (a == b || !r)
            return;
        D3D12_RESOURCE_BARRIER v {};
        v.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        v.Transition = { r, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, a, b };
        c->ResourceBarrier(1, &v);
    }

    // A fully typed SRV format for whatever 2- or 4-channel motion layout the game
    // supplied. Mirrors TemporalStability's own MotionFormat for the same reason:
    // each pass resolves the format it reads rather than trusting a caller to.
    static DXGI_FORMAT MotionFormat(DXGI_FORMAT f)
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

    void Alloc(UINT w, UINT h)
    {
        D3D12_HEAP_PROPERTIES hp {};
        hp.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC rd {};
        rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        rd.Width = w;
        rd.Height = h;
        rd.DepthOrArraySize = 1;
        rd.MipLevels = 1;
        // R16G16_FLOAT is what the host's own motion resample already hands the model,
        // so it is known to be an acceptable input format. Motion magnitudes are small
        // and only two or three are ever summed, which is well inside half precision.
        rd.Format = DXGI_FORMAT_R16G16_FLOAT;
        rd.SampleDesc.Count = 1;
        rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        for (int i = 0; i < 2; ++i)
        {
            accum[i].Reset();
            Check(device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
                                                  D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                                                  nullptr, IID_PPV_ARGS(&accum[i])));
        }
        total.Reset();
        Check(device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
                                              D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                                              nullptr, IID_PPV_ARGS(&total)));
        width = w;
        height = h;
        index = 0;
        primed = false;
    }

  public:
    explicit Interleave(ID3D12Device* d) : device(d)
    {
        D3D12_DESCRIPTOR_RANGE ranges[2] = { { D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 0, 0, 0 },
                                             { D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 2, 0, 0, 2 } };
        D3D12_ROOT_PARAMETER params[2] {};
        params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[0].DescriptorTable = { 2, ranges };
        params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        params[1].Constants = { 0, 0, 6 };
        D3D12_STATIC_SAMPLER_DESC samp {};
        samp.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        samp.AddressU = samp.AddressV = samp.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samp.ShaderRegister = 0;
        samp.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        D3D12_ROOT_SIGNATURE_DESC rsd {};
        rsd.NumParameters = 2;
        rsd.pParameters = params;
        rsd.NumStaticSamplers = 1;
        rsd.pStaticSamplers = &samp;
        Microsoft::WRL::ComPtr<ID3DBlob> b, e;
        Check(D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &b, &e));
        Check(d->CreateRootSignature(0, b->GetBufferPointer(), b->GetBufferSize(), IID_PPV_ARGS(&root)));
        Check(DlssNr::SysCompiler::Compile(MotionAccumulateShader, sizeof(MotionAccumulateShader),
                         "AMD interleave motion accumulate", nullptr, nullptr, "main", "cs_5_0", 0, 0,
                         &b, &e));
        D3D12_COMPUTE_PIPELINE_STATE_DESC pd {};
        pd.pRootSignature = root.Get();
        pd.CS = { b->GetBufferPointer(), b->GetBufferSize() };
        Check(d->CreateComputePipelineState(&pd, IID_PPV_ARGS(&pipeline)));
        D3D12_DESCRIPTOR_HEAP_DESC hd {};
        hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        hd.NumDescriptors = kRegions * kPerRegion;
        hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        Check(d->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&heap)));
        stride = d->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }

    // `cadence` is the host's configured value; anything fractional is rounded.
    void SetCadence(float cadence)
    {
        const UINT next = cadence > 1.f ? (UINT) (cadence + 0.5f) : 0u;
        if (next != period)
        {
            period = next;
            phase = 0;   // a cadence change restarts the cycle on a model frame
        }
    }

    bool Enabled() const { return period > 1; }

    // Whether the model should run this frame. Called once per recorded frame, before
    // Accumulate. A plain counter, not the fractional accumulator this used to have:
    // with whole periods the pattern is exactly one skip run of (period-1) frames
    // between model frames, evenly spaced, which is the only pattern that does not
    // beat at some third frequency of its own.
    bool BeginFrame()
    {
        if (period <= 1)
        {
            phase = 0;
            return true;
        }
        const bool runModel = phase == 0;
        phase = (phase + 1) % period;
        return runModel;
    }

    void Reset()
    {
        primed = false;
        phase = 0;
        boostLeft = 0;
        changeEma = 0.f;
    }

    // A frame on which the host does not need the accumulated motion (Edit accumulation with the
    // runtime's history off hands the model this frame's own vectors) skips Accumulate; the chain
    // it would have extended is then stale, so the next Accumulate starts it afresh. The cadence
    // is untouched.
    void SkipMotion() { primed = false; }

    // Adaptive cadence. Called once per frame with the temporal pass's last readings (three
    // frames old): the fraction of a filled frame that fell back (only when that reading came
    // from a filled frame) and the fraction of the picture in motion. Decides whether the
    // model must run on frames the cadence would skip.
    void NoteChange(float changeFrac, bool measuredFilled, float motionFrac)
    {
        if (boostLeft > 0)
            --boostLeft;
        if (measuredFilled)
            changeEma = changeEma * 0.5f + changeFrac * 0.5f;
        if ((measuredFilled && changeEma > kChangeEnter) || motionFrac > kMotionEnter)
        {
            if (boostLeft == 0)
                ++boosts;
            boostLeft = kBoostFrames;
        }
    }
    bool Boost() const { return boostLeft > 0; }
    // Sensitivity: how much visible change it takes to run the model on every frame.
    // 0 = Low (most fps, artefacts tolerated longer), 1 = Medium, 2 = High (least fps).
    void SetAdaptive(UINT sensitivity)
    {
        switch (sensitivity)
        {
        case 0: kChangeEnter = 0.06f; kMotionEnter = 0.50f; kBoostFrames = 6; break;
        case 2: kChangeEnter = 0.015f; kMotionEnter = 0.15f; kBoostFrames = 12; break;
        default: kChangeEnter = 0.03f; kMotionEnter = 0.30f; kBoostFrames = 8; break;
        }
    }
    unsigned long long Boosts() const { return boosts; }

    // The accumulated motion for THIS frame, valid after Accumulate. On a model frame
    // this is the total since the model last ran, which is what it must be handed.
    ID3D12Resource* TotalMotion() const { return total.Get(); }

    // Chains this frame's vectors onto the carried total. `motion`'s state is restored.
    // Returns the resource holding the accumulated motion, or `motion` itself if the
    // pass could not run - callers can use the result unconditionally.
    ID3D12Resource* Accumulate(ID3D12GraphicsCommandList* c, ID3D12Resource* motion,
                               D3D12_RESOURCE_STATES motionState, UINT w, UINT h, float scaleX,
                               float scaleY, bool modelFrame, bool reset)
    {
        if (!c || !motion || !w || !h)
            return motion;
        if (width != w || height != h)
            Alloc(w, h);
        ID3D12Resource* prev = accum[index].Get();
        ID3D12Resource* next = accum[index ^ 1].Get();
        const bool resetAll = reset || !primed;

        const UINT base = region * kPerRegion;
        region = (region + 1) % kRegions;
        auto cpu = heap->GetCPUDescriptorHandleForHeapStart();
        cpu.ptr += static_cast<SIZE_T>(base) * stride;
        auto addSrv = [&](ID3D12Resource* r, DXGI_FORMAT fmt) {
            D3D12_SHADER_RESOURCE_VIEW_DESC s {};
            s.Format = fmt;
            s.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            s.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            s.Texture2D.MipLevels = 1;
            device->CreateShaderResourceView(r, &s, cpu);
            cpu.ptr += stride;
        };
        addSrv(motion, MotionFormat(motion->GetDesc().Format));
        addSrv(prev, DXGI_FORMAT_R16G16_FLOAT);
        auto addUav = [&](ID3D12Resource* r) {
            D3D12_UNORDERED_ACCESS_VIEW_DESC u {};
            u.Format = DXGI_FORMAT_R16G16_FLOAT;
            u.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
            device->CreateUnorderedAccessView(r, nullptr, &u, cpu);
            cpu.ptr += stride;
        };
        addUav(next);
        addUav(total.Get());

        Barrier(c, motion, motionState, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        Barrier(c, next, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        Barrier(c, total.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        auto hh = heap.Get();
        c->SetDescriptorHeaps(1, &hh);
        c->SetComputeRootSignature(root.Get());
        c->SetPipelineState(pipeline.Get());
        auto gpu = heap->GetGPUDescriptorHandleForHeapStart();
        gpu.ptr += static_cast<UINT64>(base) * stride;
        c->SetComputeRootDescriptorTable(0, gpu);
        struct
        {
            UINT w, h;
            float sx, sy;
            UINT modelFrame, resetAll;
        } cb { w, h, scaleX, scaleY, modelFrame ? 1u : 0u, resetAll ? 1u : 0u };
        c->SetComputeRoot32BitConstants(1, 6, &cb, 0);
        c->Dispatch((w + 7) / 8, (h + 7) / 8, 1);
        Barrier(c, next, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        Barrier(c, total.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        Barrier(c, motion, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, motionState);
        index ^= 1;
        primed = true;
        return total.Get();
    }
};

} // namespace AmdPreSr
