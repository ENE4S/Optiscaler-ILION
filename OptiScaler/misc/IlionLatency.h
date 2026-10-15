#pragma once

// ILION: system latency from what each frame really does, DirectX 12.
//   input / simulation start  Reflex markers of the game (real CPU times, also through fakenvapi)
//   Present                   the present path of OptiScaler
//   end of the GPU work       a GPU timestamp written on the present queue right before Present, converted to the CPU
//                             clock with GetClockCalibration
//   on screen                 DXGI frame statistics: the refresh in which that Present was shown (flip model only)
// Nothing is estimated: a part that can't be measured is left out and said so. Works only while the overlay shows the
// latency block, otherwise every call returns at once.

#include <d3d12.h>
#include <dxgi1_4.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <mutex>

#include <Config.h>
#include <misc/IlionOverlay.h>

namespace IlionLatency
{
inline uint64_t Qpc()
{
    LARGE_INTEGER value;
    QueryPerformanceCounter(&value);
    return (uint64_t) value.QuadPart;
}

inline uint64_t QpcFrequency()
{
    static const uint64_t frequency = []
    {
        LARGE_INTEGER value;
        QueryPerformanceFrequency(&value);
        return (uint64_t) value.QuadPart;
    }();
    return frequency;
}

inline double QpcToMs(int64_t ticks) { return (double) ticks * 1000.0 / (double) QpcFrequency(); }

// Only while the overlay shows the ILION estimated latency
inline bool Enabled()
{
    return Config::Instance()->ShowFps.value_or_default() && IlionOverlay::Shows(IlionOverlay::IlionLatency);
}

// What the overlay shows: means of the last complete frames. Total and parts come from the same frames, so
// CPU + GPU + screen add up to the total (rounding apart).
struct Result
{
    bool valid = false;          // complete frames exist
    bool onScreen = false;       // ends at the refresh that showed the frame; else at the end of the GPU work
    double system = 0.0;         // simulation start -> screen (or end of GPU)
    double inputToScreen = -1.0; // the game's input sample -> screen, when the game sends that marker
    double cpu = -1.0;           // simulation start -> Present
    double gpu = -1.0;           // Present -> end of the GPU work on the present queue
    double display = -1.0;       // end of the GPU work -> refresh that showed it (queue, flip, wait for vblank)
    double displayedFps = -1.0;
    bool composed = false;       // Windows composes the window: no screen time
    uint64_t at = 0;             // when it was last updated
};

namespace detail
{
constexpr uint32_t kSimulationStart = 0; // SIMULATION_START, VULKAN_SIMULATION_START
constexpr uint32_t kPresentStart = 4;    // PRESENT_START
constexpr uint32_t kInputSample = 6;     // INPUT_SAMPLE
constexpr int kSlots = 8;                // GPU timestamps in flight
constexpr size_t kFrames = 32;           // frames followed until they reach the screen
constexpr size_t kSamples = 32;          // samples the medians are taken from

struct Marker
{
    uint64_t frameId = 0;
    uint64_t input = 0;
    uint64_t simulation = 0;
};

struct Frame
{
    bool open = false;
    uint64_t input = 0;
    uint64_t simulation = 0;
    uint64_t present = 0;
    uint64_t gpuEnd = 0;
    uint64_t screen = 0;
    UINT presentCount = 0;
    bool gpuPending = false;
    bool screenDone = false;
};

struct Window
{
    std::array<double, kSamples> values {};
    size_t count = 0;
    size_t next = 0;

    void Add(double value)
    {
        values[next] = value;
        next = (next + 1) % kSamples;
        count = std::min(count + 1, kSamples);
    }

    // A mean, not a median: means of the parts of the same frames add up to the mean of their totals
    double Mean() const
    {
        if (count == 0)
            return -1.0;

        double sum = 0.0;
        for (size_t i = 0; i < count; i++)
            sum += values[i];

        return sum / (double) count;
    }

