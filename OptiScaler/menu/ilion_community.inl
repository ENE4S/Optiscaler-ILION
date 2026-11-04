// ILION COMMUNITY — perfiles de la comunidad (rama experimental ilion-perfiles-exp)
//
// La columna derecha de PERFILES, solo en Industrial y Clean Olive con la presentación Completa (Compacta e ILION
// Tradicional no la dibujan ni cargan nada de la comunidad). Núcleo, almacenamiento y red en misc/IlionCommunity.h: aquí solo la interfaz y la aplicación de un perfil, que reutiliza la de Perfiles
// (ApplyFile: puntos de restauración, selección del escalador por el catálogo, FG por la configuración) y su aviso rojo /
// verde (OutcomeBanner), que verifica con lo que corre de verdad.
//
// Sin telemetría: al abrir la columna se consulta el catálogo público (y se renueva la sesión guardada); nada más sale del
// equipo sin pulsar ACTUALIZAR CATÁLOGO, DESCARGAR, PUBLICAR, RETIRAR o los botones de la cuenta.
// La red la hace curl.exe en su propio proceso desde un hilo de trabajo; la interfaz nunca espera.
//
// Incluido por menu_common.cpp después de ilion_profiles.inl. Guardado en UTF-8 con BOM.

#include "../misc/IlionCommunity.h"

#pragma execution_character_set(push, "utf-8")

