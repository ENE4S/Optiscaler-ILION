// ILION — Perfiles inteligentes (prototipo experimental, rama ilion-perfiles-exp). Prueba 3
//
// «Optimizar juego» configura el juego antes de la sesión: mide, decide según el modo elegido (Óptimo, Calidad,
// Equilibrado, Rendimiento), aplica con los mecanismos del menú y guarda el perfil. Nada cambia solo durante la partida.
//
// Resolución interna: ILION no cambia la resolución con la que el juego renderiza. Responde a la consulta del juego
// («qué resolución interna corresponde a este modo de calidad», UpscaleRatioOverride en NVNGX_Parameter.cpp,
// FfxApi_*, FSR2/3_*, XeSS_Common.cpp) y el juego reserva sus recursos con esa respuesta. El cambio llega cuando el
// juego vuelve a consultar. Unreal Engine (Dawnwalker) consulta una sola vez al arrancar y construye una tabla de
// porcentajes: ahí la proporción necesita reiniciar el juego. La resolución que el juego envía en cada fotograma
// (IFeature::RenderWidth, subrect de la evaluación) es la comprobación real.
//
// El MFG dinámico (R3, Dinámico ILION, eDynamic) queda fuera: Perfiles no lo activa, no lo cambia y no lo guarda en el
// perfil. Sí se guarda en los puntos de restauración, para devolver la configuración original completa.
//
// Archivos (junto al juego), ILION\Perfiles\:
//   <exe>.original.ini          configuración anterior a la primera optimización de Perfiles. Se crea una vez y nunca
//                               se sobrescribe
//   <exe>.sesion.ini            configuración anterior a la primera optimización de esta sesión del juego. La de otra
//                               sesión se archiva (<exe>.sesion.<fecha>.ini), nunca se reutiliza ni se borra
//   <exe>.perfil.ini            el perfil guardado
//   <exe>.perfil.anterior.ini   el perfil que había antes del último guardado
//   compatibilidad.ini          matriz por juego (se crea con valores internos; las claves que falten se toman de ellos)
//
// Guardado en UTF-8 con BOM, como el resto de fuentes del menú (sin BOM, MSVC lo lee con la página de códigos del
// sistema y cada acento llega al menú como dos caracteres).
//
// Incluido por menu_common.cpp antes de MenuTabs: usa el catálogo, los selectores y la medición de coste del menú.

#include <SimpleIni.h>

#pragma execution_character_set(push, "utf-8")

