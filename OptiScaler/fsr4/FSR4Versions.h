#pragma once

#include "SysUtils.h"
#include "Util.h"
#include "Config.h"
#include "Logger.h"

#include <bcrypt.h>
#include <algorithm>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#pragma comment(lib, "bcrypt.lib")

// Finds the FSR upscaler dlls the user can switch between.
//
// Every subfolder of "<game folder>/ILION/FSR_Versions" that has its own amd_fidelityfx_upscaler_dx12.dll is a
// version, the folder name is its name. "<OptiDllPath>/FSR4" is still read for installs made before this folder
// existed. Dlls are checked from the file on disk, nothing gets loaded or executed here. Loading and switching
// stays in FfxApiProxy.
class FSR4Versions
{
  public:
    struct Entry
    {
        std::wstring name;
        std::wstring dllPath;

        // File version of the dll, empty when it has none
        std::string version;

        // 64 bit dll with the ffx api exports
        bool valid = false;
        std::string problem;

        // Found in the old "<OptiDllPath>/FSR4" folder
        bool legacyFolder = false;

        // Path, size and write time, identifies the file the hash belongs to
        std::wstring hashKey;
    };

    static constexpr const wchar_t* DllName = L"amd_fidelityfx_upscaler_dx12.dll";

    static std::filesystem::path Root() { return Util::ExePath().parent_path() / L"ILION" / L"FSR_Versions"; }

    static std::filesystem::path LegacyRoot()
    {
        return std::filesystem::path(Config::Instance()->MainDllPath.value_or(L"")) / L"FSR4";
    }

    // Reads the folders again, safe to call while a dll is in use
    static std::vector<Entry> Scan()
    {
        std::vector<Entry> result;
        std::error_code ec;

        const auto root = Root();
        std::filesystem::create_directories(root, ec);

        AddFrom(root, false, result);
        AddFrom(LegacyRoot(), true, result);

        std::sort(result.begin(), result.end(), [](const Entry& a, const Entry& b) { return a.name < b.name; });

        for (const auto& entry : result)
        {
            LOG_INFO("[FSR-VERSIONS] {} | {} | version: {} | {}", wstring_to_string(entry.name),
                     wstring_to_string(entry.dllPath), entry.version.empty() ? "unknown" : entry.version,
                     entry.valid ? "compatible" : entry.problem);
        }

        StartHashing(result);

        return result;
    }

    // Path of the dll of a version, empty when there's no such version
    static std::wstring DllPathFor(const std::wstring& name)
    {
        if (name.empty())
            return L"";

        std::error_code ec;
        const auto folder = std::filesystem::path(name).filename();

        for (const auto& root : { Root(), LegacyRoot() })
        {
            auto dllPath = root / folder / DllName;

            if (std::filesystem::exists(dllPath, ec))
                return dllPath.wstring();
        }

        return L"";
    }

    // Name of the version a dll path belongs to, empty when the dll isn't inside a versions folder
    static std::wstring NameFromPath(const std::wstring& dllPath)
    {
        if (dllPath.empty())
            return L"";

        const auto folder = std::filesystem::path(dllPath).parent_path();
        const auto parent = folder.parent_path();

        if (IsSamePath(parent, Root()) || IsSamePath(parent, LegacyRoot()))
            return folder.filename().wstring();

        return L"";
    }

    // Checks the file on disk: 64 bit dll that exports the five ffx api functions
    static bool Validate(const std::wstring& dllPath, std::string* problem = nullptr)
    {
        const auto fail = [&](const char* text)
        {
            if (problem != nullptr)
                *problem = text;

            return false;
        };

        MappedFile file;

        if (!file.Open(dllPath))
            return fail("file can't be opened");

        if (file.size < sizeof(IMAGE_DOS_HEADER))
            return fail("not a dll");

        const auto dos = (const IMAGE_DOS_HEADER*) file.data;

        if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 ||
            (size_t) dos->e_lfanew + sizeof(IMAGE_NT_HEADERS64) > file.size)
        {
            return fail("not a dll");
        }

        const auto nt = (const IMAGE_NT_HEADERS64*) (file.data + dos->e_lfanew);

        if (nt->Signature != IMAGE_NT_SIGNATURE)
            return fail("not a dll");

        if (nt->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
            nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC)
        {
            return fail("not a 64 bit dll");
        }

        if ((nt->FileHeader.Characteristics & IMAGE_FILE_DLL) == 0)
            return fail("not a dll");

        const auto sections = (const BYTE*) &nt->OptionalHeader + nt->FileHeader.SizeOfOptionalHeader;
        const size_t sectionCount = nt->FileHeader.NumberOfSections;

