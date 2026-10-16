#pragma once

// ILION: system latency from what each frame really does, DirectX 12.
//   input / simulation start  Reflex markers of the game (real CPU times, also through fakenvapi). The time is taken
//                             when the marker call returns: Anti-Lag 2 and LatencyFleX may sleep inside the
//                             SIMULATION_START marker, and that wait comes before the simulation, not in it
//   Present                   the present path of OptiScaler
//   end of the GPU work       a GPU timestamp written on the present queue right before Present, converted to the CPU
//                             clock with GetClockCalibration
//   on screen                 DXGI frame statistics: the refresh in which that Present was shown (flip model only)
// The total is simulation start -> screen of one frame; CPU, GPU and screen are its consecutive parts. A part that
// can't be measured for a frame is left out with its reason, never filled in. Works only while the overlay shows the
// block, otherwise every call returns at once.

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

// Why frames of the last second gave no full figure (the most frequent one)
enum class Missing : int
{
    None,
    Settling,        // the latency reducer just changed: old frames dropped, new ones not in yet
    NoFrames,        // no DirectX 12 Present was followed
    NoSimulation,    // the game sent no SIMULATION_START marker for the presented frame
    NoGpuTime,       // the GPU timestamp of the frame didn't arrive
    Composed,        // DXGI gives no frame statistics
    RefreshNotSeen,  // the frame was shown between two reads of the statistics
    RefreshBeforeGpu, // the refresh DXGI reports comes before the end of the GPU work: no screen time
    Tearing           // tearing Present not composed: the image changes during scanout, the refresh time is not when it
                      // appeared, so the figure ends at the end of the GPU work
};

// What the overlay shows: means of the same recent frames, so the parts add up to the total (rounding apart)
struct Result
{
    bool valid = false;
    bool onScreen = false;       // ends at the refresh that showed the frame; else at the end of the GPU work
    double system = 0.0;         // simulation start -> screen (or end of GPU)
    double inputToScreen = -1.0; // the game's input sample -> screen, when the game sends that marker
    double cpu = -1.0;           // simulation start -> Present
    double gpu = -1.0;           // Present -> end of the GPU work on the present queue
    double display = -1.0;       // end of the GPU work -> refresh that showed it
    double afterPresent = -1.0;  // Present -> refresh, when the GPU part can't be split off
    double displayedFps = -1.0;
    int frames = 0;              // frames the means come from
    int presentation = -1;       // DXGI_FRAME_PRESENTATION_MODE when known: 0 composed, 1 overlay, 2 none
    Missing missing = Missing::None;
};

namespace detail
{
constexpr uint32_t kSimulationStart = 0; // SIMULATION_START
constexpr uint32_t kPresentStart = 4;    // PRESENT_START
constexpr uint32_t kInputSample = 6;     // INPUT_SAMPLE
constexpr int kSlots = 8;                // GPU timestamps in flight
constexpr size_t kFrames = 32;           // frames followed until they reach the screen
constexpr size_t kSamples = 64;          // samples the means are taken from

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
    bool tearing = false;
    Missing screenMissing = Missing::None;
};

// One closed frame; -1 = not measured for it
struct Sample
{
    uint64_t at = 0;
    bool onScreen = false;
    double system = -1.0, cpu = -1.0, gpu = -1.0, display = -1.0, afterPresent = -1.0, input = -1.0, toGpu = -1.0;
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
inline std::array<Sample, kSamples> samples {};
inline size_t sampleNext = 0;
inline std::array<uint32_t, 9> missingCount {}; // per Missing, frames of the current second
inline Missing lastMissing = Missing::NoFrames;
inline uint64_t missingSince = 0;
inline uint64_t settleUntil = 0; // frames presented before this are dropped (reducer change)
inline int reducer = -1;
inline UINT statsCount = 0;
inline uint64_t statsAt = 0;
inline double displayedFps = -1.0;
inline int presentation = -1;

inline void Note(Missing why) { missingCount[(size_t) why]++; }

inline void ClearSamples()
{
    samples = {};
    sampleNext = 0;
}

// GPU objects for one present queue; a queue that can't take timestamps leaves the GPU part out
inline bool SetupGpu(ID3D12CommandQueue* queue)
{
    if (gpu.queue == queue)
        return gpu.ok;

    // Objects of the previous queue: released once the GPU is done with them (a short wait at most), else kept
    if (gpu.fence != nullptr)
    {
        bool idle = gpu.fence->GetCompletedValue() >= gpu.nextFence;

        if (!idle)
        {
            if (HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr); event != nullptr)
            {
                if (gpu.fence->SetEventOnCompletion(gpu.nextFence, event) == S_OK)
                    idle = WaitForSingleObject(event, 100) == WAIT_OBJECT_0;

                CloseHandle(event);
            }
        }

        if (idle)
        {
            for (int i = 0; i < kSlots; i++)
            {
                if (gpu.lists[i] != nullptr)
                    gpu.lists[i]->Release();
                if (gpu.allocators[i] != nullptr)
                    gpu.allocators[i]->Release();
            }

            if (gpu.readback != nullptr)
                gpu.readback->Release();
            if (gpu.heap != nullptr)
                gpu.heap->Release();
            gpu.fence->Release();
        }
    }

