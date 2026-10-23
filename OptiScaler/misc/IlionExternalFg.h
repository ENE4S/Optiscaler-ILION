#pragma once

// ILION: external frame generation. Starts, steers and stops MFG SUPER MOTION's engine (NODIX TECH, closed
// source, installed by the user) as a separate process, the way its own app does: options as "key=value" on the
// command line, changes as lines on stdin, "stop" or a closed stdin to end it, "@..." status lines on stdout.
// Nothing of the engine is shipped or loaded into the game: it captures the game's window from outside.
// Self-contained (Win32 + STL) so it can be tested outside the game.

#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>

#include <atomic>
#include <cstdio>
#include <cwchar>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace IlionExternalFg
{
// One value per control; -1 = not sent, the engine keeps its own default
struct Settings
{
    std::wstring folder;  // the app's folder, its Engine folder or nvngx.dll itself
    int generator = 2;    // 0 DLSS MFG (RTX), 1 XeSS MFG, 2 SMFG (any card)
    int multiplier = 2;   // 2..6
    int game = 0;         // 0 auto .. 6 open world, 7 by flow
    int flow = 50;        // 10..100, only with game = 7
    int queue = 1;        // 0 least delay, 1 balanced, 2 smoothest
    int sync = 1;         // 0 off, 1 newest at each refresh, 2 VSync, 3/4 every 2nd/3rd refresh
    int captureApi = -1;  // -1 auto, 0 DXGI, 1 WGC
    int windowMode = 0;   // 0 borderless, 1 scaled to screen, 2 as it is
    int lowLatency = 1;
    int vrr = 0;
    int still = 1;
    int guard = 0;
    int screenLimit = 0;
    int counter = 1;
    int msmSearch = 0;    // SMFG: 0 full search, 1 fast
    int aml = 0;          // Adaptive Motion Learning
};

// Options changed while it runs ("Live" in the engine's README); the rest need a restart
inline bool IsLiveKey(const char* key)
{
    return strcmp(key, "generator") != 0 && strcmp(key, "window_mode") != 0;
}

inline std::vector<std::pair<std::string, int>> OptionLines(const Settings& s)
{
    std::vector<std::pair<std::string, int>> lines = {
        { "generator", s.generator }, { "multiplier", s.multiplier }, { "game", s.game },
        { "queue", s.queue },         { "sync", s.sync },             { "window_mode", s.windowMode },
        { "low_latency", s.lowLatency }, { "vrr", s.vrr },            { "still", s.still },
        { "guard", s.guard },         { "screen_limit", s.screenLimit }, { "counter", s.counter },
        { "msm_search", s.msmSearch }, { "aml", s.aml },              { "mfg", 1 },
    };

    if (s.game == 7)
        lines.push_back({ "flow", s.flow });
    if (s.captureApi >= 0)
        lines.push_back({ "capture_api", s.captureApi });

    return lines;
}

// An executable PE file (MFG SUPER MOTION's engine is an .exe named nvngx.dll; NVIDIA's real nvngx.dll is a DLL).
// Opened through ntdll: inside the game OptiScaler's Detours hook on CreateFileW (which Detours follows into
// KernelBase) sends every "nvngx.dll" path to a signed stand-in dll, so the engine would be read as that dll and
// turned away. NtOpenFile is below that hook.
inline HANDLE OpenForRead(const std::filesystem::path& file)
{
    struct NtUnicodeString
    {
        USHORT Length;
        USHORT MaximumLength;
        PWSTR Buffer;
    };
    struct NtObjectAttributes
    {
        ULONG Length;
        HANDLE RootDirectory;
        NtUnicodeString* ObjectName;
        ULONG Attributes;
        PVOID SecurityDescriptor;
        PVOID SecurityQualityOfService;
    };
    struct NtIoStatusBlock
    {
        union
        {
            LONG Status;
            PVOID Pointer;
        };
        ULONG_PTR Information;
    };
    using NtOpenFileFn = LONG(NTAPI*)(PHANDLE, ACCESS_MASK, NtObjectAttributes*, NtIoStatusBlock*, ULONG, ULONG);

    static const auto ntOpenFile =
        reinterpret_cast<NtOpenFileFn>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtOpenFile"));
    if (ntOpenFile == nullptr)
        return INVALID_HANDLE_VALUE;

    std::error_code ec;
    std::wstring nt = L"\\??\\" + std::filesystem::absolute(file, ec).wstring();
    NtUnicodeString name { (USHORT) (nt.size() * sizeof(wchar_t)), (USHORT) (nt.size() * sizeof(wchar_t)), nt.data() };
    NtObjectAttributes attributes { sizeof(NtObjectAttributes), nullptr, &name, 0x40 /* OBJ_CASE_INSENSITIVE */ };
    NtIoStatusBlock io {};
    HANDLE handle = nullptr;

    const LONG status = ntOpenFile(&handle, GENERIC_READ | SYNCHRONIZE, &attributes, &io,
                                   FILE_SHARE_READ | FILE_SHARE_DELETE,
                                   0x20 /* FILE_SYNCHRONOUS_IO_NONALERT */ | 0x40 /* FILE_NON_DIRECTORY_FILE */);
    return status >= 0 ? handle : INVALID_HANDLE_VALUE;
}

inline bool IsExecutableImage(const std::filesystem::path& file)
{
    const HANDLE f = OpenForRead(file);
    if (f == INVALID_HANDLE_VALUE)
        return false;

    const auto read = [&](LONG offset, void* to, DWORD size)
    {
        DWORD got = 0;
        return SetFilePointer(f, offset, nullptr, FILE_BEGIN) != INVALID_SET_FILE_POINTER &&
               ReadFile(f, to, size, &got, nullptr) && got == size;
    };

    IMAGE_DOS_HEADER dos {};
    IMAGE_NT_HEADERS64 nt {};
    const bool ok = read(0, &dos, sizeof(dos)) && dos.e_magic == IMAGE_DOS_SIGNATURE &&
                    read(dos.e_lfanew, &nt, sizeof(nt)) && nt.Signature == IMAGE_NT_SIGNATURE &&
                    (nt.FileHeader.Characteristics & IMAGE_FILE_DLL) == 0 &&
                    (nt.FileHeader.Characteristics & IMAGE_FILE_EXECUTABLE_IMAGE) != 0;
    CloseHandle(f);
    return ok;
}

// A path as typed or pasted: no quotes or spaces around it
inline std::wstring CleanPath(std::wstring text)
{
    while (!text.empty() && (text.back() == L'"' || iswspace(text.back())))
        text.pop_back();
    while (!text.empty() && (text.front() == L'"' || iswspace(text.front())))
        text.erase(text.begin());
    return text;
}

// The engine inside a folder the user or a search gave: the folder itself may be the app's, its Engine folder or
// nvngx.dll itself
inline std::filesystem::path EngineIn(const std::filesystem::path& p)
{
    std::error_code ec;
    for (const auto& candidate : { p, p / L"Engine" / L"nvngx.dll", p / L"nvngx.dll" })
    {
        if (std::filesystem::is_regular_file(candidate, ec) && IsExecutableImage(candidate))
            return candidate;
    }

    return {};
}

inline bool NamesSuperMotion(std::wstring text)
{
    for (auto& c : text)
        c = towlower(c);
    return text.find(L"super motion") != std::wstring::npos || text.find(L"super-motion") != std::wstring::npos ||
           text.find(L"supermotion") != std::wstring::npos;
}

// A path out of a registry or shortcut value: "C:\x\app.exe",0 or "C:\x\app.exe" --arg -> C:\x
inline std::filesystem::path FolderOf(std::wstring value)
{
    if (!value.empty() && value[0] == L'"')
    {
        const auto end = value.find(L'"', 1);
        value = value.substr(1, end == std::wstring::npos ? std::wstring::npos : end - 1);
    }
    else if (const auto comma = value.find(L','); comma != std::wstring::npos)
        value = value.substr(0, comma);

    std::filesystem::path p(value);
    return p.has_extension() ? p.parent_path() : p;
}

// Where MFG SUPER MOTION may be, most likely first. Its installer is not public, so every trace it can leave is
// looked at: its uninstall entry, its start-with-Windows entry, its shortcuts, the usual install folders, and a
// folder named after it in Downloads, Desktop or Documents (the .rar build).
inline std::vector<std::filesystem::path> CandidateFolders()
{
    namespace fs = std::filesystem;
    std::vector<fs::path> places;

    // 1. Uninstall entries, both registry views, machine and user
    const struct { HKEY root; REGSAM view; } hives[] = {
        { HKEY_CURRENT_USER, KEY_WOW64_64KEY },  { HKEY_LOCAL_MACHINE, KEY_WOW64_64KEY },
        { HKEY_LOCAL_MACHINE, KEY_WOW64_32KEY },
    };

    for (const auto& hive : hives)
    {
        HKEY uninstall = nullptr;
        if (RegOpenKeyExW(hive.root, L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall", 0,
                          KEY_READ | hive.view, &uninstall) != ERROR_SUCCESS)
            continue;

        wchar_t sub[256];
        for (DWORD i = 0;; i++)
        {
            DWORD size = 256;
            if (RegEnumKeyExW(uninstall, i, sub, &size, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS)
                break;

            const auto read = [&](const wchar_t* name)
            {
                wchar_t value[MAX_PATH * 2] {};
                DWORD bytes = sizeof(value) - sizeof(wchar_t);
                RegGetValueW(uninstall, sub, name, RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ, nullptr, value, &bytes);
                return std::wstring(value);
            };

            if (!NamesSuperMotion(read(L"DisplayName")) && !NamesSuperMotion(sub))
                continue;

            for (const wchar_t* name : { L"InstallLocation", L"DisplayIcon", L"UninstallString" })
            {
                const auto value = read(name);
                if (!value.empty())
                    places.push_back(name[0] == L'I' ? fs::path(value) : FolderOf(value));
            }
        }

        RegCloseKey(uninstall);
    }

    // 2. Start with Windows (the app writes its own path there when asked to)
    HKEY run = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_READ, &run) ==
        ERROR_SUCCESS)
    {
        wchar_t name[256], value[MAX_PATH * 2];
        for (DWORD i = 0;; i++)
        {
            DWORD nameSize = 256, bytes = sizeof(value) - sizeof(wchar_t), type = 0;
            if (RegEnumValueW(run, i, name, &nameSize, nullptr, &type, reinterpret_cast<BYTE*>(value), &bytes) !=
                ERROR_SUCCESS)
                break;
            value[bytes / sizeof(wchar_t)] = 0;
            if (type == REG_SZ && (NamesSuperMotion(name) || NamesSuperMotion(value)))
                places.push_back(FolderOf(value));
        }
        RegCloseKey(run);
    }

    // 3. Shortcuts on the desktop and in the start menu
    const auto knownFolder = [](REFKNOWNFOLDERID id)
    {
        PWSTR raw = nullptr;
        fs::path p;
        if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &raw)))
            p = raw;
        CoTaskMemFree(raw);
        return p;
    };

    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    for (const auto& id : { FOLDERID_Desktop, FOLDERID_PublicDesktop, FOLDERID_Programs, FOLDERID_CommonPrograms })
    {
        std::error_code ec;
        const fs::path root = knownFolder(id);
        if (root.empty())
            continue;

        for (auto it = fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied, ec);
             it != fs::recursive_directory_iterator(); it.increment(ec))
        {
            if (it.depth() > 2)
                it.disable_recursion_pending();
            if (_wcsicmp(it->path().extension().c_str(), L".lnk") != 0 || !NamesSuperMotion(it->path().filename()))
                continue;

            IShellLinkW* link = nullptr;
            if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link))))
                continue;

            IPersistFile* file = nullptr;
            wchar_t target[MAX_PATH] {};
            if (SUCCEEDED(link->QueryInterface(IID_PPV_ARGS(&file))) && SUCCEEDED(file->Load(it->path().c_str(), STGM_READ)) &&
                SUCCEEDED(link->GetPath(target, MAX_PATH, nullptr, SLGP_RAWPATH)) && target[0] != 0)
                places.push_back(fs::path(target).parent_path());

            if (file)
                file->Release();
            link->Release();
        }
    }
    if (SUCCEEDED(com))
        CoUninitialize();

    // 4. The usual install folders
    for (const wchar_t* env : { L"LOCALAPPDATA", L"ProgramFiles", L"ProgramFiles(x86)" })
    {
        wchar_t base[MAX_PATH] {};
        if (GetEnvironmentVariableW(env, base, MAX_PATH) == 0)
            continue;

        const fs::path root = env[0] == L'L' ? fs::path(base) / L"Programs" : fs::path(base);
        for (const wchar_t* name : { L"MFG SUPER MOTION", L"MFG-SUPER-MOTION", L"Mfg Super Motion", L"NODIX-TECH" })
            places.push_back(root / name);
    }

    // 5. An unpacked copy (the .rar build) in Downloads, Desktop or Documents, two levels down, by its folder name
    for (const auto& root : { knownFolder(FOLDERID_Downloads), knownFolder(FOLDERID_Desktop),
                              knownFolder(FOLDERID_Documents) })
    {
        std::error_code ec;
        if (root.empty())
            continue;

        for (auto it = fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied, ec);
             it != fs::recursive_directory_iterator(); it.increment(ec))
        {
            if (it.depth() >= 2)
                it.disable_recursion_pending();
            if (it->is_directory(ec) && NamesSuperMotion(it->path().filename()))
                places.push_back(it->path());
        }
    }

    return places;
}

