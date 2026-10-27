// Copyright (c) 2026 3zwr1 (AMDNR). Part of AMDNR (GPL-3.0; see Licenses/AMDNR_NOTICE.txt).
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// COM identity for D3D12 objects seen through a wrapper. Shared (0.3.3.2) by the lmxxf host - a queue or
// command list that reaches ExecuteCommandLists under another pointer (LmxxfBackend.cpp, where this
// class lived until now) - and the bridge's device gate (AmdBridge.cpp, Run), so the two agree on what
// "the same object" means. Two proxies are known: Streamline's interposer and ReShade. The lmxxf
// runtime's own contracts unwrap ReShade (runtime/native_device_identity.h, used here as it is).
// (0.3.4, P1) Also the device danielblnc's runtime is built on (ChooseNrDevice) and the queue its Submitting keeps
// (SameQueueObject); tests\034\com_identity_test.cpp checks them with fake COM objects and on WARP.
#include <d3d12.h>
#include <unknwn.h>
#include <dlssnr/lmxxf/runtime/native_device_identity.h>

namespace DlssNr
{
// The object's own IUnknown and, for a Streamline proxy, the IUnknown of the object it wraps (Streamline
// answers {ADEC44E2-...} with its base interface; a native object answers E_NOINTERFACE). One reference
// each, released with the object.
class ComIdentity
{
    IUnknown* self = nullptr;
    IUnknown* base = nullptr;

