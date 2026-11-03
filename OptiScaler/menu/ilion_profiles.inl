// ILION — Perfiles inteligentes (prototipo experimental, rama ilion-perfiles-exp). Prueba 2
//
// «Optimizar juego» configura el juego antes de la sesión: detecta juego, GPU y monitor, evalúa lo que se ha medido,
// elige escalador, modo de escalado y una generación de fotogramas FIJA, guarda un punto de restauración, aplica con los
// mecanismos del menú (catálogo de escaladores, ChooseUpscalerVariant, modelo de FSR 4, multiplicador del proveedor)
// y guarda el perfil. Jerarquía de decisión:
//   1. Proteger los FPS reales (60 o más el 90 % del tiempo observado; los fotogramas generados no cuentan)
//   2. La mayor calidad de imagen que lo permita
//   3. Generación de fotogramas solo con una base sana, 2X por defecto
//   4. La frecuencia del monitor como referencia, nunca como objetivo a alcanzar
// El MFG dinámico (R3, Dinámico ILION, eDynamic) queda fuera: Perfiles no lo activa, no lo cambia y no lo guarda. Si ya
// está activo se muestra como configuración externa y el FG fijo queda pendiente de que el usuario lo resuelva.
// Nada cambia solo: un escalador que no llega a ejecutarse se informa (con restauración), no se sustituye por otro.
//
// Archivos (junto al juego):
//   ILION\Perfiles\<exe>.perfil.ini            el perfil guardado del juego
//   ILION\Perfiles\<exe>.perfil.anterior.ini   el perfil que había antes de la última optimización
//   ILION\Perfiles\<exe>.restauracion.ini      la configuración de antes de la última optimización
//   ILION\Perfiles\compatibilidad.ini          la matriz de compatibilidad por juego (se crea con valores internos)
//
// Este archivo se guarda en UTF-8 con BOM, como el resto de fuentes del menú: sin el BOM, MSVC lo lee con la página de
// códigos del sistema y cada carácter acentuado llega al menú como dos («Ã³»).
//
// Incluido por menu_common.cpp antes de MenuTabs: usa el catálogo, los selectores y la medición de coste del menú.

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

static const char* ModeName(Mode m) { return m == Mode::Calidad ? "Calidad" : m == Mode::Equilibrado ? "Equilibrado" : "Rendimiento"; }

// Pilotos: verde aplicado / verificado, ámbar pendiente o estimado, rojo incompatible o fallo, rojo de acción (reinicio
// obligatorio: no es un error), neutro dato
enum class Mark
{
    Ok,
    Wait,
    Bad,
    Restart,
    Info,
};

struct Step
{
    Mark mark = Mark::Info;
    std::string title;
    std::string detail;
};

// Estado de cada parte del plan, de menos a más firme
enum class Stage
{
    None,         // no se cambia
    Recommended,  // recomendado, no aplicado (decide el usuario)
    Selected,     // elegido en la configuración
    Saved,        // guardado en el ini
    Pending,      // pendiente: el juego debe pedir de nuevo su resolución, o reiniciarse
    Applied,      // aplicado, sin confirmación todavía
    Verified,     // el sistema confirma que es lo que corre
    Blocked,      // incompatible con la configuración actual: lo resuelve el usuario
    Failed,       // no llegó a ejecutarse
};

static const char* StageName(Stage s)
{
    switch (s)
    {
    case Stage::Recommended:
        return "Recomendado";
    case Stage::Selected:
        return "Seleccionado";
    case Stage::Saved:
        return "Guardado";
    case Stage::Pending:
        return "Pendiente de aplicación";
    case Stage::Applied:
        return "Aplicado";
    case Stage::Verified:
        return "Verificado";
    case Stage::Blocked:
        return "Pendiente del usuario";
    case Stage::Failed:
        return "Fallido";
    default:
        return "Sin cambios";
    }
}

// Modos de escalado por su proporción (salida / interna)
struct Preset
{
    const char* name;
    float ratio;
};
static const Preset kPresets[] = {
    { "Nativa", 1.0f },     { "Ultra Calidad", 1.3f }, { "Calidad", 1.5f },
    { "Balanceado", 1.7f }, { "Rendimiento", 2.0f },   { "Ultra Rendimiento", 3.0f },
};
constexpr int kPresetCount = (int) std::size(kPresets);

static int PresetOf(float ratio)
{
    if (ratio <= 0.0f)
        return -1;
    int best = 0;
    for (int i = 1; i < kPresetCount; i++)
        if (std::fabs(kPresets[i].ratio - ratio) < std::fabs(kPresets[best].ratio - ratio))
            best = i;
    return best;
}

// Los modos de escalado de cada modo del perfil: [mejor calidad, más rápido]
static void PresetRange(Mode m, int& best, int& fastest)
{
    best = m == Mode::Calidad ? 1 : m == Mode::Equilibrado ? 2 : 3;
    fastest = best + 1;
}

// Un escalador del catálogo, por su clave estable (sobrevive a un nuevo recorrido del catálogo)
struct Choice
{
    std::string key;  // "entry:FSR4/AMD driver", "backend:xess", "ffx31", "default"
    std::string name; // como lo muestra el catálogo
    bool driverLite = false; // FSR 4 del driver con el modelo INT8Lite de su selector (no es una DLL Lite comunitaria)
    std::string why;
};

enum class FgRoute
{
    None,
    XeFgFixed,    // XeFG ya cargado: multiplicador fijo en caliente
    DlssgGame,    // DLSSG del juego (Enabler...): multiplicador fijo por la anulación de DLSSG
    DlssgOutput,  // salida DLSSG de ILION: multiplicador fijo
    FsrFg,        // FSR FG: 2X, nada que cambiar
    XeFgNew,      // XeFG como nueva salida: requiere reiniciar el juego
};

// Medición de la sesión: solo fotogramas reales
struct Measure
{
    double baseFps = 0.0;  // FPS reales
    double share60 = -1.0; // parte del tiempo observado con 60 FPS reales o más; -1 no calculable
    int frames = 0;
    bool verified = false; // sin FG, con fotogramas suficientes: el criterio del 90 % se ha podido comprobar
    bool limited = false;  // un límite o un MFG dinámico sujeta los FPS reales: no representa la capacidad del juego
    std::string source;
};

enum class Health
{
    Unknown,
    Low,
    Ok,
    Margin, // 60 o más y con margen: se puede subir la calidad
};

struct Plan
{
    // Escalador
    Choice scaler;
    bool changeScaler = false;
    Stage scalerStage = Stage::None;
    std::string scalerNote;
    // Modo de escalado
    int activePreset = -1;
    int preset = -1;
    Stage presetStage = Stage::None;
    std::string presetNote;
    // Generación de fotogramas (fija)
    FgRoute fg = FgRoute::None;
    FGInput fgInput = FGInput::NoFG;
    int count = 0; // fotogramas generados (1 = 2X)
    Stage fgStage = Stage::None;
    std::string fgNote;
    bool restart = false;
    // Evaluación
    Health health = Health::Unknown;
    bool estimated = true;
};

static Mode mode = Mode::Equilibrado;
static std::vector<Step> steps;
static bool running = false;
static bool finished = false;
static int phase = 0;
static double phaseStart = 0.0;
static bool entered = false; // la fase ya hizo su primer paso (una elección, una petición)
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
static std::filesystem::path PreviousPath() { return Folder() / (string_to_wstring(ExeKey()) + L".perfil.anterior.ini"); }
static std::filesystem::path RestorePath() { return Folder() / (string_to_wstring(ExeKey()) + L".restauracion.ini"); }
static std::filesystem::path MatrixPath() { return Folder() / L"compatibilidad.ini"; }

