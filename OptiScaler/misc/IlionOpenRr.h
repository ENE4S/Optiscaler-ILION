#pragma once

// ILION 1.5.0: OpenRR-ASI as the Ray Reconstruction provider, managed from ILION.
//
// OpenRR-ASI (openrr-amd.asi) is TheAutomatic's OptiScaler plugin, https://github.com/TheAutomatic/OpenRR-ASI,
// PolyForm Noncommercial 1.0.0. Its engine is OpenRR by OpenNR (MIT) and its ASI shell comes from DLSSNR-AMD by
// mochizuki0323 (MIT). It runs the DLSS Ray Reconstruction weights of the user's own nvngx_dlssd.dll 310.9.1 on
// RDNA4 and, experimentally, RDNA3.
//
// ILION never ships, downloads or copies OpenRR, nvngx_dlssd.dll or its weights. It manages an install the user made
// beside OptiScaler:
// - detection: the plugin loaded in the process, its data folders and the dlssd version;
// - settings: openrr-amd.ini, which the plugin reads again whenever the file changes (mode, guides and depth apply on
//   the next frame, the backend when the game creates the feature again, OptiFGHost after a restart);
// - telemetry: openrr-amd.log, read where it was left. Its figures are the plugin's own (one line every 600 network
//   frames, 64 samples), shown with their age and never filled in or smoothed.

