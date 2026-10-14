#pragma once
// ILION experiment: what the external d4r DLSS backend is really doing, from evidence only.
//
// Nothing here talks to d4r or changes it. The sources are:
//   - the lines d4r itself writes to d4r\d4r_nvngx.log (read incrementally, shared, read-only), and
//   - the results OptiScaler gets back from NGX when it creates and evaluates the DLSS feature.
// Each stage is reported from its own evidence; a stage is never inferred from another one:
//   requested   the model d4r says it applied from d4r.ini            ("d4r: player mode, model=K ...")
//   bound       the model's layers bound to native kernel files       ("[d4r-native] "name" -> file.hsaco")
//   evaluated   evaluations NGX returned success for, and failures    (OptiScaler side + D4R_*FAILURE lines)
//   executed    layers launched with backend=native                   ("[d4r-launch] ... backend=native",
//               only written with [Debug] Log = verbose)
// Anything without evidence stays unknown and the menu shows it as not verified.
#include <windows.h>
#include <psapi.h>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <mutex>
#include <string>
#include <string_view>

namespace IlionD4r
{
inline constexpr const char* kLayersK[] = { "dltss_pwin_enc0_layer", "dltss_pwin_enc1_layer", "dltss_pwin_enc2_layer",
                                            "dltss_pwin_enc3_layer", "dltss_pwin_enc4_layer", "dltss_pwin_dec5_layer",
                                            "dltss_pwin_dec4_layer", "dltss_pwin_dec3_layer", "dltss_pwin_dec2_layer",
                                            "dltss_pwin_dec1_layer", "dltss_pwin_dec0_layer" };
inline constexpr const char* kLayersM[] = { "rrlite_enc1_4x4", "rrlite_enc2_4x4", "rrlite_enc3_tube_4x4",
                                            "rrlite_dec2_4x4", "rrlite_dec1_4x4" };
inline constexpr int kCountK = 11;
inline constexpr int kCountM = 5;

struct Evidence
{
    // From the d4r log
    bool sawConfigLine = false;
    std::string requestedModel; // "K", "M", ... exactly as d4r wrote it
    std::string architecture;   // "gfx1100", ...
    std::string logLevel;       // "normal" or "verbose"
    uint32_t boundK = 0, boundM = 0;               // bit per layer bound to a native kernel file
    uint32_t launchedNativeK = 0, launchedNativeM = 0; // bit per layer launched with backend=native
    uint32_t launchedOtherK = 0, launchedOtherM = 0;   // bit per layer launched with any other backend
    uint64_t nativeLaunches = 0;                   // launch lines of K or M layers with backend=native
    uint64_t failures = 0;                         // D4R_*FAILURE lines
    uint64_t failuresCleared = 0;                  // D4R_FAILURE_CLEARED lines
    std::string lastFailure;
    uint64_t framesDone = 0;                       // D4R_FRAME lines (verbose only)
    double lastInteropNgxMs = -1.0;                // from the last D4R_FRAME line
    uint64_t validations = 0, validationsBad = 0;  // D4R_OUTPUT_VALIDATION lines; bad = nan or inf not zero
    uint64_t sharedPlaneRetries = 0;               // D4R_SHARED_PLANE_RESULT lines (v6: a buffer needed a retry)

