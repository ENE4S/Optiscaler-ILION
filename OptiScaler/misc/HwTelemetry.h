#pragma once

#include <windows.h>
#include <atomic>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <mutex>
#include <string>
#include <thread>

// Hardware readings for the performance overlay.
//
// Nothing is measured here. Values are read from the shared memory that HWiNFO and MSI Afterburner publish
// when they are running, about once per second, on a thread of its own. A reading that no source provides
// stays invalid and is shown as not available, it is never estimated.
//
// MSI Afterburner: format from MAHMSharedMemory.h of its SDK, sensors are identified by their source id.
// HWiNFO: version 2 of its sensors shared memory, needs "Shared Memory Support" enabled in HWiNFO (limited to
// 12 hours per run in the free version). Sensors are identified by their original label and reading type.
// Windows: only total CPU usage (GetSystemTimes), as a fallback.
class HwTelemetry
{
  public:
    enum class Source : int
    {
        None,
        HWiNFO,
        Afterburner,
        Windows,
        Adapter, // dedicated memory reported by the graphics adapter
    };

    enum Metric : int
    {
        GpuUsage,   // %
        GpuTemp,    // C
        GpuHotspot, // C
        VramUsed,   // MB
        VramTotal,  // MB
        CpuUsage,   // %
        CpuTemp,    // C
        MetricCount,
    };

    struct Value
    {
        bool valid = false;
        float value = 0.0f;
        Source source = Source::None;

        // Name of the sensor as the source calls it
        char label[64] = {};
    };

    struct Snapshot
    {
        Value values[MetricCount];
        bool hwinfoAvailable = false;
        bool afterburnerAvailable = false;

        // GetTickCount64 of the poll, 0 when nothing was polled yet
        uint64_t polledTick = 0;
    };

    enum class Preferred : int
    {
        Auto, // HWiNFO first, then MSI Afterburner
        HWiNFO,
        Afterburner,
    };

    static const char* SourceName(Source source)
    {
        switch (source)
        {
        case Source::HWiNFO:
            return "HWiNFO";
        case Source::Afterburner:
            return "MSI Afterburner";
        case Source::Windows:
            return "Windows";
        case Source::Adapter:
            return "DXGI";
        default:
            return "-";
        }
    }

    // Readings are wanted, e.g. a hardware line of the overlay is enabled
    static void SetEnabled(bool enabled)
    {
        _enabled.store(enabled, std::memory_order_relaxed);

        if (enabled)
            EnsureThread();
    }

    // Readings are wanted for a short while, e.g. the settings that show the sources are on screen
    static void KeepAlive()
    {
        _keepAliveTick.store(GetTickCount64(), std::memory_order_relaxed);
        EnsureThread();
    }

    static void SetPreferred(Preferred preferred) { _preferred.store((int) preferred, std::memory_order_relaxed); }

    // Helps to pick the right GPU when the sources list more than one, and gives the total VRAM
    static void SetGpuHint(const std::string& gpuName, uint64_t dedicatedVramBytes)
    {
        std::scoped_lock lock(_mutex);
        _gpuNameHint = gpuName;
        _vramTotalHintMB = (float) (dedicatedVramBytes / (1024.0 * 1024.0));
    }

    static Snapshot Get()
    {
        std::scoped_lock lock(_mutex);
        return _snapshot;
    }