namespace IlionProfiles
{
enum class Mode
{
    Optimo,
    Calidad,
    Equilibrado,
    Rendimiento,
    Count,
};

static const char* ModeName(Mode m)
{
    switch (m)
    {
    case Mode::Calidad:
        return "Calidad";
    case Mode::Equilibrado:
        return "Equilibrado";
    case Mode::Rendimiento:
        return "Rendimiento";
    default:
        return "Óptimo";
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

// Lo que cada modo busca. Las cifras son referencias, no límites: se comparan con lo medido y el resultado se marca
// como estimado cuando no hay medición fiable
struct Policy
{
    int best;        // modo de escalado de más calidad permitido (índice de kPresets)
    int fastest;     // el más rápido permitido
    double base;     // FPS reales buscados (media)
    double floor;    // percentil 10 por tiempo mínimo: por debajo la base no es sostenible aunque la media lo sea
    double up;       // con el percentil 10 por encima se sube un paso de calidad
    double fgBase;   // FPS reales mínimos para usar FG
    int fgMax;       // multiplicador fijo máximo (2 = 2X)
    double finalLo;  // FPS finales buscados (0: sin objetivo)
    double finalHi;
    const char* text;
};

static Policy PolicyOf(Mode m)
{
    switch (m)
    {
    case Mode::Calidad:
        return { 0, 2, 60.0, 55.0, 78.0, 60.0, 2, 0.0, 0.0,
                 "Nativa, Ultra Calidad o Calidad. 60 FPS reales sostenidos (caídas breves toleradas). FG 2X fijo como máximo; "
                 "no persigue los Hz del monitor." };
    case Mode::Equilibrado:
        return { 2, 3, 70.0, 60.0, 90.0, 60.0, 3, 140.0, 165.0,
                 "Calidad o Balanceado. Unos 70 FPS reales y 140-165 finales cuando tenga sentido. 2X fijo; 3X solo si la base "
                 "y el monitor lo justifican." };
    case Mode::Rendimiento:
        return { 3, 4, 55.0, 50.0, 80.0, 50.0, 4, 0.0, 0.0,
                 "Balanceado o Rendimiento. 55 FPS reales sostenidos como mínimo. Multiplicador fijo hasta 4X si el proveedor "
                 "lo admite, buscando los Hz del monitor sin sacrificar la base." };
    default:
        return { 0, 2, 70.0, 60.0, 91.0, 60.0, 2, 120.0, 144.0,
                 "Modo predeterminado. Nativa si sobra rendimiento, si no Ultra Calidad o Calidad. Unos 70 FPS reales; FG 2X fijo "
                 "para 120-144 finales cuando el monitor lo permita." };
    }
}

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

enum class Stage
{
    None,        // no se cambia
    Recommended, // recomendado, no aplicado (decide el usuario)
    Selected,    // elegido
    Pending,     // pendiente: el juego debe consultar de nuevo su resolución
    Restart,     // guardado para el próximo inicio del juego
    Applied,     // aplicado, sin confirmación todavía
    Verified,    // el sistema confirma que es lo que corre
    Blocked,     // incompatible con la configuración actual: lo resuelve el usuario
    Failed,      // no llegó a ejecutarse
};

struct Choice
{
    std::string key;
    std::string name;
    bool driverLite = false; // FSR 4 del driver con el modelo INT8Lite de su selector (no es una DLL Lite comunitaria)
    std::string why;
};

enum class FgRoute
{
    None,
    XeFgFixed,
    DlssgGame,
    DlssgOutput,
    FsrFg,
    XeFgNew, // requiere reiniciar
};

// Medición de la sesión: solo fotogramas reales
struct Measure
{
    double mean = 0.0;    // FPS reales medios (por tiempo)
    double p10 = 0.0;     // el 90 % del tiempo observado va a estos FPS reales o más; 0 sin calcular
    double share60 = -1.0;
    double seconds = 0.0;
    int frames = 0;
    bool verified = false; // sin FG y con fotogramas suficientes
    bool limited = false;  // un límite o un MFG dinámico sujeta los FPS reales
    bool withFg = false;
    std::string source;
};

// La configuración del juego antes de optimizar, para el resumen de cambios
struct Before
{
    std::string scaler;
    int preset = -1;
    unsigned rw = 0, rh = 0, tw = 0, th = 0;
    std::string fg;
    int multiplier = 0;
};

struct Plan
{
    Choice scaler;
    bool changeScaler = false;
    Stage scalerStage = Stage::None;
    int preset = -1;
    Stage presetStage = Stage::None;
    std::string presetWhy;
    FgRoute fg = FgRoute::None;
    FGInput fgInput = FGInput::NoFG;
    int count = 0;
    Stage fgStage = Stage::None;
    std::string fgWhy;
    bool restartFg = false;
    bool restartRatio = false;
};

static Mode mode = Mode::Optimo;
static bool modeLoaded = false;
static std::vector<Step> steps;
static std::vector<Step> changes; // el resumen de la última optimización
static bool running = false;
static bool finished = false;
static int phase = 0;
static double phaseStart = 0.0;
static bool entered = false;
static bool litePending = false;
static Plan plan;
static Measure measure;
static Before before;
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

static std::filesystem::path FileOf(const std::wstring& suffix) { return Folder() / (string_to_wstring(ExeKey()) + suffix); }
static std::filesystem::path ProfilePath() { return FileOf(L".perfil.ini"); }
static std::filesystem::path PreviousPath() { return FileOf(L".perfil.anterior.ini"); }
static std::filesystem::path OriginalPath() { return FileOf(L".original.ini"); }
static std::filesystem::path SessionPath() { return FileOf(L".sesion.ini"); }
static std::filesystem::path LegacyRestorePath() { return FileOf(L".restauracion.ini"); }
static std::filesystem::path MatrixPath() { return Folder() / L"compatibilidad.ini"; }

static bool Exists(const std::filesystem::path& p)
{
    std::error_code ec;
    return std::filesystem::exists(p, ec);
}

static std::string NowText()
{
    const std::time_t t = std::time(nullptr);
    std::tm tm {};
    localtime_s(&tm, &t);
    char buf[40] = {};
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
    return buf;
}

// La sesión del juego: proceso y hora a la que arrancó. Un punto de otra sesión nunca se toma por uno de esta
static std::string SessionId()
{
    static std::string id;
    if (!id.empty())
        return id;
    FILETIME created {}, exited {}, kernel {}, user {};
    uint64_t start = 0;
    if (GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user))
        start = ((uint64_t) created.dwHighDateTime << 32) | created.dwLowDateTime;
    id = StrFmt("%lu-%llu", GetCurrentProcessId(), (unsigned long long) start);
    return id;
}

// ---- Matriz de compatibilidad --------------------------------------------------------------------------------------
static const char* kDefaultMatrix =
    "; ILION — Perfiles inteligentes: compatibilidad por juego (se puede editar; se lee al optimizar)\n"
    "; [ejecutable.exe] en minúsculas. Una clave que falte se toma de los valores internos de ILION\n"
    ";   nombre     = nombre del juego\n"
    ";   origen     = ILION (comprobado en ILION) | OptiScaler wiki (documentado, sin comprobar en ILION)\n"
    ";   entradas   = entradas de escalado que ofrece el juego (según la fuente)\n"
    ";   evitar     = rutas que no se eligen: fg, xefg, dlssg_salida, ratio\n"
    ";   proporcion = inicio (el juego consulta la resolución interna solo al arrancar) | caliente (vuelve a consultar)\n"
    ";   nota       = texto que se muestra en el registro\n"
    "\n"
    "[dawnwalker.exe]\n"
    "nombre = The Blood of Dawnwalker\n"
    "origen = ILION\n"
    "entradas = DLSS, FSR, XeSS\n"
    "evitar = dlssg_salida\n"
    "proporcion = inicio\n"
    "nota = Comprobado en ILION (RX 7900 XTX): FSR 4 (driver, SDK y variantes), XeFG y Enabler. Unreal Engine 5.5: consulta la resolución interna una vez al arrancar (registro de ILION).\n"
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
    std::string name, origin, inputs, avoid, note, ratio;
    bool Avoids(const char* what) const { return avoid.find(what) != std::string::npos; }
};

static GameRule LoadRule()
{
    GameRule rule;
    std::error_code ec;
    std::filesystem::create_directories(Folder(), ec);
    if (!Exists(MatrixPath()))
    {
        std::ofstream out(MatrixPath(), std::ios::binary);
        out << kDefaultMatrix;
    }

    CSimpleIniA file(true), internal(true);
    internal.LoadData(kDefaultMatrix);
    const bool fileOk = file.LoadFile(MatrixPath().c_str()) >= 0;
    const std::string section = ExeKey();
    const bool inFile = fileOk && file.GetSectionSize(section.c_str()) >= 0;
    const bool inInternal = internal.GetSectionSize(section.c_str()) >= 0;
    if (!inFile && !inInternal)
        return rule;

    // El archivo manda; lo que no tiene se toma de los valores internos (el archivo de una prueba anterior no pierde
    // las claves nuevas)
    const auto get = [&](const char* key)
    {
        const char* v = inFile ? file.GetValue(section.c_str(), key, nullptr) : nullptr;
        if (v == nullptr && inInternal)
            v = internal.GetValue(section.c_str(), key, nullptr);
        return std::string(v != nullptr ? v : "");
    };
    rule.found = true;
    rule.name = get("nombre");
    rule.origin = get("origen");
    rule.inputs = get("entradas");
    rule.avoid = get("evitar");
    rule.ratio = get("proporcion");
    rule.note = get("nota");
    return rule;
}

// La proporción se lee solo al arrancar: lo dice la matriz, o el juego es de Unreal Engine (su complemento de escalado
// construye la tabla de porcentajes una vez, como se ve en el registro de Dawnwalker)
static bool RatioNeedsRestart(const GameRule& rule, std::string* why = nullptr)
{
    if (rule.found && rule.ratio == "inicio")
    {
        if (why != nullptr)
            *why = "compatibilidad.ini: el juego consulta la resolución interna solo al arrancar";
        return true;
    }
    if (rule.found && rule.ratio == "caliente")
        return false;
    if (State::Instance().gameEngine == GameEngineType::Unreal)
    {
        if (why != nullptr)
            *why = "Unreal Engine: consulta la resolución interna al arrancar (estimado para este juego)";
        return true;
    }
    return false;
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
// comunitaria (DLL propia), Helix FSR 4; respaldo XeSS y FSR 3.1. NVIDIA: DLSS, XeSS, FSR 3.1. Intel y otras: XeSS,
// FSR 3.1. lite: la base no llega al objetivo del modo en su modo de escalado más rápido
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
                add(driver, true, "La base no llega al objetivo en el modo de escalado más rápido: FSR 4 del driver con su modelo INT8Lite (mejora sin medir)");
            add(communityLite, false, "La base no llega al objetivo en el modo de escalado más rápido: FSR 4 Lite comunitaria (mejora sin medir)");
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

// ---- Configuración ---------------------------------------------------------------------------------------------------
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

// Lo que el perfil gestiona: escalador, modelo de FSR 4, proporción y FG fijo
static void SaveManaged(CSimpleIniA& ini, const char* section, IFeature* feature)
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

// Lo que el perfil no gestiona pero la restauración devuelve tal como estaba: MFG dinámico, su objetivo, límite y
// redondeo, y el límite de FPS
static void SaveExternal(CSimpleIniA& ini, const char* section)
{
    auto config = Config::Instance();
    ini.SetValue(section, "xefg_dinamico", OptStr(config->FGXeFGDynamicMFG).c_str());
    ini.SetValue(section, "xefg_objetivo", OptStr(config->FGXeFGFramerateTargetDMFG).c_str());
    ini.SetValue(section, "xefg_objetivo_limite", OptStr(config->FGXeFGTargetAsCapDMFG).c_str());
    ini.SetValue(section, "xefg_redondeo", OptStr(config->FGXeFGRoundingDMFG).c_str());
    ini.SetValue(section, "dlssg_dinamico_ilion", OptStr(config->FGDLSSGIlionDynamic).c_str());
    ini.SetValue(section, "dlssg_dinamico_auto", OptStr(config->FGDLSSGOverrideForceDMFG).c_str());
    ini.SetValue(section, "dlssg_dinamico_salida", OptStr(config->FGDLSSGForceDMFG).c_str());
    ini.SetValue(section, "dlssg_objetivo", OptStr(config->FGDLSSGFramerateTargetDMFG).c_str());
    ini.SetValue(section, "limite_fps", OptStr(config->FramerateLimit).c_str());
}

static void LoadExternal(const CSimpleIniA& ini, const char* section)
{
    auto config = Config::Instance();
    if (ini.GetValue(section, "xefg_dinamico", nullptr) == nullptr)
        return;
    SetFrom(config->FGXeFGDynamicMFG, ini.GetValue(section, "xefg_dinamico", nullptr), ParseBool);
    SetFrom(config->FGXeFGFramerateTargetDMFG, ini.GetValue(section, "xefg_objetivo", nullptr), ParseFloat);
    SetFrom(config->FGXeFGTargetAsCapDMFG, ini.GetValue(section, "xefg_objetivo_limite", nullptr), ParseBool);
    SetFrom(config->FGXeFGRoundingDMFG, ini.GetValue(section, "xefg_redondeo", nullptr), ParseInt);
    SetFrom(config->FGDLSSGIlionDynamic, ini.GetValue(section, "dlssg_dinamico_ilion", nullptr), ParseBool);
    SetFrom(config->FGDLSSGOverrideForceDMFG, ini.GetValue(section, "dlssg_dinamico_auto", nullptr), ParseBool);
    SetFrom(config->FGDLSSGForceDMFG, ini.GetValue(section, "dlssg_dinamico_salida", nullptr), ParseBool);
    SetFrom(config->FGDLSSGFramerateTargetDMFG, ini.GetValue(section, "dlssg_objetivo", nullptr), ParseFloat);
    SetFrom(config->FramerateLimit, ini.GetValue(section, "limite_fps", nullptr), ParseFloat);
}

// Algún MFG dinámico activo o preparado para el próximo inicio: configuración externa al perfil
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

// Escribe lo guardado en la configuración en memoria. withFg: también el FG fijo (no con un Dynamic externo activo).
// restart: cambian la entrada o la salida de FG cargadas
static void LoadManaged(const CSimpleIniA& ini, const char* section, bool withFg, std::string& scalerKey, std::string& fsr4Model,
                        bool& restart)
{
    auto config = Config::Instance();
    scalerKey = ini.GetValue(section, "escalador", "");
    fsr4Model = ini.GetValue(section, "fsr4_modelo", "auto");
    SetFrom(config->UpscaleRatioOverrideEnabled, ini.GetValue(section, "ratio_activo", nullptr), ParseBool);
    SetFrom(config->UpscaleRatioOverrideValue, ini.GetValue(section, "ratio", nullptr), ParseFloat);
    if (withFg)
    {
        SetFrom(config->FGEnabled, ini.GetValue(section, "fg_activado", nullptr), ParseBool);
        SetFrom(config->FGInput, ini.GetValue(section, "fg_entrada", nullptr), ParseEnum<FGInput>);
        SetFrom(config->FGOutput, ini.GetValue(section, "fg_salida", nullptr), ParseEnum<FGOutput>);
        SetFrom(config->FGNvngxReplacement, ini.GetValue(section, "fg_nvngx", nullptr), ParseEnum<FGNvngxReplacement>);
        SetFrom(config->FGXeFGInterpolationCount, ini.GetValue(section, "xefg_multiplicador_fijo", nullptr), ParseInt);
        SetFrom(config->FGDLSSGOverrideInterpolationCount, ini.GetValue(section, "dlssg_multiplicador_fijo", nullptr), ParseInt);
        SetFrom(config->FGDLSSGInterpolationCount, ini.GetValue(section, "dlssg_salida_multiplicador_fijo", nullptr), ParseInt);
    }
    const auto& state = State::Instance();
    restart = config->FGInput.value_or_default() != state.activeFgInput || config->FGOutput.value_or_default() != state.activeFgOutput;
}

static void SaveSnapshot(const std::filesystem::path& path, const char* origin, IFeature* feature)
{
    CSimpleIniA ini(true);
    SaveManaged(ini, "configuracion", feature);
    SaveExternal(ini, "externo");
    ini.SetValue("origen", "juego", ExeKey().c_str());
    ini.SetValue("origen", "sesion", SessionId().c_str());
    ini.SetValue("origen", "fecha", NowText().c_str());
    ini.SetValue("origen", "procedencia", origin);
    ini.SetValue("origen", "ilion", BuildInfo::IlionVersion());
    std::error_code ec;
    std::filesystem::create_directories(Folder(), ec);
    ini.SaveFile(path.c_str());
}

// Los puntos de restauración, antes del primer cambio. Devuelve lo hecho para el registro
static std::vector<Step> ProtectRestorePoints(IFeature* feature)
{
    std::vector<Step> out;
    if (!Exists(OriginalPath()))
    {
        SaveSnapshot(OriginalPath(), "configuración anterior a la primera optimización de Perfiles", feature);
        out.push_back({ Mark::Ok, "Punto original protegido", "ILION\\Perfiles\\" + ExeKey() + ".original.ini (no se sobrescribe nunca)" });
        if (Exists(LegacyRestorePath()))
            out.push_back({ Mark::Info, "Hay un punto de restauración de una prueba anterior",
                            ExeKey() + ".restauracion.ini se conserva sin usar: puede no ser la configuración original" });
    }
    else
        out.push_back({ Mark::Info, "Punto original conservado", "ILION\\Perfiles\\" + ExeKey() + ".original.ini" });

    // Un punto por sesión del juego: el de otra sesión se archiva con su fecha, nunca se reutiliza ni se borra
    CSimpleIniA session(true);
    const bool hasSession = Exists(SessionPath()) && session.LoadFile(SessionPath().c_str()) >= 0;
    const std::string sessionOf = hasSession ? session.GetValue("origen", "sesion", "") : "";
    if (hasSession && sessionOf == SessionId())
    {
        out.push_back({ Mark::Info, "Punto de esta sesión conservado", "Se mantiene el de la primera optimización de la sesión" });
        return out;
    }
    if (Exists(SessionPath()))
    {
        std::string date = hasSession ? session.GetValue("origen", "fecha", "anterior") : "anterior";
        std::replace(date.begin(), date.end(), ':', '-');
        std::replace(date.begin(), date.end(), ' ', '_');
        auto archived = FileOf(L".sesion." + string_to_wstring(date) + L".ini");
        for (int i = 2; Exists(archived) && i < 100; i++)
            archived = FileOf(L".sesion." + string_to_wstring(date) + L"_" + std::to_wstring(i) + L".ini");
        std::error_code ec;
        std::filesystem::rename(SessionPath(), archived, ec);
        out.push_back({ ec ? Mark::Bad : Mark::Info, ec ? "No se pudo archivar el punto de otra sesión" : "Punto de otra sesión archivado",
                        wstring_to_string(archived.filename().wstring()) });
        if (ec)
            return out; // no se sobrescribe: el registro lo dice y no se crea el de esta sesión
    }
    SaveSnapshot(SessionPath(), "configuración anterior a la primera optimización de esta sesión", feature);
    out.push_back({ Mark::Ok, "Punto de esta sesión guardado", "ILION\\Perfiles\\" + ExeKey() + ".sesion.ini" });
    return out;
}

// La anulación de proporción actual: de quién es
enum class RatioOwner
{
    None,
    Perfiles,
    External,
};

static RatioOwner OwnerOfOverride(float* value = nullptr)
{
    auto config = Config::Instance();
    if (!config->UpscaleRatioOverrideEnabled.value_or_default())
        return RatioOwner::None;
    const float v = config->UpscaleRatioOverrideValue.value_or_default();
    if (value != nullptr)
        *value = v;
    CSimpleIniA ini(true);
    if (Exists(ProfilePath()) && ini.LoadFile(ProfilePath().c_str()) >= 0 && strcmp(ini.GetValue("perfil", "ratio_activo", ""), "true") == 0)
    {
        float saved = 0.0f;
        if (ParseFloat(ini.GetValue("perfil", "ratio", ""), saved) && std::fabs(saved - v) < 0.005f)
            return RatioOwner::Perfiles;
    }
    return RatioOwner::External;
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

// FPS reales: los fotogramas del juego, nunca los generados. Sin FG, por tiempo: la media, el percentil 10 (el 90 % del
// tiempo observado va a esos FPS o más) y la parte del tiempo con 60 o más
static Measure MeasureNow()
{
    Measure m;
    const auto status = IlionDynamicMfg::Get();
    const auto& state = State::Instance();
    const float limit = Config::Instance()->FramerateLimit.value_or_default();

    if (Generating(status))
    {
        m.withFg = true;
        m.mean = status.baseFps;
        m.limited = status.limitFps > 0.0f || IlionDynamicMfg::DynamicEnabled() || status.backendDynamic;
        m.source = m.limited ? StrFmt("con FG y un límite activo (%.0f FPS finales): los FPS reales los fija el límite", status.limitFps)
                   : status.baseMeasured ? "fotogramas reales contados con FG activo; la estabilidad no se mide con FG"
                                         : "estimado a partir de los FPS finales";
        return m;
    }

    std::vector<double> times;
    times.reserve(state.frameTimes.size());
    for (const double ft : state.frameTimes)
        if (ft > 0.0 && ft <= 1000.0)
            times.push_back(ft);
    m.frames = (int) times.size();
    if (times.empty())
    {
        m.source = "sin fotogramas observados";
        return m;
    }
    double total = 0.0, above60 = 0.0;
    for (const double ft : times)
    {
        total += ft;
        above60 += ft <= 1000.0 / 60.0 + 0.05 ? ft : 0.0;
    }
    m.seconds = total / 1000.0;
    m.mean = times.size() * 1000.0 / total;
    m.share60 = above60 / total;
    // Percentil 10 por tiempo: los fotogramas más lentos primero, hasta sumar el 10 % del tiempo
    std::sort(times.begin(), times.end(), std::greater<double>());
    double acc = 0.0;
    for (const double ft : times)
    {
        acc += ft;
        if (acc >= total * 0.1)
        {
            m.p10 = 1000.0 / ft;
            break;
        }
    }
    if (m.frames >= 120 && m.seconds >= 1.5)
    {
        m.limited = limit > 0.0f && limit < 60.0f;
        m.verified = !m.limited;
        m.source = StrFmt("%d fotogramas (%.1f s) sin FG", m.frames, m.seconds);
    }
    else
        m.source = StrFmt("solo %d fotogramas observados", m.frames);
    return m;
}

// La base frente al objetivo del modo: -1 por debajo, 0 cumple, 1 con margen para más calidad, -2 sin datos
static int Judge(const Measure& m, const Policy& p)
{
    if (m.limited || m.mean <= 0.0)
        return -2;
    if (m.verified)
    {
        // Sostenido: el 90 % del tiempo. Las caídas breves no cuentan; una media alta con caídas largas sí
        if (m.p10 < p.floor || m.mean < p.base)
            return -1;
        return m.p10 >= p.up ? 1 : 0;
    }
    if (!m.withFg)
        return -2; // sin FG pero con pocos fotogramas
    // Con FG: solo la media de los fotogramas reales, estimado
    return m.mean < p.base ? -1 : m.mean >= p.up ? 1 : 0;
}

// ---- Registro ------------------------------------------------------------------------------------------------------
static void Add(Mark mark, std::string title, std::string detail = {}) { steps.push_back({ mark, std::move(title), std::move(detail) }); }
static Step& Last() { return steps.back(); }

static void Begin()
{
    steps.clear();
    changes.clear();
    plan = {};
    measure = {};
    before = {};
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

static void SaveProfile(IFeature* feature)
{
    if (Exists(ProfilePath()))
    {
        std::error_code ec;
        std::filesystem::copy_file(ProfilePath(), PreviousPath(), std::filesystem::copy_options::overwrite_existing, ec);
    }
    CSimpleIniA ini(true);
    SaveManaged(ini, "perfil", feature);
    ini.SetValue("perfil", "modo", ModeName(mode));
    ini.SetValue("perfil", "fecha", NowText().c_str());
    ini.SetValue("perfil", "sesion", SessionId().c_str());
    if (plan.preset >= 0)
        ini.SetValue("perfil", "modo_escalado", kPresets[plan.preset].name);
    if (plan.fg != FgRoute::None && plan.count > 0 && plan.fgStage != Stage::Blocked && plan.fgStage != Stage::Recommended)
        ini.SetValue("perfil", "fg_multiplicador", StrFmt("%dX", plan.count + 1).c_str());
    std::string restart = plan.restartFg ? "fg" : "";
    if (plan.restartRatio)
        restart += restart.empty() ? "proporcion" : ",proporcion";
    ini.SetValue("perfil", "reinicio_pendiente", restart.empty() ? "no" : restart.c_str());
    if (before.rw > 0)
        ini.SetValue("perfil", "resolucion_anterior", StrFmt("%ux%u", before.rw, before.rh).c_str());
    if (measure.mean > 0.0)
        ini.SetValue("perfil", "fps_reales", StrFmt("%.1f%s", measure.mean, measure.verified ? "" : " (estimado)").c_str());
    if (measure.p10 > 0.0)
        ini.SetValue("perfil", "fps_reales_p10", StrFmt("%.1f", measure.p10).c_str());
    std::error_code ec;
    std::filesystem::create_directories(Folder(), ec);
    ini.SaveFile(ProfilePath().c_str());
}

static std::string ResText(unsigned w, unsigned h) { return w > 0 ? StrFmt("%u×%u", w, h) : std::string("?"); }

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
    const Policy policy = PolicyOf(mode);
    static GameRule rule;
    static bool upscaling = false;
    static int judge = -2;

    switch (phase)
    {
    case 0: // juego, GPU, monitor y lo que hay antes de cambiar nada
    {
        const std::string game = !state.gameName.empty() ? state.gameName : state.gameExe;
        Add(Mark::Ok, "Juego: " + (game.empty() ? std::string("desconocido") : game),
            StrFmt("%s · API %s · motor %s · modo %s", ExeKey().c_str(), std::string(magic_enum::enum_name(state.api)).c_str(),
                   std::string(magic_enum::enum_name(state.gameEngine)).c_str(), ModeName(mode)));
        const double hz = RefreshHz();
        Add(gpu.vendorId == VendorId::Invalid ? Mark::Bad : Mark::Ok, "GPU: " + gpu.name,
            StrFmt("%s · %.0f GB · FSR 4: %s · monitor %s", ArchName(gpu).c_str(), gpu.dedicatedVramInBytes / (1024.0 * 1024.0 * 1024.0),
                   std::string(magic_enum::enum_name(gpu.fsr4Support)).c_str(), hz > 0.0 ? StrFmt("%.0f Hz", hz).c_str() : "desconocido"));

        upscaling = feature != nullptr && feature->IsInited();
        const auto catalog = GatherUpscalers(feature);
        const UpscalerVariant* current = Current(catalog);
        const auto status = IlionDynamicMfg::Get();
        before.scaler = current != nullptr ? current->name + (IsDriver(*current) && DriverLiteSelected() ? " · modelo INT8Lite" : "") : "ninguno";
        before.fg = status.provider == IlionDynamicMfg::Provider::None ? "sin FG" : status.name;
        before.multiplier = Generating(status) ? status.multiplier : 0;
        if (upscaling)
        {
            before.rw = feature->RenderWidth();
            before.rh = feature->RenderHeight();
            before.tw = feature->TargetWidth();
            before.th = feature->TargetHeight();
            before.preset = before.rw > 0 ? PresetOf((float) before.tw / (float) before.rw) : -1;
            Add(Mark::Ok, "El juego envía su escalado a ILION",
                StrFmt("Entrada %s · %s → %s · modo activo %s", ApiUpscalerInputName(state.currentInputApiName).c_str(),
                       ResText(before.rw, before.rh).c_str(), ResText(before.tw, before.th).c_str(),
                       before.preset >= 0 ? kPresets[before.preset].name : "?"));
        }
        else
            Add(Mark::Bad, "El juego no envía escalado a ILION", "Activa DLSS, FSR o XeSS en las opciones gráficas del juego y vuelve a optimizar.");

        // La anulación de proporción que ya hay: de quién es y si el juego la usa
        float value = 0.0f;
        const RatioOwner owner = OwnerOfOverride(&value);
        if (owner != RatioOwner::None)
        {
            const bool used = before.rw > 0 && std::fabs((float) before.tw / (float) before.rw - value) < 0.04f;
            Add(Mark::Info, StrFmt("Anulación de proporción %.2f: %s", value, owner == RatioOwner::Perfiles ? "de Perfiles" : "externa (manual u otra herramienta)"),
                used ? "El juego la está usando" : "El juego no la está usando ahora (pendiente de que la consulte)");
        }

        rule = LoadRule();
        if (rule.found)
            Add(Mark::Info, "Compatibilidad: " + rule.origin + (rule.origin == "ILION" ? " (comprobado)" : " (documentado)"),
                (rule.inputs.empty() ? std::string() : "Entradas: " + rule.inputs + ". ") + rule.note);
        Next();
        break;
    }
    case 1: // medición
    {
        measure = MeasureNow();
        judge = Judge(measure, policy);
        const std::string detail = measure.verified ? StrFmt("Media %.0f · el 90%% del tiempo a %.0f o más · %.0f%% del tiempo con 60 o más · %s", measure.mean,
                                                             measure.p10, measure.share60 * 100.0, measure.source.c_str())
                                                    : measure.source;
        if (judge == -2)
            Add(Mark::Wait, measure.limited ? "FPS reales: no evaluables ahora" : "FPS reales: sin datos suficientes",
                detail + ". No se cambia el modo de escalado ni el FG." +
                    (measure.limited ? " Para medir, desactiva el FG (o el Dynamic y el límite), juega 15 s y vuelve a optimizar."
                                     : " Juega unos segundos más y vuelve a optimizar."));
        else
            Add(judge < 0 ? Mark::Bad : Mark::Ok,
                StrFmt("FPS reales: %.0f · objetivo del modo %s (%.0f): %s%s", measure.mean, ModeName(mode), policy.base,
                       judge < 0 ? "no alcanzado" : judge > 0 ? "cumplido con margen" : "cumplido", measure.verified ? "" : " (estimado)"),
                detail);
        Next();
        break;
    }
    case 2: // modo de escalado
    {
        if (!upscaling || before.preset < 0 || (rule.found && rule.Avoids("ratio")))
        {
            plan.presetWhy = !upscaling ? "El juego no envía escalado" : "La matriz de compatibilidad lo excluye";
            Add(Mark::Info, "Modo de escalado: no se cambia", plan.presetWhy);
            Next();
            break;
        }
        const int cur = before.preset;
        int target = cur;
        if (cur > policy.fastest)
        {
            // Más rápido de lo que el modo permite: al modo de escalado más rápido del modo
            target = policy.fastest;
            plan.presetWhy = StrFmt("%s está por debajo de la calidad del modo %s", kPresets[cur].name, ModeName(mode));
        }
        else if (cur < policy.best)
        {
            // Más calidad de la que el modo busca: se pasa a su mejor modo de escalado, salvo que la base vaya sobrada
            target = judge == 1 ? cur : policy.best;
            plan.presetWhy = judge == 1 ? "Por encima de la calidad del modo, pero la base va sobrada: se mantiene"
                                        : StrFmt("El modo %s busca más FPS reales: de %s a %s", ModeName(mode), kPresets[cur].name,
                                                 kPresets[policy.best].name);
            if (judge == -2)
            {
                target = cur;
                plan.presetWhy = "Sin medición válida: se mantiene";
            }
        }
        else if (judge == -1 && cur < policy.fastest)
        {
            target = cur + 1;
            plan.presetWhy = "La base no llega al objetivo del modo de forma sostenida: un paso hacia más rendimiento";
        }
        else if (judge == 1 && cur > policy.best)
        {
            target = cur - 1;
            plan.presetWhy = "La base tiene margen: un paso hacia más calidad";
        }
        else
            plan.presetWhy = judge == -2   ? "Sin medición válida: se mantiene"
                             : judge == -1 ? "La base no llega al objetivo, pero ya es el modo de escalado más rápido del modo"
                                           : "La base cumple el objetivo del modo en este modo de escalado";
        plan.preset = target;
        if (target != cur)
        {
            plan.presetStage = Stage::Selected;
            Add(Mark::Wait, StrFmt("Modo de escalado: %s → %s", kPresets[cur].name, kPresets[target].name),
                plan.presetWhy + " (efecto estimado: se verifica al medir de nuevo)");
        }
        else
            Add(Mark::Ok, StrFmt("Modo de escalado: %s (se mantiene)", kPresets[cur].name), plan.presetWhy);
        Next();
        break;
    }
    case 3: // escalador y variante
    {
        if (!upscaling)
        {
            Next();
            break;
        }
        const bool liteNeeded = judge == -1 && plan.preset >= policy.fastest;
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
        const UpscalerVariant* chosen = Find(catalog, plan.scaler.key);
        const bool same = current != nullptr && KeyOf(*current) == plan.scaler.key && DriverLiteSelected() == plan.scaler.driverLite;
        const bool lite = plan.scaler.driverLite || (chosen != nullptr && IsCommunityLite(*chosen));

        if (lite && liteNeeded && (mode == Mode::Optimo || mode == Mode::Calidad))
        {
            // Una alternativa de rendimiento sin medir: en Óptimo y Calidad solo se recomienda
            plan.scalerStage = Stage::Recommended;
            Add(Mark::Wait, "Recomendado (no aplicado): " + plan.scaler.name, plan.scaler.why + ". En este modo no se cambia sin tu decisión.");
        }
        else if (same)
        {
            plan.scalerStage = Stage::Verified;
            Add(Mark::Ok, "Escalador: " + plan.scaler.name + " (ya activo)", plan.scaler.why);
        }
        else
        {
            plan.changeScaler = true;
            plan.scalerStage = Stage::Selected;
            Add(Mark::Wait, "Escalador elegido: " + plan.scaler.name, plan.scaler.why + (current != nullptr ? " · antes: " + current->name : std::string()));
        }
        Next();
        break;
    }
    case 4: // generación de fotogramas fija
    {
        const auto status = IlionDynamicMfg::Get();
        const double hz = RefreshHz();
        std::string dynamicWhat;
        const bool dynamic = ExternalDynamic(&dynamicWhat);
        const double reference = MfgCostReference();
        const double cost = Generating(status) && reference > 0.0 && mfgCost.current > 0.0 ? 1.0 - mfgCost.current / reference : -1.0;
        // Sin FG la base aún no paga el coste del FG: se descuenta un 10 % (coste sin medir, se dice)
        const double base = Generating(status) ? measure.mean : measure.mean * 0.9;

        // El multiplicador: el menor que acerca los FPS finales a lo que busca el modo sin pasar del monitor
        int maxCount = policy.fgMax - 1;
        if (mode == Mode::Equilibrado && !(measure.verified && measure.p10 >= 70.0))
            maxCount = 1; // 3X excepcional: solo con 70 reales sostenidos y medidos sin FG
        if (mode == Mode::Rendimiento && base < 60.0)
            maxCount = std::min(maxCount, 2); // 4X solo con 60 reales o más
        const double wanted = mode == Mode::Rendimiento ? hz : policy.finalLo;
        const double ceiling = hz > 0.0 ? hz * 1.05 : 0.0;
        int count = 1;
        while (count < maxCount && wanted > 0.0 && base * (count + 1) < wanted * 0.9 && (ceiling <= 0.0 || base * (count + 2) <= ceiling))
            count++;
        const bool useful = hz <= 0.0 || base * 2.0 <= hz * 1.1;

        if (rule.found && rule.Avoids("fg"))
            plan.fgWhy = "la matriz de compatibilidad lo excluye para este juego";
        else if (state.api != API::DX12)
            plan.fgWhy = "solo DirectX 12 está verificado";
        else if (judge == -2)
            plan.fgWhy = "sin FPS reales válidos no se decide el FG";
        else if (base < policy.fgBase)
        {
            plan.fgStage = Generating(status) ? Stage::Recommended : Stage::None;
            plan.fgWhy = StrFmt("la base (%.0f reales%s) no llega a %.0f", base, Generating(status) ? "" : " tras el coste estimado", policy.fgBase);
        }
        else if (cost > 0.20)
        {
            plan.fgStage = Stage::Recommended;
            plan.fgWhy = StrFmt("el FG cuesta un %.0f%% de los FPS reales (medido): conviene reconsiderarlo", cost * 100.0);
        }
        else if (!useful)
            plan.fgWhy = StrFmt("%.0f FPS reales ya cubren el monitor de %.0f Hz", base, hz);
        else if (dynamic)
        {
            plan.fgStage = Stage::Blocked;
            plan.fgWhy = dynamicWhat + " está activo (configuración externa). Perfiles no lo modifica: desactívalo en FRAME GEN para usar " +
                         StrFmt("%dX fijo", count + 1);
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
                if (StreamlineHooks::isDlssgHooked())
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
                    count = 1; // lo que XeFG admite se conoce al cargarlo: 2X hasta entonces
                }
                break;
            }
            // Lo que el proveedor admite de verdad
            if (plan.fg == FgRoute::XeFgFixed || plan.fg == FgRoute::DlssgOutput)
                count = std::clamp(count, 1, std::max(IlionDynamicMfg::MaxCountLimit(), 1));
            else if (plan.fg == FgRoute::DlssgGame)
                count = std::clamp(count, 1, std::max(state.dlssgMfgMax.value_or(1), 1));
            plan.count = count;
            if (plan.fg == FgRoute::None)
                plan.fgWhy = "no hay una ruta fija verificada para esta combinación";
            else
                plan.fgStage = Stage::Selected;
        }

        if (plan.fgStage == Stage::Selected)
        {
            const char* route = plan.fg == FgRoute::XeFgFixed     ? "XeFG (ya cargado)"
                                : plan.fg == FgRoute::DlssgGame   ? "DLSSG del juego"
                                : plan.fg == FgRoute::DlssgOutput ? "salida DLSSG de ILION"
                                : plan.fg == FgRoute::FsrFg       ? "FSR FG (ya cargado)"
                                : plan.fgInput == FGInput::DLSSG  ? "XeFG nuevo (entrada DLSSG del juego)"
                                                                  : "XeFG nuevo (entrada OptiFG)";
            Add(Mark::Wait, StrFmt("Frame Generation: %s · %dX fijo", route, count + 1),
                StrFmt("%.0f FPS reales × %d ≈ %.0f finales%s · sin MFG dinámico%s", base, count + 1, base * (count + 1),
                       hz > 0.0 ? StrFmt(" · monitor %.0f Hz", hz).c_str() : "", Generating(status) ? "" : " · coste del FG sin medir"));
        }
        else
            Add(plan.fgStage == Stage::Blocked || plan.fgStage == Stage::Recommended ? Mark::Wait : Mark::Info,
                plan.fgStage == Stage::Blocked       ? "Frame Generation: pendiente de tu decisión"
                : plan.fgStage == Stage::Recommended ? "Frame Generation: recomendado desactivarlo (no se cambia)"
                                                     : "Frame Generation: no se cambia",
                plan.fgWhy);
        Next();
        break;
    }
    case 5: // puntos de restauración, antes del primer cambio
    {
        for (auto& s : ProtectRestorePoints(feature))
            steps.push_back(std::move(s));
        Next();
        break;
    }
    case 6: // aplicar el escalador: una vez, y se confirma que corre. No se prueba otro
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
                config->Fsr4Model.reset();
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
            plan.scalerStage = Stage::Failed;
            Last() = { Mark::Bad, "No llegó a ejecutarse: " + plan.scaler.name,
                       "No se prueba otro escalador por tu cuenta. Usa RESTAURAR SESIÓN si el juego no escala bien." };
            litePending = false;
            Next();
        }
        break;
    }
    case 7: // modo de escalado: la anulación de proporción, que el juego lee al consultar su resolución interna
    {
        if (plan.presetStage == Stage::Selected && plan.preset >= 0)
        {
            float previous = 0.0f;
            const RatioOwner owner = OwnerOfOverride(&previous);
            config->UpscaleRatioOverrideEnabled = true;
            config->UpscaleRatioOverrideValue = kPresets[plan.preset].ratio;
            std::string why;
            plan.restartRatio = RatioNeedsRestart(rule, &why);
            plan.presetStage = plan.restartRatio ? Stage::Restart : Stage::Pending;
            if (owner == RatioOwner::External)
                Add(Mark::Info, StrFmt("Anulación externa %.2f sustituida", previous), "La original queda en los puntos de restauración");
            if (plan.restartRatio)
                Add(Mark::Restart, StrFmt("Modo de escalado %s: requiere reiniciar el juego", kPresets[plan.preset].name),
                    StrFmt("Proporción %.2f guardada (%s). Se verifica al siguiente inicio.", kPresets[plan.preset].ratio, why.c_str()));
            else
                Add(Mark::Wait, StrFmt("Modo de escalado %s: pendiente de aplicación", kPresets[plan.preset].name),
                    StrFmt("Proporción %.2f guardada. Se aplica cuando el juego vuelva a consultar su resolución interna; si no "
                           "cambia, cambia la calidad en el menú del juego o reinícialo.",
                           kPresets[plan.preset].ratio));
        }
        Next();
        break;
    }
    case 8: // generación de fotogramas fija
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
                plan.fgStage = Stage::Restart;
                plan.restartFg = true;
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
    case 9: // guardar el perfil y el resumen de cambios
    {
        config->SaveIni();
        SaveProfile(feature);

        const auto status = IlionDynamicMfg::Get();
        // Escalador
        if (plan.scalerStage == Stage::Verified && plan.changeScaler)
            changes.push_back({ Mark::Ok, "Escalador: " + before.scaler + " → " + plan.scaler.name, "Verificado" });
        else if (plan.scalerStage == Stage::Failed)
            changes.push_back({ Mark::Bad, "Escalador: " + plan.scaler.name + " no llegó a ejecutarse", "Sigue " + before.scaler });
        else if (plan.scalerStage == Stage::Recommended)
            changes.push_back({ Mark::Wait, "Escalador: conservado (" + before.scaler + ")", "Recomendado, no aplicado: " + plan.scaler.name });
        else if (plan.changeScaler)
            changes.push_back({ Mark::Wait, "Escalador: " + before.scaler + " → " + plan.scaler.name, "Aplicado, sin verificar" });
        else
            changes.push_back({ Mark::Ok, "Escalador: conservado (" + before.scaler + ")", plan.scaler.why });
        // Calidad y resolución interna
        if (plan.presetStage == Stage::Pending || plan.presetStage == Stage::Restart)
        {
            const unsigned nw = (unsigned) std::lround(before.tw / kPresets[plan.preset].ratio);
            const unsigned nh = (unsigned) std::lround(before.th / kPresets[plan.preset].ratio);
            changes.push_back({ plan.presetStage == Stage::Restart ? Mark::Restart : Mark::Wait,
                                StrFmt("Calidad: %s → %s", kPresets[before.preset].name, kPresets[plan.preset].name),
                                plan.presetStage == Stage::Restart ? "Se aplica al reiniciar el juego" : "Pendiente de que el juego consulte su resolución" });
            changes.push_back({ Mark::Info, "Resolución interna: " + ResText(before.rw, before.rh) + " → " + ResText(nw, nh) + " (prevista)",
                                StrFmt("Salida %s · proporción %.2f", ResText(before.tw, before.th).c_str(), kPresets[plan.preset].ratio) });
        }
        else
            changes.push_back({ Mark::Ok, before.preset >= 0 ? StrFmt("Calidad: %s conservada", kPresets[before.preset].name) : "Calidad: sin cambios",
                                plan.presetWhy });
        // FG
        const std::string fgNow = status.provider == IlionDynamicMfg::Provider::None ? "sin FG" : status.name;
        if (plan.fgStage == Stage::Verified || plan.fgStage == Stage::Applied)
            changes.push_back({ plan.fgStage == Stage::Verified ? Mark::Ok : Mark::Wait,
                                before.multiplier == plan.count + 1 ? StrFmt("FG: %s %dX conservado", fgNow.c_str(), plan.count + 1)
                                                                    : StrFmt("FG: %s %dX fijo", fgNow.c_str(), plan.count + 1),
                                plan.fgStage == Stage::Verified ? "Verificado" : "Aplicado, sin verificar" });
        else if (plan.fgStage == Stage::Restart)
            changes.push_back({ Mark::Restart, StrFmt("FG: XeFG %dX fijo en el próximo inicio", plan.count + 1), "Antes: " + before.fg });
        else
            changes.push_back({ plan.fgStage == Stage::None ? Mark::Ok : Mark::Wait,
                                before.multiplier > 0 ? StrFmt("FG: %s %dX conservado", before.fg.c_str(), before.multiplier) : "FG: " + before.fg + " conservado",
                                plan.fgWhy.empty() ? std::string() : "Motivo: " + plan.fgWhy });
        // Reinicio
        const bool restart = plan.restartFg || plan.restartRatio;
        changes.push_back({ restart ? Mark::Restart : Mark::Ok, restart ? "Reinicio: NECESARIO" : "Reinicio: no necesario",
                            !restart                              ? std::string()
                            : plan.restartFg && plan.restartRatio ? "Frame Generation y resolución interna"
                            : plan.restartFg                      ? "Frame Generation"
                                                                  : "Resolución interna" });

        const bool pending = plan.presetStage == Stage::Pending || plan.fgStage == Stage::Applied || plan.fgStage == Stage::Blocked ||
                             plan.scalerStage == Stage::Applied || judge == -2;
        const std::string where = "ILION\\Perfiles\\" + ExeKey() + ".perfil.ini";
        if (restart)
            Add(Mark::Restart, "Perfil guardado: pendiente de reinicio del juego", where);
        else if (plan.scalerStage == Stage::Failed)
            Add(Mark::Bad, "Perfil guardado con errores", where);
        else if (pending)
            Add(Mark::Wait, "Perfil guardado: hay cambios pendientes", where);
        else
            Add(Mark::Ok, "Optimización completada y verificada", where);
        running = false;
        finished = true;
        break;
    }
    default:
        running = false;
        break;
    }
}

