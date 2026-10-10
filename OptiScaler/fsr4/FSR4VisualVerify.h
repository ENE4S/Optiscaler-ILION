#pragma once

#include "SysUtils.h"
#include "Util.h"
#include "Logger.h"

#include <d3d12.h>
#include <array>
#include <atomic>
#include <mutex>
#include <vector>
#include <intrin.h>

// Experimental, only does something while enabled from the menu.
//
// While an upscale ffxDispatch runs, the command list's vtable pointer is swapped with a copy that has its own
// Dispatch entry. That entry notes which module the call came from (return address) and forwards it unchanged.
// After ffxDispatch returns the original vtable is back and a color marker is copied into the upscaler's output,
// on the same command list. The marker color comes from the module that recorded the compute work, it is never
// taken from what is selected in the menu.
class FSR4VisualVerify
{
  public:
    enum class Marker : int
    {
        None,
        Official,   // blue
        Community,  // green
        Lite,       // magenta
        OtherDll,   // white, verified but not one of the three names
        Unverified, // yellow, top bar only
    };

    enum class DrawStatus : int
    {
        NotRun,
        Drawn,
        DispatchFailed,
        NoCommandList,
        NotDirectList,
        OutputNotUav,
        UnsupportedOutput,
        CreateFailed,
    };

    struct Evidence
    {
        bool enabled = false;
        bool hasFrame = false;
        bool instrumented = false;
        bool verified = false;
        HMODULE execModule = nullptr;
        HMODULE ownerModule = nullptr;
        HMODULE otherModule = nullptr;
        uint32_t computeFromExec = 0;
        uint32_t computeFromOther = 0;
        Marker marker = Marker::None;
        DrawStatus draw = DrawStatus::NotRun;
        uint64_t frames = 0;
        uint64_t verifiedFrames = 0;
        uint64_t msSinceLastFrame = 0;
    };

    static bool Enabled() { return _enabled.load(std::memory_order_relaxed); }

    static void SetEnabled(bool enabled)
    {
        if (_enabled.exchange(enabled, std::memory_order_relaxed) != enabled)
            LOG_INFO("[SR-VISUAL] visual verification {}", enabled ? "enabled" : "disabled");
    }

    static Evidence GetEvidence()
    {
        Evidence evidence;
        evidence.enabled = Enabled();
        evidence.frames = _frames.load(std::memory_order_relaxed);
        evidence.verifiedFrames = _verifiedFrames.load(std::memory_order_relaxed);

        const auto lastTick = _lastTick.load(std::memory_order_relaxed);
        evidence.hasFrame = lastTick != 0;
        evidence.msSinceLastFrame = evidence.hasFrame ? GetTickCount64() - lastTick : 0;

        evidence.instrumented = _lastInstrumented.load(std::memory_order_relaxed);
        evidence.verified = _lastVerified.load(std::memory_order_relaxed);
        evidence.execModule = _lastExecModule.load(std::memory_order_relaxed);
        evidence.ownerModule = _lastOwnerModule.load(std::memory_order_relaxed);
        evidence.otherModule = _lastOtherModule.load(std::memory_order_relaxed);
        evidence.computeFromExec = _lastFromExec.load(std::memory_order_relaxed);
        evidence.computeFromOther = _lastFromOther.load(std::memory_order_relaxed);
        evidence.marker = (Marker) _lastMarker.load(std::memory_order_relaxed);
        evidence.draw = (DrawStatus) _lastDraw.load(std::memory_order_relaxed);

        return evidence;
    }

    // Call right before ffxDispatch, execModule is the module that owns the ffxDispatch that will be called
    static void Begin(void* commandList, HMODULE execModule)
    {
        auto& capture = _capture;

        capture.object = nullptr;
        capture.fromExec = 0;
        capture.fromOther = 0;
        capture.firstOtherCaller = nullptr;
        capture.execBegin = 0;
        capture.execEnd = 0;

        if (commandList == nullptr)
            return;

        if (execModule != nullptr)
        {
            // Module range from its PE header, so calls can be attributed without an API call each
            auto base = (const BYTE*) execModule;
            auto dos = (const IMAGE_DOS_HEADER*) base;
            auto nt = (const IMAGE_NT_HEADERS*) (base + dos->e_lfanew);

            capture.execBegin = (uintptr_t) base;
            capture.execEnd = capture.execBegin + nt->OptionalHeader.SizeOfImage;
        }

        auto object = (void***) commandList;
        void** original = *object;

        if (capture.cachedOriginal != original && !BuildShadow(capture, original))
            return;

        capture.originalVtable = original;
        _fallbackOriginal.store(original, std::memory_order_relaxed);
        capture.object = object;

        *object = &capture.shadow[1];
    }

