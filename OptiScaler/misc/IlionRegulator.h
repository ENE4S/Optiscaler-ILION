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
//
// Where the wait lands (dev.51-exp): inside the game's Present call of frame N, after the real Present. So Reflex's
// frame markers (CPU), which end at PRESENT_END, contain it; ILION's latency of frame N ends its CPU part at the
// Present call, and frame N+1 starts at its SIMULATION_START after the wait: no ILION frame contains it.
// Queue depth (dev.51-exp): with the same fence, the frames the GPU has not finished when the game gets its thread
// back from Present (after the wait), this one included. Measured also with the regulator off while the overlay shows ILION's latency (a Signal, never a wait), so
// Off can be compared with Balanced and Maximum.

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
    FrameGeneration, // frame generation is presenting (ILION's or the game's DLSSG)
    LowLatency,      // ILION 1.5: a low latency output (Anti-Lag, Reflex, XeLL, LatencyFlex) already keeps the queue short
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
inline uint64_t lastSeen = 0; // QPC of the last measured Present
inline int lastLevel = -1;    // a level change starts the means again
inline std::atomic<int> state { (int) Status::NoFrames };
inline std::atomic<float> waitMs { 0.0f };      // mean time the CPU was held per frame
inline std::atomic<float> queueFrames { -1.0f }; // mean frames not finished by the GPU at Present, -1 = not measured
inline std::atomic<uint64_t> measuredAt { 0 };   // QPC of the last measured Present

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
    detail::queueFrames.store(-1.0f, std::memory_order_relaxed);
}

// Right after the game's Present on a DirectX 12 swapchain, before returning to the game. measure: the overlay shows
// ILION's latency, so the queue depth is measured even with the regulator off (then nothing waits)
inline void AfterPresent(ID3D12CommandQueue* presentQueue, bool measure)
{
    using namespace detail;

    const int level = CurrentLevel();

    if (level == Off)
        state.store((int) Status::Off, std::memory_order_relaxed);

    if (level == Off && !measure)
    {
        queueFrames.store(-1.0f, std::memory_order_relaxed);
        return;
    }

    if (presentQueue == nullptr)
    {
        if (level != Off)
            state.store((int) Status::NoFrames, std::memory_order_relaxed);
        return;
    }

    std::scoped_lock guard(lock);

    ID3D12Device* presentDevice = nullptr;
    if (presentQueue->GetDevice(IID_PPV_ARGS(&presentDevice)) != S_OK || presentDevice == nullptr)
    {
        if (level != Off)
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
            if (level != Off)
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
        if (level != Off)
            state.store((int) Status::Failed, std::memory_order_relaxed);
        return;
    }

    value++;

    const uint64_t start = Qpc();

    const uint64_t completed = fence->GetCompletedValue();

    double heldMs = 0.0;
    if (level != Off)
    {
        // Frames allowed in flight after this one: Balanced lets one more stay queued
        const uint64_t inFlight = level == Maximum ? 1 : 2;
        const uint64_t target = value >= inFlight ? value - (inFlight - 1) : 0;

        if (target > 0 && completed < target && fence->SetEventOnCompletion(target, event) == S_OK)
        {
            WaitForSingleObject(event, 100);
            heldMs = (double) (Qpc() - start) * 1000.0 / (double) QpcFrequency();
        }
    }

    // Frames the GPU has not finished when the game gets its thread back, this one included (after the wait; the
    // first frames after a new fence count only what this fence has seen)
    const uint64_t done = fence->GetCompletedValue();
    const double depth = done >= value ? 0.0 : (double) (value - done);

    // Means over about half a second of frames; a pause of the game or a level change starts them again
    const bool fresh = lastSeen == 0 || start - lastSeen > QpcFrequency() || level != lastLevel;
    lastSeen = start;
    lastLevel = level;
    const float previousWait = waitMs.load(std::memory_order_relaxed);
    waitMs.store(fresh ? (float) heldMs : previousWait * 0.95f + (float) heldMs * 0.05f, std::memory_order_relaxed);
    const float previousDepth = queueFrames.load(std::memory_order_relaxed);
    queueFrames.store(fresh || previousDepth < 0.0f ? (float) depth : previousDepth * 0.95f + (float) depth * 0.05f,
                      std::memory_order_relaxed);
    measuredAt.store(start, std::memory_order_relaxed);

    if (level != Off)
        state.store((int) Status::Active, std::memory_order_relaxed);
}

inline Status CurrentState() { return (Status) detail::state.load(std::memory_order_relaxed); }

// Mean CPU wait per frame in ms, only meaningful while Active
inline float MeanWaitMs() { return detail::waitMs.load(std::memory_order_relaxed); }

// Mean frames the GPU had not finished when Present returned to the game (this one included), -1 when not measured in the last second
inline float MeanQueueFrames()
{
    const uint64_t at = detail::measuredAt.load(std::memory_order_relaxed);
    if (at == 0 || detail::Qpc() - at > detail::QpcFrequency())
        return -1.0f;
    return detail::queueFrames.load(std::memory_order_relaxed);
}
} // namespace IlionRegulator