namespace IlionCommunityUi
{
namespace C = IlionCommunity;
namespace P = IlionProfiles;

// Navegación: COMUNIDAD tiene tres destinos (INICIO, EXPLORAR, MIS APORTES) y la cuenta aparte
enum CPage
{
    Home,
    Explore,
    Contributions,
    AccountView,
    PublishView, // PUBLICAR PERFIL: elegir, revisar y publicar
};
static int cpage = Home;
static int cpageBack = Home;
static int requestedSection = -1; // 8 PERFILES, 9 COMUNIDAD: lo lee la barra lateral (ilion_shell.inl)
static std::string detailId;      // perfil del catálogo abierto en COMUNIDAD
// PERFILES: la biblioteca local
static std::string selectedId;
static std::string activatingId;  // tarjeta con la activación abierta
static std::string activationId;  // la tarjeta con el resultado de la última activación (solo esa lo muestra)
static std::string activationText;
static bool activationOk = false;
static std::string publishPick;   // el perfil elegido en PUBLICAR PERFIL
static std::string publishingName; // el nombre del perfil que se está publicando (lo lee la interfaz, no el hilo)
static std::vector<C::Profile> aportes; // lo publicado por la cuenta según el servidor (caché aportes.json)
static std::set<std::string> verifying; // publicados a la espera de verlos en el catálogo público
static std::map<std::string, std::string> publishErrors; // el último error de publicación de cada perfil
static std::optional<C::Profile> duplicateQueue;
static std::string deleteQueue;
static bool needCatalog = false;  // tras publicar o retirar: el catálogo se vuelve a leer para que se vea al momento
static bool testCatalog = false;  // catalogo_prueba.json local: datos de prueba, nunca del servicio
static bool editing = false;
static bool namingNew = false;
static char newName[48] = {};
static char editName[48] = {};
static std::string shareId;       // el perfil que se está compartiendo (PERFILES → COMUNIDAD)
static std::string confirmDelete;
static bool loaded = false;
static std::vector<C::Profile> mine, downloaded, catalog;
static std::set<std::string> favourites;
static std::string warning, message, communityMessage; // cada sección, sus propios mensajes
static bool messageOk = true, communityOk = true;
static int sayTarget = 0; // 0 PERFILES, 1 COMUNIDAD: dónde se dibuja ahora
static int profilesFrame = -10, communityFrame = -10; // el último fotograma de cada sección: al volver, sin mensajes viejos
static C::ServiceConfig service;
static C::Session session;
static C::Worker worker;
static int seenSerial = 0;
static auto jobSession = std::make_shared<C::Session>();
static std::string jobKind;

// Filtros
static char search[64] = {};
static int filterGame = 1;   // 0 todos, 1 este juego
static int filterGpu = 0;    // 0 todas, 1 mi arquitectura, 2 mi GPU
static int filterMode = -1;  // -1 todos
static bool onlyFavourites = false;

// Activación y edición
static std::string applied; // id activado en esta sesión
static C::Profile editDraft;
static std::shared_ptr<C::Profile> publishResult; // el perfil que publica el hilo de trabajo

// Servicio y perfiles propios publicados
static bool autoChecked = false; // la comprobación real del servicio al abrir la columna por primera vez
static auto mineRemote = std::make_shared<std::vector<C::Profile>>();
static std::string withdrawId; // retirada pendiente de confirmar

// Cuenta
static char email[128] = {};
static char code[16] = {};
static char claim[32] = {};

static std::filesystem::path MinePath() { return C::Root() / L"mis_perfiles.json"; }
static std::filesystem::path DownloadedPath() { return C::Root() / L"descargados.json"; }
static std::filesystem::path CatalogPath() { return C::Root() / L"catalogo.json"; }
static std::filesystem::path FavouritesPath() { return C::Root() / L"favoritos.json"; }
static std::filesystem::path AportesPath() { return C::Root() / L"aportes.json"; }

// Un mensaje para la sección que se está dibujando (los de PERFILES nunca aparecen en COMUNIDAD y al revés)
static void Say(bool ok, const std::string& text)
{
    if (sayTarget == 1)
        communityOk = ok, communityMessage = text;
    else
        messageOk = ok, message = text;
}

// Un trabajo del hilo que necesita la sesión: la renueva antes si ha caducado
template <typename F> static void RunWithSession(const char* task, const std::string& kind, F fn)
{
    const auto cfg = service;
    *jobSession = session;
    auto js = jobSession;
    jobKind = kind;
    worker.Start(task,
                 [cfg, js, fn]() -> std::pair<bool, std::string>
                 {
                     if (!js->Active() || js->expires < (long long) std::time(nullptr))
                         if (auto r = C::Refresh(cfg, *js); !r.first)
                             return r;
                     return fn(cfg, *js);
                 });
}

// La configuración de ILION justo antes de la última activación de un perfil de la comunidad (para volver a ella)
static std::filesystem::path SnapshotPath() { return P::FileOf(L".antes-comunidad.ini"); }

static void AddWarning(const std::string& w)
{
    if (!w.empty())
        warning += (warning.empty() ? "" : " · ") + w;
}

static void LoadAll()
{
    loaded = true;
    warning.clear();
    if (C::Root().empty())
    {
        warning = "No se encontró la carpeta de datos de la aplicación";
        return;
    }
    auto m = C::LoadList(MinePath());
    mine = m.profiles;
    AddWarning(m.warning);
    auto d = C::LoadList(DownloadedPath());
    downloaded = d.profiles;
    AddWarning(d.warning);
    // catalogo_prueba.json (si existe): para revisar la clasificación con 0, 1, 3 o 10+ perfiles sin tocar el catálogo público
    std::error_code ec;
    testCatalog = std::filesystem::exists(C::Root() / L"catalogo_prueba.json", ec);
    auto c = C::LoadList(testCatalog ? C::Root() / L"catalogo_prueba.json" : CatalogPath(), C::kMaxCatalogBytes);
    catalog = c.profiles;
    AddWarning(c.warning);
    auto a = C::LoadList(AportesPath());
    aportes = a.profiles;
    std::string fw;
    favourites = C::LoadIds(FavouritesPath(), &fw);
    AddWarning(fw);
    service = C::LoadService();
    session = C::LoadSession();
}

/// ---- El equipo actual ----------------------------------------------------------------------------------------------
// El identificador de arquitectura del catálogo técnico (una sola tabla, en Perfiles)
static const char* ArchId(const P::Arch& a) { return P::CatalogArchId(a); }

static std::string ApiId()
{
    switch (State::Instance().api)
    {
    case API::DX12:
        return "DX12";
    case API::DX11:
        return "DX11";
    case API::Vulkan:
        return "Vulkan";
    default:
        return "?";
    }
}

// La tecnología de una variante del catálogo de escaladores (vacío: no se comparte por perfiles)
static std::string TechOf(const UpscalerVariant& v)
{
    if (P::IsDriver(v))
        return "fsr4_driver";
    if (P::IsCommunityLite(v))
        return "fsr4_lite";
    if (P::IsFsr4Full(v))
        return "fsr4_dll";
    if (P::IsHlx(v))
        return "hlx_fsr4";
    if (v.entry != nullptr && v.entry->family == IlionUpscalers::Family::HelixSR)
        return "helixsr";
    if (v.entry != nullptr && v.entry->kind == IlionUpscalers::Kind::D4r)
        return "dlss_d4r";
    if (v.backend == Upscaler::DLSS)
        return "dlss";
    if (v.backend == Upscaler::XeSS)
        return "xess";
    if (v.family == FamFsr31)
        return "fsr31";
    if (v.backend == Upscaler::FSR22)
        return "fsr22";
    if (v.backend == Upscaler::FSR21)
        return "fsr21";
    return "";
}

// El modelo DLSS que nombra una entrada d4r («DLSS4/Modelo_K» → dlss_k); vacío si no lo nombra
static std::string D4rModelOf(const UpscalerVariant& v)
{
    if (v.entry == nullptr || v.entry->kind != IlionUpscalers::Kind::D4r)
        return std::string();
    std::wstring key = v.entry->key;
    std::transform(key.begin(), key.end(), key.begin(), ::towlower);
    const auto at = key.find(L"modelo_");
    if (at == std::wstring::npos || at + 7 >= key.size())
        return std::string();
    const wchar_t letter = key[at + 7];
    return letter >= L'j' && letter <= L'm' ? std::string("dlss_") + (char) letter : std::string();
}

// La variante que reproduce una tecnología (y, en d4r, su modelo si está instalado)
static const UpscalerVariant* VariantFor(const UpscalerCatalog& catalogue, const std::string& tech, const std::string& model = std::string())
{
    const UpscalerVariant* first = nullptr;
    for (const auto& v : catalogue.variants)
        if (v.usable && TechOf(v) == tech)
        {
            if (first == nullptr)
                first = &v;
            if (tech == "dlss_d4r" && !model.empty() && D4rModelOf(v) == model)
                return &v;
        }
    return first;
}

template <typename Ctx> static C::Machine MachineOf(Ctx& ctx)
{
    C::Machine m;
    const GpuInformation gpu = IdentifyGpu::getPrimaryGpu();
    const P::Arch arch = P::ArchOf(gpu);
    m.api = ApiId();
    m.arch = ArchId(arch);
    m.gpu = gpu.name;
    m.gameExe = P::ExeKey();
    IFeature* f = ctx.currentFeature;
    if (f != nullptr && f->IsInited())
        m.outW = (int) f->TargetWidth(), m.outH = (int) f->TargetHeight();
    const auto catalogue = GatherUpscalers(f);
    for (const auto& v : catalogue.variants)
        if (v.usable && !TechOf(v).empty())
            m.techs.insert(TechOf(v));
    m.liteOffered = P::DriverLiteOffered() && !arch.liteIsFull;
    // Frame Generation: las mismas comprobaciones que FRAME GEN y el optimizador (P::FgAvailable)
    for (const auto& impl : C::kFgImpls)
        if (P::FgAvailable(impl.id))
            m.fgs.insert(impl.id);
    m.routes = P::FgRoutesAvailable(f);
    return m;
}

// El modelo DLSS pedido en la configuración (preset de render para todos los modos)
static std::string DlssModelNow()
{
    auto config = Config::Instance();
    if (!config->RenderPresetOverride.value_or_default() || !config->RenderPresetForAll.has_value())
        return "dlss_auto";
    switch (config->RenderPresetForAll.value())
    {
    case NVSDK_NGX_DLSS_Hint_Render_Preset_J:
        return "dlss_j";
    case NVSDK_NGX_DLSS_Hint_Render_Preset_K:
        return "dlss_k";
    case NVSDK_NGX_DLSS_Hint_Render_Preset_L:
        return "dlss_l";
    case NVSDK_NGX_DLSS_Hint_Render_Preset_M:
        return "dlss_m";
    case NV_PRESET_LATEST:
        return "dlss_latest";
    default:
        return "dlss_auto"; // otros presets (antiguos o sin uso) no se comparten
    }
}

// La implementación de FG configurada ahora (catálogo técnico) y su ruta de entrada
static void FgNow(std::string& fg, std::string& route)
{
    auto config = Config::Instance();
    const auto status = IlionDynamicMfg::Get();
    const FGInput in = config->FGInput.value_or_default();
    const FGOutput outp = config->FGOutput.value_or_default();
    const FGNvngxReplacement nv = config->FGNvngxReplacement.value_or_default();
    route = in == FGInput::Upscaler ? "optifg"
            : in == FGInput::DLSSG  ? "streamline"
            : in == FGInput::NvngxFG ? "nvngx"
            : in == FGInput::FSRFG   ? "fsr31"
            : in == FGInput::FSRFG30 ? "fsr30"
                                     : "juego";
    const auto replacement = [&]() -> std::string
    {
        switch (nv)
        {
        case FGNvngxReplacement::Nukems:
            return "nvngx_nukem";
        case FGNvngxReplacement::Arturs:
            return "nvngx_enabler";
        case FGNvngxReplacement::FFX:
            return "nvngx_ffx";
        case FGNvngxReplacement::Combo:
            return "nvngx_combo";
        case FGNvngxReplacement::HelixSR:
            return "nvngx_fglite";
        default:
            return "dlssg_nativo";
        }
    };
    if (!config->FGEnabled.value_or_default() && status.provider != IlionDynamicMfg::Provider::DlssgGame)
        fg = "ninguno";
    else if (outp == FGOutput::XeFG)
        fg = "xefg";
    else if (outp == FGOutput::FSRFG)
        fg = "fsr_fg";
    else if (outp == FGOutput::DLSSG || in == FGInput::NvngxFG)
        fg = replacement();
    else if (status.provider == IlionDynamicMfg::Provider::DlssgGame)
        fg = "dlssg_juego", route = "juego";
    else
        fg = "ninguno";
    if (fg == "ninguno")
        route = "ninguno";
    else if (!C::FgRouteAllowed(fg, route))
        route = C::DefaultRoute(fg);
}

// La configuración activa, para rellenar el formulario (el usuario la revisa antes de guardar o publicar)
template <typename Ctx> static C::Profile FromCurrent(Ctx& ctx)
{
    C::Profile p;
    auto config = Config::Instance();
    const C::Machine m = MachineOf(ctx);
    const auto& state = State::Instance();
    p.id = C::NewLocalId();
    p.gameExe = m.gameExe;
    p.gameName = !state.gameName.empty() ? state.gameName : m.gameExe.substr(0, m.gameExe.size() - 4);
    p.api = m.api;
    p.arch = m.arch;
    p.gpu = m.gpu;
    p.outW = m.outW > 0 ? m.outW : 2560;
    p.outH = m.outH > 0 ? m.outH : 1440;
    static const char* modeIds[] = { "optimo", "calidad", "equilibrado", "rendimiento" };
    p.mode = modeIds[std::clamp((int) P::mode, 0, 3)];
    IFeature* f = ctx.currentFeature;
    const auto catalogue = GatherUpscalers(f);
    const UpscalerVariant* cur = P::Current(catalogue);
    p.tech = cur != nullptr && !TechOf(*cur).empty() ? TechOf(*cur) : "fsr4_driver";
    if (p.tech == "fsr4_driver" || p.tech == "fsr4_dll")
        p.model = P::SelectedModel() == P::Model::Lite ? "int8lite" : "completo";
    else if (p.tech == "dlss")
        p.model = DlssModelNow();
    else if (p.tech == "dlss_d4r")
        p.model = cur != nullptr && !D4rModelOf(*cur).empty() ? D4rModelOf(*cur) : "dlss_auto";
    else
        p.model = "no_aplica";
    const int preset = f != nullptr && f->IsInited() && f->RenderWidth() > 0 ? P::PresetOf((float) f->TargetWidth() / (float) f->RenderWidth()) : 2;
    p.preset = C::kPresets[std::clamp(preset, 0, 5)].id;
    FgNow(p.fg, p.fgRoute);
    // El multiplicador: el que genera ahora; si no genera, el fijo configurado para esa implementación
    const auto status = IlionDynamicMfg::Get();
    const bool gen = status.gen == IlionDynamicMfg::Gen::Detected || status.gen == IlionDynamicMfg::Gen::Confirmed ||
                     (state.currentFG != nullptr && state.currentFG->IsActive());
    int count = 1;
    if (gen && status.multiplier > 1)
        count = status.multiplier - 1;
    else if (p.fg == "xefg")
        count = config->FGXeFGInterpolationCount.value_or_default();
    else if (p.fgRoute == "nvngx" || p.fg == "dlssg_juego")
        count = config->FGDLSSGOverrideInterpolationCount.value_or(1);
    else if (p.fg != "ninguno" && p.fg != "fsr_fg")
        count = config->FGDLSSGInterpolationCount.value_or_default();
    const int maxMult = std::max(2, C::FgMaxMultiplier(p.fg, p.arch));
    p.multiplier = p.fg == "ninguno" ? 0 : std::clamp(count + 1, 2, maxMult);
    p.created = p.updated = C::NowUtc();
    return p;
}

// Lo que aún no se puede leer de verdad: GUARDAR COMO PERFIL lo exige en vez de rellenarlo con valores supuestos
template <typename Ctx> static std::string UnknownNow(Ctx& ctx)
{
    const C::Machine m = MachineOf(ctx);
    IFeature* f = ctx.currentFeature;
    std::string missing;
    const auto add = [&](const char* t) { missing += (missing.empty() ? std::string() : std::string(", ")) + t; };
    if (m.outW <= 0 || m.outH <= 0)
        add("la resolución de salida");
    const auto catalogue = GatherUpscalers(f);
    const UpscalerVariant* cur = P::Current(catalogue);
    if (cur == nullptr || TechOf(*cur).empty())
        add("el escalador activo");
    if (f == nullptr || !f->IsInited() || f->RenderWidth() == 0)
        add("el preset (la resolución interna)");
    return missing;
}

// Lo que escribe un perfil en la configuración de FG de ILION (claves de perfil.ini de Perfiles). restart: cambia la
// entrada, la salida o el reemplazo NVNGX cargados
static bool WriteFg(CSimpleIniA& ini, const char* s, const C::Profile& p, std::string& what)
{
    auto config = Config::Instance();
    const auto& state = State::Instance();
    if (p.fg == "ninguno")
    {
        ini.SetValue(s, "fg_activado", "false");
        what = "sin FG";
        return false;
    }
    const C::FgImpl* impl = C::FgImplOf(p.fg);
    ini.SetValue(s, "fg_activado", "true");
    const std::string count = std::to_string(p.multiplier - 1);

    // La entrada: la ruta del perfil; en un perfil v1 («auto»), la de siempre si no había ninguna
    FGInput in = config->FGInput.value_or_default();
    if (p.fgRoute == "optifg")
        in = FGInput::Upscaler;
    else if (p.fgRoute == "streamline")
        in = FGInput::DLSSG;
    else if (p.fgRoute == "nvngx")
        in = FGInput::NvngxFG;
    else if (p.fgRoute == "fsr31")
        in = FGInput::FSRFG;
    else if (p.fgRoute == "fsr30")
        in = FGInput::FSRFG30;
    else if (p.fgRoute == "juego")
        in = FGInput::NoFG;
    else if (in == FGInput::NoFG)
        in = state.streamlineVersion.major >= 2 ? FGInput::DLSSG : FGInput::Upscaler;

    // La salida y el reemplazo NVNGX de la implementación
    FGOutput outp = FGOutput::NoFG;
    if (p.fgRoute != "nvngx" && impl != nullptr && impl->output[0] != 0)
        if (const auto e = magic_enum::enum_cast<FGOutput>(impl->output); e.has_value())
            outp = *e;
    FGNvngxReplacement nv = config->FGNvngxReplacement.value_or_default();
    if (impl != nullptr && impl->nvngx[0] != 0)
        if (const auto e = magic_enum::enum_cast<FGNvngxReplacement>(impl->nvngx); e.has_value())
            nv = *e;

    ini.SetValue(s, "fg_entrada", std::string(magic_enum::enum_name(in)).c_str());
    ini.SetValue(s, "fg_salida", std::string(magic_enum::enum_name(outp)).c_str());
    if (impl != nullptr && impl->nvngx[0] != 0)
        ini.SetValue(s, "fg_nvngx", std::string(magic_enum::enum_name(nv)).c_str());

    // El multiplicador fijo, en la clave que usa esa implementación
    if (outp == FGOutput::XeFG)
        ini.SetValue(s, "xefg_multiplicador_fijo", count.c_str());
    else if (outp == FGOutput::DLSSG)
        ini.SetValue(s, "dlssg_salida_multiplicador_fijo", count.c_str());
    else if (p.fgRoute == "nvngx" || p.fg == "dlssg_juego")
        ini.SetValue(s, "dlssg_multiplicador_fijo", count.c_str());
    ini.SetValue(s, "fg_multiplicador", StrFmt("%dX", p.multiplier).c_str());
    what = StrFmt("%s %dX fijo · entrada %s", C::NameOf(C::kFgs, p.fg), p.multiplier, C::NameOf(C::kRoutes, p.fgRoute));
    return in != state.activeFgInput || outp != state.activeFgOutput || (impl != nullptr && impl->nvngx[0] != 0 && nv != state.activeFgNvngx);
}

// ---- Activar un perfil: reutiliza Perfiles ----------------------------------------------------------------------------
template <typename Ctx> static bool Apply(Ctx& ctx, const C::Profile& p, std::string& out)
{
    std::string err;
    if (!C::Validate(p, &err))
    {
        out = "Perfil no válido: " + err;
        return false;
    }
    const C::Machine m = MachineOf(ctx);
    if (C::Blocking(C::Compatibility(p, m)))
    {
        out = "No se activa: hay incompatibilidades con tu equipo (ver la lista)";
        return false;
    }
    IFeature* f = ctx.currentFeature;
    const auto catalogue = GatherUpscalers(f);
    const UpscalerVariant* v = VariantFor(catalogue, p.tech, p.model);
    if (v == nullptr)
    {
        out = std::string("No instalada: ") + C::NameOf(C::kTechs, p.tech);
        return false;
    }
    auto config = Config::Instance();
    int presetIndex = 0;
    for (int i = 0; i < 6; i++)
        if (p.preset == C::kPresets[i].id)
            presetIndex = i;

    // Copia de seguridad del perfil que hubiera (además de los puntos original y de sesión que protege ApplyFile)
    std::error_code ec;
    std::filesystem::create_directories(P::Folder(), ec);
    const auto backup = P::FileOf(L".perfil.antes-comunidad.ini");
    if (P::Exists(P::ProfilePath()))
    {
        std::filesystem::copy_file(P::ProfilePath(), P::PreviousPath(), std::filesystem::copy_options::overwrite_existing, ec);
        std::filesystem::copy_file(P::ProfilePath(), backup, std::filesystem::copy_options::overwrite_existing, ec);
    }

    CSimpleIniA ini(true);
    P::SaveManaged(ini, "perfil", f); // lo que el perfil no toca queda como está
    const char* s = "perfil";
    ini.SetValue(s, "escalador", P::KeyOf(*v).c_str());
    ini.SetValue(s, "escalador_nombre", v->name.c_str());
    if (p.model == "completo")
        ini.SetValue(s, "fsr4_modelo", "auto");
    else if (p.model == "int8lite")
        ini.SetValue(s, "fsr4_modelo", std::to_string((int) FSR4Model::INT8Lite).c_str());
    // DLSS nativo: su modelo (preset de render para todos los modos); «el del juego» quita la anulación
    if (p.tech == "dlss" && C::IsDlssModel(p.model))
    {
        static const struct
        {
            const char* id;
            uint32_t value;
        } presets[] = { { "dlss_j", NVSDK_NGX_DLSS_Hint_Render_Preset_J }, { "dlss_k", NVSDK_NGX_DLSS_Hint_Render_Preset_K },
                        { "dlss_l", NVSDK_NGX_DLSS_Hint_Render_Preset_L }, { "dlss_m", NVSDK_NGX_DLSS_Hint_Render_Preset_M },
                        { "dlss_latest", NV_PRESET_LATEST } };
        ini.SetValue(s, "dlss_preset_activo", "false");
        for (const auto& pr : presets)
            if (p.model == pr.id)
            {
                ini.SetValue(s, "dlss_preset_activo", "true");
                ini.SetValue(s, "dlss_preset", std::to_string(pr.value).c_str());
            }
    }

    // Preset: por el menú del juego si lee su tabla al arrancar (como OPTIMIZAR), si no por la anulación de proporción
    std::string why;
    const bool viaMenu = P::RatioNeedsRestart(P::LoadRule(), &why);
    bool ratioRestart = false;
    if (viaMenu)
    {
        ratioRestart = config->UpscaleRatioOverrideEnabled.value_or_default();
        ini.SetValue(s, "ratio_activo", "false");
    }
    else
    {
        ini.SetValue(s, "ratio_activo", "true");
        ini.SetValue(s, "ratio", StrFmt("%.2f", P::kPresets[presetIndex].ratio).c_str());
    }

    // Frame Generation fijo, la implementación exacta del perfil (nunca el MFG dinámico)
    std::string fgWhat;
    const bool fgRestart = WriteFg(ini, s, p, fgWhat);
    std::string restart = fgRestart ? "fg" : "";
    if (ratioRestart)
        restart += restart.empty() ? "proporcion" : ",proporcion";
    ini.SetValue(s, "reinicio_pendiente", restart.empty() ? "no" : restart.c_str());
    ini.SetValue(s, "modo_escalado", P::kPresets[presetIndex].name);
    ini.SetValue(s, "ruta_proporcion", viaMenu ? "menu" : "consulta");
    ini.SetValue(s, "modo", C::NameOf(C::kModes, p.mode));
    ini.SetValue(s, "fecha", P::NowText().c_str());
    ini.SetValue(s, "sesion", P::SessionId().c_str());
    ini.SetValue(s, "origen", "comunidad");
    ini.SetValue(s, "comunidad_id", p.id.c_str());
    ini.SetValue(s, "comunidad_autor", p.author.c_str());
    ini.SetValue(s, "comunidad_version", std::to_string(p.version).c_str());
    ini.SetValue(s, "comunidad_fg", p.fg.c_str());
    ini.SetValue(s, "comunidad_fg_ruta", p.fgRoute.c_str());
    ini.SetValue(s, "motivo_escalador", ("Perfil de la comunidad de " + p.author + " (no verificado técnicamente)").c_str());
    if (ini.SaveFile(P::ProfilePath().c_str()) < 0)
    {
        out = "No se pudo escribir el perfil";
        return false;
    }
    // Instantánea de la configuración actual: RESTAURAR LA CONFIGURACIÓN ANTERIOR vuelve exactamente a ella
    P::SaveSnapshot(SnapshotPath(), "configuración anterior a activar un perfil de la comunidad", f);
    if (!P::Exists(SnapshotPath()))
    {
        out = "No se activó: no se pudo guardar la configuración actual para poder restaurarla";
        return false;
    }
    P::ApplyFile(ctx, P::ProfilePath(), "perfil", "Perfil de la comunidad", false);
    // El modo del optimizador pasa a ser el del perfil
    for (int i = 0; i < 4; i++)
        if (p.mode == C::kModes[i].id)
            P::mode = (P::Mode) i;
    P::modeLoaded = true;
    applied = p.id;
    out = std::string(v->name) + ", preset " + P::kPresets[presetIndex].name + ", " + fgWhat +
          (fgRestart ? ". El cambio de FG se aplica al reiniciar el juego" : "") + ". Lo que falte por comprobar está en el cuadro de pendientes";
    return true;
}

// ---- Piezas de la interfaz -----------------------------------------------------------------------------------------
static std::string TechLine(const C::Profile& p)
{
    std::string s = C::NameOf(C::kTechs, p.tech);
    if (p.model != "no_aplica")
        s += std::string(" · ") + C::NameOf(C::kModels, p.model);
    s += std::string(" · ") + C::NameOf(C::kPresets, p.preset);
    s += p.fg == "ninguno" ? std::string(" · sin FG") : StrFmt(" · %s %dX fijo", C::NameOf(C::kFgs, p.fg), p.multiplier);
    s += StrFmt(" · %d×%d · %s", p.outW, p.outH, p.api.c_str());
    return s;
}

// El estado de un perfil: local, pendiente de publicación, pendiente de moderación, publicado, rechazado o retirado
static std::string KindOf(const C::Profile& p)
{
    if (p.origin == "comunidad")
        return p.review == "aprobado" ? "Perfil de la comunidad · publicado" : "Perfil de la comunidad";
    if (p.origin == "importado")
        return "Perfil importado";
    if (p.review == "por_publicar")
        return "Pendiente de publicación: verifica tu correo en MI CUENTA para continuar";
    return C::NameOf(C::kReviews, p.review);
}

// Las tarjetas comparan cada perfil con este equipo (lo fija el panel antes de dibujar la página)
static const C::Machine* cardMachine = nullptr;

// El color del fabricante de la GPU de una arquitectura
static ImVec4 ArchVendorColour(const std::string& arch)
{
    return P::VendorColour(C::IsAmdArch(arch) ? VendorId::AMD : C::IsNvidiaArch(arch) ? VendorId::Nvidia : C::IsIntelArch(arch) ? VendorId::Intel : VendorId::Invalid);
}

// Un tramo de texto de color en la misma línea; si no cabe, pasa a la siguiente
static void Seg(const ImVec4& colour, const std::string& text, bool first = false)
{
    if (!first)
    {
        ImGui::SameLine(0.0f, 0.0f);
        if (ImGui::GetContentRegionAvail().x < ImGui::CalcTextSize(text.c_str()).x)
            ImGui::NewLine();
    }
    ImGui::PushStyleColor(ImGuiCol_Text, toneMapColor(colour));
    ImGui::TextUnformatted(text.c_str());
    ImGui::PopStyleColor();
}

template <size_t N> static bool Combo(const char* label, const C::Item (&list)[N], std::string& value, const std::function<bool(const char*)>& allowed = nullptr)
{
    bool changed = false;
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (IlionTheme::BeginCombo(label, C::NameOf(list, value)))
    {
        for (const auto& i : list)
        {
            if (allowed && !allowed(i.id))
                continue;
            if (ImGui::Selectable(i.name, value == i.id))
                value = i.id, changed = true;
        }
        ImGui::EndCombo();
    }
    return changed;
}

static bool Matches(const C::Profile& p, const C::Machine& m)
{
    if (filterGame == 1 && p.gameExe != m.gameExe)
        return false;
    if (filterGpu == 1 && p.arch != m.arch)
        return false;
    if (filterGpu == 2 && p.gpu != m.gpu)
        return false;
    if (filterMode >= 0 && p.mode != C::kModes[filterMode].id)
        return false;
    if (onlyFavourites && favourites.count(p.id) == 0)
        return false;
    if (search[0] != 0)
    {
        std::string hay = p.author + " " + p.name + " " + p.gameName + " " + p.gameExe + " " + p.gpu, needle = search;
        std::transform(hay.begin(), hay.end(), hay.begin(), [](unsigned char c) { return (char) std::tolower(c); });
        std::transform(needle.begin(), needle.end(), needle.begin(), [](unsigned char c) { return (char) std::tolower(c); });
        if (hay.find(needle) == std::string::npos)
            return false;
    }
    return true;
}

static void ToggleFavourite(const std::string& id)
{
    if (favourites.count(id))
        favourites.erase(id);
    else
        favourites.insert(id);
    if (!C::SaveIds(FavouritesPath(), favourites))
        Say(false, "No se pudieron guardar los favoritos");
}

// Una tarjeta: el creador destacado, debajo «Juego — GPU — Modo» y la configuración en una línea
static void Card(const C::Profile& p, const char* list, bool showDownloads, const std::function<void()>& actions, int position = 0)
{
    const float font = ImGui::GetFontSize();
    ImGui::PushID((std::string(list) + p.id).c_str());
    ImGui::PushStyleColor(ImGuiCol_ChildBg, toneMapColor(IlionTheme::shell ? IlionTheme::ShellPal::Panel : ImGui::GetStyleColorVec4(ImGuiCol_FrameBg)));
    ImGui::PushStyleColor(ImGuiCol_Border, toneMapColor(IlionTheme::ShellPal::Line));
    if (ImGui::BeginChild("##card", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY))
    {
        // 1. El nombre del perfil, el elemento principal
        if (position > 0)
        {
            P::Big(P::FigureFont(), 1.2f, IlionTheme::ShellPal::Text2, StrFmt("Nº %d", position).c_str());
            ImGui::SameLine();
        }
        P::Big(P::FigureFont(), 1.3f, IlionTheme::ShellPal::Text, p.name.c_str(), true);
        // 2. El autor, con su propio tratamiento
        ImGui::PushFont(P::UiFont(), std::floor(font * 1.08f));
        Seg(IlionTheme::ShellPal::Text2, "Creado por: ", true);
        Seg(IlionTheme::ShellPal::Orange, p.author);
        ImGui::PopFont();
        // 3. Juego · GPU · arquitectura · modo
        Seg(IlionTheme::InfoCyan, p.gameName, true);
        Seg(IlionTheme::ShellPal::Dim, " · ");
        Seg(ArchVendorColour(p.arch), p.gpu);
        Seg(IlionTheme::ShellPal::Dim, " · ");
        Seg(IlionTheme::ShellPal::Text2, C::NameOf(C::kArchs, p.arch));
        Seg(IlionTheme::ShellPal::Dim, " · modo ");
        Seg(IlionTheme::ShellPilot(IlionTheme::ShellPal::Yellow), C::NameOf(C::kModes, p.mode));
        // 4. Escalador · modelo · preset
        Seg(IlionTheme::InfoBlue, C::NameOf(C::kTechs, p.tech), true);
        if (p.model != "no_aplica")
            Seg(IlionTheme::ShellPal::Text2, std::string(" · ") + C::NameOf(C::kModels, p.model));
        Seg(IlionTheme::ShellPal::Text2, std::string(" · ") + C::NameOf(C::kPresets, p.preset) + StrFmt(" · %d×%d", p.outW, p.outH));
        // 5. Frame Generation · multiplicador · ruta
        if (p.fg == "ninguno")
            Seg(IlionTheme::ShellPal::Text2, "Sin Frame Generation", true);
        else
        {
            Seg(IlionTheme::ShellPilot(IlionTheme::ShellPal::Green), C::NameOf(C::kFgs, p.fg), true);
            Seg(IlionTheme::InfoBlue, StrFmt(" %dX fijo", p.multiplier));
            Seg(IlionTheme::ShellPal::Dim, std::string(" · ") + C::NameOf(C::kRoutes, p.fgRoute));
        }
        // 6. Compatibilidad con este equipo · descargas registradas · estado
        if (cardMachine != nullptr)
        {
            const C::Fit fit = C::Overall(C::Compatibility(p, *cardMachine));
            Seg(fit == C::Fit::Confirmed ? IlionTheme::ShellPilot(IlionTheme::ShellPal::Green)
                : fit == C::Fit::Partial ? IlionTheme::ShellPilot(IlionTheme::ShellPal::Yellow)
                                         : IlionTheme::ShellPal::Red,
                C::FitText(fit), true);
        }
        std::string badges = (cardMachine != nullptr ? " · " : "") + KindOf(p) + StrFmt(" · v%d", p.version);
        if (showDownloads)
            badges += p.downloads >= 0 ? StrFmt(" · %lld descargas registradas", p.downloads) : std::string(" · descargas: sin dato");
        if (favourites.count(p.id))
            badges += " · FAVORITO";
        Seg(IlionTheme::ShellPal::Dim, badges, cardMachine == nullptr);
        actions();
    }
    ImGui::EndChild();
    ImGui::PopStyleColor(2);
    ImGui::PopID();
}

static void DownloadProfile(const C::Profile& p);

// ---- Disposición en columna ------------------------------------------------------------------------------------------
// Un botón más en la misma fila si cabe; si no, a la fila siguiente (la columna de la comunidad es estrecha)
static void Flow(const char* nextLabel)
{
    const ImGuiStyle& style = ImGui::GetStyle();
    const float width = ImGui::CalcTextSize(nextLabel, nullptr, true).x + style.FramePadding.x * 2.0f + ImGui::GetFontSize() * 0.6f;
    ImGui::SameLine();
    if (ImGui::GetContentRegionAvail().x < width)
        ImGui::NewLine();
}

// Descargar: copia el perfil a DESCARGADOS (nunca lo aplica) y, si el hilo está libre, avisa al contador del servidor
static void DownloadProfile(const C::Profile& p)
{
    C::Profile copy = p;
    copy.origin = "comunidad";
    const auto r = C::AddOrUpdate(downloaded, copy);
    if (r == C::AddResult::Full)
        Say(false, "La lista de descargados está llena (500)");
    else if (!C::SaveList(DownloadedPath(), downloaded))
        Say(false, "No se pudo guardar el perfil descargado");
    else
    {
        Say(true, r == C::AddResult::Updated ? "Perfil descargado de nuevo (actualizado). No se ha aplicado"
                                             : "Perfil descargado, no activado: actívalo desde PERFILES");
        // El contador del servidor: solo con sesión (una por cuenta y perfil). No influye en nada local
        if (!testCatalog && service.valid && !worker.Get().busy && (session.Active() || !session.refreshToken.empty()))
        {
            const std::string id = p.id;
            RunWithSession("Registrando la descarga...", "descarga",
                           [id](const C::ServiceConfig& cfg, C::Session& s)
                           {
                               const auto r = C::RegisterDownload(cfg, s, id);
                               return std::pair<bool, std::string> { true, "Perfil descargado, no activado. " + r.second };
                           });
        }
        else if (service.valid)
            Say(true, "Perfil descargado, no activado: actívalo desde PERFILES. Sin sesión la descarga no cuenta para la clasificación");
    }
}

// La fecha local de un archivo (dd/mm/aaaa hh:mm), o vacía si no existe
static std::string FileStamp(const std::filesystem::path& file)
{
    struct _stat64 st {};
    if (_wstat64(file.c_str(), &st) != 0)
        return std::string();
    std::tm local {};
    const time_t t = (time_t) st.st_mtime;
    if (localtime_s(&local, &t) != 0)
        return std::string();
    char text[32] = {};
    std::strftime(text, sizeof(text), "%d/%m/%Y %H:%M", &local);
    return text;
}

// Cuándo se descargó el catálogo en caché (fecha del archivo): los números son de ese momento, no en vivo
static std::string CatalogStamp()
{
    struct _stat64 st {};
    if (_wstat64(CatalogPath().c_str(), &st) != 0)
        return std::string();
    std::tm local {};
    const time_t t = (time_t) st.st_mtime;
    if (localtime_s(&local, &t) != 0)
        return std::string();
    char text[32] = {};
    std::strftime(text, sizeof(text), "%d/%m/%Y %H:%M", &local);
    return text;
}

// El catálogo ordenado por descargas reales del servidor (de más a menos; sin dato al final): C::RankByDownloads
static std::vector<const C::Profile*> ByDownloads()
{
    std::vector<const C::Profile*> list;
    for (size_t i : C::RankByDownloads(catalog))
        list.push_back(&catalog[i]);
    return list;
}

// ---- TOP 3: reconocimiento por descargas ------------------------------------------------------------------------------
// Rojo, verde y naranja intensos. Industrial: sus pilotos tal cual, con halo. Clean Olive: los mismos tonos ajustados a
// su paleta (rojo profundo, verde lima de sus pilotos, naranja ámbar), con el halo corto y el borde oscuro de sus pilotos
static ImVec4 PodiumColour(int rank)
{
    // Oro, plata y bronce intensos; en Clean Olive, algo más cálidos para su paleta
    const bool olive = IlionTheme::shellLemonPilots;
    if (rank == 1)
        return olive ? ImVec4(0.98f, 0.80f, 0.22f, 1.0f) : ImVec4(1.00f, 0.80f, 0.12f, 1.0f);
    if (rank == 2)
        return olive ? ImVec4(0.80f, 0.82f, 0.78f, 1.0f) : ImVec4(0.82f, 0.86f, 0.92f, 1.0f);
    return olive ? ImVec4(0.86f, 0.52f, 0.24f, 1.0f) : ImVec4(0.90f, 0.50f, 0.20f, 1.0f);
}

static ImVec4 Fade(const ImVec4& c, float a) { return ImVec4(c.x, c.y, c.z, c.w * a); }

// Una corona luminosa con el puesto dentro de un disco
static void Crown(ImDrawList* dl, ImVec2 o, float s, const ImVec4& colour, int rank)
{
    const bool olive = IlionTheme::shellLemonPilots;
    const float glow = IlionTheme::shellGlow;
    const ImVec2 pts[] = { ImVec2(o.x, o.y + s * 0.62f),           ImVec2(o.x + s * 0.04f, o.y + s * 0.10f), ImVec2(o.x + s * 0.29f, o.y + s * 0.38f),
                           ImVec2(o.x + s * 0.50f, o.y),           ImVec2(o.x + s * 0.71f, o.y + s * 0.38f), ImVec2(o.x + s * 0.96f, o.y + s * 0.10f),
                           ImVec2(o.x + s, o.y + s * 0.62f) };
    const ImVec2 centre(o.x + s * 0.5f, o.y + s * 0.42f);
    // Halo
    for (int i = 4; i >= 1; i--)
        dl->AddCircleFilled(centre, s * (0.45f + i * (olive ? 0.07f : 0.13f)), ImGui::GetColorU32(Fade(colour, 0.06f * (5 - i) * glow)));
    dl->AddConcavePolyFilled(pts, (int) std::size(pts), ImGui::GetColorU32(colour));
    dl->AddRectFilled(ImVec2(o.x, o.y + s * 0.66f), ImVec2(o.x + s, o.y + s * 0.80f), ImGui::GetColorU32(colour), s * 0.04f);
    if (olive)
        dl->AddPolyline(pts, (int) std::size(pts), ImGui::GetColorU32(ImVec4(0, 0, 0, 0.55f)), ImDrawFlags_Closed, 1.5f);
    // Las tres perlas y el brillo
    for (const ImVec2& tip : { pts[1], pts[3], pts[5] })
    {
        dl->AddCircleFilled(tip, s * 0.075f, ImGui::GetColorU32(colour));
        dl->AddCircleFilled(ImVec2(tip.x - s * 0.02f, tip.y - s * 0.02f), s * 0.03f, ImGui::GetColorU32(ImVec4(1, 1, 1, 0.70f)));
    }
    // El número, oscuro sobre el disco de la corona
    const ImVec2 disc(o.x + s * 0.5f, o.y + s * 0.47f);
    dl->AddCircleFilled(disc, s * 0.17f, ImGui::GetColorU32(ImVec4(0.03f, 0.03f, 0.03f, 0.92f)));
    char number[4] = { (char) ('0' + rank), 0 };
    ImFont* f = P::FigureFont();
    const float size = std::floor(s * 0.30f);
    const ImVec2 ts = f->CalcTextSizeA(size, FLT_MAX, 0.0f, number);
    dl->AddText(f, size, ImVec2(disc.x - ts.x * 0.5f, disc.y - ts.y * 0.5f), ImGui::GetColorU32(colour), number);
}

// La tarjeta de un puesto del TOP 3: corona y número a la izquierda, la franja del color del puesto y los datos
static C::Profile* FindLocal(const std::string& id);

static void PodiumCard(const C::Profile& p, int rank)
{
    const float font = ImGui::GetFontSize();
    const ImVec4 colour = PodiumColour(rank);
    const float big = rank == 1 ? 1.25f : 1.0f;
    ImGui::PushID(("top" + std::to_string(rank) + p.id).c_str());
    ImGui::PushStyleColor(ImGuiCol_ChildBg, toneMapColor(IlionTheme::ShellMix(IlionTheme::ShellPal::Panel, colour, rank == 1 ? 0.10f : 0.06f)));
    ImGui::PushStyleColor(ImGuiCol_Border, toneMapColor(Fade(colour, 0.9f)));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, rank == 1 ? 2.0f : 1.5f);
    if (ImGui::BeginChild("##podium", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding))
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float w = ImGui::GetContentRegionAvail().x;
        const float crown = std::floor(font * 2.2f * big);
        const ImVec2 start = ImGui::GetCursorScreenPos();
        Crown(dl, ImVec2(start.x + (w - crown) * 0.5f, start.y), crown, toneMapColor(colour), rank);
        ImGui::Dummy(ImVec2(w, crown));
        const auto centred = [&](ImFont* f, float scale, const ImVec4& c, const std::string& t)
        {
            ImGui::PushFont(f, std::floor(font * scale));
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (std::max)(0.0f, (w - ImGui::CalcTextSize(t.c_str()).x) * 0.5f));
            P::Coloured(c, t.c_str());
            ImGui::PopFont();
        };
        centred(P::FigureFont(), 1.2f * big, IlionTheme::ShellPal::Text, p.name);
        centred(P::UiFont(), 1.0f, IlionTheme::ShellPal::Orange, "de " + p.author);
        centred(P::UiFont(), 0.95f, IlionTheme::InfoCyan, p.gameName + "  ·  " + C::NameOf(C::kArchs, p.arch));
        centred(P::UiFont(), 0.95f, IlionTheme::ShellPal::Text2,
                std::string(C::NameOf(C::kTechs, p.tech)) + (p.fg == "ninguno" ? std::string(" · sin FG") : StrFmt(" · %s %dX", C::NameOf(C::kFgs, p.fg), p.multiplier)));
        centred(P::FigureFont(), 1.1f * big, colour, StrFmt("%lld descargas", (std::max)(0LL, p.downloads)));
        const float bw = std::floor((w - ImGui::GetStyle().ItemSpacing.x) * 0.5f);
        if (IlionTheme::Button("VER", ImVec2(bw, 0.0f)))
            detailId = p.id;
        ImGui::SameLine();
        const bool have = FindLocal(p.id) != nullptr;
        ImGui::BeginDisabled(have);
        if (IlionTheme::Button(have ? "DESCARGADO" : "DESCARGAR", ImVec2(bw, 0.0f)))
            DownloadProfile(p);
        ImGui::EndDisabled();
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
    ImGui::PopID();
}

