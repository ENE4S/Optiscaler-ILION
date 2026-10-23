#pragma once

// ILION: AMD Fluid Motion Frames through AMD's ADLX library (amdadlx64.dll, installed with the Radeon driver). The same
// global setting AMD Software: Adrenalin Edition switches; ILION only asks the driver, nothing runs in the game.
// The few ADLX interfaces used are declared here by their vtable order (ADLX SDK, MIT licence: ISystem.h,
// I3DSettings1.h, I3DSettings3.h, ADLXDefines.h). Every ADLX call runs off the render thread, one at a time.
// Self-contained (Win32 + STL) so it can be tested outside the game.

#include <windows.h>

#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

namespace IlionAfmf
{
// ADLX_AFMF_ALGORITHM, _SEARCH_MODE_TYPE, _PERFORMANCE_MODE_TYPE, _FAST_MOTION_RESP
enum Setting
{
    Enabled,
    Algorithm,   // 0 auto, 1 enhanced, 2 standard
    Search,      // 0 auto, 1 standard, 2 high
    Performance, // 0 auto, 1 quality, 2 performance
    FastMotion,  // 0 repeat frames, 1 blended frames
};

struct Snapshot
{
    int phase = 0;        // 0 not asked, 1 asking the driver, 2 read, 3 not available
    std::string reason;   // why it is not available
    bool supported = false;
    bool enabled = false;
    bool extended = false; // AFMF 2.1 controls (IADLX3DAMDFluidMotionFrames1)
    bool algorithmSupported = false;
    int algorithm = -1, search = -1, performance = -1, fastMotion = -1;
    bool busy = false;
    bool antiLag = false;        // Radeon Anti-Lag on (Adrenalin turns it on with AFMF)
    bool changedEnabled = false; // AFMF switched from here in this session: the game takes it at its next start
    std::string lastError;       // the last change the driver refused
};

class Control
{
  public:
    // Reads the driver's state once, in the background
    void Start()
    {
        {
            std::lock_guard lock(stateLock);
            if (state.phase != 0)
                return;
            state.phase = 1;
        }

        std::thread([this] { Work(-1, 0); }).detach();
    }

    // Changes one setting in the background, then reads everything again
    void Set(Setting which, int value)
    {
        {
            std::lock_guard lock(stateLock);
            if (state.phase != 2 || state.busy)
                return;
            state.busy = true;
            state.lastError.clear();
        }

        std::thread([this, which, value] { Work((int) which, value); }).detach();
    }

    // Reads it again (the user may have changed it in Adrenalin)
    void Refresh()
    {
        {
            std::lock_guard lock(stateLock);
            if (state.phase != 2 || state.busy)
                return;
            state.busy = true;
        }

        std::thread([this] { Work(-1, 0); }).detach();
    }

    Snapshot Get()
    {
        std::lock_guard lock(stateLock);
        return state;
    }

  private:
    using Fn = void*;
    using AdlxResult = int;
    static constexpr AdlxResult Ok = 0, AlreadyEnabled = 1, AlreadyInitialized = 2;
    static bool Succeeded(AdlxResult r) { return r == Ok || r == AlreadyEnabled || r == AlreadyInitialized; }

    // The n-th entry of an interface's vtable
    template <typename T> static T Slot(void* object, int index)
    {
        return reinterpret_cast<T>((*reinterpret_cast<void***>(object))[index]);
    }

    // IADLXInterface: Acquire 0, Release 1, QueryInterface 2
    static void* Query(void* object, const wchar_t* iid)
    {
        void* out = nullptr;
        const auto query = Slot<AdlxResult(__stdcall*)(void*, const wchar_t*, void**)>(object, 2);
        return Succeeded(query(object, iid, &out)) ? out : nullptr;
    }