// La matriz de compatibilidad: valores internos de respaldo, escritos al archivo si no existe para poder ampliarlos
// sin recompilar. «origen» distingue lo comprobado en ILION de lo documentado por la wiki de OptiScaler.
static const char* kDefaultMatrix =
    "; ILION — Perfiles inteligentes: compatibilidad por juego (se puede editar; se lee al optimizar)\n"
    "; [ejecutable.exe] en minúsculas\n"
    ";   nombre   = nombre del juego\n"
    ";   origen   = ILION (comprobado en ILION) | OptiScaler wiki (documentado, sin comprobar en ILION)\n"
    ";   entradas = entradas de escalado que ofrece el juego (según la fuente)\n"
    ";   evitar   = rutas que no se eligen: fg, xefg, dlssg_salida, ratio\n"
    ";   nota     = texto que se muestra en el registro\n"
    "\n"
    "[dawnwalker.exe]\n"
    "nombre = The Blood of Dawnwalker\n"
    "origen = ILION\n"
    "entradas = DLSS, FSR, XeSS\n"
    "evitar = dlssg_salida\n"
    "nota = Comprobado en ILION (RX 7900 XTX): FSR 4 (driver, SDK y variantes), XeFG y Enabler. Con ReShade y XeFG/FSR FG ILION envuelve la fábrica DXGI.\n"
    "\n"
    "[kingdomcome.exe]\n"
    "nombre = Kingdom Come: Deliverance II\n"
    "origen = OptiScaler wiki\n"
    "entradas = DLSS, FSR3.1, XeSS\n"
    "evitar = dlssg_salida\n"
    "nota = Wiki de OptiScaler: compatible, ver su entrada para HDR y XeFG. En ILION se observó la caída del rasterizado con salida DLSSG + Dynamic + límite.\n"
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

static std::string NowText()
{
    const std::time_t t = std::time(nullptr);
    std::tm tm {};
    localtime_s(&tm, &t);
    char buf[32] = {};
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
    return buf;
}

// ---- Escaladores del catálogo ----------------------------------------------------------------------------------------
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

static bool DriverLiteOffered()
{
    const auto models = FSR4Models::SelectableModels(FfxApiProxy::Dx12Module_SR());
    return std::find(models.begin(), models.end(), FSR4Model::INT8Lite) != models.end();
}

static bool DriverLiteSelected()
{
    auto config = Config::Instance();
    return config->Fsr4Model.has_value() && config->Fsr4Model.value() == FSR4Model::INT8Lite;
}

// Prioridad aprobada. AMD: FSR 4 del driver (modelo normal), FSR 4 del driver con su modelo INT8Lite, FSR 4 Lite
// comunitaria (DLL propia), Helix FSR 4, y como respaldo XeSS y FSR 3.1. NVIDIA: DLSS, XeSS, FSR 3.1. Intel y otras:
// XeSS, FSR 3.1. lite: la base medida no llega a 60 en el modo más rápido del perfil: las opciones Lite van delante
static std::vector<Choice> Candidates(const UpscalerCatalog& catalog, const GpuInformation& gpu, bool lite)
{
    std::vector<Choice> out;
    const auto add = [&](const UpscalerVariant* v, bool driverLite, const char* why)
    {
        if (v == nullptr || !v->usable)
            return;
        for (const auto& c : out)
            if (c.key == KeyOf(*v) && c.driverLite == driverLite)
                return;
        out.push_back({ KeyOf(*v), v->name + (driverLite ? " · modelo INT8Lite" : ""), driverLite, why });
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
                                                      !IsDriver(v) && !IsCommunityLite(v) && !IsHlx(v); });
    const UpscalerVariant* xess = backend(Upscaler::XeSS);
    const UpscalerVariant* fsr31 = first([](const UpscalerVariant& v) { return v.family == FamFsr31; });
    const bool driverLite = driver != nullptr && DriverLiteOffered();

    if (gpu.vendorId == VendorId::AMD)
    {
        if (lite)
        {
            if (driverLite)
                add(driver, true, "Base por debajo de 60 en el modo más rápido: FSR 4 del driver con su modelo INT8Lite (mejora estimada, sin medir)");
            add(communityLite, false, "Base por debajo de 60 en el modo más rápido: FSR 4 Lite comunitaria (mejora estimada, sin medir)");
        }
        add(driver, false, "Prioridad 1: FSR 4 del driver AMD, modelo normal");
        if (driverLite)
            add(driver, true, "Prioridad 2: FSR 4 del driver con el modelo INT8Lite de su selector");
        add(communityLite, false, "Prioridad 3: FSR 4 Lite comunitaria (DLL independiente)");
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

// ---- Configuración: lo que un perfil guarda y restaura (nunca el MFG dinámico) ---------------------------------------------
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
    ini.SetValue(section, "xefg_multiplicador_fijo", OptStr(config->FGXeFGInterpolationCount).c_str());
    ini.SetValue(section, "dlssg_multiplicador_fijo", OptStr(config->FGDLSSGOverrideInterpolationCount).c_str());
    ini.SetValue(section, "dlssg_salida_multiplicador_fijo", OptStr(config->FGDLSSGInterpolationCount).c_str());
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

// Algún MFG dinámico activo o preparado para el próximo inicio: es configuración externa al perfil
static bool ExternalDynamic(std::string* what = nullptr)
{
    auto config = Config::Instance();
    const auto status = IlionDynamicMfg::Get();
    const char* found = nullptr;
    if (IlionDynamicMfg::DynamicEnabled())
        found = status.provider == IlionDynamicMfg::Provider::XeFG       ? "MFG dinámico R3 de XeFG"
                : status.provider == IlionDynamicMfg::Provider::DlssgGame ? "Dinámico del DLSSG del juego"
                                                                          : "MFG dinámico de DLSSG";
    else if (status.backendDynamic)
        found = "Dinámico automático del backend DLSSG";
    else if (config->FGXeFGDynamicMFG.value_or_default())
        found = "MFG dinámico R3 de XeFG (activado en la configuración)";
    else if (config->FGDLSSGIlionDynamic.value_or_default())
        found = "Dinámico ILION del DLSSG (activado en la configuración)";
    if (what != nullptr && found != nullptr)
        *what = found;
    return found != nullptr;
}

// Escribe en la configuración en memoria lo guardado (sin MFG dinámico) y devuelve el escalador guardado para elegirlo
// con el catálogo. restart: cambian la entrada o la salida de FG cargadas. fgSkipped: había un Dynamic externo y no se
// tocó el multiplicador
static void LoadConfig(const CSimpleIniA& ini, const char* section, std::string& scalerKey, std::string& fsr4Model, bool& restart,
                       bool& fgSkipped)
{
    auto config = Config::Instance();
    scalerKey = ini.GetValue(section, "escalador", "");
    fsr4Model = ini.GetValue(section, "fsr4_modelo", "auto");

    SetFrom(config->UpscaleRatioOverrideEnabled, ini.GetValue(section, "ratio_activo", nullptr), ParseBool);
    SetFrom(config->UpscaleRatioOverrideValue, ini.GetValue(section, "ratio", nullptr), ParseFloat);

    fgSkipped = ExternalDynamic();
    if (!fgSkipped)
    {
        SetFrom(config->FGEnabled, ini.GetValue(section, "fg_activado", nullptr), ParseBool);
        SetFrom(config->FGInput, ini.GetValue(section, "fg_entrada", nullptr), ParseEnum<FGInput>);
        SetFrom(config->FGOutput, ini.GetValue(section, "fg_salida", nullptr), ParseEnum<FGOutput>);
        SetFrom(config->FGNvngxReplacement, ini.GetValue(section, "fg_nvngx", nullptr), ParseEnum<FGNvngxReplacement>);
        SetFrom(config->FGXeFGInterpolationCount, ini.GetValue(section, "xefg_multiplicador_fijo", nullptr), ParseInt);
        SetFrom(config->FGDLSSGOverrideInterpolationCount, ini.GetValue(section, "dlssg_multiplicador_fijo", nullptr), ParseInt);
        SetFrom(config->FGDLSSGInterpolationCount, ini.GetValue(section, "dlssg_salida_multiplicador_fijo", nullptr), ParseInt);
        // Los ajustes de DLSSG del juego se envían desde el hilo del juego (como el resto del menú)
        StreamlineHooks::requestDlssgOptionsUpdate("Perfiles");
    }

    const auto& state = State::Instance();
    restart = config->FGInput.value_or_default() != state.activeFgInput || config->FGOutput.value_or_default() != state.activeFgOutput;
}

// ---- Detección y medición ------------------------------------------------------------------------------------------
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
    static double cached = 0.0;
    static double at = -10.0;
    if (ImGui::GetTime() - at < 2.0)
        return cached;
    at = ImGui::GetTime();

    HWND window = Util::GetProcessWindow();
    HMONITOR monitor = MonitorFromWindow(window != nullptr ? window : GetDesktopWindow(), MONITOR_DEFAULTTONEAREST);
    MONITORINFOEXW info {};
    info.cbSize = sizeof(info);
    DEVMODEW dm {};
    dm.dmSize = sizeof(dm);
    cached = monitor != nullptr && GetMonitorInfoW(monitor, &info) && EnumDisplaySettingsW(info.szDevice, ENUM_CURRENT_SETTINGS, &dm) &&
                     dm.dmDisplayFrequency > 1
                 ? (double) dm.dmDisplayFrequency
                 : 0.0;
    return cached;
}