// MI CUENTA: lo publicado por la cuenta con los datos del servidor (estado de moderación y descargas registradas)
static bool statsRequested = false;

static void RequestAuthorStats()
{
    auto out = mineRemote;
    RunWithSession("Consultando tus publicaciones...", "mios", [out](const C::ServiceConfig& cfg, C::Session& s) { return C::FetchMine(cfg, s, *out); });
}

// Al terminar un trabajo: se recoge su resultado en el hilo de la interfaz (sus mensajes son de COMUNIDAD)
static void CollectJob();
static C::Profile* FindCatalog(const std::string& id);
static void Collect()
{
    const int keep = sayTarget;
    sayTarget = 1;
    CollectJob();
    sayTarget = keep;
}

static void CollectJob()
{
    const auto ws = worker.Get();
    if (ws.serial == seenSerial)
        return;
    seenSerial = ws.serial;
    if (!ws.result.empty())
        Say(ws.ok, ws.result);
    // Los errores, con su tipo
    if (!ws.ok && !ws.result.empty())
    {
        if (jobKind == "sesion" || jobKind == "codigo")
            Say(false, "Error de autenticación: " + ws.result);
        else if (jobKind.rfind("publicar:", 0) == 0)
            Say(false, "Error de publicación: " + ws.result);
    }
    // Los trabajos con sesión la devuelven renovada
    if (jobKind == "inicio" || jobKind == "mios" || jobKind == "descarga" || jobKind.rfind("retirar:", 0) == 0)
        if (!jobSession->refreshToken.empty())
            session = *jobSession;
    if (jobKind == "mios" && ws.ok)
    {
        // MIS APORTES = lo que el servidor dice que publicó la cuenta. Las copias locales solo actualizan su versión: si se
        // borró una en PERFILES, no reaparece
        aportes = *mineRemote;
        C::SaveList(AportesPath(), aportes);
        for (const auto& r : aportes)
            for (auto& e : mine)
                if (e.id == r.id)
                    e.review = r.review, e.version = r.version, e.downloads = r.downloads, e.authorId = r.authorId;
        C::SaveList(MinePath(), mine);
    }
    if (jobKind.rfind("retirar:", 0) == 0 && ws.ok)
    {
        needCatalog = true;
        // Ya no existe en el servidor: la copia local vuelve a ser un perfil sin publicar
        const std::string id = jobKind.substr(8);
        for (auto& e : mine)
            if (e.id == id)
            {
                e.id = C::NewLocalId();
                e.authorId.clear();
                e.review = "local";
                e.downloads = -1;
                if (selectedId == id)
                    selectedId = e.id;
            }
        C::SaveList(MinePath(), mine);
        C::RemoveLocal(aportes, id);
        C::SaveList(AportesPath(), aportes);
        verifying.erase(id);
        Say(true, "Publicación retirada. Tu copia sigue en MIS PERFILES");
    }
    if (jobKind == "catalogo" && !ws.ok)
        verifying.clear(); // sin catálogo no se puede confirmar: queda «Aún no aparece en el catálogo»
    if ((jobKind == "catalogo" || jobKind == "inicio") && ws.ok && !testCatalog)
    {
        auto c = C::LoadList(CatalogPath(), C::kMaxCatalogBytes);
        catalog = c.profiles;
        AddWarning(c.warning);
        // «Publicado» solo cuando el catálogo público lo muestra: tras esta lectura, lo encontrado queda confirmado
        bool visible = true;
        for (const auto& id : verifying)
            visible = visible && FindCatalog(id) != nullptr;
        if (!verifying.empty())
            Say(visible, visible ? "Publicado: ya es visible en el catálogo público" : "El servidor aceptó la publicación, pero aún no aparece en el catálogo");
        verifying.clear();
    }
    else if (jobKind == "sesion" && ws.ok)
    {
        session = *jobSession;
        email[0] = code[0] = 0;
    }
    else if (jobKind.rfind("publicar:", 0) == 0)
    {
        session = *jobSession;
        if (!ws.ok)
            publishErrors[jobKind.substr(9)] = ws.result.size() > 90 ? ws.result.substr(0, 90) + "…" : ws.result;
        else
            publishErrors.erase(jobKind.substr(9)), needCatalog = true;
        if (ws.ok && publishResult)
        {
            // El perfil local pasa a tener la identidad del servidor (id, autor, versión, estado)
            const std::string oldId = jobKind.substr(9);
            for (auto& e : mine)
                if (e.id == oldId)
                    e = *publishResult;
            C::SaveList(MinePath(), mine);
            // MIS APORTES: «Publicando…» hasta que el catálogo público lo muestre
            C::RemoveLocal(aportes, publishResult->id);
            aportes.push_back(*publishResult);
            C::SaveList(AportesPath(), aportes);
            if (publishResult->review == "aprobado" && !testCatalog)
                verifying.insert(publishResult->id);
            if (selectedId == oldId)
                selectedId = publishResult->id;
            if (shareId == oldId)
                shareId.clear();
        }
    }
    jobKind.clear();
}

