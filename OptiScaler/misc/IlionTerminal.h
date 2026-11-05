#pragma once

// ILION TERMINAL: technical telemetry for the Home screen (replaces ILION Advisor's box). Read only, nothing is set.
//
// Sources, in this order, and only real readings (a value no source gives stays invalid and is shown as N/A):
//   GPU AMD     ADLX (amdadlx64.dll, installed with the Radeon driver): IADLXPerformanceMonitoringServices::
//               GetCurrentGPUMetrics. Interfaces declared by their vtable order (ADLX SDK, MIT: ISystem.h,
//               IPerformanceMonitoring.h), as misc/IlionAfmf.h does for AFMF.
//   GPU NVIDIA  NVML (nvml.dll, installed with the GeForce driver), its documented C API.
//   CPU, RAM    Windows: GetSystemTimes, GlobalMemoryStatusEx, GetPhysicallyInstalledSystemMemory, GetPerformanceInfo,
//               PDH «Processor Information» (effective clock, as Task Manager shows it).
//   Optional    HWiNFO / MSI Afterburner shared memory through misc/HwTelemetry.h (CPU temperature and power, and any
//               GPU reading the native source lacks). Never required, never downloaded or started.
// One background thread reads about once a second, only while the terminal is on screen (KeepAlive). Self-contained
// (Win32 + STL + HwTelemetry.h) so it can be tested outside the game.

#include <windows.h>
#include <psapi.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>

#include "HwTelemetry.h"

#pragma execution_character_set(push, "utf-8")

namespace IlionTerminal
{
struct Reading
{
    bool valid = false;
    double value = 0.0;
    const char* source = ""; // ADLX, NVML, Windows, HWiNFO, MSI Afterburner, DXGI
};

struct Sensors
{
    // GPU
    std::string gpuName, gpuSource; // the native source that answered, or why none did
    Reading gpuTemp, gpuHotspot, gpuIntake, gpuUsage, gpuClock, gpuMemClock, gpuVoltage;
    Reading gpuChipPower;  // ADLX GPUPower: the GPU (ASIC) power
    Reading gpuBoardPower; // ADLX GPUTotalBoardPower / NVML power usage: the whole card
    Reading vramUsed, vramTotal, vramFree; // MB
    Reading fanRpm, fanPercent;
    // CPU
    std::string cpuName;
    int cores = 0, threads = 0;
    Reading cpuUsage, cpuClock, cpuBase, cpuTemp, cpuPower;
    // Memory (MB)
    Reading ramTotal, ramUsed, ramFree, ramLoad, ramReserved, commitUsed, commitLimit, cache;
    // System
    Reading uptimeS, processes, threadsAll, handles;
    bool adlx = false, nvml = false, pdh = false, hwinfo = false, afterburner = false;
    uint64_t tick = 0; // GetTickCount64 of the reading, 0 before the first one
};

namespace detail
{
inline void Set(Reading& r, double v, const char* source)
{
    r.valid = true;
    r.value = v;
    r.source = source;
}

// ---- ADLX ------------------------------------------------------------------------------------------------------------
class Adlx
{
  public:
    // Reads the metrics of the first AMD GPU; false when ADLX can't be used (no Radeon driver, interface missing)
    bool Read(Sensors& s)
    {
        if (!Open(s.gpuSource))
            return false;
        s.gpuName = name;
        s.adlx = true;
        s.gpuSource = "ADLX";
        if (vramTotalMB > 0)
            Set(s.vramTotal, vramTotalMB, "ADLX");

        // IADLXPerformanceMonitoringServices::GetCurrentGPUMetrics is entry 18
        void* metrics = nullptr;
        if (!Ok(Slot<Result(__stdcall*)(void*, void*, void**)>(perf, 18)(perf, gpu, &metrics)) || metrics == nullptr)
            return true;
        // IADLXGPUMetrics: GPUUsage 4, GPUClockSpeed 5, GPUVRAMClockSpeed 6, GPUTemperature 7, GPUHotspotTemperature 8,
        // GPUPower 9, GPUTotalBoardPower 10, GPUFanSpeed 11, GPUVRAM 12, GPUVoltage 13, GPUIntakeTemperature 14
        const auto d = [&](bool supported, int slot, Reading& to)
        {
            double v = 0.0;
            if (supported && Ok(Slot<Result(__stdcall*)(void*, double*)>(metrics, slot)(metrics, &v)))
                Set(to, v, "ADLX");
        };
        const auto i = [&](bool supported, int slot, Reading& to)
        {
            int v = 0;
            if (supported && Ok(Slot<Result(__stdcall*)(void*, int*)>(metrics, slot)(metrics, &v)))
                Set(to, v, "ADLX");
        };
        d(sup[0], 4, s.gpuUsage);
        i(sup[1], 5, s.gpuClock);
        i(sup[2], 6, s.gpuMemClock);
        d(sup[3], 7, s.gpuTemp);
        d(sup[4], 8, s.gpuHotspot);
        d(sup[5], 9, s.gpuChipPower);
        d(sup[6], 10, s.gpuBoardPower);
        i(sup[7], 11, s.fanRpm);
        i(sup[8], 12, s.vramUsed);
        i(sup[9], 13, s.gpuVoltage);
        if (sup[10])
            d(true, 14, s.gpuIntake);
        Slot<long(__stdcall*)(void*)>(metrics, 1)(metrics); // Release
        if (s.vramUsed.valid && s.vramTotal.valid)
            Set(s.vramFree, (std::max)(0.0, s.vramTotal.value - s.vramUsed.value), "ADLX");
        return true;
    }

