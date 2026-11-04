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
};
static int cpage = Home;
static int cpageBack = Home;
static int requestedSection = -1; // 8 PERFILES, 9 COMUNIDAD: lo lee la barra lateral (ilion_shell.inl)
static std::string detailId;      // perfil del catálogo abierto en COMUNIDAD
// PERFILES: la biblioteca local
static std::string selectedId;
static bool activating = false;
static bool editing = false;
static bool namingNew = false;
static char newName[48] = {};
static char editName[48] = {};
static std::string shareId;       // el perfil que se está compartiendo (PERFILES → COMUNIDAD)
static std::string confirmDelete;
static bool loaded = false;
static std::vector<C::Profile> mine, downloaded, catalog;
static std::set<std::string> favourites;
static std::string warning, message;
static bool messageOk = true;
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

static void Say(bool ok, const std::string& text)
{
    messageOk = ok;
    message = text;
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
    auto c = C::LoadList(CatalogPath(), C::kMaxCatalogBytes);
    catalog = c.profiles;
    AddWarning(c.warning);
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
    out = "Perfil activado con los mecanismos de Perfiles: " + std::string(v->name) + ", preset " + P::kPresets[presetIndex].name + ", " + fgWhat +
          (fgRestart ? " (el cambio de FG se aplica al reiniciar el juego)" : "") + ". Verde solo cuando todo corre de verdad";
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
        if (service.valid && !worker.Get().busy && (session.Active() || !session.refreshToken.empty()))
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
static void PodiumCard(const C::Profile& p, int rank)
{
    const float font = ImGui::GetFontSize();
    const ImVec4 colour = PodiumColour(rank);
    ImGui::PushID(("top" + std::to_string(rank) + p.id).c_str());
    ImGui::PushStyleColor(ImGuiCol_ChildBg, toneMapColor(IlionTheme::ShellMix(IlionTheme::ShellPal::Panel, colour, 0.07f)));
    ImGui::PushStyleColor(ImGuiCol_Border, toneMapColor(Fade(colour, 0.85f)));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.5f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(font * 0.6f, font * 0.5f));
    if (ImGui::BeginChild("##podium", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding))
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 wp = ImGui::GetWindowPos();
        const float crown = std::floor(font * 2.3f);
        // Franja luminosa del puesto en el borde izquierdo
        dl->AddRectFilled(wp, ImVec2(wp.x + font * 0.28f, wp.y + ImGui::GetWindowHeight()), ImGui::GetColorU32(toneMapColor(colour)));
        const ImVec2 start = ImGui::GetCursorScreenPos();
        Crown(dl, ImVec2(start.x + font * 0.15f, start.y + font * 0.1f), crown, toneMapColor(colour), rank);
        ImGui::Dummy(ImVec2(crown + font * 0.4f, crown));
        ImGui::SameLine();
        ImGui::BeginGroup();
        P::Big(P::FigureFont(), 1.25f, colour, StrFmt("Nº %d", rank).c_str());
        ImGui::SameLine();
        P::Big(P::UiFont(), 1.25f, IlionTheme::ShellPal::Text, p.author.c_str());
        P::Coloured(IlionTheme::ShellPal::Text2, (p.gameName + " — " + p.gpu + " — " + C::NameOf(C::kModes, p.mode)).c_str(), true);
        P::Coloured(IlionTheme::ShellPal::Text2,
                    (std::string("Escalador: ") + C::NameOf(C::kTechs, p.tech) +
                     (p.model != "no_aplica" ? std::string(" (") + C::NameOf(C::kModels, p.model) + ")" : std::string()))
                        .c_str(),
                    true);
        P::Coloured(IlionTheme::ShellPal::Text2,
                    (std::string("Frame Generation: ") + (p.fg == "ninguno" ? std::string("sin FG") : StrFmt("%s %dX fijo", C::NameOf(C::kFgs, p.fg), p.multiplier)))
                        .c_str(),
                    true);
        P::Big(P::FigureFont(), 1.1f, colour, StrFmt("%lld descargas registradas", p.downloads).c_str());
        ImGui::EndGroup();
        if (IlionTheme::Button("VER DETALLES"))
            detailId = p.id;
        Flow("DESCARGAR");
        if (IlionTheme::Button("DESCARGAR"))
            DownloadProfile(p);
        Flow(favourites.count(p.id) ? "QUITAR FAVORITO" : "GUARDAR");
        if (IlionTheme::Button(favourites.count(p.id) ? "QUITAR FAVORITO" : "GUARDAR"))
            ToggleFavourite(p.id);
    }
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
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