    // Call right after ffxDispatch returned
    static void End(void* commandList, bool dispatchOk, HMODULE execModule, HMODULE ownerModule,
                    const std::string& execName, ID3D12Resource* output, bool outputIsUav, UINT upscaleWidth,
                    UINT upscaleHeight)
    {
        auto& capture = _capture;
        const bool instrumented = capture.object != nullptr;

        // Original vtable goes back before anything else is done with the command list
        if (instrumented)
        {
            *capture.object = capture.originalVtable;
            capture.object = nullptr;
        }

        HMODULE otherModule = nullptr;

        if (capture.firstOtherCaller != nullptr)
            otherModule = Util::GetCallerModule(capture.firstOtherCaller);

        const bool verified = dispatchOk && instrumented && execModule != nullptr && ownerModule == execModule &&
                              capture.fromExec > 0 && capture.fromOther == 0;

        const Marker marker = verified ? MarkerFromName(execName) : Marker::Unverified;

        DrawStatus draw = DrawStatus::DispatchFailed;

        if (dispatchOk)
            draw = DrawMarker((ID3D12GraphicsCommandList*) commandList, output, outputIsUav, upscaleWidth,
                              upscaleHeight, marker);

        _frames.fetch_add(1, std::memory_order_relaxed);

        if (verified)
            _verifiedFrames.fetch_add(1, std::memory_order_relaxed);

        _lastInstrumented.store(instrumented, std::memory_order_relaxed);
        _lastOwnerModule.store(ownerModule, std::memory_order_relaxed);
        _lastOtherModule.store(otherModule, std::memory_order_relaxed);
        _lastFromExec.store(capture.fromExec, std::memory_order_relaxed);
        _lastFromOther.store(capture.fromOther, std::memory_order_relaxed);
        _lastTick.store(GetTickCount64(), std::memory_order_relaxed);

        const bool verifiedChanged = _lastVerified.exchange(verified, std::memory_order_relaxed) != verified;
        const bool moduleChanged = _lastExecModule.exchange(execModule, std::memory_order_relaxed) != execModule;
        const bool markerChanged = _lastMarker.exchange((int) marker, std::memory_order_relaxed) != (int) marker;
        const bool drawChanged = _lastDraw.exchange((int) draw, std::memory_order_relaxed) != (int) draw;

        // Only when something changes, never per frame
        if (verifiedChanged || moduleChanged || markerChanged || drawChanged)
        {
            LOG_INFO("[SR-VISUAL] {}: dll {} ({:X}), context owner {:X}, compute dispatches {} by the dll and {} by "
                     "others ({:X}), marker: {}, draw: {}",
                     verified ? "VERIFIED" : "NOT VERIFIED", execName, (size_t) execModule, (size_t) ownerModule,
                     capture.fromExec, capture.fromOther, (size_t) otherModule, MarkerName(marker),
                     DrawStatusName(draw));
        }
    }

    static const char* MarkerName(Marker marker)
    {
        switch (marker)
        {
        case Marker::Official:
            return "blue (Official)";
        case Marker::Community:
            return "green (Community)";
        case Marker::Lite:
            return "magenta (Lite)";
        case Marker::OtherDll:
            return "white (other dll)";
        case Marker::Unverified:
            return "yellow, top only (not verified)";
        default:
            return "none";
        }
    }

    static const char* DrawStatusName(DrawStatus status)
    {
        switch (status)
        {
        case DrawStatus::Drawn:
            return "written to the upscaler output";
        case DrawStatus::DispatchFailed:
            return "skipped, dispatch failed";
        case DrawStatus::NoCommandList:
            return "skipped, no command list or output";
        case DrawStatus::NotDirectList:
            return "skipped, not a direct command list";
        case DrawStatus::OutputNotUav:
            return "skipped, output is not in UAV state";
        case DrawStatus::UnsupportedOutput:
            return "skipped, output format isn't supported";
        case DrawStatus::CreateFailed:
            return "skipped, can't create the marker texture";
        default:
            return "not run";
        }
    }