  private:
    using Result = int;
    static bool Ok(Result r) { return r == 0 || r == 1 || r == 2; } // OK, ALREADY_ENABLED, ALREADY_INITIALIZED

    template <typename T> static T Slot(void* object, int index) { return reinterpret_cast<T>((*reinterpret_cast<void***>(object))[index]); }

    bool Open(std::string& why)
    {
        if (perf != nullptr)
            return true;
        if (failed)
        {
            why = failure;
            return false;
        }
        failed = true;
        HMODULE dll = LoadLibraryExW(L"amdadlx64.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (dll == nullptr)
            return failure = why = "ADLX no disponible (sin driver Radeon)", false;
        using QueryFullVersionFn = Result(__cdecl*)(uint64_t*);
        using InitializeFn = Result(__cdecl*)(uint64_t, void**);
        const auto queryVersion = reinterpret_cast<QueryFullVersionFn>(GetProcAddress(dll, "ADLXQueryFullVersion"));
        const auto initialize = reinterpret_cast<InitializeFn>(GetProcAddress(dll, "ADLXInitialize"));
        uint64_t version = 0;
        void* system = nullptr;
        if (queryVersion == nullptr || initialize == nullptr || !Ok(queryVersion(&version)) || !Ok(initialize(version, &system)) || system == nullptr)
            return failure = why = "ADLX no se pudo iniciar", false;
        // IADLXSystem: GetGPUs 1, GetPerformanceMonitoringServices 9
        void* gpus = nullptr;
        if (!Ok(Slot<Result(__stdcall*)(void*, void**)>(system, 1)(system, &gpus)) || gpus == nullptr)
            return failure = why = "ADLX: sin GPU AMD", false;
        // IADLXGPUList: Size 3, At 11 (the first GPU of the list, as Adrenalin's main one)
        if (Slot<unsigned(__stdcall*)(void*)>(gpus, 3)(gpus) == 0 || !Ok(Slot<Result(__stdcall*)(void*, unsigned, void**)>(gpus, 11)(gpus, 0, &gpu)) ||
            gpu == nullptr)
            return failure = why = "ADLX: sin GPU AMD", false;
        // IADLXGPU: Name 7, TotalVRAM 11
        const char* n = nullptr;
        if (Ok(Slot<Result(__stdcall*)(void*, const char**)>(gpu, 7)(gpu, &n)) && n != nullptr)
            name = n;
        unsigned vram = 0;
        if (Ok(Slot<Result(__stdcall*)(void*, unsigned*)>(gpu, 11)(gpu, &vram)))
            vramTotalMB = vram;
        if (!Ok(Slot<Result(__stdcall*)(void*, void**)>(system, 9)(system, &perf)) || perf == nullptr)
            return failure = why = "ADLX: sin monitorización de rendimiento", false;
        // IADLXPerformanceMonitoringServices::GetSupportedGPUMetrics is entry 21; IADLXGPUMetricsSupport: IsSupported…
        // Usage 3, ClockSpeed 4, VRAMClockSpeed 5, Temperature 6, Hotspot 7, Power 8, TotalBoardPower 9, FanSpeed 10,
        // VRAM 11, Voltage 12, IntakeTemperature 24
        void* support = nullptr;
        if (Ok(Slot<Result(__stdcall*)(void*, void*, void**)>(perf, 21)(perf, gpu, &support)) && support != nullptr)
        {
            const int slots[11] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 24 };
            for (int k = 0; k < 11; k++)
            {
                uint8_t on = 0;
                sup[k] = Ok(Slot<Result(__stdcall*)(void*, uint8_t*)>(support, slots[k])(support, &on)) && on != 0;
            }
            Slot<long(__stdcall*)(void*)>(support, 1)(support);
        }
        failed = false;
        return true; // ADLX objects live for the process (never released at unload: loader lock)
    }