static bool Generating(const IlionDynamicMfg::Status& status)
{
    return status.gen == IlionDynamicMfg::Gen::Detected || status.gen == IlionDynamicMfg::Gen::Confirmed;
}

// FPS reales de esta sesión: los fotogramas del juego, nunca los generados. El 90 % solo se comprueba sin FG
static Measure MeasureNow()
{
    Measure m;
    const auto status = IlionDynamicMfg::Get();
    const auto& state = State::Instance();
    const float limit = Config::Instance()->FramerateLimit.value_or_default();

    if (Generating(status))
    {
        m.baseFps = status.baseFps;
        // Con un límite (el del perfil, el objetivo como límite o un Dynamic) los FPS reales los fija el límite / el
        // multiplicador: 165 / 3X = 55 no dice cuánto puede dar el juego
        m.limited = status.limitFps > 0.0f || IlionDynamicMfg::DynamicEnabled() || status.backendDynamic;
        m.source = m.limited ? StrFmt("con FG y un límite activo (%.0f FPS finales): los FPS reales los fija el límite", status.limitFps)
                   : status.baseMeasured ? "fotogramas reales contados con FG activo (estimado: el 90 % no se comprueba con FG)"
                                         : "estimado a partir de los FPS finales";
        return m;
    }

    int fast = 0;
    double sum = 0.0;
    for (const double ft : state.frameTimes)
    {
        if (ft <= 0.0 || ft > 1000.0)
            continue;
        m.frames++;
        sum += ft;
        fast += ft <= 1000.0 / 60.0 + 0.05 ? 1 : 0;
    }
    if (m.frames > 0)
        m.baseFps = 1000.0 / (sum / m.frames);
    if (m.frames >= 120)
    {
        m.share60 = (double) fast / m.frames;
        m.limited = limit > 0.0f && limit < 60.0f;
        m.verified = !m.limited;
        m.source = StrFmt("%d fotogramas recientes sin FG", m.frames);
    }
    else
        m.source = "pocos fotogramas observados";
    return m;
}

static Health Evaluate(const Measure& m, bool& estimated)
{
    estimated = !m.verified;
    if (m.limited || m.baseFps <= 0.0)
        return Health::Unknown;
    if (m.verified)
    {
        if (m.share60 < 0.9)
            return Health::Low;
        return m.baseFps >= 78.0 ? Health::Margin : Health::Ok;
    }
    if (m.frames > 0 && m.frames < 120)
        return Health::Unknown; // sin FG pero sin fotogramas suficientes
    // FG sin límite: una media, no el 90 %. Se usa como estimación
    return m.baseFps < 60.0 ? Health::Low : m.baseFps >= 78.0 ? Health::Margin : Health::Ok;
}

// ---- Registro ------------------------------------------------------------------------------------------------------
static void Add(Mark mark, std::string title, std::string detail = {}) { steps.push_back({ mark, std::move(title), std::move(detail) }); }
static Step& Last() { return steps.back(); }

static void Begin()
{
    steps.clear();
    plan = {};
    measure = {};
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
    ini.SetValue("perfil", "modo", ModeName(mode));
    ini.SetValue("perfil", "fecha", NowText().c_str());
    if (plan.preset >= 0)
        ini.SetValue("perfil", "modo_escalado", kPresets[plan.preset].name);
    if (plan.fg != FgRoute::None && plan.count > 0 && plan.fgStage != Stage::Blocked && plan.fgStage != Stage::Recommended)
        ini.SetValue("perfil", "fg_multiplicador", StrFmt("%dX", plan.count + 1).c_str());
    ini.SetValue("perfil", "reinicio_pendiente", plan.restart ? "fg" : "no");
    if (measure.baseFps > 0.0)
        ini.SetValue("perfil", "fps_reales", StrFmt("%.1f%s", measure.baseFps, measure.verified ? "" : " (estimado)").c_str());
    if (measure.share60 >= 0.0)
        ini.SetValue("perfil", "tiempo_sobre_60", StrFmt("%.0f%%", measure.share60 * 100.0).c_str());
    std::error_code ec;
    std::filesystem::create_directories(Folder(), ec);
    ini.SaveFile(ProfilePath().c_str());
}

