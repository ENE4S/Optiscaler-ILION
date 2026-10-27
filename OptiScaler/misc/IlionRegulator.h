#pragma once

// ILION dev.51: latency regulator, DirectX 12.
// The game's CPU may run several frames ahead of the GPU. Each frame waiting in the GPU queue adds a whole frame time
// of latency, and with Neural Rendering the queue fills (the game's queue waits for the network, the CPU keeps going).
// After each Present the regulator signals a fence on the present queue and, before returning to the game, waits until
// the GPU has finished all but the allowed frames in flight:
//   Balanced  2 frames in flight: the GPU always has the next frame queued, so the frame rate stays the same when the
//             GPU is the limit; at most one frame waits in the queue.
//   Maximum   1 frame in flight: the CPU starts a frame only once the GPU has finished the previous one. Lowest latency,
//             but the GPU waits while the CPU prepares the next frame, so it costs frame rate.
// Per frame: one Signal and, only when the GPU is behind, one wait (100 ms at most, never blocks the game for good).
// Off with frame generation (its presents are paced by the generator), DX11, DXVK and the DX11/Vulkan over DX12 bridges.

#include <d3d12.h>

#include <atomic>
#include <cstdint>
#include <mutex>

#include <Config.h>

namespace IlionRegulator
{
enum Level : int
{
    Off = 0,
    Balanced = 1,
    Maximum = 2
};

// Why it isn't waiting (the menu's state)
enum class Status : int
{
    Off,
    NoFrames,        // no DirectX 12 Present seen yet
    FrameGeneration, // frame generation is presenting
    NotSupported,    // DX11, DXVK or a bridge
    Failed,          // the fence could not be made
    Active
};

namespace detail
{
inline std::mutex lock;
inline ID3D12CommandQueue* queue = nullptr; // identity only, not held
inline ID3D12Device* device = nullptr;      // identity only, not held
inline ID3D12Fence* fence = nullptr;
inline HANDLE event = nullptr;
inline uint64_t value = 0;
inline uint64_t lastSeen = 0; // QPC of the last regulated Present
inline std::atomic<int> state { (int) Status::NoFrames };
inline std::atomic<float> waitMs { 0.0f }; // mean time the CPU was held per frame

inline uint64_t Qpc()
{
    LARGE_INTEGER v;
    QueryPerformanceCounter(&v);
    return (uint64_t) v.QuadPart;
}

inline uint64_t QpcFrequency()
{
    static const uint64_t frequency = []
    {
        LARGE_INTEGER v;
        QueryPerformanceFrequency(&v);
        return (uint64_t) v.QuadPart;
    }();
    return frequency;
}

inline void Release()
{
    if (fence != nullptr)
        fence->Release();

    fence = nullptr;
    queue = nullptr;
    device = nullptr;
    value = 0;
}
} // namespace detail

inline int CurrentLevel()
{
    const int level = Config::Instance()->IlionLatencyRegulator.value_or_default();
    return level >= Off && level <= Maximum ? level : Off;
}

// The present path when this frame can't be regulated
inline void Skip(Status why)
{
    detail::state.store((int) why, std::memory_order_relaxed);
}

// Right after the game's Present on a DirectX 12 swapchain, before returning to the game
inline void AfterPresent(ID3D12CommandQueue* presentQueue)
{
    using namespace detail;

    const int level = CurrentLevel();

    if (level == Off)
    {
        state.store((int) Status::Off, std::memory_order_relaxed);
        return;
    }

    if (presentQueue == nullptr)
    {
        state.store((int) Status::NoFrames, std::memory_order_relaxed);
        return;
    }

    std::scoped_lock guard(lock);

    ID3D12Device* presentDevice = nullptr;
    if (presentQueue->GetDevice(IID_PPV_ARGS(&presentDevice)) != S_OK || presentDevice == nullptr)
    {
        state.store((int) Status::Failed, std::memory_order_relaxed);
        return;
    }
    presentDevice->Release(); // the queue keeps the device alive

    // A new queue or device (swapchain made again): start over with a fence of that device
    if (presentQueue != queue || presentDevice != device)
    {
        Release();

        if (presentDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)) != S_OK)
        {
            fence = nullptr;
            state.store((int) Status::Failed, std::memory_order_relaxed);
            return;
        }

        queue = presentQueue;
        device = presentDevice;
    }

    if (event == nullptr)
        event = CreateEventW(nullptr, FALSE, FALSE, nullptr);

    if (event == nullptr || presentQueue->Signal(fence, value + 1) != S_OK)
    {
        state.store((int) Status::Failed, std::memory_order_relaxed);
        return;
    }

    value++;

    // Frames allowed in flight after this one: Balanced lets one more stay queued
    const uint64_t inFlight = level == Maximum ? 1 : 2;
    const uint64_t target = value >= inFlight ? value - (inFlight - 1) : 0;

    const uint64_t start = Qpc();
    double heldMs = 0.0;

    if (target > 0 && fence->GetCompletedValue() < target && fence->SetEventOnCompletion(target, event) == S_OK)
    {
        WaitForSingleObject(event, 100);
        heldMs = (double) (Qpc() - start) * 1000.0 / (double) QpcFrequency();
    }

    // Mean over about half a second of frames; a pause of the game starts it again
    const bool fresh = lastSeen == 0 || start - lastSeen > QpcFrequency();
    lastSeen = start;
    const float previous = waitMs.load(std::memory_order_relaxed);
    waitMs.store(fresh ? (float) heldMs : previous * 0.95f + (float) heldMs * 0.05f, std::memory_order_relaxed);

    state.store((int) Status::Active, std::memory_order_relaxed);
}

inline Status CurrentState() { return (Status) detail::state.load(std::memory_order_relaxed); }

// Mean CPU wait per frame in ms, only meaningful while Active
inline float MeanWaitMs() { return detail::waitMs.load(std::memory_order_relaxed); }
} // namespace IlionRegulator