    void* perf = nullptr;
    void* gpu = nullptr;
    bool sup[11] {};
    std::string name;
    unsigned vramTotalMB = 0;
    bool failed = false;
    std::string failure;
};

// ---- NVML (documented C API, not tested on this machine) -------------------------------------------------------------
class Nvml
{
  public:
    bool Read(Sensors& s)
    {
        if (!Open())
            return false;
        s.nvml = true;
        s.gpuSource = "NVML";
        s.gpuName = name;
        unsigned v = 0;
        if (getTemp != nullptr && getTemp(dev, 0 /* NVML_TEMPERATURE_GPU */, &v) == 0)
            Set(s.gpuTemp, v, "NVML");
        if (getPower != nullptr && getPower(dev, &v) == 0) // mW, the board (GPU and its circuitry)
            Set(s.gpuBoardPower, v / 1000.0, "NVML");
        struct
        {
            unsigned gpu, memory;
        } util {};
        if (getUtil != nullptr && getUtil(dev, &util) == 0)
            Set(s.gpuUsage, util.gpu, "NVML");
        if (getClock != nullptr && getClock(dev, 0 /* NVML_CLOCK_GRAPHICS */, &v) == 0)
            Set(s.gpuClock, v, "NVML");
        if (getClock != nullptr && getClock(dev, 2 /* NVML_CLOCK_MEM */, &v) == 0)
            Set(s.gpuMemClock, v, "NVML");
        struct
        {
            unsigned long long total, free, used;
        } mem {};
        if (getMem != nullptr && getMem(dev, &mem) == 0)
        {
            Set(s.vramTotal, mem.total / 1048576.0, "NVML");
            Set(s.vramUsed, mem.used / 1048576.0, "NVML");
            Set(s.vramFree, mem.free / 1048576.0, "NVML");
        }
        if (getFan != nullptr && getFan(dev, &v) == 0)
            Set(s.fanPercent, v, "NVML");
        return true;
    }

  private:
    using Fn0 = int(__cdecl*)();
    bool Open()
    {
        if (dev != nullptr)
            return true;
        if (tried)
            return false;
        tried = true;
        HMODULE dll = LoadLibraryExW(L"nvml.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (dll == nullptr)
            return false;
        const auto init = reinterpret_cast<Fn0>(GetProcAddress(dll, "nvmlInit_v2"));
        const auto byIndex = reinterpret_cast<int(__cdecl*)(unsigned, void**)>(GetProcAddress(dll, "nvmlDeviceGetHandleByIndex_v2"));
        const auto getName = reinterpret_cast<int(__cdecl*)(void*, char*, unsigned)>(GetProcAddress(dll, "nvmlDeviceGetName"));
        getTemp = reinterpret_cast<int(__cdecl*)(void*, int, unsigned*)>(GetProcAddress(dll, "nvmlDeviceGetTemperature"));
        getPower = reinterpret_cast<int(__cdecl*)(void*, unsigned*)>(GetProcAddress(dll, "nvmlDeviceGetPowerUsage"));
        getUtil = reinterpret_cast<int(__cdecl*)(void*, void*)>(GetProcAddress(dll, "nvmlDeviceGetUtilizationRates"));
        getClock = reinterpret_cast<int(__cdecl*)(void*, int, unsigned*)>(GetProcAddress(dll, "nvmlDeviceGetClockInfo"));
        getMem = reinterpret_cast<int(__cdecl*)(void*, void*)>(GetProcAddress(dll, "nvmlDeviceGetMemoryInfo"));
        getFan = reinterpret_cast<int(__cdecl*)(void*, unsigned*)>(GetProcAddress(dll, "nvmlDeviceGetFanSpeed"));
        if (init == nullptr || byIndex == nullptr || init() != 0 || byIndex(0, &dev) != 0)
        {
            dev = nullptr;
            return false;
        }
        char n[96] = {};
        if (getName != nullptr && getName(dev, n, sizeof(n)) == 0)
            name = n;
        return true;
    }

    bool tried = false;
    void* dev = nullptr;
    std::string name;
    int(__cdecl* getTemp)(void*, int, unsigned*) = nullptr;
    int(__cdecl* getPower)(void*, unsigned*) = nullptr;
    int(__cdecl* getUtil)(void*, void*) = nullptr;
    int(__cdecl* getClock)(void*, int, unsigned*) = nullptr;
    int(__cdecl* getMem)(void*, void*) = nullptr;
    int(__cdecl* getFan)(void*, unsigned*) = nullptr;
};

// ---- PDH: effective CPU clock (Processor Frequency × % Processor Performance), loaded at run time ----------------------
class Pdh
{
  public:
    bool Read(double& mhz, double& baseMhz)
    {
        if (!Open())
            return false;
        if (collect(query) != 0)
            return false;
        struct
        {
            DWORD status;
            double value;
        } freq {}, perfv {};
        // PdhGetFormattedCounterValue(counter, PDH_FMT_DOUBLE = 0x200, nullptr, &value)
        if (format(freqCounter, 0x200, nullptr, &freq) != 0 || format(perfCounter, 0x200, nullptr, &perfv) != 0)
            return false;
        if (freq.value <= 0.0 || perfv.value <= 0.0)
            return false;
        baseMhz = freq.value;
        mhz = freq.value * perfv.value / 100.0;
        return true;
    }