    void Clear() { count = next = 0; }
};

struct Gpu
{
    ID3D12CommandQueue* queue = nullptr;
    ID3D12QueryHeap* heap = nullptr;
    ID3D12Resource* readback = nullptr;
    const uint64_t* mapped = nullptr;
    ID3D12Fence* fence = nullptr;
    ID3D12CommandAllocator* allocators[kSlots] {};
    ID3D12GraphicsCommandList* lists[kSlots] {};
    uint64_t fenceValues[kSlots] {};
    int frameOf[kSlots] {};
    uint64_t nextFence = 0;
    int next = 0;
    uint64_t gpuFrequency = 0;
    uint64_t calibrationGpu = 0;
    uint64_t calibrationCpu = 0;
    uint64_t calibratedAt = 0;
    bool ok = false;
};

inline std::mutex lock;
inline std::array<Marker, 64> markers {};
inline uint64_t presentingFrame = 0;
inline std::array<Frame, kFrames> frames {};
inline size_t frameNext = 0;
inline int current = -1; // frame of the Present being made
inline Gpu gpu;
inline Window systemWindow, inputWindow, cpuWindow, gpuWindow, displayWindow; // frames that reached the screen
inline Window toGpuWindow;                                                    // composed window: simulation -> end of GPU
inline bool lastOnScreen = false, composed = false;
inline UINT statsCount = 0;
inline uint64_t statsAt = 0;
inline double displayedFps = -1.0;
inline uint64_t updatedAt = 0;

// GPU objects for one present queue; a queue that can't take timestamps leaves the GPU part out
inline bool SetupGpu(ID3D12CommandQueue* queue)
{
    if (gpu.queue == queue)
        return gpu.ok;

    gpu = Gpu {};
    gpu.queue = queue;

    const D3D12_COMMAND_QUEUE_DESC desc = queue->GetDesc();
    if (desc.Type != D3D12_COMMAND_LIST_TYPE_DIRECT && desc.Type != D3D12_COMMAND_LIST_TYPE_COMPUTE)
        return false;

    ID3D12Device* device = nullptr;
    if (queue->GetDevice(IID_PPV_ARGS(&device)) != S_OK || device == nullptr)
        return false;

    bool ok = queue->GetTimestampFrequency(&gpu.gpuFrequency) == S_OK && gpu.gpuFrequency != 0;

    D3D12_QUERY_HEAP_DESC heapDesc {};
    heapDesc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    heapDesc.Count = kSlots;
    ok = ok && device->CreateQueryHeap(&heapDesc, IID_PPV_ARGS(&gpu.heap)) == S_OK;

    D3D12_HEAP_PROPERTIES heapProps {};
    heapProps.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC bufferDesc {};
    bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufferDesc.Width = sizeof(uint64_t) * kSlots;
    bufferDesc.Height = 1;
    bufferDesc.DepthOrArraySize = 1;
    bufferDesc.MipLevels = 1;
    bufferDesc.SampleDesc.Count = 1;
    bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ok = ok && device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &bufferDesc,
                                               D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&gpu.readback)) == S_OK;

    void* mapped = nullptr;
    ok = ok && gpu.readback->Map(0, nullptr, &mapped) == S_OK;
    gpu.mapped = (const uint64_t*) mapped;

    ok = ok && device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&gpu.fence)) == S_OK;

    for (int i = 0; ok && i < kSlots; i++)
    {
        ok = device->CreateCommandAllocator(desc.Type, IID_PPV_ARGS(&gpu.allocators[i])) == S_OK &&
             device->CreateCommandList(0, desc.Type, gpu.allocators[i], nullptr, IID_PPV_ARGS(&gpu.lists[i])) == S_OK &&
             gpu.lists[i]->Close() == S_OK;
        gpu.frameOf[i] = -1;
    }

    device->Release();
    gpu.ok = ok;
    return ok;
}

inline uint64_t GpuToQpc(uint64_t timestamp)
{
    const uint64_t now = Qpc();

    // The two clocks drift a little: calibrate twice a second
    if (gpu.calibratedAt == 0 || now - gpu.calibratedAt > QpcFrequency() / 2)
    {
        if (gpu.queue->GetClockCalibration(&gpu.calibrationGpu, &gpu.calibrationCpu) == S_OK)
            gpu.calibratedAt = now;
    }

    const double ticks = ((double) timestamp - (double) gpu.calibrationGpu) * (double) QpcFrequency() / (double) gpu.gpuFrequency;
    return (uint64_t) ((double) gpu.calibrationCpu + ticks);
}