// Cargar un perfil o un punto de restauración, por petición del usuario. external: un punto de restauración, que
// devuelve también el MFG dinámico y el límite tal como estaban
template <typename Ctx> static void ApplyFile(Ctx& ctx, const std::filesystem::path& path, const char* section, const char* what, bool external)
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
    if (const char* game = ini.GetValue("origen", "juego", nullptr); game != nullptr && ExeKey() != game)
    {
        Add(Mark::Bad, std::string(what) + ": es de otro juego", game);
        return;
    }
    if (external)
        Add(Mark::Info, std::string(what) + ": " + ini.GetValue("origen", "procedencia", ""),
            std::string("Guardado ") + ini.GetValue("origen", "fecha", "?") + " · ILION " + ini.GetValue("origen", "ilion", "?"));
    else
        // Cargar un perfil cambia la configuración: antes se protegen los puntos
        for (auto& s : ProtectRestorePoints(ctx.currentFeature))
            steps.push_back(std::move(s));

    const bool withFg = external || !ExternalDynamic();
    std::string scalerKey, model;
    bool restart = false;
    LoadManaged(ini, section, withFg, scalerKey, model, restart);
    if (external)
        LoadExternal(ini, "externo");
    if (withFg)
        StreamlineHooks::requestDlssgOptionsUpdate("Perfiles");
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

    std::string why;
    const bool ratioRestart = RatioNeedsRestart(LoadRule(), &why);
    Add(ratioRestart ? Mark::Restart : Mark::Wait,
        std::string(what) + ": proporción " +
            (config->UpscaleRatioOverrideEnabled.value_or_default() ? StrFmt("%.2f", config->UpscaleRatioOverrideValue.value_or_default())
                                                                    : std::string("del juego")),
        ratioRestart ? "Se aplica al reiniciar el juego (" + why + ")" : std::string("Se aplica cuando el juego vuelva a consultar su resolución interna"));
    if (!withFg)
        Add(Mark::Wait, std::string(what) + ": Frame Generation no cargado", "Hay un MFG dinámico activo (configuración externa); el perfil no lo cambia");
    if (external)
        Add(Mark::Ok, std::string(what) + ": MFG dinámico, objetivo y límite de FPS devueltos a como estaban", std::string());

    config->SaveIni();
    if (restart)
        Add(Mark::Restart, "REINICIO DEL JUEGO NECESARIO", "Cierra y vuelve a iniciar el juego para que los cambios de Frame Generation surtan efecto.");
    else
        Add(Mark::Ok, std::string(what) + ": aplicado", std::string());
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

