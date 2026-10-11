#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

// Comparison of saved ILION benchmarks.
//
// Reads the tests back from benchmarks.csv (written by IlionBenchmark) and compares the ones the user picked.
// Only values that were measured are used. A difference counts as real only when it's bigger than a margin:
//   average FPS   2 %
//   1% low        4 %  (noisier than the average)
//   variability   0.5 points and 10 % of the value
// When several of the picked tests were made with the same dll, the spread between them is taken as the noise
// of the measurement and replaces the margin if it's bigger. Inside the margin the result is a tie, and tests
// made under different conditions don't get a winner at all.
//
// Performance: highest average FPS, 1% low and 0.1% low are reported next to it.
// Stability: lowest frame time variation (standard deviation as % of the mean), P99 against the median and
//            stutters per minute are reported next to it.
// Balance: the same test wins both, or wins one and ties the other. Otherwise there's no single best balance.
//
// This compares performance and stability only. It says nothing about image quality.
class IlionCompare
{
  public:
    struct Number
    {
        bool valid = false;
        double value = 0.0;
    };

    struct Test
    {
        std::string id;
        std::string dateTime;
        std::string game;
        std::string upscaler;
        std::string resolution;
        std::string fsrVersion;
        std::string dllFileVersion;
        std::string dllSha256;
        std::string frameGeneration;
        bool fgSeen = false;
        int fgMultiplier = 1;
        int requestedSeconds = 0;
        bool stoppedEarly = false;

        Number frames, durationMs;
        Number avgFps, medianFps, low1Fps, low01Fps;
        Number meanMs, medianMs, p95Ms, p99Ms, minMs, maxMs, stdevMs, variationPercent;
        Number stutterCount, stutterPerMinute, stutterTotalMs, stutterLongestMs;
        Number framesWithoutUpscale;
        Number gpuUsageAvg, gpuUsageMax, gpuTempAvg, gpuTempMax, gpuHotspotMax;
        Number vramUsedAvg, vramUsedMax, cpuUsageAvg, cpuUsageMax, cpuTempAvg, cpuTempMax;

        // FSR version when there's one, the upscaler otherwise
        std::string Name() const { return !fsrVersion.empty() ? fsrVersion : upscaler; }
    };

    enum class Issue : int
    {
        DifferentGame,
        DifferentResolution,
        DifferentUpscaler,
        DifferentFrameGeneration,
        DifferentDuration,
        StoppedEarly,
        FramesWithoutUpscale,
        FewFrames,
        SceneNotVerified,
    };

    enum class Kind : int
    {
        Inconclusive,
        Tie,
        Winner,
    };

    enum class Reason : int
    {
        None,
        TooFewTests,
        NotComparable,
        MissingData,
        WithinMargin,
        SameDll,  // every picked test was made with the same dll
        Mixed,    // best in the main value, clearly worse in a supporting one
        TradeOff, // one test is faster, another one is more stable
        BothCategories,
        FasterAndAsStable,
        MoreStableAndAsFast,
    };

    struct Verdict
    {
        Kind kind = Kind::Inconclusive;
        Reason reason = Reason::None;
        int winner = -1;
        int runnerUp = -1;
        std::vector<int> tied;

        // Performance, winner against the runner up, in %
        double avgAdvantage = 0.0;
        Number low1Advantage;
        Number low01Advantage;

        // Winner's 1% low is clearly lower than the one of this test
        int lowsWorseThan = -1;

        // Stability, winner and runner up
        double variationWinner = 0.0;
        double variationRunnerUp = 0.0;
        Number p99RatioWinner, p99RatioRunnerUp;
        Number stutterWinner, stutterRunnerUp;

        // Balance when it's a trade off
        int faster = -1;
        int moreStable = -1;
    };

    // Tests made with the same dll count as one candidate, with the average of their values.
    // Winner, runner up and tied in the verdicts are positions in Analysis::candidates.
    struct Candidate
    {
        std::string name;
        std::vector<int> tests; // positions in the list that was analyzed
        Test average;
    };

    struct Analysis
    {
        std::vector<Candidate> candidates;

        // Conditions that differ, no winner is named with these
        std::vector<Issue> blocking;

