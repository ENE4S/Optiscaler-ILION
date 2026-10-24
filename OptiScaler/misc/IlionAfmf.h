#pragma once

// ILION: AMD Fluid Motion Frames through AMD's ADLX library (amdadlx64.dll, installed with the Radeon driver). The same
// global setting AMD Software: Adrenalin Edition switches; ILION only asks the driver, nothing runs in the game.
// The few ADLX interfaces used are declared here by their vtable order (ADLX SDK, MIT licence: ISystem.h,
// I3DSettings1.h, I3DSettings3.h, ADLXDefines.h). Every ADLX call runs off the render thread, one at a time.
// Self-contained (Win32 + STL) so it can be tested outside the game.

#include <windows.h>

#include <cstdint>
#include <cwctype>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace IlionAfmf
{
// ---- Live switch: the way Adrenalin switches AFMF while a game runs ----
// AMD's overlay service switches AFMF of the running game with its own key (HKLM\SOFTWARE\AMD\DVR,
// ToggleFrameGen, "Alt+Shift,G" by default) and writes the toast it shows into
// HKCU\Software\AMD\DVR\Overlays\<toast>: "title" ("AMD Fluid Motion Frames 3 está desactivado"), "text2" (at a game's
// start: "...|AMD Fluid Motion Frames 2.1|DESACTIVADO|disabled|..."), "toastid" counting every toast. The global
// ADLX setting is only read by a game when it starts and a game's own profile wins over it; the key acts at once.

// "Alt+Shift,G" -> VK_MENU, VK_SHIFT, 'G'
inline std::vector<WORD> ParseHotkey(const std::string& text)
{
    std::vector<WORD> keys;
    std::string word;

    const auto add = [&](std::string w)
    {
        for (auto& c : w)
            c = (char) toupper((unsigned char) c);
        while (!w.empty() && w.front() == ' ')
            w.erase(w.begin());
        while (!w.empty() && w.back() == ' ')
            w.pop_back();

        if (w.empty())
            return;
        if (w == "ALT")
            keys.push_back(VK_MENU);
        else if (w == "SHIFT")
            keys.push_back(VK_SHIFT);
        else if (w == "CTRL" || w == "CONTROL")
            keys.push_back(VK_CONTROL);
        else if (w.size() == 1 && isalnum((unsigned char) w[0]))
            keys.push_back((WORD) w[0]);
        else if (w[0] == 'F' && w.size() <= 3 && isdigit((unsigned char) w[1]))
            keys.push_back((WORD) (VK_F1 + atoi(w.c_str() + 1) - 1));
        else
            keys.clear(), keys.push_back(0); // unknown word: marks the key as unusable
    };

    for (char c : text)
    {
        if (c == '+' || c == ',')
            add(word), word.clear();
        else
            word += c;
    }
    add(word);

    for (WORD k : keys)
        if (k == 0)
            return {};
    return keys;
}

inline std::wstring ReadRegString(HKEY root, const wchar_t* key, const wchar_t* name)
{
    wchar_t value[1024] {};
    DWORD bytes = sizeof(value) - sizeof(wchar_t);
    if (RegGetValueW(root, key, name, RRF_RT_REG_SZ, nullptr, value, &bytes) != ERROR_SUCCESS)
        return L"";
    return value;
}

inline std::string ToggleHotkeyText()
{
    const std::wstring text = ReadRegString(HKEY_LOCAL_MACHINE, L"SOFTWARE\\AMD\\DVR", L"ToggleFrameGen");
    std::string narrow;
    for (wchar_t c : text.empty() ? std::wstring(L"Alt+Shift,G") : text)
        narrow += (char) (c < 128 ? c : '?');
    return narrow;
}

struct Toast
{
    int id = -1;
    std::wstring title;
    std::wstring text2;
};

// The toast AMD's overlay shows (its key under Overlays has type "toast")
inline Toast ReadToast()
{
    Toast toast;
    HKEY overlays = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\AMD\\DVR\\Overlays", 0, KEY_READ, &overlays) != ERROR_SUCCESS)
        return toast;

    wchar_t sub[128];
    for (DWORD i = 0;; i++)
    {
        DWORD size = 128;
        if (RegEnumKeyExW(overlays, i, sub, &size, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS)
            break;

        const std::wstring key = std::wstring(L"Software\\AMD\\DVR\\Overlays\\") + sub;
        if (ReadRegString(HKEY_CURRENT_USER, key.c_str(), L"type") != L"toast")
            continue;

        DWORD id = 0, bytes = sizeof(id);
        if (RegGetValueW(HKEY_CURRENT_USER, key.c_str(), L"toastid", RRF_RT_REG_DWORD, nullptr, &id, &bytes) == ERROR_SUCCESS)
            toast.id = (int) id;
        else
            toast.id = _wtoi(ReadRegString(HKEY_CURRENT_USER, key.c_str(), L"toastid").c_str());
        toast.title = ReadRegString(HKEY_CURRENT_USER, key.c_str(), L"title");
        toast.text2 = ReadRegString(HKEY_CURRENT_USER, key.c_str(), L"text2");
        break;
    }

    RegCloseKey(overlays);
    return toast;
}

inline std::wstring Lower(std::wstring s)
{
    for (auto& c : s)
        c = towlower(c);
    return s;
}

// 1 on, 0 off, -1 the toast says nothing about AFMF
inline int AfmfStateOf(const Toast& toast)
{
    // The toggle's own toast: "AMD Fluid Motion Frames 3 está activado / desactivado", "... is enabled / disabled"
    const std::wstring title = Lower(toast.title);
    if (title.find(L"fluid motion frames") != std::wstring::npos)
    {
        if (title.find(L"desactiv") != std::wstring::npos || title.find(L"disabled") != std::wstring::npos ||
            title.find(L" off") != std::wstring::npos || title.find(L"deshabilit") != std::wstring::npos)
            return 0;
        if (title.find(L"activ") != std::wstring::npos || title.find(L"enabled") != std::wstring::npos ||
            title.find(L" on") != std::wstring::npos || title.find(L"habilit") != std::wstring::npos)
            return 1;
    }

    // The game's start summary: "<name>|<shown word>|active or disabled|..."; the third word isn't translated
    const std::wstring text = Lower(toast.text2);
    const auto at = text.find(L"fluid motion frames");
    if (at != std::wstring::npos)
    {
        const auto first = text.find(L'|', at);
        const auto second = first == std::wstring::npos ? first : text.find(L'|', first + 1);
        if (second != std::wstring::npos)
        {
            const auto third = text.find(L'|', second + 1);
            const std::wstring status = text.substr(second + 1, third == std::wstring::npos ? std::wstring::npos : third - second - 1);
            if (status == L"active")
                return 1;
            if (status == L"disabled")
                return 0;
        }
    }

    return -1;
}

// Presses AMD's key the way a hand does: modifiers, the key, then all up in reverse
inline bool SendHotkey(const std::vector<WORD>& keys)
{
    if (keys.empty())
        return false;

    std::vector<INPUT> inputs;
    for (WORD k : keys)
    {
        INPUT in {};
        in.type = INPUT_KEYBOARD;
        in.ki.wVk = k;
        in.ki.wScan = (WORD) MapVirtualKeyW(k, MAPVK_VK_TO_VSC);
        inputs.push_back(in);
    }
    for (auto k = keys.rbegin(); k != keys.rend(); ++k)
    {
        INPUT in {};
        in.type = INPUT_KEYBOARD;
        in.ki.wVk = *k;
        in.ki.wScan = (WORD) MapVirtualKeyW(*k, MAPVK_VK_TO_VSC);
        in.ki.dwFlags = KEYEVENTF_KEYUP;
        inputs.push_back(in);
    }

    // Down, a short hold, up: the service sees a pressed key, not a flicker
    const UINT half = (UINT) keys.size();
    const UINT sentDown = SendInput(half, inputs.data(), sizeof(INPUT));
    Sleep(60);
    const UINT sentUp = SendInput(half, inputs.data() + half, sizeof(INPUT));
    return sentDown == half && sentUp == half;
}

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
    int live = -1;               // AFMF in the running game, from AMD's toast: 1 on, 0 off, -1 not known yet
    bool liveBusy = false;       // the key was pressed, waiting for AMD's toast
    std::string liveMessage;     // AMD's toast after the key, or why nothing came
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

    // AFMF in the running game as AMD's last toast tells it (a game's start summary, or the last switch)
    void ReadLive()
    {
        const int live = AfmfStateOf(ReadToast());
        std::lock_guard lock(stateLock);
        if (live >= 0)
            state.live = live;
    }

    // Switches AFMF in the running game at once with AMD's own key, then reads AMD's answer
    void ToggleLive()
    {
        {
            std::lock_guard lock(stateLock);
            if (state.liveBusy)
                return;
            state.liveBusy = true;
            state.liveMessage.clear();
        }

        std::thread(
            [this]
            {
                const auto keys = ParseHotkey(ToggleHotkeyText());
                const Toast before = ReadToast();
                std::string message;
                int live = -1;

                if (keys.empty())
                    message = "AMD's AFMF key could not be read";
                else if (!SendHotkey(keys))
                    message = "the key could not be sent";
                else
                {
                    // AMD's toast comes within about a second
                    for (int i = 0; i < 40; i++)
                    {
                        Sleep(75);
                        const Toast now = ReadToast();
                        if (now.id == before.id && now.title == before.title)
                            continue;

                        live = AfmfStateOf(now);
                        char text[512] {};
                        WideCharToMultiByte(CP_UTF8, 0, now.title.c_str(), -1, text, sizeof(text) - 1, nullptr, nullptr);
                        message = text;
                        break;
                    }

                    if (message.empty())
                        message = "AMD Software did not answer (it has to run with its in-game overlay on)";
                }

                std::lock_guard lock(stateLock);
                state.liveBusy = false;
                state.liveMessage = message;
                if (live >= 0)
                    state.live = live;
            })
            .detach();
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
        read.live = state.live; // kept: it comes from AMD's toasts, not from ADLX
        read.liveBusy = state.liveBusy;
        read.liveMessage = state.liveMessage;
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