// ---- Optimizar juego: un paso por fotograma; los de espera hasta que el sistema confirma -------------------------------------
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
    static bool upscaling = false;

    switch (phase)
    {
    case 0: // juego y API
    {
        const std::string game = !state.gameName.empty() ? state.gameName : state.gameExe;
        Add(Mark::Ok, "Juego: " + (game.empty() ? std::string("desconocido") : game),
            StrFmt("%s · API %s%s", ExeKey().c_str(), std::string(magic_enum::enum_name(state.api)).c_str(),
                   state.api != API::DX12 ? " · la generación de fotogramas solo está verificada en DirectX 12" : ""));
        Next();
        break;
    }
    case 1: // GPU y monitor
    {
        const double hz = RefreshHz();
        Add(gpu.vendorId == VendorId::Invalid ? Mark::Bad : Mark::Ok, "GPU: " + gpu.name,
            StrFmt("%s · %.0f GB · FSR 4: %s · monitor %s", ArchName(gpu).c_str(), gpu.dedicatedVramInBytes / (1024.0 * 1024.0 * 1024.0),
                   std::string(magic_enum::enum_name(gpu.fsr4Support)).c_str(), hz > 0.0 ? StrFmt("%.0f Hz", hz).c_str() : "desconocido"));
        Next();
        break;
    }
    case 2: // escalado del juego y tecnologías disponibles
    {
        upscaling = feature != nullptr && feature->IsInited();
        if (upscaling)
        {
            const float ratio = feature->RenderWidth() > 0 ? (float) feature->TargetWidth() / (float) feature->RenderWidth() : 0.0f;
            plan.activePreset = PresetOf(ratio);
            Add(Mark::Ok, "El juego envía su escalado a ILION",
                StrFmt("Entrada %s · %ux%u → %ux%u · modo activo %s", ApiUpscalerInputName(state.currentInputApiName).c_str(),
                       feature->RenderWidth(), feature->RenderHeight(), feature->TargetWidth(), feature->TargetHeight(),
                       plan.activePreset >= 0 ? kPresets[plan.activePreset].name : "?"));
        }
        else
            Add(Mark::Bad, "El juego no envía escalado a ILION",
                "Activa DLSS, FSR o XeSS en las opciones gráficas del juego y vuelve a optimizar.");

        const auto catalog = GatherUpscalers(feature);
        int usable = 0;
        std::string list;
        for (const auto& v : catalog.variants)
            if (v.usable && ++usable <= 6)
                list += (list.empty() ? "" : ", ") + v.name;
        if (usable > 6)
            list += StrFmt(" y %d más", usable - 6);
        Add(usable > 0 ? Mark::Ok : Mark::Bad, StrFmt("Escaladores disponibles: %d", usable), list);

        rule = LoadRule();
        if (rule.found)
            Add(Mark::Info, "Compatibilidad: " + rule.origin + (rule.origin == "ILION" ? " (comprobado)" : " (documentado)"),
                (rule.inputs.empty() ? std::string() : "Entradas: " + rule.inputs + ". ") + rule.note);
        else
            Add(Mark::Info, "Compatibilidad: sin datos del juego", "Se aplican las reglas generales");
        Next();
        break;
    }
    case 3: // medición y evaluación de la base
    {
        measure = MeasureNow();
        plan.health = Evaluate(measure, plan.estimated);
        const char* tag = plan.estimated ? " (estimado)" : " (verificado)";
        switch (plan.health)
        {
        case Health::Unknown:
            Add(Mark::Wait, measure.limited ? "FPS reales: no evaluables ahora" : "FPS reales: sin datos suficientes",
                measure.source + ". La base no se puede juzgar: no se cambia el modo de escalado ni el FG." +
                    (measure.limited ? " Para medirla, desactiva el FG (o el Dynamic y el límite), juega 15 s y vuelve a optimizar."
                                     : " Juega unos segundos más y vuelve a optimizar."));
            break;
        case Health::Low:
            Add(Mark::Bad, StrFmt("FPS reales: %.0f · objetivo de 60 NO alcanzado%s", measure.baseFps, tag),
                measure.share60 >= 0.0 ? StrFmt("%.0f%% del tiempo con 60 o más (se exige el 90%%) · %s", measure.share60 * 100.0, measure.source.c_str())
                                       : measure.source);
            break;
        default:
            Add(Mark::Ok, StrFmt("FPS reales: %.0f · objetivo de 60 cumplido%s", measure.baseFps, tag),
                measure.share60 >= 0.0 ? StrFmt("%.0f%% del tiempo con 60 o más · %s", measure.share60 * 100.0, measure.source.c_str())
                                       : measure.source);
            break;
        }
        Next();
        break;
    }
    case 4: // modo de escalado: calidad primero, la base por encima de todo
    {
        if (!upscaling || plan.activePreset < 0 || (rule.found && rule.Avoids("ratio")))
        {
            Add(Mark::Info, "Modo de escalado: no se cambia");
            Next();
            break;
        }
        int best = 0, fastest = 0;
        PresetRange(mode, best, fastest);
        const int cur = plan.activePreset;
        int target = cur;
        std::string why;
        switch (plan.health)
        {
        case Health::Unknown:
            // Sin evidencia no se baja la calidad; se sube al rango del modo solo si está por debajo de él
            target = cur > fastest ? fastest : cur;
            why = cur > fastest ? "Sin medición válida: se sube al modo de escalado más rápido del perfil (estimado)"
                                : "Sin medición válida: se mantiene el modo de escalado del juego";
            break;
        case Health::Margin:
            target = cur > best ? cur - 1 : cur;
            why = cur > best ? "Base con margen: un paso hacia más calidad" : "Base con margen: ya está en la mejor calidad del modo";
            break;
        case Health::Ok:
            target = cur > fastest ? fastest : cur;
            why = cur > fastest ? "Base suficiente: se sube la calidad al rango del modo" : "Base suficiente: se mantiene la calidad";
            break;
        case Health::Low:
            target = cur < fastest ? cur + 1 : cur;
            why = cur < fastest ? "Base por debajo de 60: un paso hacia más rendimiento dentro del modo"
                                : "Base por debajo de 60 en el modo más rápido del perfil";
            break;
        }
        plan.preset = target;
        if (target != cur)
        {
            plan.presetStage = Stage::Selected;
            Add(Mark::Wait, StrFmt("Modo de escalado: %s → %s", kPresets[cur].name, kPresets[target].name),
                why + (plan.estimated ? " · estimado" : ""));
        }
        else
            Add(Mark::Ok, StrFmt("Modo de escalado: %s (se mantiene)", kPresets[cur].name), why);
        Next();
        break;
    }
    case 5: // escalador y variante
    {
        if (!upscaling)
        {
            Add(Mark::Info, "Escalador: no se cambia");
            Next();
            break;
        }
        int best = 0, fastest = 0;
        PresetRange(mode, best, fastest);
        // Lite solo cuando la base no llega a 60 y el modo de escalado ya es el más rápido del perfil
        const bool liteNeeded = plan.health == Health::Low && plan.preset >= fastest;
        const auto catalog = GatherUpscalers(feature);
        const auto candidates = Candidates(catalog, gpu, liteNeeded);
        if (candidates.empty())
        {
            Add(Mark::Bad, "Ningún escalador compatible disponible", "Se mantiene el actual");
            Next();
            break;
        }
        plan.scaler = candidates.front();
        const UpscalerVariant* current = Current(catalog);
        const bool same = current != nullptr && KeyOf(*current) == plan.scaler.key && DriverLiteSelected() == plan.scaler.driverLite;

        if (plan.scaler.driverLite || (liteNeeded && IsCommunityLite(*Find(catalog, plan.scaler.key))))
        {
            // Una alternativa de rendimiento sin medir: en Calidad solo se recomienda
            if (mode == Mode::Calidad)
            {
                plan.scalerStage = Stage::Recommended;
                Add(Mark::Wait, "Recomendado (no aplicado): " + plan.scaler.name,
                    plan.scaler.why + ". En modo Calidad no se cambia sin tu decisión.");
                Next();
                break;
            }
        }
        if (same)
        {
            plan.scalerStage = Stage::Verified;
            Add(Mark::Ok, "Escalador: " + plan.scaler.name + " (ya activo)", plan.scaler.why);
        }
        else
        {
            plan.changeScaler = true;
            plan.scalerStage = Stage::Selected;
            Add(Mark::Wait, "Escalador elegido: " + plan.scaler.name,
                plan.scaler.why + (current != nullptr ? " · antes: " + current->name : std::string()));
        }
        Next();
        break;
    }
    case 6: // generación de fotogramas fija
    {
        const auto status = IlionDynamicMfg::Get();
        const double hz = RefreshHz();
        const double base = measure.baseFps;
        std::string dynamicWhat;
        const bool dynamic = ExternalDynamic(&dynamicWhat);

        int count = 1; // 2X por defecto
        if (mode == Mode::Rendimiento && hz > 0.0 && base * 3.0 <= hz && base * 2.0 <= hz * 0.6)
            count = 2; // 3X solo si 2X se queda lejos del monitor y 3X no lo supera
        const bool useful = hz <= 0.0 || base * 2.0 <= hz * 1.1;

        if (rule.found && rule.Avoids("fg"))
            Add(Mark::Info, "Frame Generation: no se cambia", "La matriz de compatibilidad lo excluye para este juego");
        else if (state.api != API::DX12)
            Add(Mark::Info, "Frame Generation: no se cambia", "Solo DirectX 12 está verificado");
        else if (plan.health == Health::Unknown)
            Add(Mark::Wait, "Frame Generation: no se evalúa", "Sin FPS reales válidos no se decide el FG");
        else if (plan.health == Health::Low)
        {
            plan.fgStage = Generating(status) ? Stage::Recommended : Stage::None;
            Add(Mark::Bad, "Frame Generation: no recomendado con esta base",
                Generating(status) ? "Base por debajo de 60: se recomienda desactivar el FG (no se cambia sin tu decisión)"
                                   : "Primero hay que asegurar 60 FPS reales");
        }
        else if (!useful)
            Add(Mark::Info, "Frame Generation: no necesario", StrFmt("%.0f FPS reales ya cubren el monitor de %.0f Hz", base, hz));
        else if (dynamic)
        {
            plan.fgStage = Stage::Blocked;
            Add(Mark::Wait, "Frame Generation: pendiente de tu decisión",
                dynamicWhat + " está activo (configuración externa). Perfiles no lo modifica: desactívalo en FRAME GEN para usar " +
                    StrFmt("%dX fijo.", count + 1));
        }
        else
        {
            const bool xefgDll = std::filesystem::exists(Util::DllPath().parent_path() / L"libxess_fg.dll");
            switch (status.provider)
            {
            case IlionDynamicMfg::Provider::XeFG:
                plan.fg = FgRoute::XeFgFixed;
                break;
            case IlionDynamicMfg::Provider::DlssgGame:
                if (StreamlineHooks::isDlssgHooked() && state.dlssgMfgMax.value_or(1) >= 1)
                    plan.fg = FgRoute::DlssgGame;
                break;
            case IlionDynamicMfg::Provider::DlssgOutput:
                if (!(rule.found && rule.Avoids("dlssg_salida")))
                    plan.fg = FgRoute::DlssgOutput;
                break;
            case IlionDynamicMfg::Provider::FsrFg:
                plan.fg = FgRoute::FsrFg;
                count = 1;
                break;
            default:
                if (state.activeFgOutput == FGOutput::NoFG && xefgDll && !(rule.found && rule.Avoids("xefg")) &&
                    (state.streamlineVersion.major >= 2 || (feature != nullptr && feature->IsInited())))
                {
                    plan.fg = FgRoute::XeFgNew;
                    plan.fgInput = state.streamlineVersion.major >= 2 ? FGInput::DLSSG : FGInput::Upscaler;
                }
                break;
            }

            if (plan.fg == FgRoute::XeFgFixed || plan.fg == FgRoute::DlssgOutput)
                count = std::clamp(count, 1, std::max(IlionDynamicMfg::MaxCountLimit(), 1));
            else if (plan.fg == FgRoute::DlssgGame)
                count = std::clamp(count, 1, std::max(state.dlssgMfgMax.value_or(1), 1));
            plan.count = count;

            if (plan.fg == FgRoute::None)
                Add(Mark::Info, "Frame Generation: no se cambia", "No hay una ruta fija verificada para esta combinación");
            else
            {
                plan.fgStage = Stage::Selected;
                const char* route = plan.fg == FgRoute::XeFgFixed     ? "XeFG (ya cargado)"
                                    : plan.fg == FgRoute::DlssgGame   ? "DLSSG del juego"
                                    : plan.fg == FgRoute::DlssgOutput ? "salida DLSSG de ILION"
                                    : plan.fg == FgRoute::FsrFg       ? "FSR FG (ya cargado)"
                                    : plan.fgInput == FGInput::DLSSG  ? "XeFG nuevo (entrada DLSSG del juego)"
                                                                      : "XeFG nuevo (entrada OptiFG)";
                Add(Mark::Wait, StrFmt("Frame Generation: %s · %dX fijo", route, count + 1),
                    StrFmt("Base %.0f FPS reales × %d = %.0f FPS finales%s · sin MFG dinámico", base, count + 1, base * (count + 1),
                           hz > 0.0 ? StrFmt(" (monitor %.0f Hz, solo referencia)", hz).c_str() : ""));
            }
        }
        Next();
        break;
    }
    case 7: // punto de restauración
    {
        SaveRestorePoint(feature);
        Add(Mark::Ok, "Punto de restauración guardado", "ILION\\Perfiles\\" + ExeKey() + ".restauracion.ini");
        Next();
        break;
    }
    case 8: // aplicar el escalador: una vez, y se confirma que corre. No se prueba otro
    {
        if (!plan.changeScaler)
        {
            Next();
            break;
        }
        const auto catalog = GatherUpscalers(feature);
        const UpscalerVariant* v = Find(catalog, plan.scaler.key);

        if (!entered)
        {
            entered = true;
            if (v == nullptr || !v->usable || FfxApiProxy::SrSwitchLocked())
            {
                plan.scalerStage = Stage::Failed;
                Add(Mark::Bad, "No se pudo elegir " + plan.scaler.name, v == nullptr ? "Ya no está en el catálogo" : v->reason);
                Next();
                break;
            }
            if (!v->status.active)
                ChooseUpscalerVariant(*v);
            if (!plan.scaler.driverLite && IsDriver(*v) && config->Fsr4Model.has_value())
            {
                config->Fsr4Model.reset(); // modelo normal: el que elige FFX
                State::Instance().newBackend = currentBackend != Upscaler::Reset ? currentBackend : Upscaler::FFX;
                MARK_ALL_BACKENDS_CHANGED();
            }
            plan.scalerStage = Stage::Applied;
            Add(Mark::Wait, "Aplicando " + plan.scaler.name, "Esperando a que el escalador se ejecute");
            break;
        }

        if (v != nullptr && v->status.active && !litePending)
        {
            if (plan.scaler.driverLite && !DriverLiteSelected())
            {
                const std::optional<FSR4Model> previous =
                    config->Fsr4Model.has_value() ? std::optional<FSR4Model>(config->Fsr4Model.value()) : std::nullopt;
                config->Fsr4Model = FSR4Model::INT8Lite;
                FfxApiProxy::ArmSrModelRevert(previous);
                State::Instance().newBackend = Upscaler::FFX;
                MARK_ALL_BACKENDS_CHANGED();
                litePending = true;
                phaseStart = ImGui::GetTime();
                Last().detail = "Ejecutándose; cambiando al modelo INT8Lite del driver";
                break;
            }
            plan.scalerStage = Stage::Verified;
            Last() = { Mark::Ok, "Escalador verificado: " + plan.scaler.name, "Es el que ejecuta el último escalado" };
            Next();
            break;
        }

        if (litePending && elapsed > 1.5 && v != nullptr && v->status.active && DriverLiteSelected())
        {
            const auto active = FSR4Models::ActiveModel(FfxApiProxy::Dx12Module_SR());
            plan.scalerStage = active.has_value() && *active == FSR4Model::INT8Lite ? Stage::Verified : Stage::Applied;
            Last() = { plan.scalerStage == Stage::Verified ? Mark::Ok : Mark::Wait, "Escalador aplicado: " + plan.scaler.name,
                       active.has_value() ? StrFmt("Modelo que ejecuta FFX: %s", FSR4Models::Name(*active)) : "Modelo INT8Lite pedido" };
            litePending = false;
            Next();
            break;
        }

        if (elapsed > 10.0)
        {
            // Sin sustituirlo por otro: se informa y la restauración queda a un botón
            plan.scalerStage = Stage::Failed;
            Last() = { Mark::Bad, "No llegó a ejecutarse: " + plan.scaler.name,
                       "No se prueba otro escalador por tu cuenta. Usa RESTAURAR CONFIGURACIÓN ANTERIOR si el juego no escala bien." };
            litePending = false;
            Next();
        }
        break;
    }
    case 9: // modo de escalado: la anulación de proporción, que el juego lee al pedir su resolución
    {
        if (plan.presetStage == Stage::Selected && plan.preset >= 0)
        {
            config->UpscaleRatioOverrideEnabled = true;
            config->UpscaleRatioOverrideValue = kPresets[plan.preset].ratio;
            plan.presetStage = Stage::Pending;
            Add(Mark::Wait, StrFmt("Modo de escalado %s: pendiente de aplicación", kPresets[plan.preset].name),
                StrFmt("Proporción %.2f guardada. Se aplica cuando el juego vuelva a pedir su resolución: cambia la calidad en el "
                       "menú del juego o reinícialo.", kPresets[plan.preset].ratio));
        }
        Next();
        break;
    }
    case 10: // generación de fotogramas fija
    {
        if (plan.fgStage != Stage::Selected)
        {
            Next();
            break;
        }
        if (!entered)
        {
            entered = true;
            switch (plan.fg)
            {
            case FgRoute::XeFgFixed:
            case FgRoute::DlssgOutput:
            case FgRoute::DlssgGame:
                if (!config->FGEnabled.has_value() || !config->FGEnabled.value())
                    config->FGEnabled = true;
                IlionDynamicMfg::SetMaxCount(plan.count);
                plan.fgStage = Stage::Applied;
                Add(Mark::Wait, StrFmt("Aplicando %dX fijo", plan.count + 1), "Esperando a que el proveedor genere con ese multiplicador");
                break;
            case FgRoute::FsrFg:
                plan.fgStage = Stage::Applied;
                Add(Mark::Wait, "FSR FG 2X: sin cambios", "Comprobando que genera");
                break;
            case FgRoute::XeFgNew:
                config->FGInput = plan.fgInput;
                config->FGOutput = FGOutput::XeFG;
                config->FGEnabled = true;
                config->FGXeFGInterpolationCount = plan.count;
                plan.fgStage = Stage::Pending;
                plan.restart = true;
                Add(Mark::Restart, "REINICIO DEL JUEGO NECESARIO",
                    StrFmt("XeFG %dX fijo seleccionado para el próximo inicio. Cierra y vuelve a iniciar el juego para que los cambios "
                           "de Frame Generation surtan efecto.", plan.count + 1));
                Next();
                break;
            default:
                Next();
                break;
            }
            break;
        }

        const auto status = IlionDynamicMfg::Get();
        if (Generating(status) && status.multiplier == plan.count + 1 &&
            (status.appliedSource == IlionDynamicMfg::AppliedSource::Own || status.appliedSource == IlionDynamicMfg::AppliedSource::Measured))
        {
            plan.fgStage = Stage::Verified;
            Last() = { Mark::Ok, StrFmt("Frame Generation verificado: %s %dX fijo", status.name, plan.count + 1),
                       status.appliedSource == IlionDynamicMfg::AppliedSource::Measured ? "Multiplicador medido en los presents" : "Multiplicador aplicado por ILION" };
            Next();
        }
        else if (elapsed > 8.0)
        {
            Last() = { Mark::Wait, StrFmt("Frame Generation %dX aplicado, sin verificar", plan.count + 1),
                       StrFmt("El proveedor informa %s. Revisa la pestaña FRAME GEN.",
                              status.multiplier > 0 ? StrFmt("%dX", status.multiplier).c_str() : "que no genera ahora") };
            Next();
        }
        break;
    }
    case 11: // guardar el perfil (el anterior se conserva aparte) y cerrar
    {
        config->SaveIni();
        std::error_code ec;
        if (std::filesystem::exists(ProfilePath(), ec))
        {
            std::filesystem::copy_file(ProfilePath(), PreviousPath(), std::filesystem::copy_options::overwrite_existing, ec);
            Add(ec ? Mark::Bad : Mark::Info, ec ? "No se pudo conservar el perfil anterior" : "Perfil anterior conservado",
                "ILION\\Perfiles\\" + ExeKey() + ".perfil.anterior.ini");
        }
        SaveProfile(feature);

        const auto pending = [](Stage s) { return s == Stage::Pending || s == Stage::Applied || s == Stage::Blocked || s == Stage::Recommended; };
        const bool failed = plan.scalerStage == Stage::Failed;
        const bool waiting = pending(plan.scalerStage) || pending(plan.presetStage) || pending(plan.fgStage) || plan.health == Health::Unknown;
        if (plan.restart)
            Add(Mark::Restart, "Perfil guardado: pendiente de reinicio del juego", "ILION\\Perfiles\\" + ExeKey() + ".perfil.ini");
        else if (failed)
            Add(Mark::Bad, "Perfil guardado con errores", "ILION\\Perfiles\\" + ExeKey() + ".perfil.ini");
        else if (waiting)
            Add(Mark::Wait, "Perfil guardado: hay cambios pendientes", "ILION\\Perfiles\\" + ExeKey() + ".perfil.ini");
        else
            Add(Mark::Ok, "Optimización completada y verificada", "ILION\\Perfiles\\" + ExeKey() + ".perfil.ini");
        running = false;
        finished = true;
        break;
    }
    default:
        running = false;
        break;
    }
}