        // Things to keep in mind when reading the result
        std::vector<Issue> warnings;

        // Margins that were used, in %
        double marginAvg = 2.0;
        double marginLow1 = 4.0;
        bool marginFromRepeats = false;

        Verdict performance;
        Verdict stability;
        Verdict balance;
    };

    static constexpr double MinMarginAvg = 2.0;
    static constexpr double MinMarginLow1 = 4.0;
    static constexpr double MinVariationPoints = 0.5;
    static constexpr double MinVariationRelative = 10.0;

    // Difference of a value against the reference, in %
    static Number PercentDifference(const Number& value, const Number& reference)
    {
        if (!value.valid || !reference.valid || reference.value == 0.0)
            return {};

        return { true, (value.value - reference.value) / reference.value * 100.0 };
    }

    static Number AbsoluteDifference(const Number& value, const Number& reference)
    {
        if (!value.valid || !reference.valid)
            return {};

        return { true, value.value - reference.value };
    }

    // Tests saved so far, oldest first. Empty when there's no file yet.
    static std::vector<Test> LoadHistory(const std::filesystem::path& csvPath)
    {
        std::vector<Test> tests;
        std::ifstream file(csvPath, std::ios::binary);

        if (!file)
            return tests;

        std::string line;

        if (!std::getline(file, line))
            return tests;

        const auto header = ParseCsvLine(line);
        std::unordered_map<std::string, size_t> columns;

        for (size_t i = 0; i < header.size(); i++)
            columns[header[i]] = i;

        while (std::getline(file, line))
        {
            if (line.empty() || line == "\r")
                continue;

            const auto fields = ParseCsvLine(line);

            const auto text = [&](const char* name) -> std::string
            {
                const auto it = columns.find(name);
                return it != columns.end() && it->second < fields.size() ? fields[it->second] : std::string();
            };

            const auto number = [&](const char* name) -> Number
            {
                const auto value = text(name);

                if (value.empty())
                    return {};

                char* end = nullptr;
                const double parsed = std::strtod(value.c_str(), &end);

                if (end == value.c_str() || !std::isfinite(parsed))
                    return {};

                return { true, parsed };
            };

            Test test;
            test.id = text("id");

            if (test.id.empty())
                continue;

            test.dateTime = text("date_time");
            test.game = text("game");
            test.upscaler = text("upscaler");
            test.resolution = text("resolution");
            test.fsrVersion = text("fsr_version");
            test.dllFileVersion = text("dll_file_version");
            test.dllSha256 = text("dll_sha256");
            test.frameGeneration = text("frame_generation");
            test.fgSeen = text("fg_seen") == "1";
            test.fgMultiplier = (int) number("fg_multiplier").value;
            test.requestedSeconds = (int) number("requested_s").value;
            test.stoppedEarly = text("stopped_early") == "1";

            test.frames = number("frames");
            test.durationMs = number("duration_ms");
            test.avgFps = number("avg_fps");
            test.medianFps = number("median_fps");
            test.low1Fps = number("low_1_fps");
            test.low01Fps = number("low_01_fps");
            test.meanMs = number("mean_ms");
            test.medianMs = number("median_ms");
            test.p95Ms = number("p95_ms");
            test.p99Ms = number("p99_ms");
            test.minMs = number("min_ms");
            test.maxMs = number("max_ms");
            test.stdevMs = number("stdev_ms");
            test.variationPercent = number("variation_percent");
            test.stutterCount = number("stutter_count");
            test.stutterPerMinute = number("stutter_per_minute");
            test.stutterTotalMs = number("stutter_total_ms");
            test.stutterLongestMs = number("stutter_longest_ms");
            test.framesWithoutUpscale = number("frames_without_upscale");
            test.gpuUsageAvg = number("gpu_usage_avg");
            test.gpuUsageMax = number("gpu_usage_max");
            test.gpuTempAvg = number("gpu_temp_avg");
            test.gpuTempMax = number("gpu_temp_max");
            test.gpuHotspotMax = number("gpu_hotspot_max");
            test.vramUsedAvg = number("vram_used_avg_mb");
            test.vramUsedMax = number("vram_used_max_mb");
            test.cpuUsageAvg = number("cpu_usage_avg");
            test.cpuUsageMax = number("cpu_usage_max");
            test.cpuTempAvg = number("cpu_temp_avg");
            test.cpuTempMax = number("cpu_temp_max");

            tests.push_back(std::move(test));
        }

        return tests;
    }