static void Pilot(Mark mark, float size)
{
    const ImVec2 p = ImGui::GetCursorScreenPos();
    IlionTheme::ShellLed(ImGui::GetWindowDrawList(), ImVec2(p.x + size * 0.5f, p.y + size * 0.5f), size * 0.3f, toneMapColor(MarkColour(mark)),
                         mark != Mark::Info);
    ImGui::Dummy(ImVec2(size, size));
    ImGui::SameLine(0.0f, size * 0.3f);
}

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
    std::string modeName, date, session, preset, fgOutput, fgCount, prevRes, restart;
};

static Saved ReadSaved()
{
    static Saved cached;
    static double at = -10.0;
    if (ImGui::GetTime() - at < 1.0)
        return cached;
    at = ImGui::GetTime();
    cached = {};
    CSimpleIniA ini(true);
    if (!Exists(ProfilePath()) || ini.LoadFile(ProfilePath().c_str()) < 0)
        return cached;
    cached.exists = true;
    cached.modeName = ini.GetValue("perfil", "modo", "");
    cached.date = ini.GetValue("perfil", "fecha", "");
    cached.session = ini.GetValue("perfil", "sesion", "");
    cached.preset = ini.GetValue("perfil", "modo_escalado", "");
    cached.fgOutput = ini.GetValue("perfil", "fg_salida", "");
    cached.fgCount = ini.GetValue("perfil", "fg_multiplicador", "");
    cached.prevRes = ini.GetValue("perfil", "resolucion_anterior", "");
    cached.restart = ini.GetValue("perfil", "reinicio_pendiente", "no");
    return cached;
}

