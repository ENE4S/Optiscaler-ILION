#pragma once

#include "HwTelemetry.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// Manual benchmark of ILION.
//
// The user starts every test. For the chosen time the frame time of every presented frame is stored, taken
// from the value the menu already measures (time between two presents). Nothing is switched automatically and
// the upscaler is never touched. Statistics are calculated once, after the test, on a thread of its own.
//
// How the values are calculated (N frames, frame times sorted from fastest to slowest):
//   Average FPS   = N * 1000 / sum of all frame times
//   Median FPS    = 1000 / median frame time (nearest rank)
//   P95 / P99     = frame time at rank ceil(0.95 * N) / ceil(0.99 * N)
//   1% low        = 1000 / average frame time of the slowest 1% of the frames, needs 100 frames or more
//   0.1% low      = same with the slowest 0.1%, needs 1000 frames or more
//   Variability   = standard deviation of the frame times, also as % of their mean
//   Stutter       = a frame that took more than twice the median frame time
// 1% low and 0.1% low are averages of the slowest frames, they are not the single slowest frame (that's Max ms).
class IlionBenchmark
{
  public:
    enum class State : int
    {
        Idle,
        Stabilizing, // started, waiting before the first frame is stored
        Recording,
        Processing, // statistics are being calculated
        Finished,   // result ready
        Aborted,    // stopped by itself, AbortReason says why
    };

    // What was in use when the test started, filled by the caller
    struct Conditions
    {
        std::string game;
        std::string upscaler;
        std::string resolution;
        std::string fsrVersion;
        std::string dllPath;
        std::string dllFileVersion;
        std::string dllSha256;
        std::string frameGeneration;

        // Upscale dispatches can be seen (FSR through the ffx api on DX12), needed to count frames without them
        bool upscaleObservable = false;
    };

    struct HwStat
    {
        uint32_t samples = 0;
        double sum = 0.0;
        float max = 0.0f;
        std::string source;
        std::string sensor;

        bool Valid() const { return samples > 0; }
        double Average() const { return samples > 0 ? sum / samples : 0.0; }
    };

    struct Result
    {
        std::string id;
        std::string dateTime;
        int requestedSeconds = 0;
        bool stoppedEarly = false;

        uint64_t frames = 0;
        double durationMs = 0.0;

        double avgFps = 0.0;
        double medianFps = 0.0;

        bool low1Valid = false;
        double low1Fps = 0.0;
        bool low01Valid = false;
        double low01Fps = 0.0;

        double meanMs = 0.0;
        double medianMs = 0.0;
        double p95Ms = 0.0;
        double p99Ms = 0.0;
        double minMs = 0.0;
        double maxMs = 0.0;
        double stdevMs = 0.0;
        double variationPercent = 0.0;

        uint64_t stutterCount = 0;
        double stutterPerMinute = 0.0;
        double stutterTotalMs = 0.0;
        double stutterLongestMs = 0.0;

        // Presented frames for which no upscale dispatch was seen (menus, loading, upscaler off)
        uint64_t framesWithoutUpscale = 0;

        // Frame generation was active for at least one frame, presented frames then include generated ones
        bool fgSeen = false;
        int fgMultiplier = 1;

        HwStat hardware[HwTelemetry::MetricCount];
        uint32_t hardwarePollMs = 500;

        Conditions conditions;

        std::string savedPath;
    };

    static constexpr double StabilizeMs = 2000.0;

    static State GetState() { return (State) _state.load(std::memory_order_acquire); }
    static bool Running()
    {
        const auto state = GetState();
        return state == State::Stabilizing || state == State::Recording;
    }

    static int RequestedSeconds() { return _requestedSeconds; }
    static double ElapsedMs() { return _recordedMs; }
    static uint64_t FrameCount() { return _samples.size(); }
    static double StabilizeLeftMs(double nowMs) { return (std::max)(0.0, _recordStartMs - nowMs); }
    static const char* AbortReason() { return _abortReason; }

    // Only valid while the state is Finished
    static const Result& GetResult() { return _result; }

    // Hash of the dll is calculated in the background and might arrive after the test started
    static void SetDllSha256(const std::string& sha256)
    {
        if (GetState() == State::Finished && !sha256.empty())
            _result.conditions.dllSha256 = sha256;
    }

