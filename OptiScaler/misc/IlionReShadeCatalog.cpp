#include "pch.h"
#include "IlionReShadeCatalog.h"

#include <Util.h>
#include <json.hpp>

#include <winhttp.h>

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <regex>
#include <set>
#include <sstream>
#include <thread>

namespace IlionReShadeCatalog
{
namespace
{
constexpr const char* kIndexUrl = "https://raw.githubusercontent.com/crosire/reshade-shaders/list/EffectPackages.ini";
constexpr const char* kManifestName = "ILION-ReShade-efectos.txt";

struct PackageInfo
{
    Package pub;
    std::string installPath = ".\\reshade-shaders\\Shaders";
    std::string textureInstallPath = ".\\reshade-shaders\\Textures";
    std::string owner, repo, branch; // GitHub repository of the package, empty when it isn't one
    std::vector<std::string> tree;   // its files, read when an effect of it is installed first
};

// Lives until the process ends (never destroyed): a download may still be running when the game exits
struct Catalog
{
    std::mutex lock;
    Snapshot::Index index = Snapshot::NotLoaded;
    std::string indexError;
    std::vector<PackageInfo> packages;
    std::map<std::string, EffectState> states; // by lower-case effect file
    std::vector<std::string> finished;
    std::atomic<bool> busy { false };
    std::string busyWith;
};

Catalog& C()
{
    static Catalog* catalog = new Catalog();
    return *catalog;
}

std::string Lower(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return (char) std::tolower(c); });
    return text;
}

std::filesystem::path GameDir() { return Util::ExePath().parent_path(); }

// A GET over HTTPS; the body, or false with the reason
bool HttpGet(const std::string& url, std::string& body, std::string& error)
{
    const std::wstring wurl(url.begin(), url.end());
    URL_COMPONENTS parts {};
    parts.dwStructSize = sizeof(parts);
    wchar_t host[256] {}, path[2048] {};
    parts.lpszHostName = host;
    parts.dwHostNameLength = (DWORD) std::size(host);
    parts.lpszUrlPath = path;
    parts.dwUrlPathLength = (DWORD) std::size(path);

    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &parts))
    {
        error = "invalid address";
        return false;
    }

    HINTERNET session = WinHttpOpen(L"OptiScaler-ILION/1.2", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                                    WINHTTP_NO_PROXY_BYPASS, 0);
    HINTERNET connection = session != nullptr ? WinHttpConnect(session, host, parts.nPort, 0) : nullptr;
    HINTERNET request = connection != nullptr ? WinHttpOpenRequest(connection, L"GET", path, nullptr, WINHTTP_NO_REFERER,
                                                                   WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE)
                                              : nullptr;
    bool ok = false;

    if (request != nullptr)
    {
        WinHttpSetTimeouts(session, 8000, 8000, 15000, 15000);

        if (WinHttpSendRequest(request, L"User-Agent: OptiScaler-ILION\r\n", (DWORD) -1, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
            WinHttpReceiveResponse(request, nullptr))
        {
            DWORD status = 0, size = sizeof(status);
            WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                                &status, &size, WINHTTP_NO_HEADER_INDEX);

            body.clear();
            DWORD available = 0;
            while (WinHttpQueryDataAvailable(request, &available) && available > 0)
            {
                std::string chunk(available, '\0');
                DWORD read = 0;
                if (!WinHttpReadData(request, chunk.data(), available, &read))
                    break;
                body.append(chunk.data(), read);
            }

            ok = status == 200;
            if (!ok)
                error = "HTTP " + std::to_string(status);
        }
        else
        {
            error = "no connection";
        }
    }
    else
    {
        error = "no connection";
    }

    if (request != nullptr)
        WinHttpCloseHandle(request);
    if (connection != nullptr)
        WinHttpCloseHandle(connection);
    if (session != nullptr)
        WinHttpCloseHandle(session);

    return ok;
}

std::vector<std::string> SplitList(const std::string& text)
{
    std::vector<std::string> list;
    std::stringstream stream(text);
    std::string item;
    while (std::getline(stream, item, ','))
    {
        item.erase(0, item.find_first_not_of(" \t"));
        item.erase(item.find_last_not_of(" \t\r") + 1);
        if (!item.empty())
            list.push_back(item);
    }
    return list;
}

