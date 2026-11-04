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

enum Page
{
    Top, // más descargados
    Catalog,
    Mine,
    Downloaded,
    Publish,
    Account,
};

static int page = Top;
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

// Vista previa
static std::string previewKey; // "<lista>:<id>"
static C::Profile preview;
static bool confirmApply = false;
static std::string applied; // id aplicado en esta sesión

// Formulario
static C::Profile draft;
static char draftAlias[32] = {};
static char draftName[48] = {};
static std::string draftError;
static bool draftReady = false;
static bool confirmPublish = false;
static std::string publishId;
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

// ---- El equipo actual ----------------------------------------------------------------------------------------------
static const char* ArchId(const P::Arch& a)
{
    switch (a.family)
    {
    case P::ArchFamily::Rdna4:
        return "rdna4";
    case P::ArchFamily::Rdna3:
        return "rdna3";
    case P::ArchFamily::Rdna2:
        return "rdna2";
    case P::ArchFamily::AmdApu:
        return "amd_apu";
    case P::ArchFamily::AmdOther:
        return "amd_old";
    case P::ArchFamily::NvGtx:
        return "nv_gtx";
    case P::ArchFamily::Rtx20:
        return "rtx20";
    case P::ArchFamily::Rtx30:
        return "rtx30";
    case P::ArchFamily::Rtx40:
        return "rtx40";
    case P::ArchFamily::Rtx50:
        return "rtx50";
    case P::ArchFamily::ArcA:
        return "arc_a";
    case P::ArchFamily::ArcB:
        return "arc_b";
    case P::ArchFamily::IntelXe2:
        return "intel_xe2";
    case P::ArchFamily::IntelOther:
        return "intel_other";
    default:
        return "";
    }
}

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
    if (v.backend == Upscaler::DLSS)
        return "dlss";
    if (v.backend == Upscaler::XeSS)
        return "xess";
    if (v.family == FamFsr31)
        return "fsr31";
    if (v.backend == Upscaler::FSR22)
        return "fsr22";
    return "";
}

static const UpscalerVariant* VariantFor(const UpscalerCatalog& catalogue, const std::string& tech)
{
    for (const auto& v : catalogue.variants)
        if (v.usable && TechOf(v) == tech)
            return &v;
    return nullptr;
}

template <typename Ctx> static C::Machine MachineOf(Ctx& ctx)
{
    C::Machine m;
    const auto& state = State::Instance();
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
    const auto status = IlionDynamicMfg::Get();
    m.fgs.insert("ninguno");
    std::error_code ec;
    if (status.provider == IlionDynamicMfg::Provider::XeFG || std::filesystem::exists(Util::DllPath().parent_path() / L"libxess_fg.dll", ec))
        m.fgs.insert("xefg");
    if (state.api == API::DX12 || status.provider == IlionDynamicMfg::Provider::FsrFg)
        m.fgs.insert("fsr_fg");
    if (status.provider == IlionDynamicMfg::Provider::DlssgGame || StreamlineHooks::isDlssgHooked())
        m.fgs.insert("dlssg_juego");
    if (status.provider == IlionDynamicMfg::Provider::DlssgOutput)
        m.fgs.insert("dlssg_salida");
    return m;
}

// La configuración activa, para rellenar el formulario (el usuario la revisa antes de guardar o publicar)
template <typename Ctx> static C::Profile FromCurrent(Ctx& ctx)
{
    C::Profile p;
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
    p.model = p.tech == "fsr4_driver" || p.tech == "fsr4_dll" ? (P::SelectedModel() == P::Model::Lite ? "int8lite" : "completo") : "no_aplica";
    const int preset = f != nullptr && f->IsInited() && f->RenderWidth() > 0 ? P::PresetOf((float) f->TargetWidth() / (float) f->RenderWidth()) : 2;
    p.preset = C::kPresets[std::clamp(preset, 0, 5)].id;
    const auto status = IlionDynamicMfg::Get();
    const bool gen = status.gen == IlionDynamicMfg::Gen::Detected || status.gen == IlionDynamicMfg::Gen::Confirmed;
    p.fg = status.provider == IlionDynamicMfg::Provider::XeFG          ? "xefg"
           : status.provider == IlionDynamicMfg::Provider::FsrFg       ? "fsr_fg"
           : status.provider == IlionDynamicMfg::Provider::DlssgGame   ? "dlssg_juego"
           : status.provider == IlionDynamicMfg::Provider::DlssgOutput ? "dlssg_salida"
                                                                       : "ninguno";
    p.multiplier = p.fg == "ninguno" ? 0 : std::clamp(gen && status.multiplier > 0 ? status.multiplier : 2, 2, std::max(2, C::FgMaxMultiplier(p.fg, p.arch)));
    p.created = p.updated = C::NowUtc();
    return p;
}

// ---- Aplicar un perfil: reutiliza Perfiles ----------------------------------------------------------------------------
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
        out = "No se aplica: hay incompatibilidades con tu equipo (ver la lista)";
        return false;
    }
    IFeature* f = ctx.currentFeature;
    const auto catalogue = GatherUpscalers(f);
    const UpscalerVariant* v = VariantFor(catalogue, p.tech);
    if (v == nullptr)
    {
        out = std::string("No instalada: ") + C::NameOf(C::kTechs, p.tech);
        return false;
    }
    auto config = Config::Instance();
    const auto& state = State::Instance();
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

    // Frame Generation fijo (nunca el MFG dinámico)
    bool fgRestart = false;
    const auto outName = [](FGOutput o) { return std::string(magic_enum::enum_name(o)); };
    const auto setOutput = [&](FGOutput o)
    {
        ini.SetValue(s, "fg_salida", outName(o).c_str());
        if (config->FGInput.value_or_default() == FGInput::NoFG)
            ini.SetValue(s, "fg_entrada", std::string(magic_enum::enum_name(state.streamlineVersion.major >= 2 ? FGInput::DLSSG : FGInput::Upscaler)).c_str());
        fgRestart = state.activeFgOutput != o;
    };
    if (p.fg == "ninguno")
        ini.SetValue(s, "fg_activado", "false");
    else
    {
        ini.SetValue(s, "fg_activado", "true");
        const std::string count = std::to_string(p.multiplier - 1);
        if (p.fg == "xefg")
        {
            setOutput(FGOutput::XeFG);
            ini.SetValue(s, "xefg_multiplicador_fijo", count.c_str());
        }
        else if (p.fg == "fsr_fg")
            setOutput(FGOutput::FSRFG);
        else if (p.fg == "dlssg_salida")
        {
            setOutput(FGOutput::DLSSG);
            ini.SetValue(s, "dlssg_salida_multiplicador_fijo", count.c_str());
        }
        else if (p.fg == "dlssg_juego")
            ini.SetValue(s, "dlssg_multiplicador_fijo", count.c_str());
        ini.SetValue(s, "fg_multiplicador", StrFmt("%dX", p.multiplier).c_str());
    }
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
    out = "Perfil activado con los mecanismos de Perfiles: escalador, modelo, preset y FG fijo escritos en la configuración. "
          "El resultado real se comprueba abajo: verde solo cuando todo corre de verdad";
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

