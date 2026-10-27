// Copyright (c) 2026 3zwr1 (AMDNR). Part of AMDNR (GPL-3.0; see Licenses/AMDNR_NOTICE.txt).
#pragma once
// Process names OptiScaler stays out of (FB-L9). One implementation of the rule, used by the passthrough check in
// dllmain.cpp and by AmdBridge's danielblnc installer check. Header-only and free of Windows so the unit tests
// (tests\034) build it without the DLL.
//
// Signatures fixed by W0-2 (a change is a gate request); bodies W1-F. The rule is narrow on purpose: a name part
// that no game exe carries (checked against re9, ForzaHorizon6, SHProto-Win64-Shipping, acs, eldenring,
// Cyberpunk2077), so a game is never passed through by accident.

#include <string_view>

namespace ProcessNameMatch
{
namespace detail
{
inline wchar_t Lower(wchar_t c) { return (c >= L'A' && c <= L'Z') ? static_cast<wchar_t>(c + (L'a' - L'A')) : c; }

// The file name part of `name` (after the last '\\' or '/'), so a caller that passes a path still gets the rule.
inline std::wstring_view FileName(std::wstring_view name)
{
    const size_t slash = name.find_last_of(L"\\/");
    return slash == std::wstring_view::npos ? name : name.substr(slash + 1);
}

inline bool EqualAt(std::wstring_view s, size_t at, std::wstring_view part) // `part` is lower case
{
    if (at > s.size() || s.size() - at < part.size())
        return false;
    for (size_t i = 0; i < part.size(); ++i)
        if (Lower(s[at + i]) != part[i])
            return false;
    return true;
}

inline bool Contains(std::wstring_view s, std::wstring_view part) // `part` is lower case
{
    if (part.size() > s.size())
        return false;
    for (size_t at = 0; at + part.size() <= s.size(); ++at)
        if (EqualAt(s, at, part))
            return true;
    return false;
}
} // namespace detail

// `exeName` is a file name without its folder (for example L"re9.exe"), any case.

// danielblnc's installer and its renamed copies: the name starts with "dlssnr_on_amd_setup" and ends in ".exe"
// (for example dlssnr_on_amd_setup_v0.3.1-re-engine-test.exe).
inline bool IsAmdSetupExe(std::wstring_view exeName)
{
    static constexpr std::wstring_view kPrefix = L"dlssnr_on_amd_setup";
    static constexpr std::wstring_view kSuffix = L".exe";
    const std::wstring_view name = detail::FileName(exeName);
    return name.size() >= kPrefix.size() + kSuffix.size() && detail::EqualAt(name, 0, kPrefix) &&
           detail::EqualAt(name, name.size() - kSuffix.size(), kSuffix);
}

// Crash reporters and CEF helper processes, by a narrow list of name parts (crashpad_handler, crashreport,
// crashhandler, cefsubprocess).
inline bool IsCrashReporterOrCef(std::wstring_view exeName)
{
    static constexpr std::wstring_view kParts[] = { L"crashpad_handler", L"crashreport", L"crashhandler",
                                                    L"cefsubprocess" };
    const std::wstring_view name = detail::FileName(exeName);
    for (std::wstring_view part : kParts)
        if (detail::Contains(name, part))
            return true;
    return false;
}

// Either of the two: the process is a helper OptiScaler passes through.
inline bool IsHelperProcess(std::wstring_view exeName)
{
    return IsAmdSetupExe(exeName) || IsCrashReporterOrCef(exeName);
}
} // namespace ProcessNameMatch
