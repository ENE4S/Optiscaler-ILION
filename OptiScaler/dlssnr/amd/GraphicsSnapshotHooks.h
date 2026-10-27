// Copyright (c) 2026 3zwr1 (AMDNR). Part of AMDNR (GPL-3.0; see Licenses/AMDNR_NOTICE.txt).
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Wiring for GraphicsSnapshot.h.
//
// GraphicsSnapshot.h is pure CPU bookkeeping - it models what a command list has bound
// and decides whether the graphics wait may be used on it. It had no callers at all, so
// none of that judgement was reachable and the graphics wait shipped disabled with the
// note "state restoration is incomplete". This file is the missing half: it observes the
// real D3D12 calls, keeps one ListTracker per command list, and replays what the wait
// destroys.
//
// WHY A SEPARATE TRACKER FROM D3D12_Hooks.cpp. That one already records the things a
// COMPUTE dispatch disturbs - root signatures, root arguments, the PSO, descriptor heaps
// - and ScopedNrStateEnvelope restores exactly those. The graphics wait issues real
// draws, so it also disturbs the rasteriser and output-merger state, which nothing was
// recording. Rather than widen a tracker the whole upscaler depends on, this adds the
// missing half beside it. The division is clean and stated in one line:
//
//     ScopedNrStateEnvelope -> root signatures, root arguments, PSO, descriptor heaps
//     this file             -> viewports, scissors, topology, render targets
//
// WHY ADMISSION EXISTS. Restoring state requires knowing what it was, and a vtable hook
// installed partway through a frame has not seen the calls that came before it. Every
// field therefore starts Unknown, and CanAdmitGraphics refuses on Unknown rather than
// guessing a default - guessing would restore a WRONG viewport, which is worse than not
// using the wait, because the game would then draw somewhere unexpected with no error
// anywhere to explain it. The feature turns itself on only for lists it has watched from
// a Reset onward, and reports why when it does not.
//
// NOT INSTALLED IN THIS BUILD. Admission also needs the graphics root signature known, and
// nothing here watches SetGraphicsRootSignature or SetPipelineState - SetSignature in
// GraphicsSnapshot.h has no caller outside the unit test - so CanAdmitGraphics refused
// every list with graphics_root_unknown (no ADMITTED line in any tester log), while Install
// kept twelve detours and a global lock on every hooked call from every thread. AmdPreSr.cpp
// therefore calls neither Install nor Admit (kGraphicsAdmissionPossible there). The code
// stays for the full tracker the wait would need; it is not reachable until that exists.
#include "GraphicsSnapshot.h"

#include <d3d12.h>
#include <detours/detours.h>

#include <mutex>
#include <shared_mutex>
#include <unordered_map>

namespace AmdPreSr::GraphicsSnap
{

// ---------------------------------------------------------------------------------
// Vtable slots for ID3D12GraphicsCommandList. The same numbering D3D12_Hooks.cpp uses
// (SetPipelineState 25, SetDescriptorHeaps 28, SetComputeRootSignature 29 ...), which is
// what these were checked against - they are not independently guessed.
// ---------------------------------------------------------------------------------
enum : unsigned
{
    kSlotClose = 9,
    kSlotReset = 10,
    kSlotClearState = 11,
    kSlotIASetPrimitiveTopology = 20,
    kSlotRSSetViewports = 21,
    kSlotRSSetScissorRects = 22,
    kSlotExecuteBundle = 27,
    kSlotOMSetRenderTargets = 46,
    kSlotBeginQuery = 52,
    kSlotEndQuery = 53,
    kSlotSetPredication = 55,
    kSlotExecuteIndirect = 59,
};

using PFN_Reset = HRESULT(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, ID3D12CommandAllocator*,
                                              ID3D12PipelineState*);
using PFN_Close = HRESULT(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*);
using PFN_ClearState = void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, ID3D12PipelineState*);
using PFN_IASetPrimitiveTopology = void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, D3D12_PRIMITIVE_TOPOLOGY);
using PFN_RSSetViewports = void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, UINT, const D3D12_VIEWPORT*);
using PFN_RSSetScissorRects = void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, UINT, const D3D12_RECT*);
using PFN_OMSetRenderTargets = void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, UINT,
                                                        const D3D12_CPU_DESCRIPTOR_HANDLE*, BOOL,
                                                        const D3D12_CPU_DESCRIPTOR_HANDLE*);
using PFN_ExecuteBundle = void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, ID3D12GraphicsCommandList*);
using PFN_ExecuteIndirect = void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, ID3D12CommandSignature*, UINT,
                                                     ID3D12Resource*, UINT64, ID3D12Resource*, UINT64);
