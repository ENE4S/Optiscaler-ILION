#pragma once
#include <State.h>
#include <Config.h>
#include <atomic>
#include <Util.h>
#include <fstream>
#include <format>
#include <utility>
#include <map>
#include <mutex>

// Vulkan XeFG diagnostics and confirmed-presentation counter; no SDK arguments or configuration changes.
namespace VulkanXeFGDiagnostics
{
inline bool Enabled()
{
    const auto& state = State::Instance();
    return state.swapchainInteropApi == SwapchainInteropApi::VkwDx12 &&
           state.activeFgOutput == FGOutput::XeFG;
}

// The multiplier shown is the one XeFG's SDK confirms it presented (xefgSwapChainGetLastPresentStatus), for any XeFG
// through the Vulkan bridge and both of its inputs. ILION dev.53: it no longer depends on the name of the XeMFG folder
// ("dashdogy-v1.7.0") nor on the DLSSG input; a plain XeFG (no XeMFG) reports what it presents too
inline bool FixActive()
{
    const auto& state = State::Instance();
    return Enabled() && state.swapchainApi == Vulkan &&
           (state.activeFgInput == FGInput::DLSSG || state.activeFgInput == FGInput::Upscaler);
}

// The game's Streamline tags (Vulkan resources) feed the bridge only with the DLSSG input: with the Upscaler input
// the inputs come from the upscaler, and taking the tags too would feed it twice
inline bool StreamlineTagsActive() { return FixActive() && State::Instance().activeFgInput == FGInput::DLSSG; }

inline std::atomic<unsigned> reportedGeneratedFrames { 0 };
inline void ResetPresentation() { reportedGeneratedFrames.store(0, std::memory_order_relaxed); }
inline unsigned ReportedGeneratedFrames() { return reportedGeneratedFrames.load(std::memory_order_relaxed); }
inline void ObservePresentation(bool queryOk, unsigned frames, bool generationOk, bool enabled, unsigned capacity)
{
    const bool confirmed = queryOk && generationOk && enabled && frames >= 1 && frames <= capacity;
    reportedGeneratedFrames.store(confirmed ? frames - 1 : 0, std::memory_order_relaxed);
}

inline std::mutex logMutex;
inline std::map<std::string, std::pair<unsigned, ULONGLONG>> logLimits;

template <typename... Args>
void Record(const char* event, std::format_string<Args...> format, Args&&... args) noexcept
{
    if (!Enabled())
        return;
    const auto savedError = GetLastError();
    struct RestoreError { DWORD value; ~RestoreError() { SetLastError(value); } } restore { savedError };
    try
    {
        std::scoped_lock lock(logMutex);
        auto& limit = logLimits[event];
        const auto now = GetTickCount64();
        const bool critical = std::string_view(event) == "apply" || std::string_view(event) == "capacity" ||
                              std::string_view(event) == "init" || std::string_view(event) == "reduction";
        if (!critical && limit.first >= 8 && now - limit.second < 5000)
            return;
        ++limit.first;
        limit.second = now;
        SYSTEMTIME utc {};
        GetSystemTime(&utc);
        const auto line = std::format(
            "{:04}-{:02}-{:02}T{:02}:{:02}:{:02}.{:03}Z pid={} Vulkan+XeFG event={} {}",
            utc.wYear, utc.wMonth, utc.wDay, utc.wHour, utc.wMinute, utc.wSecond,
            utc.wMilliseconds, GetCurrentProcessId(), event, std::format(format, std::forward<Args>(args)...));
        spdlog::info("{}", line);
        OutputDebugStringA((line + "\n").c_str());
        // Independent of the user's general logging setting. Append, never truncate.
        std::ofstream output(Util::DllPath().parent_path() / L"ILION-XeMFG-Vulkan-diagnostic.log",
                             std::ios::app);
        output << line << '\n';
    }
    catch (...) {} // A diagnostic failure must not affect rendering.
}
}
