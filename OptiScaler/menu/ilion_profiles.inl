// ILION — Perfiles inteligentes (prototipo experimental, rama ilion-perfiles-exp)
//
// «Optimizar juego»: detecta el juego, la GPU y el monitor, elige el escalador y su variante, el modo de escalado y la
// generación de fotogramas con las reglas aprobadas, guarda un punto de restauración, aplica con los mecanismos que el
// menú ya usa (el catálogo de escaladores, ChooseUpscalerVariant, el modelo de FSR 4, IlionDynamicMfg) y guarda el
// perfil del juego. Cada paso se muestra en el monitor cuando se ha hecho de verdad; lo que necesita reiniciar el juego
// queda en ámbar hasta que se confirme. No hay benchmarks: el único cambio de escalador sin pedirlo es pasar al
// siguiente candidato cuando el elegido no llega a ejecutarse. Nada cambia solo durante la partida.
//
// Archivos (junto al juego):
//   ILION\Perfiles\<exe>.perfil.ini         el perfil guardado del juego
//   ILION\Perfiles\<exe>.restauracion.ini   la configuración de antes de la última optimización
//   ILION\Perfiles\compatibilidad.ini        la matriz de compatibilidad por juego (se crea con valores internos)
//
// Incluido por menu_common.cpp antes de MenuTabs: usa el catálogo y los selectores del menú.

#include <SimpleIni.h>

#pragma execution_character_set(push, "utf-8")