  private:
    typedef void(STDMETHODCALLTYPE* PFN_Dispatch)(ID3D12GraphicsCommandList* This, UINT x, UINT y, UINT z);

    static constexpr size_t DispatchIndex = 14;
    static constexpr size_t MaxVtableEntries = 1024;

    struct Capture
    {
        void*** object = nullptr;
        void** originalVtable = nullptr;
        void** cachedOriginal = nullptr;
        uintptr_t execBegin = 0;
        uintptr_t execEnd = 0;
        uint32_t fromExec = 0;
        uint32_t fromOther = 0;
        void* firstOtherCaller = nullptr;

        // [0] is the entry in front of the vtable (RTTI), the vtable copy starts at [1]
        std::array<void*, MaxVtableEntries + 1> shadow {};
    };

    inline static thread_local Capture _capture;
    inline static std::atomic<void**> _fallbackOriginal { nullptr };

    inline static std::atomic<bool> _enabled { false };
    inline static std::atomic<uint64_t> _frames { 0 };
    inline static std::atomic<uint64_t> _verifiedFrames { 0 };
    inline static std::atomic<uint64_t> _lastTick { 0 };
    inline static std::atomic<bool> _lastInstrumented { false };
    inline static std::atomic<bool> _lastVerified { false };
    inline static std::atomic<HMODULE> _lastExecModule { nullptr };
    inline static std::atomic<HMODULE> _lastOwnerModule { nullptr };
    inline static std::atomic<HMODULE> _lastOtherModule { nullptr };
    inline static std::atomic<uint32_t> _lastFromExec { 0 };
    inline static std::atomic<uint32_t> _lastFromOther { 0 };
    inline static std::atomic<int> _lastMarker { 0 };
    inline static std::atomic<int> _lastDraw { 0 };

    static void STDMETHODCALLTYPE DispatchShim(ID3D12GraphicsCommandList* This, UINT x, UINT y, UINT z)
    {
        auto& capture = _capture;
        void** original = capture.originalVtable;

        if (capture.object != nullptr)
        {
            const auto caller = (uintptr_t) _ReturnAddress();

            if (caller >= capture.execBegin && caller < capture.execEnd)
            {
                capture.fromExec++;
            }
            else
            {
                capture.fromOther++;

                if (capture.firstOtherCaller == nullptr)
                    capture.firstOtherCaller = (void*) caller;
            }
        }
        else
        {
            // Not expected, command lists are single threaded. Just forward.
            original = _fallbackOriginal.load(std::memory_order_relaxed);
        }

        ((PFN_Dispatch) original[DispatchIndex])(This, x, y, z);
    }

    static size_t ReadableEntries(void** address, size_t maxEntries)
    {
        MEMORY_BASIC_INFORMATION mbi {};

        if (VirtualQuery(address, &mbi, sizeof(mbi)) == 0 || mbi.State != MEM_COMMIT ||
            (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) != 0)
        {
            return 0;
        }

        const auto regionEnd = (uintptr_t) mbi.BaseAddress + mbi.RegionSize;
        const auto entries = (regionEnd - (uintptr_t) address) / sizeof(void*);

        return entries < maxEntries ? entries : maxEntries;
    }

    static bool BuildShadow(Capture& capture, void** original)
    {
        capture.cachedOriginal = nullptr;

        const auto entries = ReadableEntries(original, MaxVtableEntries);

        if (entries <= DispatchIndex)
            return false;

        capture.shadow.fill(nullptr);
        capture.shadow[0] = ReadableEntries(original - 1, 1) == 1 ? original[-1] : nullptr;

        std::memcpy(&capture.shadow[1], original, entries * sizeof(void*));
        capture.shadow[1 + DispatchIndex] = (void*) &DispatchShim;

        capture.cachedOriginal = original;

        return true;
    }