static std::string KindOf(const C::Profile& p)
{
    if (p.origin == "comunidad")
        return p.review == "aprobado" ? "Perfil comunitario · revisado por moderación" : "Perfil comunitario";
    if (p.origin == "importado")
        return "Perfil importado";
    return p.review == "local" ? std::string("Perfil local") : StrFmt("Perfil local · publicado (%s)", C::NameOf(C::kReviews, p.review));
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
        if (position > 0)
        {
            P::Big(P::FigureFont(), 1.15f, IlionTheme::ShellPal::Text2, StrFmt("Nº %d", position).c_str());
            ImGui::SameLine();
        }
        P::Big(P::UiFont(), 1.3f, IlionTheme::ShellPal::Text, p.author.c_str());
        ImGui::SameLine();
        P::Coloured(IlionTheme::ShellPal::Dim, ("· " + p.name).c_str(), true);
        P::Coloured(IlionTheme::ShellPal::Orange, (p.gameName + " — " + p.gpu + " — " + C::NameOf(C::kModes, p.mode)).c_str(), true);
        P::Coloured(IlionTheme::ShellPal::Text2, TechLine(p).c_str(), true);
        std::string badges = KindOf(p);
        badges += StrFmt(" · v%d", p.version);
        if (showDownloads)
            badges += p.downloads >= 0 ? StrFmt(" · %lld descargas", p.downloads) : std::string(" · descargas: sin dato");
        if (favourites.count(p.id))
            badges += " · FAVORITO";
        P::Coloured(IlionTheme::ShellPal::Dim, badges.c_str(), true);
        (void) font;
        actions();
    }
    ImGui::EndChild();
    ImGui::PopStyleColor(2);
    ImGui::PopID();
}

static void DownloadProfile(const C::Profile& p);

template <typename Ctx> static void Preview(Ctx& ctx)
{
    if (previewKey.empty())
        return;
    const float font = ImGui::GetFontSize();
    ImGui::Spacing();
    IlionTheme::SectionPlate("VISTA PREVIA");
    P::Big(P::UiFont(), 1.4f, IlionTheme::ShellPal::Text, preview.author.c_str());
    P::Coloured(IlionTheme::ShellPal::Orange, (preview.gameName + " — " + preview.gpu + " — " + C::NameOf(C::kModes, preview.mode)).c_str(), true);
    P::Coloured(IlionTheme::ShellPal::Dim,
                StrFmt("%s · %s · %s · id %s · %s", preview.name.c_str(), KindOf(preview).c_str(), C::NameOf(C::kArchs, preview.arch), preview.id.c_str(),
                       preview.updated.empty() ? preview.created.c_str() : preview.updated.c_str())
                    .c_str(),
                true);

    // Configuración frente a la actual
    IFeature* f = ctx.currentFeature;
    const auto catalogue = GatherUpscalers(f);
    const UpscalerVariant* cur = P::Current(catalogue);
    const C::Profile now = FromCurrent(ctx);
    const auto diff = [&](const char* what, const std::string& want, const std::string& have)
    {
        const bool same = want == have;
        P::Row(same ? P::Mark::Ok : P::Mark::Wait, StrFmt("%s: %s", what, want.c_str()), same ? std::string("igual que ahora") : "ahora: " + have);
    };
    ImGui::Spacing();
    P::Coloured(IlionTheme::ShellPal::Text2, "CONFIGURACIÓN Y DIFERENCIAS CON LA ACTUAL");
    diff("Escalador", C::NameOf(C::kTechs, preview.tech), cur != nullptr ? cur->name : "ninguno");
    if (preview.model != "no_aplica")
        diff("Modelo", C::NameOf(C::kModels, preview.model), C::NameOf(C::kModels, now.model));
    diff("Preset", C::NameOf(C::kPresets, preview.preset), C::NameOf(C::kPresets, now.preset));
    diff("Frame Generation", preview.fg == "ninguno" ? std::string("sin FG") : StrFmt("%s %dX fijo", C::NameOf(C::kFgs, preview.fg), preview.multiplier),
         now.fg == "ninguno" ? std::string("sin FG") : StrFmt("%s %dX", C::NameOf(C::kFgs, now.fg), now.multiplier));

    // Compatibilidad real
    ImGui::Spacing();
    P::Coloured(IlionTheme::ShellPal::Text2, "COMPATIBILIDAD CON ESTE EQUIPO");
    const auto checks = C::Compatibility(preview, MachineOf(ctx));
    {
        const C::Fit fit = C::Overall(checks);
        P::Row(fit == C::Fit::Confirmed ? P::Mark::Ok : fit == C::Fit::Partial ? P::Mark::Wait : P::Mark::Bad, C::FitText(fit),
               fit == C::Fit::Partial ? "Utilizable: las diferencias se indican debajo" : std::string());
    }
    for (const auto& c : checks)
        P::Row(c.level == C::Level::Ok ? P::Mark::Ok : c.level == C::Level::Warn ? P::Mark::Wait : P::Mark::Bad, c.text, std::string());
    P::Coloured(IlionTheme::ShellPal::Dim,
                "Verificado técnicamente: no (publicar no valida un perfil). Recomendado por AutoProfile: no disponible todavía. Las "
                "descargas no indican mejor rendimiento.",
                true);

    const bool blocked = C::Blocking(checks);
    // Descargar no activa: solo un perfil descargado (o propio) se puede activar
    const bool local = previewKey.rfind("dl:", 0) == 0 || previewKey.rfind("mine:", 0) == 0 ||
                       std::any_of(downloaded.begin(), downloaded.end(), [](const C::Profile& d) { return d.id == preview.id; });
    ImGui::Spacing();
    if (blocked)
        P::Row(P::Mark::Bad, "No se puede activar en este equipo", "Resuelve lo marcado en rojo o elige otro perfil");
    else if (!local)
    {
        P::Row(P::Mark::Info, "Para activarlo, descárgalo primero", "Descargar lo guarda en DESCARGADOS sin cambiar nada de tu configuración");
        if (IlionTheme::Button("DESCARGAR##CommunityPreviewDownload", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f)))
            DownloadProfile(preview);
    }
    else
    {
        P::Coloured(IlionTheme::ShellPal::Dim,
                    "ACTIVAR PERFIL selecciona el escalador y su modelo, el preset (por la proporción de ILION o indicándote la opción "
                    "del menú del juego), el modo del optimizador y el Frame Generation con multiplicador fijo. Antes guarda tu "
                    "configuración actual para poder volver a ella.",
                    true);
        ImGui::Checkbox("He revisado las diferencias y quiero activar este perfil##CommunityConfirm", &confirmApply);
        ImGui::BeginDisabled(!confirmApply);
        ImGui::PushStyleColor(ImGuiCol_Text, toneMapColor(IlionTheme::ShellPal::Orange));
        if (IlionTheme::Button("ACTIVAR PERFIL##CommunityApply", ImVec2(ImGui::GetContentRegionAvail().x, std::floor(font * 2.2f))))
        {
            std::string out;
            Say(Apply(ctx, preview, out), out);
            confirmApply = false;
        }
        ImGui::PopStyleColor();
        ImGui::EndDisabled();
    }
    // El resultado real: el aviso de Perfiles (rojo con la acción exacta mientras falte algo, verde solo con todo
    // verificado con lo que corre) y lo que hizo la activación, paso a paso
    if (applied == preview.id)
    {
        ImGui::Spacing();
        const P::Saved saved = P::ReadSaved();
        P::OutcomeBanner(ctx, saved, P::ReadLive(ctx, saved));
        ImGui::Spacing();
        P::Coloured(IlionTheme::ShellPal::Text2, "RESULTADO DE LA ACTIVACIÓN");
        for (const auto& s : P::steps)
            P::Row(s.mark, s.title, s.detail);
        if (P::Exists(SnapshotPath()) && IlionTheme::Button("RESTAURAR LA CONFIGURACIÓN ANTERIOR##CommunityRestore", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f)))
        {
            P::ApplyFile(ctx, SnapshotPath(), "configuracion", "Configuración anterior al perfil de la comunidad", true);
            applied.clear();
            Say(true, "Configuración anterior restaurada (la de antes de la última activación)");
        }
    }
    if (IlionTheme::Button("CERRAR VISTA PREVIA##CommunityClose"))
        previewKey.clear(), confirmApply = false;
}

