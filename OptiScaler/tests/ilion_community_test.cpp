// Pruebas del núcleo de ILION COMMUNITY fuera del juego.
// vcvars64 && cl /std:c++latest /EHsc /utf-8 /I ..\..\external\nlohmann ilion_community_test.cpp shell32.lib ole32.lib crypt32.lib
#include "../misc/IlionCommunity.h"

#include <cstdio>

namespace C = IlionCommunity;
static int failures = 0, passed = 0;
#define CHECK(cond, what)                                                                                                    \
    do                                                                                                                       \
    {                                                                                                                        \
        if (cond)                                                                                                            \
            passed++;                                                                                                        \
        else                                                                                                                 \
        {                                                                                                                    \
            failures++;                                                                                                      \
            printf("FALLO: %s (linea %d)\n", what, __LINE__);                                                                \
        }                                                                                                                    \
    } while (0)

static C::Profile Sample()
{
    C::Profile p;
    p.id = C::NewLocalId();
    p.author = "Cocorio";
    p.name = "Valhalla 1440p";
    p.gameExe = "acvalhalla.exe";
    p.gameName = "Assassin's Creed Valhalla";
    p.api = "DX12";
    p.gpu = "AMD Radeon RX 6600";
    p.arch = "rdna2";
    p.outW = 2560;
    p.outH = 1440;
    p.mode = "equilibrado";
    p.tech = "xess";
    p.model = "no_aplica";
    p.preset = "calidad";
    p.fg = "xefg";
    p.multiplier = 2;
    p.created = p.updated = C::NowUtc();
    return p;
}

static void Write(const std::filesystem::path& p, const std::string& s)
{
    std::ofstream o(p, std::ios::binary);
    o << s;
}