    // One read of every source, used by the polling thread
    static Snapshot Poll(Preferred preferred, const std::string& gpuNameHint, float vramTotalHintMB)
    {
        Snapshot hwinfo;
        Snapshot afterburner;

        const bool hwinfoOk = ReadHwinfo(hwinfo, gpuNameHint);
        const bool afterburnerOk = ReadAfterburner(afterburner, gpuNameHint);

        Snapshot result;
        result.hwinfoAvailable = hwinfoOk;
        result.afterburnerAvailable = afterburnerOk;
        result.polledTick = GetTickCount64();

        const Snapshot* first = preferred == Preferred::Afterburner ? &afterburner : &hwinfo;
        const Snapshot* second = preferred == Preferred::Afterburner ? &hwinfo : &afterburner;

        // An explicit choice doesn't fall back to the other program
        if (preferred != Preferred::Auto)
            second = nullptr;

        for (int i = 0; i < MetricCount; i++)
        {
            if (first->values[i].valid)
                result.values[i] = first->values[i];
            else if (second != nullptr && second->values[i].valid)
                result.values[i] = second->values[i];
        }

        if (!result.values[CpuUsage].valid)
            ReadWindowsCpuUsage(result.values[CpuUsage]);

        if (!result.values[VramTotal].valid && vramTotalHintMB > 0.0f)
            SetValue(result.values[VramTotal], vramTotalHintMB, Source::Adapter, "Dedicated video memory");

        // Values that can't be real aren't shown: more memory in use than the card has, or out of range readings
        if (result.values[VramUsed].valid && result.values[VramTotal].valid &&
            result.values[VramUsed].value > result.values[VramTotal].value * 1.05f)
        {
            result.values[VramUsed].valid = false;
        }

        for (const Metric metric : { GpuUsage, CpuUsage })
        {
            if (result.values[metric].valid && (result.values[metric].value < 0.0f || result.values[metric].value > 100.5f))
                result.values[metric].valid = false;
        }

        for (const Metric metric : { GpuTemp, GpuHotspot, CpuTemp })
        {
            if (result.values[metric].valid && (result.values[metric].value <= 0.0f || result.values[metric].value > 150.0f))
                result.values[metric].valid = false;
        }

        return result;
    }

  private:
    inline static std::mutex _mutex;
    inline static Snapshot _snapshot;
    inline static std::string _gpuNameHint;
    inline static float _vramTotalHintMB = 0.0f;

    inline static std::atomic<bool> _enabled { false };
    inline static std::atomic<uint64_t> _keepAliveTick { 0 };
    inline static std::atomic<int> _preferred { 0 };
    inline static std::atomic<bool> _threadStarted { false };

    static void EnsureThread()
    {
        if (_threadStarted.exchange(true))
            return;

        std::thread(
            []
            {
                while (true)
                {
                    const bool wanted = _enabled.load(std::memory_order_relaxed) ||
                                        GetTickCount64() - _keepAliveTick.load(std::memory_order_relaxed) < 3000;

                    if (wanted)
                    {
                        std::string gpuName;
                        float vramTotal = 0.0f;

                        {
                            std::scoped_lock lock(_mutex);
                            gpuName = _gpuNameHint;
                            vramTotal = _vramTotalHintMB;
                        }

                        const auto snapshot =
                            Poll((Preferred) _preferred.load(std::memory_order_relaxed), gpuName, vramTotal);

                        std::scoped_lock lock(_mutex);
                        _snapshot = snapshot;
                    }

                    // HWiNFO and MSI Afterburner refresh at their own pace (2 s and 1 s by default), reading
                    // twice per second only shortens the wait for a new value
                    Sleep(500);
                }
            })
            .detach();
    }

    static void SetValue(Value& target, float value, Source source, const char* label)
    {
        target.valid = true;
        target.value = value;
        target.source = source;
        strncpy_s(target.label, label, _TRUNCATE);
    }

    static bool ContainsNoCase(const char* text, const char* needle)
    {
        const size_t needleLength = strlen(needle);

        if (needleLength == 0)
            return true;

        for (const char* start = text; *start != 0; start++)
        {
            if (_strnicmp(start, needle, needleLength) == 0)
                return true;
        }

        return false;
    }

    // Mapped view of a named section that some other program owns, with the size that can be read
    struct SharedView
    {
        HANDLE mapping = nullptr;
        const BYTE* data = nullptr;
        size_t size = 0;

        bool Open(const wchar_t* name)
        {
            mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, name);

            if (mapping == nullptr)
                return false;