    gpu = Gpu {};
    gpu.queue = queue;

    for (int i = 0; i < kSlots; i++)
        gpu.frameOf[i] = -1;

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

    // Presented before the reducer change settled: from the old configuration
    if (frame.present < settleUntil)
    {
        Note(Missing::Settling);
        return;
    }

    // Every sample starts at the simulation start of the same frame
    if (frame.simulation == 0 || frame.present < frame.simulation)
    {
        Note(Missing::NoSimulation);
        return;
    }

    // A GPU end before its own Present would be a bad conversion
    if (frame.gpuEnd != 0 && frame.gpuEnd < frame.present)
        frame.gpuEnd = 0;

    // The refresh DXGI reports is the last vblank; with tearing the frame can replace the image during the scanout
    // after it, so a refresh before the end of the GPU work isn't when it appeared: no screen time for that frame
    if (frame.screen != 0 && (frame.screen < frame.present || (frame.gpuEnd != 0 && frame.screen < frame.gpuEnd)))
    {
        frame.screen = 0;
        frame.screenMissing = Missing::RefreshBeforeGpu;
    }

    Sample sample;
    sample.at = Qpc();
    sample.cpu = QpcToMs((int64_t) (frame.present - frame.simulation));

    if (frame.gpuEnd != 0)
    {
        sample.gpu = QpcToMs((int64_t) (frame.gpuEnd - frame.present));
        sample.toGpu = QpcToMs((int64_t) (frame.gpuEnd - frame.simulation));
    }

    if (frame.screen != 0)
    {
        sample.onScreen = true;
        sample.system = QpcToMs((int64_t) (frame.screen - frame.simulation));
        sample.afterPresent = QpcToMs((int64_t) (frame.screen - frame.present));

        if (frame.gpuEnd != 0)
            sample.display = QpcToMs((int64_t) (frame.screen - frame.gpuEnd));

        // The game's input sample, on its own: it isn't part of the total
        if (frame.input != 0 && frame.input <= frame.screen)
            sample.input = QpcToMs((int64_t) (frame.screen - frame.input));

        Note(gpu.ok && frame.gpuEnd == 0 ? Missing::NoGpuTime : Missing::None);
    }
    else
    {
        Note(frame.gpuEnd == 0 && gpu.ok ? Missing::NoGpuTime
             : frame.screenMissing != Missing::None ? frame.screenMissing
                                                     : Missing::Composed);

        if (sample.toGpu < 0.0)
            return;
    }

    samples[sampleNext] = sample;
    sampleNext = (sampleNext + 1) % kSamples;
}

// Once a second: the most frequent reason frames gave no full figure
inline void RollMissing(uint64_t now)
{
    if (missingSince != 0 && now - missingSince < QpcFrequency())
        return;

    if (missingSince != 0)
    {
        lastMissing = Missing::NoFrames;
        uint32_t most = 0;
        for (size_t i = 0; i < missingCount.size(); i++)
        {
            if (missingCount[i] > most)
            {
                most = missingCount[i];
                lastMissing = (Missing) i;
            }
        }
    }

    missingCount = {};
    missingSince = now;
}
} // namespace detail

// Reflex marker of the game (D3D or Vulkan), observation only. Called when the marker call has returned, after any
// sleep a latency reducer does inside it.
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

// Takes the marker time when the hooked call returns, whatever path it returns by
struct MarkerScope
{
    uint32_t type;
    uint64_t frameId;
    ~MarkerScope() { OnMarker(type, frameId); }
};

