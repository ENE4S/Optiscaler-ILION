// ILION AL DÍA: offline checks of the feed reader (no network)
#include "misc/IlionNews.h"
#include <cstdio>

static int failures = 0, checks = 0;
#define CHECK(c, what) do { checks++; if (!(c)) { failures++; printf("FALLO: %s\n", what); } } while (0)

int main()
{
    namespace N = IlionNews;
    const N::Feed rss { "Prueba", "https://example.com/rss", N::Games };
    const std::string xml = R"(<rss><channel><title>Canal</title>
<item><title><![CDATA[GTA VI: Rockstar &amp; Take-Two confirm the date]]></title><link>https://example.com/a</link><pubDate>Wed, 04 Nov 2026 21:47:40 +0000</pubDate></item>
<item><title>Big sale: 75% off &#8211; this week</title><link>https://example.com/b</link><pubDate>Mon, 26 Oct 2026 10:00:00 +0000</pubDate></item>
<item><title>Leak suggests a new handheld</title><link>https://example.com/c</link><pubDate>Wed, 04 Nov 2026 20:00:00 +0000</pubDate></item>
<item><title>No link here</title><pubDate>Wed, 04 Nov 2026 20:00:00 +0000</pubDate></item>
<item><title>javascript link</title><link>javascript:alert(1)</link><pubDate>Wed, 04 Nov 2026 20:00:00 +0000</pubDate></item>
</channel></rss>)";
    const auto items = N::ParseFeed(xml, rss);
    CHECK(items.size() == 3, "solo los elementos con título, enlace web y fecha");
    CHECK(items.size() > 0 && items[0].title == "GTA VI: Rockstar & Take-Two confirm the date", "CDATA y entidades");
    CHECK(items.size() > 1 && items[1].title.find("\xE2\x80\x93") != std::string::npos, "entidad numérica a UTF-8");
    CHECK(items.size() > 2 && items[2].rumor && !items[0].rumor, "rumores marcados");
    CHECK(N::ParseDate("Wed, 04 Nov 2026 21:47:40 +0000") == N::ParseDate("2026-11-04T22:47:40+01:00") && N::ParseDate("basura") == 0, "fechas RFC 822 e ISO 8601");

    const std::string atom = R"(<feed><entry><title>Atom item title</title><link href="https://example.com/atom"/><updated>2026-11-04T10:00:00Z</updated></entry></feed>)";
    const auto at = N::ParseFeed(atom, rss);
    CHECK(at.size() == 1 && at[0].link == "https://example.com/atom", "Atom");

    // Selección: tres por bloque, sin duplicados, sin titulares viejos, ofertas solo frescas, una por fuente primero
    const long long now = N::ParseDate("Thu, 05 Nov 2026 01:00:00 +0000");
    std::vector<N::Item> all;
    const auto add = [&](const char* t, const char* src, int block, long long ageH)
    { N::Item i; i.title = t; i.link = std::string("https://example.com/") + t; i.source = src; i.block = block; i.time = now - ageH * 3600; all.push_back(i); };
    add("Same news told by two", "A", N::Games, 1);
    add("Same news told by two", "B", N::Games, 2);
    add("Second games headline", "A", N::Games, 3);
    add("Old games headline", "C", N::Games, 24 * 5);
    add("Week old deal 50% off", "C", N::Games, 60);
    add("Third games headline", "D", N::Games, 4);
    add("Tech one", "T", N::Tech, 1);
    add("Mod six days", "M", N::Mods, 24 * 6);
    N::Snapshot s;
    N::Select(all, s, now);
    CHECK(s.blocks[N::Games].size() == 3, "tres en videojuegos");
    bool dup = false, old = false, deal = false;
    for (const auto& i : s.blocks[N::Games])
        dup = dup || i.source == "B", old = old || i.title == "Old games headline", deal = deal || i.title.find("deal") != std::string::npos;
    CHECK(!dup && !old && !deal, "sin duplicados, sin viejos ni ofertas caducadas");
    CHECK(s.blocks[N::Games][0].source != s.blocks[N::Games][1].source, "una por fuente antes de repetir");
    CHECK(s.blocks[N::Tech].size() == 1 && s.blocks[N::Mods].size() == 1, "si faltan noticias no se inventan; mods hasta 7 días");

    printf("ILION AL DIA: %d comprobaciones, %d fallos\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