#include <Util.h>
#include <Config.h>
#include <State.h>
#include <Logger.h>
#include <misc/IdentifyGpu.h>

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace IlionOpenRr
{
// What the GPU of the game can do with OpenRR
enum class Arch
{
    Unknown,
    Rdna4,       // ACO offline kernels, FP8
    Rdna3,       // shaders-gfx11, FP16, the driver's LLPC (experimental in OpenRR)
    AmdOther,    // no cooperative matrices: not supported
    Nvidia,      // the real DLSS Ray Reconstruction runs; OpenRR stays out
    Other
};

// The plugin's life in this session, from its log
enum class RunState
{
    NoLog,    // nothing written this session
    Idle,     // loaded, no Ray Reconstruction feature (the game doesn't use RR now)
    Building, // feature created, network not ready yet: the frames show the colour without denoising
    Ready,    // network frames running
    Failed
};

struct Install
{
    bool loaded = false;        // openrr-amd.asi in the process
    std::string fileVersion;    // version resource of the asi, empty when it has none
    bool iniFound = false;
    bool dataFolder = false;    // openrr-amd\ beside the ini
    bool shadersBase = false;   // openrr-amd\shaders (RDNA4 and Linux)
    bool shadersGfx11 = false;  // openrr-amd\shaders-gfx11 (RDNA3 / RDNA3.5)
    bool acoRdna4 = false;      // openrr-amd\aco\gfx1201 or gfx1200 (Windows RDNA4)
    bool dlssdFound = false;
    std::string dlssdVersion;   // "310.9.1.0"
    bool dlssdSupported = false; // 310.9.1.x, the version OpenRR needs
    bool hostDll = false;        // openrr-ngx-host.dll present (OptiFG host experiment)
    std::vector<std::string> languages; // stems of openrr-amd\lang\*.txt
    Arch arch = Arch::Unknown;
    std::string gpuName;
};

struct Telemetry
{
    RunState state = RunState::NoLog;
    std::string build;           // "v0.1.7plus6 (03888b3)"
    bool hooks = false;          // NGX hooks installed
    bool checkPassed = false;    // the plugin's usability check
    std::string checkFailure;    // the last failed check of the game's adapter
    std::string compiler;        // "LLPC", "ACO"
    std::string gpuArch;         // "RDNA3", "RDNA4" as the plugin sees it
    std::string driver;          // "26.10.2"
    bool featureAlive = false;
    std::vector<std::string> aliveHandles; // features created and not released yet (a preset change creates the new one
                                           // before it releases the old one)
    bool vulkanFeature = false;
    unsigned renderW = 0, renderH = 0, outputW = 0, outputH = 0;
    std::string backend;         // "D3D12 Direct", "Vulkan bridge", "Vulkan host"
    bool fastModeOn = false;     // New or Fast kernels in use (false: Legacy)
    bool experimentalOn = false; // the Fast level on top of New
    bool haveModeLine = false;
    bool haveStats = false;
    unsigned long long statFrame = 0; // network frames when the last figure was written
    float rawLast = 0, rawP50 = 0, rawP95 = 0, cleanLast = 0, cleanAverage = 0, network = 0;
    unsigned statSamples = 0;
    ULONGLONG statTick = 0;      // when ILION read that line
    int vramMb = -1;
    std::string weightsVersion;  // "310.9.1.0" once the weights were rebuilt
    bool guidesSeen = false;
    std::map<std::string, int> guides; // diffuse_albedo, specular_albedo, normals, roughness, hit, dir_hit, tl, tl_op,
                                       // tl_mv, dm, sss, resp, cbt, dof, smv: 1 the game gives it, 0 it doesn't
    unsigned long long networkFramesLast = 0; // network frames of the last feature released
    std::string lastProblem;     // the last failure the plugin wrote
    bool optiFgHostActive = false;
    std::string backendNote;     // the plugin's own note on the backend it chose ("D3D12 (auto), kernels recorded ...")
    bool coexistMochi = false;   // dlssnr-amd.asi in the process: OpenRR forced D3D12 Direct
    bool fastBuilding = false;   // the network runs, its fast (New / Fast) kernels are still being built: Legacy meanwhile
    bool fastUnavailable = false;
    std::string fastUnavailableWhy;
    std::string weightsPath;     // the dlssd the weights came from
    bool offered = false;        // the plugin passed its check at start: the game was told Ray Reconstruction exists
    ULONGLONG firstLineTick = 0; // when ILION read the first line of this session's log
};

// ---- Paths --------------------------------------------------------------------------------------------------------
// OpenRR keeps its files beside the OptiScaler dll (its module_folder)
inline std::filesystem::path Folder() { return Util::DllPath().parent_path(); }
inline std::filesystem::path IniPath() { return Folder() / L"openrr-amd.ini"; }
inline std::filesystem::path LogPath() { return Folder() / L"openrr-amd.log"; }
inline std::filesystem::path DataPath() { return Folder() / L"openrr-amd"; }

inline bool PathExists(const std::filesystem::path& path)
{
    std::error_code ec;
    return std::filesystem::exists(path, ec);
}

inline std::string Lower(std::string text)
{
    for (char& c : text)
        c = (char) std::tolower((unsigned char) c);
    return text;
}

inline std::string Trim(const std::string& text)
{
    const auto a = text.find_first_not_of(" \t\r\n");
    if (a == std::string::npos)
        return {};
    const auto b = text.find_last_not_of(" \t\r\n");
    return text.substr(a, b - a + 1);
}

// ---- Install ------------------------------------------------------------------------------------------------------
inline Arch DetectArch(std::string* name)
{
    const auto gpu = IdentifyGpu::getPrimaryGpu();
    if (name != nullptr)
        *name = gpu.name;
    if (gpu.vendorId == VendorId::Nvidia)
        return Arch::Nvidia;
    if (gpu.vendorId != VendorId::AMD)
        return gpu.vendorId == VendorId::Invalid ? Arch::Unknown : Arch::Other;
    switch (gpu.amdHwGeneration)
    {
    case device_info::HwGeneration::kGfx12:
        return Arch::Rdna4;
    case device_info::HwGeneration::kGfx11:
    case device_info::HwGeneration::kGfx11_5:
        return Arch::Rdna3;
    case device_info::HwGeneration::kUndefinedGeneration:
        return Arch::Unknown;
    default:
        return Arch::AmdOther;
    }
}

inline Install ScanInstall()
{
    Install in;
    if (HMODULE module = GetModuleHandleW(L"openrr-amd.asi"); module != nullptr)
    {
        in.loaded = true;
        wchar_t path[MAX_PATH * 2] = {};
        if (GetModuleFileNameW(module, path, (DWORD) std::size(path)) > 0)
            in.fileVersion = Util::GetFileVersionString(path);
    }

    in.iniFound = PathExists(IniPath());
    const auto data = DataPath();
    in.dataFolder = PathExists(data);
    in.shadersBase = PathExists(data / L"shaders");
    in.shadersGfx11 = PathExists(data / L"shaders-gfx11");
    in.acoRdna4 = PathExists(data / L"aco" / L"gfx1201") || PathExists(data / L"aco" / L"gfx1200");
    in.hostDll = PathExists(Folder() / L"openrr-ngx-host.dll") || GetModuleHandleW(L"openrr-ngx-host.dll") != nullptr;

    // Where OpenRR looks for the dlssd: beside OptiScaler, inside openrr-amd, beside the game
    const std::filesystem::path candidates[] = { Folder() / L"nvngx_dlssd.dll", data / L"nvngx_dlssd.dll",
                                                 Util::ExePath().parent_path() / L"nvngx_dlssd.dll" };
    for (const auto& candidate : candidates)
    {
        if (!PathExists(candidate))
            continue;
        in.dlssdFound = true;
        in.dlssdVersion = Util::GetFileVersionString(candidate.wstring());
        std::replace(in.dlssdVersion.begin(), in.dlssdVersion.end(), ',', '.');
        in.dlssdVersion.erase(std::remove(in.dlssdVersion.begin(), in.dlssdVersion.end(), ' '), in.dlssdVersion.end());
        in.dlssdSupported = in.dlssdVersion.rfind("310.9.1", 0) == 0;
        break;
    }

    std::error_code ec;
    for (std::filesystem::directory_iterator it(data / L"lang", ec), end; !ec && it != end; it.increment(ec))
    {
        if (it->path().extension() == L".txt")
            in.languages.push_back(it->path().stem().string());
    }
    std::sort(in.languages.begin(), in.languages.end());

    in.arch = DetectArch(&in.gpuName);
    return in;
}

// ---- openrr-amd.ini -----------------------------------------------------------------------------------------------
// Read whole, written back whole with only the changed line replaced (comments and order kept), through a temporary
// file and one rename, so the plugin never reads half a file.
class Ini
{
  public:
    bool Load()
    {
        lines_.clear();
        const auto path = IniPath();
        FILE* file = _wfopen(path.c_str(), L"rb");
        if (file == nullptr)
            return loaded_ = false;
        std::string text;
        char buffer[4096];
        size_t n;
        while ((n = fread(buffer, 1, sizeof buffer, file)) > 0)
            text.append(buffer, n);
        fclose(file);
        if (text.size() >= 3 && (unsigned char) text[0] == 0xEF && (unsigned char) text[1] == 0xBB && (unsigned char) text[2] == 0xBF)
        {
            bom_ = true;
            text.erase(0, 3);
        }
        crlf_ = text.find("\r\n") != std::string::npos;
        size_t start = 0;
        while (start <= text.size())
        {
            size_t end = text.find('\n', start);
            if (end == std::string::npos)
                end = text.size();
            std::string line = text.substr(start, end - start);
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            lines_.push_back(line);
            if (end == text.size())
                break;
            start = end + 1;
        }
        std::error_code ec;
        mtime_ = std::filesystem::last_write_time(path, ec);
        return loaded_ = true;
    }

    // Read again when the file changed on disk (the plugin's own menu or the user)
    void Refresh()
    {
        std::error_code ec;
        const auto now = std::filesystem::last_write_time(IniPath(), ec);
        if (ec)
        {
            lines_.clear();
            loaded_ = false;
            return;
        }
        if (!loaded_ || now != mtime_)
            Load();
    }

    bool Loaded() const { return loaded_; }

    std::string Get(const char* key, const char* fallback) const
    {
        const int at = Find(key);
        if (at < 0)
            return fallback;
        const std::string& line = lines_[at];
        return Trim(line.substr(line.find('=') + 1));
    }

    bool GetBool(const char* key, bool fallback) const
    {
        const std::string v = Lower(Get(key, fallback ? "true" : "false"));
        if (v == "true" || v == "1" || v == "yes" || v == "on")
            return true;
        if (v == "false" || v == "0" || v == "no" || v == "off")
            return false;
        return fallback;
    }

    float GetFloat(const char* key, float fallback) const
    {
        const std::string v = Get(key, "");
        if (v.empty())
            return fallback;
        char* end = nullptr;
        const float f = std::strtof(v.c_str(), &end);
        return end != v.c_str() ? f : fallback;
    }

    bool Set(const char* key, const std::string& value)
    {
        if (!loaded_)
            return false;
        const int at = Find(key);
        const std::string line = std::string(key) + " = " + value;
        if (at >= 0)
        {
            if (lines_[at] == line)
                return true;
            lines_[at] = line;
        }
        else
        {
            // At the end of [OpenRR] (before the next section), or a new [OpenRR] when there is none
            int section = -1, insert = -1;
            for (int i = 0; i < (int) lines_.size(); i++)
            {
                const std::string t = Trim(lines_[i]);
                if (!t.empty() && t[0] == '[')
                {
                    if (section >= 0)
                    {
                        insert = i;
                        break;
                    }
                    if (Lower(t) == "[openrr]")
                        section = i;
                }
            }
            if (section < 0)
            {
                lines_.push_back("[OpenRR]");
                lines_.push_back(line);
            }
            else
            {
                if (insert < 0)
                    insert = (int) lines_.size();
                while (insert > section + 1 && Trim(lines_[insert - 1]).empty())
                    insert--;
                lines_.insert(lines_.begin() + insert, line);
            }
        }
        return Save();
    }

    bool SetBool(const char* key, bool value) { return Set(key, value ? "true" : "false"); }

  private:
    int Find(const char* key) const
    {
        const std::string wanted = Lower(key);
        bool inSection = false;
        for (int i = 0; i < (int) lines_.size(); i++)
        {
            const std::string t = Trim(lines_[i]);
            if (t.empty() || t[0] == ';' || t[0] == '#')
                continue;
            if (t[0] == '[')
            {
                inSection = Lower(t) == "[openrr]";
                continue;
            }
            if (!inSection)
                continue;
            const auto eq = t.find('=');
            if (eq != std::string::npos && Lower(Trim(t.substr(0, eq))) == wanted)
                return i;
        }
        return -1;
    }

    bool Save()
    {
        const auto path = IniPath();
        auto temp = path;
        temp += L".ilion-tmp";
        FILE* file = _wfopen(temp.c_str(), L"wb");
        if (file == nullptr)
            return false;
        if (bom_)
            fwrite("\xEF\xBB\xBF", 1, 3, file);
        const char* eol = crlf_ ? "\r\n" : "\n";
        for (size_t i = 0; i < lines_.size(); i++)
        {
            fwrite(lines_[i].data(), 1, lines_[i].size(), file);
            if (i + 1 < lines_.size())
                fwrite(eol, 1, strlen(eol), file);
        }
        fclose(file);
        if (!MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        {
            DeleteFileW(temp.c_str());
            return false;
        }
        std::error_code ec;
        mtime_ = std::filesystem::last_write_time(path, ec);
        return true;
    }

    std::vector<std::string> lines_;
    std::filesystem::file_time_type mtime_ {};
    bool loaded_ = false;
    bool bom_ = false;
    bool crlf_ = true;
};

// ---- openrr-amd.log -----------------------------------------------------------------------------------------------
class LogReader
{
  public:
    // Reads what was appended since the last call; a shorter file is a new session
    void Poll(Telemetry& t)
    {
        const auto path = LogPath();
        HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                  OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
            return;
        LARGE_INTEGER size {};
        GetFileSizeEx(file, &size);
        if ((unsigned long long) size.QuadPart < offset_)
        {
            offset_ = 0;
            pending_.clear();
            t = Telemetry {};
        }
        if ((unsigned long long) size.QuadPart > offset_)
        {
            LARGE_INTEGER to {};
            to.QuadPart = (LONGLONG) offset_;
            SetFilePointerEx(file, to, nullptr, FILE_BEGIN);
            // At most 1 MB per call: a long log is caught up over a few frames
            const unsigned long long want = (std::min)((unsigned long long) size.QuadPart - offset_, 1ull << 20);
            std::string chunk((size_t) want, '\0');
            DWORD got = 0;
            if (ReadFile(file, chunk.data(), (DWORD) want, &got, nullptr) && got > 0)
            {
                chunk.resize(got);
                offset_ += got;
                pending_ += chunk;
                size_t start = 0, end;
                while ((end = pending_.find('\n', start)) != std::string::npos)
                {
                    std::string line = pending_.substr(start, end - start);
                    if (!line.empty() && line.back() == '\r')
                        line.pop_back();
                    Parse(line, t);
                    start = end + 1;
                }
                pending_.erase(0, start);
            }
        }
        CloseHandle(file);
    }

  private:
    static bool Has(const std::string& line, const char* text) { return line.find(text) != std::string::npos; }

    static std::string After(const std::string& line, const char* text, const char* stop)
    {
        const auto a = line.find(text);
        if (a == std::string::npos)
            return {};
        const auto from = a + strlen(text);
        const auto b = stop != nullptr ? line.find(stop, from) : std::string::npos;
        return line.substr(from, b == std::string::npos ? std::string::npos : b - from);
    }

    static void Parse(const std::string& line, Telemetry& t)
    {
        if (t.state == RunState::NoLog)
        {
            t.state = RunState::Idle;
            t.firstLineTick = GetTickCount64();
        }

        if (Has(line, "openrr-amd.asi v"))
        {
            t.build = Trim(After(line, "openrr-amd.asi ", " loaded"));
            return;
        }
        if (Has(line, "NGX hooks installed"))
        {
            t.hooks = true;
            return;
        }
        if (Has(line, "] Check passed") || Has(line, "SystemCaps adapted"))
        {
            t.checkPassed = true;
            t.offered = true;
            return;
        }
        if (Has(line, "Check failed"))
        {
            t.checkFailure = Trim(After(line, " ms: ", nullptr));
            return;
        }
        if (Has(line, "compiler resolved"))
        {
            // compiler resolved (D3D12): LLPC (target=none, arch=RDNA3, mach=0x41)
            t.compiler = Trim(After(line, "): ", " ("));
            t.gpuArch = Trim(After(line, "arch=", ","));
            return;
        }
        if (Has(line, "Vulkan device:") && Has(line, "driver "))
        {
            t.driver = Trim(After(line, "driver ", " ("));
            return;
        }
        if (Has(line, "Ray Reconstruction feature created"))
        {
            const std::string handle = Trim(After(line, "handle=", ","));
            if (std::find(t.aliveHandles.begin(), t.aliveHandles.end(), handle) == t.aliveHandles.end())
                t.aliveHandles.push_back(handle);
            t.featureAlive = true;
            t.vulkanFeature = Has(line, "Vulkan Ray Reconstruction");
            t.state = RunState::Building;
            t.haveStats = false;
            unsigned rw = 0, rh = 0, ow = 0, oh = 0;
            const std::string dims = After(line, "render=", ",");
            if (std::sscanf(dims.c_str(), "%ux%u -> output=%ux%u", &rw, &rh, &ow, &oh) == 4)
            {
                t.renderW = rw;
                t.renderH = rh;
                t.outputW = ow;
                t.outputH = oh;
            }
            const std::string backend = After(line, "backend=", ",");
            t.backend = t.vulkanFeature ? "Vulkan host" : (backend.empty() ? "D3D12" : Trim(backend));
            t.coexistMochi = Has(line, "coexist_mochi=yes");
            return;
        }
        if (Has(line, "Ray Reconstruction feature released"))
        {
            // Only the handle released goes: with another one still alive (the new one of a preset change) RR keeps running
            const std::string handle = Trim(After(line, "handle=", ")"));
            t.aliveHandles.erase(std::remove(t.aliveHandles.begin(), t.aliveHandles.end(), handle), t.aliveHandles.end());
            if (t.aliveHandles.empty())
            {
                t.featureAlive = false;
                t.state = RunState::Idle;
            }
            return;
        }
        if (Has(line, "destroy after ") && Has(line, "network frames"))
        {
            t.networkFramesLast = std::strtoull(After(line, "destroy after ", " ").c_str(), nullptr, 10);
            return;
        }
        if (Has(line, "transitioned to READY") || line == "ready")
        {
            if (t.featureAlive)
                t.state = RunState::Ready;
            return;
        }
        if (Has(line, "weights rebuilt from") || Has(line, "weights from cache"))
        {
            t.weightsVersion = Trim(After(line, "(version ", ")"));
            const std::string path = Trim(After(line, " from ", " (version"));
            if (!path.empty())
                t.weightsPath = path;
            return;
        }
        if (line.rfind("backend: ", 0) == 0)
        {
            const std::string note = After(line, "backend: ", ";");
            t.backendNote = Trim(note.empty() ? After(line, "backend: ", nullptr) : note);
            return;
        }
        if (Has(line, "network kernels ready"))
        {
            t.fastBuilding = !t.fastUnavailable;
            return;
        }
        if (Has(line, "fast mode kernels ready"))
        {
            t.fastBuilding = false;
            return;
        }
        if (Has(line, "fast mode not available"))
        {
            t.fastBuilding = false;
            t.fastUnavailable = true;
            t.fastUnavailableWhy = Trim(After(line, "not available: ", nullptr));
            return;
        }
        if (Has(line, "fast mode on at frame"))
        {
            t.fastModeOn = true;
            t.haveModeLine = true;
            return;
        }
        if (Has(line, "fast mode off at frame"))
        {
            t.fastModeOn = false;
            t.haveModeLine = true;
            return;
        }
        if (Has(line, "experimental level on at frame"))
        {
            t.experimentalOn = true;
            return;
        }
        if (Has(line, "experimental level off at frame"))
        {
            t.experimentalOn = false;
            return;
        }
        if (Has(line, "VRAM: this feature "))
        {
            t.vramMb = std::atoi(After(line, "VRAM: this feature ", " MB").c_str());
            return;
        }
        if (Has(line, "[openrr] guides "))
        {
            t.guidesSeen = true;
            t.guides.clear();
            const std::string list = After(line, "[openrr] guides ", nullptr);
            size_t at = 0;
            while (at < list.size())
            {
                const size_t space = list.find(' ', at);
                const std::string token = list.substr(at, space == std::string::npos ? std::string::npos : space - at);
                const auto eq = token.find('=');
                if (eq != std::string::npos)
                    t.guides[token.substr(0, eq)] = std::atoi(token.c_str() + eq + 1);
                if (space == std::string::npos)
                    break;
                at = space + 1;
            }
            return;
        }
        if (line.rfind("frame ", 0) == 0 && Has(line, "GPU raw "))
        {
            // frame 600: render 1505x847 -> 2560x1440, fast mode on, D3D12, GPU raw 7.09 ms (p50 7.09, p95 7.37 of 64),
            // clean 7.09 ms (average 7.12 ms; mean since the last line: ingest 0.001, network 7.07, pack 0.005)
            unsigned long long frame = 0;
            std::sscanf(line.c_str(), "frame %llu:", &frame);
            float raw = 0, p50 = 0, p95 = 0, clean = 0, average = 0;
            unsigned samples = 0;
            const std::string rawPart = After(line, "GPU raw ", nullptr);
            if (std::sscanf(rawPart.c_str(), "%f ms (p50 %f, p95 %f of %u), clean %f ms (average %f", &raw, &p50, &p95, &samples, &clean,
                            &average) >= 4)
            {
                t.statFrame = frame;
                t.rawLast = raw;
                t.rawP50 = p50;
                t.rawP95 = p95;
                t.statSamples = samples;
                t.cleanLast = clean;
                t.cleanAverage = average;
                t.network = (float) std::atof(After(line, "network ", ",").c_str());
                t.fastModeOn = Has(line, "fast mode on");
                t.haveModeLine = true;
                // The first line of a feature comes before the network is ready (0.04 ms of nothing): no figure
                // A figure of a network frame past the first one means the network runs, even if its "ready" line was missed
                if (frame > 1 && t.featureAlive && t.state == RunState::Building)
                    t.state = RunState::Ready;
                t.haveStats = frame > 1 && t.state == RunState::Ready;
                t.statTick = GetTickCount64();
            }
            return;
        }
        if (Has(line, "[openrr-host] EXPERIMENT enabled"))
        {
            t.optiFgHostActive = true;
            return;
        }
        if (Has(line, "create failed") || Has(line, "frame failed") || Has(line, "] failed") || Has(line, "FAILED"))
        {
            t.lastProblem = Trim(line);
            if (t.featureAlive && Has(line, "create failed"))
                t.state = RunState::Failed;
            return;
        }
    }

    unsigned long long offset_ = 0;
    std::string pending_;
};

// ---- Shared state for the menu and the overlay (both on the render thread) ----------------------------------------
struct Snapshot
{
    Install install;
    Telemetry telemetry;
    bool iniLoaded = false;
};

inline std::mutex& Mutex()
{
    static std::mutex mutex;
    return mutex;
}

inline Ini& SharedIni()
{
    static Ini ini;
    return ini;
}

// Plugin present: what the menu and the overlay test first (cheap, cached)
inline bool Loaded()
{
    static int cached = -1;
    static ULONGLONG checked = 0;
    const ULONGLONG now = GetTickCount64();
    if (cached < 0 || now - checked > 2000)
    {
        cached = GetModuleHandleW(L"openrr-amd.asi") != nullptr ? 1 : 0;
        checked = now;
    }
    return cached == 1;
}

// OpenRR's own menu stays off at every start: all its settings are in ILION (IlionManaged marks the file; OpenRR ignores
// the unknown key). Its window would open with ILION's key and take the input otherwise
inline void ApplyFirstTakeover(Ini& ini)
{
    if (!ini.Loaded())
        return;
    const std::string key = Lower(ini.Get("MenuKey", "optiscaler"));
    if (key != "off")
    {
        ini.Set("MenuKey", "off");
        LOG_INFO("OpenRR: its own menu turned off (MenuKey was {}); its settings are in ILION", key);
    }
    if (!ini.GetBool("IlionManaged", false))
        ini.SetBool("IlionManaged", true);
}

// ILION 1.5.0: OpenRR's backend at start. Found in Cyberpunk 2077 (RX 7900 XTX, DLSS Enabler and DLSSG-XeFG, the
// FGDIAG build): when the game creates Ray Reconstruction at start with OpenRR's D3D12 Direct backend, Streamline's
// DLSS-G presents only the real frames for the whole session (the replacement generates and returns success, its frame
// never reaches the screen). With the Vulkan bridge, or with RR turned on later, both work. The bridge costs about
// 0.5 ms more (9.7 against 9.2 ms p50 at 1706x960 -> 1440p). So with the game's DLSSG configured ILION starts OpenRR on
// the bridge, and on D3D12 Direct otherwise. IlionBackend keeps the user's choice: auto (that rule), d3d12 or vulkan
enum class BackendReason
{
    Unknown,
    AutoForFg,   // auto, the game's DLSSG is configured: Vulkan bridge
    AutoNoFg,    // auto, no frame generation of the game: D3D12 Direct (OpenRR's own auto)
    ForcedD3d12,
    ForcedVulkan
};

inline std::atomic<BackendReason> backendReason { BackendReason::Unknown };

// The game's DLSSG through Streamline is in use: ILION reads its inputs or passes them to a replacement
inline bool GameDlssgFgConfigured()
{
    const auto input = Config::Instance()->FGInput.value_or_default();
    return input == FGInput::NvngxFG || input == FGInput::DLSSG;
}

inline std::string UserBackend(const Ini& ini)
{
    std::string v = Lower(ini.Get("IlionBackend", "auto"));
    return v == "d3d12" || v == "vulkan" ? v : std::string("auto");
}

// The value of OpenRR's Backend for the choice and this configuration, and why
inline std::string EffectiveBackend(const std::string& user, BackendReason* why)
{
    BackendReason r;
    std::string value;
    if (user == "d3d12")
    {
        r = BackendReason::ForcedD3d12;
        value = "d3d12";
    }
    else if (user == "vulkan")
    {
        r = BackendReason::ForcedVulkan;
        value = "vulkan";
    }
    else if (GameDlssgFgConfigured())
    {
        r = BackendReason::AutoForFg;
        value = "vulkan";
    }
    else
    {
        r = BackendReason::AutoNoFg;
        value = "auto";
    }
    if (why != nullptr)
        *why = r;
    return value;
}

inline void ApplyBackend(Ini& ini)
{
    if (!ini.Loaded())
        return;
    BackendReason why;
    const std::string value = EffectiveBackend(UserBackend(ini), &why);
    backendReason.store(why);
    if (Lower(ini.Get("Backend", "auto")) != value)
    {
        ini.Set("Backend", value);
        LOG_INFO("OpenRR: Backend = {} ({})", value,
                 why == BackendReason::AutoForFg ? "auto: the game's DLSSG is configured, D3D12 Direct at start hides its generated frames"
                 : why == BackendReason::AutoNoFg ? "auto: no frame generation of the game"
                                                  : "chosen by the user");
    }
}

// The interface that has the technology: Industrial presented Full (the same test as the menu's OpenRrUiAllowed)
inline bool IndustrialFull()
{
    auto config = Config::Instance();
    const std::string presentation = config->MenuUiPresentation.value_or_default();
    return config->MenuUiTheme.value_or_default() != "olive" && presentation != "compact" && presentation != "sputnik";
}

// At startup, right after the plugins are loaded, before the game creates any feature: the backend for this start (every
// interface, it is a fix), and OpenRR's own menu off before any key can open it (the interface that has its settings)
inline void EarlyTakeover()
{
    if (GetModuleHandleW(L"openrr-amd.asi") == nullptr)
        return;
    std::lock_guard<std::mutex> lock(Mutex());
    auto& ini = SharedIni();
    if (!ini.Load())
        return;
    ApplyBackend(ini);
    if (IndustrialFull())
        ApplyFirstTakeover(ini);
}

// The user's backend choice from the menu: kept in IlionBackend and applied to OpenRR's Backend at once (it takes effect
// when the game creates Ray Reconstruction again)
inline void SetUserBackend(const std::string& choice)
{
    std::lock_guard<std::mutex> lock(Mutex());
    auto& ini = SharedIni();
    ini.Set("IlionBackend", choice);
    ApplyBackend(ini);
}

inline std::string GetUserBackend()
{
    std::lock_guard<std::mutex> lock(Mutex());
    return UserBackend(SharedIni());
}

// Refreshed at most twice a second: the install every 5 s, the ini when it changed, the log where it was left
inline Snapshot Get()
{
    std::lock_guard<std::mutex> lock(Mutex());
    static Snapshot snap;
    static LogReader reader;
    static ULONGLONG lastPoll = 0, lastScan = 0;
    static bool takeoverDone = false;
    const ULONGLONG now = GetTickCount64();
    if (lastScan == 0 || now - lastScan > 5000)
    {
        snap.install = ScanInstall();
        lastScan = now;
    }
    if (lastPoll == 0 || now - lastPoll > 500)
    {
        lastPoll = now;
        if (snap.install.loaded || snap.install.iniFound)
        {
            auto& ini = SharedIni();
            ini.Refresh();
            if (!takeoverDone && snap.install.loaded && ini.Loaded() && IndustrialFull())
            {
                ApplyFirstTakeover(ini);
                takeoverDone = true;
            }
            snap.iniLoaded = ini.Loaded();
        }
        if (snap.install.loaded)
            reader.Poll(snap.telemetry);
    }
    return snap;
}

// Settings from the menu: written at once (the plugin reads the file again when it changes)
inline std::string GetValue(const char* key, const char* fallback)
{
    std::lock_guard<std::mutex> lock(Mutex());
    return SharedIni().Get(key, fallback);
}
inline bool GetBool(const char* key, bool fallback)
{
    std::lock_guard<std::mutex> lock(Mutex());
    return SharedIni().GetBool(key, fallback);
}
inline float GetFloat(const char* key, float fallback)
{
    std::lock_guard<std::mutex> lock(Mutex());
    return SharedIni().GetFloat(key, fallback);
}
inline bool SetValue(const char* key, const std::string& value)
{
    std::lock_guard<std::mutex> lock(Mutex());
    const bool ok = SharedIni().Set(key, value);
    LOG_INFO("OpenRR: {} = {}{}", key, value, ok ? "" : " (openrr-amd.ini could not be written)");
    return ok;
}
inline bool SetBool(const char* key, bool value) { return SetValue(key, value ? "true" : "false"); }

// Seconds since the plugin wrote its last figure (as ILION read it); -1 without one
inline int StatAgeSeconds(const Telemetry& t)
{
    if (!t.haveStats || t.statTick == 0)
        return -1;
    return (int) ((GetTickCount64() - t.statTick) / 1000);
}

// The upscaler of ILION is paused while the plugin denoises and upscales (the game asks for DLSSD instead of DLSS)
// ILION's own upscaler running means the game uses its normal upscaling now (DLSS instead of DLSSD). Found in Cyberpunk:
// a DLSS preset change can make the game drop Ray Reconstruction while its feature stays alive (released only at exit)
inline bool IlionUpscalerRunning()
{
    IFeature* feature = State::Instance().currentFeature;
    return feature != nullptr && feature->IsInited() && !feature->IsFrozen();
}

inline bool RrRunning(const Telemetry& t)
{
    return t.featureAlive && (t.state == RunState::Ready || t.state == RunState::Building) && !IlionUpscalerRunning();
}

// The switch is on but the game started while it was off: the game was told there is no Ray Reconstruction (its option
// is grey) until it restarts. The plugin's check runs within the first seconds; 15 s leaves room for a slow start
inline bool NeedsRestartToOffer(const Telemetry& t, bool enabled)
{
    return enabled && t.hooks && !t.offered && !t.featureAlive && t.firstLineTick != 0 && GetTickCount64() - t.firstLineTick > 15000;
}
} // namespace IlionOpenRr