            data = (const BYTE*) MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);

            if (data == nullptr)
                return false;

            MEMORY_BASIC_INFORMATION mbi {};

            if (VirtualQuery(data, &mbi, sizeof(mbi)) == 0)
                return false;

            size = mbi.RegionSize;
            return true;
        }

        ~SharedView()
        {
            if (data != nullptr)
                UnmapViewOfFile(data);

            if (mapping != nullptr)
                CloseHandle(mapping);
        }
    };

    // HWiNFO -----------------------------

#pragma pack(push, 1)
    struct HwinfoHeader
    {
        DWORD signature;
        DWORD version;
        DWORD revision;
        __time64_t pollTime;
        DWORD offsetOfSensorSection;
        DWORD sizeOfSensorElement;
        DWORD numSensorElements;
        DWORD offsetOfReadingSection;
        DWORD sizeOfReadingElement;
        DWORD numReadingElements;
    };

    struct HwinfoSensor
    {
        DWORD sensorId;
        DWORD sensorInstance;
        char nameOriginal[128];
        char nameUser[128];
    };

    struct HwinfoReading
    {
        DWORD type;
        DWORD sensorIndex;
        DWORD readingId;
        char labelOriginal[128];
        char labelUser[128];
        char unit[16];
        double value;
        double valueMin;
        double valueMax;
        double valueAvg;
    };