// =====================================================================================================================
// PERFILES y COMUNIDAD ILION (Industrial y Clean Olive en su presentación Completa). Dos secciones de la barra lateral:
// PERFILES es técnica (optimizador, biblioteca local, activación, información) y COMUNIDAD es la de participación
// (clasificación, explorar, aportes, cuenta). Compacta e ILION Tradicional no dibujan nada de esto
// =====================================================================================================================

// El espaciado común de las dos secciones: paneles con unos 16 px de margen interior, controles con unos 12 px
struct SpacingScope
{
    SpacingScope()
    {
        const float f = ImGui::GetFontSize();
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(std::floor(f * 0.95f), std::floor(f * 0.8f)));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(std::floor(f * 0.7f), std::floor(f * 0.38f)));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(std::floor(f * 0.6f), std::floor(f * 0.5f)));
    }
    ~SpacingScope() { ImGui::PopStyleVar(3); }
};

// Un bloque con título: separa la información en la columna técnica y los grupos de controles
static void Block(const char* title)
{
    ImGui::Spacing();
    IlionTheme::SectionPlate(title);
}

// Un perfil de la biblioteca local (propios y descargados)
static C::Profile* FindLocal(const std::string& id)
{
    for (auto& e : mine)
        if (e.id == id)
            return &e;
    for (auto& e : downloaded)
        if (e.id == id)
            return &e;
    return nullptr;
}

static bool IsOwn(const C::Profile& p) { return p.origin == "local"; }

static C::Profile* FindCatalog(const std::string& id)
{
    for (auto& e : catalog)
        if (e.id == id)
            return &e;
    return nullptr;
}

static void SaveLocalLists()
{
    if (!C::SaveList(MinePath(), mine) || !C::SaveList(DownloadedPath(), downloaded))
        Say(false, "No se pudo guardar la biblioteca de perfiles");
}

// ---- Editor (solo los controles necesarios, al pulsar EDITAR) -----------------------------------------------------------
static void EditFields(C::Profile& d, const C::Machine& m)
{
    const auto label = [](const char* t) { P::Coloured(IlionTheme::ShellPal::Text2, t); };
    label("Nombre del perfil");
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputText("##EditName", editName, 41);
    label("Modo");
    Combo("##EditMode", C::kModes, d.mode);
    label("Escalado");
    if (Combo("##EditTech", C::kTechs, d.tech, [&](const char* id) { return C::TechSupport(id, d.arch).possible; }))
        d.model = d.tech == "fsr4_driver" || d.tech == "fsr4_dll" ? "completo" : d.tech == "dlss" || d.tech == "dlss_d4r" ? "dlss_auto" : "no_aplica";
    if (d.model != "no_aplica")
    {
        label("Modelo");
        Combo("##EditModel", C::kModels, d.model, [&](const char* id) { return C::ModelSupport(id, d.tech, d.arch).possible; });
    }
    label("Preset");
    Combo("##EditPreset", C::kPresets, d.preset);
    label("Frame Generation (multiplicador fijo)");
    {
        ImGui::SetNextItemWidth(-FLT_MIN);
        const auto fgLabel = [](const std::string& id) { return std::string(C::NameOf(C::kFgs, id)); }; // solo el nombre
        if (IlionTheme::BeginCombo("##EditFg", fgLabel(d.fg).c_str()))
        {
            for (const auto& f : C::kFgImpls)
            {
                const std::string id = f.id;
                if (id == "dlssg_salida" || (id != "ninguno" && C::FgMaxMultiplier(id, d.arch) == 0))
                    continue;
                if (ImGui::Selectable(fgLabel(id).c_str(), d.fg == id))
                {
                    d.fg = id;
                    d.multiplier = id == "ninguno" ? 0 : 2;
                    d.fgRoute = id == "ninguno" ? "ninguno" : C::FgRouteAllowed(id, d.fgRoute) ? d.fgRoute : C::DefaultRoute(id);
                    d.schema = C::kSchema;
                }
            }
            ImGui::EndCombo();
        }
    }
    if (d.fg != "ninguno")
    {
        const float half = std::floor((ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f);
        ImGui::BeginGroup();
        label("Entrada");
        ImGui::SetNextItemWidth(half);
        if (IlionTheme::BeginCombo("##EditRoute", C::NameOf(C::kRoutes, d.fgRoute)))
        {
            for (const auto& r : C::kRoutes)
                if (std::string(r.id) != "auto" && C::FgRouteAllowed(d.fg, r.id) && ImGui::Selectable(r.name, d.fgRoute == r.id))
                    d.fgRoute = r.id, d.schema = C::kSchema;
            ImGui::EndCombo();
        }
        ImGui::EndGroup();
        ImGui::SameLine();
        ImGui::BeginGroup();
        label("Multiplicador");
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (IlionTheme::BeginCombo("##EditMult", StrFmt("%dX", d.multiplier).c_str()))
        {
            for (int x = 2; x <= C::FgMaxMultiplier(d.fg, d.arch); x++)
                if (ImGui::Selectable(StrFmt("%dX", x).c_str(), d.multiplier == x))
                    d.multiplier = x;
            ImGui::EndCombo();
        }
        ImGui::EndGroup();
    }
    // El equipo de destino: necesario para la compatibilidad; plegado (normalmente es este equipo)
    if (ImGui::TreeNode(StrFmt("Equipo de destino: %s · %s · %d×%d###EditTarget", d.gameName.c_str(), d.gpu.c_str(), d.outW, d.outH).c_str()))
    {
        label("API");
        Combo("##EditApi", C::kApis, d.api);
        label("Arquitectura");
        if (Combo("##EditArch", C::kArchs, d.arch))
        {
            d.gpu.clear();
            if (!C::TechSupport(d.tech, d.arch).possible)
                d.tech = "xess", d.model = "no_aplica";
        }
        label("GPU");
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (IlionTheme::BeginCombo("##EditGpu", d.gpu.empty() ? "Elige la GPU" : d.gpu.c_str()))
        {
            if (d.arch == m.arch && ImGui::Selectable((m.gpu + " (detectada)").c_str(), d.gpu == m.gpu))
                d.gpu = m.gpu;
            for (const auto& g : C::kGpus)
                if (d.arch == g.arch && g.name != m.gpu && ImGui::Selectable(g.name, d.gpu == g.name))
                    d.gpu = g.name;
            ImGui::EndCombo();
        }
        label("Resolución de salida");
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (IlionTheme::BeginCombo("##EditRes", StrFmt("%d×%d", d.outW, d.outH).c_str()))
        {
            if (m.outW > 0 && ImGui::Selectable(StrFmt("%d×%d (la actual)", m.outW, m.outH).c_str(), d.outW == m.outW && d.outH == m.outH))
                d.outW = m.outW, d.outH = m.outH;
            for (const auto& r : C::kResolutions)
                if (!(r.w == m.outW && r.h == m.outH) && ImGui::Selectable(StrFmt("%d×%d", r.w, r.h).c_str(), d.outW == r.w && d.outH == r.h))
                    d.outW = r.w, d.outH = r.h;
            ImGui::EndCombo();
        }
        ImGui::TreePop();
    }
}

// ---- Piezas comunes ---------------------------------------------------------------------------------------------
// Un bloque centrado de anchura limitada (sin barra propia: crece con su contenido dentro de la zona desplazable)
static bool BeginCentered(const char* id, float maxWidth)
{
    const float avail = ImGui::GetContentRegionAvail().x;
    const float w = (std::min)(avail, maxWidth);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::floor((avail - w) * 0.5f));
    return ImGui::BeginChild(id, ImVec2(w, 0.0f), ImGuiChildFlags_AutoResizeY, ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar);
}

// Una etiqueta de estado con su piloto (Publicado, Publicando…, Error…)
static void StatusChip(const ImVec4& colour, const char* text)
{
    const float h = ImGui::GetTextLineHeight();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    IlionTheme::ShellLed(ImGui::GetWindowDrawList(), ImVec2(p.x + h * 0.5f, p.y + h * 0.5f), std::floor(h * 0.24f), toneMapColor(colour), true);
    ImGui::Dummy(ImVec2(h, h));
    ImGui::SameLine(0.0f, std::floor(h * 0.35f));
    P::Coloured(colour, text);
}

// Publicar ya (con sesión y alias): sin más formularios; el resultado llega a MIS APORTES
static void StartPublish(const C::Profile& p)
{
    const auto cfg = service;
    *jobSession = session;
    auto js = jobSession;
    auto prof = std::make_shared<C::Profile>(p);
    jobKind = "publicar:" + p.id;
    publishingName = p.name;
    publishResult = prof;
    publishErrors.erase(p.id);
    worker.Start("Publicando...",
                 [cfg, js, prof]()
                 {
                     if (!js->Active() || js->expires < (long long) std::time(nullptr))
                         if (auto r = C::Refresh(cfg, *js); !r.first)
                             return r;
                     if (js->alias.empty())
                         C::FetchAlias(cfg, *js);
                     return C::Publish(cfg, *js, *prof);
                 });
}

// COMPARTIR (PERFILES): abre PUBLICAR PERFIL en COMUNIDAD con este perfil ya elegido (el mismo recorrido que desde COMUNIDAD)
static void OpenPublish(const std::string& id, int back);
static void Share(C::Profile& p)
{
    selectedId = p.id;
    OpenPublish(p.id, Contributions);
    requestedSection = 9;
}

// ---- PERFILES: tarjeta de la biblioteca con sus acciones a la vista ------------------------------------------------
// Estados de la tarjeta, excluyentes: reposo (ACTIVAR · EDITAR · COMPARTIR · ELIMINAR, y el resultado de la última activación
// si fue esta) | confirmando la activación | confirmando la eliminación. La publicación no se muestra aquí: está en MIS APORTES
static std::string ShortError(const std::string& s) { return s.size() > 140 ? s.substr(0, 140) + "…" : s; }

