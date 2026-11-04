// ILION COMMUNITY — prueba de integración contra el servicio Supabase real (solo lectura y operaciones rechazadas).
// No crea usuarios, perfiles ni descargas: comprueba la conexión, el catálogo público y que lo prohibido se rechaza.
// Uso: ilion_community_live_test.exe <ruta a servicio.json>
// Compilar: cl /std:c++latest /EHsc /utf-8 /I external\nlohmann /I OptiScaler OptiScaler\tests\ilion_community_live_test.cpp shell32.lib ole32.lib crypt32.lib

#include "misc/IlionCommunity.h"

#include <cstdio>

namespace C = IlionCommunity;

static int passed = 0, failures = 0;
#define CHECK(cond, what)                                                                                                  \
    do                                                                                                                     \
    {                                                                                                                      \
        if (cond)                                                                                                          \
            passed++;                                                                                                      \
        else                                                                                                               \
        {                                                                                                                  \
            failures++;                                                                                                    \
            printf("FALLO: %s\n", what);                                                                                   \
        }                                                                                                                  \
    } while (0)

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        printf("Uso: ilion_community_live_test <servicio.json>\n");
        return 2;
    }
    // Carpeta temporal: la caché del catálogo no toca los datos reales del usuario
    const auto root = std::filesystem::temp_directory_path() / L"ilion_community_live";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);
    std::filesystem::copy_file(argv[1], root / L"servicio.json", ec);
    C::RootOverride() = root;

    const C::ServiceConfig s = C::LoadService();
    CHECK(s.valid, "servicio.json válido");
    printf("Servicio: %s\n", s.url.c_str());

    // 1. Catálogo público sin cuenta
    int count = -1;
    const auto f = C::FetchCatalog(s, &count);
    printf("  catálogo: %s\n", f.second.c_str());
    CHECK(f.first && (C::ServiceState) C::LastServiceState().load() == C::ServiceState::Connected, "conectado de verdad (catálogo público)");
    CHECK(count >= 0, "catálogo leído");

    // 2. Sin cuenta no se publica
    C::Profile p;
    p.author = "Anonimo";
    p.name = "Prueba anonima";
    p.gameExe = "dawnwalker.exe";
    p.gameName = "Dawnwalker";
    p.api = "DX12";
    p.gpu = "AMD Radeon RX 7900 XTX";
    p.arch = "rdna3";
    p.outW = 2560;
    p.outH = 1440;
    p.mode = "optimo";
    p.tech = "fsr4_driver";
    p.model = "completo";
    p.preset = "calidad";
    p.fg = "xefg";
    p.multiplier = 2;
    auto row = C::ToServerRow(p);
    row["author_id"] = "00000000-0000-0000-0000-000000000000";
    const auto ins = C::HttpRequest("POST", s.url + "/rest/v1/ilion_profiles", C::Headers(s, ""), row.dump());
    printf("  publicar sin cuenta: %d\n", ins.status);
    // 401/403 por permisos; 400 si el servidor aún no tiene el esquema v2 (columna fg_ruta): rechazado igualmente
    CHECK(ins.status >= 400 && ins.status < 500, "publicar sin cuenta rechazado por el servidor");

    // 3. Sin cuenta no se cuenta una descarga ni se cambia el contador
    const auto rpc = C::HttpRequest("POST", s.url + "/rest/v1/rpc/ilion_register_download", C::Headers(s, ""),
                                    C::Json { { "pid", "00000000-0000-0000-0000-000000000000" } }.dump());
    printf("  contador sin cuenta: %d\n", rpc.status);
    CHECK(rpc.status == 401 || rpc.status == 403 || rpc.status == 404, "contador sin cuenta rechazado");
    const auto patch = C::HttpRequest("PATCH", s.url + "/rest/v1/ilion_profiles?id=not.is.null", C::Headers(s, ""), "{\"downloads\":9999}");
    printf("  cambiar contador sin cuenta: %d\n", patch.status);
    CHECK(patch.status == 401 || patch.status == 403, "cambiar el contador sin cuenta rechazado");
    const auto dl = C::HttpRequest("GET", s.url + "/rest/v1/ilion_downloads?select=*", C::Headers(s, "", false), "");
    printf("  leer descargas: %d\n", dl.status);
    CHECK(dl.status == 401 || dl.status == 403, "tabla de descargas no legible");

    // 4. Estados del servicio con respuestas reales
    C::ServiceConfig bad = s;
    bad.anonKey = "clave.no.valida";
    CHECK(C::Classify(C::HttpRequest("GET", C::CatalogUrl(bad), C::Headers(bad, "", false), "")) == C::ServiceState::KeyRejected,
          "clave pública falsa: clave rechazada");
    CHECK(C::Classify(C::HttpRequest("GET", s.url + "/rest/v1/tabla_que_no_existe?select=*", C::Headers(s, "", false), "")) ==
              C::ServiceState::SchemaMissing,
          "tabla ausente: falta el esquema");
    const auto stats = C::HttpRequest("GET", s.url + "/rest/v1/ilion_stats?select=*", C::Headers(s, "", false), "");
    printf("  estadísticas públicas: %d %s\n", stats.status, stats.body.substr(0, 80).c_str());
    CHECK(stats.status == 200, "estadísticas públicas legibles");

    printf("ILION COMMUNITY (servicio real): %d comprobaciones correctas, %d fallos\n", passed, failures);
    std::filesystem::remove_all(root, ec);
    return failures == 0 ? 0 : 1;
}