using PFN_BeginQuery = void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, ID3D12QueryHeap*, D3D12_QUERY_TYPE, UINT);
using PFN_EndQuery = void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, ID3D12QueryHeap*, D3D12_QUERY_TYPE, UINT);
using PFN_SetPredication = void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, ID3D12Resource*, UINT64,
                                                    D3D12_PREDICATION_OP);

struct Originals
{
    PFN_Reset Reset = nullptr;
    PFN_Close Close = nullptr;
    PFN_ClearState ClearState = nullptr;
    PFN_IASetPrimitiveTopology IASetPrimitiveTopology = nullptr;
    PFN_RSSetViewports RSSetViewports = nullptr;
    PFN_RSSetScissorRects RSSetScissorRects = nullptr;
    PFN_OMSetRenderTargets OMSetRenderTargets = nullptr;
    PFN_ExecuteBundle ExecuteBundle = nullptr;
    PFN_ExecuteIndirect ExecuteIndirect = nullptr;
    PFN_BeginQuery BeginQuery = nullptr;
    PFN_EndQuery EndQuery = nullptr;
    PFN_SetPredication SetPredication = nullptr;
};

inline Originals& Orig()
{
    static Originals o;
    return o;
}

inline std::shared_mutex& MapMutex()
{
    static std::shared_mutex m;
    return m;
}

inline std::unordered_map<ID3D12GraphicsCommandList*, ListTracker>& Trackers()
{
    static std::unordered_map<ID3D12GraphicsCommandList*, ListTracker> t;
    return t;
}

// Set while this module replays state, so the replay is not mistaken for the game
// setting that state. Same device D3D12_Hooks.cpp uses for its own restores.
inline thread_local bool g_replaying = false;

// The tracker for a list, creating it on first sight. A list first seen mid-frame keeps
// every field Unknown, which is exactly what makes CanAdmitGraphics refuse it.
inline ListTracker& TrackerFor(ID3D12GraphicsCommandList* cmd)
{
    auto& map = Trackers();
    auto it = map.find(cmd);
    if (it == map.end())
    {
        it = map.emplace(cmd, ListTracker {}).first;
        it->second.listId = reinterpret_cast<std::uint64_t>(cmd);
        it->second.live = true;
        // generationKnown stays false: we have NOT seen this list from a Reset, so we do
        // not know what was bound before the hook existed.
    }
    return it->second;
}

// ---------------------------------------------------------------------------------
// Hooks. Each records, then calls through. None of them changes what the game does.
// ---------------------------------------------------------------------------------

inline HRESULT STDMETHODCALLTYPE hkReset(ID3D12GraphicsCommandList* cmd, ID3D12CommandAllocator* alloc,
                                         ID3D12PipelineState* pso)
{
    const HRESULT hr = Orig().Reset(cmd, alloc, pso);
    if (cmd)
    {
        std::unique_lock lock(MapMutex());
        auto& t = TrackerFor(cmd);
        // A successful Reset is the ONLY point at which the state is known from first
        // principles: D3D12 defines it as "everything back to default". From here the
        // hook has seen every call, so the snapshot can be trusted and the list becomes
        // eligible. A failed Reset changes nothing, including that.
        if (SUCCEEDED(hr))
        {
            t.live = true;
            t.OnReset(true);
            t.generationKnown = true;
            // Reset's own pipeline-state argument is a real PSO bind.
            if (pso)
                t.snap.SetPso(reinterpret_cast<std::uint64_t>(pso));
            // Default predication is "none", and that is knowledge, not a guess.
            t.snap.predication.state = BindState::KnownUnset;
        }
    }
    return hr;
}

inline HRESULT STDMETHODCALLTYPE hkClose(ID3D12GraphicsCommandList* cmd)
{
    if (cmd)
    {
        std::unique_lock lock(MapMutex());
        auto it = Trackers().find(cmd);
        // A closed list cannot be recorded into, so nothing may be admitted on it until
        // the next Reset says the state is known again.
        if (it != Trackers().end())
            it->second.generationKnown = false;
    }
    return Orig().Close(cmd);
}

inline void STDMETHODCALLTYPE hkClearState(ID3D12GraphicsCommandList* cmd, ID3D12PipelineState* pso)
{
    Orig().ClearState(cmd, pso);
    if (!cmd)
        return;
    std::unique_lock lock(MapMutex());
    auto& t = TrackerFor(cmd);
    // ClearState is not Reset: bindings go to default but an open render pass or query
    // stays open. ListTracker::OnClearState encodes that difference.
    t.OnClearState();
    if (pso)
        t.snap.SetPso(reinterpret_cast<std::uint64_t>(pso));
}