std::vector<PackageInfo> ParseIndex(const std::string& ini)
{
    std::vector<PackageInfo> packages;
    std::map<std::string, std::string> keys;

    const auto flush = [&]()
    {
        if (keys.empty() || keys["PackageName"].empty())
        {
            keys.clear();
            return;
        }

        PackageInfo p;
        p.pub.name = keys["PackageName"];
        p.pub.description = keys["PackageDescription"];
        p.pub.repositoryUrl = keys["RepositoryUrl"];
        if (!keys["InstallPath"].empty())
            p.installPath = keys["InstallPath"];
        if (!keys["TextureInstallPath"].empty())
            p.textureInstallPath = keys["TextureInstallPath"];

        const auto deny = SplitList(keys["DenyEffectFiles"]);
        for (const auto& effect : SplitList(keys["EffectFiles"]))
        {
            if (std::find(deny.begin(), deny.end(), effect) == deny.end())
                p.pub.effects.push_back(effect);
        }

        // GitHub archive address: https://github.com/<owner>/<repo>/archive/[refs/heads/]<branch>.zip
        static const std::regex github(R"(https://github\.com/([^/]+)/([^/]+)/archive/(?:refs/heads/)?(.+)\.zip)");
        std::smatch m;
        if (std::regex_match(keys["DownloadUrl"], m, github))
        {
            p.owner = m[1];
            p.repo = m[2];
            p.branch = m[3];
        }

        if (!p.pub.effects.empty())
            packages.push_back(std::move(p));

        keys.clear();
    };

    std::stringstream stream(ini);
    std::string line;
    while (std::getline(stream, line))
    {
        line.erase(line.find_last_not_of("\r \t") + 1);
        if (line.empty())
            continue;
        if (line.front() == '[')
        {
            flush();
            continue;
        }
        const auto eq = line.find('=');
        if (eq != std::string::npos)
            keys[line.substr(0, eq)] = line.substr(eq + 1);
    }
    flush();

    return packages;
}

// The manifest: "effect.fx|relative\path" for every file ILION wrote
std::vector<std::pair<std::string, std::string>> ReadManifest()
{
    std::vector<std::pair<std::string, std::string>> lines;
    std::ifstream file(GameDir() / kManifestName);
    std::string line;
    while (std::getline(file, line))
    {
        const auto bar = line.find('|');
        if (bar != std::string::npos)
            lines.push_back({ line.substr(0, bar), line.substr(bar + 1) });
    }
    return lines;
}

void WriteManifest(const std::vector<std::pair<std::string, std::string>>& lines)
{
    std::ofstream file(GameDir() / kManifestName, std::ios::trunc);
    for (const auto& [effect, path] : lines)
        file << effect << '|' << path << '\n';
}

bool NeedsDepth(const std::string& text)
{
    return text.find("GetLinearizedDepth") != std::string::npos || text.find("DepthBuffer") != std::string::npos ||
           text.find(": DEPTH") != std::string::npos;
}

// Which effect files are in the shaders folder, which of them ILION installed, which read the depth buffer
void ScanDisk()
{
    std::map<std::string, EffectState> found;
    const auto manifest = ReadManifest();
    std::set<std::string> byIlion;
    for (const auto& [effect, path] : manifest)
        byIlion.insert(Lower(effect));

    std::error_code ec;
    const auto root = GameDir() / L"reshade-shaders" / L"Shaders";

    for (auto it = std::filesystem::recursive_directory_iterator(root, ec); !ec && it != std::filesystem::recursive_directory_iterator();
         it.increment(ec))
    {
        if (!it->is_regular_file() || Lower(it->path().extension().string()) != ".fx")
            continue;

        EffectState state;
        state.kind = EffectState::OnDisk;
        const std::string name = Lower(it->path().filename().string());
        state.byIlion = byIlion.count(name) > 0;

        std::ifstream file(it->path(), std::ios::binary);
        const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        state.needsDepth = NeedsDepth(text);

        found[name] = state;
    }

    auto& c = C();
    std::scoped_lock guard(c.lock);

    // Keep failures and the effect being installed; refresh the rest
    for (auto& [name, state] : c.states)
    {
        if ((state.kind == EffectState::Failed || state.kind == EffectState::Working) && found.count(name) == 0)
            found[name] = state;
    }

    c.states = std::move(found);
}