static void ShowPreview(const C::Profile& p, const char* list)
{
    previewKey = std::string(list) + ":" + p.id;
    preview = p;
    confirmApply = false;
}

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
                                             : "Perfil descargado. No se ha aplicado: ábrelo en DESCARGADOS");
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
            Say(true, "Perfil descargado, no activado: ábrelo en DESCARGADOS. Sin sesión la descarga no cuenta para el ranking");
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
    const bool olive = IlionTheme::shellLemonPilots;
    if (rank == 1)
        return olive ? ImVec4(0.92f, 0.22f, 0.16f, 1.0f) : ImVec4(1.00f, 0.16f, 0.12f, 1.0f);
    if (rank == 2)
        return olive ? ImVec4(0.80f, 0.93f, 0.26f, 1.0f) : ImVec4(0.45f, 1.00f, 0.12f, 1.0f);
    return olive ? ImVec4(0.98f, 0.58f, 0.14f, 1.0f) : ImVec4(1.00f, 0.50f, 0.06f, 1.0f);
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
        const float crown = std::floor(font * 3.0f);
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
        P::Big(P::FigureFont(), 1.15f, colour, StrFmt("%lld descargas", p.downloads).c_str());
        ImGui::EndGroup();
        if (IlionTheme::Button("VER DETALLES"))
            ShowPreview(p, "top");
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

// Más descargados: el TOP 3 con descargas reales del catálogo y, debajo, el resto por descargas descendentes
template <typename Ctx> static void TopPage(Ctx& ctx)
{
    const auto ws = worker.Get();
    const std::string stamp = CatalogStamp();
    P::Coloured(IlionTheme::ShellPal::Dim,
                catalog.empty() ? "Sin catálogo descargado"
                                : StrFmt("%d perfiles publicados · datos del catálogo descargado el %s", (int) catalog.size(), stamp.c_str()).c_str(),
                true);
    ImGui::BeginDisabled(!service.valid || ws.busy);
    if (IlionTheme::Button("ACTUALIZAR CATÁLOGO##CommunityFetchTop"))
    {
        const auto cfg = service;
        jobKind = "catalogo";
        worker.Start("Descargando el catálogo...", [cfg]() { return C::FetchCatalog(cfg, nullptr); });
    }
    ImGui::EndDisabled();
    ImGui::Spacing();

    IlionTheme::SectionPlate("TOP 3 — PERFILES MÁS DESCARGADOS");
    const auto ranked = ByDownloads();
    // Solo cuentan descargas reales registradas por el servidor: sin descargas no hay puesto
    const int podium = C::PodiumCount(catalog, C::RankByDownloads(catalog));
    for (int i = 0; i < podium; i++)
    {
        PodiumCard(*ranked[i], i + 1);
        ImGui::Spacing();
    }
    if (podium == 0)
    {
        P::Big(P::UiFont(), 1.15f, IlionTheme::ShellPal::Text2,
               catalog.empty() ? "Todavía no hay perfiles publicados" : "Todavía no hay descargas registradas", true);
        P::Coloured(IlionTheme::ShellPal::Dim,
                    !service.valid    ? ("El catálogo remoto no está configurado (" + service.problem + "). El ranking aparecerá con datos reales del servidor.").c_str()
                    : catalog.empty() ? "Pulsa ACTUALIZAR CATÁLOGO. Si sigue vacío, nadie ha publicado aún un perfil aprobado."
                                      : "El ranking aparece con la primera descarga real. No se rellena con datos de ejemplo.",
                    true);
        if (catalog.empty() && IlionTheme::Button("CREAR EL PRIMER PERFIL##CommunityFirstTop"))
        {
            draft = FromCurrent(ctx);
            draftReady = true;
            page = Publish;
        }
    }
    P::Coloured(IlionTheme::ShellPal::Dim, "Popularidad, no rendimiento: las descargas no indican que un perfil funcione mejor.", true);

    // El resto, en orden de descargas
    if ((int) ranked.size() > podium)
    {
        ImGui::Spacing();
        IlionTheme::SectionPlate("RESTO DEL CATÁLOGO");
        int position = podium;
        for (size_t i = podium; i < ranked.size(); i++)
        {
            const C::Profile& p = *ranked[i];
            position++;
            Card(p, "top", true,
                 [&]()
                 {
                     if (IlionTheme::Button("VER DETALLES"))
                         ShowPreview(p, "top");
                     Flow("DESCARGAR");
                     if (IlionTheme::Button("DESCARGAR"))
                         DownloadProfile(p);
                 },
                 position);
        }
    }
}