// The engine from what the user gave, else from every place MFG SUPER MOTION may be; empty when none is found
inline std::filesystem::path FindEngine(const std::wstring& given)
{
    if (const auto clean = CleanPath(given); !clean.empty())
    {
        if (auto engine = EngineIn(clean); !engine.empty())
            return engine;
    }

    for (const auto& folder : CandidateFolders())
    {
        if (auto engine = EngineIn(folder); !engine.empty())
            return engine;

        // A versioned folder inside (an unpacked "Mfg Super Motion 1.0.3\MFG SUPER MOTION\...")
        std::error_code ec;
        for (const auto& inside : std::filesystem::directory_iterator(folder, ec))
        {
            if (inside.is_directory(ec))
            {
                if (auto engine = EngineIn(inside.path()); !engine.empty())
                    return engine;
            }
        }
    }

    return {};
}

// The search off the render thread: started once ILION loads, read by the tab
class Finder
{
  public:
    void Start(const std::wstring& given)
    {
        if (started.exchange(true))
            return;

        std::thread(
            [this, given]
            {
                auto found = FindEngine(given);
                std::lock_guard lock(mutex);
                result = found;
                done = true;
            })
            .detach();
    }

    // Again, after the user installed it or gave a folder
    void Restart(const std::wstring& given)
    {
        {
            std::lock_guard lock(mutex);
            done = false;
        }
        started = false;
        Start(given);
    }

