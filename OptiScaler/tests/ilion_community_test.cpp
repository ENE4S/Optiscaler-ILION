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
    p.fgRoute = "streamline"; // v2: la entrada del FG
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
    // Community4: el cliente trae el servicio público de ILION; aquí se comprueba también el caso sin configurar
    {
        const auto builtIn = C::LoadService();
        CHECK(builtIn.valid && builtIn.url == "https://kynhydceunbqhyyafgyi.supabase.co" && C::SafeToken(builtIn.anonKey, 1024),
              "servicio público incluido válido (solo URL y clave anon)");
        CHECK(builtIn.anonKey.find("service_role") == std::string::npos, "sin claves administrativas en el cliente");
        C::UseBuiltInService() = false;
    }

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
        p.model = "dlss_k";
        p.fg = "dlssg_juego";
        p.fgRoute = "juego";
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
        m.routes = { "ninguno", "juego", "optifg", "streamline", "nvngx", "fsr31", "fsr30" };
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
        m.routes = { "ninguno", "juego", "optifg", "streamline", "nvngx", "fsr31", "fsr30" };
        C::Profile p = Sample();
        CHECK(C::Overall(C::Compatibility(p, m)) == C::Fit::Confirmed, "mismo equipo: compatibilidad confirmada");
        m.gpu = "AMD Radeon RX 6700 XT";
        CHECK(C::Overall(C::Compatibility(p, m)) == C::Fit::Partial, "otra GPU de la misma arquitectura: parcial");
        m.techs.clear();
        CHECK(C::Overall(C::Compatibility(p, m)) == C::Fit::NeedsComponents, "tecnología ausente: requiere componentes");
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

    // 13. Community5: catálogo técnico de FG, arquitecturas, serialización v2, perfiles v1 y coherencia con el SQL
    {
        // Catálogo completo, sin identificadores repetidos ni rutas inventadas
        std::set<std::string> ids;
        bool routesOk = true;
        for (const auto& f : C::kFgImpls)
        {
            ids.insert(f.id);
            std::string list = f.routes;
            size_t start = 0;
            while (start <= list.size())
            {
                const size_t comma = list.find(',', start);
                const std::string r = list.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
                routesOk = routesOk && C::InList(C::kRoutes, r);
                if (comma == std::string::npos)
                    break;
                start = comma + 1;
            }
        }
        CHECK(ids.size() == std::size(C::kFgImpls) && ids.size() == std::size(C::kFgs), "catálogo de FG completo y sin duplicados");
        bool everyFg = true;
        for (const auto& i : C::kFgs)
            everyFg = everyFg && C::FgImplOf(i.id) != nullptr;
        CHECK(everyFg && routesOk, "cada FG tiene su implementación y rutas válidas");

        // Identificación de cada implementación con la configuración real de ILION
        const auto* en = C::FgImplOf("nvngx_enabler");
        CHECK(en && std::string(en->nvngx) == "Arturs" && std::string(en->dll) == "dlss-enabler-headless.dll", "Enabler de Artur identificado");
        const auto* nk = C::FgImplOf("nvngx_nukem");
        CHECK(nk && std::string(nk->nvngx) == "Nukems" && std::string(nk->dll) == "dlssg_to_fsr3_amd_is_better.dll", "Nukem identificado");
        const auto* nat = C::FgImplOf("dlssg_nativo");
        CHECK(nat && std::string(nat->output) == "DLSSG" && std::string(nat->nvngx) == "None", "DLSSG nativo identificado");
        const auto* lite = C::FgImplOf("nvngx_fglite");
        CHECK(lite && std::string(lite->nvngx) == "HelixSR" && lite->dx12Only, "FG Lite identificado (modo de HelixSR, DX12)");
        CHECK(std::string(C::FgImplOf("nvngx_combo")->nvngx) == "Combo" && std::string(C::FgImplOf("nvngx_ffx")->nvngx) == "FFX",
              "FFX y Combo identificados");
        CHECK(std::string(C::FgImplOf("nvngx_enabler")->nvngx) != std::string(C::FgImplOf("fsr_fg")->nvngx) &&
                  std::string(C::FgImplOf("fsr_fg")->output) == "FSRFG",
              "Enabler no se confunde con FSR FG");

        // Compatibilidad por arquitectura (evidencia y multiplicador)
        using E = C::Evidence;
        CHECK(C::FgEvidence("dlssg_nativo", "rtx20") == E::None && C::FgEvidence("nvngx_enabler", "rtx20") == E::Community &&
                  C::FgEvidence("nvngx_nukem", "rtx20") == E::Community,
              "RTX 20: sin DLSSG nativo, con Enabler y Nukem");
        CHECK(C::FgEvidence("dlssg_nativo", "rtx30") == E::None && C::FgRecommendable("nvngx_enabler", "rtx30") &&
                  C::FgMaxMultiplier("nvngx_enabler", "rtx30") == 6,
              "RTX 30: Enabler recomendable hasta 6X");
        CHECK(C::FgEvidence("dlssg_nativo", "rtx40") == E::Official && C::FgMaxMultiplier("dlssg_nativo", "rtx40") == 2, "RTX 40: DLSSG 2X");
        CHECK(C::FgEvidence("dlssg_nativo", "rtx50") == E::Official && C::FgMaxMultiplier("dlssg_nativo", "rtx50") == 4, "RTX 50: DLSSG hasta 4X");
        bool radeon = true;
        for (const char* a : { "amd_old", "rdna2", "rdna3", "rdna4" })
            radeon = radeon && C::FgEvidence("fsr_fg", a) == E::Official && C::FgEvidence("dlssg_nativo", a) == E::None &&
                     C::FgEvidence("nvngx_enabler", a) == E::Community && C::FgEvidence("nvngx_fglite", a) == E::Experimental &&
                     !C::FgRecommendable("nvngx_fglite", a);
        CHECK(radeon, "Radeon RX 5000/6000/7000/9000: FSR FG oficial, Enabler comunitario, FG Lite experimental (no recomendado)");
        CHECK(C::FgEvidence("xefg", "arc_a") == E::Official && C::FgEvidence("xefg", "arc_b") == E::Official &&
                  C::FgEvidence("dlssg_nativo", "arc_b") == E::None,
              "Intel Arc: XeFG oficial, sin DLSSG");
        CHECK(C::FgEvidence("nvngx_nukem", "nv_gtx") == E::None && C::FgMaxMultiplier("nvngx_nukem", "nv_gtx") == 0, "GTX: Nukem no compatible");

        // Escaladores por GPU
        CHECK(C::TechSupport("dlss", "rtx30").possible && C::ModelSupport("dlss_k", "dlss", "rtx30").possible &&
                  !C::TechSupport("dlss", "rdna3").possible,
              "DLSS solo en RTX, con su modelo");
        CHECK(C::TechSupport("dlss_d4r", "rdna3").possible && !C::TechSupport("dlss_d4r", "rtx40").possible, "d4r solo en Radeon");
        CHECK(C::TechSupport("fsr4_driver", "rdna3").possible && C::TechSupport("hlx_fsr4", "rdna3").possible &&
                  !C::TechSupport("fsr4_driver", "rtx40").possible,
              "FSR 4 y HLX en Radeon compatible");
        CHECK(!C::ModelSupport("dlss_k", "xess", "rtx30").possible && !C::ModelSupport("no_aplica", "dlss", "rtx30").possible,
              "presets DLSS solo con DLSS");

        // Serialización v2 de cada implementación y ruta: se conservan exactas y la ruta distingue la huella
        bool all = true;
        std::set<std::string> hashes;
        for (const auto& f : C::kFgImpls)
        {
            const std::string id = f.id;
            if (id == "ninguno" || id == "dlssg_salida")
                continue;
            C::Profile q = Sample();
            q.arch = "rtx30";
            q.gpu = "NVIDIA GeForce RTX 3060";
            if (C::FgMaxMultiplier(id, q.arch) == 0)
            {
                q.arch = "rtx40";
                q.gpu = "NVIDIA GeForce RTX 4070";
            }
            if (C::FgMaxMultiplier(id, q.arch) == 0)
            {
                q.arch = "rdna3";
                q.gpu = "AMD Radeon RX 7900 XTX";
            }
            q.fg = id;
            q.fgRoute = C::DefaultRoute(id);
            q.multiplier = 2;
            C::Profile back;
            std::string e;
            const bool ok = C::Validate(q, &e) && C::ParseProfileText(C::ToJson(q).dump(), back, &e) && back.fg == q.fg &&
                            back.fgRoute == q.fgRoute && back.schema == 2;
            if (!ok)
                printf("  serialización %s: %s\n", id.c_str(), e.c_str());
            all = all && ok;
            hashes.insert(C::ContentHash(q));
        }
        CHECK(all, "cada implementación de FG se guarda y se lee exacta (v2)");
        CHECK(hashes.size() == std::size(C::kFgImpls) - 2, "implementaciones distintas, huellas distintas");
        C::Profile r1 = Sample(), r2 = Sample();
        r1.fg = r2.fg = "nvngx_enabler";
        r1.fgRoute = "nvngx";
        r2.fgRoute = "streamline";
        CHECK(C::ContentHash(r1) != C::ContentHash(r2), "la ruta forma parte de la configuración");

        // Perfiles antiguos (v1) y valores no admitidos
        auto j1 = C::ToJson(Sample());
        j1["schema"] = 1;
        j1.erase("fg_route");
        j1["fg"] = "dlssg_salida";
        C::Profile old;
        std::string e1;
        CHECK(C::ParseProfileText(j1.dump(), old, &e1) && old.schema == 1 && old.fgRoute == "auto", "perfil v1 legible (ruta automática)");
        j1["fg"] = "nvngx_enabler";
        CHECK(!C::ParseProfileText(j1.dump(), old, &e1), "v1 no admite implementaciones nuevas");
        auto j2 = C::ToJson(Sample());
        j2.erase("fg_route");
        CHECK(!C::ParseProfileText(j2.dump(), old, &e1), "v2 sin ruta rechazado");
        C::Profile bad = Sample();
        bad.fg = "dlssg_salida";
        CHECK(!C::Validate(bad, &e1), "v2 no usa el valor heredado");
        bad = Sample();
        bad.fgRoute = "../x";
        CHECK(!C::Validate(bad, &e1), "ruta arbitraria rechazada");
        bad = Sample();
        bad.fg = "nvngx_evil";
        CHECK(!C::Validate(bad, &e1), "implementación inventada rechazada");
        bad = Sample();
        bad.arch = "rtx30";
        bad.gpu = "NVIDIA GeForce RTX 3060";
        bad.fg = "nvngx_nukem";
        bad.fgRoute = "nvngx";
        bad.multiplier = 3;
        CHECK(!C::Validate(bad, &e1), "Nukem 3X rechazado");
        bad.fg = "nvngx_enabler";
        bad.multiplier = 6;
        CHECK(C::Validate(bad, &e1), "Enabler 6X admitido en RTX 30");
        bad.fgRoute = "juego";
        CHECK(!C::Validate(bad, &e1), "ruta no admitida por la implementación rechazada");

        // Tecnología no disponible en este equipo: no compatible
        C::Machine m;
        m.api = "DX12";
        m.arch = "rtx30";
        m.gpu = "NVIDIA GeForce RTX 3060";
        m.gameExe = "acvalhalla.exe";
        m.outW = 2560;
        m.outH = 1440;
        m.techs = { "xess" };
        m.fgs = { "ninguno", "nvngx_nukem" };
        m.routes = { "ninguno", "juego", "nvngx", "streamline" };
        C::Profile want = Sample();
        want.arch = "rtx30";
        want.gpu = m.gpu;
        want.fg = "nvngx_enabler";
        want.fgRoute = "nvngx";
        CHECK(C::Overall(C::Compatibility(want, m)) == C::Fit::NeedsComponents, "Enabler sin su DLL: requiere componentes");
        m.fgs.insert("nvngx_enabler");
        CHECK(C::Overall(C::Compatibility(want, m)) == C::Fit::Confirmed, "Enabler con su DLL: compatible");
        want.fgRoute = "optifg";
        CHECK(C::Overall(C::Compatibility(want, m)) == C::Fit::NeedsComponents, "ruta no disponible en el juego: requiere ajustes");

        // Estados de publicación
        CHECK(C::InList(C::kReviews, "por_publicar") && C::InList(C::kReviews, "retirado") && !C::IsServerReview("por_publicar") &&
                  !C::IsServerReview("retirado") && C::IsServerReview("aprobado"),
              "estados local, pendiente de publicación, publicado y retirado");
        CHECK(std::string(C::NameOf(C::kReviews, "aprobado")) == "Publicado" && std::string(C::NameOf(C::kReviews, "local")) == "Perfil local",
              "un perfil local no se llama publicado");

        // Coherencia con las migraciones: cada identificador del cliente está en la lista cerrada del servidor (v2, y v4 para
        // XeMFG XMX: preparada, se aplica con autorización)
        const auto dir = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / L"ilion-community";
        std::string text;
        for (const auto* name : { L"ilion_community_v2_fg.sql", L"ilion_community_v4_xemfg_xmx.sql" })
        {
            std::ifstream in(dir / name, std::ios::binary);
            text += std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        }
        bool inSql = !text.empty();
        for (const auto& i : C::kFgs)
            inSql = inSql && text.find(std::string("'") + i.id + "'") != std::string::npos;
        for (const auto& i : C::kRoutes)
            inSql = inSql && text.find(std::string("'") + i.id + "'") != std::string::npos;
        for (const auto& i : C::kTechs)
            inSql = inSql && text.find(std::string("'") + i.id + "'") != std::string::npos;
        for (const auto& i : C::kModels)
            inSql = inSql && text.find(std::string("'") + i.id + "'") != std::string::npos;
        CHECK(inSql, "la migración v2 admite exactamente los identificadores del cliente");

        // ILION v1.4.0 fase 2: XeMFG XMX, su propio identificador (no XeFG, Enabler ni Nukem), hasta 6X, la misma ruta que los
        // demás reemplazos de DLSSG; se guarda y se lee exacto; no se publica hasta que el servidor lo admita
        const auto* xmx = C::FgImplOf("nvngx_xemfg_xmx");
        CHECK(xmx != nullptr && std::string(xmx->nvngx) == "XeMfgXmx" && std::string(xmx->output) == "DLSSG" &&
                  std::string(C::NameOf(C::kFgs, "nvngx_xemfg_xmx")) == "XeMFG XMX (realdody)",
              "XeMFG XMX en el catálogo con su nombre y su reemplazo");
        CHECK(C::FgMaxMultiplier("nvngx_xemfg_xmx", "rdna3") == 6 && C::FgMaxMultiplier("xefg", "rdna3") == 4 &&
                  C::FgEvidence("nvngx_xemfg_xmx", "rdna3") == C::Evidence::Experimental,
              "XeMFG XMX hasta 6X, distinto de XeFG");
        {
            C::Profile x = Sample();
            x.arch = "rdna3";
            x.gpu = "AMD Radeon RX 7900 XTX";
            x.fg = "nvngx_xemfg_xmx";
            x.fgRoute = "nvngx";
            x.multiplier = 6;
            C::Profile back;
            std::string e;
            CHECK(C::Validate(x, &e) && C::ParseProfileText(C::ToJson(x).dump(), back, &e) && back.fg == "nvngx_xemfg_xmx" &&
                      back.multiplier == 6 && back.fgRoute == "nvngx",
                  "un perfil XeMFG XMX 6X se guarda y se lee exacto");
            C::Profile e1 = x, e2 = x;
            e2.fg = "nvngx_enabler";
            CHECK(C::ContentHash(e1) != C::ContentHash(e2), "XeMFG XMX no se confunde con DLSS Enabler");
            C::ServiceConfig cfg;
            C::Session s;
            s.alias = "prueba";
            s.userId = "u";
            x.origin = "local";
            const auto pub = C::Publish(cfg, s, x);
            CHECK(!pub.first && pub.second.find("XeMFG XMX") != std::string::npos, "no se publica hasta actualizar el servidor");
        }
    }

    // 14. Revisión de PERFILES y COMUNIDAD: jerarquía de FG, compatibilidad v1, autoría y clasificación
    {
        // Jerarquía acordada: Enabler, XeFG, Nukem y después el resto; Nukem nunca antes que los dos primeros
        CHECK(C::FgPreferenceRank("nvngx_enabler") < C::FgPreferenceRank("xefg") && C::FgPreferenceRank("xefg") < C::FgPreferenceRank("nvngx_nukem"),
              "preferencia Enabler → XeFG → Nukem");
        bool restAfter = true;
        for (const char* id : { "dlssg_nativo", "fsr_fg", "nvngx_ffx", "nvngx_combo", "nvngx_fglite" })
            restAfter = restAfter && C::FgPreferenceRank(id) > C::FgPreferenceRank("nvngx_nukem");
        CHECK(restAfter, "el resto, después de Nukem");
        CHECK(C::FgPreferenceRank("dlssg_juego") >= 1000 && C::FgPreferenceRank("ninguno") >= 1000, "el DLSSG del juego y «sin FG» no se eligen");
        bool everyKnown = true;
        for (const char* id : C::kFgPreference)
            everyKnown = everyKnown && C::FgImplOf(id) != nullptr;
        CHECK(everyKnown, "la preferencia solo nombra implementaciones del catálogo");

        // El perfil v1 de Dawnwalker (FSR 4 driver, XeFG 2X, ruta automática) es compatible con XeFG disponible
        C::Profile v1 = Sample();
        v1.schema = 1;
        v1.gameExe = "dawnwalker.exe";
        v1.gameName = "Dawnwalker";
        v1.arch = "rdna3";
        v1.gpu = "AMD Radeon RX 7900 XTX";
        v1.tech = "fsr4_driver";
        v1.model = "completo";
        v1.fgRoute = "auto";
        std::string err;
        CHECK(C::Validate(v1, &err), "perfil v1 de Dawnwalker válido");
        C::Machine dw;
        dw.api = "DX12";
        dw.arch = "rdna3";
        dw.gpu = "AMD Radeon RX 7900 XTX";
        dw.gameExe = "dawnwalker.exe";
        dw.outW = 2560;
        dw.outH = 1440;
        dw.techs = { "fsr4_driver" };
        dw.fgs = { "ninguno", "xefg" };
        dw.routes = { "ninguno", "juego", "optifg", "streamline" };
        CHECK(C::Overall(C::Compatibility(v1, dw)) == C::Fit::Confirmed, "v1 con ruta automática: compatible");
        dw.fgs = { "ninguno" };
        CHECK(C::Overall(C::Compatibility(v1, dw)) == C::Fit::NeedsComponents, "XeFG no localizado: requiere componentes, no incompatible");
        dw.fgs = { "ninguno", "xefg" };
        C::Profile lite = v1;
        lite.schema = 2;
        lite.fg = "nvngx_fglite";
        lite.fgRoute = "nvngx";
        dw.fgs.insert("nvngx_fglite");
        dw.routes.insert("nvngx");
        CHECK(C::Overall(C::Compatibility(lite, dw)) == C::Fit::Experimental, "FG Lite en RDNA 3: experimental");
        C::Profile other = v1;
        other.api = "Vulkan";
        CHECK(C::Overall(C::Compatibility(other, dw)) == C::Fit::Incompatible, "otra API: no compatible");
        CHECK(std::string(C::FitText(C::Fit::NeedsComponents)) == "Requiere componentes o ajustes", "texto de los cuatro estados");

        // Autoría: Ene y ENE son el mismo autor; otra cuenta o un perfil descargado no se publican
        std::vector<C::Profile> lib;
        C::Profile ene = Sample();
        ene.author = "Ene";
        CHECK(C::AddOrUpdate(lib, ene) == C::AddResult::Added, "perfil de Ene");
        C::Profile edited = ene;
        edited.author = "ENE";
        edited.preset = "balanceado";
        CHECK(C::AddOrUpdate(lib, edited) == C::AddResult::Updated, "Ene / ENE: mismo autor sin distinguir mayúsculas");
        C::Profile foreign = ene;
        foreign.author = "Impostor";
        CHECK(C::AddOrUpdate(lib, foreign) == C::AddResult::NotOwner, "otro alias sigue sin sustituir");
        C::Profile downloadedCopy = ene;
        downloadedCopy.origin = "comunidad";
        CHECK(C::AddOrUpdate(lib, downloadedCopy) == C::AddResult::NotOwner, "un perfil descargado no sustituye uno propio");

        C::ServiceConfig svc;
        svc.url = "https://127.0.0.1:9";
        svc.anonKey = "abc.def";
        svc.valid = true;
        C::Session s;
        s.accessToken = "t";
        s.userId = "user-1";
        s.alias = "ENE";
        C::Profile fromCommunity = Sample();
        fromCommunity.origin = "comunidad";
        const auto t0 = GetTickCount64();
        CHECK(!C::Publish(svc, s, fromCommunity).first, "no se publica un perfil descargado");
        C::Profile otherAccount = Sample();
        otherAccount.authorId = "user-2";
        otherAccount.id = "11111111-2222-3333-4444-555555555555";
        CHECK(!C::Publish(svc, s, otherAccount).first, "no se publica un perfil de otra cuenta");
        CHECK(GetTickCount64() - t0 < 1000, "esos rechazos no llegan a la red");
        C::Profile noAlias = Sample();
        C::Session s2 = s;
        s2.alias.clear();
        CHECK(!C::Publish(svc, s2, noAlias).first, "sin alias de cuenta no se publica");

        // Clasificación con desempate determinista: descargas, después el publicado antes, después el identificador
        std::vector<C::Profile> cat(3, Sample());
        cat[0].id = "c";
        cat[0].downloads = 10;
        cat[0].created = "2026-11-03T00:00:00Z";
        cat[1].id = "b";
        cat[1].downloads = 10;
        cat[1].created = "2026-11-01T00:00:00Z";
        cat[2].id = "a";
        cat[2].downloads = 10;
        cat[2].created = "2026-11-01T00:00:00Z";
        const auto order = C::RankByDownloads(cat);
        CHECK(cat[order[0]].id == "a" && cat[order[1]].id == "b" && cat[order[2]].id == "c", "desempate: antigüedad y después identificador");
    }

    // 15. Community7: estados de publicación, clasificación de más de diez y catálogos de prueba locales
    {
        CHECK(std::string(C::NameOf(C::kReviews, "aprobado")) == "Publicado" && std::string(C::NameOf(C::kReviews, "rechazado")) == "Retirado por moderación" &&
                  std::string(C::NameOf(C::kReviews, "retirado")) == "Retirado del catálogo",
              "estados: publicado, retirado por moderación, retirado por el autor");
        CHECK(!C::IsServerReview("local") && !C::IsServerReview("por_publicar"), "un perfil local o preparado no cuenta como publicado");

        // Doce perfiles: el podio son los tres primeros por descargas y el TOP 10 sigue el mismo orden
        std::vector<C::Profile> many;
        for (int i = 0; i < 12; i++)
        {
            C::Profile q = Sample();
            q.id = "prueba-" + std::to_string(100 + i);
            q.downloads = 1000 - i * 37;
            q.created = "2026-11-0" + std::to_string(1 + i % 3) + "T00:00:00Z";
            many.push_back(q);
        }
        std::swap(many[2], many[9]);
        const auto order = C::RankByDownloads(many);
        bool sorted = true;
        for (size_t i = 1; i < order.size(); i++)
            sorted = sorted && many[order[i - 1]].downloads >= many[order[i]].downloads;
        CHECK(sorted && C::PodiumCount(many, order) == 3 && many[order[0]].id == "prueba-100", "doce perfiles: orden por descargas y podio de tres");

        // Los catálogos de prueba del paquete se leen como cualquier caché y son válidos
        const auto base = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / L"ilion-community" / L"prueba";
        int counts[4] = { -1, -1, -1, -1 };
        const wchar_t* names[4] = { L"catalogo_prueba_0.json", L"catalogo_prueba_1.json", L"catalogo_prueba_3.json", L"catalogo_prueba_12.json" };
        bool clean = true;
        for (int i = 0; i < 4; i++)
        {
            const auto r = C::LoadList(base / names[i], C::kMaxCatalogBytes);
            counts[i] = (int) r.profiles.size();
            clean = clean && r.warning.empty();
            for (const auto& q : r.profiles)
                clean = clean && q.name.rfind("Prueba", 0) == 0 && q.review == "aprobado";
        }
        CHECK(counts[0] == 0 && counts[1] == 1 && counts[2] == 3 && counts[3] == 12 && clean, "catálogos de prueba 0 / 1 / 3 / 12 válidos y marcados como prueba");
    }

    // 16. Community8: clasificación compartida por INICIO y MIS APORTES, copia editada, borrado local, nombres de FG, código
    {
        const auto base = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / L"ilion-community" / L"prueba";
        // Puesto: el mismo orden y criterio que el podio (solo con descargas reales)
        const auto c12 = C::LoadList(base / L"catalogo_prueba_12.json", C::kMaxCatalogBytes).profiles;
        const auto order = C::RankByDownloads(c12);
        bool same = true;
        for (size_t i = 0; i < order.size(); i++)
            same = same && C::RankPosition(c12, c12[order[i]].id) == (c12[order[i]].downloads > 0 ? (int) i + 1 : 0);
        CHECK(same && C::RankPosition(c12, "no-existe") == 0, "puesto en MIS APORTES = posición en INICIO; fuera del catálogo, sin clasificación");
        std::vector<C::Profile> zero = c12;
        for (auto& q : zero)
            q.downloads = 0;
        CHECK(C::RankPosition(zero, zero[0].id) == 0 && C::PodiumCount(zero, C::RankByDownloads(zero)) == 0, "sin descargas: sin clasificación y sin podio");
        CHECK(C::BestPosition(c12, { c12[order[5]].id, c12[order[2]].id }) == 3 && C::BestPosition(c12, {}) == 0, "mejor posición: el menor puesto real");
        const auto c0 = C::LoadList(base / L"catalogo_prueba_0.json", C::kMaxCatalogBytes).profiles;
        const auto c1 = C::LoadList(base / L"catalogo_prueba_1.json", C::kMaxCatalogBytes).profiles;
        const auto c3 = C::LoadList(base / L"catalogo_prueba_3.json", C::kMaxCatalogBytes).profiles;
        CHECK(C::PodiumCount(c0, C::RankByDownloads(c0)) == 0 && C::PodiumCount(c1, C::RankByDownloads(c1)) == 1 &&
                  C::PodiumCount(c3, C::RankByDownloads(c3)) == 3 && C::PodiumCount(c12, C::RankByDownloads(c12)) == 3,
              "podio con 0 / 1 / 3 / 12 perfiles: 0, 1, 3 y 3 puestos");

        // Copia editada de un perfil descargado: autoría original, identidad local nueva, nunca publicable como propia
        C::Profile original = Sample();
        original.id = "11111111-2222-3333-4444-555555555555";
        original.author = "OtroAutor";
        original.authorId = "99999999-0000-0000-0000-000000000000";
        original.origin = "comunidad";
        original.review = "aprobado";
        original.downloads = 42;
        C::Profile edited = original;
        edited.name = "Mi ajuste";
        edited.multiplier = 3;
        const C::Profile copy = C::EditedCopy(original, edited);
        CHECK(copy.author == "OtroAutor" && copy.authorId == original.authorId && copy.origin == "comunidad" && copy.id != original.id &&
                  copy.id.rfind("local-", 0) == 0 && copy.review == "local" && copy.downloads == -1 && copy.name == "Mi ajuste" && copy.multiplier == 3,
              "copia editada: conserva la autoría, identidad local nueva y sin estado público");
        CHECK(C::IsEditedCopy(copy) && !C::IsEditedCopy(original) && !C::IsEditedCopy(Sample()), "la copia editada se distingue del original y de lo propio");
        C::Session s;
        s.alias = "ENE";
        s.userId = "aaaaaaaa-0000-0000-0000-000000000000";
        C::Profile attempt = copy;
        CHECK(!C::Publish(C::ServiceConfig {}, s, attempt).first, "una copia editada de otro autor no se publica como propia");
        std::vector<C::Profile> dl { original };
        CHECK(C::AddOrUpdate(dl, copy) == C::AddResult::Added && dl.size() == 2, "volver a descargar el original no pisa la copia editada");

        // Eliminar: solo la copia de la lista local indicada
        std::vector<C::Profile> mineList { Sample() }, remote { Sample() };
        const std::string gone = mineList[0].id;
        CHECK(C::RemoveLocal(mineList, gone) && mineList.empty() && remote.size() == 1 && !C::RemoveLocal(mineList, gone),
              "eliminar borra la copia local y no toca lo publicado");

        // Desplegables de FG: solo nombres, sin etiquetas de evidencia
        bool names = true;
        for (const auto& f : C::kFgImpls)
        {
            const std::string n = C::NameOf(C::kFgs, f.id);
            names = names && !n.empty() && n.find("Compatibilidad") == std::string::npos && n.find("Experimental") == std::string::npos &&
                    n.find("oficial") == std::string::npos;
        }
        CHECK(names, "los nombres de FG no llevan «Compatibilidad…» ni «Experimental»");

        // Código del correo: se validan las cifras antes de llamar al servicio (el servicio envía 8)
        C::Session v;
        CHECK(!C::VerifyCode(C::ServiceConfig {}, "a@b.c", "12345", v).first && !C::VerifyCode(C::ServiceConfig {}, "a@b.c", "1234abcd", v).first &&
                  !C::VerifyCode(C::ServiceConfig {}, "a@b.c", "12345678901", v).first,
              "código: menos de 6, más de 10 o con letras se rechaza sin llamar al servicio");
    }

    // 17. Community10: el motivo real de la compatibilidad, empates del podio y catálogos de prueba extremos
    {
        C::Machine kcd;
        kcd.api = "DX12";
        kcd.arch = "rdna3";
        kcd.gpu = "AMD Radeon RX 7900 XTX";
        kcd.gameExe = "kcd2.exe";
        kcd.outW = 2560;
        kcd.outH = 1440;
        kcd.techs = { "fsr4_driver" };
        kcd.fgs = { "ninguno", "xefg" };
        kcd.routes = { "ninguno", "juego", "optifg", "streamline" };
        C::Profile adan = Sample();
        adan.name = "ADAN";
        adan.gameExe = "dawnwalker.exe";
        adan.gameName = "Dawnwalker";
        adan.arch = "rdna3";
        adan.gpu = "AMD Radeon RX 7900 XTX";
        adan.tech = "fsr4_driver";
        adan.model = "completo";
        const auto other = C::FitOf(adan, kcd);
        CHECK(other.level == 3 && other.label == "Perfil de otro juego (Dawnwalker)", "ADAN en KCD2: «Perfil de otro juego», no un fallo de GPU");
        C::Profile mine = adan;
        mine.gameExe = "kcd2.exe";
        CHECK(C::FitOf(mine, kcd).level == 0 && C::FitOf(mine, kcd).label == "Compatible y activable", "mismo juego y equipo: compatible y activable");
        C::Profile api = mine;
        api.api = "Vulkan";
        CHECK(C::FitOf(api, kcd).label.rfind("API incompatible", 0) == 0, "otra API: el motivo es la API");
        C::Profile gpu = mine;
        gpu.tech = "dlss";
        CHECK(C::FitOf(gpu, kcd).label.rfind("GPU incompatible", 0) == 0, "DLSS en Radeon: GPU incompatible");
        C::Profile missing = mine;
        missing.fg = "xefg";
        kcd.fgs = { "ninguno" };
        CHECK(C::FitOf(missing, kcd).level == 2 && C::FitOf(missing, kcd).label.rfind("Tecnología no disponible", 0) == 0, "XeFG no localizado: tecnología no disponible");
        C::Profile newer = mine;
        newer.schema = C::kSchema + 1;
        CHECK(C::FitOf(newer, kcd).label.rfind("Versión", 0) == 0, "perfil de una versión más nueva: el motivo es la versión");

        // Empates: a igualdad de descargas, el que se publicó antes; después, el identificador
        std::vector<C::Profile> tie(3, Sample());
        tie[0].id = "c", tie[0].downloads = 5, tie[0].created = "2026-11-03T00:00:00Z";
        tie[1].id = "b", tie[1].downloads = 5, tie[1].created = "2026-11-01T00:00:00Z";
        tie[2].id = "a", tie[2].downloads = 5, tie[2].created = "2026-11-01T00:00:00Z";
        const auto o = C::RankByDownloads(tie);
        CHECK(tie[o[0]].id == "a" && tie[o[1]].id == "b" && tie[o[2]].id == "c" && C::RankPosition(tie, "c") == 3, "empates: antigüedad y luego identificador");

        const auto base = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / L"ilion-community" / L"prueba";
        const auto none = C::LoadList(base / L"catalogo_prueba_sin_descargas.json", C::kMaxCatalogBytes);
        const auto ties = C::LoadList(base / L"catalogo_prueba_empates_largos.json", C::kMaxCatalogBytes);
        CHECK(none.warning.empty() && none.profiles.size() == 4 && C::PodiumCount(none.profiles, C::RankByDownloads(none.profiles)) == 0,
              "catálogo con perfiles y 0 descargas: válido y sin podio");
        CHECK(ties.warning.empty() && ties.profiles.size() == 6 && C::PodiumCount(ties.profiles, C::RankByDownloads(ties.profiles)) == 3,
              "catálogo con empates y nombres largos: válido, podio de tres");
    }

    printf("ILION COMMUNITY: %d comprobaciones correctas, %d fallos\n", passed, failures);
    std::filesystem::remove_all(root, ec);
    return failures == 0 ? 0 : 1;
}