  private:
    bool Open()
    {
        if (query != nullptr)
            return true;
        if (tried)
            return false;
        tried = true;
        HMODULE dll = LoadLibraryExW(L"pdh.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (dll == nullptr)
            return false;
        const auto open = reinterpret_cast<long(__stdcall*)(const wchar_t*, uintptr_t, void**)>(GetProcAddress(dll, "PdhOpenQueryW"));
        const auto add = reinterpret_cast<long(__stdcall*)(void*, const wchar_t*, uintptr_t, void**)>(GetProcAddress(dll, "PdhAddEnglishCounterW"));
        collect = reinterpret_cast<long(__stdcall*)(void*)>(GetProcAddress(dll, "PdhCollectQueryData"));
        format = reinterpret_cast<long(__stdcall*)(void*, DWORD, DWORD*, void*)>(GetProcAddress(dll, "PdhGetFormattedCounterValue"));
        if (open == nullptr || add == nullptr || collect == nullptr || format == nullptr || open(nullptr, 0, &query) != 0)
            return query = nullptr, false;
        if (add(query, L"\\Processor Information(_Total)\\Processor Frequency", 0, &freqCounter) != 0 ||
            add(query, L"\\Processor Information(_Total)\\% Processor Performance", 0, &perfCounter) != 0)
            return query = nullptr, false;
        collect(query); // the rate counter needs two samples: the first one is taken now
        return true;
    }

    bool tried = false;
    void* query = nullptr;
    void* freqCounter = nullptr;
    void* perfCounter = nullptr;
    long(__stdcall* collect)(void*) = nullptr;
    long(__stdcall* format)(void*, DWORD, DWORD*, void*) = nullptr;
};

inline std::string CpuNameFromRegistry()
{
    char text[128] = {};
    DWORD bytes = sizeof(text) - 1;
    if (RegGetValueA(HKEY_LOCAL_MACHINE, "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", "ProcessorNameString", RRF_RT_REG_SZ, nullptr, text,
                     &bytes) != ERROR_SUCCESS)
        return std::string();
    std::string s = text;
    while (!s.empty() && s.back() == ' ')
        s.pop_back();
    while (!s.empty() && s.front() == ' ')
        s.erase(s.begin());
    return s;
}

inline void CpuTopology(int& cores, int& threads)
{
    DWORD len = 0;
    GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &len);
    std::string buffer(len, '\0');
    if (len == 0 || !GetLogicalProcessorInformationEx(RelationProcessorCore, (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX) buffer.data(), &len))
        return;
    cores = threads = 0;
    for (DWORD at = 0; at < len;)
    {
        const auto* info = (const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*) (buffer.data() + at);
        cores++;
        for (WORD g = 0; g < info->Processor.GroupCount; g++)
            for (KAFFINITY m = info->Processor.GroupMask[g].Mask; m != 0; m &= m - 1)
                threads++;
        at += info->Size;
    }
}
} // namespace detail

// One reading of every source (used by the thread; callable directly in a test)
class Reader
{
  public:
    Sensors Read()
    {
        Sensors s;
        s.tick = GetTickCount64();
        // GPU: the native source of the vendor first
        if (!adlx.Read(s))
            nvml.Read(s);
        if (!s.adlx && !s.nvml && s.gpuSource.empty())
            s.gpuSource = "sin fuente nativa";

        // CPU
        if (cpuName.empty())
        {
            cpuName = detail::CpuNameFromRegistry();
            detail::CpuTopology(cores, threads);
        }
        s.cpuName = cpuName;
        s.cores = cores;
        s.threads = threads;
        FILETIME idle, kernel, user;
        if (GetSystemTimes(&idle, &kernel, &user))
        {
            const auto u64 = [](const FILETIME& f) { return ((uint64_t) f.dwHighDateTime << 32) | f.dwLowDateTime; };
            const uint64_t i = u64(idle), total = u64(kernel) + u64(user);
            if (lastTotal != 0 && total > lastTotal)
                detail::Set(s.cpuUsage, 100.0 * (1.0 - (double) (i - lastIdle) / (double) (total - lastTotal)), "Windows");
            lastIdle = i, lastTotal = total;
        }
        double mhz = 0.0, base = 0.0;
        if (pdh.Read(mhz, base))
        {
            s.pdh = true;
            detail::Set(s.cpuClock, mhz, "Windows PDH");
            detail::Set(s.cpuBase, base, "Windows PDH");
        }

        // Memory
        MEMORYSTATUSEX ms { sizeof(ms) };
        if (GlobalMemoryStatusEx(&ms))
        {
            detail::Set(s.ramTotal, ms.ullTotalPhys / 1048576.0, "Windows");
            detail::Set(s.ramFree, ms.ullAvailPhys / 1048576.0, "Windows");
            detail::Set(s.ramUsed, (ms.ullTotalPhys - ms.ullAvailPhys) / 1048576.0, "Windows");
            detail::Set(s.ramLoad, ms.dwMemoryLoad, "Windows");
            ULONGLONG installedKb = 0;
            if (GetPhysicallyInstalledSystemMemory(&installedKb) && installedKb * 1024ULL > ms.ullTotalPhys)
                detail::Set(s.ramReserved, (installedKb * 1024ULL - ms.ullTotalPhys) / 1048576.0, "Windows");
        }
        PERFORMANCE_INFORMATION pi { sizeof(pi) };
        if (GetPerformanceInfo(&pi, sizeof(pi)))
        {
            const double page = (double) pi.PageSize / 1048576.0;
            detail::Set(s.commitUsed, pi.CommitTotal * page, "Windows");
            detail::Set(s.commitLimit, pi.CommitLimit * page, "Windows");
            detail::Set(s.cache, pi.SystemCache * page, "Windows");
            detail::Set(s.processes, pi.ProcessCount, "Windows");
            detail::Set(s.threadsAll, pi.ThreadCount, "Windows");
            detail::Set(s.handles, pi.HandleCount, "Windows");
        }
        detail::Set(s.uptimeS, GetTickCount64() / 1000.0, "Windows");

        // Optional: HWiNFO / MSI Afterburner shared memory (only what the native sources don't give)
        const auto hw = HwTelemetry::Get();
        s.hwinfo = hw.hwinfoAvailable;
        s.afterburner = hw.afterburnerAvailable;
        const auto take = [&](HwTelemetry::Metric m, Reading& to)
        {
            const auto& v = hw.values[m];
            if (!to.valid && v.valid && (v.source == HwTelemetry::Source::HWiNFO || v.source == HwTelemetry::Source::Afterburner))
                detail::Set(to, v.value, HwTelemetry::SourceName(v.source));
        };
        take(HwTelemetry::CpuTemp, s.cpuTemp);
        take(HwTelemetry::CpuPower, s.cpuPower);
        take(HwTelemetry::GpuTemp, s.gpuTemp);
        take(HwTelemetry::GpuHotspot, s.gpuHotspot);
        take(HwTelemetry::GpuUsage, s.gpuUsage);
        return s;
    }