    static Marker MarkerFromName(const std::string& name)
    {
        std::string lower = name;
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return (char) std::tolower(c); });

        if (lower.find("official") != std::string::npos)
            return Marker::Official;

        if (lower.find("community") != std::string::npos)
            return Marker::Community;

        if (lower.find("lite") != std::string::npos)
            return Marker::Lite;

        return Marker::OtherDll;
    }

    static void MarkerColor(Marker marker, float color[4])
    {
        color[3] = 1.0f;

        switch (marker)
        {
        case Marker::Official:
            color[0] = 0.0f, color[1] = 0.25f, color[2] = 1.0f;
            break;
        case Marker::Community:
            color[0] = 0.0f, color[1] = 1.0f, color[2] = 0.15f;
            break;
        case Marker::Lite:
            color[0] = 1.0f, color[1] = 0.0f, color[2] = 1.0f;
            break;
        case Marker::OtherDll:
            color[0] = 1.0f, color[1] = 1.0f, color[2] = 1.0f;
            break;
        default:
            color[0] = 1.0f, color[1] = 0.85f, color[2] = 0.0f;
            break;
        }
    }

    static DXGI_FORMAT RenderTargetFormat(DXGI_FORMAT format)
    {
        switch (format)
        {
        case DXGI_FORMAT_R16G16B16A16_TYPELESS:
        case DXGI_FORMAT_R16G16B16A16_FLOAT:
            return DXGI_FORMAT_R16G16B16A16_FLOAT;

        case DXGI_FORMAT_R32G32B32A32_TYPELESS:
        case DXGI_FORMAT_R32G32B32A32_FLOAT:
            return DXGI_FORMAT_R32G32B32A32_FLOAT;

        case DXGI_FORMAT_R10G10B10A2_TYPELESS:
        case DXGI_FORMAT_R10G10B10A2_UNORM:
            return DXGI_FORMAT_R10G10B10A2_UNORM;

        case DXGI_FORMAT_R8G8B8A8_TYPELESS:
        case DXGI_FORMAT_R8G8B8A8_UNORM:
            return DXGI_FORMAT_R8G8B8A8_UNORM;

        case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
            return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;

        case DXGI_FORMAT_B8G8R8A8_TYPELESS:
        case DXGI_FORMAT_B8G8R8A8_UNORM:
            return DXGI_FORMAT_B8G8R8A8_UNORM;

        case DXGI_FORMAT_R11G11B10_FLOAT:
            return DXGI_FORMAT_R11G11B10_FLOAT;

        default:
            return DXGI_FORMAT_UNKNOWN;
        }
    }

    // Marker bar texture, written with ClearRenderTargetView and copied into the output.
    // Neither needs a pipeline, root signature or descriptor heap, so the game's command list state isn't touched.
    struct MarkerTexture
    {
        ID3D12Resource* texture = nullptr;
        ID3D12DescriptorHeap* rtvHeap = nullptr;
        UINT64 width = 0;
        UINT height = 0;
        DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    };

    struct RetiredTexture
    {
        ID3D12Resource* texture = nullptr;
        ID3D12DescriptorHeap* rtvHeap = nullptr;
        uint64_t releaseAtFrame = 0;
    };

    inline static std::mutex _markerMutex;
    inline static MarkerTexture _markerTexture;
    inline static std::vector<RetiredTexture> _retired;

    static void Barrier(ID3D12GraphicsCommandList* commandList, ID3D12Resource* resource, D3D12_RESOURCE_STATES before,
                        D3D12_RESOURCE_STATES after)
    {
        D3D12_RESOURCE_BARRIER barrier {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = resource;
        barrier.Transition.StateBefore = before;
        barrier.Transition.StateAfter = after;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        commandList->ResourceBarrier(1, &barrier);
    }

    static bool EnsureMarkerTexture(ID3D12Resource* output, UINT64 width, UINT height, DXGI_FORMAT format,
                                    DXGI_FORMAT rtvFormat)
    {
        const auto frame = _frames.load(std::memory_order_relaxed);

        // Old textures might still be referenced by command lists that weren't executed yet
        for (auto it = _retired.begin(); it != _retired.end();)
        {
            if (frame >= it->releaseAtFrame)
            {
                it->texture->Release();
                it->rtvHeap->Release();
                it = _retired.erase(it);
            }
            else
            {
                ++it;
            }
        }

        if (_markerTexture.texture != nullptr && _markerTexture.width == width && _markerTexture.height == height &&
            _markerTexture.format == format)
        {
            return true;
        }

        if (_markerTexture.texture != nullptr)
        {
            _retired.push_back({ _markerTexture.texture, _markerTexture.rtvHeap, frame + 600 });
            _markerTexture = {};
        }

        ID3D12Device* device = nullptr;

        if (output->GetDevice(IID_PPV_ARGS(&device)) != S_OK || device == nullptr)
            return false;

        D3D12_HEAP_PROPERTIES heapProperties {};
        heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;

        D3D12_RESOURCE_DESC desc {};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = width;
        desc.Height = height;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = format;
        desc.SampleDesc.Count = 1;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

        D3D12_DESCRIPTOR_HEAP_DESC heapDesc {};
        heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        heapDesc.NumDescriptors = 1;

        ID3D12Resource* texture = nullptr;
        ID3D12DescriptorHeap* rtvHeap = nullptr;

        HRESULT result = device->CreateCommittedResource(&heapProperties, D3D12_HEAP_FLAG_NONE, &desc,
                                                         D3D12_RESOURCE_STATE_COPY_SOURCE, nullptr,
                                                         IID_PPV_ARGS(&texture));

        if (result == S_OK)
            result = device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&rtvHeap));

        if (result == S_OK)
        {
            D3D12_RENDER_TARGET_VIEW_DESC rtvDesc {};
            rtvDesc.Format = rtvFormat;
            rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

            device->CreateRenderTargetView(texture, &rtvDesc, rtvHeap->GetCPUDescriptorHandleForHeapStart());
        }

        device->Release();

        if (result != S_OK)
        {
            LOG_ERROR("[SR-VISUAL] can't create the marker texture: {:X}", (UINT) result);

            if (texture != nullptr)
                texture->Release();

            if (rtvHeap != nullptr)
                rtvHeap->Release();

            return false;
        }

        _markerTexture = { texture, rtvHeap, width, height, format };

        return true;
    }

    static DrawStatus DrawMarker(ID3D12GraphicsCommandList* commandList, ID3D12Resource* output, bool outputIsUav,
                                 UINT upscaleWidth, UINT upscaleHeight, Marker marker)
    {
        if (commandList == nullptr || output == nullptr)
            return DrawStatus::NoCommandList;

        if (commandList->GetType() != D3D12_COMMAND_LIST_TYPE_DIRECT)
            return DrawStatus::NotDirectList;

        if (!outputIsUav)
            return DrawStatus::OutputNotUav;

        const auto outputDesc = output->GetDesc();
        const auto rtvFormat = RenderTargetFormat(outputDesc.Format);

        if (outputDesc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D || outputDesc.SampleDesc.Count != 1 ||
            rtvFormat == DXGI_FORMAT_UNKNOWN)
        {
            return DrawStatus::UnsupportedOutput;
        }

        UINT64 width = outputDesc.Width;
        UINT height = outputDesc.Height;

        if (upscaleWidth > 0 && upscaleWidth < width)
            width = upscaleWidth;

        if (upscaleHeight > 0 && upscaleHeight < height)
            height = upscaleHeight;

        UINT barHeight = height / 36;

        if (barHeight < 8)
            barHeight = 8;

        if (width == 0 || height < barHeight * 4)
            return DrawStatus::UnsupportedOutput;

        std::scoped_lock lock(_markerMutex);

        if (!EnsureMarkerTexture(output, width, barHeight, outputDesc.Format, rtvFormat))
            return DrawStatus::CreateFailed;

        float color[4];
        MarkerColor(marker, color);

        Barrier(commandList, _markerTexture.texture, D3D12_RESOURCE_STATE_COPY_SOURCE,
                D3D12_RESOURCE_STATE_RENDER_TARGET);

        commandList->ClearRenderTargetView(_markerTexture.rtvHeap->GetCPUDescriptorHandleForHeapStart(), color, 0,
                                           nullptr);

        Barrier(commandList, _markerTexture.texture, D3D12_RESOURCE_STATE_RENDER_TARGET,
                D3D12_RESOURCE_STATE_COPY_SOURCE);

        Barrier(commandList, output, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST);

        D3D12_TEXTURE_COPY_LOCATION source {};
        source.pResource = _markerTexture.texture;
        source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        source.SubresourceIndex = 0;

        D3D12_TEXTURE_COPY_LOCATION destination {};
        destination.pResource = output;
        destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        destination.SubresourceIndex = 0;

        commandList->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);

        // Bottom bar only for a verified frame, so a not verified one can't be mistaken for a variant color
        if (marker != Marker::Unverified)
            commandList->CopyTextureRegion(&destination, 0, height - barHeight, 0, &source, nullptr);

        Barrier(commandList, output, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        return DrawStatus::Drawn;
    }
};
