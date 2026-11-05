#pragma once

// ILION AL DÍA: real headlines from the public RSS feeds of gaming and technology outlets, for the Home screen
// (replaces ILION Advisor's box).
//
// What is shown of each item: its headline, its outlet, its date and a link to the original article. Never the article,
// its summary or its pictures. Feeds that refuse automated readers (VideoCardz, Nexus Mods answer 403) are not used.
// Each installation asks the feeds itself, at most every 30 minutes, through curl.exe out of process (the same HTTP
// path as ILION COMMUNITY), on a thread of its own; the last good result is kept on disk and shown with its age when
// there is no connection. Nothing is invented: a block without verified news says so.
//
// Editorial control: an optional table of the ILION Community service (ilion_news_editorial, prepared in
// ilion-community/ilion_news_editorial.sql, NOT installed). Read with the public key only; while it does not exist the
// reading answers "not installed" and everything stays automatic. The client holds no editorial credentials.

#include "IlionCommunity.h"

#include <algorithm>
#include <atomic>
#include <ctime>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace IlionNews
{
namespace C = IlionCommunity;

enum Block : int
{
    Games, // A: videogames
    Tech,  // B: technology
    Mods,  // C: mods and community
    BlockCount,
};

inline const char* BlockName(int b) { return b == Games ? "VIDEOJUEGOS" : b == Tech ? "TECNOLOGÍA" : "MODS Y COMUNIDAD"; }

struct Feed
{
    const char* source;
    const char* url;
    Block block;
};

// Public RSS feeds checked on 2026-11-04 (HTTP 200, title, link and date per item)
inline const Feed kFeeds[] = {
    { "IGN", "https://www.ign.com/rss/articles/feed", Games },
    { "GameSpot", "https://www.gamespot.com/feeds/mashup/", Games },
    { "VGC", "https://www.videogameschronicle.com/feed/", Games },
    { "Eurogamer", "https://www.eurogamer.net/feed", Games },
    { "PC Gamer", "https://www.pcgamer.com/rss/", Games },
    { "TechPowerUp", "https://www.techpowerup.com/rss/news", Tech },
    { "AMD GPUOpen", "https://gpuopen.com/feed.xml", Tech },
    { "DSOGaming", "https://www.dsogaming.com/feed/", Tech },
    { "ModDB", "https://rss.moddb.com/news/feed/rss.xml", Mods },
    { "DSOGaming Mods", "https://www.dsogaming.com/category/mods/feed/", Mods },
};

struct Item
{
    std::string title, link, source;
    long long time = 0; // UTC, seconds
    int block = Games;
    bool rumor = false;
    bool editorial = false, pinned = false, sponsored = false;
};

struct Snapshot
{
    std::vector<Item> blocks[BlockCount]; // up to three each
    long long fetched = 0;                // when the shown items were fetched (UTC), 0 never
    bool busy = false;
    bool offline = false;                 // the last attempt failed: the items are the cached ones
    int sourcesOk = 0, sourcesTried = 0;
    std::string editorial = "sin comprobar"; // state of the editorial table
};

// ---- Parsing ---------------------------------------------------------------------------------------------------------
inline void AppendUtf8(std::string& out, unsigned cp)
{
    if (cp < 0x80)
        out += (char) cp;
    else if (cp < 0x800)
        out += (char) (0xC0 | (cp >> 6)), out += (char) (0x80 | (cp & 0x3F));
    else if (cp < 0x10000)
        out += (char) (0xE0 | (cp >> 12)), out += (char) (0x80 | ((cp >> 6) & 0x3F)), out += (char) (0x80 | (cp & 0x3F));
    else
        out += (char) (0xF0 | (cp >> 18)), out += (char) (0x80 | ((cp >> 12) & 0x3F)), out += (char) (0x80 | ((cp >> 6) & 0x3F)),
            out += (char) (0x80 | (cp & 0x3F));
}

// Text of an element: CDATA unwrapped, tags removed, entities decoded, spaces collapsed
inline std::string CleanText(std::string s)
{
    for (size_t a; (a = s.find("<![CDATA[")) != std::string::npos;)
    {
        const size_t b = s.find("]]>", a);
        s = s.substr(0, a) + s.substr(a + 9, (b == std::string::npos ? s.size() : b) - a - 9) + (b == std::string::npos ? "" : s.substr(b + 3));
    }
    std::string noTags;
    bool inTag = false;
    for (char c : s)
    {
        if (c == '<')
            inTag = true;
        else if (c == '>')
            inTag = false;
        else if (!inTag)
            noTags += c;
    }
    std::string out;
    for (size_t i = 0; i < noTags.size(); i++)
    {
        if (noTags[i] != '&')
        {
            out += noTags[i];
            continue;
        }
        const size_t end = noTags.find(';', i);
        if (end == std::string::npos || end - i > 10)
        {
            out += '&';
            continue;
        }
        const std::string e = noTags.substr(i + 1, end - i - 1);
        if (e == "amp")
            out += '&';
        else if (e == "lt")
            out += '<';
        else if (e == "gt")
            out += '>';
        else if (e == "quot")
            out += '"';
        else if (e == "apos")
            out += '\'';
        else if (e == "nbsp")
            out += ' ';
        else if (!e.empty() && e[0] == '#')
            AppendUtf8(out, (unsigned) strtoul(e.c_str() + (e.size() > 1 && (e[1] == 'x' || e[1] == 'X') ? 2 : 1), nullptr,
                                                e.size() > 1 && (e[1] == 'x' || e[1] == 'X') ? 16 : 10));
        else
            out += noTags.substr(i, end - i + 1);
        i = end;
    }
    std::string collapsed;
    for (char c : out)
    {
        const bool space = c == ' ' || c == '\n' || c == '\r' || c == '\t';
        if (space && (collapsed.empty() || collapsed.back() == ' '))
            continue;
        collapsed += space ? ' ' : c;
    }
    while (!collapsed.empty() && collapsed.back() == ' ')
        collapsed.pop_back();
    return collapsed;
}

inline std::string Inner(const std::string& xml, const char* tag, size_t from = 0, size_t to = std::string::npos)
{
    const std::string open = std::string("<") + tag;
    for (size_t a = xml.find(open, from); a != std::string::npos && a < to; a = xml.find(open, a + 1))
    {
        const char next = a + open.size() < xml.size() ? xml[a + open.size()] : 0;
        if (next != '>' && next != ' ' && next != '/')
            continue; // <titleX> is not <title>
        const size_t start = xml.find('>', a);
        if (start == std::string::npos || start > to)
            return std::string();
        if (xml[start - 1] == '/')
            return std::string(); // <tag/>
        const size_t end = xml.find(std::string("</") + tag + ">", start);
        if (end == std::string::npos || end > to)
            return std::string();
        return xml.substr(start + 1, end - start - 1);
    }
    return std::string();
}

// "Wed, 04 Nov 2026 21:47:40 +0000" (RFC 822) or "2026-11-04T21:47:40Z" / "+01:00" (ISO 8601), to UTC seconds
inline long long ParseDate(const std::string& text)
{
    static const char* months[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
    std::tm tm {};
    int offset = 0;
    if (text.size() >= 19 && text[4] == '-' && text[10] == 'T')
    {
        if (sscanf_s(text.c_str(), "%d-%d-%dT%d:%d:%d", &tm.tm_year, &tm.tm_mon, &tm.tm_mday, &tm.tm_hour, &tm.tm_min, &tm.tm_sec) != 6)
            return 0;
        tm.tm_year -= 1900, tm.tm_mon -= 1;
        const size_t z = text.find_first_of("+-", 19);
        if (z != std::string::npos && z + 5 < text.size() + 1)
        {
            const int h = atoi(text.substr(z + 1, 2).c_str()), m = atoi(text.substr(z + 4, 2).c_str());
            offset = (text[z] == '-' ? -1 : 1) * (h * 3600 + m * 60);
        }
    }
    else
    {
        char mon[8] = {}, zone[16] = {};
        int day = 0, year = 0, h = 0, mi = 0, se = 0;
        const char* p = text.c_str();
        if (const char* comma = strchr(p, ','); comma != nullptr)
            p = comma + 1;
        if (sscanf_s(p, "%d %3s %d %d:%d:%d %15s", &day, mon, (unsigned) sizeof(mon), &year, &h, &mi, &se, zone, (unsigned) sizeof(zone)) < 6)
            return 0;
        int month = -1;
        for (int i = 0; i < 12; i++)
            if (_stricmp(mon, months[i]) == 0)
                month = i;
        if (month < 0)
            return 0;
        tm.tm_year = year - 1900, tm.tm_mon = month, tm.tm_mday = day, tm.tm_hour = h, tm.tm_min = mi, tm.tm_sec = se;
        if ((zone[0] == '+' || zone[0] == '-') && strlen(zone) >= 5)
        {
            const int v = atoi(zone + 1);
            offset = (zone[0] == '-' ? -1 : 1) * ((v / 100) * 3600 + (v % 100) * 60);
        }
    }
    const long long t = (long long) _mkgmtime(&tm);
    return t <= 0 ? 0 : t - offset;
}

// The items of an RSS (<item>) or Atom (<entry>) feed: title, link, date
inline std::vector<Item> ParseFeed(const std::string& xml, const Feed& feed)
{
    std::vector<Item> out;
    const bool atom = xml.find("<entry") != std::string::npos && xml.find("<item") == std::string::npos;
    const char* tag = atom ? "entry" : "item";
    const std::string open = std::string("<") + tag, close = std::string("</") + tag + ">";
    for (size_t a = xml.find(open); a != std::string::npos && out.size() < 40; a = xml.find(open, a + 1))
    {
        const size_t b = xml.find(close, a);
        if (b == std::string::npos)
            break;
        Item it;
        it.source = feed.source;
        it.block = feed.block;
        it.title = CleanText(Inner(xml, "title", a, b));
        if (atom)
        {
            const size_t l = xml.find("<link", a);
            const size_t h = l == std::string::npos || l > b ? std::string::npos : xml.find("href=\"", l);
            if (h != std::string::npos && h < b)
                it.link = xml.substr(h + 6, xml.find('"', h + 6) - h - 6);
            it.time = ParseDate(CleanText(Inner(xml, "updated", a, b)));
        }
        else
        {
            it.link = CleanText(Inner(xml, "link", a, b));
            std::string date = CleanText(Inner(xml, "pubDate", a, b));
            if (date.empty())
                date = CleanText(Inner(xml, "dc:date", a, b));
            it.time = ParseDate(date);
        }
        // Only real web links; titles of a sane length
        if (it.title.size() < 8 || it.title.size() > 300 || (it.link.rfind("https://", 0) != 0 && it.link.rfind("http://", 0) != 0) || it.time <= 0)
            continue;
        std::string lower = it.title;
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return (char) std::tolower(c); });
        for (const char* w : { "rumor", "rumour", "leak", "reportedly", "allegedly", "insider" })
            it.rumor = it.rumor || lower.find(w) != std::string::npos;
        out.push_back(std::move(it));
    }
    return out;
}