inline void STDMETHODCALLTYPE hkIASetPrimitiveTopology(ID3D12GraphicsCommandList* cmd, D3D12_PRIMITIVE_TOPOLOGY topo)
{
    Orig().IASetPrimitiveTopology(cmd, topo);
    if (!cmd || g_replaying)
        return;
    std::unique_lock lock(MapMutex());
    TrackerFor(cmd).snap.SetTopology(static_cast<std::uint32_t>(topo));
}

inline void STDMETHODCALLTYPE hkRSSetViewports(ID3D12GraphicsCommandList* cmd, UINT count, const D3D12_VIEWPORT* vp)
{
    Orig().RSSetViewports(cmd, count, vp);
    if (!cmd || g_replaying)
        return;
    Viewport local[kMaxViewports] {};
    const UINT n = count > kMaxViewports ? 0u : count;
    for (UINT i = 0; i < n && vp; ++i)
        local[i] = Viewport { vp[i].TopLeftX, vp[i].TopLeftY, vp[i].Width,
                              vp[i].Height,   vp[i].MinDepth, vp[i].MaxDepth };
    std::unique_lock lock(MapMutex());
    TrackerFor(cmd).snap.SetViewports(vp && n ? local : nullptr, n);
}

inline void STDMETHODCALLTYPE hkRSSetScissorRects(ID3D12GraphicsCommandList* cmd, UINT count, const D3D12_RECT* r)
{
    Orig().RSSetScissorRects(cmd, count, r);
    if (!cmd || g_replaying)
        return;
    ScissorRect local[kMaxScissors] {};
    const UINT n = count > kMaxScissors ? 0u : count;
    for (UINT i = 0; i < n && r; ++i)
        local[i] = ScissorRect { r[i].left, r[i].top, r[i].right, r[i].bottom };
    std::unique_lock lock(MapMutex());
    TrackerFor(cmd).snap.SetScissors(r && n ? local : nullptr, n);
}

inline void STDMETHODCALLTYPE hkOMSetRenderTargets(ID3D12GraphicsCommandList* cmd, UINT numRTVs,
                                                   const D3D12_CPU_DESCRIPTOR_HANDLE* rtvs, BOOL singleRange,
                                                   const D3D12_CPU_DESCRIPTOR_HANDLE* dsv)
{
    Orig().OMSetRenderTargets(cmd, numRTVs, rtvs, singleRange, dsv);
    if (!cmd || g_replaying)
        return;
    std::uint64_t handles[kMaxRTVs] {};
    const UINT n = numRTVs > kMaxRTVs ? 0u : numRTVs;
    if (rtvs && n)
    {
        // With a single handle range only the first handle is meaningful; the rest are
        // derived at restore time from the device increment, exactly as D3D12 does.
        const UINT copies = singleRange ? 1u : n;
        for (UINT i = 0; i < copies; ++i)
            handles[i] = static_cast<std::uint64_t>(rtvs[i].ptr);
    }
    std::unique_lock lock(MapMutex());
    // (0, nullptr, FALSE, nullptr) is a genuine unbind, and SetRenderTargets records it
    // as KnownUnset rather than Unknown - the distinction that lets an empty binding be
    // restored as empty instead of blocking admission.
    TrackerFor(cmd).snap.SetRenderTargets(numRTVs > kMaxRTVs ? numRTVs : n, rtvs ? handles : nullptr,
                                          singleRange != FALSE, dsv != nullptr,
                                          dsv ? static_cast<std::uint64_t>(dsv->ptr) : 0ull);
}

inline void STDMETHODCALLTYPE hkSetPredication(ID3D12GraphicsCommandList* cmd, ID3D12Resource* buffer, UINT64 offset,
                                               D3D12_PREDICATION_OP op)
{
    Orig().SetPredication(cmd, buffer, offset, op);
    if (!cmd || g_replaying)
        return;
    std::unique_lock lock(MapMutex());
    TrackerFor(cmd).snap.SetPredication(reinterpret_cast<std::uint64_t>(buffer), offset,
                                        static_cast<std::uint32_t>(op));
}