std::string RawUrl(const PackageInfo& p, const std::string& path)
{
    std::string encoded;
    for (const char ch : path)
        encoded += ch == ' ' ? std::string("%20") : std::string(1, ch);
    return "https://raw.githubusercontent.com/" + p.owner + "/" + p.repo + "/" + p.branch + "/" + encoded;
}

std::string DirOf(const std::string& path)
{
    const auto slash = path.find_last_of('/');
    return slash == std::string::npos ? "" : path.substr(0, slash + 1);
}

// "a/b/../c" -> "a/c"
std::string Normalize(const std::string& path)
{
    std::vector<std::string> parts;
    std::stringstream stream(path);
    std::string part;
    while (std::getline(stream, part, '/'))
    {
        if (part.empty() || part == ".")
            continue;
        if (part == ".." && !parts.empty())
            parts.pop_back();
        else
            parts.push_back(part);
    }
    std::string out;
    for (const auto& p : parts)
        out += (out.empty() ? "" : "/") + p;
    return out;
}

// The folder of the repository that maps to the install path: the one named Shaders (or Textures), else the file's own
std::string RootFor(const std::string& path, const char* folder)
{
    const std::string lower = Lower(path);
    const std::string key = Lower(std::string(folder)) + "/";
    size_t at = lower.rfind("/" + key);
    if (at != std::string::npos)
        return path.substr(0, at + 1 + key.size());
    if (lower.rfind(key, 0) == 0)
        return path.substr(0, key.size());
    return DirOf(path);
}

const std::string* FindInTree(const std::vector<std::string>& tree, const std::string& exact, const std::string& name)
{
    for (const auto& path : tree)
        if (Lower(path) == Lower(exact))
            return &path;

    const std::string suffix = "/" + Lower(name);
    for (const auto& path : tree)
    {
        const std::string lower = Lower(path);
        if (lower == Lower(name) || (lower.size() > suffix.size() && lower.compare(lower.size() - suffix.size(), suffix.size(), suffix) == 0))
            return &path;
    }

    return nullptr;
}

bool ExistsUnder(const std::filesystem::path& root, const std::string& name)
{
    std::error_code ec;
    const std::string lower = Lower(std::filesystem::path(name).filename().string());
    for (auto it = std::filesystem::recursive_directory_iterator(root, ec); !ec && it != std::filesystem::recursive_directory_iterator();
         it.increment(ec))
    {
        if (it->is_regular_file() && Lower(it->path().filename().string()) == lower)
            return true;
    }
    return false;
}

void SetState(const std::string& effect, EffectState::Kind kind, const std::string& message, bool depth = false)
{
    auto& c = C();
    std::scoped_lock guard(c.lock);
    auto& s = c.states[Lower(effect)];
    s.kind = kind;
    s.message = message;
    s.needsDepth = depth;
}