inline void CloseFrame(Frame& frame)
{
    frame.open = false;

    // A GPU end before its own Present would be a bad conversion: left out
    if (frame.gpuEnd != 0 && frame.gpuEnd < frame.present)
        frame.gpuEnd = 0;

    // Every sample starts at the simulation start of the same frame; a frame without it isn't counted
    if (frame.simulation == 0 || frame.present < frame.simulation)
        return;

    if (frame.screen != 0)
    {
        if (frame.screen < frame.present)
            return;

        // GPU timestamps work but this frame's didn't arrive: the frame is left out whole, so parts and total
        // keep coming from the same frames
        if (gpu.ok && (frame.gpuEnd == 0 || frame.screen < frame.gpuEnd))
            return;

        systemWindow.Add(QpcToMs((int64_t) (frame.screen - frame.simulation)));
        cpuWindow.Add(QpcToMs((int64_t) (frame.present - frame.simulation)));

        if (frame.gpuEnd != 0)
        {
            gpuWindow.Add(QpcToMs((int64_t) (frame.gpuEnd - frame.present)));
            displayWindow.Add(QpcToMs((int64_t) (frame.screen - frame.gpuEnd)));
        }
        else
        {
            displayWindow.Add(QpcToMs((int64_t) (frame.screen - frame.present))); // no GPU timestamps on this queue
        }

        // The game's input sample, on its own: it isn't mixed into the total
        if (frame.input != 0 && frame.input <= frame.screen)
            inputWindow.Add(QpcToMs((int64_t) (frame.screen - frame.input)));

        lastOnScreen = true;
        updatedAt = Qpc();
        return;
    }

    // No screen time (composed window): up to the end of the GPU work, in its own window
    if (frame.gpuEnd != 0)
    {
        toGpuWindow.Add(QpcToMs((int64_t) (frame.gpuEnd - frame.simulation)));
        lastOnScreen = false;
        updatedAt = Qpc();
    }
}
} // namespace detail

// Reflex marker of the game (D3D or Vulkan), observation only
inline void OnMarker(uint32_t type, uint64_t frameId)
{
    using namespace detail;

    if (frameId == 0 || !Enabled())
        return;

    const uint64_t now = Qpc();
    std::scoped_lock guard(lock);

    auto& marker = markers[frameId % markers.size()];
    if (marker.frameId != frameId)
        marker = Marker { frameId };

    if (type == kSimulationStart)
        marker.simulation = now;
    else if (type == kInputSample)
        marker.input = now;
    else if (type == kPresentStart)
        presentingFrame = frameId;
}

// Right before a DirectX 12 Present. Frames generated by frame generation aren't followed, only real ones.
inline void BeforePresent(ID3D12CommandQueue* queue, bool interpolated)
{
    using namespace detail;

    current = -1;

    if (queue == nullptr || interpolated || !Enabled())
        return;

    std::scoped_lock guard(lock);

    const int index = (int) frameNext;
    frameNext = (frameNext + 1) % kFrames;

    Frame& frame = frames[index];
    frame = Frame {};
    frame.open = true;

    const auto& marker = markers[presentingFrame % markers.size()];
    if (presentingFrame != 0 && marker.frameId == presentingFrame)
    {
        frame.input = marker.input;
        frame.simulation = marker.simulation;
    }

    // End of the GPU work: a timestamp after everything already on the present queue
    if (SetupGpu(queue))
    {
        const int slot = gpu.next;

        if (gpu.fenceValues[slot] == 0 || gpu.fence->GetCompletedValue() >= gpu.fenceValues[slot])
        {
            // A frame the slot still held is over: whatever it didn't get stays unmeasured
            if (gpu.frameOf[slot] >= 0)
                frames[gpu.frameOf[slot]].gpuPending = false;

            auto* list = gpu.lists[slot];
            if (gpu.allocators[slot]->Reset() == S_OK && list->Reset(gpu.allocators[slot], nullptr) == S_OK)
            {
                list->EndQuery(gpu.heap, D3D12_QUERY_TYPE_TIMESTAMP, (UINT) slot);
                list->ResolveQueryData(gpu.heap, D3D12_QUERY_TYPE_TIMESTAMP, (UINT) slot, 1, gpu.readback,
                                       sizeof(uint64_t) * slot);

                if (list->Close() == S_OK)
                {
                    ID3D12CommandList* lists[] = { list };
                    queue->ExecuteCommandLists(1, lists);
                    queue->Signal(gpu.fence, ++gpu.nextFence);
                    gpu.fenceValues[slot] = gpu.nextFence;
                    gpu.frameOf[slot] = index;
                    frame.gpuPending = true;
                    gpu.next = (slot + 1) % kSlots;
                }
            }
        }
    }

    frame.present = Qpc();
    current = index;
}