    // Removes saved tests for good: their rows of benchmarks.csv, their json and their frame times file.
    // Only files named exactly after one of the ids are touched. Returns how many tests were removed.
    static int DeleteTests(const std::filesystem::path& folder, const std::vector<std::string>& ids,
                           std::string* error = nullptr)
    {
        const auto fail = [&](const char* text)
        {
            if (error != nullptr)
                *error = text;

            return 0;
        };

        // An id is a file name made by the benchmark, never a path
        for (const auto& id : ids)
        {
            if (id.empty() || id.find_first_of("\\/:*?\"<>|") != std::string::npos || id.find("..") != std::string::npos)
                return fail("Invalid test id");
        }

        const auto wanted = [&](const std::string& id) { return std::find(ids.begin(), ids.end(), id) != ids.end(); };

        const auto csvPath = folder / "benchmarks.csv";
        const auto tempPath = folder / "benchmarks.csv.tmp";

        std::vector<std::string> removed;

        {
            std::ifstream input(csvPath, std::ios::binary);

            if (!input)
                return fail("Can't read the saved tests");

            std::ofstream output(tempPath, std::ios::binary | std::ios::trunc);

            if (!output)
                return fail("Can't write to the benchmarks folder");

            std::string line;
            bool header = true;

            while (std::getline(input, line))
            {
                if (!line.empty() && line.back() == '\r')
                    line.pop_back();

                if (line.empty())
                    continue;

                if (!header)
                {
                    const auto fields = ParseCsvLine(line);

                    if (!fields.empty() && wanted(fields[0]))
                    {
                        removed.push_back(fields[0]);
                        continue;
                    }
                }

                header = false;
                output << line << '\n';
            }

            if (!output.good())
                return fail("Can't write to the benchmarks folder");
        }

        std::error_code ec;

        if (removed.empty())
        {
            std::filesystem::remove(tempPath, ec);
            return 0;
        }

        // The list is replaced first, a test without its row doesn't exist for ILION anymore
        std::filesystem::rename(tempPath, csvPath, ec);

        if (ec)
        {
            std::filesystem::remove(tempPath, ec);
            return fail("Can't update the saved tests");
        }

        for (const auto& id : removed)
        {
            std::filesystem::remove(folder / (id + ".json"), ec);
            std::filesystem::remove(folder / (id + "_frametimes.csv"), ec);
        }

        return (int) removed.size();
    }

    static Analysis Analyze(const std::vector<const Test*>& tests)
    {
        Analysis analysis;
        const int count = (int) tests.size();

        analysis.warnings.push_back(Issue::SceneNotVerified);

        if (count < 2)
        {
            analysis.performance.reason = Reason::TooFewTests;
            analysis.stability.reason = Reason::TooFewTests;
            analysis.balance.reason = Reason::TooFewTests;
            return analysis;
        }

        CheckConditions(tests, analysis);
        CalculateMargins(tests, analysis);

        if (!analysis.blocking.empty())
        {
            analysis.performance.reason = Reason::NotComparable;
            analysis.stability.reason = Reason::NotComparable;
            analysis.balance.reason = Reason::NotComparable;
            return analysis;
        }

        BuildCandidates(tests, analysis);

        // Runs of one dll can't beat each other, what differs between them is the noise of the measurement
        if (analysis.candidates.size() < 2)
        {
            analysis.performance.reason = Reason::SameDll;
            analysis.stability.reason = Reason::SameDll;
            analysis.balance.reason = Reason::SameDll;
            return analysis;
        }

        std::vector<const Test*> averages;

        for (const auto& candidate : analysis.candidates)
            averages.push_back(&candidate.average);

        analysis.performance = Performance(averages, analysis);
        analysis.stability = Stability(averages);
        analysis.balance = Balance(analysis.performance, analysis.stability);

        return analysis;
    }