static void AuthorStats()
{
    const auto ws = worker.Get();
    if (!statsRequested && !ws.busy)
    {
        statsRequested = true;
        RequestAuthorStats();
    }
    int approved = 0, pending = 0, rejected = 0;
    long long total = 0;
    std::vector<const C::Profile*> published;
    for (const auto& e : mine)
        if (e.review != "local")
        {
            published.push_back(&e);
            approved += e.review == "aprobado";
            pending += e.review == "pendiente";
            rejected += e.review == "rechazado";
            if (e.review == "aprobado" && e.downloads > 0)
                total += e.downloads;
        }
    ImGui::Spacing();
    IlionTheme::SectionPlate("MI PERFIL DE CREADOR");
    P::Row(P::Mark::Info, StrFmt("Perfiles publicados: %d", (int) published.size()),
           StrFmt("%d aprobados · %d pendientes de revisión · %d rechazados", approved, pending, rejected));
    P::Row(total > 0 ? P::Mark::Ok : P::Mark::Info, StrFmt("Descargas registradas recibidas: %lld", total),
           "De tus perfiles aprobados; una por cuenta con sesión, sin contar las tuyas");
    // Reconocimientos: un puesto real en LOS 3 MÁS DESCARGADOS del catálogo
    const auto order = C::RankByDownloads(catalog);
    const int podium = C::PodiumCount(catalog, order);
    bool any = false;
    for (int i = 0; i < podium; i++)
        for (const auto* e : published)
            if (e->id == catalog[order[i]].id)
            {
                P::Row(P::Mark::Ok, StrFmt("Reconocimiento: Nº %d en LOS 3 MÁS DESCARGADOS", i + 1), e->name + " — " + e->gameName);
                any = true;
            }
    if (!any)
        P::Coloured(IlionTheme::ShellPal::Dim, "Reconocimientos: ninguno todavía. Llegan con un puesto en LOS 3 MÁS DESCARGADOS.", true);
    if (!published.empty())
    {
        ImGui::Spacing();
        P::Coloured(IlionTheme::ShellPal::Text2, "HISTORIAL DE PUBLICACIONES");
        for (const auto* e : published)
            P::Row(e->review == "aprobado" ? P::Mark::Ok : e->review == "pendiente" ? P::Mark::Wait : P::Mark::Bad, e->name + " — " + e->gameName,
                   StrFmt("%s · v%d · %s", C::NameOf(C::kReviews, e->review), e->version,
                          e->downloads >= 0 ? StrFmt("%lld descargas registradas", e->downloads).c_str() : "descargas: sin dato"));
    }
    ImGui::BeginDisabled(ws.busy);
    if (IlionTheme::Button("ACTUALIZAR MIS ESTADÍSTICAS##CommunityStats"))
        RequestAuthorStats();
    ImGui::EndDisabled();
    Flow("EDITAR EN PERFILES");
    if (IlionTheme::Button("EDITAR EN PERFILES##CommunityToProfiles"))
        requestedSection = 8;
    ImGui::Spacing();
}