    bool Done()
    {
        std::lock_guard lock(mutex);
        return done;
    }

    std::filesystem::path Result()
    {
        std::lock_guard lock(mutex);
        return result;
    }

  private:
    std::atomic<bool> started = false;
    std::mutex mutex;
    bool done = false;
    std::filesystem::path result;
};

// Text of the Windows clipboard, read only when the user presses Paste (ImGui's own paste does not reach every game)
inline std::wstring ClipboardText()
{
    std::wstring text;
    if (!OpenClipboard(nullptr))
        return text;

    if (HANDLE data = GetClipboardData(CF_UNICODETEXT))
    {
        if (auto* chars = static_cast<const wchar_t*>(GlobalLock(data)))
        {
            text = chars;
            GlobalUnlock(data);
        }
    }
    CloseClipboard();

    // A path copied from Explorer comes in quotes
    return CleanPath(text);
}

struct Status
{
    bool running = false;
    std::string state;   // starting | picking | running | stopped
    std::string mfg;     // multiplier running | off | unavailable <why>
    std::string source;  // <hwnd> <w> <h> <dxgi|wgc> <placement>
    std::string error;
    std::string aml;
    double baseFps = 0.0;
    double outputFps = 0.0;
    std::vector<std::string> lastLines; // the last status lines, newest last
};