  private:
    // Quoted fields, quotes inside a field are doubled
    static std::vector<std::string> ParseCsvLine(const std::string& line)
    {
        std::vector<std::string> fields;
        std::string field;
        bool quoted = false;

        for (size_t i = 0; i < line.size(); i++)
        {
            const char c = line[i];

            if (quoted)
            {
                if (c == '"' && i + 1 < line.size() && line[i + 1] == '"')
                {
                    field += '"';
                    i++;
                }
                else if (c == '"')
                {
                    quoted = false;
                }
                else
                {
                    field += c;
                }
            }
            else if (c == '"')
            {
                quoted = true;
            }
            else if (c == ',')
            {
                fields.push_back(std::move(field));
                field.clear();
            }
            else if (c != '\r' && c != '\n')
            {
                field += c;
            }
        }

        fields.push_back(std::move(field));
        return fields;
    }

    static bool Contains(const std::vector<int>& values, int value)
    {
        return std::find(values.begin(), values.end(), value) != values.end();
    }

    // "FSR 4.1.1" and "FSR 4.1.0" are the same technology, the version belongs to the dll being compared
    static std::string Technology(const std::string& upscaler)
    {
        const auto space = upscaler.find(' ');
        return space == std::string::npos ? upscaler : upscaler.substr(0, space);
    }

    static void CheckConditions(const std::vector<const Test*>& tests, Analysis& analysis)
    {
        const auto& first = *tests.front();

        bool game = false, resolution = false, upscaler = false, fg = false;
        bool duration = false, stopped = false, withoutUpscale = false, fewFrames = false;

        for (const auto* test : tests)
        {
            game = game || _stricmp(test->game.c_str(), first.game.c_str()) != 0;
            resolution = resolution || test->resolution != first.resolution;
            upscaler = upscaler || Technology(test->upscaler) != Technology(first.upscaler);
            fg = fg || test->frameGeneration != first.frameGeneration || test->fgSeen != first.fgSeen ||
                 test->fgMultiplier != first.fgMultiplier;

            if (test->durationMs.valid && first.durationMs.valid && first.durationMs.value > 0.0)
            {
                duration = duration ||
                           std::abs(test->durationMs.value - first.durationMs.value) / first.durationMs.value > 0.10;
            }

            stopped = stopped || test->stoppedEarly;

            if (test->framesWithoutUpscale.valid && test->frames.valid && test->frames.value > 0.0)
                withoutUpscale = withoutUpscale || test->framesWithoutUpscale.value / test->frames.value > 0.02;

            fewFrames = fewFrames || !test->frames.valid || test->frames.value < 1000.0;
        }

        if (game)
            analysis.blocking.push_back(Issue::DifferentGame);

        if (resolution)
            analysis.blocking.push_back(Issue::DifferentResolution);

        if (upscaler)
            analysis.blocking.push_back(Issue::DifferentUpscaler);

        if (fg)
            analysis.blocking.push_back(Issue::DifferentFrameGeneration);

        if (duration)
            analysis.warnings.push_back(Issue::DifferentDuration);

        if (stopped)
            analysis.warnings.push_back(Issue::StoppedEarly);

        if (withoutUpscale)
            analysis.warnings.push_back(Issue::FramesWithoutUpscale);

        if (fewFrames)
            analysis.warnings.push_back(Issue::FewFrames);
    }

    static bool SameDll(const Test& a, const Test& b)
    {
        if (!a.dllSha256.empty() || !b.dllSha256.empty())
            return a.dllSha256 == b.dllSha256;

        return a.Name() == b.Name();
    }

    static void BuildCandidates(const std::vector<const Test*>& tests, Analysis& analysis)
    {
        for (int i = 0; i < (int) tests.size(); i++)
        {
            Candidate* found = nullptr;

            for (auto& candidate : analysis.candidates)
            {
                if (SameDll(*tests[candidate.tests.front()], *tests[i]))
                    found = &candidate;
            }

            if (found == nullptr)
            {
                analysis.candidates.push_back({ tests[i]->Name(), {}, *tests[i] });
                found = &analysis.candidates.back();
            }

            found->tests.push_back(i);
        }

        // Average of every value the verdicts use, only valid when all the runs have it
        static constexpr Number Test::* fields[] = { &Test::avgFps,           &Test::low1Fps,  &Test::low01Fps,
                                                    &Test::variationPercent, &Test::p99Ms,    &Test::medianMs,
                                                    &Test::stutterPerMinute, &Test::meanMs };

        for (auto& candidate : analysis.candidates)
        {
            if (candidate.tests.size() < 2)
                continue;

            for (const auto field : fields)
            {
                Number average { true, 0.0 };

                for (const int index : candidate.tests)
                {
                    const Number& value = tests[index]->*field;
                    average.valid = average.valid && value.valid;
                    average.value += value.value;
                }

                average.value /= (double) candidate.tests.size();
                candidate.average.*field = average;
            }
        }
    }