// The install itself, on the background thread
void InstallJob(size_t packageIndex, std::string effect)
{
    auto& c = C();
    PackageInfo package;
    {
        std::scoped_lock guard(c.lock);
        package = c.packages[packageIndex];
    }

    const auto fail = [&](const std::string& why)
    {
        SetState(effect, EffectState::Failed, why);
        c.busy = false;
    };

    if (package.owner.empty())
        return fail("Only packages hosted on GitHub can be installed from ILION");

    std::string body, error;

    // The repository's file tree, once per package
    if (package.tree.empty())
    {
        const std::string api = "https://api.github.com/repos/" + package.owner + "/" + package.repo + "/git/trees/" + package.branch + "?recursive=1";
        if (!HttpGet(api, body, error))
            return fail("The package's file list couldn't be read (" + error + ")");

        try
        {
            const auto json = nlohmann::json::parse(body);
            for (const auto& entry : json.at("tree"))
                if (entry.value("type", "") == "blob")
                    package.tree.push_back(entry.value("path", ""));
        }
        catch (...)
        {
            return fail("The package's file list couldn't be read");
        }

        std::scoped_lock guard(c.lock);
        c.packages[packageIndex].tree = package.tree;
    }

    const std::string* effectPath = FindInTree(package.tree, "", effect);
    if (effectPath == nullptr)
        return fail("The effect isn't in its package's repository any more");

    const std::filesystem::path game = GameDir();
    const std::filesystem::path shaderDest = game / std::filesystem::path(package.installPath).lexically_normal();
    const std::filesystem::path textureDest = game / std::filesystem::path(package.textureInstallPath).lexically_normal();
    const std::filesystem::path shaderSearch = game / L"reshade-shaders" / L"Shaders";
    const std::filesystem::path textureSearch = game / L"reshade-shaders" / L"Textures";
    const std::string shaderRoot = RootFor(*effectPath, "Shaders");

    // The files to write: repository path -> local path; headers and textures are followed from the effect
    std::vector<std::pair<std::string, std::filesystem::path>> files;
    std::map<std::string, std::string> contents;
    std::vector<std::string> pending { *effectPath };
    std::set<std::string> seen;
    std::vector<std::string> missing;
    bool depth = false;

    static const std::regex include(R"(#\s*include\s*[<"]([^>"]+)[>"])");
    static const std::regex texture(R"(source\s*=\s*"([^"]+)")");

    while (!pending.empty())
    {
        const std::string path = pending.back();
        pending.pop_back();
        if (!seen.insert(Lower(path)).second)
            continue;

        if (!HttpGet(RawUrl(package, path), body, error))
            return fail("Download failed: " + std::filesystem::path(path).filename().string() + " (" + error + ")");

        contents[path] = body;
        const std::string relative = path.size() > shaderRoot.size() && path.compare(0, shaderRoot.size(), shaderRoot) == 0
                                         ? path.substr(shaderRoot.size())
                                         : std::filesystem::path(path).filename().string();
        files.push_back({ path, shaderDest / std::filesystem::path(relative).make_preferred() });
        depth = depth || NeedsDepth(body);

        for (std::sregex_iterator it(body.begin(), body.end(), include), end; it != end; ++it)
        {
            const std::string name = (*it)[1];
            if (const auto* found = FindInTree(package.tree, Normalize(DirOf(path) + name), name); found != nullptr)
                pending.push_back(*found);
            else if (!ExistsUnder(shaderSearch, name))
                missing.push_back(name);
        }

        for (std::sregex_iterator it(body.begin(), body.end(), texture), end; it != end; ++it)
        {
            const std::string name = (*it)[1];
            if (seen.count(Lower("tex:" + name)) > 0)
                continue;
            seen.insert(Lower("tex:" + name));

            if (const auto* found = FindInTree(package.tree, "", name); found != nullptr)
            {
                std::string data;
                if (!HttpGet(RawUrl(package, *found), data, error))
                    return fail("Download failed: " + name + " (" + error + ")");

                const std::string root = RootFor(*found, "Textures");
                const std::string rel = found->size() > root.size() ? found->substr(root.size()) : name;
                contents[*found] = data;
                files.push_back({ *found, textureDest / std::filesystem::path(rel).make_preferred() });
            }
            else if (!ExistsUnder(textureSearch, name))
            {
                missing.push_back(name);
            }
        }
    }

    if (!missing.empty())
    {
        std::string list;
        for (const auto& m : missing)
            list += (list.empty() ? "" : ", ") + m;
        return fail("Missing files it needs: " + list);
    }

    // Write: files ILION already owns are replaced, anyone else's file stays as it is
    auto manifest = ReadManifest();
    std::set<std::string> owned;
    for (const auto& [e, p] : manifest)
        owned.insert(Lower(p));

    for (const auto& [repoPath, local] : files)
    {
        const std::string rel = std::filesystem::relative(local, game).string();
        std::error_code ec;

        if (std::filesystem::exists(local, ec) && owned.count(Lower(rel)) == 0)
            continue; // the user's or the installer's copy

        std::filesystem::create_directories(local.parent_path(), ec);
        std::ofstream out(local, std::ios::binary | std::ios::trunc);
        out.write(contents[repoPath].data(), (std::streamsize) contents[repoPath].size());
        if (!out)
            return fail("Can't write " + rel);

        bool listed = false;
        for (const auto& [e, p] : manifest)
            listed = listed || (Lower(e) == Lower(effect) && Lower(p) == Lower(rel));
        if (!listed)
            manifest.push_back({ effect, rel });
    }

    WriteManifest(manifest);
    ScanDisk();

    {
        std::scoped_lock guard(c.lock);
        auto& s = c.states[Lower(effect)];
        s.kind = EffectState::OnDisk;
        s.byIlion = true;
        s.needsDepth = depth;
        s.message.clear();
        c.finished.push_back(effect);
    }

    c.busy = false;
}
void RemoveJob(const std::string& effectFile);
} // namespace

void LoadIndex()
{
    auto& c = C();
    bool expected = false;
    if (!c.busy.compare_exchange_strong(expected, true))
        return;

    {
        std::scoped_lock guard(c.lock);
        c.index = Snapshot::Loading;
        c.busyWith.clear();
    }

    std::thread(
        []
        {
            auto& c = C();
            std::string body, error;
            const bool ok = HttpGet(kIndexUrl, body, error);
            auto packages = ok ? ParseIndex(body) : std::vector<PackageInfo> {};

            ScanDisk();

            std::scoped_lock guard(c.lock);
            if (ok && !packages.empty())
            {
                c.packages = std::move(packages);
                c.index = Snapshot::Ready;
                c.indexError.clear();
            }
            else
            {
                c.index = Snapshot::IndexFailed;
                c.indexError = ok ? "The package list is empty" : "The package list couldn't be downloaded (" + error + ")";
            }
            c.busy = false;
        })
        .detach();
}

Snapshot Get()
{
    auto& c = C();
    std::scoped_lock guard(c.lock);
    Snapshot s;
    s.index = c.index;
    s.indexError = c.indexError;
    for (const auto& p : c.packages)
        s.packages.push_back(p.pub);
    s.busy = c.busy;
    s.busyWith = c.busyWith;
    return s;
}

EffectState StateOf(const std::string& effectFile)
{
    auto& c = C();
    std::scoped_lock guard(c.lock);
    const auto found = c.states.find(Lower(effectFile));
    return found != c.states.end() ? found->second : EffectState {};
}

bool Install(size_t packageIndex, const std::string& effectFile)
{
    auto& c = C();
    bool expected = false;
    if (!c.busy.compare_exchange_strong(expected, true))
        return false;

    {
        std::scoped_lock guard(c.lock);
        if (packageIndex >= c.packages.size())
        {
            c.busy = false;
            return false;
        }
        c.busyWith = effectFile;
        c.states[Lower(effectFile)].kind = EffectState::Working;
        c.states[Lower(effectFile)].message.clear();
    }

    std::thread(InstallJob, packageIndex, effectFile).detach();
    return true;
}

bool Remove(const std::string& effectFile)
{
    auto& c = C();
    bool expected = false;
    if (!c.busy.compare_exchange_strong(expected, true))
        return false;

    std::thread([effectFile] { RemoveJob(effectFile); }).detach();
    return true;
}

void RemoveJob(const std::string& effectFile)
{
    auto manifest = ReadManifest();
    std::vector<std::pair<std::string, std::string>> keep;
    std::vector<std::string> candidates;

    for (const auto& line : manifest)
    {
        if (Lower(line.first) == Lower(effectFile))
            candidates.push_back(line.second);
        else
            keep.push_back(line);
    }

    // A file another effect still uses stays
    for (const auto& path : candidates)
    {
        bool shared = false;
        for (const auto& [e, p] : keep)
            shared = shared || Lower(p) == Lower(path);

        std::error_code ec;
        if (!shared)
            std::filesystem::remove(GameDir() / path, ec);
    }

    WriteManifest(keep);
    ScanDisk();
    C().busy = false;
}

std::vector<std::string> TakeFinished()
{
    auto& c = C();
    std::scoped_lock guard(c.lock);
    std::vector<std::string> out;
    out.swap(c.finished);
    return out;
}
} // namespace IlionReShadeCatalog