class Engine
{
  public:
    // Nothing waited for at unload (loader lock): when the game ends its end of stdin closes and the engine stops
    ~Engine()
    {
        if (reader.joinable())
            reader.detach();
    }

    // Starts the engine on the game's window; false and a reason when it can't
    bool Start(const std::filesystem::path& engine, HWND gameWindow, const Settings& s, std::string& why)
    {
        if (IsRunning())
        {
            why = "already running";
            return false;
        }

        Close();

        std::wstring cmd = L"\"" + engine.wstring() + L"\"";
        for (const auto& [key, value] : OptionLines(s))
            cmd += L" " + std::wstring(key.begin(), key.end()) + L"=" + std::to_wstring(value);

        cmd += L" delay=0";
        if (gameWindow != nullptr)
            cmd += L" hwnd=" + std::to_wstring(reinterpret_cast<unsigned long long>(gameWindow));

        SECURITY_ATTRIBUTES sa { sizeof(sa), nullptr, TRUE };
        HANDLE inRead = nullptr, outWrite = nullptr;
        if (!CreatePipe(&inRead, &inWrite, &sa, 0) || !CreatePipe(&outRead, &outWrite, &sa, 0))
        {
            why = "CreatePipe failed";
            if (inRead)
                CloseHandle(inRead);
            Close();
            return false;
        }

        // Our ends stay ours
        SetHandleInformation(inWrite, HANDLE_FLAG_INHERIT, 0);
        SetHandleInformation(outRead, HANDLE_FLAG_INHERIT, 0);

        // Only the two pipe ends are handed down, none of the game's handles
        SIZE_T size = 0;
        InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
        std::vector<char> buffer(size);
        auto* list = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(buffer.data());
        HANDLE inherit[2] = { inRead, outWrite };
        const bool listOk = InitializeProcThreadAttributeList(list, 1, 0, &size) &&
                            UpdateProcThreadAttribute(list, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherit,
                                                      sizeof(inherit), nullptr, nullptr);

        STARTUPINFOEXW si {};
        si.StartupInfo.cb = sizeof(si);
        si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
        si.StartupInfo.hStdInput = inRead;
        si.StartupInfo.hStdOutput = outWrite;
        si.StartupInfo.hStdError = outWrite;
        si.lpAttributeList = listOk ? list : nullptr;

        PROCESS_INFORMATION pi {};
        const std::wstring folder = engine.parent_path().wstring();
        const BOOL created =
            CreateProcessW(engine.c_str(), cmd.data(), nullptr, nullptr, TRUE,
                           CREATE_NO_WINDOW | (listOk ? EXTENDED_STARTUPINFO_PRESENT : 0), nullptr, folder.c_str(),
                           &si.StartupInfo, &pi);
        const DWORD lastError = GetLastError();

        if (listOk)
            DeleteProcThreadAttributeList(list);
        CloseHandle(inRead);
        CloseHandle(outWrite);

        if (!created)
        {
            why = "CreateProcess failed, error " + std::to_string(lastError);
            Close();
            return false;
        }

        CloseHandle(pi.hThread);
        process = pi.hProcess;

        {
            std::lock_guard lock(mutex);
            status = {};
            status.running = true;
            status.state = "starting";
        }

        HANDLE pipe = outRead;
        outRead = nullptr;
        reader = std::thread([this, pipe] { ReadLines(pipe); });
        return true;
    }