// ---- Páginas ---------------------------------------------------------------------------------------------------------
template <typename Ctx> static void CatalogPage(Ctx& ctx, const C::Machine& m)
{
    const auto ws = worker.Get();
    if (!service.valid)
        P::Row(P::Mark::Info, "Catálogo remoto no configurado", service.problem + ". Puedes crear, exportar e importar perfiles localmente.");
    ImGui::BeginDisabled(!service.valid || ws.busy);
    if (IlionTheme::Button("ACTUALIZAR CATÁLOGO##CommunityFetch"))
    {
        const auto cfg = service;
        jobKind = "catalogo";
        worker.Start("Descargando el catálogo...", [cfg]() { return C::FetchCatalog(cfg, nullptr); });
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    P::Coloured(IlionTheme::ShellPal::Dim,
                catalog.empty() ? "Sin catálogo en caché" : StrFmt("%d perfiles publicados (caché local) · ordenados por descargas", (int) catalog.size()).c_str());

    int shown = 0;
    for (const auto& p : catalog)
    {
        if (!Matches(p, m))
            continue;
        shown++;
        Card(p, "cat", true,
             [&]()
             {
                 if (IlionTheme::Button("VER DETALLES"))
                     ShowPreview(p, "cat");
                 Flow(favourites.count(p.id) ? "QUITAR FAVORITO" : "GUARDAR");
                 if (IlionTheme::Button(favourites.count(p.id) ? "QUITAR FAVORITO" : "GUARDAR"))
                     ToggleFavourite(p.id);
                 Flow("DESCARGAR");
                 if (IlionTheme::Button("DESCARGAR"))
                     DownloadProfile(p);
             });
    }
    if (shown == 0)
    {
        ImGui::Spacing();
        P::Big(P::UiFont(), 1.2f, IlionTheme::ShellPal::Text2, catalog.empty() ? "Todavía no hay perfiles en el catálogo" : "Ningún perfil coincide con los filtros", true);
        P::Coloured(IlionTheme::ShellPal::Dim,
                    catalog.empty() ? "Comparte tu configuración con tu alias: otros jugadores con tu juego y tu GPU podrán descargarla." : "Prueba con «Todos» en juego o GPU.",
                    true);
        if (catalog.empty() && IlionTheme::Button("CREAR EL PRIMER PERFIL##CommunityFirst"))
        {
            draft = FromCurrent(ctx);
            draftReady = true;
            page = Publish;
        }
    }
}

template <typename Ctx> static void DownloadedPage(Ctx& ctx, const C::Machine& m)
{
    (void) ctx;
    int shown = 0;
    std::string remove;
    for (const auto& p : downloaded)
    {
        if (!Matches(p, m))
            continue;
        shown++;
        Card(p, "dl", true,
             [&]()
             {
                 if (IlionTheme::Button("ACTIVAR PERFIL"))
                     ShowPreview(p, "dl");
                 Flow(favourites.count(p.id) ? "QUITAR FAVORITO" : "GUARDAR");
                 if (IlionTheme::Button(favourites.count(p.id) ? "QUITAR FAVORITO" : "GUARDAR"))
                     ToggleFavourite(p.id);
                 Flow("QUITAR DE LA LISTA");
                 if (IlionTheme::Button("QUITAR DE LA LISTA"))
                     remove = p.id;
             });
    }
    if (!remove.empty())
    {
        downloaded.erase(std::remove_if(downloaded.begin(), downloaded.end(), [&](const C::Profile& p) { return p.id == remove; }), downloaded.end());
        if (!C::SaveList(DownloadedPath(), downloaded))
            Say(false, "No se pudo guardar la lista");
    }
    if (shown == 0)
        P::Coloured(IlionTheme::ShellPal::Dim, "Sin perfiles descargados que coincidan. Descarga uno del CATÁLOGO o importa uno del portapapeles.", true);

    ImGui::Spacing();
    if (IlionTheme::Button("IMPORTAR DEL PORTAPAPELES##CommunityImport"))
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
                r == C::AddResult::Duplicate  ? "Ya tienes un perfil igual de ese autor"
                : r == C::AddResult::NotOwner ? "Ya hay un perfil con ese identificador de otro autor: no se sustituye"
                : r == C::AddResult::Full     ? "La lista está llena (500)"
                                              : "Perfil importado y validado. No se ha aplicado");
            if (r == C::AddResult::Added || r == C::AddResult::Updated)
                C::SaveList(DownloadedPath(), downloaded);
        }
    }
}