template <typename Ctx> static void LibraryCard(Ctx& ctx, C::Profile& p, const C::Machine& m)
{
    const float font = ImGui::GetFontSize();
    const bool sel = selectedId == p.id;
    const bool own = IsOwn(p);
    ImGui::PushID(p.id.c_str());
    ImGui::PushStyleColor(ImGuiCol_ChildBg, toneMapColor(sel ? IlionTheme::ShellPal::PanelHi : IlionTheme::ShellPal::Panel));
    ImGui::PushStyleColor(ImGuiCol_Border, toneMapColor(sel ? IlionTheme::ShellPal::Orange : IlionTheme::ShellPal::Line));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, sel ? 2.0f : 1.0f);
    if (ImGui::BeginChild("##libCard", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding))
    {
        // Nombre (pulsar muestra sus detalles a la derecha)
        if (ImGui::Selectable("##libName", sel, ImGuiSelectableFlags_AllowOverlap, ImVec2(0.0f, std::floor(font * 1.35f))))
            selectedId = sel ? std::string() : p.id;
        ImGui::SameLine(0.0f, 0.0f);
        ImGui::SetCursorPosX(ImGui::GetStyle().WindowPadding.x);
        P::Big(P::FigureFont(), 1.15f, IlionTheme::ShellPal::Text, p.name.c_str());
        // Juego · escalado · FG y multiplicador
        Seg(IlionTheme::InfoCyan, p.gameName, true);
        Seg(IlionTheme::ShellPal::Dim, "  ·  ");
        Seg(IlionTheme::InfoBlue, C::NameOf(C::kTechs, p.tech));
        Seg(IlionTheme::ShellPal::Dim, "  ·  ");
        Seg(IlionTheme::ShellPilot(IlionTheme::ShellPal::Green), p.fg == "ninguno" ? std::string("sin FG") : StrFmt("%s %dX", C::NameOf(C::kFgs, p.fg), p.multiplier));
        // Procedencia: creado aquí, descargado, copia editada o importado (la autoría original se conserva)
        if (own)
            Seg(IlionTheme::ShellPal::Text2, "Creado en este equipo", true);
        else if (C::IsEditedCopy(p))
            Seg(IlionTheme::ShellPal::Orange, "Copia editada · original de " + p.author, true);
        else
            Seg(IlionTheme::ShellPal::Orange, (p.origin == "importado" ? "Importado · de " : "Descargado · de ") + p.author, true);
        // Compatibilidad solo cuando importa (si no es plenamente compatible)
        const C::Fit fit = C::Overall(C::Compatibility(p, m));
        if (fit != C::Fit::Confirmed && fit != C::Fit::Partial)
            StatusChip(fit == C::Fit::Incompatible ? IlionTheme::ShellPal::Red : IlionTheme::ShellPilot(IlionTheme::ShellPal::Yellow), C::FitText(fit));

        if (activatingId == p.id)
        {
            // Confirmando la activación: solo esto, sin resultados anteriores
            const bool blocked = C::Blocking(C::Compatibility(p, m));
            P::Row(blocked ? P::Mark::Bad : P::Mark::Wait, blocked ? "No se puede activar en este equipo" : "¿Activar este perfil?",
                   blocked ? "El detalle está a la derecha" : "Antes se guarda tu configuración actual para poder volver a ella");
            ImGui::BeginDisabled(blocked);
            ImGui::PushStyleColor(ImGuiCol_Text, toneMapColor(IlionTheme::ShellPal::Orange));
            if (IlionTheme::Button("ACTIVAR AHORA"))
            {
                std::string out;
                activationOk = Apply(ctx, p, out);
                activationText = out;
                activationId = p.id;
                activatingId.clear();
                selectedId = p.id;
            }
            ImGui::PopStyleColor();
            ImGui::EndDisabled();
            Flow("CANCELAR");
            if (IlionTheme::Button("CANCELAR"))
                activatingId.clear();
        }
        else if (confirmDelete == p.id)
        {
            // Confirmando la eliminación: solo la copia de este equipo
            const bool published = own && std::any_of(aportes.begin(), aportes.end(), [&](const C::Profile& a) { return a.id == p.id; });
            P::Row(P::Mark::Wait, "¿Eliminar «" + p.name + "» de este equipo?",
                   published ? "Solo se borra la copia local. Su publicación sigue en COMUNIDAD: retírala en MIS APORTES si quieres"
                             : "Se borra de MIS PERFILES. No afecta a nada publicado");
            ImGui::PushStyleColor(ImGuiCol_Text, toneMapColor(IlionTheme::ShellPal::Red));
            if (IlionTheme::Button("ELIMINAR##delGo"))
                deleteQueue = p.id;
            ImGui::PopStyleColor();
            Flow("CANCELAR##del");
            if (IlionTheme::Button("CANCELAR##del"))
                confirmDelete.clear();
        }
        else
        {
            // Reposo: el resultado de la última activación, si fue esta tarjeta, y las acciones
            if (activationId == p.id)
            {
                StatusChip(activationOk ? IlionTheme::ShellPilot(IlionTheme::ShellPal::Green) : IlionTheme::ShellPal::Red,
                           activationOk ? "Activado" : "No se activó");
                P::Coloured(IlionTheme::ShellPal::Dim, ShortError(activationText).c_str(), true);
            }
            ImGui::PushStyleColor(ImGuiCol_Text, toneMapColor(IlionTheme::ShellPal::Orange));
            if (IlionTheme::Button("ACTIVAR"))
            {
                activatingId = p.id, selectedId = p.id;
                confirmDelete.clear();
                activationId.clear();
                editing = false;
            }
            ImGui::PopStyleColor();
            Flow("EDITAR");
            if (IlionTheme::Button("EDITAR"))
            {
                editing = true;
                selectedId = p.id;
                editDraft = p;
                strncpy_s(editName, p.name.c_str(), _TRUNCATE);
                activationId.clear();
            }
            if (own)
            {
                Flow("COMPARTIR");
                if (IlionTheme::Button("COMPARTIR"))
                    Share(p);
            }
            Flow("ELIMINAR");
            if (IlionTheme::Button("ELIMINAR"))
            {
                confirmDelete = p.id;
                editing = editing && selectedId != p.id;
                activationId.clear();
            }
            Flow("···");
            if (IlionTheme::Button("···"))
                ImGui::OpenPopup("##cardMore");
        }
        if (ImGui::BeginPopup("##cardMore"))
        {
            if (ImGui::MenuItem("Duplicar"))
            {
                C::Profile copy = p;
                copy.id = C::NewLocalId();
                copy.name = (copy.name.size() > 31 ? copy.name.substr(0, 31) : copy.name) + " (copia)";
                copy.review = "local";
                if (IsOwn(copy))
                    copy.authorId.clear();
                copy.downloads = -1;
                copy.created = copy.updated = C::NowUtc();
                duplicateQueue = copy; // se añade al terminar el dibujo de la lista
            }
            if (ImGui::MenuItem("Exportar (portapapeles y archivo)"))
            {
                ImGui::SetClipboardText(C::ToJson(p).dump(1).c_str());
                const auto file = C::Root() / L"exportados" / (string_to_wstring(p.id) + L".ilionprofile.json");
                Say(C::WriteAtomic(file, C::ToJson(p).dump(1)), "Copiado al portapapeles y exportado a " + wstring_to_string(file.wstring()));
            }
            ImGui::EndPopup();
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
    ImGui::PopID();

    // El editor, debajo de su tarjeta y solo mientras se edita. En un perfil descargado se guarda una copia editada que
    // conserva la autoría original
    if (editing && selectedId == p.id)
    {
        ImGui::Indent(font * 0.6f);
        if (!own)
            P::Coloured(IlionTheme::ShellPal::Dim, ("Se guardará como copia editada. La autoría original (" + p.author + ") se conserva y lo publicado no cambia.").c_str(),
                        true);
        EditFields(editDraft, m);
        C::Profile check = editDraft;
        check.name = editName;
        std::string err;
        const bool valid = C::Validate(check, &err);
        if (!valid)
            P::Row(P::Mark::Bad, "Falta completar: " + err, std::string());
        if (IlionTheme::Button("RELLENAR CON LO ACTUAL##EditFill"))
        {
            if (const std::string missing = UnknownNow(ctx); !missing.empty())
                Say(false, "Aún no se puede leer " + missing + ": entra en una partida y vuelve a intentarlo");
            else
            {
                const C::Profile keep = editDraft;
                editDraft = FromCurrent(ctx);
                editDraft.id = keep.id, editDraft.author = keep.author, editDraft.authorId = keep.authorId, editDraft.origin = keep.origin;
                editDraft.review = keep.review, editDraft.created = keep.created, editDraft.version = keep.version;
            }
        }
        Flow("GUARDAR CAMBIOS##EditSave");
        ImGui::BeginDisabled(!valid);
        if (IlionTheme::Button("GUARDAR CAMBIOS##EditSave"))
        {
            check.updated = C::NowUtc();
            if (own)
            {
                const auto r = C::AddOrUpdate(mine, check);
                if (r == C::AddResult::Updated)
                {
                    SaveLocalLists();
                    editing = false;
                    Say(true, "Cambios guardados en «" + check.name + "»");
                }
                else
                    Say(false, "No se pudo guardar");
            }
            else
            {
                // La copia editada sustituye a la descargada en la biblioteca; el original se puede volver a descargar
                const C::Profile original = p; // p es el elemento de la lista que se sustituye
                for (auto& e : downloaded)
                    if (e.id == original.id)
                    {
                        if (C::IsEditedCopy(original))
                        {
                            e = check;
                            e.author = original.author, e.authorId = original.authorId, e.origin = original.origin;
                            e.review = "local", e.downloads = -1;
                        }
                        else
                            e = C::EditedCopy(original, check);
                        selectedId = e.id;
                        break;
                    }
                SaveLocalLists();
                editing = false;
                Say(true, "Guardado como copia editada. El original de " + original.author + " sigue en COMUNIDAD");
            }
        }
        ImGui::EndDisabled();
        Flow("CANCELAR##EditCancel");
        if (IlionTheme::Button("CANCELAR##EditCancel"))
            editing = false;
        ImGui::Unindent(font * 0.6f);
    }
}

// ---- PERFILES: perfil seleccionado (columna técnica): compatibilidad, diferencias con lo actual y resultado ----------
template <typename Ctx> static void SelectedInfo(Ctx& ctx, const C::Profile& p, const C::Machine& m)
{
    Block("PERFIL SELECCIONADO");
    P::Big(P::FigureFont(), 1.2f, IlionTheme::ShellPal::Text, p.name.c_str(), true);
    Seg(IlionTheme::ShellPal::Text2, "Creado por: ", true);
    Seg(IlionTheme::ShellPal::Orange, IsOwn(p) ? (session.alias.empty() ? std::string("ti (perfil local)") : session.alias) : p.author);
    Seg(IlionTheme::ShellPal::Dim, "  ·  " + KindOf(p));
    const auto checks = C::Compatibility(p, m);
    const C::Fit fit = C::Overall(checks);
    P::Row(fit == C::Fit::Confirmed || fit == C::Fit::Partial ? P::Mark::Ok : fit == C::Fit::Incompatible ? P::Mark::Bad : P::Mark::Wait, C::FitText(fit),
           fit == C::Fit::NeedsComponents ? "Lo que falta se indica en amarillo debajo" : std::string());
    for (const auto& c : checks)
        if (!c.note)
            P::Row(c.level == C::Level::Ok ? P::Mark::Ok : c.level == C::Level::Bad && !c.component ? P::Mark::Bad : P::Mark::Wait, c.text, std::string());
    // Diferencias con lo que corre ahora
    const C::Profile now = FromCurrent(ctx);
    const auto diff = [&](const char* what, const std::string& want, const std::string& have)
    {
        if (want != have)
            P::Row(P::Mark::Info, StrFmt("%s: %s", what, want.c_str()), "ahora: " + have);
    };
    ImGui::Spacing();
    P::Coloured(IlionTheme::ShellPal::Text2, "DIFERENCIAS CON LO ACTUAL");
    diff("Escalado", C::NameOf(C::kTechs, p.tech), C::NameOf(C::kTechs, now.tech));
    diff("Modelo", C::NameOf(C::kModels, p.model), C::NameOf(C::kModels, now.model));
    diff("Preset", C::NameOf(C::kPresets, p.preset), C::NameOf(C::kPresets, now.preset));
    diff("Frame Generation", p.fg == "ninguno" ? std::string("sin FG") : StrFmt("%s %dX", C::NameOf(C::kFgs, p.fg), p.multiplier),
         now.fg == "ninguno" ? std::string("sin FG") : StrFmt("%s %dX", C::NameOf(C::kFgs, now.fg), now.multiplier));
    if (p.fg != "ninguno")
        diff("Entrada del FG", C::NameOf(C::kRoutes, p.fgRoute), C::NameOf(C::kRoutes, now.fgRoute));
    diff("Modo", C::NameOf(C::kModes, p.mode), C::NameOf(C::kModes, now.mode));
    if (C::ContentHash(p) == C::ContentHash(now))
        P::Coloured(IlionTheme::ShellPal::Dim, "Sin diferencias con la configuración actual.", true);
    if (applied == p.id)
        P::Coloured(IlionTheme::ShellPal::Dim, "Activado en esta sesión: lo pendiente está en el cuadro de la izquierda y los pasos en el registro.", true);
}

// ---- PERFILES: dos columnas con un único desplazamiento (el del área de la sección) --------------------------------
template <typename Ctx> static void ProfilesPanel(Ctx& ctx)
{
    if (!loaded)
        LoadAll();
    Collect();
    // Al volver a PERFILES (o desde COMUNIDAD), sin mensajes ni confirmaciones de antes
    if (profilesFrame < ImGui::GetFrameCount() - 1)
    {
        message.clear();
        activatingId.clear();
        confirmDelete.clear();
        P::lastMessage.clear();
    }
    profilesFrame = ImGui::GetFrameCount();
    sayTarget = 0;
    SpacingScope spacing;
    const float font = ImGui::GetFontSize();
    const C::Machine m = MachineOf(ctx);
    cardMachine = &m;

    ImGui::PushStyleColor(ImGuiCol_TableBorderLight, toneMapColor(IlionTheme::ShellPal::Line));
    if (ImGui::BeginTable("##Profiles2", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_PadOuterX | ImGuiTableFlags_NoSavedSettings))
    {
        ImGui::TableSetupColumn("##config", ImGuiTableColumnFlags_WidthStretch, 0.45f);
        ImGui::TableSetupColumn("##info", ImGuiTableColumnFlags_WidthStretch, 0.55f);
        ImGui::TableNextRow();

        // ======== Izquierda: configuración y acciones ========
        ImGui::TableSetColumnIndex(0);
        Block("OPTIMIZADOR INTELIGENTE");
        P::OptimizerBlock();
        // GUARDAR COMO PERFIL: la configuración actual, con un nombre
        if (!namingNew)
        {
            if (IlionTheme::Button("GUARDAR COMO PERFIL##SaveAs", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f)))
                namingNew = true, newName[0] = 0;
        }
        else
        {
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::InputTextWithHint("##NewName", "Nombre del perfil (3 a 40 caracteres)", newName, 41);
            if (IlionTheme::Button("GUARDAR##SaveAsGo"))
            {
                // Guarda lo que corre ahora, venga del optimizador o de una elección manual; lo que no se puede leer no se inventa
                C::Profile p = FromCurrent(ctx);
                p.name = newName;
                p.author = session.alias.empty() ? "Local" : session.alias;
                std::string err;
                if (const std::string missing = UnknownNow(ctx); !missing.empty())
                    Say(false, "No se guardó: aún no se puede leer " + missing + ". Entra en una partida y vuelve a intentarlo");
                else if (!C::Validate(p, &err))
                    Say(false, "No se guardó: " + err);
                else
                {
                    const auto r = C::AddOrUpdate(mine, p);
                    if (r == C::AddResult::Duplicate)
                        Say(false, "Ya tienes un perfil con esta misma configuración");
                    else if (r == C::AddResult::Full)
                        Say(false, "La biblioteca está llena (500)");
                    else
                    {
                        SaveLocalLists();
                        selectedId = p.id;
                        namingNew = false;
                        Say(true, "Guardado en MIS PERFILES: " + p.name);
                    }
                }
            }
            Flow("CANCELAR##SaveAsCancel");
            if (IlionTheme::Button("CANCELAR##SaveAsCancel"))
                namingNew = false;
        }
        // Cuadro compacto: solo lo pendiente (desaparece con todo verificado) y el último mensaje
        ImGui::Spacing();
        {
            const P::Saved saved = P::ReadSaved();
            P::OutcomeBanner(ctx, saved, P::ReadLive(ctx, saved), true);
            const auto ws = worker.Get();
            if (ws.busy)
                P::Row(P::Mark::Wait, ws.task, "En segundo plano: el juego no espera");
            else if (!message.empty())
                P::Row(messageOk ? P::Mark::Ok : P::Mark::Bad, message, std::string());
            if (!P::lastMessage.empty())
                P::Row(P::Mark::Info, P::lastMessage, std::string());
            if (!warning.empty())
                P::Row(P::Mark::Wait, "Archivos locales", warning);
        }

        Block("MIS PERFILES");
        if (mine.empty() && downloaded.empty())
            P::Coloured(IlionTheme::ShellPal::Dim, "Aún no hay perfiles. Optimiza y pulsa GUARDAR COMO PERFIL, o descarga uno en COMUNIDAD.", true);
        for (auto& p : mine)
            LibraryCard(ctx, p, m), ImGui::Spacing();
        for (auto& p : downloaded)
            LibraryCard(ctx, p, m), ImGui::Spacing();
        // Cambios en la lista, fuera del bucle que la recorre
        if (duplicateQueue.has_value())
        {
            auto& list = IsOwn(*duplicateQueue) ? mine : downloaded; // la copia conserva su procedencia
            if (C::AddOrUpdate(list, *duplicateQueue) == C::AddResult::Added)
                SaveLocalLists(), selectedId = duplicateQueue->id;
            duplicateQueue.reset();
        }
        if (!deleteQueue.empty())
        {
            // Solo la copia de este equipo: lo publicado sigue en COMUNIDAD hasta que se retire en MIS APORTES
            const C::Profile* gone = FindLocal(deleteQueue);
            const std::string name = gone != nullptr ? gone->name : std::string();
            C::RemoveLocal(mine, deleteQueue);
            C::RemoveLocal(downloaded, deleteQueue);
            SaveLocalLists();
            Say(true, "«" + name + "» eliminado de este equipo");
            if (activationId == deleteQueue)
                activationId.clear();
            if (selectedId == deleteQueue)
                selectedId.clear();
            confirmDelete.clear();
            deleteQueue.clear();
        }

        // IMPORTAR Y RECUPERAR, plegado: qué se importa o restaura y cuándo se guardó cada punto
        ImGui::Spacing();
        if (IlionTheme::shell)
            ImGui::PushStyleColor(ImGuiCol_Text, toneMapColor(IlionTheme::ShellPal::Text2));
        const bool openImport = ImGui::CollapsingHeader("IMPORTAR Y RECUPERAR##LibImportRestore");
        if (IlionTheme::shell)
            ImGui::PopStyleColor();
        if (openImport)
        {
            ImGui::Indent(font * 0.4f);
            P::Coloured(IlionTheme::ShellPal::Text2, "Importar perfil", true);
            P::Coloured(IlionTheme::ShellPal::Dim, "Desde el portapapeles (un perfil exportado con «···» → Exportar). Se añade a MIS PERFILES sin activarlo.", true);
            if (IlionTheme::Button("IMPORTAR PERFIL##LibImport"))
            {
                const char* clip = ImGui::GetClipboardText();
                C::Profile p;
                std::string err;
                if (clip == nullptr || !C::ParseProfileText(clip, p, &err))
                    Say(false, "No se importó: " + (clip == nullptr ? std::string("el portapapeles está vacío") : err));
                else
                {
                    p.origin = "importado";
                    const auto r = C::AddOrUpdate(downloaded, p);
                    Say(r == C::AddResult::Added || r == C::AddResult::Updated,
                        r == C::AddResult::Duplicate  ? "Ya tienes ese perfil"
                        : r == C::AddResult::NotOwner ? "Ya hay un perfil con ese identificador de otro autor: no se sustituye"
                        : r == C::AddResult::Full     ? "La biblioteca está llena (500)"
                                                      : "Perfil importado en MIS PERFILES. No se ha activado");
                    if (r == C::AddResult::Added || r == C::AddResult::Updated)
                        SaveLocalLists(), selectedId = p.id;
                }
            }
            ImGui::Spacing();
            P::Coloured(IlionTheme::ShellPal::Text2, "Restaurar configuración anterior", true);
            const auto point = [&](const char* id, const char* what, const std::filesystem::path& file, const char* kind, const char* label, bool restart)
            {
                const std::string when = P::Exists(file) ? FileStamp(file) : std::string();
                ImGui::BeginDisabled(when.empty());
                if (IlionTheme::Button(id))
                {
                    P::ApplyFile(ctx, file, kind, label, restart);
                    activationId.clear();
                    Say(true, std::string("Restaurada: ") + what);
                }
                ImGui::EndDisabled();
                ImGui::SameLine();
                P::Coloured(when.empty() ? IlionTheme::ShellPal::Dim : IlionTheme::ShellPal::Text, (std::string(what) + (when.empty() ? " — no hay copia" : " — guardada el " + when)).c_str(),
                            true);
            };
            point("RESTAURAR##RestoreSnap", "la configuración de antes de la última activación de un perfil", SnapshotPath(), "configuracion", "Configuración anterior a la activación", true);
            point("RESTAURAR##RestoreSession", "la de antes de la primera optimización de esta sesión", P::SessionPath(), "configuracion", "Restauración de la sesión", true);
            point("RESTAURAR##RestoreOriginal", "la original, antes de usar Perfiles en este juego", P::OriginalPath(), "configuracion", "Restauración original", true);
            ImGui::Spacing();
            P::Coloured(IlionTheme::ShellPal::Text2, "Perfil del juego", true);
            if (IlionTheme::Button("GUARDAR LO ACTUAL##ProfileSave"))
            {
                P::SaveProfile(ctx.currentFeature);
                Say(true, "Perfil del juego guardado con la configuración actual");
            }
            Flow("VOLVER A CARGARLO##ProfileLoad");
            ImGui::BeginDisabled(!P::Exists(P::ProfilePath()));
            if (IlionTheme::Button("VOLVER A CARGARLO##ProfileLoad"))
                P::ApplyFile(ctx, P::ProfilePath(), "perfil", "Perfil", false);
            ImGui::EndDisabled();
            ImGui::Unindent(font * 0.4f);
        }

        // ======== Derecha: información técnica ========
        ImGui::TableSetColumnIndex(1);
        {
            const auto& state = State::Instance();
            const GpuInformation gpu = IdentifyGpu::getPrimaryGpu();
            const P::Arch arch = P::ArchOf(gpu);
            Block("JUEGO Y EQUIPO");
            const std::string game = !state.gameName.empty() ? state.gameName : state.gameExe;
            P::Big(P::FigureFont(), 1.35f, IlionTheme::ShellPal::Text, game.empty() ? "Juego desconocido" : game.c_str(), true);
            Seg(P::VendorColour(gpu.vendorId), gpu.name.empty() ? std::string("GPU no identificada") : gpu.name, true);
            Seg(IlionTheme::ShellPal::Text2, std::string("  ·  ") + arch.name + "  ·  " + ApiId() +
                                                 StrFmt("  ·  FSR 4: %s", std::string(magic_enum::enum_name(gpu.fsr4Support)).c_str()));
            Block("CONFIGURACIÓN ACTIVA");
            P::ActiveMonitor(ctx, 0.0f, true);
            Block("SELECCIÓN DE FRAME GENERATION");
            P::FgDiagnosis();
            if (const C::Profile* s = FindLocal(selectedId); s != nullptr)
                SelectedInfo(ctx, *s, m);
            ImGui::Spacing();
            if (ImGui::TreeNode("Registro de operaciones##OpLog"))
            {
                P::OperationLog();
                ImGui::TreePop();
            }
        }
        ImGui::EndTable();
    }
    ImGui::PopStyleColor();
    cardMachine = nullptr;
}