// The key used to drop the same news told by two outlets: its title, lower case, letters and digits only
inline std::string TitleKey(const std::string& title)
{
    std::string k;
    for (unsigned char c : title)
        if (std::isalnum(c))
            k += (char) std::tolower(c);
    return k.substr(0, 48);
}

// Promotions are only shown while fresh (a deal from days ago may be over); everything else within the age of its block
inline bool Promotion(const std::string& title)
{
    std::string lower = title;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return (char) std::tolower(c); });
    for (const char* w : { "deal", "% off", "sale", "free to keep", "free on", "giveaway", "discount" })
        if (lower.find(w) != std::string::npos)
            return true;
    return false;
}

// Three per block: the newest, one per outlet first, no duplicates; A and B within 3 days, mods within 7, deals 48 h
inline void Select(std::vector<Item> all, Snapshot& s, long long now)
{
    std::sort(all.begin(), all.end(), [](const Item& a, const Item& b) { return a.pinned != b.pinned ? a.pinned : a.time > b.time; });
    std::vector<std::string> seen;
    for (int b = 0; b < BlockCount; b++)
    {
        s.blocks[b].clear();
        const long long maxAge = (b == Mods ? 7 : 3) * 86400LL;
        for (int pass = 0; pass < 2 && s.blocks[b].size() < 3; pass++)
            for (const auto& it : all)
            {
                if (s.blocks[b].size() >= 3)
                    break;
                if (it.block != b || (!it.editorial && (now - it.time > maxAge || (Promotion(it.title) && now - it.time > 48 * 3600LL))))
                    continue;
                const std::string key = TitleKey(it.title);
                if (std::find(seen.begin(), seen.end(), key) != seen.end())
                    continue;
                // First pass: one per outlet, so a block is not three headlines of the same site
                if (pass == 0 && !it.editorial &&
                    std::any_of(s.blocks[b].begin(), s.blocks[b].end(), [&](const Item& x) { return x.source == it.source; }))
                    continue;
                s.blocks[b].push_back(it);
                seen.push_back(key);
            }
    }
}