template <typename Ctx> static void MinePage(Ctx& ctx, const C::Machine& m)
{
    (void) m;
    const auto ws = worker.Get();
    std::string remove;
    for (const auto& p : mine)
    {
        Card(p, "mine", p.review != "local",
             [&]()
             {
                 if (IlionTheme::Button("VISTA PREVIA"))
                     ShowPreview(p, "mine");
                 Flow("EDITAR");
                 if (IlionTheme::Button("EDITAR"))
                 {
                     draft = p;
                     strncpy_s(draftAlias, p.author.c_str(), _TRUNCATE);
                     strncpy_s(draftName, p.name.c_str(), _TRUNCATE);
                     draftReady = true;
                     page = Publish;
                 }
                 Flow("COPIAR (EXPORTAR)");
                 if (IlionTheme::Button("COPIAR (EXPORTAR)"))
                 {
                     ImGui::SetClipboardText(C::ToJson(p).dump(1).c_str());
                     const auto file = C::Root() / L"exportados" / (string_to_wstring(p.id) + L".ilionprofile.json");
                     Say(C::WriteAtomic(file, C::ToJson(p).dump(1)), "Copiado al portapapeles y exportado a " + wstring_to_string(file.wstring()));
                 }
                 Flow(p.review == "local" ? "PUBLICAR" : "ACTUALIZAR PUBLICACIÓN");
                 ImGui::BeginDisabled(!service.valid || ws.busy);
                 if (IlionTheme::Button(p.review == "local" ? "PUBLICAR" : "ACTUALIZAR PUBLICACIÓN"))
                 {
                     publishId = p.id;
                     confirmPublish = false;
                 }
                 ImGui::EndDisabled();
                 if (p.review != "local")
                 {
                     Flow("RETIRAR DEL CATÁLOGO");
                     ImGui::BeginDisabled(!service.valid || ws.busy);
                     if (IlionTheme::Button("RETIRAR DEL CATÁLOGO"))
                         withdrawId = p.id;
                     ImGui::EndDisabled();
                 }
                 Flow("ELIMINAR");
                 if (IlionTheme::Button("ELIMINAR"))
                     remove = p.id;
                 if (withdrawId == p.id)
                 {
                     P::Row(P::Mark::Wait, "Retirar del catálogo comunitario", "Se borra del servidor; tu copia local se conserva como perfil sin publicar");
                     if (IlionTheme::Button("CONFIRMAR RETIRADA##CommunityWithdrawGo"))
                     {
                         const std::string id = p.id;
                         RunWithSession("Retirando el perfil...", "retirar:" + id,
                                        [id](const C::ServiceConfig& cfg, C::Session& s) { return C::Withdraw(cfg, s, id); });
                         withdrawId.clear();
                     }
                     Flow("CANCELAR##CommunityWithdrawCancel");
                     if (IlionTheme::Button("CANCELAR##CommunityWithdrawCancel"))
                         withdrawId.clear();
                 }
                 // Publicar: el usuario ve lo que se envía y lo confirma
                 if (publishId == p.id)
                 {
                     P::Row(P::Mark::Wait, "Se enviará al catálogo comunitario",
                            "Alias, nombre del perfil, juego, API, GPU, arquitectura, resolución y la configuración de la tarjeta. Nada más: "
                            "ni FPS, ni datos del equipo, ni el correo.");
                     if (!session.Active() && session.refreshToken.empty())
                         P::Row(P::Mark::Bad, "Inicia sesión en CUENTA para publicar", "La publicación necesita una identidad verificada");
                     else
                     {
                         ImGui::Checkbox("Confirmo que quiero publicar estos datos##CommunityPublishConfirm", &confirmPublish);
                         ImGui::BeginDisabled(!confirmPublish);
                         if (IlionTheme::Button("ENVIAR##CommunityPublishSend"))
                         {
                             const auto cfg = service;
                             *jobSession = session;
                             auto js = jobSession;
                             auto prof = std::make_shared<C::Profile>(p);
                             jobKind = "publicar:" + p.id;
                             publishResult = prof;
                             worker.Start("Publicando...",
                                          [cfg, js, prof]()
                                          {
                                              if (!js->Active() || js->expires < (long long) std::time(nullptr))
                                                  if (auto r = C::Refresh(cfg, *js); !r.first)
                                                      return r;
                                              return C::Publish(cfg, *js, *prof);
                                          });
                             publishId.clear();
                         }
                         ImGui::EndDisabled();
                         Flow("CANCELAR##CommunityPublishCancel");
                         if (IlionTheme::Button("CANCELAR##CommunityPublishCancel"))
                             publishId.clear();
                     }
                 }
             });
    }
    if (!remove.empty())
    {
        mine.erase(std::remove_if(mine.begin(), mine.end(), [&](const C::Profile& p) { return p.id == remove; }), mine.end());
        if (!C::SaveList(MinePath(), mine))
            Say(false, "No se pudo guardar la lista");
    }
    // El estado de moderación y las descargas de lo publicado, consultados al servidor
    if (service.valid && (session.Active() || !session.refreshToken.empty()))
    {
        ImGui::BeginDisabled(ws.busy);
        if (IlionTheme::Button("ACTUALIZAR ESTADO DE MIS PUBLICACIONES##CommunityMine"))
        {
            auto out = mineRemote;
            RunWithSession("Consultando tus perfiles publicados...", "mios",
                           [out](const C::ServiceConfig& cfg, C::Session& s) { return C::FetchMine(cfg, s, *out); });
        }
        ImGui::EndDisabled();
    }
    if (mine.empty())
    {
        P::Coloured(IlionTheme::ShellPal::Dim, "Aún no has creado perfiles.", true);
        if (IlionTheme::Button("CREAR PERFIL##CommunityCreate"))
        {
            draft = FromCurrent(ctx);
            draftReady = true;
            page = Publish;
        }
    }
}