// Right before a DirectX 12 Present. Frames generated by frame generation aren't followed, only real ones.
inline void BeforePresent(ID3D12CommandQueue* queue, bool interpolated, bool tearing)
{
    using namespace detail;

    current = -1;

    if (queue == nullptr || interpolated || !Enabled())
        return;

    std::scoped_lock guard(lock);

    const int index = (int) frameNext;
    frameNext = (frameNext + 1) % kFrames;

    // A frame still referenced by a GPU slot is being replaced: that slot must not write into the new one
    for (int slot = 0; slot < kSlots; slot++)
    {
        if (gpu.frameOf[slot] == index)
            gpu.frameOf[slot] = -1;
    }

    Frame& frame = frames[index];
    frame = Frame {};
    frame.open = true;
    frame.tearing = tearing;

    // The frame that sent PRESENT_START last, used once: a second Present without a new marker gets no start time
    const auto& marker = markers[presentingFrame % markers.size()];
    if (presentingFrame != 0 && marker.frameId == presentingFrame)
    {
        frame.input = marker.input;
        frame.simulation = marker.simulation;
    }
    presentingFrame = 0;

    // GPU objects first (a queue change may wait for the old ones), then the time of the Present, then the timestamp.
    // The time is taken before the timestamp is submitted: an idle GPU can execute it at once, and a Present time
    // taken afterwards would come after the end of the GPU work and drop the frame.
    const bool gpuReady = SetupGpu(queue);
    frame.present = Qpc();

    // End of the GPU work: a timestamp after everything already on the present queue
    if (gpuReady)
    {
        const int slot = gpu.next;

        if (gpu.fenceValues[slot] == 0 || gpu.fence->GetCompletedValue() >= gpu.fenceValues[slot])
        {
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
    else if (current >= 0)
        frames[current].open = false; // the Present failed or has no number: it can't be followed

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

    // The refresh the last shown Present reached. The media statistics also say whether Windows composed it. The
    // interface is released at once: holding it would keep the game's swapchain alive after the game drops it.
    DXGI_FRAME_STATISTICS stats {};
    DXGI_FRAME_STATISTICS_MEDIA mediaStats {};
    bool haveStats = false;
    bool haveMedia = false;

    if (IDXGISwapChainMedia* media = nullptr; swapChain->QueryInterface(IID_PPV_ARGS(&media)) == S_OK && media != nullptr)
    {
        haveMedia = media->GetFrameStatisticsMedia(&mediaStats) == S_OK && mediaStats.PresentCount != 0;
        media->Release();
    }

    if (haveMedia)
    {
        haveStats = true;
        stats.PresentCount = mediaStats.PresentCount;
        stats.SyncQPCTime = mediaStats.SyncQPCTime;
        presentation = (int) mediaStats.CompositionMode;
    }
    else
    {
        haveStats = swapChain->GetFrameStatistics(&stats) == S_OK && stats.PresentCount != 0;
        presentation = -1;
    }

    const uint64_t now = Qpc();

    // How many Presents reach the screen per second
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

        // Tearing and not composed: shown as soon as it is flipped, during a scanout DXGI gives no time for
        if (frame.tearing && presentation != 0 && !frame.screenDone)
        {
            frame.screenDone = true;
            frame.screenMissing = Missing::Tearing;
        }

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
                frame.screenMissing = Missing::RefreshNotSeen;
            }
        }

        // Closed once its GPU timestamp is in and its screen time is known or can't be known
        if (!frame.gpuPending && (frame.screenDone || !haveStats))
            CloseFrame(frame);
    }

    RollMissing(now);
}

// reducerId: the active latency reducer and whether it is on; a change drops the samples and waits half a second
inline Result Get(int reducerId)
{
    using namespace detail;

    std::scoped_lock guard(lock);

    const uint64_t now = Qpc();

    if (reducerId != reducer)
    {
        if (reducer != -1)
        {
            ClearSamples();
            settleUntil = now + QpcFrequency() / 2;
        }

        reducer = reducerId;
    }

    Result result;
    result.displayedFps = displayedFps;
    result.presentation = presentation;
    result.missing = now < settleUntil ? Missing::Settling : lastMissing;

    // Recent samples only: a meter that stops getting frames goes back to waiting instead of showing old values
    const uint64_t oldest = now > QpcFrequency() ? now - QpcFrequency() : 0;

    int screenCount = 0, splitCount = 0, gpuCount = 0;
    for (const auto& sample : samples)
    {
        if (sample.at == 0 || sample.at < oldest)
            continue;

        screenCount += sample.onScreen;
        splitCount += sample.onScreen && sample.display >= 0.0;
        gpuCount += sample.toGpu >= 0.0;
    }

    // Which frames the means come from: every one of them has every part shown, so the parts add up
    enum
    {
        None,
        Split,
        ScreenOnly,
        ToGpu
    } set = None;

    if (splitCount >= 4 && splitCount * 2 >= screenCount)
        set = Split;
    else if (screenCount >= 4)
        set = ScreenOnly;
    else if (gpuCount >= 4)
        set = ToGpu;

    if (set == None)
        return result;

    double system = 0, cpu = 0, gpuMs = 0, display = 0, afterPresent = 0, input = 0;
    int n = 0, inputs = 0;

    for (const auto& sample : samples)
    {
        if (sample.at == 0 || sample.at < oldest)
            continue;

        if ((set == Split && !(sample.onScreen && sample.display >= 0.0)) || (set == ScreenOnly && !sample.onScreen) ||
            (set == ToGpu && sample.toGpu < 0.0))
            continue;

        n++;
        cpu += sample.cpu;

        if (set == ToGpu)
        {
            system += sample.toGpu;
            gpuMs += sample.gpu;
            continue;
        }

        system += sample.system;
        afterPresent += sample.afterPresent;

        if (set == Split)
        {
            gpuMs += sample.gpu;
            display += sample.display;
        }

        if (sample.input >= 0.0)
        {
            input += sample.input;
            inputs++;
        }
    }

    result.valid = true;
    result.frames = n;
    result.onScreen = set != ToGpu;
    result.system = system / n;
    result.cpu = cpu / n;

    if (set == ToGpu || set == Split)
        result.gpu = gpuMs / n;

    if (set == Split)
        result.display = display / n;
    else if (set == ScreenOnly)
        result.afterPresent = afterPresent / n;

    // Input to screen only when most of these frames have that marker
    if (set != ToGpu && inputs * 2 >= n)
        result.inputToScreen = input / inputs;

    if (set == Split)
        result.missing = Missing::None;

    return result;
}
} // namespace IlionLatency