    static int Count(uint32_t bits)
    {
        int n = 0;
        for (; bits; bits &= bits - 1)
            ++n;
        return n;
    }
};

// Text after `key` up to the next space, quote, comma or end of line
inline std::string_view ValueAfter(std::string_view line, std::string_view key)
{
    const auto at = line.find(key);
    if (at == std::string_view::npos)
        return {};
    auto rest = line.substr(at + key.size());
    const auto end = rest.find_first_of(" \t\",\r\n");
    return end == std::string_view::npos ? rest : rest.substr(0, end);
}

inline int LayerIndex(std::string_view name, const char* const* layers, int count)
{
    for (int i = 0; i < count; ++i)
        if (name == layers[i])
            return i;
    return -1;
}

// One complete line of the d4r log
inline void ParseLine(std::string_view line, Evidence& e)
{
    if (line.rfind("d4r: player mode,", 0) == 0)
    {
        e.sawConfigLine = true;
        e.requestedModel = std::string(ValueAfter(line, "model="));
        e.architecture = std::string(ValueAfter(line, "architecture="));
        e.logLevel = std::string(ValueAfter(line, "log="));
        return;
    }

    if (line.rfind("[d4r-native] \"", 0) == 0)
    {
        // [d4r-native] "dltss_pwin_enc0_layer" -> C:\...\dltss_pwin_enc0_layer.hsaco (prep: ...
        const auto rest = line.substr(14);
        const auto quote = rest.find('"');
        if (quote == std::string_view::npos || rest.find("\" -> ") != quote || rest.find(".hsaco") == std::string_view::npos)
            return;
        const auto name = rest.substr(0, quote);
        if (const int k = LayerIndex(name, kLayersK, kCountK); k >= 0)
            e.boundK |= 1u << k;
        else if (const int m = LayerIndex(name, kLayersM, kCountM); m >= 0)
            e.boundM |= 1u << m;
        return;
    }

    if (line.rfind("[d4r-launch] kernel=\"", 0) == 0)
    {
        const auto rest = line.substr(21);
        const auto quote = rest.find('"');
        if (quote == std::string_view::npos)
            return;
        const auto name = rest.substr(0, quote);
        const bool native = ValueAfter(line, "backend=") == "native";
        if (const int k = LayerIndex(name, kLayersK, kCountK); k >= 0)
        {
            (native ? e.launchedNativeK : e.launchedOtherK) |= 1u << k;
            e.nativeLaunches += native;
        }
        else if (const int m = LayerIndex(name, kLayersM, kCountM); m >= 0)
        {
            (native ? e.launchedNativeM : e.launchedOtherM) |= 1u << m;
            e.nativeLaunches += native;
        }
        return;
    }

    if (line.rfind("D4R_FAILURE_CLEARED", 0) == 0)
    {
        ++e.failuresCleared;
        return;
    }

    if (line.rfind("D4R_", 0) == 0)
    {
        const auto space = line.find(' ');
        const auto tag = line.substr(0, space);
        if (tag.size() > 8 && tag.substr(tag.size() - 8) == "_FAILURE")
        {
            ++e.failures;
            e.lastFailure = space == std::string_view::npos ? std::string(tag) : std::string(line.substr(space + 1));
            while (!e.lastFailure.empty() && (e.lastFailure.back() == '\r' || e.lastFailure.back() == '\n'))
                e.lastFailure.pop_back();
            return;
        }
        if (tag == "D4R_FRAME")
        {
            ++e.framesDone;
            const auto value = ValueAfter(line, "interop_ngx_ms=");
            if (!value.empty())
                e.lastInteropNgxMs = std::atof(std::string(value).c_str());
            return;
        }
        if (tag == "D4R_OUTPUT_VALIDATION")
        {
            ++e.validations;
            if (ValueAfter(line, "nan=") != "0" || ValueAfter(line, "inf=") != "0")
                ++e.validationsBad;
            return;
        }
        if (tag == "D4R_SHARED_PLANE_RESULT")
            ++e.sharedPlaneRetries;
    }
}

// Splits a byte stream into lines; keeps an unfinished line until the rest arrives
class LineFeeder
{
    std::string pending_;

  public:
    void Reset() { pending_.clear(); }