        if (sections + sectionCount * sizeof(IMAGE_SECTION_HEADER) > file.data + file.size)
            return fail("damaged dll");

        // File position of an address inside the loaded image, with how many bytes can be read from it
        const auto fromRva = [&](DWORD rva, size_t* available) -> const BYTE*
        {
            for (size_t i = 0; i < sectionCount; i++)
            {
                const auto section = (const IMAGE_SECTION_HEADER*) (sections + i * sizeof(IMAGE_SECTION_HEADER));

                if (rva < section->VirtualAddress || rva - section->VirtualAddress >= section->SizeOfRawData)
                    continue;

                const size_t offset = (size_t) section->PointerToRawData + (rva - section->VirtualAddress);
                size_t left = section->SizeOfRawData - (rva - section->VirtualAddress);

                if (offset >= file.size)
                    return nullptr;

                if (left > file.size - offset)
                    left = file.size - offset;

                *available = left;
                return file.data + offset;
            }

            return nullptr;
        };

        if (nt->OptionalHeader.NumberOfRvaAndSizes <= IMAGE_DIRECTORY_ENTRY_EXPORT)
            return fail("dll has no exports");

        const auto& exportEntry = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];

        if (exportEntry.VirtualAddress == 0)
            return fail("dll has no exports");

        size_t available = 0;
        const auto exports = (const IMAGE_EXPORT_DIRECTORY*) fromRva(exportEntry.VirtualAddress, &available);

        if (exports == nullptr || available < sizeof(IMAGE_EXPORT_DIRECTORY))
            return fail("damaged dll");

        const size_t nameCount = exports->NumberOfNames;
        const auto names = (const DWORD*) fromRva(exports->AddressOfNames, &available);

        if (names == nullptr || nameCount > 65536 || available < nameCount * sizeof(DWORD))
            return fail("damaged dll");

        static constexpr const char* required[] = { "ffxCreateContext", "ffxDestroyContext", "ffxConfigure",
                                                    "ffxQuery", "ffxDispatch" };
        bool found[std::size(required)] = {};

        for (size_t i = 0; i < nameCount; i++)
        {
            const auto exportName = (const char*) fromRva(names[i], &available);

            if (exportName == nullptr)
                continue;

            const size_t length = strnlen(exportName, available);

            for (size_t r = 0; r < std::size(required); r++)
            {
                if (length == strlen(required[r]) && memcmp(exportName, required[r], length) == 0)
                    found[r] = true;
            }
        }

        for (const bool exportFound : found)
        {
            if (!exportFound)
                return fail("not an FSR upscaler dll (ffx exports missing)");
        }

        return true;
    }

    // SHA-256 of the dll as hex, empty while it's still being calculated
    static std::string HashFor(const Entry& entry)
    {
        std::scoped_lock lock(_hashMutex);

        const auto it = _hashes.find(entry.hashKey);
        return it != _hashes.end() ? it->second : std::string();
    }

    // Starts hashing a dll that isn't part of the scanned folders, e.g. the default one
    static void RequestHash(const std::wstring& dllPath)
    {
        std::error_code ec;

        if (dllPath.empty() || !std::filesystem::exists(dllPath, ec))
            return;

        Entry entry;
        entry.dllPath = dllPath;
        entry.hashKey = HashKey(dllPath);

        StartHashing({ entry });
    }

    // Same for a dll that's known only by its path, e.g. the one that's in use
    static std::string HashForPath(const std::wstring& dllPath)
    {
        const auto key = HashKey(dllPath);

        std::scoped_lock lock(_hashMutex);

        const auto it = _hashes.find(key);
        return it != _hashes.end() ? it->second : std::string();
    }

  private:
    struct MappedFile
    {
        HANDLE file = INVALID_HANDLE_VALUE;
        HANDLE mapping = nullptr;
        const BYTE* data = nullptr;
        size_t size = 0;

        bool Open(const std::wstring& path)
        {
            file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                               FILE_ATTRIBUTE_NORMAL, nullptr);

            if (file == INVALID_HANDLE_VALUE)
                return false;

            LARGE_INTEGER fileSize {};

            if (!GetFileSizeEx(file, &fileSize) || fileSize.QuadPart <= 0)
                return false;

            mapping = CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0, nullptr);

            if (mapping == nullptr)
                return false;

            data = (const BYTE*) MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
            size = (size_t) fileSize.QuadPart;

            return data != nullptr;
        }

        ~MappedFile()
        {
            if (data != nullptr)
                UnmapViewOfFile(data);

            if (mapping != nullptr)
                CloseHandle(mapping);

            if (file != INVALID_HANDLE_VALUE)
                CloseHandle(file);
        }
    };

    inline static std::mutex _hashMutex;
    inline static std::unordered_map<std::wstring, std::string> _hashes;

    static bool IsSamePath(const std::filesystem::path& a, const std::filesystem::path& b)
    {
        const auto pathA = a.lexically_normal().wstring();
        const auto pathB = b.lexically_normal().wstring();

        return !pathA.empty() && _wcsicmp(pathA.c_str(), pathB.c_str()) == 0;
    }

    static std::wstring HashKey(const std::wstring& dllPath)
    {
        std::error_code ec;
        const auto size = std::filesystem::file_size(dllPath, ec);
        const auto time = std::filesystem::last_write_time(dllPath, ec).time_since_epoch().count();

        auto key = std::filesystem::path(dllPath).lexically_normal().wstring();
        std::transform(key.begin(), key.end(), key.begin(), towlower);

        return key + L"|" + std::to_wstring(size) + L"|" + std::to_wstring(time);
    }

    static void AddFrom(const std::filesystem::path& root, bool legacy, std::vector<Entry>& result)
    {
        std::error_code ec;

        if (!std::filesystem::is_directory(root, ec))
            return;

        for (const auto& folder : std::filesystem::directory_iterator(root, ec))
        {
            if (!folder.is_directory(ec))
                continue;

            Entry entry;
            entry.name = folder.path().filename().wstring();
            entry.legacyFolder = legacy;

            // The new folder wins when both have a version with the same name
            const bool known = std::any_of(result.begin(), result.end(), [&](const Entry& other)
                                           { return _wcsicmp(other.name.c_str(), entry.name.c_str()) == 0; });

            if (known)
                continue;

            const auto dllPath = folder.path() / DllName;

            if (std::filesystem::exists(dllPath, ec))
            {
                entry.dllPath = dllPath.wstring();
                entry.valid = Validate(entry.dllPath, &entry.problem);
                entry.hashKey = HashKey(entry.dllPath);

                version_t version;

                if (Util::GetFileVersion(entry.dllPath, &version))
                {
                    entry.version =
                        std::format("{}.{}.{}.{}", version.major, version.minor, version.patch, version.reserved);
                }
            }
            else
            {
                // Only listed when the folder has some other dll, so the user sees why it can't be used
                bool hasOtherDll = false;

                for (const auto& file : std::filesystem::directory_iterator(folder.path(), ec))
                {
                    if (_wcsicmp(file.path().extension().c_str(), L".dll") == 0)
                        hasOtherDll = true;
                }

                if (!hasOtherDll)
                    continue;

                entry.problem = "the dll has to be named amd_fidelityfx_upscaler_dx12.dll";
            }

            result.push_back(std::move(entry));
        }
    }

    static std::string Sha256(const std::wstring& dllPath)
    {
        MappedFile file;

        if (!file.Open(dllPath) || file.size > MAXDWORD)
            return "";

        BCRYPT_ALG_HANDLE algorithm = nullptr;

        if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
            return "";

        BYTE digest[32] = {};
        const auto status =
            BCryptHash(algorithm, nullptr, 0, (PUCHAR) file.data, (ULONG) file.size, digest, sizeof(digest));

        BCryptCloseAlgorithmProvider(algorithm, 0);

        if (status < 0)
            return "";

        std::string hex;
        hex.reserve(sizeof(digest) * 2);

        for (const BYTE value : digest)
            hex += std::format("{:02X}", value);

        return hex;
    }

    // Hashing tens of megabytes per dll, kept away from the thread that draws the menu
    static void StartHashing(const std::vector<Entry>& entries)
    {
        std::vector<Entry> pending;

        {
            std::scoped_lock lock(_hashMutex);

            for (const auto& entry : entries)
            {
                if (!entry.dllPath.empty() && !_hashes.contains(entry.hashKey))
                    pending.push_back(entry);
            }
        }

        if (pending.empty())
            return;

        std::thread(
            [pending = std::move(pending)]()
            {
                for (const auto& entry : pending)
                {
                    auto hash = Sha256(entry.dllPath);

                    if (hash.empty())
                        hash = "unavailable";

                    LOG_INFO("[FSR-VERSIONS] {} | SHA-256: {}", wstring_to_string(entry.dllPath), hash);

                    std::scoped_lock lock(_hashMutex);
                    _hashes[entry.hashKey] = hash;
                }
            })
            .detach();
    }
};
