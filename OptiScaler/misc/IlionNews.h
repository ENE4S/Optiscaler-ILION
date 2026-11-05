#pragma once

// ILION AL DÍA: real headlines from the public RSS feeds of gaming and technology outlets, for the Home screen
// (replaces ILION Advisor's box).
//
// What is shown of each item: its headline, a short excerpt of the description the outlet publishes in its own feed for
// that purpose, the picture the outlet attaches to the item (or the og:image / twitter:image of the article), the outlet,
// the date and a link to the original article. Never the article itself. v1.4.0: outlets that publish in Spanish, so the
// headline and the excerpt are theirs and in Spanish (no machine translation: ILION has no translator without sending the
// texts to an outside service). Feeds that refuse automated readers are not used.
// Each installation asks the feeds itself, at most every 30 minutes, through curl.exe out of process (the same HTTP
// path as ILION COMMUNITY), on a thread of its own; the last good result is kept on disk and shown with its age when
// there is no connection. Nothing is invented: a block without verified news says so.
//
// Editorial control: an optional table of the ILION Community service (ilion_news_editorial, prepared in
// ilion-community/ilion_news_editorial.sql, NOT installed). Read with the public key only; while it does not exist the
// reading answers "not installed" and everything stays automatic. The client holds no editorial credentials.

#include "IlionCommunity.h"

#include <wincodec.h>

#pragma execution_character_set(push, "utf-8")

#include <algorithm>
#include <atomic>
#include <cmath>
#include <ctime>
#include <fstream>
#include <memory>
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

// Public RSS feeds in Spanish, checked on 2026-11-05 (HTTP 200; title, link, date, description and a picture per item)
inline const Feed kFeeds[] = {
    { "Eurogamer.es", "https://www.eurogamer.es/feed", Games },
    { "IGN España", "https://es.ign.com/feed.xml", Games },
    { "HobbyConsolas", "https://www.hobbyconsolas.com/rss/", Games },
    { "3DJuegos", "https://www.3djuegos.com/feedburner.xml", Games },
    { "VidaExtra", "https://www.vidaextra.com/feedburner.xml", Games },
    { "Xataka", "https://www.xataka.com/feedburner.xml", Tech },
    { "HardZone", "https://hardzone.es/feed/", Tech },
    { "MuyComputer", "https://www.muycomputer.com/feed/", Tech },
    { "Profesional Review", "https://www.profesionalreview.com/feed/", Tech },
    { "3DJuegos · Mods", "https://www.3djuegos.com/tag/mods/rss2.xml", Mods },
    { "VidaExtra · Mods", "https://www.vidaextra.com/tag/mods/rss2.xml", Mods },
};

// The picture of an item, cropped (centred, never stretched) to a fixed size for one reusable texture of the menu
constexpr int kImageW = 320, kImageH = 180;
using Pixels = std::shared_ptr<const std::vector<uint8_t>>; // kImageW x kImageH RGBA, or null

struct Item
{
    std::string title, link, source;
    std::string summary;  // a short excerpt of the outlet's own description, or empty
    std::string imageUrl; // the picture of the item (feed, og:image or twitter:image), or empty
    Pixels image;         // decoded picture, null when there is none valid
    long long time = 0; // UTC, seconds
    int block = Games;
    bool rumor = false;
    bool editorial = false, pinned = false, sponsored = false;
};

