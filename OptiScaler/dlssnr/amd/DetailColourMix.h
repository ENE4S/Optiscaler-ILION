// Copyright (c) 2026 3zwr1 (AMDNR). Part of AMDNR (GPL-3.0; see Licenses/AMDNR_NOTICE.txt).
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <d3d12.h>
#include <d3dcompiler.h>
#include "SystemCompiler.h"
#include <wrl/client.h>
#include <algorithm>
#include <stdexcept>
#include <string>
namespace AmdPreSr {
// Detail strength and Colour strength for the AMD in-place denoise. The model
// overwrites the colour buffer, so there is no ratio composition to attach these
// to as the DX12 path does; instead this captures the pre-denoise colour and, at
// the end, blends the denoised result back toward it:
//   detail  - how far the brightness moves toward the model's answer (1 = full).
//   colour  - whether the model's own hue is used (1) or the original hue is kept
//             at the model's brightness (0).
// At detail=1 and colour=1 the pass is skipped entirely, so it is byte-identical
// when left at its defaults. Self-contained (own heap/PSO/resources), run on the
// render queue like ColorEncoding / TemporalStability.
inline constexpr char DetailColourShader[] = R"(
Texture2D<float4> den  : register(t0);
Texture2D<float4> base : register(t1);
RWTexture2D<float4> dst : register(u0);
cbuffer P : register(b0) { uint w; uint h; float detail; float colour; }
[numthreads(8,8,1)] void main(uint3 tid:SV_DispatchThreadID) {
 if (tid.x>=w || tid.y>=h) return;
 int2 p = int2(tid.xy);
 float4 dc = den.Load(int3(p,0));
 float3 d = dc.rgb, o = base.Load(int3(p,0)).rgb;
 if (!all(isfinite(d)) || !all(isfinite(o))) { dst[p]=dc; return; }
 const float3 luma = float3(0.2126,0.7152,0.0722);
 float yo = dot(o,luma), yd = dot(d,luma);
 float y = lerp(yo, yd, detail);
 float3 dScaled = d * (y / max(yd, 1e-5));
 float3 oScaled = o * (y / max(yo, 1e-5));
 float3 outc = lerp(oScaled, dScaled, colour);
 dst[p] = float4(clamp(outc, -65504, 65504), dc.a);
}
)";
class DetailColourMix {
 using Res = Microsoft::WRL::ComPtr<ID3D12Resource>;
 static constexpr UINT kRegions = 5;      // one descriptor region per in-flight NR frame
 static constexpr UINT kPerRegion = 3;    // t0 den, t1 base, u0 out
 Microsoft::WRL::ComPtr<ID3D12Device> device;
 Microsoft::WRL::ComPtr<ID3D12RootSignature> root;
 Microsoft::WRL::ComPtr<ID3D12PipelineState> pipeline;
 Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> heap;
 Res baseline; // pre-denoise copy (matches the denoise buffer's desc)
 Res output;   // mixed result
 UINT width = 0, height = 0, region = 0, stride = 0;
 bool captured = false;
 static void Check(HRESULT hr) { if (FAILED(hr)) throw std::runtime_error("AMD mix D3D12 error " + std::to_string((UINT)hr)); }
 static void Barrier(ID3D12GraphicsCommandList* c, ID3D12Resource* r, D3D12_RESOURCE_STATES a, D3D12_RESOURCE_STATES b) {
  if (a == b || !r) return;
  D3D12_RESOURCE_BARRIER v {}; v.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  v.Transition = { r, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, a, b }; c->ResourceBarrier(1, &v);
 }
 public:
 DetailColourMix(ID3D12Device* d) : device(d) {
  D3D12_DESCRIPTOR_RANGE ranges[2] = { { D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 0, 0, 0 },
                                       { D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0, 2 } };
  D3D12_ROOT_PARAMETER params[2] {};
  params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; params[0].DescriptorTable = { 2, ranges };
  params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS; params[1].Constants = { 0, 0, 4 };
  D3D12_ROOT_SIGNATURE_DESC rd {}; rd.NumParameters = 2; rd.pParameters = params;
  Microsoft::WRL::ComPtr<ID3DBlob> b, e;
  Check(D3D12SerializeRootSignature(&rd, D3D_ROOT_SIGNATURE_VERSION_1, &b, &e));
  Check(d->CreateRootSignature(0, b->GetBufferPointer(), b->GetBufferSize(), IID_PPV_ARGS(&root)));
  Check(DlssNr::SysCompiler::Compile(DetailColourShader, sizeof(DetailColourShader), "AMD detail/colour mix", nullptr,
                   nullptr, "main", "cs_5_0", 0, 0, &b, &e));
  D3D12_COMPUTE_PIPELINE_STATE_DESC pd {}; pd.pRootSignature = root.Get(); pd.CS = { b->GetBufferPointer(), b->GetBufferSize() };
  Check(d->CreateComputePipelineState(&pd, IID_PPV_ARGS(&pipeline)));
  D3D12_DESCRIPTOR_HEAP_DESC hd {}; hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV; hd.NumDescriptors = kRegions * kPerRegion;
  hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  Check(d->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&heap)));
  stride = d->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
 }
 // Copy the pre-denoise colour so Apply can blend against it. `src` is the buffer
 // the model is about to overwrite in place; `srcState` is restored.
 void Capture(ID3D12GraphicsCommandList* c, ID3D12Resource* src, D3D12_RESOURCE_STATES srcState, UINT w, UINT h) {
  if (!c || !src || !w || !h) { captured = false; return; }
  const auto sd = src->GetDesc();
  if (!baseline || width != w || height != h) {
   D3D12_HEAP_PROPERTIES hp {}; hp.Type = D3D12_HEAP_TYPE_DEFAULT;
   D3D12_RESOURCE_DESC rd = sd; rd.Flags = D3D12_RESOURCE_FLAG_NONE;
   baseline.Reset();
   Check(device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
       D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&baseline)));
   D3D12_RESOURCE_DESC od {}; od.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; od.Width = w; od.Height = h;
   od.DepthOrArraySize = 1; od.MipLevels = 1; od.Format = DXGI_FORMAT_R16G16B16A16_FLOAT; od.SampleDesc.Count = 1;
   od.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS; output.Reset();
   Check(device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &od,
       D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, nullptr, IID_PPV_ARGS(&output)));
   width = w; height = h;
  } else {
   Barrier(c, baseline.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
  }
  Barrier(c, src, srcState, D3D12_RESOURCE_STATE_COPY_SOURCE);
  c->CopyResource(baseline.Get(), src);
  Barrier(c, src, D3D12_RESOURCE_STATE_COPY_SOURCE, srcState);
  Barrier(c, baseline.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  captured = true;
 }
 // Blend the denoised colour back toward the captured baseline. Returns the mixed
 // buffer, or `denoised` unchanged when inert. `denoisedState` is restored.
 ID3D12Resource* Apply(ID3D12GraphicsCommandList* c, ID3D12Resource* denoised, D3D12_RESOURCE_STATES denoisedState,
                       UINT w, UINT h, float detail, float colour) {
  if (!c || !denoised || !captured || width != w || height != h) return denoised;
  if (detail >= 1.f && colour >= 1.f) return denoised;
  const UINT base = region * kPerRegion;
  region = (region + 1) % kRegions;
  auto cpu = heap->GetCPUDescriptorHandleForHeapStart();
  cpu.ptr += static_cast<SIZE_T>(base) * stride;
  auto addSrv = [&](ID3D12Resource* r) {
   D3D12_SHADER_RESOURCE_VIEW_DESC s {}; s.Format = DXGI_FORMAT_R16G16B16A16_FLOAT; s.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
   s.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; s.Texture2D.MipLevels = 1;
   device->CreateShaderResourceView(r, &s, cpu); cpu.ptr += stride;
  };
  addSrv(denoised);
  addSrv(baseline.Get());
  { D3D12_UNORDERED_ACCESS_VIEW_DESC u {}; u.Format = DXGI_FORMAT_R16G16B16A16_FLOAT; u.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    device->CreateUnorderedAccessView(output.Get(), nullptr, &u, cpu); }
  Barrier(c, denoised, denoisedState, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  Barrier(c, output.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  auto hh = heap.Get(); c->SetDescriptorHeaps(1, &hh);
  c->SetComputeRootSignature(root.Get()); c->SetPipelineState(pipeline.Get());
  auto gpu = heap->GetGPUDescriptorHandleForHeapStart(); gpu.ptr += static_cast<UINT64>(base) * stride;
  c->SetComputeRootDescriptorTable(0, gpu);
  struct { UINT w, h; float detail, colour; } cb { w, h, std::clamp(detail, 0.f, 1.f), std::clamp(colour, 0.f, 1.f) };
  c->SetComputeRoot32BitConstants(1, 4, &cb, 0);
  c->Dispatch((w + 7) / 8, (h + 7) / 8, 1);
  Barrier(c, output.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  Barrier(c, denoised, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, denoisedState);
  return output.Get();
 }
};
}