// ---- Cache (%LOCALAPPDATA%\ILION\News\noticias.json) ------------------------------------------------------------------
inline std::filesystem::path CachePath() { return C::Root() / L"News" / L"noticias.json"; }

inline void SaveCache(const Snapshot& s)
{
    C::Json j { { "fetched", s.fetched }, { "items", C::Json::array() } };
    for (int b = 0; b < BlockCount; b++)
        for (const auto& it : s.blocks[b])
            j["items"].push_back({ { "title", it.title }, { "link", it.link }, { "source", it.source }, { "time", it.time }, { "block", it.block },
                                   { "rumor", it.rumor }, { "editorial", it.editorial }, { "pinned", it.pinned } });
    C::WriteAtomic(CachePath(), j.dump(1));
}

inline bool LoadCache(Snapshot& s)
{
    std::ifstream f(CachePath(), std::ios::binary);
    if (!f)
        return false;
    const C::Json j = C::Json::parse(f, nullptr, false);
    if (!j.is_object() || !j.contains("items") || !j["items"].is_array())
        return false;
    s.fetched = j.value("fetched", 0LL);
    for (const auto& e : j["items"])
    {
        if (!e.is_object())
            continue;
        Item it;
        it.title = e.value("title", std::string());
        it.link = e.value("link", std::string());
        it.source = e.value("source", std::string());
        it.time = e.value("time", 0LL);
        it.block = std::clamp(e.value("block", 0), 0, BlockCount - 1);
        it.rumor = e.value("rumor", false);
        it.editorial = e.value("editorial", false);
        it.pinned = e.value("pinned", false);
        if (!it.title.empty() && it.link.rfind("http", 0) == 0 && s.blocks[it.block].size() < 3)
            s.blocks[it.block].push_back(it);
    }
    return true;
}