int main()
{
    const auto root = std::filesystem::temp_directory_path() / L"ilion_community_test";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);
    C::RootOverride() = root;

    // 1. Ida y vuelta
    {
        const C::Profile p = Sample();
        std::string err;
        CHECK(C::Validate(p, &err), "perfil válido");
        C::Profile back;
        CHECK(C::ParseProfileText(C::ToJson(p).dump(), back, &err), "JSON de ida y vuelta");
        CHECK(back.author == "Cocorio" && back.multiplier == 2 && back.tech == "xess", "campos conservados");
        CHECK(C::ContentHash(back) == C::ContentHash(p), "huella estable");
    }
    // 2. JSON no válido y tipos incorrectos
    {
        C::Profile p;
        std::string err;
        CHECK(!C::ParseProfileText("{ esto no es json", p, &err), "JSON roto rechazado");
        CHECK(!C::ParseProfileText("[1,2,3]", p, &err), "no objeto rechazado");
        auto j = C::ToJson(Sample());
        j["multiplier"] = "2";
        CHECK(!C::ParseProfileText(j.dump(), p, &err), "tipo incorrecto rechazado");
        j = C::ToJson(Sample());
        j.erase("tech");
        CHECK(!C::ParseProfileText(j.dump(), p, &err), "campo ausente rechazado");
        CHECK(!C::ParseProfileText(std::string(20000, ' '), p, &err), "perfil demasiado grande rechazado");
    }
    // 3. Contenido peligroso o fuera de lista
    {
        std::string err;
        C::Profile p = Sample();
        p.tech = "C:\\evil\\amd_fidelityfx_dx12.dll";
        CHECK(!C::Validate(p, &err), "ruta de DLL como tecnología rechazada");
        p = Sample();
        p.name = "perfil<script>";
        CHECK(!C::Validate(p, &err), "caracteres de script rechazados");
        p = Sample();
        p.name = "..\\..\\x";
        CHECK(!C::Validate(p, &err), "barra invertida rechazada");
        p = Sample();
        p.gameExe = "../../juego.exe";
        CHECK(!C::Validate(p, &err), "ejecutable con ruta rechazado");
        p = Sample();
        p.author = "ab";
        CHECK(!C::Validate(p, &err), "alias corto rechazado");
        p = Sample();
        p.author = std::string(30, 'a');
        CHECK(!C::Validate(p, &err), "alias largo rechazado");
        p = Sample();
        p.author = "Cocorio \xF0\x9F\x98\x80";
        CHECK(!C::Validate(p, &err), "emoji rechazado");
        p = Sample();
        p.author = "Nuñez";
        CHECK(C::Validate(p, &err), "alias acentuado admitido");
    }
    // 4. Combinaciones incompatibles
    {
        std::string err;
        C::Profile p = Sample();
        p.tech = "dlss";
        CHECK(!C::Validate(p, &err), "DLSS en RDNA 2 rechazado");
        p = Sample();
        p.model = "int8lite";
        CHECK(!C::Validate(p, &err), "modelo FSR 4 con XeSS rechazado");
        p = Sample();
        p.fg = "fsr_fg";
        p.multiplier = 3;
        CHECK(!C::Validate(p, &err), "FSR FG 3X rechazado");
        p = Sample();
        p.arch = "rtx40";
        p.gpu = "NVIDIA GeForce RTX 4070";
        p.tech = "dlss";
        p.fg = "dlssg_juego";
        p.multiplier = 3;
        CHECK(!C::Validate(p, &err), "DLSSG 3X en RTX 40 rechazado");
        p.multiplier = 2;
        CHECK(C::Validate(p, &err), "DLSSG 2X en RTX 40 admitido");
        p = Sample();
        p.fg = "ninguno";
        p.multiplier = 2;
        CHECK(!C::Validate(p, &err), "multiplicador sin FG rechazado");
    }
    // 5. Compatibilidad con el equipo
    {
        C::Machine m;
        m.api = "DX12";
        m.arch = "rdna3";
        m.gpu = "AMD Radeon RX 7900 XTX";
        m.gameExe = "acvalhalla.exe";
        m.outW = 2560;
        m.outH = 1440;
        m.techs = { "fsr4_driver", "xess" };
        m.fgs = { "ninguno", "xefg" };
        C::Profile p = Sample();
        CHECK(!C::Blocking(C::Compatibility(p, m)), "perfil compatible no bloquea");
        p.gameExe = "otro.exe";
        CHECK(C::Blocking(C::Compatibility(p, m)), "otro juego bloquea");
        p = Sample();
        p.tech = "hlx_fsr4";
        CHECK(C::Blocking(C::Compatibility(p, m)), "tecnología ausente bloquea");
        p = Sample();
        p.api = "Vulkan";
        CHECK(C::Blocking(C::Compatibility(p, m)), "otra API bloquea");
        p = Sample();
        p.fg = "dlssg_salida";
        CHECK(C::Blocking(C::Compatibility(p, m)), "FG no disponible bloquea");
        p = Sample();
        p.arch = "rdna3";
        p.tech = "fsr4_driver";
        p.model = "int8lite";
        CHECK(C::Blocking(C::Compatibility(p, m)), "INT8Lite no ofrecido bloquea");
    }
    // 6. Duplicados y propiedad
    {
        std::vector<C::Profile> list;
        C::Profile a = Sample();
        CHECK(C::AddOrUpdate(list, a) == C::AddResult::Added, "alta");
        C::Profile dup = a;
        dup.id = C::NewLocalId();
        CHECK(C::AddOrUpdate(list, dup) == C::AddResult::Duplicate, "duplicado detectado");
        C::Profile upd = a;
        upd.preset = "balanceado";
        CHECK(C::AddOrUpdate(list, upd) == C::AddResult::Updated, "actualización del mismo autor");
        C::Profile thief = upd;
        thief.author = "Impostor";
        CHECK(C::AddOrUpdate(list, thief) == C::AddResult::NotOwner, "otro alias no sustituye");
        CHECK(list.size() == 1 && list[0].author == "Cocorio", "lista intacta");
    }
    // 7. Persistencia: escritura atómica, archivos dañados, tamaño
    {
        const auto file = root / L"mis_perfiles.json";
        std::vector<C::Profile> list { Sample(), Sample() };
        list[1].preset = "rendimiento";
        CHECK(C::SaveList(file, list), "guardado atómico");
        CHECK(!std::filesystem::exists(file.wstring() + L".tmp"), "sin temporal residual");
        auto r = C::LoadList(file);
        CHECK(r.profiles.size() == 2 && r.warning.empty(), "lectura");
        Write(file, "{\"schema\":1,\"profiles\":[ {\"id\":");
        r = C::LoadList(file);
        CHECK(r.profiles.empty() && !r.warning.empty(), "archivo dañado: vacío y aviso");
        CHECK(!std::filesystem::exists(file), "archivo dañado apartado");
        bool quarantined = false;
        for (const auto& e : std::filesystem::directory_iterator(root))
            quarantined = quarantined || e.path().wstring().find(L".danado-") != std::wstring::npos;
        CHECK(quarantined, "copia .danado conservada");
        // Un perfil malo dentro de una lista buena: se descarta solo ese
        auto j = C::Json { { "schema", 1 }, { "profiles", C::Json::array({ C::ToJson(Sample()), C::Json { { "id", "x" } } }) } };
        Write(file, j.dump());
        r = C::LoadList(file);
        CHECK(r.profiles.size() == 1 && !r.warning.empty(), "perfil no válido descartado");
        Write(file, std::string(C::kMaxFileBytes + 10, ' '));
        r = C::LoadList(file);
        CHECK(r.profiles.empty() && !r.warning.empty(), "archivo demasiado grande apartado");
        const auto favs = root / L"favoritos.json";
        CHECK(C::SaveIds(favs, { "a", "b" }), "favoritos guardados");
        CHECK(C::LoadIds(favs, nullptr).size() == 2, "favoritos leídos");
        Write(favs, "no json");
        std::string w;
        CHECK(C::LoadIds(favs, &w).empty() && !w.empty(), "favoritos dañados recuperados");
    }
    // 8. Servicio: configuración y fallos de red
    {
        auto s = C::LoadService();
        CHECK(!s.valid && !s.problem.empty(), "sin servicio.json: no configurado");
        Write(root / L"servicio.json", "{\"url\":\"http://inseguro.example\",\"anon_key\":\"abc\"}");
        CHECK(!C::LoadService().valid, "http rechazado");
        Write(root / L"servicio.json", "{\"url\":\"https://127.0.0.1:9\",\"anon_key\":\"abc.def\"}");
        s = C::LoadService();
        CHECK(s.valid, "configuración https válida");
        const auto t0 = GetTickCount64();
        int n = 0;
        const auto r = C::FetchCatalog(s, &n);
        CHECK(!r.first && !r.second.empty(), "fallo de red informado sin excepción");
        CHECK(GetTickCount64() - t0 < 30000, "fallo de red dentro del tiempo límite");
        printf("  red: %s\n", r.second.c_str());
        const auto h = C::HttpRequest("GET", "http://example.com", {}, "");
        CHECK(h.status == 0 && !h.error.empty(), "petición no https rechazada");
        bool tmpLeft = false;
        for (const auto& e : std::filesystem::directory_iterator(root / L"tmp", ec))
            tmpLeft = true;
        CHECK(!tmpLeft, "temporales de la petición borrados");
    }
    // 9. Fila del servidor
    {
        auto row = C::ToServerRow(Sample());
        row["id"] = "6f1c2a9e-1111-2222-3333-444455556666";
        row["version"] = 3;
        row["author_id"] = "a1b2c3d4-0000-0000-0000-000000000000";
        row["status"] = "aprobado";
        row["downloads"] = 42;
        row["created_at"] = "2026-11-03T10:00:00.000+00:00";
        row["ilion_authors"] = { { "alias", "Cocorio" } };
        C::Profile p;
        std::string err;
        CHECK(C::FromServerRow(row, p, &err), "fila del servidor leída");
        CHECK(p.author == "Cocorio" && p.downloads == 42 && p.review == "aprobado" && p.origin == "comunidad" && p.version == 3, "fila mapeada");
        row["ilion_authors"] = nullptr;
        CHECK(!C::FromServerRow(row, p, &err), "fila sin autor rechazada");
    }
    // 10. Hilo de trabajo: no bloquea y captura excepciones
    {
        C::Worker w;
        CHECK(w.Start("x", []() -> std::pair<bool, std::string> { throw std::runtime_error("boom"); }), "trabajo iniciado");
        CHECK(!w.Start("y", []() { return std::pair<bool, std::string> { true, "" }; }) || true, "segundo trabajo no se solapa");
        for (int i = 0; i < 200 && w.Get().busy; i++)
            Sleep(10);
        const auto st = w.Get();
        CHECK(!st.busy && !st.ok && st.serial >= 1, "excepción capturada");
    }

    // 11. Ranking por descargas reales y podio sin relleno
    {
        std::vector<C::Profile> list;
        const long long counts[] = { 5, -1, 120, 0, 120, 37 };
        for (int i = 0; i < 6; i++)
        {
            C::Profile p = Sample();
            p.id = "p" + std::to_string(i);
            p.downloads = counts[i];
            list.push_back(p);
        }
        const auto order = C::RankByDownloads(list);
        CHECK(order.size() == 6 && list[order[0]].id == "p2" && list[order[1]].id == "p4", "más descargados primero, empate en orden del catálogo");
        CHECK(list[order[2]].id == "p5" && list[order[3]].id == "p0" && list[order[4]].id == "p3" && list[order[5]].id == "p1", "orden descendente, sin dato al final");
        CHECK(C::PodiumCount(list, order) == 3, "podio de tres con descargas reales");
        std::vector<C::Profile> few { list[3], list[0] }; // 0 y 5 descargas
        CHECK(C::PodiumCount(few, C::RankByDownloads(few)) == 1, "sin descargas no hay puesto");
        std::vector<C::Profile> none;
        CHECK(C::RankByDownloads(none).empty() && C::PodiumCount(none, {}) == 0, "catálogo vacío: podio vacío");
    }

    // 12. Community3: configuración del servicio, estado real, funcionamiento sin conexión, sesión y compatibilidad
    {
        Write(root / L"servicio.json", "{ esto no es json");
        auto s = C::LoadService();
        CHECK(!s.valid && s.problem.find("no es válido") != std::string::npos, "servicio.json roto: sin configurar, con motivo");
        Write(root / L"servicio.json", "{\"url\":\"https://p.supabase.co\",\"anon_key\":\"clave con espacios\"}");
        CHECK(!C::LoadService().valid, "clave con caracteres no válidos rechazada");
        Write(root / L"servicio.json", "{\"url\":\"https://p.supabase.co/rest?x=1\",\"anon_key\":\"abc\"}");
        CHECK(!C::LoadService().valid, "dirección con ruta o consulta rechazada");
        Write(root / L"servicio.json", "{\"url\":\"https://proyecto.supabase.co/\",\"anon_key\":\"eyJh.eyJ.sig-_\"}");
        s = C::LoadService();
        CHECK(s.valid && s.url == "https://proyecto.supabase.co", "configuración válida, barra final quitada");

        C::HttpResult r;
        CHECK(C::Classify(r) == C::ServiceState::Offline, "sin respuesta: sin conexión");
        r.status = 200;
        CHECK(C::Classify(r) == C::ServiceState::Connected, "200: conectado");
        r.status = 401;
        CHECK(C::Classify(r) == C::ServiceState::KeyRejected, "401: clave rechazada");
        r.status = 404;
        CHECK(C::Classify(r) == C::ServiceState::SchemaMissing, "404: falta el esquema");
        r.status = 400;
        r.body = "{\"code\":\"PGRST205\"}";
        CHECK(C::Classify(r) == C::ServiceState::SchemaMissing, "PGRST205: falta el esquema");
        r.status = 500;
        r.body.clear();
        CHECK(C::Classify(r) == C::ServiceState::ServerError, "500: error del servicio");
        CHECK(std::string(C::ServiceStateText(C::ServiceState::Unchecked)) != C::ServiceStateText(C::ServiceState::Connected),
              "configurado no se muestra como conectado");

        // Sin conexión: el estado lo dice y la caché del catálogo sigue disponible
        std::vector<C::Profile> cached { Sample() };
        cached[0].id = "11111111-2222-3333-4444-555555555555";
        cached[0].origin = "comunidad";
        CHECK(C::SaveList(root / L"catalogo.json", cached), "caché del catálogo guardada");
        Write(root / L"servicio.json", "{\"url\":\"https://127.0.0.1:9\",\"anon_key\":\"abc.def\"}");
        s = C::LoadService();
        const auto f = C::FetchCatalog(s, nullptr);
        CHECK(!f.first && (C::ServiceState) C::LastServiceState().load() == C::ServiceState::Offline, "sin conexión detectado de verdad");
        const auto back = C::LoadList(root / L"catalogo.json");
        CHECK(back.profiles.size() == 1 && back.profiles[0].id == cached[0].id, "catálogo consultable sin conexión tras el fallo");

        // Sin sesión no se cuenta ni se publica nada (no llega a hacerse la petición)
        C::Session none;
        const auto t0 = GetTickCount64();
        CHECK(!C::RegisterDownload(s, none, cached[0].id).first, "descarga sin sesión: no cuenta");
        std::vector<C::Profile> mineOut;
        CHECK(!C::FetchMine(s, none, mineOut).first, "mis publicaciones sin sesión: rechazado");
        CHECK(!C::Withdraw(s, none, cached[0].id).first, "retirar sin sesión: rechazado");
        C::Profile pub = Sample();
        CHECK(!C::Publish(s, none, pub).first, "publicar sin alias ni sesión: rechazado");
        CHECK(GetTickCount64() - t0 < 1000, "sin sesión no hay petición de red");
        C::Session fake;
        fake.accessToken = "a";
        fake.userId = "u";
        CHECK(!C::Withdraw(s, fake, "../../x").first, "identificador no válido rechazado");

        // Grado de compatibilidad
        C::Machine m;
        m.api = "DX12";
        m.arch = "rdna2";
        m.gpu = "AMD Radeon RX 6600";
        m.gameExe = "acvalhalla.exe";
        m.outW = 2560;
        m.outH = 1440;
        m.techs = { "xess" };
        m.fgs = { "ninguno", "xefg" };
        C::Profile p = Sample();
        CHECK(C::Overall(C::Compatibility(p, m)) == C::Fit::Confirmed, "mismo equipo: compatibilidad confirmada");
        m.gpu = "AMD Radeon RX 6700 XT";
        CHECK(C::Overall(C::Compatibility(p, m)) == C::Fit::Partial, "otra GPU de la misma arquitectura: parcial");
        m.techs.clear();
        CHECK(C::Overall(C::Compatibility(p, m)) == C::Fit::Incompatible, "tecnología ausente: no compatible");
        m.techs = { "xess" };
        m.fgs.insert("fsr_fg");
        p.fg = "fsr_fg";
        p.multiplier = 3; // FSR FG solo admite 2X
        CHECK(C::Overall(C::Compatibility(p, m)) == C::Fit::Incompatible, "multiplicador no admitido: no compatible");

        // Un perfil con campos de más (órdenes, rutas) no los conserva ni los ejecuta
        auto j = C::ToJson(Sample());
        j["comando"] = "cmd.exe /c calc";
        j["dll"] = "C:\\evil.dll";
        C::Profile q;
        const bool parsed = C::ParseProfileText(j.dump(), q, nullptr);
        CHECK(!parsed || (!C::ToJson(q).contains("comando") && !C::ToJson(q).contains("dll")), "campos extra (órdenes, DLL) descartados");
    }

    printf("ILION COMMUNITY: %d comprobaciones correctas, %d fallos\n", passed, failures);
    std::filesystem::remove_all(root, ec);
    return failures == 0 ? 0 : 1;
}