// Cargar un perfil o el punto de restauración, por petición del usuario
template <typename Ctx> static void ApplyFile(Ctx& ctx, const std::filesystem::path& path, const char* section, const char* what)
{
    Begin();
    running = false;
    finished = true;

    CSimpleIniA ini(true);
    if (ini.LoadFile(path.c_str()) < 0)
    {
        Add(Mark::Bad, std::string("No se pudo leer: ") + what, wstring_to_string(path.wstring()));
        return;
    }

    std::string scalerKey, model;
    bool restart = false, fgSkipped = false;
    LoadConfig(ini, section, scalerKey, model, restart, fgSkipped);
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
        Add(Mark::Ok, std::string(what) + ": escalador seleccionado", v->name);
    }
    else if (!scalerKey.empty())
        Add(Mark::Bad, std::string(what) + ": el escalador guardado no está disponible", scalerKey);

    if (const char* r = ini.GetValue(section, "ratio_activo", nullptr); r != nullptr && strcmp(r, "true") == 0)
        Add(Mark::Wait, std::string(what) + ": modo de escalado pendiente de aplicación",
            "Se aplica cuando el juego vuelva a pedir su resolución (cambia la calidad en el juego o reinícialo)");

    if (fgSkipped)
        Add(Mark::Wait, std::string(what) + ": Frame Generation no cargado", "Hay un MFG dinámico activo (configuración externa); Perfiles no lo cambia");

    config->SaveIni();
    if (restart)
        Add(Mark::Restart, "REINICIO DEL JUEGO NECESARIO", "Cierra y vuelve a iniciar el juego para que los cambios de Frame Generation surtan efecto.");
    else
        Add(Mark::Ok, std::string(what) + ": aplicado", ini.GetValue(section, "fecha", ""));
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
    case Mark::Restart:
        return IlionTheme::ShellPal::Red;
    default:
        return IlionTheme::ShellPal::Text2;
    }
}