    // Sends one changed option; only while it runs
    bool Send(const std::string& key, int value)
    {
        return WriteLine(key + "=" + std::to_string(value));
    }

    void Stop()
    {
        if (process != nullptr)
        {
            WriteLine("stop");

            // A closed stdin is a stop too; the engine puts the game's window back as it was
            if (inWrite != nullptr)
            {
                CloseHandle(inWrite);
                inWrite = nullptr;
            }

            if (WaitForSingleObject(process, 3000) == WAIT_TIMEOUT)
            {
                // Its own stop event, then nothing more: never killed while it holds the game's window
                if (HANDLE stopEvent = OpenEventW(EVENT_MODIFY_STATE, FALSE, L"Local\\SuperMotionEngineStop"))
                {
                    SetEvent(stopEvent);
                    CloseHandle(stopEvent);
                }
            }
        }

        Close();
    }

    bool IsRunning() const
    {
        return process != nullptr && WaitForSingleObject(process, 0) == WAIT_TIMEOUT;
    }

    Status GetStatus()
    {
        std::lock_guard lock(mutex);
        Status copy = status;
        copy.running = IsRunning();
        return copy;
    }

    // One status line, as the engine writes it (public for the tests)
    void Parse(const std::string& line)
    {
        if (line.empty() || line[0] != '@')
            return;

        std::lock_guard lock(mutex);
        status.lastLines.push_back(line);
        if (status.lastLines.size() > 8)
            status.lastLines.erase(status.lastLines.begin());

        const auto space = line.find(' ');
        const std::string tag = line.substr(0, space);
        const std::string rest = space == std::string::npos ? "" : line.substr(space + 1);

        if (tag == "@state")
            status.state = rest;
        else if (tag == "@mfg")
            status.mfg = rest;
        else if (tag == "@source")
            status.source = rest;
        else if (tag == "@error")
            status.error = rest;
        else if (tag == "@aml")
            status.aml = rest;
        else if (tag == "@fps")
            sscanf_s(rest.c_str(), "%lf %lf", &status.baseFps, &status.outputFps);
    }