template <typename Ctx> static void PublishPage(Ctx& ctx, const C::Machine& m)
{
    if (!draftReady)
    {
        draft = FromCurrent(ctx);
        if (!session.alias.empty())
            draft.author = session.alias;
        strncpy_s(draftAlias, draft.author.c_str(), _TRUNCATE);
        draftReady = true;
    }
    const float half = std::floor((ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f);
    const auto label = [](const char* t) { P::Coloured(IlionTheme::ShellPal::Text2, t); };

    IlionTheme::SectionPlate("IDENTIDAD");
    label("Nombre público (alias)");
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputText("##CommunityAlias", draftAlias, 25);
    label("Nombre del perfil");
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputText("##CommunityName", draftName, 41);
    if (!session.alias.empty() && session.alias != draftAlias)
        P::Coloured(IlionTheme::ShellPilot(IlionTheme::ShellPal::Yellow), ("Para publicar, el alias debe ser el de tu cuenta: " + session.alias).c_str(), true);

    IlionTheme::SectionPlate("COMPATIBILIDAD");
    // Juego: el que está en marcha o uno de la matriz de compatibilidad de Perfiles
    {
        std::vector<std::pair<std::string, std::string>> games { { m.gameExe, State::Instance().gameName.empty() ? m.gameExe : State::Instance().gameName } };
        CSimpleIniA matrix(true);
        if (matrix.LoadFile(P::MatrixPath().c_str()) >= 0)
        {
            CSimpleIniA::TNamesDepend sections;
            matrix.GetAllSections(sections);
            for (const auto& sec : sections)
                if (C::ValidExe(sec.pItem) && std::none_of(games.begin(), games.end(), [&](const auto& g) { return g.first == sec.pItem; }))
                    games.push_back({ sec.pItem, matrix.GetValue(sec.pItem, "nombre", sec.pItem) });
        }
        label("Juego");
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (IlionTheme::BeginCombo("##CommunityGame", (draft.gameName + " (" + draft.gameExe + ")").c_str()))
        {
            for (const auto& g : games)
                if (ImGui::Selectable((g.second + " (" + g.first + ")").c_str(), draft.gameExe == g.first))
                    draft.gameExe = g.first, draft.gameName = g.second;
            ImGui::EndCombo();
        }
    }
    ImGui::BeginGroup();
    ImGui::PushItemWidth(half);
    label("API");
    Combo("##CommunityApi", C::kApis, draft.api);
    label("Arquitectura");
    if (Combo("##CommunityArch", C::kArchs, draft.arch))
    {
        draft.gpu.clear();
        if (!C::TechSupport(draft.tech, draft.arch).possible)
            draft.tech = "xess", draft.model = "no_aplica";
    }
    ImGui::PopItemWidth();
    ImGui::EndGroup();
    label("GPU");
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (IlionTheme::BeginCombo("##CommunityGpu", draft.gpu.empty() ? "Elige la GPU" : draft.gpu.c_str()))
    {
        if (draft.arch == m.arch && ImGui::Selectable((m.gpu + " (detectada)").c_str(), draft.gpu == m.gpu))
            draft.gpu = m.gpu;
        for (const auto& g : C::kGpus)
            if (draft.arch == g.arch && g.name != m.gpu && ImGui::Selectable(g.name, draft.gpu == g.name))
                draft.gpu = g.name;
        ImGui::EndCombo();
    }
    label("Resolución de salida");
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (IlionTheme::BeginCombo("##CommunityRes", StrFmt("%d×%d", draft.outW, draft.outH).c_str()))
    {
        if (m.outW > 0 && ImGui::Selectable(StrFmt("%d×%d (la actual)", m.outW, m.outH).c_str(), draft.outW == m.outW && draft.outH == m.outH))
            draft.outW = m.outW, draft.outH = m.outH;
        for (const auto& r : C::kResolutions)
            if (!(r.w == m.outW && r.h == m.outH) && ImGui::Selectable(StrFmt("%d×%d", r.w, r.h).c_str(), draft.outW == r.w && draft.outH == r.h))
                draft.outW = r.w, draft.outH = r.h;
        ImGui::EndCombo();
    }

    IlionTheme::SectionPlate("CONFIGURACIÓN");
    label("Modo");
    Combo("##CommunityMode", C::kModes, draft.mode);
    label("Tecnología de escalado");
    if (Combo("##CommunityTech", C::kTechs, draft.tech, [&](const char* id) { return C::TechSupport(id, draft.arch).possible; }))
        draft.model = draft.tech == "fsr4_driver" || draft.tech == "fsr4_dll" ? "completo" : "no_aplica";
    label("Modelo");
    Combo("##CommunityModel", C::kModels, draft.model, [&](const char* id) { return C::ModelSupport(id, draft.tech, draft.arch).possible; });
    label("Preset");
    Combo("##CommunityPreset", C::kPresets, draft.preset);
    label("Frame Generation (fijo; el MFG dinámico no forma parte de los perfiles)");
    if (Combo("##CommunityFg", C::kFgs, draft.fg, [&](const char* id) { return std::string(id) == "ninguno" || C::FgMaxMultiplier(id, draft.arch) > 0; }))
        draft.multiplier = draft.fg == "ninguno" ? 0 : 2;
    if (draft.fg != "ninguno")
    {
        label("Multiplicador fijo");
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (IlionTheme::BeginCombo("##CommunityMult", StrFmt("%dX", draft.multiplier).c_str()))
        {
            for (int x = 2; x <= C::FgMaxMultiplier(draft.fg, draft.arch); x++)
                if (ImGui::Selectable(StrFmt("%dX", x).c_str(), draft.multiplier == x))
                    draft.multiplier = x;
            ImGui::EndCombo();
        }
    }

    // Validación en vivo
    C::Profile check = draft;
    check.author = draftAlias;
    check.name = draftName;
    const bool valid = C::Validate(check, &draftError);
    ImGui::Spacing();
    if (!valid)
        P::Row(P::Mark::Bad, "Falta completar: " + draftError, std::string());
    if (IlionTheme::Button("RELLENAR CON MI CONFIGURACIÓN ACTUAL##CommunityFill"))
    {
        const C::Profile keep = draft;
        draft = FromCurrent(ctx);
        draft.id = keep.id;
        draft.version = keep.version;
        draft.authorId = keep.authorId;
        draft.review = keep.review;
        draft.created = keep.created;
    }
    Flow("GUARDAR EN MIS PERFILES##CommunitySave");
    ImGui::BeginDisabled(!valid);
    if (IlionTheme::Button("GUARDAR EN MIS PERFILES##CommunitySave"))
    {
        check.updated = C::NowUtc();
        if (check.created.empty())
            check.created = check.updated;
        // Editar un perfil ya publicado: misma identidad, nueva versión
        for (const auto& e : mine)
            if (e.id == check.id && e.review != "local" && C::ContentHash(e) != C::ContentHash(check))
                check.version = e.version + 1;
        const auto r = C::AddOrUpdate(mine, check);
        if (r == C::AddResult::Duplicate)
            Say(false, "Ya tienes un perfil con esta misma configuración");
        else if (r == C::AddResult::NotOwner)
            Say(false, "Ese perfil está firmado por otro alias: no se sustituye. Guárdalo como perfil nuevo");
        else if (r == C::AddResult::Full)
            Say(false, "La lista está llena (500)");
        else if (!C::SaveList(MinePath(), mine))
            Say(false, "No se pudo guardar");
        else
        {
            Say(true, r == C::AddResult::Updated ? "Perfil actualizado en MIS PERFILES" : "Perfil guardado en MIS PERFILES. Publicarlo es opcional");
            draftReady = false;
            draftName[0] = 0;
            page = Mine;
        }
    }
    ImGui::EndDisabled();
}

template <typename Ctx> static void AccountPage(Ctx& ctx)
{
    (void) ctx;
    const auto ws = worker.Get();
    P::Coloured(IlionTheme::ShellPal::Dim,
                "Consultar y descargar no necesita cuenta. Publicar sí: el alias queda ligado a tu cuenta en el servidor, así nadie puede "
                "publicar en tu nombre. Se inicia con un código de un solo uso enviado a tu correo; ILION no guarda el correo.",
                true);
    if (!service.valid)
    {
        P::Row(P::Mark::Info, "Servicio comunitario no configurado", service.problem);
        P::Coloured(IlionTheme::ShellPal::Dim, ("Carpeta de datos: " + wstring_to_string(C::Root().wstring())).c_str(), true);
        if (IlionTheme::Button("VOLVER A LEER servicio.json##CommunityReload"))
            service = C::LoadService();
        return;
    }
    if (!session.refreshToken.empty() || session.Active())
    {
        P::Row(P::Mark::Ok, session.alias.empty() ? "Sesión iniciada · sin alias reservado" : "Sesión iniciada como " + session.alias, std::string());
        if (session.alias.empty())
        {
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::InputText("##CommunityClaim", claim, 25);
            ImGui::BeginDisabled(ws.busy);
            if (IlionTheme::Button("RESERVAR ALIAS##CommunityClaimGo"))
            {
                const auto cfg = service;
                *jobSession = session;
                auto js = jobSession;
                const std::string alias = claim;
                jobKind = "sesion";
                worker.Start("Reservando el alias...",
                             [cfg, js, alias]()
                             {
                                 if (!js->Active() || js->expires < (long long) std::time(nullptr))
                                     if (auto r = C::Refresh(cfg, *js); !r.first)
                                         return r;
                                 return C::ClaimAlias(cfg, *js, alias);
                             });
            }
            ImGui::EndDisabled();
        }
        if (IlionTheme::Button("CERRAR SESIÓN##CommunityLogout"))
        {
            C::ForgetSession();
            session = {};
            Say(true, "Sesión cerrada en este equipo");
        }
        return;
    }
    P::Coloured(IlionTheme::ShellPal::Text2, "Correo");
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputText("##CommunityEmail", email, sizeof(email));
    ImGui::BeginDisabled(ws.busy);
    if (IlionTheme::Button("ENVIAR CÓDIGO##CommunityOtp"))
    {
        const auto cfg = service;
        const std::string mail = email;
        jobKind = "codigo";
        worker.Start("Pidiendo el código...", [cfg, mail]() { return C::RequestCode(cfg, mail); });
    }
    ImGui::EndDisabled();
    P::Coloured(IlionTheme::ShellPal::Text2, "Código recibido");
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputText("##CommunityCode", code, sizeof(code), ImGuiInputTextFlags_CharsDecimal);
    ImGui::BeginDisabled(ws.busy);
    if (IlionTheme::Button("VERIFICAR##CommunityVerify"))
    {
        const auto cfg = service;
        const std::string mail = email, otp = code;
        *jobSession = C::Session {};
        auto js = jobSession;
        jobKind = "sesion";
        worker.Start("Verificando...", [cfg, mail, otp, js]() { return C::VerifyCode(cfg, mail, otp, *js); });
    }
    ImGui::EndDisabled();
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
                e.review = "local";
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
        }
    }
    jobKind.clear();
}