    // Tests made with the same dll show how much a result moves from one run to the next
    static void CalculateMargins(const std::vector<const Test*>& tests, Analysis& analysis)
    {
        double noiseAvg = 0.0;
        double noiseLow1 = 0.0;
        bool repeats = false;

        for (size_t i = 0; i < tests.size(); i++)
        {
            for (size_t j = i + 1; j < tests.size(); j++)
            {
                const auto& a = *tests[i];
                const auto& b = *tests[j];

                if (!SameDll(a, b))
                    continue;

                repeats = true;

                const auto spread = [](const Number& x, const Number& y)
                {
                    if (!x.valid || !y.valid || x.value <= 0.0 || y.value <= 0.0)
                        return 0.0;

                    return std::abs(x.value - y.value) / (std::min)(x.value, y.value) * 100.0;
                };

                noiseAvg = (std::max)(noiseAvg, spread(a.avgFps, b.avgFps));
                noiseLow1 = (std::max)(noiseLow1, spread(a.low1Fps, b.low1Fps));
            }
        }

        analysis.marginAvg = (std::max)(MinMarginAvg, noiseAvg);
        analysis.marginLow1 = (std::max)(MinMarginLow1, noiseLow1);
        analysis.marginFromRepeats = repeats && (noiseAvg > MinMarginAvg || noiseLow1 > MinMarginLow1);
    }

    static Verdict Performance(const std::vector<const Test*>& tests, const Analysis& analysis)
    {
        Verdict verdict;
        const int count = (int) tests.size();

        std::vector<int> order;

        for (int i = 0; i < count; i++)
        {
            if (!tests[i]->avgFps.valid || tests[i]->avgFps.value <= 0.0)
            {
                verdict.reason = Reason::MissingData;
                return verdict;
            }

            order.push_back(i);
        }

        std::sort(order.begin(), order.end(),
                  [&](int a, int b) { return tests[a]->avgFps.value > tests[b]->avgFps.value; });

        const int best = order[0];
        const int second = order[1];
        const double bestAvg = tests[best]->avgFps.value;

        verdict.avgAdvantage = (bestAvg / tests[second]->avgFps.value - 1.0) * 100.0;

        if (verdict.avgAdvantage <= analysis.marginAvg)
        {
            // Everything inside the margin of the best one is tied with it
            verdict.kind = Kind::Tie;
            verdict.reason = Reason::WithinMargin;

            for (const int index : order)
            {
                if ((bestAvg / tests[index]->avgFps.value - 1.0) * 100.0 <= analysis.marginAvg)
                    verdict.tied.push_back(index);
            }

            return verdict;
        }

        verdict.kind = Kind::Winner;
        verdict.winner = best;
        verdict.runnerUp = second;
        verdict.low1Advantage = PercentDifference(tests[best]->low1Fps, tests[second]->low1Fps);
        verdict.low01Advantage = PercentDifference(tests[best]->low01Fps, tests[second]->low01Fps);

        // More frames on average but clearly lower lows than some other test
        for (int i = 0; i < count; i++)
        {
            if (i == best || !tests[i]->low1Fps.valid || !tests[best]->low1Fps.valid)
                continue;

            if (tests[best]->low1Fps.value < tests[i]->low1Fps.value * (1.0 - analysis.marginLow1 / 100.0))
            {
                verdict.lowsWorseThan = i;
                break;
            }
        }

        return verdict;
    }

    static Number P99Ratio(const Test& test)
    {
        if (!test.p99Ms.valid || !test.medianMs.valid || test.medianMs.value <= 0.0)
            return {};

        return { true, test.p99Ms.value / test.medianMs.value };
    }

