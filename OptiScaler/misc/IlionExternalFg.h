#pragma once

// ILION: external frame generation. Starts, steers and stops MFG SUPER MOTION's engine (NODIX TECH, closed
// source, installed by the user) as a separate process, the way its own app does: options as "key=value" on the
// command line, changes as lines on stdin, "stop" or a closed stdin to end it, "@..." status lines on stdout.
// Nothing of the engine is shipped or loaded into the game: it captures the game's window from outside.
// Self-contained (Win32 + STL) so it can be tested outside the game.

#include <windows.h>

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

// An executable PE file (MFG SUPER MOTION's engine is an .exe named nvngx.dll; NVIDIA's real nvngx.dll is a DLL)
inline bool IsExecutableImage(const std::filesystem::path& file)
{
    FILE* f = nullptr;
    _wfopen_s(&f, file.c_str(), L"rb");
    if (f == nullptr)
        return false;

    IMAGE_DOS_HEADER dos {};
    IMAGE_NT_HEADERS64 nt {};
    bool ok = fread(&dos, sizeof(dos), 1, f) == 1 && dos.e_magic == IMAGE_DOS_SIGNATURE &&
              fseek(f, dos.e_lfanew, SEEK_SET) == 0 && fread(&nt, sizeof(nt), 1, f) == 1 &&
              nt.Signature == IMAGE_NT_SIGNATURE && (nt.FileHeader.Characteristics & IMAGE_FILE_DLL) == 0 &&
              (nt.FileHeader.Characteristics & IMAGE_FILE_EXECUTABLE_IMAGE) != 0;
    fclose(f);
    return ok;
}

// The engine from what the user gave, or from the usual install places; empty when none is found
inline std::filesystem::path FindEngine(const std::wstring& given)
{
    std::vector<std::filesystem::path> places;

    if (!given.empty())
    {
        std::filesystem::path p(given);
        places.push_back(p);
        places.push_back(p / L"Engine" / L"nvngx.dll");
        places.push_back(p / L"nvngx.dll");
    }

    for (const wchar_t* env : { L"LOCALAPPDATA", L"ProgramFiles", L"ProgramFiles(x86)" })
    {
        wchar_t base[MAX_PATH] {};
        if (GetEnvironmentVariableW(env, base, MAX_PATH) == 0)
            continue;

        const std::filesystem::path root = env[0] == L'L' ? std::filesystem::path(base) / L"Programs" : base;
        for (const wchar_t* name : { L"MFG SUPER MOTION", L"MFG-SUPER-MOTION", L"Mfg Super Motion" })
            places.push_back(root / name / L"Engine" / L"nvngx.dll");
    }

    std::error_code ec;
    for (const auto& p : places)
    {
        if (std::filesystem::is_regular_file(p, ec) && IsExecutableImage(p))
            return p;
    }

    return {};
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
    s.folder = folder;

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
