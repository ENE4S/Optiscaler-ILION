#pragma once

#include "Config.h"
#include "Util.h"
#include "Logger.h"
#include <fsr4/FSR4Versions.h>

#include <algorithm>
#include <cstring>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <format>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

// The upscalers ILION can switch between, by family, found from files only: nothing gets loaded or executed here.
//
//   <game folder>\ILION\Escaladores\FSR4\<name>\amd_fidelityfx_upscaler_dx12.dll      FSR 4 builds (FFX API)
//   <game folder>\ILION\Escaladores\HelixSR\helixsr_upscaler.dll                       HelixSR (FFX API, FSR 3.1 drop-in)
//   <game folder>\ILION\Escaladores\DLSS4\<name>\_nvngx.dll + d4r.ini ...              DLSS 4 through d4r (NGX)
//   <game folder>\ILION\Escaladores\<other>\<name>\amd_fidelityfx_upscaler_dx12.dll   any other FFX upscaler
//
// Older installs keep working without moving anything: ILION\FSR_Versions\<name> and <OptiDllPath>\FSR4\<name> are
// still read (their saved names stay valid), and a "d4r" folder next to OptiScaler is a DLSS 4 backend.
//
// The family of an FFX dll is decided from the file, never from the name of its folder: HelixSR's setup appends its
// network to the dll it builds, ending the file with the marker "HXSRNET1" (helixsr_install.py, has_embedded_network).
// A dll without it is not HelixSR, whatever folder it is in.
namespace IlionUpscalers
{
enum class Family
{
    Fsr4,     // AMD's FSR 4 dll and builds of it
    HelixSR,  // HelixSR (lonewolf0622), FSR 3.1 FFX drop-in running DLSS's model E network
    OtherFfx, // any other dll with the FFX upscaler exports
    Dlss4,    // DLSS 4 through the external d4r backend (countervolts)
};

// How the upscaler is loaded: FFX dlls are switched while the game runs, d4r is loaded by OptiScaler at start
enum class Kind
{
    Ffx,
    D4r,
};

struct Entry
{
    // Saved in the config: "FSR4/Oficial", "HelixSR", "DLSS4/Modelo_K", or the folder name of an older install
    std::wstring key;
    // Name shown, an English text that goes through the menu's translation
    std::string display;
    // Shown after the name when it isn't part of it (the folder name of a build nobody named)
    std::string detail;
    Family family = Family::Fsr4;
    Kind kind = Kind::Ffx;
    // FFX: the upscaler dll. d4r: the backend folder.
    std::wstring path;
    // File version of an FFX dll, for the tooltip only: builds of FSR 4 all share AMD's 4.1.1.2740
    std::string fileVersion;
    bool valid = false;
    std::string problem;
    // Next to a HelixSR dll: AMD's FSR that HelixSR lists after itself (its "forwarding")
    bool forwardsToAmd = false;
    // Found in a folder of an older install
    bool legacy = false;
};

// Last scan (see Entries), guarded by _mutex
inline std::mutex _mutex;
inline std::vector<Entry> _cache;
inline bool _scanned = false;

inline std::filesystem::path Root() { return Util::ExePath().parent_path() / L"ILION" / L"Escaladores"; }
inline std::filesystem::path LegacyRoot() { return FSR4Versions::Root(); }
inline std::filesystem::path LegacyOptiRoot() { return FSR4Versions::LegacyRoot(); }
inline std::filesystem::path LegacyD4r() { return Util::DllPath().parent_path() / L"d4r"; }

inline constexpr const wchar_t* FfxDllName = L"amd_fidelityfx_upscaler_dx12.dll";

inline std::wstring Lower(std::wstring text)
{
    std::transform(text.begin(), text.end(), text.begin(), [](wchar_t c) { return (wchar_t) std::towlower(c); });
    return text;
}

inline bool Contains(const std::wstring& text, const wchar_t* lowerNeedle)
{
    return Lower(text).find(lowerNeedle) != std::wstring::npos;
}

// HelixSR's built dll: its setup appends the network and ends the file with a 32 byte trailer starting "HXSRNET1"
inline bool HasHelixNetwork(const std::wstring& path)
{
    std::ifstream file(std::filesystem::path(path), std::ios::binary);
    if (!file)
        return false;

    file.seekg(-32, std::ios::end);
    char marker[8] = {};

    return file.read(marker, sizeof marker) && memcmp(marker, "HXSRNET1", 8) == 0;
}

// HelixSR's code, with or without its network: its dll carries its own settings names (HELIXSR_PROFILE, ...)
inline bool LooksLikeHelix(const std::wstring& path)
{
    std::ifstream file(std::filesystem::path(path), std::ios::binary);
    if (!file)
        return false;

    static constexpr char needle[] = "HELIXSR_";
    std::string carry;
    char buffer[1 << 16];

    while (file.read(buffer, sizeof buffer) || file.gcount() > 0)
    {
        std::string chunk = carry + std::string(buffer, (size_t) file.gcount());

        if (chunk.find(needle) != std::string::npos)
            return true;

        carry = chunk.substr(chunk.size() > 8 ? chunk.size() - 8 : 0);
    }

    return false;
}

inline std::string FileVersionOf(const std::wstring& path)
{
    version_t version;

    if (!Util::GetFileVersion(path, &version))
        return "";

    return std::format("{}.{}.{}.{}", version.major, version.minor, version.patch, version.reserved);
}

// Name of a build of FSR 4 from the folder the user put it in. Only the name: what it is comes from the file.
inline void NameFsr4(Entry& entry, const std::wstring& folder)
{
    if (Contains(folder, L"lite"))
        entry.display = "FSR 4 Lite";
    else if (Contains(folder, L"4.1.1b") || Contains(folder, L"411b") || Contains(folder, L"community") ||
             Contains(folder, L"comunidad"))
        entry.display = "FSR 4.1.1B";
    else if (Contains(folder, L"oficial") || Contains(folder, L"official"))
        entry.display = "FSR 4 official (AMD)";
    else
    {
        entry.display = "FSR 4";
        entry.detail = wstring_to_string(folder);
    }
}

// An FFX dll in a folder: its family from the file, valid when it's a 64 bit dll with the FFX upscaler exports
inline Entry FfxEntry(const std::wstring& key, const std::filesystem::path& dll, const std::wstring& folderName, bool legacy,
                      Family expected)
{
    Entry entry;
    entry.key = key;
    entry.kind = Kind::Ffx;
    entry.path = dll.wstring();
    entry.legacy = legacy;
    entry.fileVersion = FileVersionOf(entry.path);
    entry.valid = FSR4Versions::Validate(entry.path, &entry.problem);

    if (HasHelixNetwork(entry.path))
    {
        entry.family = Family::HelixSR;
        entry.display = "HelixSR";
    }
    else if (expected == Family::OtherFfx)
    {
        entry.family = Family::OtherFfx;
        entry.display = wstring_to_string(folderName);
    }
    else
    {
        entry.family = Family::Fsr4;
        NameFsr4(entry, folderName);
    }

    return entry;
}

// The HelixSR folder: HelixSR's own file names first. A file that isn't HelixSR is reported, not used.
inline void AddHelix(const std::filesystem::path& folder, const std::wstring& key, std::vector<Entry>& out)
{
    std::error_code ec;

    if (!std::filesystem::is_directory(folder, ec))
        return;

    Entry entry;
    entry.key = key;
    entry.family = Family::HelixSR;
    entry.kind = Kind::Ffx;
    entry.display = "HelixSR";
    entry.forwardsToAmd = std::filesystem::exists(folder / L"amd_fidelityfx_upscaler_dx12.amd.dll", ec);

    for (const wchar_t* name : { L"helixsr_upscaler.dll", L"helixsr.dll", L"amd_fidelityfx_dx12.dll", FfxDllName })
    {
        const auto path = folder / name;

        if (!std::filesystem::exists(path, ec))
            continue;

        if (!HasHelixNetwork(path.wstring()))
        {
            // The mistakes this folder invites, said plainly instead of loading something else
            if (entry.problem.empty())
            {
                entry.path = path.wstring();

                if (LooksLikeHelix(entry.path))
                    entry.problem = "HelixSR without its network: run HelixSR's setup and copy the dll it builds";
                else if (_wcsicmp(name, FfxDllName) == 0)
                    entry.problem = "this is AMD's FSR, not HelixSR (HelixSR's own dll is amd_fidelityfx_dx12.dll, "
                                    "built by its setup)";
                else
                    entry.problem = "this file is not HelixSR";
            }

            continue;
        }

        entry.path = path.wstring();
        entry.fileVersion = FileVersionOf(entry.path);
        entry.problem.clear();

        // A file named like the game's FSR dll can be mixed up with it once loaded; HelixSR's setup uses its own
        // name for OptiScaler for that reason (helixsr_upscaler.dll)
        if (_wcsicmp(name, L"amd_fidelityfx_dx12.dll") == 0 || _wcsicmp(name, FfxDllName) == 0)
            entry.problem = "rename it to helixsr_upscaler.dll";
        else
            entry.valid = FSR4Versions::Validate(entry.path, &entry.problem);

        break;
    }

    if (!entry.path.empty())
        out.push_back(std::move(entry));
}

// A d4r backend folder: the loader, its settings and the NVIDIA files the user has to supply
inline void AddD4r(const std::filesystem::path& folder, const std::wstring& key, bool legacy, std::vector<Entry>& out)
{
    std::error_code ec;

    if (!std::filesystem::exists(folder / L"_nvngx.dll", ec) || !std::filesystem::exists(folder / L"d4r.ini", ec))
        return;

    Entry entry;
    entry.key = key;
    entry.family = Family::Dlss4;
    entry.kind = Kind::D4r;
    entry.path = folder.lexically_normal().wstring();
    entry.legacy = legacy;
    entry.display = "DLSS 4 - model K";
    entry.valid = true;

    // Model asked for in d4r.ini; only K belongs to this version
    std::ifstream ini(folder / L"d4r.ini");
    std::string line;

    while (std::getline(ini, line))
    {
        const auto at = line.find("Model");
        if (at == std::string::npos || line.find_first_not_of(" \t") != at)
            continue;

        const auto value = line.find_first_not_of(" \t=", at + 5);
        if (value != std::string::npos && line[value] != 'K' && line[value] != 'k')
        {
            entry.valid = false;
            entry.display = "DLSS (d4r)";
            entry.problem = "d4r.ini asks for another model; this version of ILION only uses model K";
        }

        break;
    }

    if (entry.valid && (!std::filesystem::exists(folder / L"nvngx_dlss.dll", ec) ||
                        !std::filesystem::exists(folder / L"ngx" / L"_nvngx.dll", ec)))
    {
        entry.valid = false;
        entry.problem = "NVIDIA's nvngx_dlss.dll and ngx\\_nvngx.dll are missing in the d4r folder";
    }

    out.push_back(std::move(entry));
}

// Every upscaler found, sorted by family and name. Reads the folders again; called with _mutex held.
inline std::vector<Entry> ScanLocked()
{
    std::vector<Entry> result;
    std::error_code ec;

    const auto root = Root();

    const auto known = [&](const std::wstring& key)
    {
        return std::any_of(result.begin(), result.end(),
                           [&](const Entry& other) { return _wcsicmp(other.key.c_str(), key.c_str()) == 0; });
    };

    for (const auto& family : std::filesystem::directory_iterator(root, ec))
    {
        if (!family.is_directory(ec))
            continue;

        const auto familyName = family.path().filename().wstring();

        if (_wcsicmp(familyName.c_str(), L"HelixSR") == 0)
        {
            AddHelix(family.path(), L"HelixSR", result);

            for (const auto& sub : std::filesystem::directory_iterator(family.path(), ec))
            {
                if (sub.is_directory(ec))
                    AddHelix(sub.path(), L"HelixSR/" + sub.path().filename().wstring(), result);
            }

            continue;
        }

        for (const auto& sub : std::filesystem::directory_iterator(family.path(), ec))
        {
            if (!sub.is_directory(ec))
                continue;

            const auto key = familyName + L"/" + sub.path().filename().wstring();

            if (_wcsicmp(familyName.c_str(), L"DLSS4") == 0)
            {
                AddD4r(sub.path(), key, false, result);
                continue;
            }

            const auto dll = sub.path() / FfxDllName;

            if (std::filesystem::exists(dll, ec))
            {
                const auto expected = _wcsicmp(familyName.c_str(), L"FSR4") == 0 ? Family::Fsr4 : Family::OtherFfx;
                result.push_back(FfxEntry(key, dll, sub.path().filename().wstring(), false, expected));
            }
        }
    }

    // Older installs, under their folder names as before
    for (const auto& legacyRoot : { LegacyRoot(), LegacyOptiRoot() })
    {
        if (!std::filesystem::is_directory(legacyRoot, ec))
            continue;

        for (const auto& sub : std::filesystem::directory_iterator(legacyRoot, ec))
        {
            const auto name = sub.path().filename().wstring();
            const auto dll = sub.path() / FfxDllName;

            if (sub.is_directory(ec) && !known(name) && std::filesystem::exists(dll, ec))
                result.push_back(FfxEntry(name, dll, name, true, Family::Fsr4));
        }
    }

    if (!known(L"d4r"))
        AddD4r(LegacyD4r(), L"d4r", true, result);

    std::stable_sort(result.begin(), result.end(),
                     [](const Entry& a, const Entry& b)
                     {
                         if (a.family != b.family)
                             return (int) a.family < (int) b.family;
                         return a.display + a.detail < b.display + b.detail;
                     });

    for (const auto& entry : result)
    {
        LOG_INFO("[UPSCALERS] {} | {} | {} | {}", wstring_to_string(entry.key), entry.display, wstring_to_string(entry.path),
                 entry.valid ? std::string("usable") : entry.problem);
    }

    _cache = result;
    return result;
}

// Last scan, scanned once if there was none. Copies, safe from any thread.
inline std::vector<Entry> Entries()
{
    std::scoped_lock lock(_mutex);

    if (!_scanned)
    {
        _scanned = true;
        ScanLocked();
    }

    return _cache;
}

inline std::vector<Entry> Rescan()
{
    // The folder is made the first time the user looks for upscalers, so it's there to put them in
    std::error_code ec;
    std::filesystem::create_directories(Root(), ec);

    std::scoped_lock lock(_mutex);
    _scanned = true;
    return ScanLocked();
}

inline bool SamePath(const std::wstring& a, const std::wstring& b)
{
    if (a.empty() || b.empty())
        return false;

    std::error_code ec;
    return std::filesystem::equivalent(a, b, ec) || _wcsicmp(a.c_str(), b.c_str()) == 0;
}

inline std::optional<Entry> FindByKey(const std::wstring& key)
{
    if (key.empty())
        return std::nullopt;

    for (const auto& entry : Entries())
    {
        if (_wcsicmp(entry.key.c_str(), key.c_str()) == 0)
            return entry;
    }

    return std::nullopt;
}

inline std::optional<Entry> FindByPath(const std::wstring& path)
{
    for (const auto& entry : Entries())
    {
        if (SamePath(entry.path, path))
            return entry;
    }

    return std::nullopt;
}

// Path of the FFX dll saved under a key, empty when there's no usable FFX upscaler with it
inline std::wstring FfxDllPathFor(const std::wstring& key)
{
    const auto entry = FindByKey(key);
    return entry.has_value() && entry->kind == Kind::Ffx && entry->valid ? entry->path : std::wstring();
}

// Name to show for a loaded dll, the family and build as found on disk; empty when it isn't one of them
inline std::string DisplayForPath(const std::wstring& path)
{
    const auto entry = FindByPath(path);
    if (!entry.has_value())
        return "";

    return entry->detail.empty() ? entry->display : entry->display + " " + entry->detail;
}

// Short name of the technology an FFX upscaler dll belongs to, for the feature OptiScaler creates with it
inline std::string TechnologyForFfxPath(const std::wstring& path)
{
    const auto entry = FindByPath(path);

    if (!entry.has_value())
        return "FSR"; // the default dll or the game's own, both AMD's

    switch (entry->family)
    {
    case Family::HelixSR:
        return "HelixSR";
    case Family::OtherFfx:
        return "FFX";
    default:
        return "FSR";
    }
}

// Where DLSS 4 through d4r is installed: the first usable backend of the DLSS4 family, else the older d4r folder
inline std::wstring DefaultD4rFolder()
{
    for (const auto& entry : Entries())
    {
        if (entry.kind == Kind::D4r && entry.valid)
            return entry.path;
    }

    return L"";
}

} // namespace IlionUpscalers