  public:
    explicit ComIdentity(IUnknown* o)
    {
        static constexpr GUID kStreamlineBase = { 0xADEC44E2, 0x61F0, 0x45C3, { 0xAD, 0x9F, 0x1B, 0x37, 0x37, 0x92, 0x84, 0xFF } };
        if (!o)
            return;
        if (FAILED(o->QueryInterface(IID_IUnknown, reinterpret_cast<void**>(&self))))
            self = nullptr;
        IUnknown* wrapped = nullptr;
        const HRESULT hr = o->QueryInterface(kStreamlineBase, reinterpret_cast<void**>(&wrapped));
        if (hr == S_OK && wrapped && FAILED(wrapped->QueryInterface(IID_IUnknown, reinterpret_cast<void**>(&base))))
            base = nullptr;
        if (wrapped)
            wrapped->Release();
    }
    ~ComIdentity()
    {
        if (base) base->Release();
        if (self) self->Release();
    }
    ComIdentity(const ComIdentity&) = delete;
    ComIdentity& operator=(const ComIdentity&) = delete;
    // One object: the same IUnknown, or the same object behind a proxy (or behind two).
    bool Same(const ComIdentity& o) const
    {
        if (self && self == o.self)
            return true;
        IUnknown* const a = base ? base : self;
        IUnknown* const b = o.base ? o.base : o.self;
        return a && a == b;
    }
    IUnknown* Self() const { return self; }
    IUnknown* Key() const { return base ? base : self; } // the object behind a proxy, else the object
};
inline bool SameComObject(IUnknown* a, IUnknown* b)
{
    if (a == b)
        return true;
    if (!a || !b)
        return false;
    return ComIdentity(a).Same(ComIdentity(b));
}
// One D3D12 device, however it is wrapped. The pointers first (the every-frame case costs nothing
// more), then the adapter - two devices on different adapters are never one - and only then COM
// identity: ReShade's unwrap as the lmxxf runtime's contracts apply it, or Streamline's base object.
// An adapter LUID alone never proves it (a game can make two devices on one adapter).
inline bool SameD3D12Device(ID3D12Device* a, ID3D12Device* b)
{
    if (a == b)
        return true;
    if (!a || !b)
        return false;
    const LUID la = a->GetAdapterLuid(), lb = b->GetAdapterLuid();
    if (la.LowPart != lb.LowPart || la.HighPart != lb.HighPart)
        return false;
    return NativeSameDevice(a, b) || SameComObject(a, b);
}

// (0.3.4, P1 Marvel's Midnight Suns, [DlssNr] AmdStreamlineDeviceFix) WHICH DEVICE danielblnc's runtime is built on.
// The runtime creates its own command lists on the device it is handed and executes them on the queue it is handed,
// so the two must be one identity family: a list made through a proxy device is a proxy object, and a native queue's
// ExecuteCommandLists reads it as its own. Midnight Suns (UE 4.26) creates its device through Streamline 1's
// interposer; the bridge took the device from the game's command list (the proxy) and the queue from the swapchain
// (OptiScaler stores it unwrapped, DxgiFactory_Hooks.cpp), and the runtime's first own submission faulted in
// D3D12Core. ChooseNrDevice compares the frame's device with the device of the queue NR is built with.
enum class NrDevicePairing
{
    SameDevice,         // one device: native games, ReShade (device and queue both its proxies), the D3D11 / Vulkan bridges
    QueueDeviceUnknown, // the queue did not name its device: as 0.3.3.2
    OtherAdapter,       // another adapter ([DlssNr] AmdDeviceGate stops this before it is asked): as 0.3.3.2
    QueueWrapsFrame,    // the queue's device is a wrapper of the frame's device: as 0.3.3.2
    ProxyByGuid,        // the frame's device is a Streamline proxy of the queue's device: it answers {ADEC44E2-...}
    ProxyByFence,       // the same without the GUID: a fence the frame's device creates belongs to the queue's device
    NotProven,          // two devices on one adapter, neither shown to wrap the other: danielblnc cannot run on them
};
struct NrDeviceChoice
{
    NrDevicePairing pairing = NrDevicePairing::SameDevice;
    ID3D12Device* device = nullptr; // ProxyByGuid / ProxyByFence: the queue's device, one reference for the caller
};
inline const char* NrDevicePairingName(NrDevicePairing p)
{
    switch (p)
    {
    case NrDevicePairing::SameDevice: return "same device";
    case NrDevicePairing::QueueDeviceUnknown: return "queue device unknown";
    case NrDevicePairing::OtherAdapter: return "another adapter";
    case NrDevicePairing::QueueWrapsFrame: return "the queue's device wraps the frame's";
    case NrDevicePairing::ProxyByGuid: return "Streamline proxy (GUID proof)";
    case NrDevicePairing::ProxyByFence: return "proxy (fence proof)";
    case NrDevicePairing::NotProven: return "two devices, no proof";
    }
    return "?";
}
// The same COM object by its own IUnknown, no proxy unwrapped (a wrapper answers IUnknown with itself).
inline bool SameIUnknown(IUnknown* a, IUnknown* b)
{
    if (a == b)
        return true;
    if (!a || !b)
        return false;
    IUnknown *x = nullptr, *y = nullptr;
    const bool same = SUCCEEDED(a->QueryInterface(IID_PPV_ARGS(&x))) && SUCCEEDED(b->QueryInterface(IID_PPV_ARGS(&y))) &&
                      x && x == y;
    if (x)
        x->Release();
    if (y)
        y->Release();
    return same;
}
// Whether a fence `maker` creates reports `owner` as its device. Streamline and ReShade wrap devices, queues and
// command lists but not fences, so a proxy device's fence belongs to the device behind it. One fence, released at once.
inline bool FenceBelongsTo(ID3D12Device* maker, ID3D12Device* owner)
{
    if (!maker || !owner)
        return false;
    ID3D12Fence* fence = nullptr;
    if (FAILED(maker->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence))) || !fence)
    {
        if (fence)
            fence->Release();
        return false;
    }
    ID3D12Device* made = nullptr;
    const bool same = SUCCEEDED(fence->GetDevice(IID_PPV_ARGS(&made))) && made && SameIUnknown(made, owner);
    if (made)
        made->Release();
    fence->Release();
    return same;
}
// The device NR's runtime is to be built on, asked once when the backend is built (not per frame: the fence probe
// creates a fence). The pointers first, then the adapter, then COM identity (Streamline's base object), and only then
// the fence probe in both directions. An adapter LUID alone never proves anything.
inline NrDeviceChoice ChooseNrDevice(ID3D12Device* frameDevice, ID3D12CommandQueue* queue)
{
    NrDeviceChoice c;
    ID3D12Device* qd = nullptr;
    if (!frameDevice || !queue || FAILED(queue->GetDevice(IID_PPV_ARGS(&qd))) || !qd)
    {
        if (qd)
            qd->Release();
        c.pairing = NrDevicePairing::QueueDeviceUnknown;
        return c;
    }
    const auto answer = [&](NrDevicePairing p) {
        c.pairing = p;
        if (p == NrDevicePairing::ProxyByGuid || p == NrDevicePairing::ProxyByFence)
            c.device = qd; // the caller's reference
        else
            qd->Release();
        return c;
    };
    if (SameIUnknown(frameDevice, qd))
        return answer(NrDevicePairing::SameDevice);
    const LUID a = frameDevice->GetAdapterLuid(), b = qd->GetAdapterLuid();
    if (a.LowPart != b.LowPart || a.HighPart != b.HighPart)
        return answer(NrDevicePairing::OtherAdapter);
    {
        const ComIdentity f(frameDevice), d(qd);
        if (f.Key() && f.Key() == d.Key())
            // One object behind both. A frame device with a Streamline base is the proxy: NR takes the queue's device.
            // A frame device without one is the object the queue's device wraps.
            return answer(f.Key() != f.Self() ? NrDevicePairing::ProxyByGuid : NrDevicePairing::QueueWrapsFrame);
    }
    if (FenceBelongsTo(frameDevice, qd))
        return answer(NrDevicePairing::ProxyByFence);
    if (FenceBelongsTo(qd, frameDevice))
        return answer(NrDevicePairing::QueueWrapsFrame);
    return answer(NrDevicePairing::NotProven);
}
// The device behind a proxy frame device (ProxyByGuid / ProxyByFence), one reference for the caller, else null.
inline ID3D12Device* DeviceBehindProxy(ID3D12Device* frameDevice, ID3D12CommandQueue* q)
{
    return ChooseNrDevice(frameDevice, q).device;
}
// (0.3.4, P1) One D3D12 object behind two pointers, shown without any wrapper GUID: a private-data token set on `bound`
// is read back through `seen` (a wrapper forwards ID3D12Object's private data to the object it wraps). The token is
// removed again at once.
inline bool SharesPrivateData(ID3D12Object* seen, ID3D12Object* bound)
{
    static constexpr GUID kProbe = { 0x5A1D8E3C, 0x2B7F, 0x4C61, { 0x9E, 0x0A, 0x3D, 0x4F, 0x7B, 0x8C, 0x21, 0xA6 } };
    if (!seen || !bound)
        return false;
    const UINT64 token = 0xA3D0'0000'0000'0000ull ^ static_cast<UINT64>(reinterpret_cast<UINT_PTR>(bound));
    if (FAILED(bound->SetPrivateData(kProbe, sizeof token, &token)))
        return false;
    UINT64 read = 0;
    UINT size = sizeof read;
    const bool same = SUCCEEDED(seen->GetPrivateData(kProbe, &size, &read)) && size == sizeof read && read == token;
    bound->SetPrivateData(kProbe, 0, nullptr);
    return same;
}
// One command queue however it is wrapped: the pointers, COM identity (Streamline's base object), then the private
// data probe (a Streamline 1 proxy that does not answer the GUID). danielblnc's Submitting asks it for a backend built
// on the device behind a proxy (AmdPreSr.cpp).
inline bool SameQueueObject(ID3D12CommandQueue* seen, ID3D12CommandQueue* bound)
{
    if (seen == bound)
        return true;
    if (!seen || !bound)
        return false;
    return SameComObject(seen, bound) || SharesPrivateData(seen, bound);
}
// (0.3.4, P1 review) The command queue a Streamline proxy queue wraps (it answers {ADEC44E2-...} with its base object),
// one reference for the caller; null for a queue that does not answer (a native queue, a ReShade proxy of one, a proxy
// without the GUID) or whose base is not a command queue. danielblnc's Submitting binds it when the game really moves to
// another queue in a backend built on the device behind a proxy, so the runtime's own native lists keep going to a
// native queue (AmdPreSr.cpp).
inline ID3D12CommandQueue* QueueBehindProxy(ID3D12CommandQueue* q)
{
    static constexpr GUID kStreamlineBase = { 0xADEC44E2, 0x61F0, 0x45C3, { 0xAD, 0x9F, 0x1B, 0x37, 0x37, 0x92, 0x84, 0xFF } };
    if (!q)
        return nullptr;
    IUnknown* base = nullptr;
    ID3D12CommandQueue* behind = nullptr;
    if (q->QueryInterface(kStreamlineBase, reinterpret_cast<void**>(&base)) == S_OK && base &&
        FAILED(base->QueryInterface(IID_PPV_ARGS(&behind))))
        behind = nullptr;
    if (base)
        base->Release();
    if (behind && SameIUnknown(behind, q)) // not a proxy after all: nothing behind it
    {
        behind->Release();
        behind = nullptr;
    }
    return behind;
}
} // namespace DlssNr