    static Verdict Stability(const std::vector<const Test*>& tests)
    {
        Verdict verdict;
        const int count = (int) tests.size();

        std::vector<int> order;

        for (int i = 0; i < count; i++)
        {
            if (!tests[i]->variationPercent.valid)
            {
                verdict.reason = Reason::MissingData;
                return verdict;
            }

            order.push_back(i);
        }

        std::sort(order.begin(), order.end(),
                  [&](int a, int b) { return tests[a]->variationPercent.value < tests[b]->variationPercent.value; });

        const int best = order[0];
        const int second = order[1];
        const double bestVariation = tests[best]->variationPercent.value;

        const auto clearlyLower = [&](double other)
        {
            const double points = other - bestVariation;
            return points >= MinVariationPoints && other > 0.0 && points / other * 100.0 >= MinVariationRelative;
        };

        verdict.variationWinner = bestVariation;
        verdict.variationRunnerUp = tests[second]->variationPercent.value;
        verdict.p99RatioWinner = P99Ratio(*tests[best]);
        verdict.p99RatioRunnerUp = P99Ratio(*tests[second]);
        verdict.stutterWinner = tests[best]->stutterPerMinute;
        verdict.stutterRunnerUp = tests[second]->stutterPerMinute;

        if (!clearlyLower(verdict.variationRunnerUp))
        {
            verdict.kind = Kind::Tie;
            verdict.reason = Reason::WithinMargin;

            for (const int index : order)
            {
                if (!clearlyLower(tests[index]->variationPercent.value))
                    verdict.tied.push_back(index);
            }

            return verdict;
        }

        verdict.winner = best;
        verdict.runnerUp = second;

        // Steadier frame times on the whole but clearly more stutters than some other test
        for (int i = 0; i < count; i++)
        {
            if (i == best || !tests[i]->stutterPerMinute.valid || !tests[best]->stutterPerMinute.valid)
                continue;

            const double mine = tests[best]->stutterPerMinute.value;
            const double theirs = tests[i]->stutterPerMinute.value;

            if (mine > theirs + 2.0 && mine > theirs * 1.5)
            {
                verdict.kind = Kind::Inconclusive;
                verdict.reason = Reason::Mixed;
                verdict.runnerUp = i;
                return verdict;
            }
        }

        verdict.kind = Kind::Winner;
        return verdict;
    }

    static Verdict Balance(const Verdict& performance, const Verdict& stability)
    {
        Verdict verdict;

        const bool fastest = performance.kind == Kind::Winner;
        const bool steadiest = stability.kind == Kind::Winner;

        if (fastest && steadiest)
        {
            if (performance.winner == stability.winner)
            {
                verdict.kind = Kind::Winner;
                verdict.reason = Reason::BothCategories;
                verdict.winner = performance.winner;
            }
            else
            {
                verdict.reason = Reason::TradeOff;
                verdict.faster = performance.winner;
                verdict.moreStable = stability.winner;
            }

            return verdict;
        }

        if (fastest && stability.kind == Kind::Tie && Contains(stability.tied, performance.winner))
        {
            verdict.kind = Kind::Winner;
            verdict.reason = Reason::FasterAndAsStable;
            verdict.winner = performance.winner;
            return verdict;
        }

        if (steadiest && performance.kind == Kind::Tie && Contains(performance.tied, stability.winner))
        {
            verdict.kind = Kind::Winner;
            verdict.reason = Reason::MoreStableAndAsFast;
            verdict.winner = stability.winner;
            return verdict;
        }

        if (performance.kind == Kind::Tie && stability.kind == Kind::Tie)
        {
            verdict.kind = Kind::Tie;
            verdict.reason = Reason::WithinMargin;
            return verdict;
        }

        // A winner in one category that isn't among the best of the other one
        if (fastest || steadiest)
        {
            verdict.reason = Reason::TradeOff;
            verdict.faster = fastest ? performance.winner : -1;
            verdict.moreStable = steadiest ? stability.winner : -1;
            return verdict;
        }

        verdict.reason = Reason::MissingData;
        return verdict;
    }
};