static ImFont* UiFont() { return IlionTheme::modernFont != nullptr ? IlionTheme::modernFont : ImGui::GetFont(); }
static ImFont* FigureFont() { return IlionTheme::crtTitleFont != nullptr ? IlionTheme::crtTitleFont : UiFont(); }

static void Coloured(const ImVec4& colour, const char* text, bool wrap = false)
{
    ImGui::PushStyleColor(ImGuiCol_Text, toneMapColor(colour));
    if (wrap)
        ImGui::TextWrapped("%s", text);
    else
        ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
}

static void Big(ImFont* font, float scale, const ImVec4& colour, const char* text, bool wrap = false)
{
    ImGui::PushFont(font, std::floor(ImGui::GetFontSize() * scale));
    Coloured(colour, text, wrap);
    ImGui::PopFont();
}

// Piloto del tamaño de la línea, alineado con el texto que le sigue
static void Pilot(Mark mark, float size)
{
    const ImVec2 p = ImGui::GetCursorScreenPos();
    IlionTheme::ShellLed(ImGui::GetWindowDrawList(), ImVec2(p.x + size * 0.5f, p.y + size * 0.5f), size * 0.3f, toneMapColor(MarkColour(mark)),
                         mark != Mark::Info);
    ImGui::Dummy(ImVec2(size, size));
    ImGui::SameLine(0.0f, size * 0.3f);
}

// Cabecera de un bloque: piloto, título en mayúsculas y el estado a la derecha
static void BlockHead(Mark mark, const char* title, const char* state)
{
    const float h = ImGui::GetTextLineHeight();
    Pilot(mark, h);
    Coloured(IlionTheme::ShellPal::Text2, title);
    if (state != nullptr && state[0] != 0)
    {
        const float w = ImGui::CalcTextSize(state).x;
        ImGui::SameLine();
        if (const float room = ImGui::GetContentRegionAvail().x; room > w)
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + room - w);
        Coloured(MarkColour(mark), state);
    }
    ImGui::Spacing();
}

static void Row(Mark mark, const std::string& title, const std::string& detail)
{
    const float h = ImGui::GetTextLineHeight();
    Pilot(mark, h);
    ImGui::BeginGroup();
    Coloured(mark == Mark::Info ? IlionTheme::ShellPal::Text : MarkColour(mark), title.c_str(), true);
    if (!detail.empty())
        Coloured(IlionTheme::ShellPal::Dim, detail.c_str(), true);
    ImGui::EndGroup();
}

// Lo que el perfil guardado dejó pendiente para este inicio
struct Saved
{
    bool exists = false;
    std::string modeName, date, preset, fgOutput, fgCount;
    bool restart = false;
};

static Saved ReadSaved()
{
    static Saved cached;
    static double at = -10.0;
    if (ImGui::GetTime() - at < 1.0)
        return cached;
    at = ImGui::GetTime();
    cached = {};
    std::error_code ec;
    if (!std::filesystem::exists(ProfilePath(), ec))
        return cached;
    CSimpleIniA ini(true);
    if (ini.LoadFile(ProfilePath().c_str()) < 0)
        return cached;
    cached.exists = true;
    cached.modeName = ini.GetValue("perfil", "modo", "");
    cached.date = ini.GetValue("perfil", "fecha", "");
    cached.preset = ini.GetValue("perfil", "modo_escalado", "");
    cached.fgOutput = ini.GetValue("perfil", "fg_salida", "");
    cached.fgCount = ini.GetValue("perfil", "fg_multiplicador", "");
    cached.restart = strcmp(ini.GetValue("perfil", "reinicio_pendiente", "no"), "fg") == 0;
    return cached;
}