// The three things that make a list permanently unsafe for this frame. A bundle or an
// indirect execute can bind anything without the hook seeing it, and a query open across
// our draws would record our work into the game's timings.
inline void STDMETHODCALLTYPE hkExecuteBundle(ID3D12GraphicsCommandList* cmd, ID3D12GraphicsCommandList* bundle)
{
    if (cmd)
    {
        std::unique_lock lock(MapMutex());
        auto& t = TrackerFor(cmd);
        t.snap.bundleOrIndirectSeen = true;
        t.MarkIneligible();
    }
    Orig().ExecuteBundle(cmd, bundle);
}

inline void STDMETHODCALLTYPE hkExecuteIndirect(ID3D12GraphicsCommandList* cmd, ID3D12CommandSignature* sig,
                                                UINT maxCount, ID3D12Resource* args, UINT64 argOffset,
                                                ID3D12Resource* count, UINT64 countOffset)
{
    if (cmd)
    {
        std::unique_lock lock(MapMutex());
        auto& t = TrackerFor(cmd);
        t.snap.bundleOrIndirectSeen = true;
        t.MarkIneligible();
    }
    Orig().ExecuteIndirect(cmd, sig, maxCount, args, argOffset, count, countOffset);
}

inline void STDMETHODCALLTYPE hkBeginQuery(ID3D12GraphicsCommandList* cmd, ID3D12QueryHeap* heap, D3D12_QUERY_TYPE type,
                                           UINT index)
{
    if (cmd)
    {
        std::unique_lock lock(MapMutex());
        TrackerFor(cmd).snap.queryActive = true;
    }
    Orig().BeginQuery(cmd, heap, type, index);
}

inline void STDMETHODCALLTYPE hkEndQuery(ID3D12GraphicsCommandList* cmd, ID3D12QueryHeap* heap, D3D12_QUERY_TYPE type,
                                         UINT index)
{
    Orig().EndQuery(cmd, heap, type, index);
    if (cmd)
    {
        std::unique_lock lock(MapMutex());
        TrackerFor(cmd).snap.queryActive = false;
    }
}

// ---------------------------------------------------------------------------------
// Installation. One transaction, once per process - the vtable entries are shared by
// every command list of this type, which is the same reason D3D12_Hooks.cpp only needs
// one list to hook them all.
// ---------------------------------------------------------------------------------
inline bool& Installed()
{
    static bool v = false;
    return v;
}

inline bool Install(ID3D12GraphicsCommandList* cmd)
{
    static std::mutex once;
    std::lock_guard guard(once);
    if (Installed())
        return true;
    if (!cmd)
        return false;

    void** vt = *reinterpret_cast<void***>(cmd);
    auto& o = Orig();
    o.Reset = reinterpret_cast<PFN_Reset>(vt[kSlotReset]);
    o.Close = reinterpret_cast<PFN_Close>(vt[kSlotClose]);
    o.ClearState = reinterpret_cast<PFN_ClearState>(vt[kSlotClearState]);
    o.IASetPrimitiveTopology = reinterpret_cast<PFN_IASetPrimitiveTopology>(vt[kSlotIASetPrimitiveTopology]);
    o.RSSetViewports = reinterpret_cast<PFN_RSSetViewports>(vt[kSlotRSSetViewports]);
    o.RSSetScissorRects = reinterpret_cast<PFN_RSSetScissorRects>(vt[kSlotRSSetScissorRects]);
    o.OMSetRenderTargets = reinterpret_cast<PFN_OMSetRenderTargets>(vt[kSlotOMSetRenderTargets]);
    o.ExecuteBundle = reinterpret_cast<PFN_ExecuteBundle>(vt[kSlotExecuteBundle]);
    o.ExecuteIndirect = reinterpret_cast<PFN_ExecuteIndirect>(vt[kSlotExecuteIndirect]);
    o.BeginQuery = reinterpret_cast<PFN_BeginQuery>(vt[kSlotBeginQuery]);
    o.EndQuery = reinterpret_cast<PFN_EndQuery>(vt[kSlotEndQuery]);
    o.SetPredication = reinterpret_cast<PFN_SetPredication>(vt[kSlotSetPredication]);

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourAttach(&(PVOID&) o.Reset, hkReset);
    DetourAttach(&(PVOID&) o.Close, hkClose);
    DetourAttach(&(PVOID&) o.ClearState, hkClearState);
    DetourAttach(&(PVOID&) o.IASetPrimitiveTopology, hkIASetPrimitiveTopology);
    DetourAttach(&(PVOID&) o.RSSetViewports, hkRSSetViewports);
    DetourAttach(&(PVOID&) o.RSSetScissorRects, hkRSSetScissorRects);
    DetourAttach(&(PVOID&) o.OMSetRenderTargets, hkOMSetRenderTargets);
    DetourAttach(&(PVOID&) o.ExecuteBundle, hkExecuteBundle);
    DetourAttach(&(PVOID&) o.ExecuteIndirect, hkExecuteIndirect);
    DetourAttach(&(PVOID&) o.BeginQuery, hkBeginQuery);
    DetourAttach(&(PVOID&) o.EndQuery, hkEndQuery);
    DetourAttach(&(PVOID&) o.SetPredication, hkSetPredication);
    const LONG rc = DetourTransactionCommit();
    Installed() = rc == NO_ERROR;
    return Installed();
}