// Al terminar un trabajo: se recoge su resultado en el hilo de la interfaz
static void Collect()
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
        for (const auto& r : *mineRemote)
        {
            bool found = false;
            for (auto& e : mine)
                if (e.id == r.id)
                {
                    e.review = r.review, e.version = r.version, e.downloads = r.downloads, e.authorId = r.authorId;
                    found = true;
                }
            if (!found && mine.size() < C::kMaxEntries)
                mine.push_back(r);
        }
        C::SaveList(MinePath(), mine);
    }
    if (jobKind.rfind("retirar:", 0) == 0 && ws.ok)
    {
        // Ya no existe en el servidor: la copia local vuelve a ser un perfil sin publicar
        const std::string id = jobKind.substr(8);
        for (auto& e : mine)
            if (e.id == id)
            {
                e.id = C::NewLocalId();
                e.authorId.clear();
                e.review = "retirado";
                e.downloads = -1;
            }
        C::SaveList(MinePath(), mine);
    }
    if ((jobKind == "catalogo" || jobKind == "inicio") && ws.ok)
    {
        auto c = C::LoadList(CatalogPath(), C::kMaxCatalogBytes);
        catalog = c.profiles;
        AddWarning(c.warning);
    }
    else if (jobKind == "sesion" && ws.ok)
    {
        session = *jobSession;
        email[0] = code[0] = 0;
    }
    else if (jobKind.rfind("publicar:", 0) == 0)
    {
        session = *jobSession;
        if (ws.ok && publishResult)
        {
            // El perfil local pasa a tener la identidad del servidor (id, autor, versión, estado)
            const std::string oldId = jobKind.substr(9);
            for (auto& e : mine)
                if (e.id == oldId)
                    e = *publishResult;
            C::SaveList(MinePath(), mine);
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
        const auto fgLabel = [&](const std::string& id)
        { return std::string(C::NameOf(C::kFgs, id)) + (id == "ninguno" ? "" : std::string("  (") + C::EvidenceText(C::FgEvidence(id, d.arch)) + ")"); };
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

// ---- Fila de la biblioteca: nombre destacado, autor y lo esencial; seleccionable ------------------------------------
static void LibraryRow(const C::Profile& p)
{
    const float font = ImGui::GetFontSize();
    const bool sel = selectedId == p.id;
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const float rowH = std::floor(font * 2.75f);
    ImGui::PushID(p.id.c_str());
    if (ImGui::Selectable("##libRow", sel, ImGuiSelectableFlags_AllowOverlap, ImVec2(0.0f, rowH)))
    {
        selectedId = sel ? std::string() : p.id;
        activating = editing = false;
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float w = ImGui::GetItemRectSize().x;
    dl->AddRect(start, ImVec2(start.x + w, start.y + rowH), ImGui::GetColorU32(toneMapColor(sel ? IlionTheme::ShellPal::Orange : IlionTheme::ShellPal::Line)),
                3.0f, 0, sel ? 2.0f : 1.0f);
    if (sel)
        dl->AddRectFilled(start, ImVec2(start.x + font * 0.25f, start.y + rowH), ImGui::GetColorU32(toneMapColor(IlionTheme::ShellPal::Orange)));
    const float pad = std::floor(font * 0.6f);
    dl->PushClipRect(start, ImVec2(start.x + w - pad, start.y + rowH), true);
    ImFont* tf = P::FigureFont();
    dl->AddText(tf, std::floor(font * 1.1f), ImVec2(start.x + pad, start.y + font * 0.25f), ImGui::GetColorU32(toneMapColor(IlionTheme::ShellPal::Text)), p.name.c_str());
    const std::string who = IsOwn(p) ? std::string("Tuyo") : "De " + p.author;
    const std::string line = who + "  ·  " + p.gameName + "  ·  " + C::NameOf(C::kTechs, p.tech) +
                             (p.fg == "ninguno" ? std::string("  ·  sin FG") : StrFmt("  ·  %s %dX", C::NameOf(C::kFgs, p.fg), p.multiplier));
    dl->AddText(P::UiFont(), std::floor(font * 0.9f), ImVec2(start.x + pad, start.y + font * 1.5f),
                ImGui::GetColorU32(toneMapColor(IsOwn(p) ? IlionTheme::ShellPal::Text2 : IlionTheme::ShellPal::Orange)), line.c_str());
    dl->PopClipRect();
    ImGui::PopID();
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
    {
        ImGui::Spacing();
        P::Coloured(IlionTheme::ShellPal::Text2, "RESULTADO DE LA ACTIVACIÓN");
        for (const auto& s : P::steps)
            P::Row(s.mark, s.title, s.detail);
    }
}

// ---- PERFILES: dos columnas (configuración y acciones | información técnica) con la alerta fija arriba ---------------
template <typename Ctx> static void ProfilesPanel(Ctx& ctx)
{
    if (!loaded)
        LoadAll();
    Collect();
    SpacingScope spacing;
    const float font = ImGui::GetFontSize();
    const C::Machine m = MachineOf(ctx);
    cardMachine = &m;

    // ---- Zona fija de avisos: la alerta roja / verde de Perfiles y el último mensaje, fuera de las zonas desplazables ----
    {
        const P::Saved saved = P::ReadSaved();
        P::OutcomeBanner(ctx, saved, P::ReadLive(ctx, saved));
        const auto ws = worker.Get();
        if (ws.busy)
            P::Row(P::Mark::Wait, ws.task, "En segundo plano: el juego no espera");
        else if (!message.empty())
            P::Row(messageOk ? P::Mark::Ok : P::Mark::Bad, message, std::string());
        if (!P::lastMessage.empty())
            P::Row(P::Mark::Wait, P::lastMessage, std::string());
        if (!warning.empty())
            P::Row(P::Mark::Wait, "Archivos locales", warning);
    }

    const float height = (std::max)(ImGui::GetContentRegionAvail().y, font * 18.0f);
    ImGui::PushStyleColor(ImGuiCol_TableBorderLight, toneMapColor(IlionTheme::ShellPal::Line));
    if (ImGui::BeginTable("##Profiles2", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_NoSavedSettings))
    {
        ImGui::TableSetupColumn("##config", ImGuiTableColumnFlags_WidthStretch, 0.42f);
        ImGui::TableSetupColumn("##info", ImGuiTableColumnFlags_WidthStretch, 0.58f);
        ImGui::TableNextRow();

        // ======== Izquierda: configuración y acciones ========
        ImGui::TableSetColumnIndex(0);
        if (ImGui::BeginChild("##ProfilesConfig", ImVec2(0.0f, height), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoBackground))
        {
            Block("OPTIMIZADOR INTELIGENTE");
            P::OptimizerBlock();
            // Guardar como perfil: la configuración actual, con un nombre
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
                    C::Profile p = FromCurrent(ctx);
                    p.name = newName;
                    p.author = session.alias.empty() ? "Local" : session.alias;
                    std::string err;
                    if (!C::Validate(p, &err))
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
                            Say(true, "Perfil guardado en tu biblioteca: " + p.name);
                        }
                    }
                }
                Flow("CANCELAR##SaveAsCancel");
                if (IlionTheme::Button("CANCELAR##SaveAsCancel"))
                    namingNew = false;
            }

            Block("MIS PERFILES");
            if (mine.empty() && downloaded.empty())
                P::Coloured(IlionTheme::ShellPal::Dim, "Aún no hay perfiles. Optimiza y pulsa GUARDAR COMO PERFIL, o descarga uno en COMUNIDAD.", true);
            for (const auto& p : mine)
                LibraryRow(p);
            for (const auto& p : downloaded)
                LibraryRow(p);

            // Acciones del perfil seleccionado
            C::Profile* sel = FindLocal(selectedId);
            if (sel != nullptr)
            {
                ImGui::Spacing();
                const auto checks = C::Compatibility(*sel, m);
                const bool blocked = C::Blocking(checks);
                if (!editing && !activating)
                {
                    ImGui::PushStyleColor(ImGuiCol_Text, toneMapColor(IlionTheme::ShellPal::Orange));
                    if (IlionTheme::Button("ACTIVAR##LibActivate"))
                        activating = true;
                    ImGui::PopStyleColor();
                    Flow("EDITAR##LibEdit");
                    ImGui::BeginDisabled(!IsOwn(*sel));
                    if (IlionTheme::Button("EDITAR##LibEdit"))
                    {
                        editing = true;
                        editDraft = *sel;
                        strncpy_s(editName, sel->name.c_str(), _TRUNCATE);
                    }
                    ImGui::EndDisabled();
                    if (IsOwn(*sel))
                    {
                        Flow("COMPARTIR EN COMUNIDAD##LibShare");
                        if (IlionTheme::Button("COMPARTIR EN COMUNIDAD##LibShare"))
                        {
                            if (!C::IsServerReview(sel->review))
                            {
                                sel->review = "por_publicar";
                                SaveLocalLists();
                            }
                            shareId = sel->id;
                            cpage = Contributions;
                            requestedSection = 9;
                        }
                    }
                    Flow("OPCIONES##LibMore");
                    if (IlionTheme::Button("OPCIONES##LibMore"))
                        ImGui::OpenPopup("##LibOptions");
                }
                if (activating)
                {
                    P::Row(blocked ? P::Mark::Bad : P::Mark::Wait, blocked ? "No se puede activar: falta algo en este equipo" : "Activar «" + sel->name + "»",
                           blocked ? "Los detalles están a la derecha" : "Se guarda antes tu configuración para poder volver a ella");
                    ImGui::BeginDisabled(blocked);
                    ImGui::PushStyleColor(ImGuiCol_Text, toneMapColor(IlionTheme::ShellPal::Orange));
                    if (IlionTheme::Button("ACTIVAR AHORA##LibActivateGo"))
                    {
                        std::string out;
                        Say(Apply(ctx, *sel, out), out);
                        activating = false;
                    }
                    ImGui::PopStyleColor();
                    ImGui::EndDisabled();
                    Flow("CANCELAR##LibActivateCancel");
                    if (IlionTheme::Button("CANCELAR##LibActivateCancel"))
                        activating = false;
                }
                if (editing)
                {
                    Block("EDITAR PERFIL");
                    EditFields(editDraft, m);
                    C::Profile check = editDraft;
                    check.name = editName;
                    std::string err;
                    const bool valid = C::Validate(check, &err);
                    if (!valid)
                        P::Row(P::Mark::Bad, "Falta completar: " + err, std::string());
                    if (IlionTheme::Button("RELLENAR CON LO ACTUAL##EditFill"))
                    {
                        const C::Profile keep = editDraft;
                        editDraft = FromCurrent(ctx);
                        editDraft.id = keep.id, editDraft.author = keep.author, editDraft.authorId = keep.authorId;
                        editDraft.review = keep.review, editDraft.created = keep.created, editDraft.version = keep.version;
                    }
                    Flow("GUARDAR CAMBIOS##EditSave");
                    ImGui::BeginDisabled(!valid);
                    if (IlionTheme::Button("GUARDAR CAMBIOS##EditSave"))
                    {
                        check.updated = C::NowUtc();
                        const auto r = C::AddOrUpdate(mine, check);
                        if (r == C::AddResult::Updated)
                        {
                            SaveLocalLists();
                            editing = false;
                            Say(true, C::IsServerReview(check.review) ? "Cambios guardados. Para actualizar la publicación, compártelo de nuevo"
                                                                      : "Cambios guardados");
                        }
                        else
                            Say(false, r == C::AddResult::NotOwner ? "Este perfil no es tuyo: duplícalo para tener tu versión" : "No se pudo guardar");
                    }
                    ImGui::EndDisabled();
                    Flow("CANCELAR##EditCancel");
                    if (IlionTheme::Button("CANCELAR##EditCancel"))
                        editing = false;
                }
                // Opciones secundarias, fuera de cada fila
                if (ImGui::BeginPopup("##LibOptions"))
                {
                    if (ImGui::MenuItem("Duplicar"))
                    {
                        C::Profile copy = *sel;
                        copy.id = C::NewLocalId();
                        copy.name = (copy.name.size() > 31 ? copy.name.substr(0, 31) : copy.name) + " (copia)";
                        copy.review = "local";
                        copy.authorId.clear();
                        copy.downloads = -1;
                        copy.created = copy.updated = C::NowUtc();
                        auto& list = IsOwn(*sel) ? mine : downloaded; // la copia conserva su procedencia
                        if (C::AddOrUpdate(list, copy) == C::AddResult::Added)
                        {
                            SaveLocalLists();
                            selectedId = copy.id;
                        }
                    }
                    if (ImGui::MenuItem("Exportar (portapapeles y archivo)"))
                    {
                        ImGui::SetClipboardText(C::ToJson(*sel).dump(1).c_str());
                        const auto file = C::Root() / L"exportados" / (string_to_wstring(sel->id) + L".ilionprofile.json");
                        Say(C::WriteAtomic(file, C::ToJson(*sel).dump(1)), "Copiado al portapapeles y exportado a " + wstring_to_string(file.wstring()));
                    }
                    if (ImGui::MenuItem("Eliminar de la biblioteca"))
                        confirmDelete = sel->id;
                    ImGui::EndPopup();
                }
                if (confirmDelete == sel->id)
                {
                    P::Row(P::Mark::Wait, "¿Eliminar «" + sel->name + "» de la biblioteca?",
                           C::IsServerReview(sel->review) ? "Su publicación en la comunidad no se retira: hazlo en MIS APORTES" : std::string());
                    if (IlionTheme::Button("ELIMINAR##LibDeleteGo"))
                    {
                        const std::string id = sel->id;
                        const auto drop = [&](std::vector<C::Profile>& l)
                        { l.erase(std::remove_if(l.begin(), l.end(), [&](const C::Profile& e) { return e.id == id; }), l.end()); };
                        drop(mine);
                        drop(downloaded);
                        SaveLocalLists();
                        selectedId.clear();
                        confirmDelete.clear();
                    }
                    Flow("CANCELAR##LibDeleteCancel");
                    if (IlionTheme::Button("CANCELAR##LibDeleteCancel"))
                        confirmDelete.clear();
                }
            }

            // Importar y restaurar: siempre a mano, en un bloque aparte
            Block("IMPORTAR Y RESTAURAR");
            if (IlionTheme::Button("IMPORTAR DEL PORTAPAPELES##LibImport"))
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
                                                      : "Perfil importado y validado. No se ha activado");
                    if (r == C::AddResult::Added || r == C::AddResult::Updated)
                        SaveLocalLists(), selectedId = p.id;
                }
            }
            Flow("RESTAURAR##LibRestore");
            if (IlionTheme::Button("RESTAURAR##LibRestore"))
                ImGui::OpenPopup("##LibRestoreMenu");
            if (ImGui::BeginPopup("##LibRestoreMenu"))
            {
                if (ImGui::MenuItem("La configuración anterior a la última activación", nullptr, false, P::Exists(SnapshotPath())))
                {
                    P::ApplyFile(ctx, SnapshotPath(), "configuracion", "Configuración anterior a la activación", true);
                    applied.clear();
                }
                if (ImGui::MenuItem("La de antes de la primera optimización de esta sesión", nullptr, false, P::Exists(P::SessionPath())))
                    P::ApplyFile(ctx, P::SessionPath(), "configuracion", "Restauración de la sesión", true);
                if (ImGui::MenuItem("La original (antes de usar Perfiles en este juego)", nullptr, false, P::Exists(P::OriginalPath())))
                    P::ApplyFile(ctx, P::OriginalPath(), "configuracion", "Restauración original", true);
                ImGui::Separator();
                if (ImGui::MenuItem("Guardar la configuración actual en el perfil del juego"))
                {
                    P::SaveProfile(ctx.currentFeature);
                    P::lastMessage = "Perfil del juego guardado con la configuración actual";
                }
                if (ImGui::MenuItem("Volver a cargar el perfil del juego", nullptr, false, P::Exists(P::ProfilePath())))
                    P::ApplyFile(ctx, P::ProfilePath(), "perfil", "Perfil", false);
                ImGui::EndPopup();
            }
            ImGui::Spacing();
            P::Coloured(IlionTheme::ShellPal::Dim,
                        "Perfiles configura el juego al pulsar un botón; nada cambia solo durante la partida. Ni el MFG dinámico ni Neural "
                        "Rendering forman parte de los perfiles.",
                        true);
        }
        ImGui::EndChild();

        // ======== Derecha: información técnica ========
        ImGui::TableSetColumnIndex(1);
        if (ImGui::BeginChild("##ProfilesInfo", ImVec2(0.0f, height), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoBackground))
        {
            const auto& state = State::Instance();
            const GpuInformation gpu = IdentifyGpu::getPrimaryGpu();
            const P::Arch arch = P::ArchOf(gpu);
            Block("JUEGO Y EQUIPO");
            const std::string game = !state.gameName.empty() ? state.gameName : state.gameExe;
            P::Big(P::FigureFont(), 1.35f, IlionTheme::ShellPal::Text, game.empty() ? "Juego desconocido" : game.c_str(), true);
            Seg(P::VendorColour(gpu.vendorId), gpu.name.empty() ? std::string("GPU no identificada") : gpu.name, true);
            Seg(IlionTheme::ShellPal::Text2, std::string("  ·  ") + arch.name + "  ·  " + ApiId());
            P::Coloured(IlionTheme::ShellPal::Dim, StrFmt("FSR 4 en esta GPU: %s · valores detectados", std::string(magic_enum::enum_name(gpu.fsr4Support)).c_str()).c_str(),
                        true);
            Block("CONFIGURACIÓN ACTIVA");
            P::ActiveMonitor(ctx, 0.0f, true);
            Block("SELECCIÓN DE FRAME GENERATION");
            P::FgDiagnosis();
            if (const C::Profile* s = FindLocal(selectedId); s != nullptr)
                SelectedInfo(ctx, *s, m);
            Block("REGISTRO DE OPERACIONES");
            P::OperationLog();
        }
        ImGui::EndChild();
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

