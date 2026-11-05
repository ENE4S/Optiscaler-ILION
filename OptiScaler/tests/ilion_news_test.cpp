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

    // v1.4.0: resumen, imagen del elemento (feed u og:image), validación y recorte centrado de una imagen real
    {
        const N::Feed es { "Prueba ES", "https://example.com/es", N::Tech };
        const std::string x = R"(<rss><channel>
<item><title>Nueva GPU anunciada hoy</title><link>https://example.com/n1</link><pubDate>Wed, 04 Nov 2026 21:00:00 +0000</pubDate>
<description>&lt;img src=&quot;https://cdn.example.com/a.jpg&quot; /&gt;Nueva GPU anunciada hoy. La compañía ha presentado su tarjeta con 16 GB de memoria y un consumo de 250 W. Llegará a las tiendas en diciembre. The post Nueva GPU appeared first on Ejemplo.</description></item>
<item><title>Con media content</title><link>https://example.com/n2</link><pubDate>Wed, 04 Nov 2026 20:00:00 +0000</pubDate>
<media:content url="https://cdn.example.com/b.png" medium="image" /><description>Texto breve de la noticia con suficientes caracteres.</description></item>
<item><title>Enclosure de video</title><link>https://example.com/n3</link><pubDate>Wed, 04 Nov 2026 20:00:00 +0000</pubDate>
<enclosure url="https://cdn.example.com/v.mp4" type="video/mp4" /></item>
</channel></rss>)";
        const auto it = N::ParseFeed(x, es);
        CHECK(it.size() == 3 && it[0].imageUrl == "https://cdn.example.com/a.jpg", "imagen escapada dentro de la descripción");
        CHECK(it.size() == 3 && it[0].summary.rfind("La compañía", 0) == 0 && it[0].summary.find("<img") == std::string::npos &&
                  it[0].summary.find("The post") == std::string::npos,
              "resumen sin repetir el titular, sin etiquetas ni coletillas");
        CHECK(it.size() == 3 && it[1].imageUrl == "https://cdn.example.com/b.png" && it[2].imageUrl.empty(), "media:content sí, vídeo no");
        CHECK(N::MetaImage(R"(<html><head><meta property="og:image" content="https://cdn.example.com/og.jpg"></head>)") == "https://cdn.example.com/og.jpg" &&
                  N::MetaImage(R"(<meta name="twitter:image" content="https://cdn.example.com/tw.jpg">)") == "https://cdn.example.com/tw.jpg" &&
                  N::MetaImage(R"(<meta property="og:image" content="javascript:x">)").empty(),
              "og:image, twitter:image, y nada que no sea https");
        CHECK(!N::LooksLikeImage("<html>no soy una imagen</html>") && N::LooksLikeImage(std::string("\xFF\xD8\xFF\xE0", 4)), "tipo por los primeros bytes");
        // Una BMP real de 400x100 en memoria: se decodifica y se recorta al centro a 320x180 sin deformarla
        const int w = 400, h = 100;
        std::string bmp(54 + w * h * 3, '\0');
        auto put = [&](size_t at, uint32_t v, int n) { for (int k = 0; k < n; k++) bmp[at + k] = (char) ((v >> (8 * k)) & 0xFF); };
        bmp[0] = 'B', bmp[1] = 'M';
        put(2, (uint32_t) bmp.size(), 4), put(10, 54, 4), put(14, 40, 4), put(18, w, 4), put(22, h, 4), put(26, 1, 2), put(28, 24, 2);
        for (int yy = 0; yy < h; yy++)
            for (int xx = 0; xx < w; xx++)
            {
                const size_t at = 54 + ((size_t) yy * w + xx) * 3;
                bmp[at + 2] = (char) (xx < w / 2 ? 255 : 0); // izquierda roja, derecha negra
            }
        const auto px = N::DecodeCover(bmp);
        CHECK(px && px->size() == (size_t) N::kImageW * N::kImageH * 4, "decodificada a 320x180 RGBA");
        CHECK(px && (*px)[(90 * N::kImageW + 100) * 4] > 200 && (*px)[(90 * N::kImageW + 220) * 4] < 40 && (*px)[(90 * N::kImageW + 100) * 4 + 3] == 255,
              "recorte centrado: rojo a la izquierda del centro, negro a la derecha");
        CHECK(!N::DecodeCover(std::string(2000, 'x')), "lo que no es imagen no se decodifica");
    }

    // Codificación: entidades con nombre y numéricas, UTF-8 directo, bytes no válidos
    {
        CHECK(N::CleanText("Acci&oacute;n, Espa&ntilde;a, ping&uuml;ino, &iquest;Qu&eacute;? &iexcl;S&iacute;! &Ntilde;u, &Aacute;lvaro, M&uacute;sica") ==
                  "Acción, España, pingüino, ¿Qué? ¡Sí! Ñu, Álvaro, Música",
              "entidades con nombre de Latin-1 (á é í ó ú ñ Ñ ü ¿ ¡)");
        CHECK(N::CleanText("Rock&rsquo;s &ldquo;GTA VI&rdquo; &hellip; 2026 &ndash; 50&euro; &copy; &deg;") == "Rock’s “GTA VI” … 2026 – 50€ © °",
              "tipografía: comillas, puntos suspensivos, rayas, euro, grados");
        CHECK(N::CleanText("Pok&#233;mon &#xF1; &#8217; Bj&ouml;rk Dvo&#345;&#225;k") == "Pokémon ñ ’ Björk Dvořák", "entidades numéricas y nombres internacionales");
        CHECK(N::CleanText("Ya está aquí: ñandú, ¿cuándo? ¡ahora! Zoë · Łódź") == "Ya está aquí: ñandú, ¿cuándo? ¡ahora! Zoë · Łódź", "UTF-8 directo, intacto");
        CHECK(N::CleanText("Bien\xC3 roto \xFF fin") == "Bien\xEF\xBF\xBD roto \xEF\xBF\xBD fin", "bytes no válidos: U+FFFD, nunca texto roto");
        CHECK(N::CleanText("a&nbsp;b&amp;oacute;") == "a b&oacute;" && N::CleanText(N::CleanText("a&amp;oacute;")) == "aó",
              "nbsp, y descripción doblemente escapada");
        const N::Feed es { "Prueba", "https://example.com/es", N::Games };
        const auto it = N::ParseFeed(R"(<rss><item><title>El regreso de &lt;b&gt;Pok&eacute;mon&lt;/b&gt;: &iquest;listo?</title><link>https://example.com/x</link><pubDate>Wed, 04 Nov 2026 21:00:00 +0000</pubDate><description>&lt;p&gt;Nintendo confirm&oacute; la fecha del pr&oacute;ximo juego de la saga en Espa&ntilde;a.&lt;/p&gt;</description></item></rss>)", es);
        CHECK(it.size() == 1 && it[0].title == "El regreso de Pokémon: ¿listo?" && it[0].summary == "Nintendo confirmó la fecha del próximo juego de la saga en España.",
              "titular y resumen de un feed con entidades y HTML escapado");
        CHECK(N::CleanText("Rendimiento: A < B y C > D") == "Rendimiento: A < B y C > D", "un «<» suelto es texto, no una etiqueta");
        CHECK(std::string(N::BlockName(N::Tech)) == "TECNOLOGÍA" && std::string(N::kFeeds[1].source) == "IGN España",
              "los textos fijos del código salen en UTF-8 (sin doble codificación)");
    }

    printf("ILION AL DIA: %d comprobaciones, %d fallos\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