// Las cifras del monitor: se leen en cada fotograma y se muestran cada 500 ms (media de la ventana; el mínimo de FPS
// reales de la ventana colorea la cifra, para que una caída no quede escondida). No toca lo que mide el optimizador
struct Shown
{
    double base = 0.0, baseMin = 0.0, final = 0.0, frameMs = 0.0;
    bool limited = false;
};

static Shown ShownFigures(bool generating, const IlionDynamicMfg::Status& status, const Measure& live)
{
    static Shown shown;
    static double at = -10.0;
    static double sumBase = 0.0, sumFinal = 0.0, minBase = 1.0e9;
    static int samples = 0;
    const double base = live.mean;
    const double final = generating && status.finalFps > 0.0f ? status.finalFps : live.mean;
    if (base > 0.0)
    {
        sumBase += base;
        sumFinal += final;
        minBase = std::min(minBase, base);
        samples++;
    }
    if (ImGui::GetTime() - at >= 0.5)
    {
        at = ImGui::GetTime();
        if (samples > 0)
        {
            shown.base = sumBase / samples;
            shown.baseMin = minBase;
            shown.final = sumFinal / samples;
            shown.frameMs = shown.final > 0.0 ? 1000.0 / shown.final : 0.0;
        }
        else
            shown = {};
        shown.limited = live.limited;
        sumBase = sumFinal = 0.0;
        minBase = 1.0e9;
        samples = 0;
    }
    return shown;
}

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
    const float ratio = upscaling && feature->RenderWidth() > 0 ? (float) feature->TargetWidth() / (float) feature->RenderWidth() : 0.0f;
    const int activePreset = PresetOf(ratio);
    const bool overrideOn = config->UpscaleRatioOverrideEnabled.value_or_default();
    const float overrideValue = config->UpscaleRatioOverrideValue.value_or_default();
    const bool ratioApplied = overrideOn && ratio > 0.0f && std::fabs(ratio - overrideValue) < 0.04f;

    // Reinicios pendientes: los de esta optimización, o los del perfil mientras no estén cargados
    const bool fgWanted = !saved.fgOutput.empty() && saved.fgOutput != "auto" && saved.fgOutput != "NoFG";
    const bool fgLoaded = fgWanted && std::string(magic_enum::enum_name(state.activeFgOutput)) == saved.fgOutput;
    const bool savedThisSession = saved.session == SessionId();
    const bool fgRestart = (plan.restartFg && state.activeFgOutput != FGOutput::XeFG) ||
                           (saved.restart.find("fg") != std::string::npos && fgWanted && !fgLoaded);
    // La proporción: pendiente de reinicio mientras sea de esta sesión (en otra ya se habría leído al arrancar)
    const bool ratioRestart = overrideOn && !ratioApplied && upscaling &&
                              (plan.restartRatio || (savedThisSession && saved.restart.find("proporcion") != std::string::npos));
    const bool restartNeeded = fgRestart || ratioRestart;

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
            Coloured(IlionTheme::ShellPal::Text,
                     fgRestart && ratioRestart ? "Cierra y vuelve a iniciar el juego para que los cambios de Frame Generation y de resolución interna surtan efecto."
                     : fgRestart               ? "Cierra y vuelve a iniciar el juego para que los cambios de Frame Generation surtan efecto."
                                               : "Cierra y vuelve a iniciar el juego para que la nueva resolución interna surta efecto.",
                     true);
        }
        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(2);
        ImGui::Spacing();
    }

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
            const Mark mark = upscaling ? Mark::Ok : current != nullptr ? Mark::Wait : Mark::Bad;
            BlockHead(mark, "ESCALADO", upscaling ? "ACTIVO" : current != nullptr ? "SIN ESCALAR" : "NINGUNO");
            Big(UiFont(), 1.7f, IlionTheme::ShellPal::Text, current != nullptr ? current->name.c_str() : "Sin escalador", true);
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
                Coloured(IlionTheme::ShellPal::Dim, StrFmt("· activa %.2f", ratio).c_str());
            }
            else
                Coloured(IlionTheme::ShellPal::Dim, "El juego no envía escalado ahora", true);

            // Proporción: anterior, recomendada, guardada, activa
            ImGui::Spacing();
            if (overrideOn)
            {
                const RatioOwner owner = OwnerOfOverride();
                const int wanted = PresetOf(overrideValue);
                Row(ratioApplied ? Mark::Ok : ratioRestart ? Mark::Restart : Mark::Wait,
                    StrFmt("Guardada %.2f (%s): %s", overrideValue, wanted >= 0 ? kPresets[wanted].name : "?",
                           ratioApplied ? "verificada" : ratioRestart ? "pendiente de reinicio" : "pendiente de aplicación"),
                    owner == RatioOwner::Perfiles ? "Anulación de Perfiles" : "Anulación externa (no la gestiona el perfil)");
            }
            else
                Row(Mark::Info, "Proporción del juego (sin anulación)", std::string());
            if (finished && before.rw > 0 && plan.preset >= 0 && plan.preset != before.preset)
                Row(Mark::Info, StrFmt("Anterior %s (%s) · recomendada %s", kPresets[before.preset].name, ResText(before.rw, before.rh).c_str(),
                                       kPresets[plan.preset].name),
                    std::string());
            else if (!saved.prevRes.empty() && !savedThisSession && upscaling)
                Row(Mark::Info, "Resolución interna antes del perfil: " + saved.prevRes,
                    "Ahora " + ResText(feature->RenderWidth(), feature->RenderHeight()));
            if (plan.scalerStage == Stage::Failed || plan.scalerStage == Stage::Recommended)
                Row(plan.scalerStage == Stage::Failed ? Mark::Bad : Mark::Wait,
                    (plan.scalerStage == Stage::Failed ? "Fallido: " : "Recomendado: ") + plan.scaler.name,
                    plan.scalerStage == Stage::Failed ? "Usa RESTAURAR SESIÓN" : "No aplicado en este modo");
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
            const Mark mark = generating ? Mark::Ok : fgRestart ? Mark::Restart : none ? Mark::Info : Mark::Wait;
            BlockHead(mark, "FRAME GENERATION", generating ? "ACTIVO" : fgRestart ? "REINICIO" : none ? "DESACTIVADO" : "EN PAUSA");
            Big(UiFont(), 1.7f, IlionTheme::ShellPal::Text, none ? "Sin FG" : status.name, true);
            if (generating && status.multiplier > 0)
            {
                Big(FigureFont(), 1.6f, IlionTheme::InfoBlue, StrFmt("%dX", status.multiplier).c_str());
                ImGui::SameLine();
                Coloured(IlionTheme::ShellPal::Text2, status.appliedSource == IlionDynamicMfg::AppliedSource::Measured ? "medido"
                                                      : status.appliedSource == IlionDynamicMfg::AppliedSource::Own    ? "aplicado"
                                                                                                                       : "pedido");
            }
            ImGui::Spacing();
            if (dynamic)
                Row(Mark::Wait, "Dynamic: configuración externa", dynamicWhat + ". No es una decisión del perfil; el perfil no lo modifica.");
            else if (generating)
                Row(Mark::Ok, "Activo: multiplicador fijo", std::string());
            if (fgWanted && !fgLoaded && saved.restart.find("fg") != std::string::npos)
                Row(Mark::Restart, "Pendiente para el próximo inicio: " + saved.fgOutput + (saved.fgCount.empty() ? "" : " " + saved.fgCount),
                    "Aún no cargado en esta sesión");
            else if (fgWanted && fgLoaded && saved.restart.find("fg") != std::string::npos)
                Row(generating ? Mark::Ok : Mark::Wait, "Del perfil: " + saved.fgOutput + " " + saved.fgCount,
                    generating ? "Cargado y generando: verificado" : "Cargado, todavía sin generar");
            if (plan.fgStage == Stage::Blocked)
                Row(Mark::Wait, "Recomendado: FG fijo, pendiente de tu decisión", "Desactiva el Dynamic en FRAME GEN y vuelve a optimizar");
            else if (plan.fgStage == Stage::Recommended)
                Row(Mark::Bad, "Recomendado: desactivar el FG", plan.fgWhy);
        }
        ImGui::EndTable();
    }
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
    ImGui::Spacing();

    // ---- Rendimiento ----
    const Measure live = MeasureNow();
    const Shown shown = ShownFigures(generating, status, live);
    const double hz = RefreshHz();
    ImGui::PushStyleColor(ImGuiCol_TableBorderStrong, toneMapColor(IlionTheme::ShellPal::Line));
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(font * 0.7f, font * 0.45f));
    if (ImGui::BeginTable("##ProfilesPerf", 4, ImGuiTableFlags_BordersOuter | ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings))
    {
        const auto figure = [&](int column, const char* label, const std::string& value, const ImVec4& colour, const std::string& unit)
        {
            ImGui::TableSetColumnIndex(column);
            ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, ImGui::GetColorU32(toneMapColor(panel)));
            Coloured(IlionTheme::ShellPal::Text2, label);
            Big(FigureFont(), 2.0f, colour, value.c_str());
            Coloured(IlionTheme::ShellPal::Dim, unit.c_str());
        };
        ImGui::TableNextRow();
        figure(0, "FPS REALES", shown.base > 0.0 ? StrFmt("%.0f", shown.base) : "—",
               shown.base > 0.0 ? IlionTheme::FpsBand(shown.baseMin) : IlionTheme::ShellPal::Dim,
               shown.base <= 0.0 ? std::string("sin datos") : shown.limited ? std::string("sujetos por un límite") : StrFmt("mín. %.0f (0,5 s)", shown.baseMin));
        figure(1, "FPS FINALES", shown.final > 0.0 ? StrFmt("%.0f", shown.final) : "—", IlionTheme::ShellPal::Text, generating ? "con FG" : "sin FG");
        figure(2, "MONITOR", hz > 0.0 ? StrFmt("%.0f", hz) : "—", IlionTheme::ShellPal::Text2, "Hz · referencia");
        figure(3, "FRAME TIME", shown.frameMs > 0.0 ? StrFmt("%.1f", shown.frameMs) : "—", IlionTheme::ShellPal::Text, "ms presentados");
        ImGui::EndTable();
    }
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();

    // Objetivo del modo y coste del FG, solo con datos fiables
    {
        const Policy policy = PolicyOf(mode);
        const int judge = Judge(live, policy);
        if (judge == -2)
            Row(Mark::Wait, StrFmt("Objetivo %s (%.0f FPS reales): sin datos fiables", ModeName(mode), policy.base), live.source);
        else if (live.verified)
            Row(judge < 0 ? Mark::Bad : Mark::Ok,
                StrFmt("Objetivo %s (%.0f FPS reales): %s", ModeName(mode), policy.base, judge < 0 ? "NO cumplido" : "cumplido"),
                StrFmt("El 90%% del tiempo a %.0f o más · %.0f%% del tiempo con 60 o más · verificado sin FG", live.p10, live.share60 * 100.0));
        else
            Row(judge < 0 ? Mark::Bad : Mark::Wait,
                StrFmt("Objetivo %s (%.0f FPS reales): %s (estimado)", ModeName(mode), policy.base, judge < 0 ? "NO cumplido" : "probable"),
                "Con FG activo la estabilidad no se puede comprobar");

        const double reference = MfgCostReference();
        if (generating && reference > 0.0 && mfgCost.current > 0.0)
            Row(Mark::Info, StrFmt("Coste del FG: %.0f%% de los FPS reales", std::max(0.0, (1.0 - mfgCost.current / reference) * 100.0)),
                StrFmt("Medido: %.0f sin FG → %.0f con FG", reference, mfgCost.current));
    }

    // ---- Cambios de la última optimización ----
    if (!changes.empty())
    {
        ImGui::Spacing();
        Coloured(IlionTheme::ShellPal::Text2, "ÚLTIMA OPTIMIZACIÓN");
        for (const auto& c : changes)
            Row(c.mark, c.title, c.detail);
    }

    // ---- Registro de operaciones ----
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
    // El modo del perfil guardado; sin perfil, Óptimo
    if (!modeLoaded)
    {
        modeLoaded = true;
        const Saved saved = ReadSaved();
        for (int i = 0; i < (int) Mode::Count; i++)
            if (saved.modeName == ModeName((Mode) i))
                mode = (Mode) i;
    }

    Tick(ctx);

    const auto& state = State::Instance();
    const GpuInformation gpu = IdentifyGpu::getPrimaryGpu();
    const float font = ImGui::GetFontSize();
    const bool columns = ImGui::GetContentRegionAvail().x > font * 40.0f;
    const bool hasProfile = Exists(ProfilePath());
    const bool hasSession = Exists(SessionPath());
    const bool hasOriginal = Exists(OriginalPath());

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
    const float modeWidth = std::floor((ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) / 2.0f);
    ImGui::BeginDisabled(running);
    for (int i = 0; i < (int) Mode::Count; i++)
    {
        if (i % 2 == 1)
            ImGui::SameLine();
        const bool selected = (int) mode == i;
        if (selected)
            ImGui::PushStyleColor(ImGuiCol_Text, toneMapColor(IlionTheme::ShellPilot(IlionTheme::ShellPal::Green)));
        if (IlionTheme::Button(StrFmt("%s##ProfileMode%d", ModeName((Mode) i), i).c_str(), ImVec2(modeWidth, 0.0f)))
            mode = (Mode) i;
        if (selected)
            ImGui::PopStyleColor();
    }
    Coloured(IlionTheme::ShellPal::Dim, PolicyOf(mode).text, true);
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
            SaveProfile(ctx.currentFeature);
            lastMessage = "Perfil guardado con la configuración actual";
        }
    }
    ImGui::BeginDisabled(!hasProfile);
    if (IlionTheme::Button("CARGAR PERFIL##ProfilesLoad", ImVec2(full, 0.0f)))
        ApplyFile(ctx, ProfilePath(), "perfil", "Perfil", false);
    ImGui::EndDisabled();
    ImGui::BeginDisabled(!hasSession);
    if (IlionTheme::Button("RESTAURAR SESIÓN##ProfilesRestoreSession", ImVec2(full, 0.0f)))
        ApplyFile(ctx, SessionPath(), "configuracion", "Restauración de la sesión", true);
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s", "La configuración de antes de la primera optimización de esta sesión del juego");
    ImGui::BeginDisabled(!hasOriginal);
    if (IlionTheme::Button("RESTAURAR ORIGINAL##ProfilesRestoreOriginal", ImVec2(full, 0.0f)))
        ApplyFile(ctx, OriginalPath(), "configuracion", "Restauración original", true);
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s", "La configuración de antes de la primera optimización de Perfiles en este juego");
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