struct Snapshot
{
    std::vector<Item> blocks[BlockCount]; // up to three each
    std::vector<Item> rotation;           // one at a time in this order: A1, B1, C1, A2, B2…
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

// Named HTML entities: all of Latin-1 (á, ñ, ¿, ¡, ü, º…) and the usual typography (’, “, …, –, €). Without them,
// a feed that writes «Acci&oacute;n» showed the entity itself
inline unsigned NamedEntity(const std::string& e)
{
    static const char* latin1[96] = { "nbsp", "iexcl", "cent", "pound", "curren", "yen", "brvbar", "sect", "uml", "copy", "ordf", "laquo",
                                      "not", "shy", "reg", "macr", "deg", "plusmn", "sup2", "sup3", "acute", "micro", "para", "middot",
                                      "cedil", "sup1", "ordm", "raquo", "frac14", "frac12", "frac34", "iquest", "Agrave", "Aacute", "Acirc",
                                      "Atilde", "Auml", "Aring", "AElig", "Ccedil", "Egrave", "Eacute", "Ecirc", "Euml", "Igrave", "Iacute",
                                      "Icirc", "Iuml", "ETH", "Ntilde", "Ograve", "Oacute", "Ocirc", "Otilde", "Ouml", "times", "Oslash",
                                      "Ugrave", "Uacute", "Ucirc", "Uuml", "Yacute", "THORN", "szlig", "agrave", "aacute", "acirc", "atilde",
                                      "auml", "aring", "aelig", "ccedil", "egrave", "eacute", "ecirc", "euml", "igrave", "iacute", "icirc",
                                      "iuml", "eth", "ntilde", "ograve", "oacute", "ocirc", "otilde", "ouml", "divide", "oslash", "ugrave",
                                      "uacute", "ucirc", "uuml", "yacute", "thorn", "yuml" };
    for (unsigned i = 0; i < 96; i++)
        if (e == latin1[i])
            return 160 + i;
    static const struct
    {
        const char* name;
        unsigned cp;
    } other[] = { { "ndash", 0x2013 }, { "mdash", 0x2014 }, { "lsquo", 0x2018 }, { "rsquo", 0x2019 }, { "sbquo", 0x201A },
                  { "ldquo", 0x201C }, { "rdquo", 0x201D }, { "bdquo", 0x201E }, { "hellip", 0x2026 }, { "bull", 0x2022 },
                  { "euro", 0x20AC }, { "trade", 0x2122 }, { "prime", 0x2032 }, { "lsaquo", 0x2039 }, { "rsaquo", 0x203A },
                  { "OElig", 0x152 }, { "oelig", 0x153 }, { "Scaron", 0x160 }, { "scaron", 0x161 }, { "Yuml", 0x178 }, { "thinsp", 0x2009 },
                  { "ensp", 0x2002 }, { "emsp", 0x2003 }, { "zwnj", 0x200C }, { "zwj", 0x200D } };
    for (const auto& o : other)
        if (e == o.name)
            return o.cp;
    return 0;
}

// Bytes that are not valid UTF-8 become U+FFFD: what reaches ImGui and the cache is always valid text
inline std::string ValidUtf8(const std::string& s)
{
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size();)
    {
        const unsigned char c = (unsigned char) s[i];
        const int n = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 0;
        bool ok = n > 0 && i + n <= s.size();
        for (int k = 1; ok && k < n; k++)
            ok = ((unsigned char) s[i + k] & 0xC0) == 0x80;
        if (ok && n == 2 && c < 0xC2)
            ok = false; // overlong
        if (ok)
            out.append(s, i, n), i += n;
        else
            out += "\xEF\xBF\xBD", i++; // U+FFFD, written as bytes so it doesn't depend on the source encoding
    }
    return out;
}