    void Feed(const char* data, size_t size, Evidence& e)
    {
        size_t start = 0;
        for (size_t i = 0; i < size; ++i)
        {
            if (data[i] != '\n')
                continue;
            if (pending_.empty())
            {
                ParseLine(std::string_view(data + start, i - start), e);
            }
            else
            {
                pending_.append(data + start, i - start);
                ParseLine(pending_, e);
                pending_.clear();
            }
            start = i + 1;
        }
        // A line longer than this is not one of ours
        if (size > start && pending_.size() < 4096)
            pending_.append(data + start, size - start);
    }
};

// OptiScaler side: what NGX answered. Written from the render thread, read from the menu.
struct Calls
{
    std::atomic<uint64_t> generation { 0 };  // DLSS feature creations that succeeded
    std::atomic<uint64_t> evaluateOk { 0 };  // since the last creation
    std::atomic<uint64_t> evaluateFailed { 0 };
    std::atomic<uint32_t> lastResult { 0 };  // NGX result of the last evaluation; 1 = success
    std::atomic<bool> evaluatedOnce { false };
    // DLSS.Hint.Render.Preset.* read from the NGX parameters around the last evaluation: what the game and
    // OptiScaler passed in, and what was there once d4r had taken them. kPresetUnknown / kPresetMixed otherwise.
    std::atomic<uint32_t> presetBefore { 0xFFFFFFFFu };
    std::atomic<uint32_t> presetAfter { 0xFFFFFFFFu };
};

inline constexpr uint32_t kPresetUnknown = 0xFFFFFFFFu; // could not be read
inline constexpr uint32_t kPresetMixed = 0xFFFFFFFEu;   // the six quality modes do not share one preset

inline Calls& GetCalls()
{
    static Calls calls;
    return calls;
}

inline void OnFeatureCreated()
{
    auto& c = GetCalls();
    c.evaluateOk = 0;
    c.evaluateFailed = 0;
    c.lastResult = 0;
    c.evaluatedOnce = false;
    c.presetBefore = kPresetUnknown;
    c.presetAfter = kPresetUnknown;
    ++c.generation;
}

inline void OnPresets(uint32_t before, uint32_t after)
{
    auto& c = GetCalls();
    c.presetBefore = before;
    c.presetAfter = after;
}

// NGX numbers render presets from 1 (A); 0 is the default
inline char PresetLetter(uint32_t preset) { return preset >= 1 && preset <= 26 ? char('A' + preset - 1) : 0; }

inline void OnEvaluate(uint32_t ngxResult)
{
    auto& c = GetCalls();
    c.lastResult = ngxResult;
    c.evaluatedOnce = true;
    if (ngxResult == 1)
        ++c.evaluateOk;
    else
        ++c.evaluateFailed;
}

struct LogState
{
    bool readable = false; // opened and from this session
    bool locked = false;
    bool stale = false;
};

// Reads what d4r appended to its log since the last call. The file is opened shared and read-only.
class LogReader
{
    std::mutex mutex_;
    std::wstring path_;
    uint64_t offset_ = 0;
    uint64_t lastTick_ = 0;
    bool readable_ = false;
    bool locked_ = false; // another handle holds the file without sharing it (d4r while the game runs)
    bool stale_ = false;  // written before this process started: not this session
    LineFeeder feeder_;
    Evidence evidence_;
    // What the log had counted when the current DLSS feature was created, to report per feature
    uint64_t generation_ = 0;
    uint64_t failuresAtCreation_ = 0;
    uint32_t launchedNativeKAtCreation_ = 0, launchedNativeMAtCreation_ = 0;

    void Restart()
    {
        offset_ = 0;
        feeder_.Reset();
        evidence_ = Evidence {};
        failuresAtCreation_ = 0;
    }

  public:
    // At most one read every intervalMs. Returns a copy of the evidence so far.
    Evidence Poll(const std::wstring& path, LogState* state, uint64_t* failuresThisFeature, uint64_t intervalMs = 500)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const uint64_t now = GetTickCount64();
        if (path != path_)
        {
            path_ = path;
            Restart();
            lastTick_ = 0;
        }