// ---- COMUNIDAD: detalle de un perfil del catálogo ------------------------------------------------------------------
static void DetailView(const C::Profile& p, const C::Machine& m)
{
    const float font = ImGui::GetFontSize();
    if (IlionTheme::Button("< VOLVER##DetailBack"))
        detailId.clear();
    ImGui::Spacing();
    P::Big(P::FigureFont(), 1.5f, IlionTheme::ShellPal::Text, p.name.c_str(), true);
    ImGui::PushFont(P::UiFont(), std::floor(font * 1.1f));
    Seg(IlionTheme::ShellPal::Text2, "Creado por: ", true);
    Seg(IlionTheme::ShellPal::Orange, p.author);
    ImGui::PopFont();
    Seg(IlionTheme::InfoCyan, p.gameName, true);
    Seg(IlionTheme::ShellPal::Dim, "  ·  ");
    Seg(ArchVendorColour(p.arch), p.gpu);
    Seg(IlionTheme::ShellPal::Dim, "  ·  ");
    Seg(IlionTheme::ShellPal::Text2, C::NameOf(C::kArchs, p.arch));
    Seg(IlionTheme::ShellPal::Dim, "  ·  modo ");
    Seg(IlionTheme::ShellPilot(IlionTheme::ShellPal::Yellow), C::NameOf(C::kModes, p.mode));
    Seg(IlionTheme::InfoBlue, C::NameOf(C::kTechs, p.tech), true);
    if (p.model != "no_aplica")
        Seg(IlionTheme::ShellPal::Text2, std::string(" · ") + C::NameOf(C::kModels, p.model));
    Seg(IlionTheme::ShellPal::Text2, std::string(" · ") + C::NameOf(C::kPresets, p.preset) + StrFmt(" · %d×%d", p.outW, p.outH));
    if (p.fg == "ninguno")
        Seg(IlionTheme::ShellPal::Text2, "Sin Frame Generation", true);
    else
    {
        Seg(IlionTheme::ShellPilot(IlionTheme::ShellPal::Green), C::NameOf(C::kFgs, p.fg), true);
        Seg(IlionTheme::InfoBlue, StrFmt(" %dX fijo", p.multiplier));
        Seg(IlionTheme::ShellPal::Dim, std::string(" · ") + C::NameOf(C::kRoutes, p.fgRoute));
    }
    P::Coloured(IlionTheme::ShellPal::Dim, StrFmt("%lld descargas registradas · v%d", (std::max)(0LL, p.downloads), p.version).c_str());
    Block("COMPATIBILIDAD CON TU EQUIPO");
    const auto checks = C::Compatibility(p, m);
    const C::Fit fit = C::Overall(checks);
    P::Row(fit == C::Fit::Confirmed || fit == C::Fit::Partial ? P::Mark::Ok : fit == C::Fit::Incompatible ? P::Mark::Bad : P::Mark::Wait, C::FitText(fit), std::string());
    for (const auto& c : checks)
        if (!c.note)
            P::Row(c.level == C::Level::Ok ? P::Mark::Ok : c.level == C::Level::Bad && !c.component ? P::Mark::Bad : P::Mark::Wait, c.text, std::string());
    ImGui::Spacing();
    const bool have = FindLocal(p.id) != nullptr;
    ImGui::PushStyleColor(ImGuiCol_Text, toneMapColor(IlionTheme::ShellPal::Orange));
    if (IlionTheme::Button(have ? "VER EN PERFILES##DetailOpen" : "DESCARGAR##DetailDownload", ImVec2(font * 14.0f, std::floor(font * 2.0f))))
    {
        if (!have)
            DownloadProfile(p);
        selectedId = p.id;
        if (have)
            requestedSection = 8;
    }
    ImGui::PopStyleColor();
    if (have)
        P::Coloured(IlionTheme::ShellPal::Dim, "Descargado. Se activa desde PERFILES, con su comprobación y confirmación: descargar nunca lo activa.", true);
    P::Coloured(IlionTheme::ShellPal::Dim, "Las descargas indican popularidad, no rendimiento verificado.", true);
}

// ---- COMUNIDAD: EXPLORAR (una búsqueda, filtros compactos y tarjetas) --------------------------------------------
static void ExploreView(const C::Machine& m)
{
    const float font = ImGui::GetFontSize();
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float avail = ImGui::GetContentRegionAvail().x;
    ImGui::SetNextItemWidth(std::floor(avail * 0.4f));
    ImGui::InputTextWithHint("##ExploreSearch", "Buscar perfil, juego, autor o GPU", search, sizeof(search));
    const float third = std::floor((avail * 0.6f - spacing * 3.0f) / 3.0f);
    static const char* games[] = { "Todos los juegos", "Este juego" };
    static const char* gpus[] = { "Todas las GPU", "Mi arquitectura", "Mi GPU" };
    ImGui::SameLine();
    ImGui::SetNextItemWidth(third);
    if (IlionTheme::BeginCombo("##ExploreGame", games[filterGame]))
    {
        for (int i = 0; i < 2; i++)
            if (ImGui::Selectable(games[i], filterGame == i))
                filterGame = i;
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(third);
    if (IlionTheme::BeginCombo("##ExploreGpu", gpus[filterGpu]))
    {
        for (int i = 0; i < 3; i++)
            if (ImGui::Selectable(gpus[i], filterGpu == i))
                filterGpu = i;
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(third);
    if (IlionTheme::BeginCombo("##ExploreMode", filterMode < 0 ? "Todos los modos" : C::kModes[filterMode].name))
    {
        if (ImGui::Selectable("Todos los modos", filterMode < 0))
            filterMode = -1;
        for (int i = 0; i < 4; i++)
            if (ImGui::Selectable(C::kModes[i].name, filterMode == i))
                filterMode = i;
        ImGui::EndCombo();
    }
    const auto ws = worker.Get();
    ImGui::BeginDisabled(!service.valid || ws.busy);
    if (IlionTheme::Button("ACTUALIZAR##ExploreFetch"))
    {
        const auto cfg = service;
        jobKind = "catalogo";
        worker.Start("Descargando el catálogo...", [cfg]() { return C::FetchCatalog(cfg, nullptr); });
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    P::Coloured(IlionTheme::ShellPal::Dim, catalog.empty() ? "Sin perfiles publicados todavía" : StrFmt("%d perfiles publicados", (int) catalog.size()).c_str());
    ImGui::Spacing();

    // Tarjetas en dos columnas cuando hay sitio
    std::vector<const C::Profile*> shown;
    for (const C::Profile* p : ByDownloads())
        if (Matches(*p, m))
            shown.push_back(p);
    if (shown.empty())
    {
        P::Big(P::FigureFont(), 1.2f, IlionTheme::ShellPal::Text2, catalog.empty() ? "Todavía no hay perfiles compartidos" : "Ningún perfil coincide", true);
        P::Coloured(IlionTheme::ShellPal::Dim, catalog.empty() ? "Publica el tuyo con PUBLICAR PERFIL en MIS APORTES." : "Prueba con «Todos» en juego o GPU.", true);
        return;
    }
    const int cols = avail > font * 52.0f ? 2 : 1;
    if (ImGui::BeginTable("##ExploreGrid", cols, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings))
    {
        for (size_t i = 0; i < shown.size(); i++)
        {
            if (i % cols == 0)
                ImGui::TableNextRow();
            ImGui::TableSetColumnIndex((int) (i % cols));
            const C::Profile& p = *shown[i];
            Card(p, "exp", true,
                 [&]()
                 {
                     if (IlionTheme::Button("VER DETALLES"))
                         detailId = p.id;
                     const bool have = FindLocal(p.id) != nullptr;
                     Flow(have ? "DESCARGADO" : "DESCARGAR");
                     ImGui::BeginDisabled(have);
                     if (IlionTheme::Button(have ? "DESCARGADO" : "DESCARGAR"))
                         DownloadProfile(p);
                     ImGui::EndDisabled();
                 });
        }
        ImGui::EndTable();
    }
}

// ---- COMUNIDAD: abrir PUBLICAR PERFIL (desde INICIO, MIS APORTES o COMPARTIR en PERFILES) ----------------------------
static void OpenPublish(const std::string& id, int back)
{
    publishPick = id;
    cpageBack = back;
    cpage = PublishView;
    detailId.clear();
    communityMessage.clear();
}

// Una tabla compacta de perfiles del catálogo (TOP 10 o recién publicados). rankFrom > 0: numerada desde ese puesto
static void ProfileTable(const char* id, const std::vector<const C::Profile*>& rows, int rankFrom)
{
    const float font = ImGui::GetFontSize();
    ImGui::PushStyleColor(ImGuiCol_TableBorderLight, toneMapColor(IlionTheme::ShellPal::Line));
    if (ImGui::BeginTable(id, 6, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_NoSavedSettings))
    {
        ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, font * 2.0f);
        ImGui::TableSetupColumn("Perfil", ImGuiTableColumnFlags_WidthStretch, 1.6f);
        ImGui::TableSetupColumn("Juego", ImGuiTableColumnFlags_WidthStretch, 1.1f);
        ImGui::TableSetupColumn("GPU", ImGuiTableColumnFlags_WidthStretch, 1.1f);
        ImGui::TableSetupColumn("Configuración", ImGuiTableColumnFlags_WidthStretch, 1.4f);
        ImGui::TableSetupColumn("Descargas", ImGuiTableColumnFlags_WidthFixed, font * 5.5f);
        ImGui::TableHeadersRow();
        for (size_t i = 0; i < rows.size(); i++)
        {
            const C::Profile& p = *rows[i];
            ImGui::TableNextRow();
            ImGui::PushID((int) i);
            ImGui::TableSetColumnIndex(0);
            const std::string rank = rankFrom > 0 ? std::to_string(rankFrom + (int) i) : std::string("—");
            if (ImGui::Selectable((rank + "##row").c_str(), false, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap))
                detailId = p.id;
            ImGui::TableSetColumnIndex(1);
            P::Coloured(IlionTheme::ShellPal::Text, p.name.c_str());
            P::Coloured(IlionTheme::ShellPal::Orange, p.author.c_str());
            ImGui::TableSetColumnIndex(2);
            P::Coloured(IlionTheme::InfoCyan, p.gameName.c_str(), true);
            ImGui::TableSetColumnIndex(3);
            P::Coloured(ArchVendorColour(p.arch), C::NameOf(C::kArchs, p.arch), true);
            ImGui::TableSetColumnIndex(4);
            P::Coloured(IlionTheme::ShellPal::Text2,
                        (std::string(C::NameOf(C::kTechs, p.tech)) + (p.fg == "ninguno" ? std::string(" · sin FG") : StrFmt(" · %s %dX", C::NameOf(C::kFgs, p.fg), p.multiplier)))
                            .c_str(),
                        true);
            ImGui::TableSetColumnIndex(5);
            P::Coloured(IlionTheme::ShellPal::Text, StrFmt("%lld", (std::max)(0LL, p.downloads)).c_str());
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::PopStyleColor();
}

// Una tarjeta centrada y compacta para los estados vacíos de INICIO, con su botón principal
static void EmptyCard(const char* id, const char* title, const char* line, const char* button, bool crown)
{
    const float font = ImGui::GetFontSize();
    ImGui::Spacing();
    if (BeginCentered(id, font * 34.0f))
    {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, toneMapColor(IlionTheme::ShellPal::Panel));
        ImGui::PushStyleColor(ImGuiCol_Border, toneMapColor(PodiumColour(1)));
        if (ImGui::BeginChild("##EmptyCard", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding))
        {
            const float w = ImGui::GetContentRegionAvail().x;
            if (crown)
            {
                const float size = std::floor(font * 2.6f);
                Crown(ImGui::GetWindowDrawList(), ImVec2(ImGui::GetCursorScreenPos().x + (w - size) * 0.5f, ImGui::GetCursorScreenPos().y), size,
                      toneMapColor(PodiumColour(1)), 1);
                ImGui::Dummy(ImVec2(w, size));
            }
            ImGui::PushFont(P::FigureFont(), std::floor(font * 1.4f));
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (std::max)(0.0f, (w - ImGui::CalcTextSize(title).x) * 0.5f));
            P::Coloured(IlionTheme::ShellPal::Text, title);
            ImGui::PopFont();
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (std::max)(0.0f, (w - ImGui::CalcTextSize(line).x) * 0.5f));
            P::Coloured(IlionTheme::ShellPal::Text2, line);
            ImGui::Spacing();
            const float bw = (std::min)(w, font * 16.0f);
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (std::max)(0.0f, (w - bw) * 0.5f));
            ImGui::PushStyleColor(ImGuiCol_Text, toneMapColor(IlionTheme::ShellPal::Orange));
            if (IlionTheme::Button(button, ImVec2(bw, std::floor(font * 2.2f))))
                OpenPublish(std::string(), Home);
            ImGui::PopStyleColor();
        }
        ImGui::EndChild();
        ImGui::PopStyleColor(2);
    }
    ImGui::EndChild();
}

// ---- COMUNIDAD: una cifra en una tarjeta (MIS APORTES) ------------------------------------------------------------
static void StatTile(const char* id, const char* label, const std::string& value, const ImVec4& colour, float width)
{
    ImGui::PushStyleColor(ImGuiCol_ChildBg, toneMapColor(IlionTheme::ShellPal::Panel));
    ImGui::PushStyleColor(ImGuiCol_Border, toneMapColor(IlionTheme::ShellPal::Line));
    if (ImGui::BeginChild(id, ImVec2(width, 0.0f), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding,
                          ImGuiWindowFlags_NoScrollbar))
    {
        P::Big(P::FigureFont(), 2.0f, colour, value.c_str());
        P::Coloured(IlionTheme::ShellPal::Text2, label);
    }
    ImGui::EndChild();
    ImGui::PopStyleColor(2);
}

// ---- COMUNIDAD: INICIO (podio 2 · 1 · 3 con el primero destacado, y el resto del TOP 10) ---------------------------
// Clasificación: el mismo orden y criterio que MIS APORTES (C::RankByDownloads / C::RankPosition: solo con descargas reales)
static void HomeView(const C::Machine& m)
{
    const float font = ImGui::GetFontSize();
    const auto ranked = ByDownloads();
    const int podium = C::PodiumCount(catalog, C::RankByDownloads(catalog));
    if (catalog.empty())
    {
        EmptyCard("##HomeEmpty", "Todavía no hay perfiles compartidos", "Comparte tu configuración y ocupa el primer puesto.", "PUBLICAR MI PERFIL##HomePublish",
                  true);
        return;
    }
    if (podium == 0)
    {
        // Hay perfiles pero ninguno con descargas: sin podio (nada se rellena) y los publicados, sin puesto
        EmptyCard("##HomeNoDownloads", "Aún no hay descargas registradas", "El podio aparece con las primeras descargas.", "PUBLICAR MI PERFIL##HomePublish2",
                  false);
        std::vector<const C::Profile*> recent;
        for (const auto& p : catalog)
            recent.push_back(&p);
        std::stable_sort(recent.begin(), recent.end(), [](const C::Profile* a, const C::Profile* b) { return a->created > b->created; });
        if (recent.size() > 10)
            recent.resize(10);
        ImGui::Spacing();
        if (BeginCentered("##HomeRecent", font * 70.0f))
        {
            IlionTheme::SectionPlate("RECIÉN PUBLICADOS");
            ProfileTable("##Recent", recent, 0);
        }
        ImGui::EndChild();
        return;
    }

    // Podio: 2 · 1 · 3, el primero más ancho y con la corona más grande
    if (BeginCentered("##HomePodium", font * 70.0f))
    {
        const int order[3] = { 2, 1, 3 };
        const float weights[3] = { 1.0f, 1.3f, 1.0f };
        int cols = 0;
        for (int i = 0; i < 3; i++)
            cols += order[i] <= podium ? 1 : 0;
        if (ImGui::BeginTable("##Podium", cols, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings))
        {
            for (int i = 0; i < 3; i++)
                if (order[i] <= podium)
                    ImGui::TableSetupColumn(StrFmt("##p%d", i).c_str(), ImGuiTableColumnFlags_WidthStretch, weights[i]);
            ImGui::TableNextRow();
            int col = 0;
            for (int i = 0; i < 3; i++)
            {
                if (order[i] > podium)
                    continue;
                ImGui::TableSetColumnIndex(col++);
                if (order[i] != 1)
                    ImGui::Dummy(ImVec2(0.0f, font * 1.6f)); // el primero, más alto
                PodiumCard(*ranked[order[i] - 1], order[i]);
            }
            ImGui::EndTable();
        }
    }
    ImGui::EndChild();

    // Del cuarto al décimo, solo con descargas reales (el mismo criterio que el podio)
    std::vector<const C::Profile*> rest;
    int unranked = 0;
    for (int i = podium; i < (int) ranked.size(); i++)
    {
        if (ranked[i]->downloads > 0 && i < 10)
            rest.push_back(ranked[i]);
        unranked += ranked[i]->downloads > 0 ? 0 : 1;
    }
    if (!rest.empty() || unranked > 0)
    {
        ImGui::Spacing();
        if (BeginCentered("##HomeTop10", font * 70.0f))
        {
            if (!rest.empty())
            {
                IlionTheme::SectionPlate("TOP 10");
                ProfileTable("##Top10", rest, podium + 1);
            }
            if (unranked > 0)
                P::Coloured(IlionTheme::ShellPal::Dim, StrFmt("%d perfil%s sin descargas todavía, sin clasificación: están en EXPLORAR.", unranked, unranked == 1 ? "" : "es").c_str(),
                            true);
        }
        ImGui::EndChild();
    }
    (void) m;
}