#pragma pack(pop)

    static constexpr DWORD HwinfoSignature = 0x53695748; // "HWiS"
    static constexpr DWORD HwinfoTypeTemperature = 1;
    static constexpr DWORD HwinfoTypeUsage = 7;
    static constexpr DWORD HwinfoTypeOther = 8;

    struct Candidate
    {
        int score = 0;
        float value = 0.0f;
        char label[64] = {};
    };

    static void Offer(Candidate& candidate, int score, double value, const char* label)
    {
        if (score <= candidate.score || !std::isfinite(value))
            return;

        candidate.score = score;
        candidate.value = (float) value;
        strncpy_s(candidate.label, label, _TRUNCATE);
    }

    static bool ReadHwinfo(Snapshot& out, const std::string& gpuNameHint)
    {
        SharedView view;

        if (!view.Open(L"Global\\HWiNFO_SENS_SM2") || view.size < sizeof(HwinfoHeader))
            return false;

        // HWiNFO rewrites the readings on every poll of its own, its mutex keeps a read from landing in between
        HANDLE mutex = OpenMutexW(SYNCHRONIZE, FALSE, L"Global\\HWiNFO_SM2_MUTEX");
        bool locked = false;

        if (mutex != nullptr)
        {
            const DWORD wait = WaitForSingleObject(mutex, 50);
            locked = wait == WAIT_OBJECT_0 || wait == WAIT_ABANDONED;

            if (!locked)
            {
                CloseHandle(mutex);
                return false;
            }
        }

        const bool ok = ParseHwinfo(view, out, gpuNameHint);

        if (mutex != nullptr)
        {
            if (locked)
                ReleaseMutex(mutex);

            CloseHandle(mutex);
        }

        return ok;
    }

    static bool ParseHwinfo(const SharedView& view, Snapshot& out, const std::string& gpuNameHint)
    {
        HwinfoHeader header;
        memcpy(&header, view.data, sizeof(header));

        if (header.signature != HwinfoSignature)
            return false;

        // Sizes and offsets come from the section itself, everything has to fit in what's mapped
        if (header.sizeOfSensorElement < sizeof(HwinfoSensor) || header.sizeOfReadingElement < sizeof(HwinfoReading) ||
            header.numSensorElements > 4096 || header.numReadingElements > 65536)
        {
            return false;
        }

        const uint64_t sensorsEnd =
            (uint64_t) header.offsetOfSensorSection + (uint64_t) header.sizeOfSensorElement * header.numSensorElements;
        const uint64_t readingsEnd = (uint64_t) header.offsetOfReadingSection +
                                     (uint64_t) header.sizeOfReadingElement * header.numReadingElements;

        if (sensorsEnd > view.size || readingsEnd > view.size)
            return false;

        // Readings that HWiNFO stopped updating aren't shown as current
        const __time64_t now = _time64(nullptr);

        if (header.pollTime <= 0 || now - header.pollTime > 15)
            return false;

        Candidate candidates[MetricCount];

        for (DWORD i = 0; i < header.numReadingElements; i++)
        {
            HwinfoReading reading;
            memcpy(&reading, view.data + header.offsetOfReadingSection + (size_t) i * header.sizeOfReadingElement,
                   sizeof(reading));

            reading.labelOriginal[sizeof(reading.labelOriginal) - 1] = 0;
            reading.unit[sizeof(reading.unit) - 1] = 0;

            if (reading.sensorIndex >= header.numSensorElements)
                continue;

            HwinfoSensor sensor;
            memcpy(&sensor,
                   view.data + header.offsetOfSensorSection + (size_t) reading.sensorIndex * header.sizeOfSensorElement,
                   sizeof(sensor));

            sensor.nameOriginal[sizeof(sensor.nameOriginal) - 1] = 0;

            const char* label = reading.labelOriginal;
            const bool gpuSensor = ContainsNoCase(sensor.nameOriginal, "GPU");

            // The GPU the game uses wins over an integrated one
            const int hintBonus =
                gpuSensor && !gpuNameHint.empty() && ContainsNoCase(sensor.nameOriginal, gpuNameHint.c_str()) ? 20 : 0;

            if (reading.type == HwinfoTypeTemperature)
            {
                if (_stricmp(label, "GPU Temperature") == 0)
                    Offer(candidates[GpuTemp], 100 + hintBonus, reading.value, label);

                // The overall hot spot wins over the per die ones some cards also list (GCD, MCD1..)
                if (_stricmp(label, "GPU Hot Spot Temperature") == 0 || _stricmp(label, "GPU Hotspot Temperature") == 0)
                {
                    Offer(candidates[GpuHotspot], 100 + hintBonus, reading.value, label);
                }
                else if (gpuSensor && (ContainsNoCase(label, "Hot Spot") || ContainsNoCase(label, "Hotspot")) &&
                         !ContainsNoCase(label, "Memory"))
                {
                    Offer(candidates[GpuHotspot], 50 + hintBonus, reading.value, label);
                }

                if (!gpuSensor)
                {
                    if (_stricmp(label, "CPU (Tctl/Tdie)") == 0)
                        Offer(candidates[CpuTemp], 100, reading.value, label);
                    else if (_stricmp(label, "CPU Package") == 0)
                        Offer(candidates[CpuTemp], 95, reading.value, label);
                    else if (_stricmp(label, "CPU Die (average)") == 0)
                        Offer(candidates[CpuTemp], 90, reading.value, label);
                    else if (_stricmp(label, "Core Temperatures") == 0)
                        Offer(candidates[CpuTemp], 80, reading.value, label);
                    else if (_stricmp(label, "CPU") == 0)
                        Offer(candidates[CpuTemp], 60, reading.value, label);
                }
            }
            else if (reading.type == HwinfoTypeUsage)
            {
                if (gpuSensor)
                {
                    if (_stricmp(label, "GPU Core Load") == 0)
                        Offer(candidates[GpuUsage], 100 + hintBonus, reading.value, label);
                    else if (_stricmp(label, "GPU Utilization") == 0)
                        Offer(candidates[GpuUsage], 95 + hintBonus, reading.value, label);
                    else if (_stricmp(label, "GPU D3D Usage") == 0)
                        Offer(candidates[GpuUsage], 60 + hintBonus, reading.value, label);
                }
                else if (_stricmp(label, "Total CPU Usage") == 0)
                {
                    Offer(candidates[CpuUsage], 100, reading.value, label);
                }
            }

            // Only values in MB are taken. Dedicated memory as Windows reports it comes first: it exists for every
            // vendor, and "GPU Memory Usage" was seen reporting more than the card has on an RX 7900 XTX.
            if (gpuSensor && _stricmp(reading.unit, "MB") == 0)
            {
                if (_stricmp(label, "GPU D3D Memory Dedicated") == 0)
                    Offer(candidates[VramUsed], 100 + hintBonus, reading.value, label);
                else if (_stricmp(label, "GPU Memory Allocated") == 0)
                    Offer(candidates[VramUsed], 90 + hintBonus, reading.value, label);
                else if (_stricmp(label, "GPU Memory Usage") == 0)
                    Offer(candidates[VramUsed], 60 + hintBonus, reading.value, label);
            }
        }

        for (int i = 0; i < MetricCount; i++)
        {
            if (candidates[i].score > 0)
                SetValue(out.values[i], candidates[i].value, Source::HWiNFO, candidates[i].label);
        }

        return true;
    }

    // MSI Afterburner -----------------------------

    // Layout of MAHMSharedMemory.h v2.0, time is 32 bit in the section for both 32 and 64 bit readers
    struct AfterburnerHeader
    {
        DWORD signature;
        DWORD version;
        DWORD headerSize;
        DWORD numEntries;
        DWORD entrySize;
        __time32_t time;
        DWORD numGpuEntries;
        DWORD gpuEntrySize;
    };

    struct AfterburnerEntry
    {
        char srcName[MAX_PATH];
        char srcUnits[MAX_PATH];
        char localizedSrcName[MAX_PATH];
        char localizedSrcUnits[MAX_PATH];
        char recommendedFormat[MAX_PATH];
        float data;
        float minLimit;
        float maxLimit;
        DWORD flags;
        DWORD gpu;
        DWORD srcId;
    };

    struct AfterburnerGpuEntry
    {
        char gpuId[MAX_PATH];
        char family[MAX_PATH];
        char device[MAX_PATH];
        char driver[MAX_PATH];
        char bios[MAX_PATH];
        DWORD memAmount; // KB
    };

    static constexpr DWORD AfterburnerSignature = 0x4D41484D; // 'MAHM'
    static constexpr DWORD AfterburnerIdGpuTemperature = 0x00000000;
    static constexpr DWORD AfterburnerIdGpuUsage = 0x00000030;
    static constexpr DWORD AfterburnerIdMemoryUsage = 0x00000031;
    static constexpr DWORD AfterburnerIdCpuTemperature = 0x00000080;
    static constexpr DWORD AfterburnerIdCpuUsage = 0x00000090;
    static constexpr DWORD AfterburnerGlobalSource = 0xFFFFFFFF;

    static bool ReadAfterburner(Snapshot& out, const std::string& gpuNameHint)
    {
        SharedView view;

        if (!view.Open(L"MAHMSharedMemory") || view.size < sizeof(AfterburnerHeader))
            return false;

        AfterburnerHeader header;
        memcpy(&header, view.data, sizeof(header));

        // Source ids exist from v2.0 on
        if (header.signature != AfterburnerSignature || header.version < 0x00020000 ||
            header.headerSize < sizeof(AfterburnerHeader) || header.entrySize < sizeof(AfterburnerEntry) ||
            header.numEntries > 4096 || header.numGpuEntries > 64)
        {
            return false;
        }

        const uint64_t entriesEnd = (uint64_t) header.headerSize + (uint64_t) header.entrySize * header.numEntries;
        const uint64_t gpuEntriesEnd = entriesEnd + (uint64_t) header.gpuEntrySize * header.numGpuEntries;

        if (entriesEnd > view.size)
            return false;

        const __time32_t now = _time32(nullptr);

        if (header.time <= 0 || now - header.time > 15)
            return false;

        // GPU the readings are taken from, the one the game uses when it can be told
        DWORD gpuIndex = 0;
        const bool gpuEntriesValid =
            header.gpuEntrySize >= sizeof(AfterburnerGpuEntry) && gpuEntriesEnd <= view.size;

        if (gpuEntriesValid && !gpuNameHint.empty())
        {
            for (DWORD i = 0; i < header.numGpuEntries; i++)
            {
                AfterburnerGpuEntry gpu;
                memcpy(&gpu, view.data + entriesEnd + (size_t) i * header.gpuEntrySize, sizeof(gpu));
                gpu.device[MAX_PATH - 1] = 0;

                if (gpu.device[0] != 0 &&
                    (ContainsNoCase(gpu.device, gpuNameHint.c_str()) || ContainsNoCase(gpuNameHint.c_str(), gpu.device)))
                {
                    gpuIndex = i;
                    break;
                }
            }
        }

        for (DWORD i = 0; i < header.numEntries; i++)
        {
            AfterburnerEntry entry;
            memcpy(&entry, view.data + header.headerSize + (size_t) i * header.entrySize, sizeof(entry));

            entry.srcName[MAX_PATH - 1] = 0;
            entry.srcUnits[MAX_PATH - 1] = 0;

            // FLT_MAX means the source has no data at the moment
            if (entry.data == FLT_MAX || !std::isfinite(entry.data))
                continue;

            const bool forGpu = entry.gpu == gpuIndex;

            switch (entry.srcId)
            {
            case AfterburnerIdGpuTemperature:
                if (forGpu)
                    SetValue(out.values[GpuTemp], entry.data, Source::Afterburner, entry.srcName);
                break;

            case AfterburnerIdGpuUsage:
                if (forGpu)
                    SetValue(out.values[GpuUsage], entry.data, Source::Afterburner, entry.srcName);
                break;

            case AfterburnerIdMemoryUsage:
                if (forGpu && _stricmp(entry.srcUnits, "MB") == 0)
                    SetValue(out.values[VramUsed], entry.data, Source::Afterburner, entry.srcName);
                break;

            case AfterburnerIdCpuTemperature:
                SetValue(out.values[CpuTemp], entry.data, Source::Afterburner, entry.srcName);
                break;

            case AfterburnerIdCpuUsage:
                SetValue(out.values[CpuUsage], entry.data, Source::Afterburner, entry.srcName);
                break;

            default:
                break;
            }
        }

        if (gpuEntriesValid && gpuIndex < header.numGpuEntries)
        {
            AfterburnerGpuEntry gpu;
            memcpy(&gpu, view.data + entriesEnd + (size_t) gpuIndex * header.gpuEntrySize, sizeof(gpu));

            if (gpu.memAmount > 0)
                SetValue(out.values[VramTotal], gpu.memAmount / 1024.0f, Source::Afterburner, "On-board memory");
        }

        return true;
    }

    // Windows -----------------------------

    static void ReadWindowsCpuUsage(Value& out)
    {
        static uint64_t lastIdle = 0;
        static uint64_t lastTotal = 0;

        FILETIME idleTime, kernelTime, userTime;

        if (!GetSystemTimes(&idleTime, &kernelTime, &userTime))
            return;

        const auto toUint = [](const FILETIME& time)
        { return ((uint64_t) time.dwHighDateTime << 32) | time.dwLowDateTime; };

        // Kernel time includes idle time
        const uint64_t idle = toUint(idleTime);
        const uint64_t total = toUint(kernelTime) + toUint(userTime);

        const uint64_t idleDelta = idle - lastIdle;
        const uint64_t totalDelta = total - lastTotal;
        const bool hasPrevious = lastTotal != 0;

        lastIdle = idle;
        lastTotal = total;

        // The first call has nothing to compare with
        if (!hasPrevious || totalDelta == 0)
            return;

        SetValue(out, (float) (100.0 * (double) (totalDelta - idleDelta) / (double) totalDelta), Source::Windows,
                 "GetSystemTimes");
    }
};