// Text of an element: CDATA unwrapped, tags removed, entities decoded, spaces collapsed
inline std::string CleanText(std::string s)
{
    for (size_t a; (a = s.find("<![CDATA[")) != std::string::npos;)
    {
        const size_t b = s.find("]]>", a);
        s = s.substr(0, a) + s.substr(a + 9, (b == std::string::npos ? s.size() : b) - a - 9) + (b == std::string::npos ? "" : s.substr(b + 3));
    }
    // A tag starts with < and a letter, / ! or ? and ends at its >; a lone «<» (A < B) is text
    std::string noTags;
    for (size_t i = 0; i < s.size(); i++)
    {
        const char next = i + 1 < s.size() ? s[i + 1] : 0;
        const size_t close = s[i] == '<' && (std::isalpha((unsigned char) next) || next == '/' || next == '!' || next == '?') ? s.find('>', i) : std::string::npos;
        if (close != std::string::npos)
            i = close;
        else
            noTags += s[i];
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
        else if (const unsigned cp = NamedEntity(e); cp != 0)
            AppendUtf8(out, cp == 160 ? 32 : cp); // a non-breaking space as a plain one
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
    return ValidUtf8(collapsed);
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

// The value of an attribute inside a tag's text: url="…" or url='…'
inline std::string Attr(const std::string& tag, const char* name)
{
    const std::string key = std::string(name) + "=";
    for (size_t at = tag.find(key); at != std::string::npos; at = tag.find(key, at + 1))
    {
        if (at > 0 && std::isalnum((unsigned char) tag[at - 1]))
            continue;
        const size_t q = at + key.size();
        if (q >= tag.size() || (tag[q] != '"' && tag[q] != '\''))
            continue;
        const size_t end = tag.find(tag[q], q + 1);
        if (end != std::string::npos)
            return tag.substr(q + 1, end - q - 1);
    }
    return std::string();
}

inline bool WebImageUrl(const std::string& u)
{
    if (u.rfind("https://", 0) != 0 || u.size() > 1000)
        return false;
    for (char c : u)
        if ((unsigned char) c < 0x21 || c == '"' || c == '<' || c == '>')
            return false;
    return true;
}

// The picture the feed gives an item: media:content / media:thumbnail / an image enclosure, else the first <img> of its
// description or content
inline std::string ItemImage(const std::string& xml, size_t a, size_t b)
{
    for (const char* tag : { "<media:content", "<media:thumbnail", "<enclosure" })
        for (size_t at = xml.find(tag, a); at != std::string::npos && at < b; at = xml.find(tag, at + 1))
        {
            const size_t end = xml.find('>', at);
            if (end == std::string::npos || end > b)
                break;
            const std::string t = xml.substr(at, end - at);
            const std::string type = Attr(t, "type"), medium = Attr(t, "medium");
            if (!type.empty() && type.rfind("image/", 0) != 0)
                continue;
            if (!medium.empty() && medium != "image")
                continue;
            std::string url = CleanText(Attr(t, "url"));
            if (WebImageUrl(url))
                return url;
        }
    for (const char* tag : { "description", "content:encoded" })
    {
        // The HTML of the description comes escaped (&lt;img …) or in CDATA
        std::string html = Inner(xml, tag, a, b);
        for (size_t c; (c = html.find("<![CDATA[")) != std::string::npos;)
            html.erase(c, 9);
        for (size_t c; (c = html.find("]]>")) != std::string::npos;)
            html.erase(c, 3);
        std::string decoded;
        for (size_t i = 0; i < html.size(); i++)
        {
            if (html.compare(i, 4, "&lt;") == 0)
                decoded += '<', i += 3;
            else if (html.compare(i, 4, "&gt;") == 0)
                decoded += '>', i += 3;
            else if (html.compare(i, 6, "&quot;") == 0)
                decoded += '"', i += 5;
            else if (html.compare(i, 5, "&amp;") == 0)
                decoded += '&', i += 4;
            else
                decoded += html[i];
        }
        for (size_t at = decoded.find("<img"); at != std::string::npos; at = decoded.find("<img", at + 1))
        {
            const size_t end = decoded.find('>', at);
            if (end == std::string::npos)
                break;
            const std::string url = Attr(decoded.substr(at, end - at), "src");
            if (WebImageUrl(url))
                return url;
        }
    }
    return std::string();
}

// A short excerpt of the outlet's description (its own text, never written here): tags and links out, without repeating
// the headline, cut at the end of a sentence or a word
inline std::string Summary(std::string text, const std::string& title)
{
    for (const char* tail : { "The post ", "La entrada ", "Leer más", "Seguir leyendo", "Read more", "[…]", "[...]" })
        if (const size_t at = text.find(tail); at != std::string::npos && at > 20)
            text.resize(at);
    if (!title.empty() && text.rfind(title, 0) == 0)
        text.erase(0, title.size());
    while (!text.empty() && (text.front() == ' ' || text.front() == '.' || text.front() == ':' || text.front() == '-'))
        text.erase(text.begin());
    constexpr size_t kMax = 230;
    if (text.size() > kMax)
    {
        size_t cut = std::string::npos;
        for (size_t i = kMax; i > 80; i--)
            if (text[i] == '.' && (i + 1 >= text.size() || text[i + 1] == ' '))
            {
                cut = i + 1;
                break;
            }
        if (cut == std::string::npos)
        {
            cut = text.rfind(' ', kMax);
            text = text.substr(0, cut == std::string::npos ? kMax : cut) + "…";
        }
        else
            text.resize(cut);
    }
    while (!text.empty() && text.back() == ' ')
        text.pop_back();
    return text.size() >= 20 ? text : std::string();
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
        it.title = CleanText(CleanText(Inner(xml, "title", a, b))); // twice: some outlets escape HTML inside the title too
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
            // Twice: some outlets escape the HTML of the description (&lt;img …&gt;), so tags appear after decoding
            std::string desc = CleanText(CleanText(Inner(xml, "description", a, b)));
            if (desc.size() < 20)
                desc = CleanText(CleanText(Inner(xml, "content:encoded", a, b)));
            it.summary = Summary(desc, it.title);
            it.imageUrl = ItemImage(xml, a, b);
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

// One at a time: the first of each block, then the second… (never the same twice in a row)
inline void BuildRotation(Snapshot& s)
{
    s.rotation.clear();
    for (size_t i = 0; i < 3; i++)
        for (int b = 0; b < BlockCount; b++)
            if (i < s.blocks[b].size())
                s.rotation.push_back(s.blocks[b][i]);
}

// Three per block: the newest, one per outlet first, no duplicates; A and B within 3 days, mods within 7, deals 48 h
inline void Select(std::vector<Item> all, Snapshot& s, long long now)
{
    std::sort(all.begin(), all.end(), [](const Item& a, const Item& b) { return a.pinned != b.pinned ? a.pinned : a.time > b.time; });
    std::vector<std::string> seen;
    for (int b = 0; b < BlockCount; b++)
    {
        s.blocks[b].clear();
        const long long maxAge = (b == Mods ? 14 : 3) * 86400LL;
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
    BuildRotation(s);
}

// ---- Pictures ---------------------------------------------------------------------------------------------------------
// Only https, at most 3 MB, only JPEG / PNG / GIF / BMP / WebP by their first bytes, at least 160x90; decoded by Windows
// (WIC), scaled to cover kImageW x kImageH and cropped from the centre. Kept on disk by the address (News\img), so a
// picture is downloaded once. All of it on the news thread, never in a frame of the game
inline bool LooksLikeImage(const std::string& b)
{
    const auto u = [&](size_t i) { return i < b.size() ? (unsigned char) b[i] : 0; };
    return (u(0) == 0xFF && u(1) == 0xD8 && u(2) == 0xFF) || (u(0) == 0x89 && b.compare(1, 3, "PNG") == 0) || b.compare(0, 4, "GIF8") == 0 ||
           b.compare(0, 2, "BM") == 0 || (b.compare(0, 4, "RIFF") == 0 && b.size() > 12 && b.compare(8, 4, "WEBP") == 0);
}

inline Pixels DecodeCover(const std::string& bytes)
{
    if (bytes.size() < 1024 || bytes.size() > 3 * 1024 * 1024 || !LooksLikeImage(bytes))
        return nullptr;
    const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool comOwned = com == S_OK || com == S_FALSE;
    IWICImagingFactory* factory = nullptr;
    IWICStream* stream = nullptr;
    IWICBitmapDecoder* decoder = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter* converter = nullptr;
    IWICBitmapScaler* scaler = nullptr;
    IWICBitmapClipper* clipper = nullptr;
    UINT w = 0, h = 0;
    auto out = std::make_shared<std::vector<uint8_t>>((size_t) kImageW * kImageH * 4);
    bool ok = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)) == S_OK &&
              factory->CreateStream(&stream) == S_OK && stream->InitializeFromMemory((BYTE*) bytes.data(), (DWORD) bytes.size()) == S_OK &&
              factory->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnDemand, &decoder) == S_OK && decoder->GetFrame(0, &frame) == S_OK &&
              frame->GetSize(&w, &h) == S_OK && w >= 160 && h >= 90 && w <= 16384 && h <= 16384;
    if (ok)
    {
        const double scale = (std::max)((double) kImageW / w, (double) kImageH / h);
        const UINT sw = (std::max)((UINT) kImageW, (UINT) std::ceil(w * scale)), sh = (std::max)((UINT) kImageH, (UINT) std::ceil(h * scale));
        const WICRect crop { (INT) (sw - kImageW) / 2, (INT) (sh - kImageH) / 2, kImageW, kImageH };
        // Scale and crop in the decoder's own format, then convert: the scaler keeps (or picks) its own pixel order, so a
        // conversion before it doesn't hold (red and blue came out swapped)
        ok = factory->CreateBitmapScaler(&scaler) == S_OK && scaler->Initialize(frame, sw, sh, WICBitmapInterpolationModeFant) == S_OK &&
             factory->CreateBitmapClipper(&clipper) == S_OK && clipper->Initialize(scaler, &crop) == S_OK &&
             factory->CreateFormatConverter(&converter) == S_OK &&
             converter->Initialize(clipper, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom) == S_OK &&
             converter->CopyPixels(nullptr, kImageW * 4, (UINT) out->size(), out->data()) == S_OK;
    }
    for (IUnknown* x : { (IUnknown*) converter, (IUnknown*) clipper, (IUnknown*) scaler, (IUnknown*) frame, (IUnknown*) decoder, (IUnknown*) stream,
                         (IUnknown*) factory })
        if (x != nullptr)
            x->Release();
    if (comOwned)
        CoUninitialize();
    if (!ok)
        return nullptr;
    for (size_t i = 3; i < out->size(); i += 4)
        (*out)[i] = 255; // a photo: opaque
    return out;
}

inline std::filesystem::path ImageDir() { return C::Root() / L"News" / L"img"; }

inline std::filesystem::path ImagePath(const std::string& url)
{
    uint64_t hash = 1469598103934665603ULL; // FNV-1a
    for (unsigned char c : url)
        hash = (hash ^ c) * 1099511628211ULL;
    wchar_t name[32];
    swprintf_s(name, L"%016llx.img", (unsigned long long) hash);
    return ImageDir() / name;
}

// The picture of an address: from the disk cache, or downloaded (network) and kept
inline Pixels LoadImage(const std::string& url, bool network, const std::vector<std::string>& headers)
{
    if (!WebImageUrl(url))
        return nullptr;
    const auto path = ImagePath(url);
    std::string bytes;
    if (C::ReadFileLimited(path, 3 * 1024 * 1024, bytes) && LooksLikeImage(bytes))
        return DecodeCover(bytes);
    if (!network)
        return nullptr;
    const C::HttpResult r = C::HttpRequest("GET", url, headers, "");
    if (r.status != 200 || !LooksLikeImage(r.body))
        return nullptr;
    Pixels px = DecodeCover(r.body);
    if (px)
        C::WriteAtomic(path, r.body);
    return px;
}

// og:image, then twitter:image, of an article (only its head is read)
inline std::string MetaImage(const std::string& html)
{
    const std::string head = html.substr(0, 400 * 1024);
    for (const char* key : { "og:image", "twitter:image" })
        for (size_t at = head.find(key); at != std::string::npos; at = head.find(key, at + 1))
        {
            const size_t open = head.rfind('<', at), close = head.find('>', at);
            if (open == std::string::npos || close == std::string::npos || head.compare(open, 5, "<meta") != 0)
                continue;
            const std::string url = CleanText(Attr(head.substr(open, close - open), "content"));
            if (WebImageUrl(url))
                return url;
        }
    return std::string();
}

// ---- Cache (%LOCALAPPDATA%\ILION\News\noticias.json) ------------------------------------------------------------------
inline std::filesystem::path CachePath() { return C::Root() / L"News" / L"noticias.json"; }

inline void SaveCache(const Snapshot& s)
{
    C::Json j { { "fetched", s.fetched }, { "items", C::Json::array() } };
    for (int b = 0; b < BlockCount; b++)
        for (const auto& it : s.blocks[b])
            j["items"].push_back({ { "title", it.title }, { "link", it.link }, { "source", it.source }, { "time", it.time }, { "block", it.block },
                                   { "rumor", it.rumor }, { "editorial", it.editorial }, { "pinned", it.pinned }, { "summary", it.summary },
                                   { "image", it.imageUrl } });
    // Never throws: a byte that is not UTF-8 would make dump() throw on the news thread
    C::WriteAtomic(CachePath(), j.dump(1, ' ', false, C::Json::error_handler_t::replace));
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
        it.summary = e.value("summary", std::string());
        it.imageUrl = e.value("image", std::string());
        it.image = LoadImage(it.imageUrl, false, {}); // only from the disk cache
        if (!it.title.empty() && it.link.rfind("http", 0) == 0 && s.blocks[it.block].size() < 3)
            s.blocks[it.block].push_back(it);
    }
    BuildRotation(s);
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
        it.summary = e.value("summary", std::string());
        it.imageUrl = e.value("image_url", std::string());
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
        Snapshot fresh;
        {
            std::lock_guard lock(mutex);
            snap.busy = true;
            fresh = snap;
        }
        // Everything below runs without the lock: the menu keeps showing the last good items meanwhile
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
        offline = ok == 0;

        if (ok > 0)
        {
            Select(all, fresh, now);
            fresh.fetched = now;
            // The pictures of the chosen items: the feed's, else the article's og:image / twitter:image; kept on disk
            std::vector<std::filesystem::path> keep;
            for (int b = 0; b < BlockCount; b++)
                for (auto& it : fresh.blocks[b])
                {
                    if (it.imageUrl.empty())
                    {
                        const C::HttpResult page = C::HttpRequest("GET", it.link, headers, "");
                        if (page.status == 200)
                            it.imageUrl = MetaImage(page.body);
                    }
                    it.image = LoadImage(it.imageUrl, true, headers);
                    if (it.image)
                        keep.push_back(ImagePath(it.imageUrl));
                }
            BuildRotation(fresh);
            std::error_code ec;
            for (const auto& f : std::filesystem::directory_iterator(ImageDir(), ec))
                if (std::find(keep.begin(), keep.end(), f.path()) == keep.end())
                    std::filesystem::remove(f.path(), ec);
            SaveCache(fresh);
        }

        std::lock_guard lock(mutex);
        if (ok > 0)
            snap = fresh;
        snap.busy = false;
        snap.sourcesOk = ok;
        snap.sourcesTried = tried;
        snap.editorial = editorial;
        snap.offline = offline;
    }

    std::atomic<uint64_t> alive { 0 };
    std::atomic<bool> started { false }, force { false };
    long long lastTry = 0;
    bool offline = false;
    std::mutex mutex;
    Snapshot snap;
};
} // namespace IlionNews

#pragma execution_character_set(pop)