  private:
    detail::Adlx adlx;
    detail::Nvml nvml;
    detail::Pdh pdh;
    std::string cpuName;
    int cores = 0, threads = 0;
    uint64_t lastIdle = 0, lastTotal = 0;
};

// The background reader: about once a second while the terminal is on screen (KeepAlive), stopped otherwise
class Monitor
{
  public:
    static Monitor& Instance()
    {
        static Monitor m;
        return m;
    }

    void KeepAlive()
    {
        alive.store(GetTickCount64(), std::memory_order_relaxed);
        HwTelemetry::KeepAlive(); // the optional sources, only while the terminal is shown
        if (!started.exchange(true))
            std::thread([this] { Loop(); }).detach();
    }

    Sensors Get()
    {
        std::lock_guard lock(mutex);
        return latest;
    }

  private:
    void Loop()
    {
        Reader reader;
        while (true)
        {
            if (GetTickCount64() - alive.load(std::memory_order_relaxed) < 2500)
            {
                Sensors s = reader.Read();
                std::lock_guard lock(mutex);
                latest = std::move(s);
            }
            Sleep(1000);
        }
    }

    std::atomic<uint64_t> alive { 0 };
    std::atomic<bool> started { false };
    std::mutex mutex;
    Sensors latest;
};
} // namespace IlionTerminal

#pragma execution_character_set(pop)