// ---------------------------------------------------------------------------------
// The two calls the backend makes.
// ---------------------------------------------------------------------------------

// Freeze the candidate and say whether the graphics wait may run on it. `reason` is set
// either way, so a refusal is always explainable in the log rather than silent.
inline bool Admit(ID3D12GraphicsCommandList* cmd, GraphicsSnapshot& out, const char*& reason)
{
    if (!Installed())
    {
        reason = "hooks_not_installed";
        return false;
    }
    if (!cmd)
    {
        reason = "no_command_list";
        return false;
    }
    std::shared_lock lock(MapMutex());
    auto it = Trackers().find(cmd);
    if (it == Trackers().end())
    {
        reason = "list_never_seen";
        return false;
    }
    const auto verdict = CanAdmitGraphics(it->second);
    reason = verdict.reason;
    if (!verdict.ok)
        return false;
    return TryFreeze(it->second, out);
}

// Replay the rasteriser and output-merger state the wait's draws overwrote. Root
// signatures, root arguments, the PSO and the descriptor heaps are NOT touched here -
// ScopedNrStateEnvelope owns those, and restoring them twice would be a second source of
// truth for the same state.
inline void Restore(ID3D12GraphicsCommandList* cmd, ID3D12Device* device, const GraphicsSnapshot& s)
{
    if (!cmd || !Installed())
        return;
    g_replaying = true;

    if (s.topologyState == BindState::KnownValue)
        Orig().IASetPrimitiveTopology(cmd, static_cast<D3D12_PRIMITIVE_TOPOLOGY>(s.topology));

    if (s.viewportState == BindState::KnownValue && s.viewportCount)
    {
        D3D12_VIEWPORT vp[kMaxViewports] {};
        for (std::uint32_t i = 0; i < s.viewportCount; ++i)
            vp[i] = { s.viewports[i].topLeftX, s.viewports[i].topLeftY, s.viewports[i].width,
                      s.viewports[i].height,   s.viewports[i].minDepth, s.viewports[i].maxDepth };
        Orig().RSSetViewports(cmd, s.viewportCount, vp);
    }

    if (s.scissorState == BindState::KnownValue && s.scissorCount)
    {
        D3D12_RECT r[kMaxScissors] {};
        for (std::uint32_t i = 0; i < s.scissorCount; ++i)
            r[i] = { s.scissors[i].left, s.scissors[i].top, s.scissors[i].right, s.scissors[i].bottom };
        Orig().RSSetScissorRects(cmd, s.scissorCount, r);
    }

    if (s.om.state == BindState::KnownUnset)
    {
        Orig().OMSetRenderTargets(cmd, 0, nullptr, FALSE, nullptr);
    }
    else if (s.om.state == BindState::KnownValue)
    {
        D3D12_CPU_DESCRIPTOR_HANDLE rtv[kMaxRTVs] {};
        if (s.om.singleHandleRange)
        {
            // One handle plus the device's own RTV increment reproduces the contiguous
            // range D3D12 would walk. Guessing the increment is not an option, which is
            // why the device is a parameter here.
            rtv[0].ptr = static_cast<SIZE_T>(s.om.rtvHandles[0]);
        }
        else
        {
            for (std::uint32_t i = 0; i < s.om.numRTVs; ++i)
                rtv[i].ptr = static_cast<SIZE_T>(s.om.rtvHandles[i]);
        }
        D3D12_CPU_DESCRIPTOR_HANDLE dsv {};
        dsv.ptr = static_cast<SIZE_T>(s.om.dsvHandle);
        Orig().OMSetRenderTargets(cmd, s.om.numRTVs, s.om.numRTVs ? rtv : nullptr,
                                  s.om.singleHandleRange ? TRUE : FALSE, s.om.hasDsv ? &dsv : nullptr);
    }
    (void) device;

    g_replaying = false;
}

} // namespace AmdPreSr::GraphicsSnap
