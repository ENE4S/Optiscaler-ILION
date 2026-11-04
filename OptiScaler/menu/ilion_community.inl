// ILION COMMUNITY — perfiles de la comunidad (rama experimental ilion-perfiles-exp)
//
// Panel plegable al final de la pestaña External FG (FRAME GEN), independiente de sus controles. Núcleo, almacenamiento y
// red en misc/IlionCommunity.h: aquí solo la interfaz y la aplicación de un perfil, que reutiliza la de Perfiles
// (ApplyFile: puntos de restauración, selección del escalador por el catálogo, FG por la configuración) y su aviso rojo /
// verde (OutcomeBanner), que verifica con lo que corre de verdad.
//
// Sin telemetría: nada sale del equipo sin pulsar ACTUALIZAR CATÁLOGO, DESCARGAR, PUBLICAR o los botones de la cuenta.
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
    Catalog,
    Downloaded,
    Mine,
    Publish,
    Account,
};

static bool open = true;
static int page = Catalog;
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
    P::ApplyFile(ctx, P::ProfilePath(), "perfil", "Perfil de la comunidad", false);
    applied = p.id;
    out = "Aplicado con los mecanismos de Perfiles. Copia del perfil anterior: " + wstring_to_string(backup.filename().wstring()) +
          " · puntos de restauración en ILION\\Perfiles. Comprueba el aviso: verde solo cuando todo corre de verdad";
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
static void Card(const C::Profile& p, const char* list, bool showDownloads, const std::function<void()>& actions)
{
    const float font = ImGui::GetFontSize();
    ImGui::PushID((std::string(list) + p.id).c_str());
    ImGui::PushStyleColor(ImGuiCol_ChildBg, toneMapColor(IlionTheme::shell ? IlionTheme::ShellPal::Panel : ImGui::GetStyleColorVec4(ImGuiCol_FrameBg)));
    ImGui::PushStyleColor(ImGuiCol_Border, toneMapColor(IlionTheme::ShellPal::Line));
    if (ImGui::BeginChild("##card", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY))
    {
        P::Big(P::UiFont(), 1.3f, IlionTheme::ShellPal::Text, p.author.c_str());
        ImGui::SameLine();
        P::Coloured(IlionTheme::ShellPal::Dim, ("· " + p.name).c_str());
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
    for (const auto& c : checks)
        P::Row(c.level == C::Level::Ok ? P::Mark::Ok : c.level == C::Level::Warn ? P::Mark::Wait : P::Mark::Bad, c.text, std::string());
    P::Coloured(IlionTheme::ShellPal::Dim,
                "Verificado técnicamente: no (publicar no valida un perfil). Recomendado por AutoProfile: no disponible todavía. Las "
                "descargas no indican mejor rendimiento.",
                true);

    const bool blocked = C::Blocking(checks);
    ImGui::Spacing();
    if (blocked)
        P::Row(P::Mark::Bad, "No se puede aplicar en este equipo", "Resuelve lo marcado en rojo o elige otro perfil");
    else
    {
        ImGui::Checkbox("He revisado las diferencias y quiero aplicar este perfil##CommunityConfirm", &confirmApply);
        ImGui::BeginDisabled(!confirmApply);
        if (IlionTheme::Button("APLICAR PERFIL##CommunityApply", ImVec2(ImGui::GetContentRegionAvail().x, std::floor(font * 2.0f))))
        {
            std::string out;
            Say(Apply(ctx, preview, out), out);
            confirmApply = false;
        }
        ImGui::EndDisabled();
    }
    // El aviso de Perfiles: rojo con la acción exacta mientras falte algo, verde solo con todo verificado
    if (applied == preview.id)
    {
        ImGui::Spacing();
        const P::Saved saved = P::ReadSaved();
        P::OutcomeBanner(ctx, saved, P::ReadLive(ctx, saved));
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
                 if (IlionTheme::Button("VISTA PREVIA"))
                     ShowPreview(p, "cat");
                 ImGui::SameLine();
                 if (IlionTheme::Button(favourites.count(p.id) ? "QUITAR FAVORITO" : "GUARDAR"))
                     ToggleFavourite(p.id);
                 ImGui::SameLine();
                 if (IlionTheme::Button("DESCARGAR"))
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
                         Say(true, r == C::AddResult::Updated ? "Perfil descargado de nuevo (actualizado). No se ha aplicado" : "Perfil descargado. No se ha aplicado: ábrelo en DESCARGADOS");
                         // El contador del servidor, si el hilo está libre. No influye en nada local
                         if (service.valid && !worker.Get().busy)
                         {
                             const auto cfg = service;
                             const std::string id = p.id;
                             jobKind = "descarga";
                             worker.Start("Registrando la descarga...", [cfg, id]() { C::RegisterDownload(cfg, id); return std::pair<bool, std::string> { true, "" }; });
                         }
                     }
                 }
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
                 if (IlionTheme::Button("VISTA PREVIA"))
                     ShowPreview(p, "dl");
                 ImGui::SameLine();
                 if (IlionTheme::Button(favourites.count(p.id) ? "QUITAR FAVORITO" : "GUARDAR"))
                     ToggleFavourite(p.id);
                 ImGui::SameLine();
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
                 ImGui::SameLine();
                 if (IlionTheme::Button("EDITAR"))
                 {
                     draft = p;
                     strncpy_s(draftAlias, p.author.c_str(), _TRUNCATE);
                     strncpy_s(draftName, p.name.c_str(), _TRUNCATE);
                     draftReady = true;
                     page = Publish;
                 }
                 ImGui::SameLine();
                 if (IlionTheme::Button("COPIAR (EXPORTAR)"))
                 {
                     ImGui::SetClipboardText(C::ToJson(p).dump(1).c_str());
                     const auto file = C::Root() / L"exportados" / (string_to_wstring(p.id) + L".ilionprofile.json");
                     Say(C::WriteAtomic(file, C::ToJson(p).dump(1)), "Copiado al portapapeles y exportado a " + wstring_to_string(file.wstring()));
                 }
                 ImGui::SameLine();
                 ImGui::BeginDisabled(!service.valid || ws.busy);
                 if (IlionTheme::Button(p.review == "local" ? "PUBLICAR" : "ACTUALIZAR PUBLICACIÓN"))
                 {
                     publishId = p.id;
                     confirmPublish = false;
                 }
                 ImGui::EndDisabled();
                 ImGui::SameLine();
                 if (IlionTheme::Button("ELIMINAR"))
                     remove = p.id;
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
                         ImGui::SameLine();
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
    ImGui::SameLine();
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
    if (jobKind == "catalogo" && ws.ok)
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

template <typename Ctx> static void Panel(Ctx& ctx)
{
    const float font = ImGui::GetFontSize();
    ImGui::Spacing();
    ImGui::Spacing();
    // Plegable, para no quitar sitio a los controles de la pestaña
    ImGui::PushStyleColor(ImGuiCol_Text, toneMapColor(IlionTheme::ShellPal::Orange));
    if (IlionTheme::Button(open ? "ILION COMMUNITY — PERFILES DE LA COMUNIDAD   [-]##CommunityToggle"
                                : "ILION COMMUNITY — PERFILES DE LA COMUNIDAD   [+]##CommunityToggle",
                           ImVec2(ImGui::GetContentRegionAvail().x, std::floor(font * 1.9f))))
        open = !open;
    ImGui::PopStyleColor();
    if (!open)
        return;
    if (!loaded)
        LoadAll();
    Collect();

    P::Coloured(IlionTheme::ShellPal::Dim,
                "Perfiles compartidos voluntariamente y firmados por su autor. Independiente de los controles de FG de esta pestaña: "
                "nada se aplica sin tu confirmación y nada sale del equipo sin que lo pidas. Sin telemetría.",
                true);
    if (!warning.empty())
        P::Row(P::Mark::Wait, "Archivos locales", warning);

    // Páginas
    static const char* names[] = { "CATÁLOGO", "DESCARGADOS", "MIS PERFILES", "PUBLICAR", "CUENTA" };
    const float w = std::floor((ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * 4.0f) / 5.0f);
    for (int i = 0; i < 5; i++)
    {
        if (i > 0)
            ImGui::SameLine();
        const bool sel = page == i;
        if (sel)
            ImGui::PushStyleColor(ImGuiCol_Text, toneMapColor(IlionTheme::ShellPilot(IlionTheme::ShellPal::Green)));
        if (IlionTheme::Button(StrFmt("%s##CommunityPage%d", names[i], i).c_str(), ImVec2(w, 0.0f)))
            page = i;
        if (sel)
            ImGui::PopStyleColor();
    }

    const C::Machine m = MachineOf(ctx);
    // Búsqueda y filtros (catálogo y descargados)
    if (page == Catalog || page == Downloaded)
    {
        ImGui::SetNextItemWidth(std::floor(ImGui::GetContentRegionAvail().x * 0.34f));
        ImGui::InputTextWithHint("##CommunitySearch", "Buscar autor, juego o GPU", search, sizeof(search));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(font * 8.0f);
        static const char* games[] = { "Todos los juegos", "Este juego" };
        if (IlionTheme::BeginCombo("##CommunityFilterGame", games[filterGame]))
        {
            for (int i = 0; i < 2; i++)
                if (ImGui::Selectable(games[i], filterGame == i))
                    filterGame = i;
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(font * 8.0f);
        static const char* gpus[] = { "Todas las GPU", "Mi arquitectura", "Mi GPU" };
        if (IlionTheme::BeginCombo("##CommunityFilterGpu", gpus[filterGpu]))
        {
            for (int i = 0; i < 3; i++)
                if (ImGui::Selectable(gpus[i], filterGpu == i))
                    filterGpu = i;
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(font * 7.0f);
        if (IlionTheme::BeginCombo("##CommunityFilterMode", filterMode < 0 ? "Todos los modos" : C::kModes[filterMode].name))
        {
            if (ImGui::Selectable("Todos los modos", filterMode < 0))
                filterMode = -1;
            for (int i = 0; i < 4; i++)
                if (ImGui::Selectable(C::kModes[i].name, filterMode == i))
                    filterMode = i;
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        ImGui::Checkbox("Favoritos##CommunityFav", &onlyFavourites);
    }
    ImGui::Spacing();

    switch (page)
    {
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

    const auto ws = worker.Get();
    if (ws.busy)
        P::Row(P::Mark::Wait, ws.task, "En segundo plano: el juego no espera");
    else if (!message.empty())
        P::Row(messageOk ? P::Mark::Ok : P::Mark::Bad, message, std::string());
    if (page != Publish && page != Account)
        Preview(ctx);
}
} // namespace IlionCommunityUi

#pragma execution_character_set(pop)

void MenuCommon::RenderCommunityPanel(RenderMenuContext& ctx) { IlionCommunityUi::Panel(ctx); }