// ---- Editorial table (optional, public read) -------------------------------------------------------------------------
// Rows: block, title, url, source, pinned, sponsored, hide_url (drops an automatic item with that link), priority.
// The server only serves the active ones (starts_at <= now < ends_at) through its read policy
inline std::string ReadEditorial(std::vector<Item>& add, std::vector<std::string>& hide)
{
    const C::ServiceConfig cfg = C::LoadService();
    if (!cfg.valid)
        return "servicio no configurado";
    const C::HttpResult r = C::HttpRequest("GET", cfg.url + "/rest/v1/ilion_news_editorial?select=*&order=priority.desc&limit=30", C::Headers(cfg, "", false), "");
    if (r.status == 404 || r.body.find("PGRST205") != std::string::npos || r.body.find("does not exist") != std::string::npos)
        return "no instalado (todo automático)";
    const C::Json j = C::Json::parse(r.body, nullptr, false);
    if (r.status != 200 || !j.is_array())
        return "no disponible ahora";
    for (const auto& e : j)
    {
        if (!e.is_object())
            continue;
        if (const std::string h = e.value("hide_url", std::string()); !h.empty())
            hide.push_back(h);
        Item it;
        it.title = e.value("title", std::string());
        it.link = e.value("url", std::string());
        it.source = e.value("source", std::string("ILION"));
        it.block = std::clamp(e.value("block", 0), 0, BlockCount - 1);
        it.pinned = e.value("pinned", false);
        it.sponsored = e.value("sponsored", false);
        it.editorial = true;
        it.time = (long long) std::time(nullptr);
        if (it.title.size() >= 4 && it.link.rfind("https://", 0) == 0)
            add.push_back(it);
    }
    return j.empty() ? "instalado, sin entradas (todo automático)" : "instalado, " + std::to_string(j.size()) + " entradas";
}