        if (lastTick_ == 0 || now - lastTick_ >= intervalMs)
        {
            lastTick_ = now;
            readable_ = false;
            locked_ = false;
            stale_ = false;
            const HANDLE file = CreateFileW(path_.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (file == INVALID_HANDLE_VALUE)
            {
                locked_ = GetLastError() == ERROR_SHARING_VIOLATION;
            }
            else
            {
                LARGE_INTEGER size {};
                FILETIME written {}, started {}, unused {};
                if (GetFileTime(file, nullptr, nullptr, &written) &&
                    GetProcessTimes(GetCurrentProcess(), &started, &unused, &unused, &unused) &&
                    CompareFileTime(&written, &started) < 0)
                {
                    // Nothing was written to it since this process started: it is an earlier session's log
                    stale_ = true;
                    Restart();
                }
                else if (GetFileSizeEx(file, &size))
                {
                    readable_ = true;
                    // A shorter file is a new log (the game was restarted, or the file was replaced)
                    if (uint64_t(size.QuadPart) < offset_)
                        Restart();

                    LARGE_INTEGER seek {};
                    seek.QuadPart = LONGLONG(offset_);
                    if (SetFilePointerEx(file, seek, nullptr, FILE_BEGIN))
                    {
                        char buffer[65536];
                        // Bounded work per poll: the rest is read at the next one
                        for (int chunk = 0; chunk < 64; ++chunk)
                        {
                            DWORD got = 0;
                            if (!ReadFile(file, buffer, sizeof buffer, &got, nullptr) || got == 0)
                                break;
                            feeder_.Feed(buffer, got, evidence_);
                            offset_ += got;
                        }
                    }
                }
                CloseHandle(file);
            }
        }

        // Per-feature view: what happened in the log since OptiScaler last created the DLSS feature
        const uint64_t generation = GetCalls().generation.load();
        if (generation != generation_)
        {
            generation_ = generation;
            failuresAtCreation_ = evidence_.failures;
            launchedNativeKAtCreation_ = evidence_.launchedNativeK;
            launchedNativeMAtCreation_ = evidence_.launchedNativeM;
            // Launch evidence must be from this feature: start collecting again
            evidence_.launchedNativeK = evidence_.launchedNativeM = 0;
            evidence_.launchedOtherK = evidence_.launchedOtherM = 0;
        }

        if (state)
            *state = LogState { readable_, locked_, stale_ };
        if (failuresThisFeature)
            *failuresThisFeature = evidence_.failures - failuresAtCreation_;
        return evidence_;
    }
};

inline LogReader& GetLogReader()
{
    static LogReader reader;
    return reader;
}

// The architecture of the d4r shim loaded in this process ("_nvngx_<architecture>.dll"), empty if none is
inline std::string LoadedShimArchitecture()
{
    static std::mutex mutex;
    static std::string found;
    static uint64_t lastTick = 0;
    std::lock_guard<std::mutex> lock(mutex);
    const uint64_t now = GetTickCount64();
    if (!found.empty() || (lastTick != 0 && now - lastTick < 2000))
        return found;

    lastTick = now;
    HMODULE modules[1024];
    DWORD needed = 0;
    if (!K32EnumProcessModules(GetCurrentProcess(), modules, sizeof modules, &needed))
        return found;

    const DWORD count = (needed < sizeof modules ? needed : DWORD(sizeof modules)) / sizeof(HMODULE);
    for (DWORD i = 0; i < count; ++i)
    {
        wchar_t name[MAX_PATH] = L"";
        if (!K32GetModuleBaseNameW(GetCurrentProcess(), modules[i], name, MAX_PATH))
            continue;

        const std::wstring_view base(name);
        if (base.size() <= 12 || _wcsnicmp(name, L"_nvngx_", 7) != 0 || _wcsicmp(name + base.size() - 4, L".dll") != 0)
            continue;

        // The module also has to be d4r's: it exports the backend version OptiScaler validated
        if (!GetProcAddress(modules[i], "d4r_WindowsBackendVersion"))
            continue;

        for (const wchar_t ch : base.substr(7, base.size() - 11))
            found.push_back(char(ch));
        break;
    }

    return found;
}

// The architecture shim d4r loaded for this GPU, checked in the process itself
inline bool ShimLoaded(const std::string& architecture)
{
    if (architecture.empty() || architecture.size() > 32)
        return false;
    const std::wstring name = L"_nvngx_" + std::wstring(architecture.begin(), architecture.end()) + L".dll";
    return GetModuleHandleW(name.c_str()) != nullptr;
}
} // namespace IlionD4r