    static void Start(int seconds, double nowMs, const Conditions& conditions, HMODULE activeModule)
    {
        if (Running() || GetState() == State::Processing)
            return;

        _requestedSeconds = seconds;
        _conditions = conditions;
        _module = activeModule;
        _recordStartMs = nowMs + StabilizeMs;
        _recordedMs = 0.0;
        _framesWithoutUpscale = 0;
        _fgSeen = false;
        _fgMultiplier = 1;
        _lastHwTick = 0;
        _abortReason = "";

        for (auto& stat : _hardware)
            stat = {};

        // The only allocation of a test, room for 1000 fps for the whole time
        _samples.clear();
        _samples.reserve((size_t) seconds * 1000);

        _state.store((int) State::Stabilizing, std::memory_order_release);
    }

    // Ends the test now and keeps what was recorded
    static void Stop()
    {
        if (GetState() == State::Recording && !_samples.empty())
            Finish(true);
        else if (Running())
            Cancel();
    }

    // Ends the test and discards it
    static void Cancel()
    {
        if (!Running())
            return;

        _samples.clear();
        _state.store((int) State::Idle, std::memory_order_release);
    }

    static void Dismiss()
    {
        const auto state = GetState();

        if (state == State::Finished || state == State::Aborted)
            _state.store((int) State::Idle, std::memory_order_release);
    }

    // Once per presented frame, from the thread that draws the menu
    static void OnFrame(double frameTimeMs, double nowMs, HMODULE activeModule, bool upscaleDispatched, bool fgActive,
                        int fgMultiplier, const HwTelemetry::Snapshot& hardware)
    {
        const auto state = GetState();

        if (state != State::Stabilizing && state != State::Recording)
            return;

        // A test belongs to one upscaler dll. If another one starts executing, the test isn't valid anymore.
        if (upscaleDispatched && activeModule != _module)
        {
            _samples.clear();
            _abortReason = "The upscaler dll changed during the test";
            _state.store((int) State::Aborted, std::memory_order_release);
            return;
        }

        if (state == State::Stabilizing)
        {
            if (nowMs >= _recordStartMs)
                _state.store((int) State::Recording, std::memory_order_release);

            // The frame that ends the wait still belongs to it
            return;
        }

        if (!(frameTimeMs > 0.0) || !std::isfinite(frameTimeMs))
            return;

        // Never grows past what was reserved at the start
        if (_samples.size() < _samples.capacity())
            _samples.push_back((float) frameTimeMs);

        _recordedMs += frameTimeMs;

        if (!upscaleDispatched)
            _framesWithoutUpscale++;

        if (fgActive)
        {
            _fgSeen = true;
            _fgMultiplier = (std::max)(_fgMultiplier, fgMultiplier);
        }

        // One hardware sample per poll of the telemetry thread, not per frame
        if (hardware.polledTick != 0 && hardware.polledTick != _lastHwTick)
        {
            _lastHwTick = hardware.polledTick;

            for (int i = 0; i < HwTelemetry::MetricCount; i++)
            {
                const auto& value = hardware.values[i];

                if (!value.valid)
                    continue;

                auto& stat = _hardware[i];
                stat.samples++;
                stat.sum += value.value;
                stat.max = (std::max)(stat.max, value.value);
                stat.source = HwTelemetry::SourceName(value.source);
                stat.sensor = value.label;
            }
        }

        if (_recordedMs >= (double) _requestedSeconds * 1000.0 || _samples.size() >= _samples.capacity())
            Finish(false);
    }

    // Statistics of a set of frame times, also used by the tests of this file
    static void Calculate(const std::vector<float>& frameTimes, Result& result)
    {
        const size_t count = frameTimes.size();
        result.frames = count;

        if (count == 0)
            return;

        std::vector<float> sorted = frameTimes;
        std::sort(sorted.begin(), sorted.end());

        double sum = 0.0;

        for (const float value : sorted)
            sum += value;

        const auto rank = [&](double fraction)
        {
            const size_t index = (size_t) std::ceil(fraction * (double) count);
            return (double) sorted[std::clamp<size_t>(index, 1, count) - 1];
        };

        result.durationMs = sum;
        result.meanMs = sum / (double) count;
        result.avgFps = (double) count * 1000.0 / sum;
        result.medianMs = rank(0.5);
        result.medianFps = 1000.0 / result.medianMs;
        result.p95Ms = rank(0.95);
        result.p99Ms = rank(0.99);
        result.minMs = sorted.front();
        result.maxMs = sorted.back();

        // Average of the slowest part of the frames, as fps
        const auto slowest = [&](size_t frames)
        {
            double slowSum = 0.0;

            for (size_t i = count - frames; i < count; i++)
                slowSum += sorted[i];

            return 1000.0 / (slowSum / (double) frames);
        };

        result.low1Valid = count >= 100;
        result.low01Valid = count >= 1000;

        if (result.low1Valid)
            result.low1Fps = slowest(count / 100);

        if (result.low01Valid)
            result.low01Fps = slowest(count / 1000);

        double squares = 0.0;

        for (const float value : sorted)
            squares += ((double) value - result.meanMs) * ((double) value - result.meanMs);

        result.stdevMs = std::sqrt(squares / (double) count);
        result.variationPercent = result.meanMs > 0.0 ? result.stdevMs / result.meanMs * 100.0 : 0.0;

        const double stutterLimit = result.medianMs * 2.0;

        for (const float value : sorted)
        {
            if ((double) value <= stutterLimit)
                continue;

            result.stutterCount++;
            result.stutterTotalMs += value;
            result.stutterLongestMs = (std::max)(result.stutterLongestMs, (double) value);
        }

        result.stutterPerMinute = sum > 0.0 ? (double) result.stutterCount * 60000.0 / sum : 0.0;
    }