// ---- COMUNIDAD: INICIO (podio oro / plata / bronce, los diez siguientes e invitación) ------------------------------
static void HomeView(const C::Machine& m)
{
    const float font = ImGui::GetFontSize();
    const auto ranked = ByDownloads();
    const int podium = C::PodiumCount(catalog, C::RankByDownloads(catalog));
    Block("LOS 3 MÁS DESCARGADOS");
    if (podium == 0)
    {
        ImGui::Spacing();
        P::Big(P::FigureFont(), 1.4f, IlionTheme::ShellPal::Text, catalog.empty() ? "El podio te espera" : "Aún no hay descargas registradas", true);
        P::Coloured(IlionTheme::ShellPal::Text2,
                    catalog.empty() ? "Todavía no hay perfiles aprobados. Comparte tu configuración desde PERFILES y sé el primero de la clasificación."
                                    : "El podio aparece con las primeras descargas registradas.",
                    true);
        if (IlionTheme::Button("IR A PERFILES PARA COMPARTIR##HomeToProfiles"))
            requestedSection = 8;
    }
    else
    {
        // Lado a lado cuando hay sitio: 2 · 1 · 3 al estilo podio no; en orden de puesto, legible
        const float avail = ImGui::GetContentRegionAvail().x;
        const bool row = avail > font * 48.0f;
        if (row && ImGui::BeginTable("##Podium", podium, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings))
        {
            ImGui::TableNextRow();
            for (int i = 0; i < podium; i++)
            {
                ImGui::TableSetColumnIndex(i);
                PodiumCard(*ranked[i], i + 1);
            }
            ImGui::EndTable();
        }
        else
            for (int i = 0; i < podium; i++)
                PodiumCard(*ranked[i], i + 1), ImGui::Spacing();
    }
    // Los diez siguientes, en lista compacta
    if ((int) ranked.size() > podium)
    {
        Block("CLASIFICACIÓN");
        for (int i = podium; i < (int) ranked.size() && i < podium + 10; i++)
        {
            const C::Profile& p = *ranked[i];
            ImGui::PushID(i);
            if (ImGui::Selectable(StrFmt("%2d.  %s  —  %s  ·  %s  ·  %lld descargas##rank", i + 1, p.name.c_str(), p.author.c_str(), p.gameName.c_str(),
                                         (std::max)(0LL, p.downloads))
                                      .c_str()))
                detailId = p.id;
            ImGui::PopID();
        }
    }
    P::Coloured(IlionTheme::ShellPal::Dim,
                "Criterio: descargas registradas (una por cuenta y perfil, sin contar al autor); a igualdad, el publicado antes. "
                "Popularidad, no rendimiento.",
                true);
    (void) m;
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
        P::Big(P::FigureFont(), 1.2f, IlionTheme::ShellPal::Text2, catalog.empty() ? "Todavía no hay perfiles aprobados" : "Ningún perfil coincide", true);
        P::Coloured(IlionTheme::ShellPal::Dim, catalog.empty() ? "Comparte el tuyo desde PERFILES." : "Prueba con «Todos» en juego o GPU.", true);
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

// ---- COMUNIDAD: MIS APORTES (lo enviado a la comunidad y su estado; el perfil que se está compartiendo) ------------
static void ContributionsView()
{
    const float font = ImGui::GetFontSize();
    const auto ws = worker.Get();
    const bool signedIn = session.Active() || !session.refreshToken.empty();
    // 1. Lo que se está compartiendo: un solo paso con sesión y alias; sin sesión, la cuenta y vuelta aquí
    for (auto& p : mine)
    {
        if (p.review != "por_publicar" && p.id != shareId)
            continue;
        if (C::IsServerReview(p.review) && p.id != shareId)
            continue;
        Block("COMPARTIR EN LA COMUNIDAD");
        P::Big(P::FigureFont(), 1.3f, IlionTheme::ShellPal::Text, p.name.c_str(), true);
        P::Coloured(IlionTheme::ShellPal::Text2, TechLine(p).c_str(), true);
        P::Coloured(IlionTheme::ShellPal::Dim,
                    "Se envía: nombre, juego, API, GPU, arquitectura, resolución y la configuración. Nada más: ni FPS, ni datos del equipo, ni el correo.",
                    true);
        if (!signedIn)
        {
            P::Row(P::Mark::Wait, "Para publicar necesitas una cuenta", "Entra con tu correo: el perfil sigue preparado y vuelves aquí");
            if (IlionTheme::Button("ENTRAR / CREAR CUENTA##ShareLogin"))
                cpageBack = Contributions, cpage = AccountView;
        }
        else if (session.alias.empty())
        {
            P::Row(P::Mark::Wait, "Falta tu alias público", "Se elige una sola vez en tu cuenta");
            if (IlionTheme::Button("ELEGIR ALIAS##ShareAlias"))
                cpageBack = Contributions, cpage = AccountView;
        }
        else
        {
            P::Coloured(IlionTheme::ShellPal::Text2, ("Se publicará como " + session.alias).c_str());
            ImGui::BeginDisabled(ws.busy);
            ImGui::PushStyleColor(ImGuiCol_Text, toneMapColor(IlionTheme::ShellPal::Orange));
            if (IlionTheme::Button(StrFmt("%s##ShareSend", C::IsServerReview(p.review) ? "ENVIAR LA ACTUALIZACIÓN A MODERACIÓN" : "ENVIAR A MODERACIÓN").c_str(),
                                   ImVec2(font * 22.0f, std::floor(font * 2.0f))))
            {
                const auto cfg = service;
                *jobSession = session;
                auto js = jobSession;
                auto prof = std::make_shared<C::Profile>(p);
                jobKind = "publicar:" + p.id;
                publishResult = prof;
                worker.Start("Enviando a moderación...",
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
            ImGui::PopStyleColor();
            ImGui::EndDisabled();
        }
        Flow("NO COMPARTIR##ShareCancel");
        if (IlionTheme::Button("NO COMPARTIR##ShareCancel"))
        {
            if (p.review == "por_publicar")
                p.review = "local";
            shareId.clear();
            SaveLocalLists();
        }
        break; // uno cada vez
    }

    // 2. Mis aportes: lo publicado y su estado (no la biblioteca local)
    Block("MIS APORTES");
    std::vector<C::Profile*> mineSent;
    for (auto& p : mine)
        if (C::IsServerReview(p.review) || p.review == "retirado")
            mineSent.push_back(&p);
    if (signedIn && service.valid)
    {
        ImGui::BeginDisabled(ws.busy);
        if (IlionTheme::Button("ACTUALIZAR ESTADO##ContribRefresh"))
            RequestAuthorStats();
        ImGui::EndDisabled();
    }
    if (mineSent.empty())
        P::Coloured(IlionTheme::ShellPal::Dim, "Todavía no has enviado perfiles. Selecciona uno en PERFILES y pulsa COMPARTIR EN COMUNIDAD.", true);
    for (C::Profile* p : mineSent)
    {
        const bool approved = p->review == "aprobado", pending = p->review == "pendiente", rejected = p->review == "rechazado";
        P::Row(approved ? P::Mark::Ok : pending ? P::Mark::Wait : rejected ? P::Mark::Bad : P::Mark::Info, p->name + "  —  " + p->gameName,
               StrFmt("%s · v%d%s", C::NameOf(C::kReviews, p->review), p->version,
                      p->downloads >= 0 ? StrFmt(" · %lld descargas registradas", p->downloads).c_str() : ""));
        if (C::IsServerReview(p->review))
        {
            ImGui::PushID(p->id.c_str());
            ImGui::Indent(font * 1.4f);
            if (withdrawId != p->id)
            {
                ImGui::BeginDisabled(!signedIn || ws.busy);
                if (IlionTheme::Button("RETIRAR"))
                    withdrawId = p->id;
                ImGui::EndDisabled();
            }
            else
            {
                P::Coloured(IlionTheme::ShellPal::Text2, "Se borra del catálogo; tu copia local se conserva.", true);
                if (IlionTheme::Button("CONFIRMAR RETIRADA"))
                {
                    const std::string id = p->id;
                    RunWithSession("Retirando el perfil...", "retirar:" + id, [id](const C::ServiceConfig& cfg, C::Session& s) { return C::Withdraw(cfg, s, id); });
                    withdrawId.clear();
                }
                Flow("CANCELAR");
                if (IlionTheme::Button("CANCELAR"))
                    withdrawId.clear();
            }
            ImGui::Unindent(font * 1.4f);
            ImGui::PopID();
        }
    }
    if (signedIn && !session.alias.empty())
        AuthorStats();
}

// ---- COMUNIDAD: la cuenta (no es una pestaña: se abre desde el encabezado) ---------------------------------------
static void AccountScreen()
{
    const auto ws = worker.Get();
    if (IlionTheme::Button("< VOLVER##AccountBack"))
        cpage = cpageBack == AccountView ? Home : cpageBack;
    Block("TU CUENTA");
    P::Coloured(IlionTheme::ShellPal::Text2,
                "La cuenta solo hace falta para publicar y gestionar tus aportes. Explorar, descargar y activar perfiles funcionan sin ella.", true);
    P::Coloured(IlionTheme::ShellPal::Dim,
                "Sin contraseñas: entras con un código de un solo uso que llega a tu correo. Tu alias público se elige una vez y queda ligado "
                "a la cuenta; ILION no guarda ni publica el correo.",
                true);
    if (!service.valid)
    {
        P::Row(P::Mark::Info, "Servicio comunitario no disponible", service.problem);
        return;
    }
    if (!session.refreshToken.empty() || session.Active())
    {
        P::Row(P::Mark::Ok, session.alias.empty() ? "Sesión iniciada" : "Sesión iniciada como " + session.alias,
               session.alias.empty() ? "Elige tu alias público para poder publicar" : "Tu alias público se recupera de la cuenta en cada inicio");
        if (session.alias.empty())
        {
            P::Coloured(IlionTheme::ShellPal::Text2, "Alias público (3 a 24 caracteres; se elige una sola vez)", true);
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 18.0f);
            ImGui::InputText("##AccountClaim", claim, 25);
            ImGui::BeginDisabled(ws.busy);
            if (IlionTheme::Button("GUARDAR ALIAS##AccountClaimGo"))
            {
                const std::string alias = claim;
                RunWithSession("Reservando el alias...", "sesion", [alias](const C::ServiceConfig& cfg, C::Session& s) { return C::ClaimAlias(cfg, s, alias); });
            }
            ImGui::EndDisabled();
        }
        else if (std::any_of(mine.begin(), mine.end(), [](const C::Profile& p) { return p.review == "por_publicar"; }))
        {
            ImGui::PushStyleColor(ImGuiCol_Text, toneMapColor(IlionTheme::ShellPal::Orange));
            if (IlionTheme::Button("CONTINUAR CON LA PUBLICACIÓN##AccountContinue"))
                cpage = Contributions;
            ImGui::PopStyleColor();
        }
        ImGui::Spacing();
        if (IlionTheme::Button("CERRAR SESIÓN##AccountLogout"))
        {
            C::ForgetSession();
            statsRequested = false;
            session = {};
            Say(true, "Sesión cerrada en este equipo");
        }
        return;
    }
    Block("ENTRAR O CREAR CUENTA");
    P::Coloured(IlionTheme::ShellPal::Text2, "1. Escribe tu correo y pulsa ENVIAR CÓDIGO", true);
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 22.0f);
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
    P::Coloured(IlionTheme::ShellPal::Text2, "2. Escribe el código que llega al correo (todas sus cifras) y pulsa VERIFICAR", true);
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12.0f);
    ImGui::InputTextWithHint("##AccountCode", "código", code, sizeof(code), ImGuiInputTextFlags_CharsDecimal);
    ImGui::SameLine();
    ImGui::BeginDisabled(ws.busy);
    if (IlionTheme::Button("VERIFICAR##AccountVerify"))
    {
        const auto cfg = service;
        const std::string mail = email, otp = code;
        *jobSession = C::Session {};
        auto js = jobSession;
        jobKind = "sesion";
        worker.Start("Verificando...", [cfg, mail, otp, js]() { return C::VerifyCode(cfg, mail, otp, *js); });
    }
    ImGui::EndDisabled();
    P::Coloured(IlionTheme::ShellPal::Dim, "3. La primera vez, elige tu alias público. Si ya tienes cuenta, se recupera solo.", true);
}