// La columna derecha de PERFILES (Industrial y Clean Olive en su presentación Completa): título, secciones, avisos y el
// área desplazable con su contenido. Solo PERFILES la llama, y solo en esos temas; nada se carga antes de dibujarla
template <typename Ctx> static void Panel(Ctx& ctx)
{
    const float font = ImGui::GetFontSize();
    if (!loaded)
        LoadAll();
    Collect();

    // La primera vez que se ve la columna: comprobación real del servicio (el catálogo público) y, si hay sesión
    // guardada, su renovación. En segundo plano; sin servicio configurado no se contacta con nada
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

    // ---- Cabecera ----
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float h = std::floor(font * 1.9f);
        const ImVec4 accent = IlionTheme::ShellPilot(IlionTheme::ShellPal::Green);
        // Piloto encendido junto al título
        IlionTheme::ShellLed(dl, ImVec2(p.x + h * 0.32f, p.y + h * 0.5f), h * 0.18f, toneMapColor(accent), true);
        ImGui::SetCursorScreenPos(ImVec2(p.x + h * 0.75f, p.y));
        ImGui::BeginGroup();
        P::Big(P::UiFont(), 1.7f, IlionTheme::ShellPal::Text, "ILION COMMUNITY");
        P::Coloured(IlionTheme::ShellPal::Orange, "Perfiles de la comunidad");
        ImGui::EndGroup();
        const float right = p.x + ImGui::GetContentRegionAvail().x;
        const float y = ImGui::GetItemRectMax().y + font * 0.35f;
        dl->AddLine(ImVec2(p.x, y), ImVec2(right, y), ImGui::GetColorU32(toneMapColor(IlionTheme::ShellPal::Line)), 1.0f);
        ImGui::SetCursorScreenPos(ImVec2(p.x, y + font * 0.45f));
    }

    // ---- Estado real del servicio y de la cuenta ----
    {
        const C::ServiceState st = !service.valid ? C::ServiceState::NotConfigured : (C::ServiceState) C::LastServiceState().load();
        const P::Mark mark = st == C::ServiceState::Connected                                          ? P::Mark::Ok
                             : st == C::ServiceState::Unchecked || st == C::ServiceState::Offline ? P::Mark::Wait
                             : st == C::ServiceState::NotConfigured                               ? P::Mark::Info
                                                                                                  : P::Mark::Bad;
        const std::string detail = st == C::ServiceState::NotConfigured ? service.problem + ". Los perfiles locales funcionan igual"
                                   : st == C::ServiceState::Offline     ? std::string("El optimizador y los perfiles descargados funcionan sin conexión")
                                   : st == C::ServiceState::KeyRejected ? std::string("Revisa anon_key en servicio.json")
                                   : st == C::ServiceState::SchemaMissing ? std::string("Ejecuta ilion_community_supabase.sql en el proyecto")
                                                                          : std::string();
        P::Row(mark, C::ServiceStateText(st), detail);
        if (service.valid)
            P::Row(session.Active() ? P::Mark::Ok : P::Mark::Info,
                   session.Active() ? "Sesión: " + (session.alias.empty() ? std::string("iniciada, sin alias") : session.alias)
                   : !session.refreshToken.empty() ? std::string("Sesión guardada, sin renovar todavía")
                                                   : std::string("Sin sesión: consultar y descargar no la necesitan"),
                   std::string());
    }

    // ---- Secciones: dos filas de botones compactos ----
    static const char* names[] = { "MÁS DESCARGADOS", "CATÁLOGO", "MIS PERFILES", "DESCARGADOS", "PUBLICAR PERFIL", "CUENTA" };
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float w = std::floor((ImGui::GetContentRegionAvail().x - spacing * 2.0f) / 3.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(font * 0.3f, ImGui::GetStyle().FramePadding.y));
    for (int i = 0; i < 6; i++)
    {
        if (i % 3 != 0)
            ImGui::SameLine();
        const bool sel = page == i;
        if (sel)
            ImGui::PushStyleColor(ImGuiCol_Text, toneMapColor(IlionTheme::ShellPilot(IlionTheme::ShellPal::Green)));
        // La cuenta solo hace falta para publicar: se marca atenuada mientras no haya servicio
        const bool dimAccount = i == Account && !service.valid && !sel;
        if (dimAccount)
            ImGui::PushStyleColor(ImGuiCol_Text, toneMapColor(IlionTheme::ShellPal::Dim));
        if (IlionTheme::Button(StrFmt("%s##CommunityPage%d", names[i], i).c_str(), ImVec2(w, 0.0f)))
        {
            page = i;
            previewKey.clear();
            confirmApply = false;
        }
        if (dimAccount)
            ImGui::PopStyleColor();
        if (sel)
            ImGui::PopStyleColor();
    }
    ImGui::PopStyleVar();

    // ---- Avisos: siempre a la vista, fuera del área desplazable ----
    if (!warning.empty())
        P::Row(P::Mark::Wait, "Archivos locales", warning);
    {
        const auto ws = worker.Get();
        if (ws.busy)
            P::Row(P::Mark::Wait, ws.task, "En segundo plano: el juego no espera");
        else if (!message.empty())
            P::Row(messageOk ? P::Mark::Ok : P::Mark::Bad, message, std::string());
    }

    const C::Machine m = MachineOf(ctx);
    const bool listPage = page == Top || page == Catalog || page == Downloaded || page == Mine;
    const bool previewing = listPage && !previewKey.empty();

    // ---- Búsqueda y filtros (catálogo y descargados) ----
    if ((page == Catalog || page == Downloaded) && !previewing)
    {
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputTextWithHint("##CommunitySearch", "Buscar autor, juego o GPU", search, sizeof(search));
        const float third = std::floor((ImGui::GetContentRegionAvail().x - spacing * 2.0f) / 3.0f);
        ImGui::SetNextItemWidth(third);
        static const char* games[] = { "Todos los juegos", "Este juego" };
        if (IlionTheme::BeginCombo("##CommunityFilterGame", games[filterGame]))
        {
            for (int i = 0; i < 2; i++)
                if (ImGui::Selectable(games[i], filterGame == i))
                    filterGame = i;
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(third);
        static const char* gpus[] = { "Todas las GPU", "Mi arquitectura", "Mi GPU" };
        if (IlionTheme::BeginCombo("##CommunityFilterGpu", gpus[filterGpu]))
        {
            for (int i = 0; i < 3; i++)
                if (ImGui::Selectable(gpus[i], filterGpu == i))
                    filterGpu = i;
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(third);
        if (IlionTheme::BeginCombo("##CommunityFilterMode", filterMode < 0 ? "Todos los modos" : C::kModes[filterMode].name))
        {
            if (ImGui::Selectable("Todos los modos", filterMode < 0))
                filterMode = -1;
            for (int i = 0; i < 4; i++)
                if (ImGui::Selectable(C::kModes[i].name, filterMode == i))
                    filterMode = i;
            ImGui::EndCombo();
        }
        ImGui::Checkbox("Solo favoritos##CommunityFav", &onlyFavourites);
    }
    ImGui::Spacing();

    // ---- Contenido: lo único que se desplaza ----
    ImGui::PushStyleColor(ImGuiCol_ChildBg, toneMapColor(ImVec4(0, 0, 0, 0)));
    if (ImGui::BeginChild("##CommunityScroll", ImVec2(0.0f, (std::max)(ImGui::GetContentRegionAvail().y, font * 12.0f)), ImGuiChildFlags_None))
    {
        if (previewing)
        {
            if (IlionTheme::Button("< VOLVER AL LISTADO##CommunityBack"))
                previewKey.clear(), confirmApply = false;
            Preview(ctx);
        }
        else
        {
            switch (page)
            {
            case Top:
                TopPage(ctx);
                break;
            case Catalog:
                CatalogPage(ctx, m);
                break;
            case Downloaded:
                DownloadedPage(ctx, m);
                break;
            case Mine:
                MinePage(ctx, m);
                break;
            case Publish:
                PublishPage(ctx, m);
                break;
            default:
                AccountPage(ctx);
                break;
            }
        }
        ImGui::Spacing();
        P::Coloured(IlionTheme::ShellPal::Dim,
                    "Perfiles compartidos voluntariamente y firmados por su autor. Descargar no activa: nada se aplica sin tu "
                    "confirmación. Al abrir se consulta el catálogo público; no se envía ningún dato tuyo. Sin telemetría.",
                    true);
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
}
} // namespace IlionCommunityUi

#pragma execution_character_set(pop)

void MenuCommon::RenderCommunityPanel(RenderMenuContext& ctx) { IlionCommunityUi::Panel(ctx); }