// ---- COMUNIDAD: PUBLICAR PERFIL (elegir → revisar lo que será público → publicar) ---------------------------------
static void PublishScreen()
{
    const float font = ImGui::GetFontSize();
    const auto ws = worker.Get();
    const bool signedIn = session.Active() || !session.refreshToken.empty();
    if (!BeginCentered("##Publish", font * 52.0f))
    {
        ImGui::EndChild();
        return;
    }
    if (IlionTheme::Button("< VOLVER##PublishBack"))
        cpage = cpageBack == PublishView || cpageBack == AccountView ? Home : cpageBack, publishPick.clear();
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_ChildBg, toneMapColor(IlionTheme::ShellPal::Panel));
    ImGui::PushStyleColor(ImGuiCol_Border, toneMapColor(IlionTheme::ShellPal::Line));
    if (ImGui::BeginChild("##PublishCard", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding))
    {
        P::Big(P::FigureFont(), 1.3f, IlionTheme::ShellPal::Text, "PUBLICAR PERFIL");
        P::Coloured(IlionTheme::ShellPal::Text2, "Elige uno de tus perfiles, revisa exactamente lo que será público y publícalo.", true);

        // 1. Elegir
        ImGui::Spacing();
        IlionTheme::SectionPlate("1. ELIGE EL PERFIL");
        if (mine.empty())
        {
            P::Coloured(IlionTheme::ShellPal::Dim, "Aún no tienes perfiles propios. En PERFILES, pulsa GUARDAR COMO PERFIL.", true);
            if (IlionTheme::Button("IR A PERFILES##PublishToProfiles"))
                requestedSection = 8;
        }
        for (const auto& p : mine)
        {
            const bool published = std::any_of(aportes.begin(), aportes.end(), [&](const C::Profile& a) { return a.id == p.id; });
            const std::string label = p.name + "  ·  " + p.gameName + "  ·  " + C::NameOf(C::kTechs, p.tech) +
                                      (p.fg == "ninguno" ? std::string(" · sin FG") : StrFmt(" · %s %dX", C::NameOf(C::kFgs, p.fg), p.multiplier)) +
                                      (published ? "   (ya publicado: se actualizará)" : "");
            if (ImGui::Selectable((label + "##pick" + p.id).c_str(), publishPick == p.id))
                publishPick = p.id;
        }

        // 2. Revisar
        const C::Profile* pick = nullptr;
        for (const auto& p : mine)
            if (p.id == publishPick)
                pick = &p;
        if (pick != nullptr)
        {
            ImGui::Spacing();
            IlionTheme::SectionPlate("2. LO QUE SERÁ PÚBLICO");
            if (ImGui::BeginTable("##PublishFields", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg | ImGuiTableFlags_NoSavedSettings))
            {
                ImGui::TableSetupColumn("##k", ImGuiTableColumnFlags_WidthStretch, 0.4f);
                ImGui::TableSetupColumn("##v", ImGuiTableColumnFlags_WidthStretch, 0.6f);
                const auto row = [](const char* k, const std::string& v)
                {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    P::Coloured(IlionTheme::ShellPal::Text2, k);
                    ImGui::TableSetColumnIndex(1);
                    P::Coloured(IlionTheme::ShellPal::Text, v.c_str(), true);
                };
                row("Autor (alias)", session.alias.empty() ? std::string("— (falta tu alias)") : session.alias);
                row("Nombre", pick->name);
                row("Juego", pick->gameName + "  (" + pick->gameExe + ")");
                row("API", pick->api);
                row("GPU y arquitectura", pick->gpu + "  ·  " + C::NameOf(C::kArchs, pick->arch));
                row("Resolución de salida", StrFmt("%d×%d", pick->outW, pick->outH));
                row("Modo", C::NameOf(C::kModes, pick->mode));
                row("Escalado", std::string(C::NameOf(C::kTechs, pick->tech)) + (pick->model != "no_aplica" ? std::string(" · ") + C::NameOf(C::kModels, pick->model) : ""));
                row("Preset", C::NameOf(C::kPresets, pick->preset));
                row("Frame Generation", pick->fg == "ninguno" ? std::string("Sin Frame Generation")
                                                              : StrFmt("%s %dX fijo · %s", C::NameOf(C::kFgs, pick->fg), pick->multiplier, C::NameOf(C::kRoutes, pick->fgRoute)));
                ImGui::EndTable();
            }
            P::Coloured(IlionTheme::ShellPal::Dim, "Nada más: ni tu correo, ni rutas de archivos, ni datos del equipo fuera de esta lista.", true);

            // 3. Publicar
            ImGui::Spacing();
            IlionTheme::SectionPlate("3. PUBLICAR");
            const bool update = std::any_of(aportes.begin(), aportes.end(), [&](const C::Profile& a) { return a.id == pick->id; });
            if (!service.valid)
                P::Row(P::Mark::Info, "Servicio comunitario no disponible", service.problem);
            else if (!signedIn)
            {
                P::Coloured(IlionTheme::ShellPal::Text2, "La cuenta solo hace falta para publicar. Al entrar vuelves aquí con este perfil elegido.", true);
                if (IlionTheme::Button("ENTRAR PARA PUBLICAR##PublishLogin"))
                    cpageBack = PublishView, cpage = AccountView;
            }
            else if (session.alias.empty())
            {
                if (IlionTheme::Button("ELEGIR MI ALIAS##PublishAlias"))
                    cpageBack = PublishView, cpage = AccountView;
            }
            else
            {
                ImGui::BeginDisabled(ws.busy);
                ImGui::PushStyleColor(ImGuiCol_Text, toneMapColor(IlionTheme::ShellPal::Orange));
                if (IlionTheme::Button(update ? "ACTUALIZAR PUBLICACIÓN##PublishGo" : "PUBLICAR##PublishGo", ImVec2(font * 16.0f, std::floor(font * 2.2f))))
                {
                    StartPublish(*pick);
                    publishPick.clear();
                    cpage = Contributions;
                }
                ImGui::PopStyleColor();
                ImGui::EndDisabled();
            }
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleColor(2);
    ImGui::EndChild();
}

// ---- COMUNIDAD: MIS APORTES (cifras arriba y tus publicaciones en una cuadrícula) -----------------------------------
// Lo publicado sale del servidor (aportes, con caché local): borrar un perfil en PERFILES no lo retira, y retirarlo aquí no
// borra la copia local
static void ContributionsView()
{
    const float font = ImGui::GetFontSize();
    const auto ws = worker.Get();
    const bool signedIn = session.Active() || !session.refreshToken.empty();
    if (signedIn && service.valid && !statsRequested && !ws.busy)
    {
        statsRequested = true;
        RequestAuthorStats(); // el estado real de lo publicado y sus descargas
    }

    int published = 0;
    long long downloadsTotal = 0;
    std::vector<std::string> ids;
    for (const auto& a : aportes)
    {
        published += a.review == "aprobado" ? 1 : 0;
        downloadsTotal += a.review == "aprobado" ? (std::max)(0LL, a.downloads) : 0;
        ids.push_back(a.id);
    }
    const int best = C::BestPosition(catalog, ids);

    if (BeginCentered("##Contrib", font * 72.0f))
    {
        // Franja de cifras
        const float sp = ImGui::GetStyle().ItemSpacing.x;
        const float tw = std::floor((ImGui::GetContentRegionAvail().x - sp * 2.0f) / 3.0f);
        StatTile("##tPub", "Publicaciones", std::to_string(published), IlionTheme::ShellPilot(IlionTheme::ShellPal::Green), tw);
        ImGui::SameLine();
        StatTile("##tDl", "Descargas recibidas", std::to_string(downloadsTotal), PodiumColour(1), tw);
        ImGui::SameLine();
        StatTile("##tBest", "Mejor posición", best > 0 ? StrFmt("Nº %d", best) : std::string("Sin clasificación"), best > 0 ? PodiumColour((std::min)(best, 3)) : IlionTheme::ShellPal::Text2,
                 tw);

        // PUBLICAR PERFIL, siempre a la vista
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, toneMapColor(IlionTheme::ShellPal::Orange));
        if (IlionTheme::Button("PUBLICAR PERFIL##ContribPublish", ImVec2(font * 16.0f, std::floor(font * 2.2f))))
            OpenPublish(std::string(), Contributions);
        ImGui::PopStyleColor();
        if (!signedIn)
        {
            ImGui::SameLine();
            if (IlionTheme::Button("ENTRAR##ContribLogin", ImVec2(0.0f, std::floor(font * 2.2f))))
                cpageBack = Contributions, cpage = AccountView;
        }

        // Lo que se está publicando ahora y los errores de publicación (con el nombre del perfil)
        if (ws.busy && jobKind.rfind("publicar:", 0) == 0)
            StatusChip(IlionTheme::ShellPilot(IlionTheme::ShellPal::Yellow), ("Publicando «" + (publishingName) + "»…").c_str());
        for (const auto& [id, err] : publishErrors)
        {
            const C::Profile* p = FindLocal(id);
            StatusChip(IlionTheme::ShellPal::Red, ("No se publicó «" + (p != nullptr ? p->name : id) + "»: " + err).c_str());
        }

        // Tus publicaciones, en una cuadrícula
        ImGui::Spacing();
        IlionTheme::SectionPlate("TUS PUBLICACIONES");
        if (aportes.empty())
            P::Coloured(IlionTheme::ShellPal::Dim,
                        signedIn ? "Aún no has publicado. Pulsa PUBLICAR PERFIL o COMPARTIR en una tarjeta de PERFILES."
                                 : "Entra con tu cuenta para ver tus publicaciones. Explorar, descargar y activar funcionan sin cuenta.",
                        true);
        const int cols = ImGui::GetContentRegionAvail().x > font * 50.0f ? 3 : ImGui::GetContentRegionAvail().x > font * 32.0f ? 2 : 1;
        if (!aportes.empty() && ImGui::BeginTable("##ContribGrid", cols, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings))
        {
            for (size_t i = 0; i < aportes.size(); i++)
            {
                const C::Profile& p = aportes[i];
                if (i % cols == 0)
                    ImGui::TableNextRow();
                ImGui::TableSetColumnIndex((int) (i % cols));
                ImGui::PushID(p.id.c_str());
                ImGui::PushStyleColor(ImGuiCol_ChildBg, toneMapColor(IlionTheme::ShellPal::Panel));
                ImGui::PushStyleColor(ImGuiCol_Border, toneMapColor(IlionTheme::ShellPal::Line));
                if (ImGui::BeginChild("##contribCard", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding))
                {
                    P::Big(P::FigureFont(), 1.1f, IlionTheme::ShellPal::Text, p.name.c_str(), true);
                    P::Coloured(IlionTheme::InfoCyan, p.gameName.c_str(), true);
                    P::Coloured(IlionTheme::ShellPal::Text2,
                                (std::string(C::NameOf(C::kTechs, p.tech)) + (p.fg == "ninguno" ? std::string(" · sin FG") : StrFmt(" · %s %dX", C::NameOf(C::kFgs, p.fg), p.multiplier)))
                                    .c_str(),
                                true);
                    // El estado público real: «Publicado» solo si el catálogo público lo muestra
                    const bool visible = FindCatalog(p.id) != nullptr;
                    if (p.review == "rechazado")
                        StatusChip(IlionTheme::ShellPal::Red, "Retirado por moderación");
                    else if (p.review == "pendiente")
                        StatusChip(IlionTheme::ShellPilot(IlionTheme::ShellPal::Yellow), "Pendiente de moderación");
                    else if (verifying.count(p.id))
                        StatusChip(IlionTheme::ShellPilot(IlionTheme::ShellPal::Yellow), "Publicando… (comprobando el catálogo)");
                    else if (visible)
                        StatusChip(IlionTheme::ShellPilot(IlionTheme::ShellPal::Green), "Publicado");
                    else
                        StatusChip(IlionTheme::ShellPilot(IlionTheme::ShellPal::Yellow), testCatalog ? "Sin comprobar (datos de prueba)" : "Aún no aparece en el catálogo");
                    const int pos = C::RankPosition(catalog, p.id);
                    P::Coloured(IlionTheme::ShellPal::Dim,
                                StrFmt("%lld descargas · %s · v%d", (std::max)(0LL, p.downloads), pos > 0 ? StrFmt("Nº %d", pos).c_str() : "Sin clasificación", p.version).c_str(),
                                true);
                    if (visible && IlionTheme::Button("VER"))
                        detailId = p.id, cpage = Explore;
                    if (p.review != "rechazado")
                    {
                        if (withdrawId != p.id)
                        {
                            if (visible)
                                Flow("RETIRAR PUBLICACIÓN");
                            ImGui::BeginDisabled(!signedIn || ws.busy);
                            if (IlionTheme::Button("RETIRAR PUBLICACIÓN"))
                                withdrawId = p.id;
                            ImGui::EndDisabled();
                        }
                        else
                        {
                            P::Coloured(IlionTheme::ShellPal::Text2, "Se quita del catálogo. Tu copia en MIS PERFILES se conserva.", true);
                            ImGui::PushStyleColor(ImGuiCol_Text, toneMapColor(IlionTheme::ShellPal::Red));
                            if (IlionTheme::Button("CONFIRMAR RETIRADA"))
                            {
                                const std::string id = p.id;
                                RunWithSession("Retirando la publicación...", "retirar:" + id, [id](const C::ServiceConfig& cfg, C::Session& s) { return C::Withdraw(cfg, s, id); });
                                withdrawId.clear();
                            }
                            ImGui::PopStyleColor();
                            Flow("CANCELAR");
                            if (IlionTheme::Button("CANCELAR"))
                                withdrawId.clear();
                        }
                    }
                }
                ImGui::EndChild();
                ImGui::PopStyleColor(2);
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        if (signedIn && service.valid)
        {
            ImGui::Spacing();
            ImGui::BeginDisabled(ws.busy);
            if (IlionTheme::Button("ACTUALIZAR CIFRAS##ContribRefresh"))
            {
                RequestAuthorStats();
                needCatalog = true; // el puesto sale del catálogo público
            }
            ImGui::EndDisabled();
        }
    }
    ImGui::EndChild();
}

// ---- COMUNIDAD: la cuenta (desde el encabezado; no es una pestaña) ------------------------------------------------
static void AccountScreen()
{
    const float font = ImGui::GetFontSize();
    const auto ws = worker.Get();
    if (!BeginCentered("##Account", font * 40.0f))
    {
        ImGui::EndChild();
        return;
    }
    if (IlionTheme::Button("< VOLVER##AccountBack"))
        cpage = cpageBack == AccountView ? Home : cpageBack;
    const bool fromPublish = cpageBack == PublishView;
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_ChildBg, toneMapColor(IlionTheme::ShellPal::Panel));
    ImGui::PushStyleColor(ImGuiCol_Border, toneMapColor(IlionTheme::ShellPal::Line));
    if (ImGui::BeginChild("##AccountCard", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding))
    {
        P::Big(P::FigureFont(), 1.3f, IlionTheme::ShellPal::Text, "TU CUENTA");
        P::Coloured(IlionTheme::ShellPal::Text2,
                    fromPublish ? "Entra para publicar el perfil elegido; después vuelves a PUBLICAR PERFIL."
                                : "Solo hace falta para publicar y gestionar tus aportes. Explorar, descargar y activar funcionan sin cuenta.",
                    true);
        ImGui::Spacing();
        if (!service.valid)
            P::Row(P::Mark::Info, "Servicio comunitario no disponible", service.problem);
        else if (!session.refreshToken.empty() || session.Active())
        {
            P::Row(P::Mark::Ok, session.alias.empty() ? "Sesión iniciada" : "Sesión iniciada como " + session.alias,
                   session.alias.empty() ? "Falta elegir tu alias público (una sola vez)" : "Tu alias se recupera de la cuenta automáticamente");
            if (session.alias.empty())
            {
                ImGui::SetNextItemWidth(font * 16.0f);
                ImGui::InputTextWithHint("##AccountClaim", "Alias público (3 a 24)", claim, 25);
                ImGui::SameLine();
                ImGui::BeginDisabled(ws.busy);
                if (IlionTheme::Button("GUARDAR ALIAS##AccountClaimGo"))
                {
                    const std::string alias = claim;
                    RunWithSession("Guardando el alias...", "sesion", [alias](const C::ServiceConfig& cfg, C::Session& s) { return C::ClaimAlias(cfg, s, alias); });
                }
                ImGui::EndDisabled();
            }
            else if (cpageBack == PublishView)
            {
                ImGui::PushStyleColor(ImGuiCol_Text, toneMapColor(IlionTheme::ShellPal::Orange));
                if (IlionTheme::Button("CONTINUAR CON LA PUBLICACIÓN##AccountContinue"))
                    cpage = PublishView, cpageBack = Contributions;
                ImGui::PopStyleColor();
            }
            ImGui::Spacing();
            if (IlionTheme::Button("CERRAR SESIÓN##AccountLogout"))
            {
                C::ForgetSession();
                statsRequested = false;
                session = {};
                aportes.clear();
                C::SaveList(AportesPath(), aportes);
                Say(true, "Sesión cerrada en este equipo");
            }
        }
        else
        {
            P::Coloured(IlionTheme::ShellPal::Text2, "1. Tu correo", true);
            ImGui::SetNextItemWidth(font * 20.0f);
            ImGui::InputTextWithHint("##AccountEmail", "tu@correo.com", email, sizeof(email));
            ImGui::SameLine();
            ImGui::BeginDisabled(ws.busy);
            if (IlionTheme::Button("ENVIAR CÓDIGO##AccountOtp"))
            {
                const auto cfg = service;
                const std::string mail = email;
                jobKind = "codigo";
                worker.Start("Pidiendo el código...", [cfg, mail]() { return C::RequestCode(cfg, mail); });
            }
            ImGui::EndDisabled();
            P::Coloured(IlionTheme::ShellPal::Text2, "2. El código de 8 cifras que llega al correo", true);
            ImGui::SetNextItemWidth(font * 10.0f);
            ImGui::InputTextWithHint("##AccountCode", "8 cifras", code, 11, ImGuiInputTextFlags_CharsDecimal);
            ImGui::SameLine();
            ImGui::BeginDisabled(ws.busy);
            if (IlionTheme::Button("ENTRAR##AccountVerify"))
            {
                const auto cfg = service;
                const std::string mail = email, otp = code;
                *jobSession = C::Session {};
                auto js = jobSession;
                jobKind = "sesion";
                worker.Start("Verificando...", [cfg, mail, otp, js]() { return C::VerifyCode(cfg, mail, otp, *js); });
            }
            ImGui::EndDisabled();
            P::Coloured(IlionTheme::ShellPal::Dim, "Sin contraseñas. La primera vez eliges tu alias público; después se recupera solo. ILION no guarda ni publica el correo.",
                        true);
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleColor(2);
    ImGui::EndChild();
}

// ---- COMUNIDAD ILION: encabezado, cuenta, INICIO | EXPLORAR | MIS APORTES y una única zona desplazable ------------
template <typename Ctx> static void CommunityPanel(Ctx& ctx)
{
    if (!loaded)
        LoadAll();
    Collect();
    // Al volver a COMUNIDAD, sin mensajes de antes
    if (communityFrame < ImGui::GetFrameCount() - 1)
        communityMessage.clear(), withdrawId.clear();
    communityFrame = ImGui::GetFrameCount();
    sayTarget = 1;
    SpacingScope spacing;
    const float font = ImGui::GetFontSize();

    // Tras publicar o retirar, el catálogo se vuelve a leer: lo publicado se ve al momento en EXPLORAR
    if (needCatalog && service.valid && !worker.Get().busy && !testCatalog)
    {
        needCatalog = false;
        const auto cfg = service;
        jobKind = "catalogo";
        worker.Start("Actualizando el catálogo...", [cfg]() { return C::FetchCatalog(cfg, nullptr); });
    }
    // Comprobación real del servicio la primera vez (en segundo plano)
    if (!autoChecked && service.valid && !worker.Get().busy)
    {
        autoChecked = true;
        const auto cfg = service;
        *jobSession = session;
        auto js = jobSession;
        jobKind = "inicio";
        worker.Start("Comprobando el servicio...",
                     [cfg, js]()
                     {
                         if (!js->refreshToken.empty())
                             C::Refresh(cfg, *js);
                         return C::FetchCatalog(cfg, nullptr);
                     });
    }

    // La cuenta, arriba a la derecha (no es una pestaña)
    {
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const float width = ImGui::GetContentRegionAvail().x;
        const bool signedIn = session.Active() || !session.refreshToken.empty();
        const std::string label = !signedIn ? std::string("ENTRAR / CREAR CUENTA") : session.alias.empty() ? std::string("MI CUENTA") : session.alias;
        const float bw = ImGui::CalcTextSize(label.c_str()).x + font * (signedIn ? 3.2f : 2.0f);
        ImGui::SetCursorScreenPos(ImVec2(origin.x + width - bw - font * 0.4f, origin.y + font * 0.4f));
        const ImVec2 bp = ImGui::GetCursorScreenPos();
        if (IlionTheme::Button(("##AccountBtn" + label).c_str(), ImVec2(bw, std::floor(font * 1.9f))))
        {
            if (cpage != AccountView)
                cpageBack = cpage;
            cpage = AccountView;
            detailId.clear();
        }
        ImDrawList* dl = ImGui::GetWindowDrawList();
        float tx = bp.x + font;
        if (signedIn)
        {
            // Icono de usuario: cabeza y hombros
            const ImU32 ic = ImGui::GetColorU32(toneMapColor(IlionTheme::ShellPal::Orange));
            const ImVec2 c(bp.x + font * 1.15f, bp.y + font * 0.95f);
            dl->AddCircle(ImVec2(c.x, c.y - font * 0.22f), font * 0.22f, ic, 0, 1.6f);
            dl->AddBezierCubic(ImVec2(c.x - font * 0.42f, c.y + font * 0.5f), ImVec2(c.x - font * 0.38f, c.y + font * 0.05f),
                               ImVec2(c.x + font * 0.38f, c.y + font * 0.05f), ImVec2(c.x + font * 0.42f, c.y + font * 0.5f), ic, 1.6f);
            tx = bp.x + font * 1.9f;
        }
        dl->AddText(ImVec2(tx, bp.y + std::floor(font * 0.45f)), ImGui::GetColorU32(toneMapColor(signedIn ? IlionTheme::ShellPal::Orange : IlionTheme::ShellPal::Text)),
                    label.c_str());
        ImGui::SetCursorScreenPos(origin);
    }

    // ---- Cabecera: COMUNIDAD ILION ----
    // Título en la tipografía de títulos de ILION (Russo One) con espaciado entre letras, centrado con su piloto. El
    // piloto deja su halo entero dentro de la columna y separado del texto
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float width = ImGui::GetContentRegionAvail().x;
        ImFont* tf = P::FigureFont();
        const float ts = std::floor(font * 1.75f);
        const float tracking = std::floor(font * 0.16f);
        static const char* title = "COMUNIDAD ILION";
        float tw = 0.0f;
        for (const char* c = title; *c; c++)
            tw += tf->CalcTextSizeA(ts, FLT_MAX, 0.0f, c, c + 1).x + (c[1] ? tracking : 0.0f);
        const float r = std::floor(font * 0.24f);
        const float halo = r * 3.4f; // el halo más grande de ShellLed llega a 3,2 radios
        const float gap = std::floor(font * 0.85f);
        const float margin = std::floor(font * 0.7f);
        // Estrellas rojas con contorno blanco fino a cada lado del título, separadas de las letras
        const float star = std::floor(ts * 0.36f); // radio exterior
        const float starGap = std::floor(font * 0.7f);
        const float starsWidth = (star * 2.0f + starGap) * 2.0f;
        const bool stars = width >= halo * 2.0f + gap + tw + starsWidth + margin * 2.0f; // sin sitio, sin estrellas (nunca cortadas)
        const float block = halo * 2.0f + gap + tw + (stars ? starsWidth : 0.0f);
        const float x0 = p.x + (std::max)(margin, std::floor((width - block) * 0.5f));
        const float top = p.y + std::floor(font * 0.75f);
        IlionTheme::ShellLed(dl, ImVec2(x0 + halo, top + ts * 0.52f), r, toneMapColor(IlionTheme::ShellPilot(IlionTheme::ShellPal::Green)), true);
        const auto drawStar = [&](ImVec2 c)
        {
            ImVec2 pts[10];
            for (int i = 0; i < 10; i++)
            {
                const float a = -1.5707964f + i * 0.62831855f;
                const float rr = i % 2 == 0 ? star : star * 0.45f;
                pts[i] = ImVec2(c.x + std::cos(a) * rr, c.y + std::sin(a) * rr);
            }
            dl->AddConcavePolyFilled(pts, 10, ImGui::GetColorU32(toneMapColor(ImVec4(0.93f, 0.12f, 0.10f, 1.0f))));
            dl->AddPolyline(pts, 10, ImGui::GetColorU32(toneMapColor(ImVec4(1.0f, 1.0f, 1.0f, 0.95f))), ImDrawFlags_Closed, 1.0f);
        };
        float x = x0 + halo * 2.0f + gap;
        const float starY = top + ts * 0.5f;
        if (stars)
        {
            drawStar(ImVec2(x + star, starY));
            x += star * 2.0f + starGap;
            drawStar(ImVec2(x + tw + starGap + star, starY));
        }
        const ImU32 titleColour = ImGui::GetColorU32(toneMapColor(IlionTheme::ShellPal::Text));
        for (const char* c = title; *c; c++)
        {
            dl->AddText(tf, ts, ImVec2(x, top), titleColour, c, c + 1);
            x += tf->CalcTextSizeA(ts, FLT_MAX, 0.0f, c, c + 1).x + tracking;
        }
        // Subtítulo centrado
        static const char* subtitle = "Perfiles creados por nuestros usuarios";
        ImFont* uf = P::UiFont();
        const float ss = std::floor(font * 0.98f);
        const ImVec2 sz = uf->CalcTextSizeA(ss, FLT_MAX, 0.0f, subtitle);
        const float subY = top + ts + std::floor(font * 0.45f);
        dl->AddText(uf, ss, ImVec2(p.x + std::floor((width - sz.x) * 0.5f), subY), ImGui::GetColorU32(toneMapColor(IlionTheme::ShellPal::Orange)), subtitle);
        // Línea decorativa con márgenes y un tramo de acento en el centro
        const float lineY = subY + sz.y + std::floor(font * 0.6f);
        dl->AddLine(ImVec2(p.x + margin, lineY), ImVec2(p.x + width - margin, lineY), ImGui::GetColorU32(toneMapColor(IlionTheme::ShellPal::Line)), 1.0f);
        const float accent = std::floor(tw * 0.35f);
        dl->AddLine(ImVec2(p.x + std::floor((width - accent) * 0.5f), lineY), ImVec2(p.x + std::floor((width + accent) * 0.5f), lineY),
                    ImGui::GetColorU32(toneMapColor(IlionTheme::ShellPal::Orange)), 2.0f);
        ImGui::SetCursorScreenPos(ImVec2(p.x, lineY + std::floor(font * 0.6f)));
        ImGui::Dummy(ImVec2(width, 0.0f));
    }

    // ---- Estado del servicio y de la cuenta: una línea discreta (el detalle, al pasar por encima) ----
    {
        const C::ServiceState st = !service.valid ? C::ServiceState::NotConfigured : (C::ServiceState) C::LastServiceState().load();
        const ImVec4 led = st == C::ServiceState::Connected                                          ? IlionTheme::ShellPilot(IlionTheme::ShellPal::Green)
                           : st == C::ServiceState::Unchecked || st == C::ServiceState::Offline ? IlionTheme::ShellPilot(IlionTheme::ShellPal::Yellow)
                           : st == C::ServiceState::NotConfigured                               ? IlionTheme::ShellPal::Dim
                                                                                                : IlionTheme::ShellPal::Red;
        const std::string detail = st == C::ServiceState::NotConfigured ? service.problem + ". Los perfiles locales funcionan igual"
                                   : st == C::ServiceState::Offline     ? std::string("El optimizador y los perfiles descargados funcionan sin conexión")
                                   : st == C::ServiceState::KeyRejected ? std::string("Revisa anon_key en servicio.json")
                                   : st == C::ServiceState::SchemaMissing ? std::string("Falta instalar el esquema de ILION en el servicio")
                                                                          : std::string("Catálogo público de la comunidad");
        const std::string account = !service.valid                    ? std::string()
                                    : session.Active()                 ? " · " + (session.alias.empty() ? std::string("sesión iniciada") : session.alias)
                                    : !session.refreshToken.empty()    ? std::string(" · sesión guardada")
                                                                       : std::string(" · sin sesión");
        const ImVec2 lp = ImGui::GetCursorScreenPos();
        const float lh = ImGui::GetTextLineHeight();
        IlionTheme::ShellLed(ImGui::GetWindowDrawList(), ImVec2(lp.x + font * 0.7f, lp.y + lh * 0.5f), std::floor(font * 0.17f), toneMapColor(led), true);
        ImGui::SetCursorScreenPos(ImVec2(lp.x + font * 1.3f, lp.y));
        P::Coloured(IlionTheme::ShellPal::Dim, (std::string(C::ServiceStateText(st)) + account).c_str());
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", detail.c_str());
    }

    // ---- Navegación: INICIO | EXPLORAR | MIS APORTES ----
    {
        static const char* names[] = { "INICIO", "EXPLORAR", "MIS APORTES" };
        ImGui::Spacing();
        const float bw = std::floor(font * 9.5f);
        const float total = bw * 3.0f + ImGui::GetStyle().ItemSpacing.x * 2.0f;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (std::max)(0.0f, std::floor((ImGui::GetContentRegionAvail().x - total) * 0.5f)));
        for (int i = 0; i < 3; i++)
        {
            if (i > 0)
                ImGui::SameLine();
            const bool sel = cpage == i;
            if (sel)
                ImGui::PushStyleColor(ImGuiCol_Text, toneMapColor(IlionTheme::ShellPilot(IlionTheme::ShellPal::Green)));
            if (IlionTheme::Button(StrFmt("%s##CommunityTab%d", names[i], i).c_str(), ImVec2(bw, std::floor(font * 1.8f))))
            {
                if (cpage != i)
                    communityMessage.clear(), withdrawId.clear(); // cada pestaña, sin mensajes de otra
                cpage = i, detailId.clear();
            }
            if (sel)
                ImGui::PopStyleColor();
        }
    }
    // Avisos (fuera de la zona desplazable)
    {
        const auto ws = worker.Get();
        if (ws.busy)
            P::Row(P::Mark::Wait, ws.task, "En segundo plano: el juego no espera");
        else if (!communityMessage.empty())
            P::Row(communityOk ? P::Mark::Ok : P::Mark::Bad, communityMessage, std::string());
    }

    // ---- Una única zona desplazable por vista ----
    const C::Machine m = MachineOf(ctx);
    cardMachine = &m;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, toneMapColor(ImVec4(0, 0, 0, 0)));
    if (ImGui::BeginChild("##CommunityView", ImVec2(0.0f, (std::max)(ImGui::GetContentRegionAvail().y, font * 12.0f)), ImGuiChildFlags_AlwaysUseWindowPadding))
    {
        const C::Profile* detail = detailId.empty() ? nullptr : FindCatalog(detailId);
        if (testCatalog)
            P::Row(P::Mark::Wait, "DATOS DE PRUEBA LOCALES (catalogo_prueba.json)", "No proceden del servicio: bórralo para volver al catálogo real");
        if (detail != nullptr && (cpage == Home || cpage == Explore))
        {
            if (BeginCentered("##Detail", font * 56.0f))
                DetailView(*detail, m);
            ImGui::EndChild();
        }
        else if (cpage == Home)
            HomeView(m);
        else if (cpage == Explore)
            ExploreView(m);
        else if (cpage == Contributions)
            ContributionsView();
        else if (cpage == PublishView)
            PublishScreen();
        else
            AccountScreen();
        ImGui::Spacing();
        P::Coloured(IlionTheme::ShellPal::Dim,
                    "Perfiles compartidos voluntariamente y firmados por su autor. Descargar no activa: se activa en PERFILES con tu confirmación. "
                    "Sin telemetría.",
                    true);
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
    cardMachine = nullptr;
}
} // namespace IlionCommunityUi

#pragma execution_character_set(pop)

void MenuCommon::RenderCommunityPanel(RenderMenuContext& ctx) { IlionCommunityUi::CommunityPanel(ctx); }