// Configuración activa: lo que el juego ejecuta ahora, leído del sistema, y lo pendiente aparte
template <typename Ctx> static void ActiveMonitor(Ctx& ctx, float logHeight)
{
    auto config = Config::Instance();
    const auto& state = State::Instance();
    const auto status = IlionDynamicMfg::Get();
    const Saved saved = ReadSaved();
    IFeature* feature = ctx.currentFeature;
    const bool upscaling = feature != nullptr && feature->IsInited();
    const auto catalog = GatherUpscalers(feature);
    const UpscalerVariant* current = Current(catalog);
    const bool generating = Generating(status);
    std::string dynamicWhat;
    const bool dynamic = ExternalDynamic(&dynamicWhat) && status.provider != IlionDynamicMfg::Provider::None;
    const float font = ImGui::GetFontSize();

    // El reinicio pendiente: el de esta optimización o el del perfil, mientras la salida elegida no esté cargada
    const bool fgWanted = !saved.fgOutput.empty() && saved.fgOutput != "auto" && saved.fgOutput != "NoFG";
    const bool fgLoaded = fgWanted && std::string(magic_enum::enum_name(state.activeFgOutput)) == saved.fgOutput;
    const bool restartNeeded = (plan.restart && !(plan.fg == FgRoute::XeFgNew && state.activeFgOutput == FGOutput::XeFG)) ||
                               (saved.restart && fgWanted && !fgLoaded);

    const ImVec4 panel = IlionTheme::shell ? IlionTheme::ShellPal::Panel : ImGui::GetStyleColorVec4(ImGuiCol_FrameBg);

    if (restartNeeded)
    {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, toneMapColor(ImVec4(0.30f, 0.04f, 0.03f, 1.0f)));
        ImGui::PushStyleColor(ImGuiCol_Border, toneMapColor(IlionTheme::ShellPal::Red));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 2.0f);
        if (ImGui::BeginChild("##ProfilesRestart", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY))
        {
            Pilot(Mark::Restart, std::floor(font * 1.4f));
            Big(UiFont(), 1.35f, IlionTheme::ShellPal::Red, "REINICIO DEL JUEGO NECESARIO");
            Coloured(IlionTheme::ShellPal::Text, "Cierra y vuelve a iniciar el juego para que los cambios de Frame Generation surtan efecto.", true);
        }
        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(2);
        ImGui::Spacing();
    }

    // ESCALADO y FRAME GENERATION: dos bloques de la misma altura, uno al lado del otro si caben
    const bool side = ImGui::GetContentRegionAvail().x > font * 30.0f;
    const ImGuiTableFlags flags = ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_PadOuterX |
                                  ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings;
    ImGui::PushStyleColor(ImGuiCol_TableBorderStrong, toneMapColor(IlionTheme::ShellPal::Line));
    ImGui::PushStyleColor(ImGuiCol_TableBorderLight, toneMapColor(IlionTheme::ShellPal::Line));
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(font * 0.7f, font * 0.5f));
    if (ImGui::BeginTable("##ProfilesActive", side ? 2 : 1, flags))
    {
        // ---- Escalado ----
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, ImGui::GetColorU32(toneMapColor(panel)));
        {
            const float ratio = upscaling && feature->RenderWidth() > 0 ? (float) feature->TargetWidth() / (float) feature->RenderWidth() : 0.0f;
            const int activePreset = PresetOf(ratio);
            const Mark mark = upscaling ? Mark::Ok : current != nullptr ? Mark::Wait : Mark::Bad;
            BlockHead(mark, "ESCALADO", upscaling ? "ACTIVO" : current != nullptr ? "SIN ESCALAR" : "NINGUNO");
            std::string name = current != nullptr ? current->name : std::string("Sin escalador");
            Big(UiFont(), 1.7f, IlionTheme::ShellPal::Text, name.c_str(), true);
            if (current != nullptr && IsDriver(*current))
                Coloured(IlionTheme::ShellPal::Text2, DriverLiteSelected() ? "Driver AMD · modelo INT8Lite" : "Driver AMD · modelo normal");
            ImGui::Spacing();
            Big(UiFont(), 1.35f, IlionTheme::ShellPal::Orange, activePreset >= 0 ? kPresets[activePreset].name : "—");
            if (upscaling)
            {
                ImGui::PushStyleColor(ImGuiCol_Text, toneMapColor(IlionTheme::ShellPal::Text));
                ImGui::Text("%u×%u", feature->RenderWidth(), feature->RenderHeight());
                ImGui::PopStyleColor();
                ImGui::SameLine();
                Coloured(IlionTheme::ShellPal::Dim, "→");
                ImGui::SameLine();
                ImGui::PushStyleColor(ImGuiCol_Text, toneMapColor(IlionTheme::InfoCyan));
                ImGui::Text("%u×%u", feature->TargetWidth(), feature->TargetHeight());
                ImGui::PopStyleColor();
                ImGui::SameLine();
                Coloured(IlionTheme::ShellPal::Dim, StrFmt("· proporción %.2f", ratio).c_str());
            }
            else
                Coloured(IlionTheme::ShellPal::Dim, "El juego no envía escalado ahora", true);

            // Lo elegido para cuando el juego pida de nuevo su resolución
            if (config->UpscaleRatioOverrideEnabled.value_or_default())
            {
                const int wanted = PresetOf(config->UpscaleRatioOverrideValue.value_or_default());
                const bool applied = activePreset >= 0 && wanted == activePreset;
                ImGui::Spacing();
                Row(applied ? Mark::Ok : Mark::Wait,
                    StrFmt("%s: %s", applied ? "Verificado" : "Pendiente de aplicación", wanted >= 0 ? kPresets[wanted].name : "?"),
                    applied ? std::string() : "Cambia la calidad en el menú del juego o reinícialo");
            }
            if (plan.scalerStage == Stage::Failed || plan.scalerStage == Stage::Recommended)
                Row(plan.scalerStage == Stage::Failed ? Mark::Bad : Mark::Wait, std::string(StageName(plan.scalerStage)) + ": " + plan.scaler.name,
                    plan.scalerStage == Stage::Failed ? "Usa RESTAURAR CONFIGURACIÓN ANTERIOR" : std::string());
        }

        // ---- Frame Generation ----
        if (side)
            ImGui::TableSetColumnIndex(1);
        else
        {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
        }
        ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, ImGui::GetColorU32(toneMapColor(panel)));
        {
            const bool none = status.provider == IlionDynamicMfg::Provider::None;
            const Mark mark = generating ? Mark::Ok : restartNeeded ? Mark::Restart : none ? Mark::Info : Mark::Wait;
            BlockHead(mark, "FRAME GENERATION", generating ? "ACTIVO" : restartNeeded ? "REINICIO" : none ? "DESACTIVADO" : "EN PAUSA");
            Big(UiFont(), 1.7f, IlionTheme::ShellPal::Text, none ? "Sin FG" : status.name, true);
            if (generating && status.multiplier > 0)
            {
                Big(FigureFont(), 1.6f, IlionTheme::InfoBlue, StrFmt("%dX", status.multiplier).c_str());
                ImGui::SameLine();
                Coloured(IlionTheme::ShellPal::Text2,
                         status.appliedSource == IlionDynamicMfg::AppliedSource::Measured ? "medido"
                         : status.appliedSource == IlionDynamicMfg::AppliedSource::Own    ? "aplicado"
                                                                                          : "pedido");
            }
            ImGui::Spacing();
            if (dynamic)
                Row(Mark::Wait, "Dynamic: configuración externa", dynamicWhat + ". No es una decisión del perfil; el perfil no lo modifica.");
            else if (generating)
                Row(Mark::Ok, "Multiplicador fijo", std::string());

            if (fgWanted && !fgLoaded && saved.restart)
                Row(Mark::Restart, "Seleccionado para el próximo inicio: " + saved.fgOutput + (saved.fgCount.empty() ? "" : " " + saved.fgCount),
                    "Aún no cargado en esta sesión");
            else if (fgWanted && fgLoaded && saved.restart)
                Row(generating ? Mark::Ok : Mark::Wait, "Seleccionado en el perfil: " + saved.fgOutput + " " + saved.fgCount,
                    generating ? "Cargado y generando: verificado" : "Cargado, todavía sin generar");
            if (plan.fgStage == Stage::Blocked)
                Row(Mark::Wait, "FG fijo pendiente de tu decisión", "Desactiva el Dynamic en FRAME GEN y vuelve a optimizar");
            else if (plan.fgStage == Stage::Recommended)
                Row(Mark::Bad, "Recomendado: desactivar el FG", "La base no llega a 60 FPS reales");
        }
        ImGui::EndTable();
    }
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
    ImGui::Spacing();

    // ---- Rendimiento ----
    const Measure live = MeasureNow();
    const double hz = RefreshHz();
    const double finalFps = generating && status.finalFps > 0.0f ? status.finalFps : live.baseFps;
    const double frameMs = finalFps > 0.0 ? 1000.0 / finalFps : 0.0;
    ImGui::PushStyleColor(ImGuiCol_TableBorderStrong, toneMapColor(IlionTheme::ShellPal::Line));
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(font * 0.7f, font * 0.45f));
    if (ImGui::BeginTable("##ProfilesPerf", 4, ImGuiTableFlags_BordersOuter | ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings))
    {
        const auto figure = [&](int column, const char* label, const std::string& value, const ImVec4& colour, const char* unit)
        {
            ImGui::TableSetColumnIndex(column);
            ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, ImGui::GetColorU32(toneMapColor(panel)));
            Coloured(IlionTheme::ShellPal::Text2, label);
            Big(FigureFont(), 2.0f, colour, value.c_str());
            Coloured(IlionTheme::ShellPal::Dim, unit);
        };
        ImGui::TableNextRow();
        figure(0, "FPS REALES", live.baseFps > 0.0 ? StrFmt("%.0f", live.baseFps) : "—", live.baseFps > 0.0 ? IlionTheme::FpsBand(live.baseFps) : IlionTheme::ShellPal::Dim,
               generating ? (live.limited ? "sujetos por un límite" : "sin generados") : "rasterizado");
        figure(1, "FPS FINALES", finalFps > 0.0 ? StrFmt("%.0f", finalFps) : "—", IlionTheme::ShellPal::Text, generating ? "con FG" : "sin FG");
        figure(2, "MONITOR", hz > 0.0 ? StrFmt("%.0f", hz) : "—", IlionTheme::ShellPal::Text2, "Hz · referencia");
        figure(3, "FRAME TIME", frameMs > 0.0 ? StrFmt("%.1f", frameMs) : "—", IlionTheme::ShellPal::Text, "ms presentados");
        ImGui::EndTable();
    }
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();

    // Objetivo de 60 reales y coste del FG, solo con datos fiables
    {
        bool estimated = true;
        const Health h = Evaluate(live, estimated);
        if (h == Health::Unknown)
            Row(Mark::Wait, "Objetivo 60 FPS reales: sin datos fiables", live.source);
        else if (live.share60 >= 0.0)
            Row(h == Health::Low ? Mark::Bad : Mark::Ok,
                StrFmt("Objetivo 60 FPS reales: %s · %.0f%% del tiempo con 60 o más", h == Health::Low ? "NO cumplido" : "cumplido", live.share60 * 100.0),
                "Verificado sin FG (se exige el 90%)");
        else
            Row(h == Health::Low ? Mark::Bad : Mark::Wait, StrFmt("Objetivo 60 FPS reales: %s (estimado)", h == Health::Low ? "NO cumplido" : "probable"),
                "Con FG activo el 90% del tiempo no se puede comprobar");

        const double reference = MfgCostReference();
        if (generating && reference > 0.0 && mfgCost.current > 0.0)
            Row(Mark::Info, StrFmt("Coste del FG: %.0f%% de los FPS reales", std::max(0.0, (1.0 - mfgCost.current / reference) * 100.0)),
                StrFmt("Medido: %.0f sin FG → %.0f con FG", reference, mfgCost.current));
    }

    // ---- Registro de operaciones (secundario, con su propio desplazamiento) ----
    ImGui::Spacing();
    Coloured(IlionTheme::ShellPal::Dim, "REGISTRO DE OPERACIONES");
    ImGui::PushStyleColor(ImGuiCol_ChildBg, toneMapColor(panel));
    const float height = (std::max)(logHeight > 0.0f ? ImGui::GetContentRegionAvail().y : 0.0f, font * 7.0f);
    if (ImGui::BeginChild("##ProfilesLog", ImVec2(0.0f, logHeight > 0.0f ? height : font * 9.0f), ImGuiChildFlags_Borders))
    {
        ImGui::PushFont(UiFont(), std::floor(font * 0.88f));
        if (steps.empty())
            Coloured(IlionTheme::ShellPal::Dim,
                     saved.exists ? StrFmt("Perfil guardado: modo %s · %s", saved.modeName.c_str(), saved.date.c_str()).c_str()
                                  : "Sin operaciones. Pulsa OPTIMIZAR JUEGO.",
                     true);
        for (const auto& s : steps)
            Row(s.mark, s.title, s.detail);
        if (running && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4.0f)
            ImGui::SetScrollHereY(1.0f);
        ImGui::PopFont();
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
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
        ImGui::TableSetupColumn("##controls", ImGuiTableColumnFlags_WidthStretch, 0.62f);
        ImGui::TableSetupColumn("##monitor", ImGuiTableColumnFlags_WidthStretch, 1.38f);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
    }

    // ---- Controles ----
    const std::string game = !state.gameName.empty() ? state.gameName : state.gameExe;
    Big(UiFont(), 1.45f, IlionTheme::ShellPal::Text, game.empty() ? "Juego desconocido" : game.c_str(), true);
    Coloured(IlionTheme::ShellPal::Text2, StrFmt("%s · %s", gpu.name.c_str(), ArchName(gpu).c_str()).c_str(), true);
    Coloured(IlionTheme::ShellPal::Dim,
             StrFmt("API %s · FSR 4: %s", std::string(magic_enum::enum_name(state.api)).c_str(),
                    std::string(magic_enum::enum_name(gpu.fsr4Support)).c_str()).c_str(), true);
    ImGui::Spacing();
    ImGui::Spacing();

    Coloured(IlionTheme::ShellPal::Text2, "MODO");
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
        if (IlionTheme::Button(StrFmt("%s##ProfileMode%d", modes[i], i).c_str(), ImVec2(modeWidth, 0.0f)))
            mode = (Mode) i;
        if (selected)
            ImGui::PopStyleColor();
    }
    Coloured(IlionTheme::ShellPal::Dim,
             mode == Mode::Calidad      ? "Ultra Calidad o Calidad. 60 FPS reales primero; FG 2X fijo solo con la base asegurada."
             : mode == Mode::Equilibrado ? "Calidad o Balanceado. 60 FPS reales el 90 % del tiempo; FG 2X fijo."
                                         : "Balanceado o Rendimiento, modelos Lite si hace falta. FG 2X fijo (3X solo si cabe sin superar el monitor).",
             true);
    ImGui::Spacing();

    const float full = ImGui::GetContentRegionAvail().x;
    ImGui::PushStyleColor(ImGuiCol_Text, toneMapColor(IlionTheme::ShellPal::Orange));
    if (IlionTheme::Button(running ? "OPTIMIZANDO...##ProfilesGo" : "OPTIMIZAR JUEGO##ProfilesGo", ImVec2(full, std::floor(font * 2.6f))) && !running)
        Begin();
    ImGui::PopStyleColor();
    ImGui::Spacing();

    if (IlionTheme::Button("GUARDAR PERFIL##ProfilesSave", ImVec2(full, 0.0f)))
    {
        if (hasProfile && !ImGui::GetIO().KeyShift)
            lastMessage = "Ya hay un perfil: pulsa GUARDAR PERFIL con Mayús para sustituirlo (el anterior se conserva aparte)";
        else
        {
            if (hasProfile)
                std::filesystem::copy_file(ProfilePath(), PreviousPath(), std::filesystem::copy_options::overwrite_existing, ec);
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
        Coloured(IlionTheme::ShellPilot(IlionTheme::ShellPal::Yellow), lastMessage.c_str(), true);
    ImGui::Spacing();
    Coloured(IlionTheme::ShellPal::Dim,
             "Prototipo experimental. Perfiles configura el juego al pulsar un botón; nada cambia solo durante la partida. "
             "El MFG dinámico no forma parte del perfil.",
             true);

    // ---- Monitor ----
    if (columns)
        ImGui::TableSetColumnIndex(1);
    else
        ImGui::Spacing();
    ActiveMonitor(ctx, columns ? 1.0f : 0.0f);

    if (columns)
        ImGui::EndTable();
}
} // namespace IlionProfiles

#pragma execution_character_set(pop)

void MenuCommon::RenderProfilesTab(RenderMenuContext& ctx) { IlionProfiles::Panel(ctx); }