    bool Open(std::string& why)
    {
        if (afmf != nullptr)
            return true;

        HMODULE adlx = LoadLibraryExW(L"amdadlx64.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (adlx == nullptr)
        {
            why = "amdadlx64.dll not found (no AMD Radeon driver)";
            return false;
        }

        using QueryFullVersionFn = AdlxResult(__cdecl*)(uint64_t*);
        using InitializeFn = AdlxResult(__cdecl*)(uint64_t, void**);
        const auto queryVersion = reinterpret_cast<QueryFullVersionFn>(GetProcAddress(adlx, "ADLXQueryFullVersion"));
        const auto initialize = reinterpret_cast<InitializeFn>(GetProcAddress(adlx, "ADLXInitialize"));

        uint64_t version = 0;
        if (queryVersion == nullptr || initialize == nullptr || !Succeeded(queryVersion(&version)))
        {
            why = "ADLX entry points missing";
            return false;
        }

        // The runtime's own version: the interfaces asked for are checked one by one below
        void* system = nullptr;
        if (const AdlxResult r = initialize(version, &system); !Succeeded(r) || system == nullptr)
        {
            why = "ADLXInitialize failed (" + std::to_string(r) + ")";
            return false;
        }

        // IADLXSystem (not counted): Get3DSettingsServices is entry 7
        void* services = nullptr;
        const auto get3d = Slot<AdlxResult(__stdcall*)(void*, void**)>(system, 7);
        if (!Succeeded(get3d(system, &services)) || services == nullptr)
        {
            why = "3D settings services not available";
            return false;
        }

        // Radeon Anti-Lag of every card: Adrenalin turns it on with AFMF (seen in the driver's registry,
        // KMD_DeLagEnabled 0 -> 1 when AFMF is switched on there). IADLXSystem::GetGPUs is entry 1;
        // IADLXGPUList: Size 3, At_GPUList 11; IADLX3DSettingsServices::GetAntiLag(gpu) is entry 3.
        void* gpus = nullptr;
        if (Succeeded(Slot<AdlxResult(__stdcall*)(void*, void**)>(system, 1)(system, &gpus)) && gpus != nullptr)
        {
            const unsigned count = Slot<unsigned(__stdcall*)(void*)>(gpus, 3)(gpus);
            for (unsigned i = 0; i < count && antiLagCount < MaxCards; i++)
            {
                void* gpu = nullptr;
                void* antiLag = nullptr;
                if (Succeeded(Slot<AdlxResult(__stdcall*)(void*, unsigned, void**)>(gpus, 11)(gpus, i, &gpu)) && gpu != nullptr &&
                    Succeeded(Slot<AdlxResult(__stdcall*)(void*, void*, void**)>(services, 3)(services, gpu, &antiLag)) &&
                    antiLag != nullptr)
                {
                    antiLags[antiLagCount++] = antiLag;
                }
            }
        }

        // IADLX3DSettingsServices1 (ADLX 1.3): GetAMDFluidMotionFrames is entry 17
        void* services1 = Query(services, L"IADLX3DSettingsServices1");
        if (services1 == nullptr)
        {
            why = "this driver has no AFMF control in ADLX (update the Radeon driver)";
            return false;
        }

        const auto getAfmf = Slot<AdlxResult(__stdcall*)(void*, void**)>(services1, 17);
        if (!Succeeded(getAfmf(services1, &afmf)) || afmf == nullptr)
        {
            why = "AFMF interface not available";
            return false;
        }

        // AFMF 2.1 controls, when the driver has them
        afmf1 = Query(afmf, L"IADLX3DAMDFluidMotionFrames1");
        return true;
    }

    // IADLX3DAMDFluidMotionFrames: IsSupported 3, IsEnabled 4, SetEnabled 5
    // IADLX3DAMDFluidMotionFrames1: IsSupportedAlgorithm 6, Get/SetAlgorithm 7/8, Get/SetSearchMode 9/10,
    //                               Get/SetPerformanceMode 11/12, Get/SetFastMotionResponse 13/14
    void Work(int which, int value)
    {
        std::lock_guard adlx(adlxLock);

        Snapshot read;
        std::string why;
        std::string error;

        if (!Open(why))
        {
            std::lock_guard lock(stateLock);
            state.phase = 3;
            state.reason = why;
            state.busy = false;
            return;
        }

        if (which >= 0)
        {
            AdlxResult r = -1;
            if (which == Enabled)
            {
                r = Slot<AdlxResult(__stdcall*)(void*, uint8_t)>(afmf, 5)(afmf, value != 0 ? 1 : 0);

                // As Adrenalin does: AFMF on brings Anti-Lag on (IADLX3DAntiLag: IsSupported 3, SetEnabled 5).
                // Turning AFMF off leaves Anti-Lag as it is.
                for (int i = 0; value != 0 && Succeeded(r) && i < antiLagCount; i++)
                {
                    uint8_t supported = 0;
                    if (Succeeded(Slot<AdlxResult(__stdcall*)(void*, uint8_t*)>(antiLags[i], 3)(antiLags[i], &supported)) &&
                        supported != 0 && !Succeeded(Slot<AdlxResult(__stdcall*)(void*, uint8_t)>(antiLags[i], 5)(antiLags[i], 1)))
                        error = "AFMF on, but the driver refused Radeon Anti-Lag";
                }
            }
            else if (afmf1 != nullptr)
                r = Slot<AdlxResult(__stdcall*)(void*, int)>(afmf1, 6 + which * 2)(afmf1, value);

            if (!Succeeded(r))
                error = "the driver refused the change (" + std::to_string(r) + ")";
            else if (which == Enabled)
                read.changedEnabled = true;
        }

        // Anti-Lag: on when every card that has it has it on (IsEnabled 4)
        read.antiLag = antiLagCount > 0;
        for (int i = 0; i < antiLagCount; i++)
        {
            uint8_t supported = 0, on = 0;
            if (Succeeded(Slot<AdlxResult(__stdcall*)(void*, uint8_t*)>(antiLags[i], 3)(antiLags[i], &supported)) && supported != 0 &&
                !(Succeeded(Slot<AdlxResult(__stdcall*)(void*, uint8_t*)>(antiLags[i], 4)(antiLags[i], &on)) && on != 0))
                read.antiLag = false;
        }

        uint8_t flag = 0;
        read.phase = 2;
        read.supported = Succeeded(Slot<AdlxResult(__stdcall*)(void*, uint8_t*)>(afmf, 3)(afmf, &flag)) && flag != 0;
        flag = 0;
        read.enabled = Succeeded(Slot<AdlxResult(__stdcall*)(void*, uint8_t*)>(afmf, 4)(afmf, &flag)) && flag != 0;

        if (afmf1 != nullptr)
        {
            read.extended = true;
            flag = 0;
            read.algorithmSupported =
                Succeeded(Slot<AdlxResult(__stdcall*)(void*, uint8_t*)>(afmf1, 6)(afmf1, &flag)) && flag != 0;

            const auto get = [&](int index, int& to)
            {
                int v = -1;
                to = Succeeded(Slot<AdlxResult(__stdcall*)(void*, int*)>(afmf1, index)(afmf1, &v)) ? v : -1;
            };
            get(7, read.algorithm);
            get(9, read.search);
            get(11, read.performance);
            get(13, read.fastMotion);
        }

        std::lock_guard lock(stateLock);
        read.lastError = error;
        read.changedEnabled = read.changedEnabled || state.changedEnabled;
        state = read;
    }

    std::mutex adlxLock;  // one ADLX call at a time
    std::mutex stateLock; // what the menu reads
    Snapshot state;
    void* afmf = nullptr;  // kept for the life of the process, never released at unload (loader lock)
    void* afmf1 = nullptr;
    static constexpr int MaxCards = 8;
    void* antiLags[MaxCards] {};
    int antiLagCount = 0;
};
} // namespace IlionAfmf