// ---- COMUNIDAD ILION: encabezado, cuenta, INICIO | EXPLORAR | MIS APORTES y una única zona desplazable ------------
template <typename Ctx> static void CommunityPanel(Ctx& ctx)
{
    if (!loaded)
        LoadAll();
    Collect();
    SpacingScope spacing;
    const float font = ImGui::GetFontSize();

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
                cpage = i, detailId.clear();
            if (sel)
                ImGui::PopStyleColor();
        }
    }
    // Avisos (fuera de la zona desplazable)
    {
        const auto ws = worker.Get();
        if (ws.busy)
            P::Row(P::Mark::Wait, ws.task, "En segundo plano: el juego no espera");
        else if (!message.empty())
            P::Row(messageOk ? P::Mark::Ok : P::Mark::Bad, message, std::string());
    }

    // ---- Una única zona desplazable por vista ----
    const C::Machine m = MachineOf(ctx);
    cardMachine = &m;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, toneMapColor(ImVec4(0, 0, 0, 0)));
    if (ImGui::BeginChild("##CommunityView", ImVec2(0.0f, (std::max)(ImGui::GetContentRegionAvail().y, font * 12.0f)), ImGuiChildFlags_AlwaysUseWindowPadding))
    {
        const C::Profile* detail = detailId.empty() ? nullptr : FindCatalog(detailId);
        if (detail != nullptr && (cpage == Home || cpage == Explore))
            DetailView(*detail, m);
        else if (cpage == Home)
            HomeView(m);
        else if (cpage == Explore)
            ExploreView(m);
        else if (cpage == Contributions)
            ContributionsView();
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