namespace IlionProfiles
{
enum class Mode
{
    Calidad,
    Equilibrado,
    Rendimiento,
};

enum class Mark
{
    Ok,   // verde: detectado, compatible, aplicado
    Wait, // ámbar: evaluando, pendiente o requiere reinicio
    Bad,  // rojo: incompatible, error o fallo al aplicar
    Info, // neutro: dato sin estado
};

struct Step
{
    Mark mark = Mark::Info;
    std::string title;
    std::string detail;
};

// Un escalador del catálogo, por su clave estable (sobrevive a un nuevo recorrido del catálogo)
struct Choice
{
    std::string key;   // "entry:FSR4/AMD driver", "backend:xess", "ffx31", "default"
    std::string name;  // como lo muestra el catálogo
    bool lite = false; // ruta del driver con la red INT8 Lite
    std::string why;
};

// Lo que se aplica a la generación de fotogramas
enum class FgRoute
{
    None,         // no se cambia
    IlionDynamic, // DLSSG del juego (Enabler...) con el Dinámico ILION: caliente
    XeFgDynamic,  // XeFG ya cargado con MFG dinámico R3: caliente
    XeFgNew,      // XeFG como nueva salida: requiere reiniciar
};

struct Plan
{
    std::vector<Choice> candidates; // en orden; se aplica el primero que funcione
    bool changeScaler = false;
    float ratio = 0.0f; // 0: no se toca
    FgRoute fg = FgRoute::None;
    FGInput fgInput = FGInput::NoFG; // para XeFgNew
    int maxCount = 0;                // fotogramas generados como máximo (1 = 2X)
    float target = 0.0f;             // FPS finales objetivo
};

// Medición de esta sesión
struct Measure
{
    double baseFps = 0.0;  // FPS reales (sin generados)
    double share60 = -1.0; // parte del tiempo observado con 60 FPS reales o más, -1 desconocida
    bool measured = false; // medidos, no estimados
    std::string source;
};

static Mode mode = Mode::Equilibrado;
static std::vector<Step> steps;
static bool running = false;
static bool finished = false;
static int phase = 0;
static double phaseStart = 0.0;
static bool entered = false; // the phase has done its first step (a choice, a request)
static size_t candidate = 0;
static bool litePending = false;
static Plan plan;
static Measure measure;
static std::string lastMessage;

// ---- Archivos ------------------------------------------------------------------------------------------------------
static std::filesystem::path Folder() { return Util::ExePath().parent_path() / L"ILION" / L"Perfiles"; }

static std::string ExeKey()
{
    std::string exe = State::Instance().gameExe;
    if (exe.empty())
        exe = wstring_to_string(Util::ExePath().filename().wstring());
    std::transform(exe.begin(), exe.end(), exe.begin(), [](unsigned char c) { return (char) std::tolower(c); });
    return exe;
}

static std::filesystem::path ProfilePath() { return Folder() / (string_to_wstring(ExeKey()) + L".perfil.ini"); }
static std::filesystem::path RestorePath() { return Folder() / (string_to_wstring(ExeKey()) + L".restauracion.ini"); }
static std::filesystem::path MatrixPath() { return Folder() / L"compatibilidad.ini"; }

// La matriz de compatibilidad: valores internos de respaldo, escritos al archivo si no existe para poder ampliarlos
// sin recompilar. «origen» distingue lo comprobado en ILION de lo documentado por la wiki de OptiScaler.
static const char* kDefaultMatrix =
    "; ILION — Perfiles inteligentes: compatibilidad por juego (se puede editar; se lee al optimizar)\n"
    "; [ejecutable.exe] en minusculas\n"
    ";   nombre   = nombre del juego\n"
    ";   origen   = ILION (comprobado en ILION) | OptiScaler wiki (documentado, sin comprobar en ILION)\n"
    ";   entradas = entradas de escalado que ofrece el juego (segun la fuente)\n"
    ";   evitar   = rutas que no se eligen: fg, xefg, dlssg_salida_dinamico, ratio\n"
    ";   nota     = texto que se muestra en el monitor\n"
    "\n"
    "[dawnwalker.exe]\n"
    "nombre = The Blood of Dawnwalker\n"
    "origen = ILION\n"
    "entradas = DLSS, FSR, XeSS\n"
    "evitar = dlssg_salida_dinamico\n"
    "nota = Comprobado en ILION (RX 7900 XTX): FSR 4 (driver, SDK y variantes), XeFG R3 y Enabler con Dinamico ILION. Con ReShade y XeFG/FSR FG ILION envuelve la fabrica DXGI.\n"
    "\n"
    "[kingdomcome.exe]\n"
    "nombre = Kingdom Come: Deliverance II\n"
    "origen = OptiScaler wiki\n"
    "entradas = DLSS, FSR3.1, XeSS\n"
    "evitar = dlssg_salida_dinamico\n"
    "nota = Wiki de OptiScaler: compatible, ver su entrada para HDR y XeFG. En ILION se observo la caida del rasterizado con salida DLSSG + Dynamic + limite.\n"
    "\n"
    "[cyberpunk2077.exe]\n"
    "nombre = Cyberpunk 2077\n"
    "origen = OptiScaler wiki\n"
    "entradas = DLSS, FSR3, FSR3.1/4, XeSS\n"
    "nota = Wiki de OptiScaler: compatible; FSR 4 aparece como preset del juego con OptiScaler.\n"
    "\n"
    "[b1-win64-shipping.exe]\n"
    "nombre = Black Myth: Wukong\n"
    "origen = OptiScaler wiki\n"
    "entradas = DLSS, FSR3, FSR4, XeSS\n"
    "nota = Wiki de OptiScaler: compatible, con soporte de OptiPatcher.\n"
    "\n"
    "[doomthedarkages.exe]\n"
    "nombre = Doom: The Dark Ages\n"
    "origen = OptiScaler wiki\n"
    "entradas = FSR3.1, XeSS\n"
    "nota = Wiki de OptiScaler: sin entrada DLSS.\n"
    "\n"
    "[hogwartslegacy.exe]\n"
    "nombre = Hogwarts Legacy\n"
    "origen = OptiScaler wiki\n"
    "entradas = DLSS, FSR3.1, XeSS\n"
    "nota = Wiki de OptiScaler: el spoofing tiene un coste notable; recomienda OptiPatcher.\n";

struct GameRule
{
    bool found = false;
    std::string name, origin, inputs, avoid, note;
    bool Avoids(const char* what) const { return avoid.find(what) != std::string::npos; }
};

static GameRule LoadRule()
{
    GameRule rule;
    std::error_code ec;
    std::filesystem::create_directories(Folder(), ec);

    if (!std::filesystem::exists(MatrixPath(), ec))
    {
        std::ofstream out(MatrixPath(), std::ios::binary);
        out << kDefaultMatrix;
    }

    CSimpleIniA ini(true);
    if (ini.LoadFile(MatrixPath().c_str()) < 0)
        ini.LoadData(kDefaultMatrix);

    const std::string section = ExeKey();
    if (ini.GetSectionSize(section.c_str()) < 0)
        return rule;

    rule.found = true;
    rule.name = ini.GetValue(section.c_str(), "nombre", "");
    rule.origin = ini.GetValue(section.c_str(), "origen", "");
    rule.inputs = ini.GetValue(section.c_str(), "entradas", "");
    rule.avoid = ini.GetValue(section.c_str(), "evitar", "");
    rule.note = ini.GetValue(section.c_str(), "nota", "");
    return rule;
}

// ---- Configuración: lo que un perfil guarda y restaura --------------------------------------------------------------
static std::string KeyOf(const UpscalerVariant& v)
{
    if (v.entry != nullptr)
        return "entry:" + wstring_to_string(v.entry->key);
    if (v.backend != Upscaler::Reset)
        return "backend:" + UpscalerToCode(v.backend);
    if (v.vkDriver)
        return "vk:" + std::to_string(v.ffxIndex);
    if (v.ffxIndex >= 0)
        return "ffx31";
    return "default";
}

// El escalador que corre (o, si nada corre, el elegido)
static const UpscalerVariant* Current(const UpscalerCatalog& catalog)
{
    const UpscalerVariant* best = nullptr;
    int bestRank = 0;
    for (const auto& v : catalog.variants)
    {
        const int rank = v.status.active ? 3 : v.status.loaded ? 2 : v.status.selected ? 1 : 0;
        if (rank > bestRank)
        {
            bestRank = rank;
            best = &v;
        }
    }
    return best;
}

static const UpscalerVariant* Find(const UpscalerCatalog& catalog, const std::string& key)
{
    for (const auto& v : catalog.variants)
        if (KeyOf(v) == key)
            return &v;
    return nullptr;
}

template <typename T> static std::string OptStr(const T& option)
{
    if (!option.has_value())
        return "auto";
    if constexpr (std::is_same_v<std::decay_t<decltype(option.value())>, bool>)
        return option.value() ? "true" : "false";
    else if constexpr (std::is_enum_v<std::decay_t<decltype(option.value())>>)
        return std::string(magic_enum::enum_name(option.value()));
    else
        return std::to_string(option.value());
}

static void SaveConfig(CSimpleIniA& ini, const char* section, IFeature* feature)
{
    auto config = Config::Instance();
    const auto catalog = GatherUpscalers(feature);
    const UpscalerVariant* current = Current(catalog);

    ini.SetValue(section, "escalador", current != nullptr ? KeyOf(*current).c_str() : "");
    ini.SetValue(section, "escalador_nombre", current != nullptr ? current->name.c_str() : "");
    ini.SetValue(section, "fsr4_modelo", config->Fsr4Model.has_value() ? std::to_string((int) config->Fsr4Model.value()).c_str() : "auto");
    ini.SetValue(section, "ratio_activo", OptStr(config->UpscaleRatioOverrideEnabled).c_str());
    ini.SetValue(section, "ratio", OptStr(config->UpscaleRatioOverrideValue).c_str());
    ini.SetValue(section, "fg_activado", OptStr(config->FGEnabled).c_str());
    ini.SetValue(section, "fg_entrada", OptStr(config->FGInput).c_str());
    ini.SetValue(section, "fg_salida", OptStr(config->FGOutput).c_str());
    ini.SetValue(section, "fg_nvngx", OptStr(config->FGNvngxReplacement).c_str());
    ini.SetValue(section, "xefg_dinamico", OptStr(config->FGXeFGDynamicMFG).c_str());
    ini.SetValue(section, "xefg_multiplicador", OptStr(config->FGXeFGInterpolationCount).c_str());
    ini.SetValue(section, "objetivo", OptStr(config->FGXeFGFramerateTargetDMFG).c_str());
    ini.SetValue(section, "objetivo_limite", OptStr(config->FGXeFGTargetAsCapDMFG).c_str());
    ini.SetValue(section, "dlssg_dinamico_ilion", OptStr(config->FGDLSSGIlionDynamic).c_str());
    ini.SetValue(section, "dlssg_dinamico_auto", OptStr(config->FGDLSSGOverrideForceDMFG).c_str());
    ini.SetValue(section, "dlssg_multiplicador", OptStr(config->FGDLSSGOverrideInterpolationCount).c_str());
}

template <typename T> static void SetFrom(T& option, const char* text, bool (*parse)(const char*, typename T::value_type&))
{
    if (text == nullptr)
        return;
    if (strcmp(text, "auto") == 0)
    {
        option.reset();
        return;
    }
    typename T::value_type v {};
    if (parse(text, v))
        option = v;
}

static bool ParseBool(const char* t, bool& v)
{
    v = strcmp(t, "true") == 0;
    return strcmp(t, "true") == 0 || strcmp(t, "false") == 0;
}
static bool ParseInt(const char* t, int& v)
{
    char* end = nullptr;
    v = (int) strtol(t, &end, 10);
    return end != t;
}
static bool ParseFloat(const char* t, float& v)
{
    char* end = nullptr;
    v = strtof(t, &end);
    return end != t;
}
template <typename E> static bool ParseEnum(const char* t, E& v)
{
    if (auto e = magic_enum::enum_cast<E>(std::string_view(t)); e.has_value())
    {
        v = *e;
        return true;
    }
    return false;
}

// Devuelve el escalador guardado (para elegirlo con el catálogo) y escribe el resto en la configuración en memoria.
// true si cambia la entrada o la salida de FG cargadas: necesita reiniciar
static bool LoadConfig(const CSimpleIniA& ini, const char* section, std::string& scalerKey, std::string& fsr4Model)
{
    auto config = Config::Instance();
    scalerKey = ini.GetValue(section, "escalador", "");
    fsr4Model = ini.GetValue(section, "fsr4_modelo", "auto");

    SetFrom(config->UpscaleRatioOverrideEnabled, ini.GetValue(section, "ratio_activo", nullptr), ParseBool);
    SetFrom(config->UpscaleRatioOverrideValue, ini.GetValue(section, "ratio", nullptr), ParseFloat);
    SetFrom(config->FGEnabled, ini.GetValue(section, "fg_activado", nullptr), ParseBool);
    SetFrom(config->FGInput, ini.GetValue(section, "fg_entrada", nullptr), ParseEnum<FGInput>);
    SetFrom(config->FGOutput, ini.GetValue(section, "fg_salida", nullptr), ParseEnum<FGOutput>);
    SetFrom(config->FGNvngxReplacement, ini.GetValue(section, "fg_nvngx", nullptr), ParseEnum<FGNvngxReplacement>);
    SetFrom(config->FGXeFGDynamicMFG, ini.GetValue(section, "xefg_dinamico", nullptr), ParseBool);
    SetFrom(config->FGXeFGInterpolationCount, ini.GetValue(section, "xefg_multiplicador", nullptr), ParseInt);
    SetFrom(config->FGXeFGFramerateTargetDMFG, ini.GetValue(section, "objetivo", nullptr), ParseFloat);
    SetFrom(config->FGXeFGTargetAsCapDMFG, ini.GetValue(section, "objetivo_limite", nullptr), ParseBool);
    SetFrom(config->FGDLSSGIlionDynamic, ini.GetValue(section, "dlssg_dinamico_ilion", nullptr), ParseBool);
    SetFrom(config->FGDLSSGOverrideForceDMFG, ini.GetValue(section, "dlssg_dinamico_auto", nullptr), ParseBool);
    SetFrom(config->FGDLSSGOverrideInterpolationCount, ini.GetValue(section, "dlssg_multiplicador", nullptr), ParseInt);

    // Los ajustes de DLSSG del juego se envían desde el hilo del juego (como el resto del menú)
    StreamlineHooks::requestDlssgOptionsUpdate("Perfiles");

    const auto& state = State::Instance();
    return config->FGInput.value_or_default() != state.activeFgInput || config->FGOutput.value_or_default() != state.activeFgOutput;
}

// ---- Detección -------------------------------------------------------------------------------------------------------
static std::string ArchName(const GpuInformation& gpu)
{
    using HW = device_info::HwGeneration;
    if (gpu.vendorId == VendorId::AMD)
    {
        switch (gpu.amdHwGeneration)
        {
        case HW::kGfx12:
            return "RDNA 4";
        case HW::kGfx11:
            return "RDNA 3";
        case HW::kGfx10_3:
            return "RDNA 2";
        case HW::kGfx10:
            return "RDNA 1";
        default:
            return "AMD (otra arquitectura)";
        }
    }
    if (gpu.vendorId == VendorId::Nvidia)
        return gpu.dlssCapable ? "NVIDIA RTX" : "NVIDIA (sin DLSS)";
    if (gpu.vendorId == VendorId::Intel)
        return "Intel";
    return "Desconocida";
}

static double RefreshHz()
{
    HWND window = Util::GetProcessWindow();
    HMONITOR monitor = MonitorFromWindow(window != nullptr ? window : GetDesktopWindow(), MONITOR_DEFAULTTONEAREST);
    MONITORINFOEXW info {};
    info.cbSize = sizeof(info);
    if (monitor == nullptr || !GetMonitorInfoW(monitor, &info))
        return 0.0;
    DEVMODEW mode {};
    mode.dmSize = sizeof(mode);
    if (!EnumDisplaySettingsW(info.szDevice, ENUM_CURRENT_SETTINGS, &mode) || mode.dmDisplayFrequency <= 1)
        return 0.0;
    return (double) mode.dmDisplayFrequency;
}

// FPS reales de esta sesión: los fotogramas del juego, nunca los generados
static Measure MeasureNow()
{
    Measure m;
    const auto status = IlionDynamicMfg::Get();
    const auto& state = State::Instance();
    const bool generating = status.gen == IlionDynamicMfg::Gen::Detected || status.gen == IlionDynamicMfg::Gen::Confirmed;

    if (generating)
    {
        if (status.baseFps > 0.0f)
        {
            m.baseFps = status.baseFps;
            m.measured = status.baseMeasured;
            m.source = status.baseMeasured ? "fotogramas reales contados con FG activo" : "estimado a partir de los FPS finales";
        }
        return m; // con FG no hay serie de tiempos reales para el 90 %
    }

    // Sin FG: los tiempos de los últimos fotogramas presentados son los reales
    int counted = 0, fast = 0;
    double sum = 0.0;
    for (const double ft : state.frameTimes)
    {
        if (ft <= 0.0 || ft > 1000.0)
            continue;
        counted++;
        sum += ft;
        fast += ft <= 1000.0 / 60.0 + 0.05 ? 1 : 0;
    }
    if (counted >= 120)
    {
        m.baseFps = 1000.0 / (sum / counted);
        m.share60 = (double) fast / counted;
        m.measured = true;
        m.source = StrFmt("%d fotogramas recientes, sin FG", counted);
    }
    return m;
}

// ---- Plan --------------------------------------------------------------------------------------------------------
static bool IsCommunityLite(const UpscalerVariant& v)
{
    if (v.entry == nullptr || v.entry->family != IlionUpscalers::Family::Fsr4 || v.entry->amdOfficial)
        return false;
    std::wstring key = v.entry->key;
    std::transform(key.begin(), key.end(), key.begin(), ::towlower);
    return v.name.find("Lite") != std::string::npos || key.find(L"lite") != std::wstring::npos || key.find(L"lossy") != std::wstring::npos;
}

static bool IsDriver(const UpscalerVariant& v) { return v.entry != nullptr && v.entry->kind == IlionUpscalers::Kind::Provider; }
static bool IsHlx(const UpscalerVariant& v) { return v.entry != nullptr && v.entry->hlx; }

static std::vector<Choice> Candidates(const UpscalerCatalog& catalog, const GpuInformation& gpu, bool insufficient)
{
    std::vector<Choice> out;
    const auto add = [&](const UpscalerVariant* v, bool lite, const char* why)
    {
        if (v == nullptr || !v->usable)
            return;
        for (const auto& c : out)
            if (c.key == KeyOf(*v) && c.lite == lite)
                return;
        out.push_back({ KeyOf(*v), v->name + (lite ? " · modelo Lite" : ""), lite, why });
    };
    const auto first = [&](auto pred) -> const UpscalerVariant*
    {
        for (const auto& v : catalog.variants)
            if (v.usable && pred(v))
                return &v;
        return nullptr;
    };
    const auto backend = [&](Upscaler u) { return first([u](const UpscalerVariant& v) { return v.backend == u; }); };

    const UpscalerVariant* driver = first(IsDriver);
    const UpscalerVariant* communityLite = first(IsCommunityLite);
    const UpscalerVariant* hlx = first(IsHlx);
    const UpscalerVariant* otherFsr4 = first([](const UpscalerVariant& v)
                                             { return v.entry != nullptr && v.entry->family == IlionUpscalers::Family::Fsr4 &&
                                                      !IsDriver(v) && !IsCommunityLite(v); });
    const UpscalerVariant* xess = backend(Upscaler::XeSS);
    const UpscalerVariant* fsr31 = first([](const UpscalerVariant& v) { return v.family == FamFsr31; });

    if (gpu.vendorId == VendorId::AMD)
    {
        // Prioridad aprobada: driver normal, driver Lite, Lite comunitaria, Helix FSR 4. Con rendimiento insuficiente
        // (medido) las opciones Lite van delante; Helix nunca delante del driver cuando este funciona
        if (insufficient)
        {
            add(driver, true, "Rendimiento insuficiente medido: FSR 4 del driver con su modelo Lite");
            add(communityLite, false, "Rendimiento insuficiente medido: FSR 4 Lite comunitaria");
            add(driver, false, "FSR 4 del driver, modelo normal");
        }
        else
        {
            add(driver, false, "Prioridad 1: FSR 4 del driver AMD, modelo normal");
            add(driver, true, "Prioridad 2: FSR 4 del driver con su modelo Lite");
            add(communityLite, false, "Prioridad 3: FSR 4 Lite comunitaria");
        }
        add(otherFsr4, false, "FSR 4 por DLL (el driver no está disponible)");
        add(hlx, false, "Prioridad 4: Helix FSR 4");
        add(xess, false, "Respaldo: XeSS");
        add(fsr31, false, "Respaldo: FSR 3.1");
    }
    else if (gpu.vendorId == VendorId::Nvidia)
    {
        add(backend(Upscaler::DLSS), false, "DLSS nativo de NVIDIA");
        add(xess, false, "XeSS (DP4a)");
        add(fsr31, false, "FSR 3.1");
    }
    else
    {
        add(xess, false, gpu.vendorId == VendorId::Intel ? "XeSS en Intel" : "XeSS (DP4a)");
        add(fsr31, false, "FSR 3.1");
    }

    add(backend(Upscaler::FSR22), false, "Respaldo final: FSR 2.2");
    return out;
}

static float RatioFor(Mode m, int outHeight, bool insufficient)
{
    // Calidad / Equilibrado / Rendimiento, un paso más bajo en 4K, un paso más alto en 1080p o menos
    static const float steps[] = { 1.3f, 1.5f, 1.7f, 2.0f, 2.3f };
    int index = m == Mode::Calidad ? 1 : m == Mode::Equilibrado ? 2 : 3;
    if (outHeight >= 2000)
        index++;
    else if (outHeight > 0 && outHeight <= 1100)
        index--;
    if (insufficient && m != Mode::Calidad)
        index++;
    return steps[std::clamp(index, 0, 4)];
}

// ---- Monitor -----------------------------------------------------------------------------------------------------
static void Add(Mark mark, std::string title, std::string detail = {}) { steps.push_back({ mark, std::move(title), std::move(detail) }); }
static Step& Last() { return steps.back(); }

static void Begin()
{
    steps.clear();
    plan = {};
    measure = {};
    candidate = 0;
    litePending = false;
    running = true;
    finished = false;
    phase = 0;
    phaseStart = ImGui::GetTime();
    entered = false;
    lastMessage.clear();
}

static void Next()
{
    phase++;
    phaseStart = ImGui::GetTime();
    entered = false;
}

static void Finish(Mark mark, const std::string& title, const std::string& detail = {})
{
    Add(mark, title, detail);
    running = false;
    finished = true;
}

static std::string NowText()
{
    const std::time_t t = std::time(nullptr);
    std::tm tm {};
    localtime_s(&tm, &t);
    char buf[32] = {};
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
    return buf;
}

static void SaveRestorePoint(IFeature* feature)
{
    CSimpleIniA ini(true);
    SaveConfig(ini, "configuracion", feature);
    ini.SetValue("configuracion", "fecha", NowText().c_str());
    std::error_code ec;
    std::filesystem::create_directories(Folder(), ec);
    ini.SaveFile(RestorePath().c_str());
}

static void SaveProfile(IFeature* feature)
{
    CSimpleIniA ini(true);
    SaveConfig(ini, "perfil", feature);
    ini.SetValue("perfil", "modo", mode == Mode::Calidad ? "Calidad" : mode == Mode::Equilibrado ? "Equilibrado" : "Rendimiento");
    ini.SetValue("perfil", "fecha", NowText().c_str());
    if (measure.baseFps > 0.0)
        ini.SetValue("perfil", "fps_reales_medidos", StrFmt("%.1f", measure.baseFps).c_str());
    if (measure.share60 >= 0.0)
        ini.SetValue("perfil", "tiempo_sobre_60", StrFmt("%.0f%%", measure.share60 * 100.0).c_str());
    std::error_code ec;
    std::filesystem::create_directories(Folder(), ec);
    ini.SaveFile(ProfilePath().c_str());
}

// ---- Optimizar juego: un paso por fotograma, los de espera hasta que el sistema confirma --------------------------------
template <typename Ctx> static void Tick(Ctx& ctx)
{
    if (!running)
        return;

    auto config = Config::Instance();
    auto& state = State::Instance();
    IFeature* feature = ctx.currentFeature;
    const double elapsed = ImGui::GetTime() - phaseStart;
    const GpuInformation gpu = IdentifyGpu::getPrimaryGpu();
    static GameRule rule;
    static double refresh = 0.0;
    static int outHeight = 0;

    switch (phase)
    {
    case 0: // juego y API
    {
        const std::string game = !state.gameName.empty() ? state.gameName : state.gameExe;
        Add(Mark::Ok, "Juego detectado: " + (game.empty() ? std::string("desconocido") : game),
            StrFmt("Ejecutable %s · API %s", ExeKey().c_str(), std::string(magic_enum::enum_name(state.api)).c_str()));
        if (state.api != API::DX12)
            Last().detail += " · Perfiles solo está probado en DirectX 12";
        Next();
        break;
    }
    case 1: // GPU y monitor
    {
        refresh = RefreshHz();
        outHeight = feature != nullptr && feature->IsInited() ? (int) feature->TargetHeight() : (int) state.screenHeight;
        Add(gpu.vendorId == VendorId::Invalid ? Mark::Bad : Mark::Ok, "GPU: " + gpu.name,
            StrFmt("Arquitectura %s · %.0f GB · FSR 4: %s · monitor %s", ArchName(gpu).c_str(),
                   gpu.dedicatedVramInBytes / (1024.0 * 1024.0 * 1024.0), std::string(magic_enum::enum_name(gpu.fsr4Support)).c_str(),
                   refresh > 0.0 ? StrFmt("%.0f Hz", refresh).c_str() : "desconocido"));
        Next();
        break;
    }
    case 2: // escalado que envía el juego
    {
        const bool upscaling = feature != nullptr && feature->IsInited();
        if (upscaling)
            Add(Mark::Ok, "El juego envía su escalado a ILION",
                StrFmt("Entrada %s · %s · %ux%u → %ux%u", ApiUpscalerInputName(state.currentInputApiName).c_str(), UpscalerLabel(feature).c_str(),
                       feature->RenderWidth(), feature->RenderHeight(), feature->TargetWidth(), feature->TargetHeight()));
        else
            Add(Mark::Bad, "El juego no envía escalado a ILION",
                "Activa DLSS, FSR o XeSS en las opciones gráficas del juego y vuelve a optimizar. Solo se revisará la generación de fotogramas.");
        plan.changeScaler = upscaling;
        Next();
        break;
    }
    case 3: // tecnologías disponibles
    {
        const auto catalog = GatherUpscalers(feature);
        std::string list;
        int usable = 0;
        for (const auto& v : catalog.variants)
        {
            if (!v.usable)
                continue;
            usable++;
            if (usable <= 7)
                list += (list.empty() ? "" : ", ") + v.name;
        }
        if (usable > 7)
            list += StrFmt(" y %d más", usable - 7);
        Add(usable > 0 ? Mark::Ok : Mark::Bad, StrFmt("Tecnologías de escalado disponibles: %d", usable), list);
        Next();
        break;
    }
    case 4: // compatibilidad del juego
    {
        rule = LoadRule();
        if (rule.found)
            Add(Mark::Info, StrFmt("Compatibilidad del juego: %s", rule.origin.c_str()),
                (rule.inputs.empty() ? std::string() : "Entradas: " + rule.inputs + ". ") + rule.note);
        else
            Add(Mark::Info, "Compatibilidad del juego: sin datos",
                "No está en ILION\\Perfiles\\compatibilidad.ini: se aplican las reglas generales");
        Next();
        break;
    }
    case 5: // medición
    {
        measure = MeasureNow();
        if (measure.baseFps <= 0.0)
            Add(Mark::Wait, "Rendimiento: sin medición suficiente", "La recomendación será estimada por arquitectura y modo");
        else if (measure.share60 >= 0.0)
            Add(measure.share60 >= 0.9 ? Mark::Ok : Mark::Wait, StrFmt("Rendimiento medido: %.0f FPS reales", measure.baseFps),
                StrFmt("%.0f%% del tiempo observado con 60 FPS reales o más (criterio: 90%%) · %s", measure.share60 * 100.0,
                       measure.source.c_str()));
        else
            Add(Mark::Wait, StrFmt("Rendimiento: %.0f FPS reales", measure.baseFps),
                "El 90% del tiempo no se puede comprobar con FG activo: " + measure.source);
        Next();
        break;
    }
    case 6: // escalador y variante
    {
        if (!plan.changeScaler)
        {
            Add(Mark::Info, "Escalador: no se cambia");
            Next();
            break;
        }
        const bool insufficient = measure.measured && measure.baseFps > 0.0 &&
                                  (measure.baseFps < 60.0 || (measure.share60 >= 0.0 && measure.share60 < 0.9)) && mode != Mode::Calidad;
        const auto catalog = GatherUpscalers(feature);
        plan.candidates = Candidates(catalog, gpu, insufficient);
        if (plan.candidates.empty())
        {
            plan.changeScaler = false;
            Add(Mark::Bad, "Ningún escalador compatible disponible", "Se mantiene el actual");
        }
        else
        {
            const auto& c = plan.candidates.front();
            std::string rest;
            for (size_t i = 1; i < plan.candidates.size() && i < 4; i++)
                rest += (rest.empty() ? "" : ", ") + plan.candidates[i].name;
            Add(Mark::Ok, "Escalador elegido: " + c.name, c.why + (rest.empty() ? std::string() : ". Alternativas si falla: " + rest));
        }
        plan.ratio = plan.changeScaler && !(rule.found && rule.Avoids("ratio")) ? RatioFor(mode, outHeight, insufficient) : 0.0f;
        Next();
        break;
    }
    case 7: // modo de escalado
    {
        if (plan.ratio > 0.0f)
            Add(Mark::Info, StrFmt("Modo de escalado: proporción %.2f", plan.ratio),
                StrFmt("Modo %s, salida de %d líneas", mode == Mode::Calidad ? "Calidad" : mode == Mode::Equilibrado ? "Equilibrado" : "Rendimiento",
                       outHeight));
        else
            Add(Mark::Info, "Modo de escalado: no se cambia");
        Next();
        break;
    }
    case 8: // generación de fotogramas
    {
        const auto status = IlionDynamicMfg::Get();
        const double minBase = mode == Mode::Calidad ? 60.0 : mode == Mode::Equilibrado ? 55.0 : 45.0;
        const int modeMax = mode == Mode::Calidad ? 2 : mode == Mode::Equilibrado ? 3 : 4;
        float target = refresh > 0.0 ? (float) refresh - 3.0f : 0.0f;
        if (const float limit = config->FramerateLimit.value_or_default(); limit > 0.0f && (target <= 0.0f || limit < target))
            target = limit;

        if (rule.found && rule.Avoids("fg"))
            Add(Mark::Info, "Generación de fotogramas: no se cambia", "La matriz de compatibilidad la excluye para este juego");
        else if (state.api != API::DX12)
            Add(Mark::Info, "Generación de fotogramas: no se cambia", "Solo DirectX 12 está verificado");
        else if (target <= 0.0f)
            Add(Mark::Wait, "Generación de fotogramas: no se cambia", "No se conoce la frecuencia del monitor ni hay límite de FPS");
        else if (measure.baseFps <= 0.0)
            Add(Mark::Wait, "Generación de fotogramas: no se cambia", "Sin FPS reales medidos no se calcula el multiplicador");
        else if (measure.baseFps < minBase)
            Add(Mark::Wait, StrFmt("Generación de fotogramas: no recomendada (%.0f FPS reales)", measure.baseFps),
                StrFmt("El modo exige al menos %.0f FPS reales; la latencia empeoraría", minBase));
        else
        {
            plan.target = target;
            const int wanted = std::clamp((int) std::ceil(target / measure.baseFps), 2, modeMax);
            const char* route = nullptr;

            if (status.provider == IlionDynamicMfg::Provider::DlssgGame && state.activeFgOutput == FGOutput::NoFG &&
                StreamlineHooks::isDlssgHooked() && state.dlssgMfgMax.value_or(0) >= 2)
            {
                plan.fg = FgRoute::IlionDynamic;
                plan.maxCount = std::clamp(wanted - 1, 1, state.dlssgMfgMax.value_or(1));
                route = "DLSSG del juego con Dinámico ILION (con límite de FPS)";
            }
            else if (state.activeFgOutput == FGOutput::XeFG && state.currentFG != nullptr &&
                     !(rule.found && rule.Avoids("xefg")))
            {
                plan.fg = FgRoute::XeFgDynamic;
                plan.maxCount = std::clamp(wanted - 1, 1, std::max(state.currentFG->GetMaxInterpolationCount(), 1));
                route = "XeFG con MFG dinámico R3 (con límite de FPS)";
            }
            else if (state.activeFgOutput == FGOutput::DLSSG)
            {
                Add(Mark::Wait, "Generación de fotogramas: ruta en revisión",
                    "La salida DLSSG de ILION con Dynamic y límite reduce el rasterizado; se mantiene la configuración actual");
            }
            else if (state.activeFgOutput == FGOutput::NoFG && !(rule.found && rule.Avoids("xefg")) &&
                     std::filesystem::exists(Util::DllPath().parent_path() / L"libxess_fg.dll") &&
                     (state.streamlineVersion.major >= 2 || (feature != nullptr && feature->IsInited())))
            {
                plan.fg = FgRoute::XeFgNew;
                plan.fgInput = state.streamlineVersion.major >= 2 ? FGInput::DLSSG : FGInput::Upscaler;
                plan.maxCount = wanted - 1;
                route = plan.fgInput == FGInput::DLSSG ? "XeFG nueva (entrada DLSSG del juego) con MFG dinámico R3"
                                                       : "XeFG nueva (entrada OptiFG) con MFG dinámico R3";
            }
            else
                Add(Mark::Info, "Generación de fotogramas: no se cambia", "No hay una ruta verificada para esta combinación");

            if (route != nullptr)
                Add(Mark::Ok, StrFmt("Generación de fotogramas: %s", route),
                    StrFmt("Objetivo %.0f FPS finales · multiplicador máximo %dX (%.0f FPS reales × %d) · modo %s", target, plan.maxCount + 1,
                           measure.baseFps, plan.maxCount + 1,
                           mode == Mode::Calidad ? "Calidad" : mode == Mode::Equilibrado ? "Equilibrado" : "Rendimiento"));
        }
        Next();
        break;
    }
    case 9: // punto de restauración
    {
        SaveRestorePoint(feature);
        Add(Mark::Ok, "Punto de restauración guardado", "ILION\\Perfiles\\" + ExeKey() + ".restauracion.ini");
        Next();
        break;
    }
    case 10: // aplicar el escalador: el candidato debe llegar a ejecutarse
    {
        if (!plan.changeScaler || candidate >= plan.candidates.size())
        {
            if (plan.changeScaler)
            {
                // Ninguno se ejecutó: se vuelve al escalador de antes
                CSimpleIniA ini(true);
                ini.LoadFile(RestorePath().c_str());
                const std::string key = ini.GetValue("configuracion", "escalador", "");
                const auto catalog = GatherUpscalers(feature);
                if (const auto* v = Find(catalog, key))
                    ChooseUpscalerVariant(*v);
                Add(Mark::Bad, "Ningún escalador candidato llegó a ejecutarse", "Se ha vuelto al escalador anterior");
            }
            phase = 12;
            phaseStart = ImGui::GetTime();
            entered = false;
            break;
        }

        const Choice& c = plan.candidates[candidate];
        const auto catalog = GatherUpscalers(feature);
        const UpscalerVariant* v = Find(catalog, c.key);

        if (!entered)
        {
            entered = true;
            if (v == nullptr || !v->usable || FfxApiProxy::SrSwitchLocked())
            {
                Add(Mark::Bad, "No se pudo elegir " + c.name, v == nullptr ? "Ya no está en el catálogo" : v->reason);
                candidate++;
                phaseStart = ImGui::GetTime();
                entered = false;
                break;
            }
            if (!v->status.active)
                ChooseUpscalerVariant(*v);
            if (!c.lite && IsDriver(*v) && config->Fsr4Model.has_value())
            {
                config->Fsr4Model.reset(); // modelo normal: el que elige FFX
                State::Instance().newBackend = currentBackend != Upscaler::Reset ? currentBackend : Upscaler::FFX;
                MARK_ALL_BACKENDS_CHANGED();
            }
            Add(Mark::Wait, "Aplicando " + c.name, "Esperando a que el escalador se ejecute");
            break;
        }

        if (v != nullptr && v->status.active && !litePending)
        {
            if (c.lite)
            {
                // El modelo Lite del driver: solo si la DLL en uso lo ofrece
                const auto models = FSR4Models::SelectableModels(FfxApiProxy::Dx12Module_SR());
                if (std::find(models.begin(), models.end(), FSR4Model::INT8Lite) == models.end())
                {
                    Last() = { Mark::Bad, "El driver no ofrece el modelo Lite", "Se prueba el siguiente candidato" };
                    candidate++;
                    phaseStart = ImGui::GetTime();
                    entered = false;
                    break;
                }
                const std::optional<FSR4Model> previous =
                    config->Fsr4Model.has_value() ? std::optional<FSR4Model>(config->Fsr4Model.value()) : std::nullopt;
                config->Fsr4Model = FSR4Model::INT8Lite;
                FfxApiProxy::ArmSrModelRevert(previous);
                State::Instance().newBackend = Upscaler::FFX;
                MARK_ALL_BACKENDS_CHANGED();
                litePending = true;
                phaseStart = ImGui::GetTime();
                Last().detail = "Ejecutándose; cambiando al modelo Lite";
                break;
            }
            Last() = { Mark::Ok, "Escalador aplicado: " + c.name, "Confirmado: es el que ejecuta el último escalado" };
            Next();
            break;
        }

        if (litePending && elapsed > 1.5)
        {
            const auto active = FSR4Models::ActiveModel(FfxApiProxy::Dx12Module_SR());
            if (v != nullptr && v->status.active && config->Fsr4Model.has_value() && config->Fsr4Model.value() == FSR4Model::INT8Lite)
            {
                Last() = { Mark::Ok, "Escalador aplicado: " + c.name,
                           active.has_value() ? StrFmt("Modelo determinado por FFX: %s", FSR4Models::Name(*active)) : "Modelo Lite pedido" };
                litePending = false;
                Next();
                break;
            }
        }

        if (elapsed > 10.0)
        {
            Last() = { Mark::Bad, "No llegó a ejecutarse: " + c.name, "Se prueba el siguiente candidato" };
            litePending = false;
            candidate++;
            phaseStart = ImGui::GetTime();
            entered = false;
        }
        break;
    }
    case 11: // modo de escalado
    {
        if (plan.ratio > 0.0f)
        {
            config->UpscaleRatioOverrideEnabled = true;
            config->UpscaleRatioOverrideValue = plan.ratio;
            Add(Mark::Wait, StrFmt("Proporción de escalado %.2f guardada", plan.ratio),
                "Se aplica cuando el juego vuelva a pedir su resolución: cambia la calidad en el juego o reinícialo");
        }
        Next();
        break;
    }
    case 12: // generación de fotogramas
    {
        if (!entered)
        {
            entered = true;
            switch (plan.fg)
            {
            case FgRoute::IlionDynamic:
                IlionDynamicMfg::SetExternalDynamicMode(IlionDynamicMfg::ExternalMode::Ilion);
                IlionDynamicMfg::SetMaxCount(plan.maxCount);
                IlionDynamicMfg::SetTargetFps(plan.target);
                IlionDynamicMfg::SetTargetAsCap(true);
                Add(Mark::Wait, "Aplicando Dinámico ILION", "Esperando a que el backend acepte el multiplicador");
                break;
            case FgRoute::XeFgDynamic:
                config->FGXeFGInterpolationCount = plan.maxCount;
                config->FGXeFGDynamicMFG = true;
                config->FGEnabled = true;
                IlionDynamicMfg::SetTargetFps(plan.target);
                IlionDynamicMfg::SetTargetAsCap(true);
                Add(Mark::Wait, "Aplicando MFG dinámico R3 de XeFG", "Esperando a que XeFG genere");
                break;
            case FgRoute::XeFgNew:
                config->FGInput = plan.fgInput;
                config->FGOutput = FGOutput::XeFG;
                config->FGEnabled = true;
                config->FGXeFGInterpolationCount = plan.maxCount;
                config->FGXeFGDynamicMFG = true;
                IlionDynamicMfg::SetTargetFps(plan.target);
                IlionDynamicMfg::SetTargetAsCap(true);
                Add(Mark::Wait, "XeFG configurado: requiere reiniciar el juego",
                    "No se muestra como aplicado hasta que el juego arranque con XeFG");
                Next();
                break;
            default:
                Next();
                break;
            }
            break;
        }

        const auto status = IlionDynamicMfg::Get();
        if (plan.fg == FgRoute::IlionDynamic && status.ilionRefused != nullptr)
        {
            Last() = { Mark::Bad, "Dinámico ILION detenido", status.ilionRefused };
            Next();
        }
        else if (plan.fg == FgRoute::IlionDynamic && status.ilionDrives && status.appliedSource == IlionDynamicMfg::AppliedSource::Measured)
        {
            Last() = { Mark::Ok, "Dinámico ILION aplicado",
                       StrFmt("Medido %dX · objetivo %.0f FPS finales con límite", status.multiplier, plan.target) };
            Next();
        }
        else if (plan.fg == FgRoute::XeFgDynamic && (State::Instance().activeFgOutput == FGOutput::XeFG && Config::Instance()->FGXeFGDynamicMFG.value_or_default()) && status.gen == IlionDynamicMfg::Gen::Confirmed)
        {
            Last() = { Mark::Ok, "MFG dinámico R3 aplicado", StrFmt("XeFG genera · objetivo %.0f FPS finales con límite", plan.target) };
            Next();
        }
        else if (elapsed > 8.0)
        {
            Last() = { Mark::Bad, "La generación de fotogramas no se confirmó en 8 s", "Revisa la pestaña FRAME GEN; el punto de restauración sigue disponible" };
            Next();
        }
        break;
    }
    case 13: // guardar
    {
        config->SaveIni();
        // Never overwritten silently: the profile there before is kept apart and named here
        std::error_code ec;
        if (std::filesystem::exists(ProfilePath(), ec))
        {
            const auto previous = Folder() / (string_to_wstring(ExeKey()) + L".perfil.anterior.ini");
            std::filesystem::copy_file(ProfilePath(), previous, std::filesystem::copy_options::overwrite_existing, ec);
            Add(ec ? Mark::Bad : Mark::Info, ec ? "No se pudo conservar el perfil anterior" : "Perfil anterior conservado",
                "ILION\Perfiles\\" + ExeKey() + ".perfil.anterior.ini");
        }
        SaveProfile(feature);
        const bool pending = std::any_of(steps.begin(), steps.end(), [](const Step& s) { return s.mark == Mark::Wait; });
        const bool failed = std::any_of(steps.begin(), steps.end(), [](const Step& s) { return s.mark == Mark::Bad; });
        Finish(failed ? Mark::Bad : pending ? Mark::Wait : Mark::Ok,
               failed ? "Perfil guardado con avisos" : pending ? "Perfil guardado: hay cambios pendientes de reinicio" : "Perfil guardado y aplicado",
               "ILION\\Perfiles\\" + ExeKey() + ".perfil.ini");
        break;
    }
    default:
        running = false;
        break;
    }
}

// Cargar un perfil o restaurar el punto de restauración: el mismo camino de aplicación, con su escalador guardado
template <typename Ctx> static void ApplyFile(Ctx& ctx, const std::filesystem::path& path, const char* section, const char* what)
{
    Begin();
    running = false;
    finished = true;

    CSimpleIniA ini(true);
    if (ini.LoadFile(path.c_str()) < 0)
    {
        Add(Mark::Bad, std::string("No se pudo leer ") + what, wstring_to_string(path.wstring()));
        return;
    }

    std::string scalerKey, model;
    const bool restart = LoadConfig(ini, section, scalerKey, model);
    auto config = Config::Instance();

    if (model == "auto")
        config->Fsr4Model.reset();
    else if (int value = 0; ParseInt(model.c_str(), value) && value >= 0 && value < (int) FSR4Model::Count)
        config->Fsr4Model = (FSR4Model) value;

    const auto catalog = GatherUpscalers(ctx.currentFeature);
    if (const auto* v = Find(catalog, scalerKey); v != nullptr && v->usable)
    {
        ChooseUpscalerVariant(*v);
        State::Instance().newBackend = currentBackend != Upscaler::Reset ? currentBackend : Upscaler::FFX;
        MARK_ALL_BACKENDS_CHANGED();
        Add(Mark::Ok, std::string(what) + ": escalador " + v->name, "Se aplica en caliente o al reinicializar el escalado");
    }
    else if (!scalerKey.empty())
        Add(Mark::Bad, std::string(what) + ": el escalador guardado no está disponible", scalerKey);

    config->SaveIni();
    Add(restart ? Mark::Wait : Mark::Ok, std::string(what) + (restart ? ": requiere reiniciar el juego" : ": aplicado"),
        restart ? "La entrada o la salida de FG cambian al reiniciar" : ini.GetValue(section, "fecha", ""));
}

// ---- Pantalla ----------------------------------------------------------------------------------------------------
static ImVec4 MarkColour(Mark m)
{
    switch (m)
    {
    case Mark::Ok:
        return IlionTheme::ShellPilot(IlionTheme::ShellPal::Green);
    case Mark::Wait:
        return IlionTheme::ShellPilot(IlionTheme::ShellPal::Yellow);
    case Mark::Bad:
        return IlionTheme::ShellPal::Red;
    default:
        return IlionTheme::ShellPal::Text2;
    }
}

static void Row(Mark mark, const std::string& title, const std::string& detail)
{
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float h = ImGui::GetTextLineHeight();
    IlionTheme::ShellLed(ImGui::GetWindowDrawList(), ImVec2(p.x + h * 0.5f, p.y + h * 0.5f), h * 0.24f, toneMapColor(MarkColour(mark)),
                         mark != Mark::Info);
    ImGui::Dummy(ImVec2(h, h));
    ImGui::SameLine(0.0f, h * 0.45f);
    ImGui::BeginGroup();
    ImGui::PushStyleColor(ImGuiCol_Text, toneMapColor(mark == Mark::Info ? IlionTheme::ShellPal::Text : MarkColour(mark)));
    ImGui::TextWrapped("%s", title.c_str());
    ImGui::PopStyleColor();
    if (!detail.empty())
    {
        ImGui::PushStyleColor(ImGuiCol_Text, toneMapColor(IlionTheme::ShellPal::Text2));
        ImGui::TextWrapped("%s", detail.c_str());
        ImGui::PopStyleColor();
    }
    ImGui::EndGroup();
    ImGui::Dummy(ImVec2(0.0f, h * 0.2f));
}

// La configuración activa, leída del sistema: lo que el monitor muestra fuera de una operación
template <typename Ctx> static void ActiveRows(Ctx& ctx)
{
    const auto& state = State::Instance();
    const auto catalog = GatherUpscalers(ctx.currentFeature);
    const UpscalerVariant* current = Current(catalog);
    const bool upscaling = ctx.currentFeature != nullptr && ctx.currentFeature->IsInited();

    Row(upscaling ? Mark::Ok : Mark::Wait, "Escalador: " + (current != nullptr ? current->name : std::string("ninguno")),
        upscaling ? StrFmt("%ux%u → %ux%u", ctx.currentFeature->RenderWidth(), ctx.currentFeature->RenderHeight(),
                           ctx.currentFeature->TargetWidth(), ctx.currentFeature->TargetHeight())
                  : std::string("El juego no envía escalado ahora"));

    const auto status = IlionDynamicMfg::Get();
    const bool generating = status.gen == IlionDynamicMfg::Gen::Detected || status.gen == IlionDynamicMfg::Gen::Confirmed;
    std::string fg = status.provider == IlionDynamicMfg::Provider::None ? "sin generación de fotogramas" : status.name;
    if (generating && status.multiplier > 0)
        fg += StrFmt(" · %dX%s", status.multiplier, status.appliedSource == IlionDynamicMfg::AppliedSource::Measured ? " medido" : "");
    if (status.ilionDrives)
        fg += " · Dinámico ILION";
    else if (status.backendDynamic)
        fg += " · Dinámico automático";
    else if ((State::Instance().activeFgOutput == FGOutput::XeFG && Config::Instance()->FGXeFGDynamicMFG.value_or_default()))
        fg += " · MFG dinámico R3";
    Row(generating ? Mark::Ok : Mark::Info, "Generación de fotogramas: " + fg,
        status.baseFps > 0.0f ? StrFmt("%.0f FPS reales · %.0f FPS finales", status.baseFps, status.finalFps) : std::string());

    if (status.limitFps > 0.0f)
        Row(Mark::Info, StrFmt("Objetivo y límite: %.0f FPS finales", status.limitFps), {});

    std::error_code ec;
    if (std::filesystem::exists(ProfilePath(), ec))
    {
        CSimpleIniA ini(true);
        ini.LoadFile(ProfilePath().c_str());
        Row(Mark::Ok, std::string("Perfil guardado: modo ") + ini.GetValue("perfil", "modo", "?"), ini.GetValue("perfil", "fecha", ""));

        // Lo que quedó pendiente de reinicio: confirmado si el juego arrancó con esa salida de FG
        if (const char* out = ini.GetValue("perfil", "fg_salida", nullptr); out != nullptr && strcmp(out, "auto") != 0)
        {
            const bool same = std::string(magic_enum::enum_name(state.activeFgOutput)) == out;
            Row(same ? Mark::Ok : Mark::Wait, StrFmt("Salida de FG del perfil: %s", out),
                same ? "Cargada en esta sesión" : "Aún no cargada: requiere reiniciar el juego");
        }
    }
    else
        Row(Mark::Info, "Sin perfil guardado para este juego", "Pulsa OPTIMIZAR JUEGO o GUARDAR PERFIL");
}

template <typename Ctx> static void Panel(Ctx& ctx)
{
    Tick(ctx);

    const auto& state = State::Instance();
    const GpuInformation gpu = IdentifyGpu::getPrimaryGpu();
    const float font = ImGui::GetFontSize();
    const bool columns = ImGui::GetContentRegionAvail().x > font * 40.0f;
    std::error_code ec;
    const bool hasProfile = std::filesystem::exists(ProfilePath(), ec);
    const bool hasRestore = std::filesystem::exists(RestorePath(), ec);

    if (columns && !ImGui::BeginTable("##IlionProfiles", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings))
        return;
    if (columns)
    {
        ImGui::TableSetupColumn("##controls", ImGuiTableColumnFlags_WidthStretch, 0.85f);
        ImGui::TableSetupColumn("##monitor", ImGuiTableColumnFlags_WidthStretch, 1.25f);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
    }

    // ---- Controles ----
    const std::string game = !state.gameName.empty() ? state.gameName : state.gameExe;
    ImGui::PushFont(ImGui::GetFont(), std::floor(font * 1.45f));
    ImGui::PushStyleColor(ImGuiCol_Text, toneMapColor(IlionTheme::ShellPal::Text));
    ImGui::TextWrapped("%s", game.empty() ? "Juego desconocido" : game.c_str());
    ImGui::PopStyleColor();
    ImGui::PopFont();
    ImGui::PushStyleColor(ImGuiCol_Text, toneMapColor(IlionTheme::ShellPal::Text2));
    ImGui::TextWrapped("%s · %s", gpu.name.c_str(), ArchName(gpu).c_str());
    ImGui::TextWrapped("API %s · FSR 4: %s", std::string(magic_enum::enum_name(state.api)).c_str(),
                       std::string(magic_enum::enum_name(gpu.fsr4Support)).c_str());
    ImGui::PopStyleColor();
    ImGui::Spacing();
    ImGui::Spacing();

    ImGui::TextUnformatted("Modo");
    const char* modes[] = { "Calidad", "Equilibrado", "Rendimiento" };
    const float modeWidth = std::floor((ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * 2.0f) / 3.0f);
    ImGui::BeginDisabled(running);
    for (int i = 0; i < 3; i++)
    {
        if (i > 0)
            ImGui::SameLine();
        const bool selected = (int) mode == i;
        if (selected)
            ImGui::PushStyleColor(ImGuiCol_Text, toneMapColor(IlionTheme::ShellPilot(IlionTheme::ShellPal::Green)));
        if (IlionTheme::Button(StrFmt("%s%s##ProfileMode%d", selected ? "» " : "", modes[i], i).c_str(), ImVec2(modeWidth, 0.0f)))
            mode = (Mode) i;
        if (selected)
            ImGui::PopStyleColor();
    }
    ImGui::PushStyleColor(ImGuiCol_Text, toneMapColor(IlionTheme::ShellPal::Text2));
    ImGui::TextWrapped("%s", mode == Mode::Calidad      ? "Prioriza la imagen; FG hasta 2X y solo con 60 FPS reales o más."
                             : mode == Mode::Equilibrado ? "Imagen y fluidez; FG hasta 3X desde 55 FPS reales."
                                                         : "Prioriza los FPS; modelos Lite si hace falta y FG hasta 4X desde 45 FPS reales.");
    ImGui::PopStyleColor();
    ImGui::Spacing();

    const float full = ImGui::GetContentRegionAvail().x;
    ImGui::PushStyleColor(ImGuiCol_Text, toneMapColor(IlionTheme::ShellPal::Orange));
    if (IlionTheme::Button(running ? "OPTIMIZANDO…##ProfilesGo" : "OPTIMIZAR JUEGO##ProfilesGo", ImVec2(full, std::floor(font * 2.6f))) && !running)
        Begin();
    ImGui::PopStyleColor();
    ImGui::Spacing();

    if (IlionTheme::Button("GUARDAR PERFIL##ProfilesSave", ImVec2(full, 0.0f)))
    {
        if (hasProfile && !ImGui::GetIO().KeyShift)
            lastMessage = "Ya hay un perfil: pulsa GUARDAR PERFIL con Mayús para sobrescribirlo";
        else
        {
            SaveProfile(ctx.currentFeature);
            lastMessage = "Perfil guardado con la configuración actual";
        }
    }
    ImGui::BeginDisabled(!hasProfile);
    if (IlionTheme::Button("CARGAR PERFIL##ProfilesLoad", ImVec2(full, 0.0f)))
        ApplyFile(ctx, ProfilePath(), "perfil", "Perfil");
    ImGui::EndDisabled();
    ImGui::BeginDisabled(!hasRestore);
    if (IlionTheme::Button("RESTAURAR CONFIGURACIÓN ANTERIOR##ProfilesRestore", ImVec2(full, 0.0f)))
        ApplyFile(ctx, RestorePath(), "configuracion", "Restauración");
    ImGui::EndDisabled();
    ImGui::EndDisabled();

    if (!lastMessage.empty())
    {
        ImGui::PushStyleColor(ImGuiCol_Text, toneMapColor(IlionTheme::ShellPilot(IlionTheme::ShellPal::Yellow)));
        ImGui::TextWrapped("%s", lastMessage.c_str());
        ImGui::PopStyleColor();
    }
    ImGui::PushStyleColor(ImGuiCol_Text, toneMapColor(IlionTheme::ShellPal::Dim));
    ImGui::TextWrapped("%s", "Prototipo experimental. No hay benchmarks: la elección sigue las reglas de compatibilidad y lo medido "
                             "en esta sesión. Nada cambia solo durante la partida.");
    ImGui::PopStyleColor();

    // ---- Monitor ----
    if (columns)
        ImGui::TableSetColumnIndex(1);
    else
        ImGui::Spacing();

    ImGui::PushStyleColor(ImGuiCol_ChildBg, toneMapColor(IlionTheme::ShellPal::Field));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(font * 0.8f, font * 0.6f));
    const float height = columns ? std::max(ImGui::GetContentRegionAvail().y, font * 18.0f) : 0.0f;
    if (ImGui::BeginChild("##ProfilesMonitor", ImVec2(0.0f, height),
                          columns ? ImGuiChildFlags_Borders : (ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY)))
    {
        ImGui::PushStyleColor(ImGuiCol_Text, toneMapColor(IlionTheme::ShellPal::Yellow));
        ImGui::TextUnformatted("MONITOR DE CONFIGURACIÓN");
        ImGui::PopStyleColor();
        ImGui::Separator();
        ImGui::Spacing();

        if (steps.empty())
        {
            ImGui::PushStyleColor(ImGuiCol_Text, toneMapColor(IlionTheme::ShellPal::Text2));
            ImGui::TextUnformatted("Configuración activa");
            ImGui::PopStyleColor();
            ImGui::Spacing();
            ActiveRows(ctx);
        }
        else
        {
            for (const auto& s : steps)
                Row(s.mark, s.title, s.detail);

            if (finished)
            {
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();
                ImGui::PushStyleColor(ImGuiCol_Text, toneMapColor(IlionTheme::ShellPal::Text2));
                ImGui::TextUnformatted("Configuración activa");
                ImGui::PopStyleColor();
                ImGui::Spacing();
                ActiveRows(ctx);
            }
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();

    if (columns)
        ImGui::EndTable();
}
} // namespace IlionProfiles

#pragma execution_character_set(pop)

void MenuCommon::RenderProfilesTab(RenderMenuContext& ctx) { IlionProfiles::Panel(ctx); }