// ---- The background reader -------------------------------------------------------------------------------------------
class Service
{
  public:
    static Service& Instance()
    {
        static Service s;
        return s;
    }

    // While AL DÍA is on screen: loads the cache once, refreshes every 30 minutes (and at once when asked)
    void KeepAlive()
    {
        alive.store(GetTickCount64(), std::memory_order_relaxed);
        if (!started.exchange(true))
            std::thread([this] { Loop(); }).detach();
    }

    void RefreshNow() { force.store(true); }

    Snapshot Get()
    {
        std::lock_guard lock(mutex);
        return snap;
    }

  private:
    void Loop()
    {
        {
            Snapshot cached;
            if (LoadCache(cached))
            {
                std::lock_guard lock(mutex);
                snap = cached;
            }
        }
        while (true)
        {
            const long long now = (long long) std::time(nullptr);
            const bool wanted = GetTickCount64() - alive.load(std::memory_order_relaxed) < 5000;
            long long fetched;
            {
                std::lock_guard lock(mutex);
                fetched = snap.fetched;
            }
            const bool due = force.exchange(false) || fetched == 0 || now - fetched >= 30 * 60 || (lastTry > 0 && now - lastTry >= 10 * 60 && offline);
            if (wanted && due && (lastTry == 0 || now - lastTry >= 60))
                Fetch(now);
            Sleep(1000);
        }
    }

    void Fetch(long long now)
    {
        lastTry = now;
        {
            std::lock_guard lock(mutex);
            snap.busy = true;
        }
        std::vector<Item> all;
        int ok = 0, tried = 0;
        const std::vector<std::string> headers = { "User-Agent: ILION-News/1.4 (OptiScaler ILION; headlines with links to the source)" };
        for (const auto& feed : kFeeds)
        {
            tried++;
            const C::HttpResult r = C::HttpRequest("GET", feed.url, headers, "");
            if (r.status != 200)
                continue;
            auto items = ParseFeed(r.body, feed);
            if (!items.empty())
                ok++;
            all.insert(all.end(), std::make_move_iterator(items.begin()), std::make_move_iterator(items.end()));
        }
        std::vector<Item> add;
        std::vector<std::string> hide;
        const std::string editorial = ReadEditorial(add, hide);
        all.erase(std::remove_if(all.begin(), all.end(), [&](const Item& it) { return std::find(hide.begin(), hide.end(), it.link) != hide.end(); }),
                  all.end());
        all.insert(all.end(), add.begin(), add.end());

        std::lock_guard lock(mutex);
        snap.busy = false;
        snap.sourcesOk = ok;
        snap.sourcesTried = tried;
        snap.editorial = editorial;
        offline = ok == 0;
        snap.offline = offline;
        if (ok > 0)
        {
            Snapshot fresh = snap;
            Select(all, fresh, now);
            fresh.fetched = now;
            snap = fresh;
            SaveCache(snap);
        }
    }

    std::atomic<uint64_t> alive { 0 };
    std::atomic<bool> started { false }, force { false };
    long long lastTry = 0;
    bool offline = false;
    std::mutex mutex;
    Snapshot snap;
};
} // namespace IlionNews