  private:
    bool WriteLine(const std::string& text)
    {
        if (inWrite == nullptr || !IsRunning())
            return false;

        const std::string line = text + "\n";
        DWORD written = 0;
        return WriteFile(inWrite, line.data(), (DWORD) line.size(), &written, nullptr) && written == line.size();
    }

    void ReadLines(HANDLE pipe)
    {
        std::string pending;
        char chunk[512];
        DWORD read = 0;

        while (ReadFile(pipe, chunk, sizeof(chunk), &read, nullptr) && read > 0)
        {
            pending.append(chunk, read);

            size_t end;
            while ((end = pending.find('\n')) != std::string::npos)
            {
                std::string line = pending.substr(0, end);
                if (!line.empty() && line.back() == '\r')
                    line.pop_back();
                Parse(line);
                pending.erase(0, end + 1);
            }
        }

        CloseHandle(pipe);

        std::lock_guard lock(mutex);
        status.running = false;
    }

    void Close()
    {
        if (inWrite != nullptr)
            CloseHandle(inWrite);
        inWrite = nullptr;

        // The reader ends once the engine's end of the pipe is gone (it closes its own end)
        if (reader.joinable())
        {
            if (IsRunning())
                reader.detach();
            else
                reader.join();
        }

        if (outRead != nullptr)
            CloseHandle(outRead);
        outRead = nullptr;

        if (process != nullptr)
            CloseHandle(process);
        process = nullptr;
    }

    HANDLE process = nullptr;
    HANDLE inWrite = nullptr;
    HANDLE outRead = nullptr;
    std::thread reader;
    std::mutex mutex;
    Status status;
};

// Settings kept in ILION\ExternalFG.ini beside the dll, apart from OptiScaler.ini
inline void Load(Settings& s, const std::filesystem::path& ini)
{
    const wchar_t* f = ini.c_str();
    wchar_t folder[MAX_PATH] {};
    GetPrivateProfileStringW(L"SuperMotion", L"Folder", L"", folder, MAX_PATH, f);
    s.folder = CleanPath(folder);

    const auto get = [&](const wchar_t* key, int& v) { v = (int) GetPrivateProfileIntW(L"SuperMotion", key, v, f); };
    get(L"Generator", s.generator);
    get(L"Multiplier", s.multiplier);
    get(L"Game", s.game);
    get(L"Flow", s.flow);
    get(L"Queue", s.queue);
    get(L"Sync", s.sync);
    int capture = s.captureApi + 1;
    get(L"CaptureApi", capture);
    s.captureApi = capture - 1;
    get(L"WindowMode", s.windowMode);
    get(L"LowLatency", s.lowLatency);
    get(L"Vrr", s.vrr);
    get(L"Still", s.still);
    get(L"Guard", s.guard);
    get(L"ScreenLimit", s.screenLimit);
    get(L"Counter", s.counter);
    get(L"MsmSearch", s.msmSearch);
    get(L"Aml", s.aml);
}

inline void Save(const Settings& s, const std::filesystem::path& ini)
{
    std::error_code ec;
    std::filesystem::create_directories(ini.parent_path(), ec);

    const wchar_t* f = ini.c_str();
    WritePrivateProfileStringW(L"SuperMotion", L"Folder", s.folder.c_str(), f);

    const auto put = [&](const wchar_t* key, int v)
    { WritePrivateProfileStringW(L"SuperMotion", key, std::to_wstring(v).c_str(), f); };
    put(L"Generator", s.generator);
    put(L"Multiplier", s.multiplier);
    put(L"Game", s.game);
    put(L"Flow", s.flow);
    put(L"Queue", s.queue);
    put(L"Sync", s.sync);
    put(L"CaptureApi", s.captureApi + 1);
    put(L"WindowMode", s.windowMode);
    put(L"LowLatency", s.lowLatency);
    put(L"Vrr", s.vrr);
    put(L"Still", s.still);
    put(L"Guard", s.guard);
    put(L"ScreenLimit", s.screenLimit);
    put(L"Counter", s.counter);
    put(L"MsmSearch", s.msmSearch);
    put(L"Aml", s.aml);
}
} // namespace IlionExternalFg