    // Writes the finished result: one json per test, one line in benchmarks.csv, and the frame times.
    // Existing files are never overwritten.
    static bool Save(const std::filesystem::path& folder, std::string* error)
    {
        if (GetState() != State::Finished)
            return false;

        std::error_code ec;
        std::filesystem::create_directories(folder, ec);

        auto jsonPath = folder / (_result.id + ".json");

        for (int n = 2; std::filesystem::exists(jsonPath, ec) && n < 100; n++)
            jsonPath = folder / (_result.id + "_" + std::to_string(n) + ".json");

        if (std::filesystem::exists(jsonPath, ec))
        {
            if (error != nullptr)
                *error = "A result with this id already exists";

            return false;
        }

        const auto baseName = jsonPath.stem().string();

        {
            std::ofstream json(jsonPath, std::ios::binary);

            if (!json)
            {
                if (error != nullptr)
                    *error = "Can't write to the benchmarks folder";

                return false;
            }

            WriteJson(json, _result, baseName);
        }

        {
            std::ofstream frames(folder / (baseName + "_frametimes.csv"), std::ios::binary);
            frames << "frame,frametime_ms\n";

            for (size_t i = 0; i < _samples.size(); i++)
                frames << i << ',' << _samples[i] << '\n';
        }

        {
            const auto csvPath = folder / "benchmarks.csv";
            const bool newFile = !std::filesystem::exists(csvPath, ec);

            std::ofstream csv(csvPath, std::ios::binary | std::ios::app);

            if (newFile)
                csv << CsvHeader() << '\n';

            csv << CsvLine(_result, baseName) << '\n';
        }

        _result.savedPath = jsonPath.string();
        return true;
    }

    static std::string CsvHeader()
    {
        return "id,date_time,game,upscaler,resolution,fsr_version,dll_file_version,dll_sha256,dll_path,frame_generation,"
               "fg_seen,fg_multiplier,requested_s,stopped_early,frames,duration_ms,avg_fps,median_fps,low_1_fps,"
               "low_01_fps,mean_ms,median_ms,p95_ms,p99_ms,min_ms,max_ms,stdev_ms,variation_percent,stutter_count,"
               "stutter_per_minute,stutter_total_ms,stutter_longest_ms,frames_without_upscale,gpu_usage_avg,"
               "gpu_usage_max,gpu_temp_avg,gpu_temp_max,gpu_hotspot_max,vram_used_avg_mb,vram_used_max_mb,"
               "cpu_usage_avg,cpu_usage_max,cpu_temp_avg,cpu_temp_max";
    }

  private:
    inline static std::atomic<int> _state { (int) State::Idle };
    inline static int _requestedSeconds = 60;
    inline static Conditions _conditions;
    inline static HMODULE _module = nullptr;
    inline static double _recordStartMs = 0.0;
    inline static double _recordedMs = 0.0;
    inline static uint64_t _framesWithoutUpscale = 0;
    inline static bool _fgSeen = false;
    inline static int _fgMultiplier = 1;
    inline static uint64_t _lastHwTick = 0;
    inline static const char* _abortReason = "";
    inline static HwStat _hardware[HwTelemetry::MetricCount];
    inline static std::vector<float> _samples;
    inline static Result _result;