// Right after every Present (real or generated): its number, finished GPU timestamps, the refresh things reached
inline void AfterPresent(IDXGISwapChain* swapChain, HRESULT result)
{
    using namespace detail;

    if (swapChain == nullptr || !Enabled())
        return;

    std::scoped_lock guard(lock);

    UINT count = 0;
    const bool counted = swapChain->GetLastPresentCount(&count) == S_OK;

    if (current >= 0 && SUCCEEDED(result) && counted)
        frames[current].presentCount = count;

    current = -1;

    // Finished GPU timestamps
    if (gpu.ok)
    {
        const uint64_t done = gpu.fence->GetCompletedValue();

        for (int slot = 0; slot < kSlots; slot++)
        {
            const int index = gpu.frameOf[slot];
            if (index < 0 || gpu.fenceValues[slot] > done)
                continue;

            frames[index].gpuEnd = GpuToQpc(gpu.mapped[slot]);
            frames[index].gpuPending = false;
            gpu.frameOf[slot] = -1;
        }
    }

    // The refresh the last shown Present reached; and how many Presents reach the screen per second
    DXGI_FRAME_STATISTICS stats {};
    const bool haveStats = swapChain->GetFrameStatistics(&stats) == S_OK && stats.PresentCount != 0;
    composed = !haveStats;

    const uint64_t now = Qpc();

    if (haveStats)
    {
        if (statsAt == 0 || stats.PresentCount < statsCount)
        {
            statsCount = stats.PresentCount;
            statsAt = now;
        }
        else if (now - statsAt >= QpcFrequency() / 2)
        {
            displayedFps = (double) (stats.PresentCount - statsCount) * (double) QpcFrequency() / (double) (now - statsAt);
            statsCount = stats.PresentCount;
            statsAt = now;
        }
    }
    else
    {
        displayedFps = -1.0;
        statsAt = 0;
    }

    for (auto& frame : frames)
    {
        if (!frame.open || frame.presentCount == 0)
            continue;

        if (haveStats && !frame.screenDone)
        {
            if (frame.presentCount == stats.PresentCount)
            {
                frame.screen = (uint64_t) stats.SyncQPCTime.QuadPart;
                frame.screenDone = true;
            }
            else if (frame.presentCount < stats.PresentCount)
            {
                frame.screenDone = true; // shown between two reads: its refresh wasn't seen
            }
        }

        // Closed once its GPU timestamp is in and its screen time is known or can't be known
        if (!frame.gpuPending && (frame.screenDone || !haveStats))
            CloseFrame(frame);
    }
}

inline Result Get()
{
    using namespace detail;

    std::scoped_lock guard(lock);

    Result result;
    result.at = updatedAt;
    result.composed = composed;
    result.displayedFps = displayedFps;

    if (updatedAt == 0 || Qpc() - updatedAt > QpcFrequency())
        return result;

    // Frames reaching the screen give the full figure; a composed window only the part up to the end of the GPU
    result.onScreen = lastOnScreen && systemWindow.count > 0;

    if (result.onScreen)
    {
        result.valid = true;
        result.system = systemWindow.Mean();
        result.inputToScreen = inputWindow.Mean();
        result.cpu = cpuWindow.Mean();
        result.gpu = gpuWindow.Mean();
        result.display = displayWindow.Mean();
    }
    else if (toGpuWindow.count > 0)
    {
        result.valid = true;
        result.system = toGpuWindow.Mean();
    }

    return result;
}
} // namespace IlionLatency