    static void Finish(bool stoppedEarly)
    {
        _state.store((int) State::Processing, std::memory_order_release);

        Result result;
        result.requestedSeconds = _requestedSeconds;
        result.stoppedEarly = stoppedEarly;
        result.framesWithoutUpscale = _framesWithoutUpscale;
        result.fgSeen = _fgSeen;
        result.fgMultiplier = _fgMultiplier;
        result.conditions = _conditions;

        for (int i = 0; i < HwTelemetry::MetricCount; i++)
            result.hardware[i] = _hardware[i];

        SYSTEMTIME time;
        GetLocalTime(&time);

        char buffer[64];
        snprintf(buffer, sizeof(buffer), "%04d%02d%02d_%02d%02d%02d", time.wYear, time.wMonth, time.wDay, time.wHour,
                 time.wMinute, time.wSecond);
        result.id = buffer;

        snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d %02d:%02d:%02d", time.wYear, time.wMonth, time.wDay,
                 time.wHour, time.wMinute, time.wSecond);
        result.dateTime = buffer;

        // Sorting a couple of hundred thousand values is kept away from the thread that renders
        std::thread(
            [result]() mutable
            {
                Calculate(_samples, result);
                _result = std::move(result);
                _state.store((int) State::Finished, std::memory_order_release);
            })
            .detach();
    }

    static std::string JsonEscape(const std::string& text)
    {
        std::string out;
        out.reserve(text.size() + 8);

        for (const char c : text)
        {
            switch (c)
            {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                out += (unsigned char) c < 0x20 ? ' ' : c;
                break;
            }
        }

        return out;
    }

    static std::string Number(double value)
    {
        char buffer[48];
        snprintf(buffer, sizeof(buffer), "%.3f", std::isfinite(value) ? value : 0.0);
        return buffer;
    }

    static void WriteJson(std::ostream& out, const Result& r, const std::string& baseName)
    {
        const auto text = [&](const char* key, const std::string& value, bool comma = true)
        { out << "    \"" << key << "\": \"" << JsonEscape(value) << "\"" << (comma ? ",\n" : "\n"); };

        const auto number = [&](const char* key, double value, bool comma = true)
        { out << "    \"" << key << "\": " << Number(value) << (comma ? ",\n" : "\n"); };

        const auto optional = [&](const char* key, bool valid, double value, bool comma = true)
        { out << "    \"" << key << "\": " << (valid ? Number(value) : std::string("null")) << (comma ? ",\n" : "\n"); };

        out << "{\n";
        out << "  \"id\": \"" << JsonEscape(baseName) << "\",\n";
        out << "  \"date_time\": \"" << JsonEscape(r.dateTime) << "\",\n";
        out << "  \"frametimes_file\": \"" << JsonEscape(baseName + "_frametimes.csv") << "\",\n";

        out << "  \"conditions\": {\n";
        text("game", r.conditions.game);
        text("upscaler", r.conditions.upscaler);
        text("resolution", r.conditions.resolution);
        text("fsr_version", r.conditions.fsrVersion);
        text("dll_path", r.conditions.dllPath);
        text("dll_file_version", r.conditions.dllFileVersion);
        text("dll_sha256", r.conditions.dllSha256);
        text("frame_generation", r.conditions.frameGeneration);
        out << "    \"fg_seen_during_test\": " << (r.fgSeen ? "true" : "false") << ",\n";
        number("fg_multiplier", r.fgMultiplier, false);
        out << "  },\n";

        out << "  \"test\": {\n";
        number("requested_seconds", r.requestedSeconds);
        out << "    \"stopped_early\": " << (r.stoppedEarly ? "true" : "false") << ",\n";
        number("stabilize_ms_before_recording", StabilizeMs);
        number("frames", (double) r.frames);
        optional("frames_without_upscale", r.conditions.upscaleObservable, (double) r.framesWithoutUpscale);
        number("duration_ms", r.durationMs, false);
        out << "  },\n";

        out << "  \"metrics\": {\n";
        number("avg_fps", r.avgFps);
        number("median_fps", r.medianFps);
        optional("low_1_percent_fps", r.low1Valid, r.low1Fps);
        optional("low_01_percent_fps", r.low01Valid, r.low01Fps);
        number("mean_ms", r.meanMs);
        number("median_ms", r.medianMs);
        number("p95_ms", r.p95Ms);
        number("p99_ms", r.p99Ms);
        number("min_ms", r.minMs);
        number("max_ms", r.maxMs);
        number("stdev_ms", r.stdevMs);
        number("variation_percent", r.variationPercent);
        number("stutter_count", (double) r.stutterCount);
        number("stutter_per_minute", r.stutterPerMinute);
        number("stutter_total_ms", r.stutterTotalMs);
        number("stutter_longest_ms", r.stutterLongestMs, false);
        out << "  },\n";

        static constexpr const char* hardwareKeys[HwTelemetry::MetricCount] = {
            "gpu_usage_percent", "gpu_temperature_c", "gpu_hotspot_c", "vram_used_mb",
            "vram_total_mb",     "cpu_usage_percent", "cpu_temperature_c"
        };

        out << "  \"hardware\": {\n";
        out << "    \"poll_ms\": " << r.hardwarePollMs << ",\n";

        for (int i = 0; i < HwTelemetry::MetricCount; i++)
        {
            const auto& stat = r.hardware[i];
            out << "    \"" << hardwareKeys[i] << "\": ";

            if (stat.Valid())
            {
                out << "{ \"avg\": " << Number(stat.Average()) << ", \"max\": " << Number(stat.max)
                    << ", \"samples\": " << stat.samples << ", \"source\": \"" << JsonEscape(stat.source)
                    << "\", \"sensor\": \"" << JsonEscape(stat.sensor) << "\" }";
            }
            else
            {
                out << "null";
            }

            out << (i + 1 < HwTelemetry::MetricCount ? ",\n" : "\n");
        }

        out << "  },\n";

        out << "  \"method\": {\n";
        text("frame_time", "time between two presents as measured by the menu, every presented frame");
        text("avg_fps", "frames * 1000 / sum of frame times");
        text("median_and_percentiles", "nearest rank on the sorted frame times");
        text("low_1_percent_fps", "1000 / average frame time of the slowest 1% of the frames, needs 100 frames");
        text("low_01_percent_fps", "1000 / average frame time of the slowest 0.1% of the frames, needs 1000 frames");
        text("stutter", "a frame that took more than twice the median frame time");
        text("frame_generation",
             "with frame generation active the presented frames include generated ones, they can't be told apart",
             false);
        out << "  }\n";
        out << "}\n";
    }

    static std::string CsvText(const std::string& text)
    {
        std::string out = "\"";

        for (const char c : text)
        {
            if (c == '"')
                out += "\"\"";
            else if (c != '\r' && c != '\n')
                out += c;
        }

        return out + "\"";
    }

    static std::string CsvLine(const Result& r, const std::string& baseName)
    {
        std::string line;

        const auto add = [&](const std::string& value)
        {
            if (!line.empty())
                line += ',';

            line += value;
        };

        const auto hw = [&](HwTelemetry::Metric metric, bool average)
        {
            const auto& stat = r.hardware[metric];
            add(stat.Valid() ? Number(average ? stat.Average() : stat.max) : std::string());
        };

        add(CsvText(baseName));
        add(CsvText(r.dateTime));
        add(CsvText(r.conditions.game));
        add(CsvText(r.conditions.upscaler));
        add(CsvText(r.conditions.resolution));
        add(CsvText(r.conditions.fsrVersion));
        add(CsvText(r.conditions.dllFileVersion));
        add(CsvText(r.conditions.dllSha256));
        add(CsvText(r.conditions.dllPath));
        add(CsvText(r.conditions.frameGeneration));
        add(r.fgSeen ? "1" : "0");
        add(std::to_string(r.fgMultiplier));
        add(std::to_string(r.requestedSeconds));
        add(r.stoppedEarly ? "1" : "0");
        add(std::to_string(r.frames));
        add(Number(r.durationMs));
        add(Number(r.avgFps));
        add(Number(r.medianFps));
        add(r.low1Valid ? Number(r.low1Fps) : std::string());
        add(r.low01Valid ? Number(r.low01Fps) : std::string());
        add(Number(r.meanMs));
        add(Number(r.medianMs));
        add(Number(r.p95Ms));
        add(Number(r.p99Ms));
        add(Number(r.minMs));
        add(Number(r.maxMs));
        add(Number(r.stdevMs));
        add(Number(r.variationPercent));
        add(std::to_string(r.stutterCount));
        add(Number(r.stutterPerMinute));
        add(Number(r.stutterTotalMs));
        add(Number(r.stutterLongestMs));
        add(r.conditions.upscaleObservable ? std::to_string(r.framesWithoutUpscale) : std::string());
        hw(HwTelemetry::GpuUsage, true);
        hw(HwTelemetry::GpuUsage, false);
        hw(HwTelemetry::GpuTemp, true);
        hw(HwTelemetry::GpuTemp, false);
        hw(HwTelemetry::GpuHotspot, false);
        hw(HwTelemetry::VramUsed, true);
        hw(HwTelemetry::VramUsed, false);
        hw(HwTelemetry::CpuUsage, true);
        hw(HwTelemetry::CpuUsage, false);
        hw(HwTelemetry::CpuTemp, true);
        hw(HwTelemetry::CpuTemp, false);

        return line;
    }
};
