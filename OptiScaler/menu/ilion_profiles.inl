// ILION — Perfiles inteligentes (prototipo experimental, rama ilion-perfiles-exp). Prueba 6
//
// Prueba 6: el escalador se elige entre todas las alternativas compatibles con la arquitectura (fidelidad de referencia
// + FPS reales medidos en este juego y esta GPU); una alternativa más ligera solo gana con una mejora medida, y a igual
// fidelidad se conserva la que ya corre. Historial por GPU. Aviso de optimización pendiente / aplicada bajo el botón.
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
                 "Máxima fidelidad: el escalador de más calidad compatible (FSR 4 completo, DLSS, XeSS XMX) y la resolución "
                 "interna más alta (Nativa, Ultra Calidad o Calidad) que sostiene unos 60 FPS reales. Nunca baja a un modelo Lite; "
                 "si no llega, lo dice y muestra las opciones. FG 2X fijo como máximo, solo si aporta." };
    case Mode::Equilibrado:
        return { 2, 3, 70.0, 60.0, 90.0, 60.0, 3, 140.0, 165.0,
                 "Compromiso: Calidad o Balanceado, unos 75 FPS reales. Compara el modelo completo con INT8Lite y otras "
                 "alternativas compatibles: el completo salvo una mejora medida del 8 % (o si ningún preset llega). 2X fijo; 3X "
                 "solo si la base medida y el monitor lo justifican." };
    case Mode::Rendimiento:
        return { 3, 4, 55.0, 50.0, 80.0, 50.0, 4, 0.0, 0.0,
                 "Fluidez: Balanceado o Rendimiento, la mitad de los Hz del monitor en FPS reales (82 como mínimo; 55 es solo el "
                 "suelo). INT8Lite primero; otra alternativa (Lite comunitaria, HLX, HelixSR, XeSS) solo si rinde un 5 % más "
                 "medido. Multiplicador fijo hasta 4X si el proveedor lo admite y la base lo sostiene." };
    default:
        return { 0, 2, 70.0, 60.0, 91.0, 60.0, 2, 120.0, 144.0,
                 "Modo predeterminado: imagen excelente y fluidez alta. El escalador de más fidelidad compatible (FSR 4 completo "
                 "en Radeon, DLSS en RTX), Nativa, Ultra Calidad o Calidad para unos 70 FPS reales; FG 2X fijo para 120-144 "
                 "finales cuando el monitor lo permita. No baja a Lite por una estimación." };
    }
}

// Los FPS reales que cada modo busca al elegir preset (no el suelo: Rendimiento busca la mitad de los Hz, para que 2X
// los alcance, y nunca menos de 82)
static double WantFps(Mode m, double hz)
{
    switch (m)
    {
    case Mode::Calidad:
        return 60.0;
    case Mode::Equilibrado:
        return 75.0;
    case Mode::Rendimiento:
        return std::max(82.0, hz > 0.0 ? hz * 0.5 : 90.0);
    default:
        return 70.0;
    }
}

// FPS reales en otro preset a partir de los medidos: proporcionales a los píxeles internos elevados a 0,7. Supone que
// limita la GPU; si limita la CPU la ganancia real es menor. Siempre se presenta como estimación, y lo medido en este
// juego (historial) la sustituye
constexpr double kPixelExponent = 0.7;
static double EstimateAt(double fps, int from, int to)
{
    const double r = kPresets[to].ratio / kPresets[from].ratio;
    return fps * std::pow(r * r, kPixelExponent);
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
    GameMenu,    // el preset se elige en el menú gráfico del juego (cambio inmediato); ILION lo verifica
};

// El modelo FSR 4 que se pide: el completo que FFX elige para la GPU (FP8 en RDNA 4, INT8 en RDNA 3, INT8 RDNA2) o
// INT8Lite, el del selector de modelos. No es el preset de resolución, y no es una DLL Lite comunitaria
enum class Model
{
    Any,  // no es FSR 4: no hay modelo que elegir
    Full, // el modelo completo de la GPU (Fsr4Model automático)
    Lite, // INT8Lite del selector
};

struct Choice
{
    std::string key;
    std::string name;
    Model model = Model::Any;
    std::string why;
    int tier = 0;        // fidelidad de referencia de la tecnología (no medida en este juego): 5 la más alta
    std::string variant; // nombre del escalador sin el modelo
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
    std::string scalerKey;
    Model model = Model::Any;
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
    bool viaMenu = false; // el preset se cambia desde el menú del juego (tabla de calidades leída al arrancar)
    bool belowWant = false; // ningún preset del modo llega a lo que el modo busca
    std::string scalerEval;  // qué se comparó para elegir el escalador
    std::string fgAlt;       // otra ruta de FG que convendría (no se aplica sola)
    // El estado del FG cuando se decidió: una recomendación deja de valer si cambia
    bool fgEvalGenerating = false;
    IlionDynamicMfg::Provider fgEvalProvider = IlionDynamicMfg::Provider::None;
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
static bool modelChanged = false; // el escalador elegido ya corría; se cambió solo su modelo FSR 4
static Plan plan;
static Measure measure;
static Before before;
static std::string lastMessage;
static std::vector<std::string> resEvents; // cambios de resolución interna vistos en esta sesión del juego
static int optJudge = -2;                  // la base frente al objetivo en la última optimización (-2: sin datos)

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

static bool IsFsr4Full(const UpscalerVariant& v)
{
    return v.entry != nullptr && v.entry->family == IlionUpscalers::Family::Fsr4 && !IsCommunityLite(v);
}

// El modelo pedido para un FSR 4 del selector: Lite si está seleccionado INT8Lite, si no el completo
static Model SelectedModel() { return DriverLiteSelected() ? Model::Lite : Model::Full; }

// ---- Arquitectura: capacidades comprobables, no solo la marca ----------------------------------------------------------
enum class ArchFamily
{
    Unknown,
    Rdna4,
    Rdna3,
    Rdna2,
    AmdApu,   // GPU cuyo FSR 4 propio es INT8Lite (APU, Navi 12/14)
    AmdOther, // RDNA 1 y anteriores: sin FSR 4
    NvGtx,    // sin DLSS (GTX, GTX 16)
    Rtx20,
    Rtx30,
    Rtx40,
    Rtx50,
    ArcA,     // Alchemist (XMX)
    ArcB,     // Battlemage (XMX)
    IntelXe2, // Xe2 integrada (XMX)
    IntelOther,
};

struct Arch
{
    ArchFamily family = ArchFamily::Unknown;
    const char* name = "Desconocida";
    bool fsr4 = false;         // la GPU ejecuta algún modelo FSR 4 (fsr4Support de IdentifyGpu)
    const char* fsr4Full = ""; // el modelo completo que FFX elige para ella
    bool liteIsFull = false;   // su modelo propio ya es INT8Lite
    bool dlssSR = false;       // NGX dice que admite DLSS SR
    bool dlssFG = false;       // DLSSG nativo (RTX 40 o posterior)
    bool dlssMFG = false;      // DLSSG con más de un fotograma generado (RTX 50)
    bool xessXmx = false;      // XeSS con XMX (Arc y Xe2); el resto usa DP4a
    bool xefgTested = false;   // XeFG por ILION comprobado en esta familia (RDNA 3) u oficial (Intel con XMX)
};

static Arch ArchOf(const GpuInformation& gpu)
{
    Arch a;
    using HW = device_info::HwGeneration;
    if (gpu.vendorId == VendorId::AMD)
    {
        a.fsr4 = gpu.fsr4Support != FSR4Support::None;
        a.fsr4Full = gpu.fsr4Support == FSR4Support::FP8      ? "FP8"
                     : gpu.fsr4Support == FSR4Support::INT8   ? "INT8"
                     : gpu.fsr4Support == FSR4Support::INT8_2 ? "INT8 RDNA2"
                     : gpu.fsr4Support == FSR4Support::INT8Lite ? "INT8 Lite"
                                                                : "";
        a.liteIsFull = gpu.fsr4Support == FSR4Support::INT8Lite;
        if (a.liteIsFull)
            a = { ArchFamily::AmdApu, "AMD (APU / bajo consumo)", a.fsr4, a.fsr4Full, true };
        else if (gpu.amdHwGeneration == HW::kGfx12)
            a.family = ArchFamily::Rdna4, a.name = "RDNA 4";
        else if (gpu.amdHwGeneration == HW::kGfx11)
            a.family = ArchFamily::Rdna3, a.name = "RDNA 3", a.xefgTested = true;
        else if (gpu.amdHwGeneration == HW::kGfx10_3)
            a.family = ArchFamily::Rdna2, a.name = "RDNA 2";
        else
            a.family = ArchFamily::AmdOther, a.name = gpu.amdHwGeneration == HW::kGfx10 ? "RDNA 1" : "AMD (anterior a RDNA 2)";
    }
    else if (gpu.vendorId == VendorId::Nvidia)
    {
        const auto id = gpu.nvidiaArchInfo.architecture_id;
        a.dlssSR = gpu.dlssCapable;
        if (!gpu.dlssCapable)
            a.family = ArchFamily::NvGtx, a.name = "NVIDIA GTX (sin DLSS)";
        else if (id >= NV_GPU_ARCHITECTURE_GB200)
            a.family = ArchFamily::Rtx50, a.name = "NVIDIA RTX 50", a.dlssFG = true, a.dlssMFG = true;
        else if (id >= NV_GPU_ARCHITECTURE_AD100)
            a.family = ArchFamily::Rtx40, a.name = "NVIDIA RTX 40", a.dlssFG = true;
        else if (id >= NV_GPU_ARCHITECTURE_GA100)
            a.family = ArchFamily::Rtx30, a.name = "NVIDIA RTX 30";
        else
            a.family = ArchFamily::Rtx20, a.name = id >= NV_GPU_ARCHITECTURE_TU100 ? "NVIDIA RTX 20" : "NVIDIA RTX (generación sin identificar)";
    }
    else if (gpu.vendorId == VendorId::Intel)
    {
        // Por el identificador de dispositivo: 0x56xx Alchemist, 0xE2xx Battlemage, 0x64xx Lunar Lake (Xe2)
        const uint32_t series = gpu.deviceId & 0xFF00;
        if (series == 0x5600)
            a.family = ArchFamily::ArcA, a.name = "Intel Arc A (Alchemist)", a.xessXmx = true, a.xefgTested = true;
        else if (series == 0xE200)
            a.family = ArchFamily::ArcB, a.name = "Intel Arc B (Battlemage)", a.xessXmx = true, a.xefgTested = true;
        else if (series == 0x6400)
            a.family = ArchFamily::IntelXe2, a.name = "Intel Xe2 integrada", a.xessXmx = true, a.xefgTested = true;
        else
            a.family = ArchFamily::IntelOther, a.name = "Intel (sin XMX)";
    }
    return a;
}

static std::string ArchSummary(const Arch& a)
{
    std::string s = a.name;
    if (a.fsr4)
        s += StrFmt(" · FSR 4 completo: %s", a.fsr4Full);
    if (a.dlssSR)
        s += a.dlssMFG ? " · DLSS SR, DLSSG hasta 4X" : a.dlssFG ? " · DLSS SR, DLSSG 2X" : " · DLSS SR, sin DLSSG";
    if (a.family == ArchFamily::ArcA || a.family == ArchFamily::ArcB || a.family == ArchFamily::IntelXe2)
        s += " · XeSS XMX";
    if (a.xefgTested)
        s += " · XeFG fijo comprobado";
    return s;
}

// ---- Historial de evidencias: FPS reales medidos sin FG por combinación, en este juego ----------------------------------
// Solo mediciones verificadas (sin FG, sin límite, 120 fotogramas o más). Las escenas no se controlan: una combinación
// se compara con otra solo con dos mediciones o más de cada una, en el mismo preset y la misma salida
struct Evidence
{
    int count = 0;
    double mean = 0.0, p10 = 0.0;
};

// Uno por GPU (fabricante-dispositivo): una medición de otra GPU nunca se toma por local. El historial de las Pruebas 4
// y 5 (<exe>.historial.ini, sin GPU) se copia una vez al de la GPU actual, la única con la que se probaron; el original
// se conserva
static std::string GpuTag()
{
    const GpuInformation gpu = IdentifyGpu::getPrimaryGpu();
    return StrFmt("%04x-%04x", (unsigned) gpu.vendorId, (unsigned) gpu.deviceId);
}

static std::filesystem::path HistoryPath()
{
    const auto path = FileOf(L".historial." + string_to_wstring(GpuTag()) + L".ini");
    static bool migrated = false;
    if (!migrated)
    {
        migrated = true;
        const auto legacy = FileOf(L".historial.ini");
        std::error_code ec;
        if (!std::filesystem::exists(path, ec) && std::filesystem::exists(legacy, ec))
            std::filesystem::copy_file(legacy, path, ec);
    }
    return path;
}

static std::string EvidenceKey(const std::string& scaler, Model model, int preset, unsigned outHeight)
{
    return StrFmt("%s|%s|%s|%u", scaler.c_str(), model == Model::Lite ? "INT8Lite" : model == Model::Full ? "completo" : "-",
                  preset >= 0 ? kPresets[preset].name : "?", outHeight);
}

static Evidence LoadEvidence(const std::string& key)
{
    Evidence e;
    CSimpleIniA ini(true);
    if (!Exists(HistoryPath()) || ini.LoadFile(HistoryPath().c_str()) < 0)
        return e;
    if (const char* v = ini.GetValue("historial", key.c_str(), nullptr); v != nullptr)
        sscanf_s(v, "%d;%lf;%lf", &e.count, &e.mean, &e.p10);
    return e;
}

// Media acumulada; a partir de cinco mediciones pesa como cinco, para que una escena nueva cuente
static Evidence RecordEvidence(const std::string& key, double mean, double p10)
{
    CSimpleIniA ini(true);
    ini.LoadFile(HistoryPath().c_str());
    Evidence e = LoadEvidence(key);
    const int n = std::min(e.count, 4);
    e.mean = (e.mean * n + mean) / (n + 1);
    e.p10 = (e.p10 * n + p10) / (n + 1);
    e.count++;
    ini.SetValue("historial", key.c_str(), StrFmt("%d;%.1f;%.1f;%s", e.count, e.mean, e.p10, NowText().c_str()).c_str());
    const GpuInformation gpu = IdentifyGpu::getPrimaryGpu();
    ini.SetValue("equipo", "gpu", gpu.name.c_str());
    ini.SetValue("equipo", "id", GpuTag().c_str());
    ini.SetValue("equipo", "arquitectura", ArchOf(gpu).name);
    ini.SetValue("equipo", "api", std::string(magic_enum::enum_name(State::Instance().api)).c_str());
    std::error_code ec;
    std::filesystem::create_directories(Folder(), ec);
    ini.SaveFile(HistoryPath().c_str());
    return e;
}

// ---- Escaladores: alternativas compatibles y decisión ----------------------------------------------------------------
// Cada alternativa con su fidelidad de referencia (orden de la tecnología, no medido en este juego) y lo medido en este
// juego, esta GPU, este preset y esta salida (historial):
//   5  FSR 4 modelo completo (driver o DLL), DLSS SR nativo en RTX, XeSS con XMX en Intel Arc / Xe2
//   4  HLX FSR 4, HelixSR (red del modelo E de DLSS por FFX): fidelidad sin clasificar frente al FSR 4 completo
//   3  INT8Lite del driver, FSR 4 Lite comunitaria
//   2  XeSS DP4a        1  FSR 3.1        0  FSR 2.2
// d4r (DLSS por backend externo) no entra: necesita reiniciar y es experimental. Una DLL comunitaria fuera de RDNA 3
// (donde ILION la comprobó) solo entra con mediciones propias: que el archivo exista no la hace compatible
struct Option
{
    Choice choice;
    Evidence e;                 // medido sin FG en este juego, esta GPU, este preset y esta salida
    bool current = false;       // es lo que corre ahora (escalador y modelo)
    bool needsEvidence = false; // solo se elige con mediciones propias (dos o más)
};

static std::vector<Option> Alternatives(const UpscalerCatalog& catalog, const Arch& arch, int preset, unsigned outHeight,
                                        std::string* excluded)
{
    std::vector<Option> out;
    const UpscalerVariant* cur = Current(catalog);
    const bool liteSelected = DriverLiteSelected();
    const bool rdna3 = arch.family == ArchFamily::Rdna3;
    const auto add = [&](const UpscalerVariant* v, Model model, int tier, bool needsEvidence, const std::string& why)
    {
        if (v == nullptr || !v->usable)
            return;
        for (const auto& o : out)
            if (o.choice.key == KeyOf(*v) && o.choice.model == model)
                return;
        Option o;
        const std::string modelName = model == Model::Lite ? " · modelo INT8Lite"
                                      : model == Model::Full && arch.fsr4 ? StrFmt(" · modelo completo (%s)", arch.fsr4Full)
                                                                          : std::string();
        o.choice = { KeyOf(*v), v->name + modelName, model, why, tier, v->name };
        o.e = LoadEvidence(EvidenceKey(o.choice.key, model, preset, outHeight));
        o.current = cur != nullptr && KeyOf(*cur) == o.choice.key && (model == Model::Any || (model == Model::Lite) == liteSelected);
        o.needsEvidence = needsEvidence;
        out.push_back(o);
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
    if (arch.fsr4)
    {
        add(driver, Model::Full, 5, false, StrFmt("FSR 4 del driver AMD, modelo completo %s para %s", arch.fsr4Full, arch.name));
        // INT8Lite del driver: en una GPU cuyo modelo propio ya es INT8Lite no hay elección
        if (driver != nullptr && DriverLiteOffered() && !arch.liteIsFull)
            add(driver, Model::Lite, 3, false, "INT8Lite del driver: modelo más ligero, menos fidelidad");
        for (const auto& v : catalog.variants)
            if (IsFsr4Full(v) && !IsDriver(v))
                add(&v, Model::Full, 5, false, StrFmt("FSR 4 por DLL, modelo completo %s", arch.fsr4Full));
    }
    if (arch.dlssSR)
        add(backend(Upscaler::DLSS), Model::Any, 5, false, StrFmt("DLSS Super Resolution nativo (%s)", arch.name));
    const bool amd = arch.family == ArchFamily::Rdna4 || arch.family == ArchFamily::Rdna3 || arch.family == ArchFamily::Rdna2 ||
                     arch.family == ArchFamily::AmdApu || arch.family == ArchFamily::AmdOther;
    if (amd)
        for (const auto& v : catalog.variants)
            if (IsCommunityLite(v))
                add(&v, Model::Any, 3, !rdna3, "FSR 4 Lite comunitaria (DLL independiente, menos fidelidad)");
    for (const auto& v : catalog.variants)
        if (IsHlx(v))
            add(&v, Model::Any, 4, !rdna3, "HLX FSR 4 (fidelidad sin clasificar frente al FSR 4 completo)");
    for (const auto& v : catalog.variants)
        if (v.entry != nullptr && v.entry->family == IlionUpscalers::Family::HelixSR && !v.entry->hlx)
            add(&v, Model::Any, 4, !rdna3, "HelixSR: red del modelo E de DLSS por FFX (fidelidad sin clasificar)");
    add(backend(Upscaler::XeSS), Model::Any, arch.xessXmx ? 5 : 2, false,
        arch.xessXmx ? StrFmt("XeSS con XMX (%s)", arch.name) : std::string("XeSS DP4a"));
    add(first([](const UpscalerVariant& v) { return v.family == FamFsr31; }), Model::Any, 1, false, "FSR 3.1");
    add(backend(Upscaler::FSR22), Model::Any, 0, false, "FSR 2.2");

    // Lo que no entra, con su motivo (las cinco primeras)
    if (excluded != nullptr)
    {
        int n = 0;
        for (const auto& v : catalog.variants)
        {
            const bool d4r = v.entry != nullptr && v.entry->kind == IlionUpscalers::Kind::D4r;
            const bool listed = std::any_of(out.begin(), out.end(), [&](const Option& o) { return o.choice.key == KeyOf(v); });
            if (listed || (!d4r && v.usable && (v.family == FamFsr2 || v.family == FamFfx)))
                continue;
            if (n++ < 5)
                *excluded += (excluded->empty() ? "" : " · ") + v.name + " (" +
                             (d4r          ? std::string("necesita reiniciar; experimental")
                              : !v.usable  ? v.reason
                              : v.isDefault ? std::string("DLL por defecto: su tecnología no se identifica, no se compara")
                                            : std::string("sin validar en esta GPU")) +
                             ")";
        }
    }
    return out;
}

static std::string OptionsText(const std::vector<Option>& options)
{
    std::string s;
    for (const auto& o : options)
        s += StrFmt("%s%s%s: %s%s", s.empty() ? "" : " · ", o.choice.name.c_str(), o.current ? " (activo)" : "",
                    o.e.count > 0 ? StrFmt("%.0f FPS reales medidos (%d)", o.e.p10, o.e.count).c_str() : "sin medir",
                    o.needsEvidence && o.e.count < 2 ? ", sin validar aquí" : "");
    return s;
}

// La decisión, según el modo. top: la de más fidelidad; a igual fidelidad la que ya corre (no se cambia por cambiar).
// Óptimo y Calidad: siempre top. Equilibrado y Rendimiento: una más ligera solo con una mejora medida (8 % / 5 %, el
// 90 % del tiempo, dos mediciones o más de cada una en el mismo preset); sin medir, INT8Lite en Rendimiento (y en
// Equilibrado si ningún preset llega), como en las Pruebas 4 y 5
static Choice Decide(const std::vector<Option>& options, Mode m, bool insufficient)
{
    const Option* top = nullptr;
    for (const auto& o : options)
    {
        if (o.needsEvidence && o.e.count < 2)
            continue;
        if (top == nullptr || o.choice.tier > top->choice.tier || (o.choice.tier == top->choice.tier && o.current && !top->current))
            top = &o;
    }
    if (top == nullptr)
        return {};
    Choice pick = top->choice;
    const auto gainOf = [&](const Option& o) -> std::optional<double>
    {
        if (o.e.count < 2 || top->e.count < 2 || top->e.p10 <= 0.0)
            return std::nullopt;
        return o.e.p10 / top->e.p10 - 1.0;
    };
    const std::string kept = top->current ? " · ya activo: se mantiene" : "";

    if (m == Mode::Calidad || m == Mode::Optimo)
    {
        pick.why = StrFmt("%s: la de más fidelidad compatible. %s%s", ModeName(m), top->choice.why.c_str(), kept.c_str());
        return pick;
    }

    const double need = m == Mode::Equilibrado ? 0.08 : 0.05;
    const Option* best = nullptr;
    double bestGain = 0.0;
    for (const auto& o : options)
    {
        if (&o == top || o.choice.tier >= top->choice.tier || o.choice.tier < (m == Mode::Equilibrado ? 3 : 2))
            continue;
        const auto gain = gainOf(o);
        if (!gain.has_value() || *gain < need)
            continue;
        if (best == nullptr || *gain > bestGain + 0.02 || (std::fabs(*gain - bestGain) <= 0.02 && o.current && !best->current))
            best = &o, bestGain = *gain;
    }
    if (best != nullptr)
    {
        pick = best->choice;
        pick.why = StrFmt("%s: %s rinde un %.0f%% más que %s, medido en este juego (%.0f frente a %.0f FPS reales el 90 %% del "
                          "tiempo; %d y %d mediciones)",
                          ModeName(m), best->choice.name.c_str(), bestGain * 100.0, top->choice.name.c_str(), best->e.p10, top->e.p10,
                          best->e.count, top->e.count);
        return pick;
    }

    const Option* lite = nullptr;
    for (const auto& o : options)
        if (o.choice.model == Model::Lite)
            lite = &o;
    if (lite != nullptr && !gainOf(*lite).has_value() && (m == Mode::Rendimiento || insufficient))
    {
        pick = lite->choice;
        pick.why = m == Mode::Rendimiento ? "Rendimiento: INT8Lite es la primera opción (mejora sin medir en este juego)"
                                          : "Equilibrado: ningún preset llega con el modelo completo; INT8Lite (mejora sin medir)";
        return pick;
    }
    bool measuredAny = false;
    for (const auto& o : options)
        measuredAny = measuredAny || (&o != top && o.choice.tier < top->choice.tier && gainOf(o).has_value());
    pick.why = StrFmt("%s: %s. %s%s", ModeName(m), top->choice.why.c_str(),
                      measuredAny ? "Las alternativas medidas no mejoran lo suficiente"
                                  : "Ninguna alternativa más ligera tiene una mejora medida en este juego",
                      kept.c_str());
    return pick;
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
    ini.SetValue(section, "tabla_activa", OptStr(config->QualityRatioOverrideEnabled).c_str());
    ini.SetValue(section, "tabla_dlaa", OptStr(config->QualityRatio_DLAA).c_str());
    ini.SetValue(section, "tabla_ultra_calidad", OptStr(config->QualityRatio_UltraQuality).c_str());
    ini.SetValue(section, "tabla_calidad", OptStr(config->QualityRatio_Quality).c_str());
    ini.SetValue(section, "tabla_balanceado", OptStr(config->QualityRatio_Balanced).c_str());
    ini.SetValue(section, "tabla_rendimiento", OptStr(config->QualityRatio_Performance).c_str());
    ini.SetValue(section, "tabla_ultra_rendimiento", OptStr(config->QualityRatio_UltraPerformance).c_str());
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
    SetFrom(config->QualityRatioOverrideEnabled, ini.GetValue(section, "tabla_activa", nullptr), ParseBool);
    SetFrom(config->QualityRatio_DLAA, ini.GetValue(section, "tabla_dlaa", nullptr), ParseFloat);
    SetFrom(config->QualityRatio_UltraQuality, ini.GetValue(section, "tabla_ultra_calidad", nullptr), ParseFloat);
    SetFrom(config->QualityRatio_Quality, ini.GetValue(section, "tabla_calidad", nullptr), ParseFloat);
    SetFrom(config->QualityRatio_Balanced, ini.GetValue(section, "tabla_balanceado", nullptr), ParseFloat);
    SetFrom(config->QualityRatio_Performance, ini.GetValue(section, "tabla_rendimiento", nullptr), ParseFloat);
    SetFrom(config->QualityRatio_UltraPerformance, ini.GetValue(section, "tabla_ultra_rendimiento", nullptr), ParseFloat);
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

// Lo que el juego leyó al arrancar: la configuración de proporción del punto de esta sesión, guardado antes del primer
// cambio de Perfiles en ella. Si la actual es distinta, un juego que lee su tabla al arrancar aún no la conoce
static bool RatioConfigChangedThisSession()
{
    CSimpleIniA s(true);
    if (!Exists(SessionPath()) || s.LoadFile(SessionPath().c_str()) < 0 || SessionId() != s.GetValue("origen", "sesion", ""))
        return false;
    auto c = Config::Instance();
    const auto differs = [&](const char* key, const std::string& now)
    {
        const char* v = s.GetValue("configuracion", key, nullptr);
        return v != nullptr && now != v;
    };
    return differs("ratio_activo", OptStr(c->UpscaleRatioOverrideEnabled)) ||
           (c->UpscaleRatioOverrideEnabled.value_or_default() && differs("ratio", OptStr(c->UpscaleRatioOverrideValue))) ||
           differs("tabla_activa", OptStr(c->QualityRatioOverrideEnabled)) || differs("tabla_dlaa", OptStr(c->QualityRatio_DLAA)) ||
           differs("tabla_ultra_calidad", OptStr(c->QualityRatio_UltraQuality)) || differs("tabla_calidad", OptStr(c->QualityRatio_Quality)) ||
           differs("tabla_balanceado", OptStr(c->QualityRatio_Balanced)) || differs("tabla_rendimiento", OptStr(c->QualityRatio_Performance)) ||
           differs("tabla_ultra_rendimiento", OptStr(c->QualityRatio_UltraPerformance));
}

// La fila del menú gráfico del juego que corresponde a cada modo de escalado de ILION
static const char* GameRowName(int preset)
{
    static const char* names[] = { "DLAA / Nativa", "Ultra Calidad (Ultra Quality)", "Calidad (Quality)", "Equilibrado (Balanced)",
                                   "Rendimiento (Performance)", "Ultra Rendimiento (Ultra Performance)" };
    return preset >= 0 && preset < kPresetCount ? names[preset] : "?";
}

static int PresetByName(const std::string& name)
{
    for (int i = 0; i < kPresetCount; i++)
        if (name == kPresets[i].name)
            return i;
    return -1;
}

// El modelo FSR 4: pedido (configuración), determinado por FFX (sus comprobaciones de soporte al crear el contexto, como
// el «modelo determinado» del menú de escalado) y si coinciden. No es una lectura de lo que ejecuta la GPU
struct ModelState
{
    Mark mark = Mark::Info;
    std::string title, detail;
    bool verified = false;
};

static ModelState ModelNow(IFeature* feature)
{
    auto config = Config::Instance();
    const Arch arch = ArchOf(IdentifyGpu::getPrimaryGpu());
    const bool runs = feature != nullptr && feature->IsInited() && feature->Version().major >= 4 &&
                      (feature->GetUpscalerType() == Upscaler::FFX || feature->GetUpscalerType() == Upscaler::FFX_on12);
    const auto determined = runs ? FSR4Models::ActiveModel(FfxApiProxy::Dx12Module_SR()) : std::nullopt;
    const std::string asked = config->Fsr4Model.has_value() ? std::string(FSR4Models::Name(config->Fsr4Model.value()))
                                                            : StrFmt("completo (%s, automático)", arch.fsr4Full);
    ModelState s;
    if (!determined.has_value())
    {
        s = { Mark::Info, "Modelo pedido: " + asked, runs ? "FFX aún no ha determinado el modelo" : "FSR 4 no se está ejecutando ahora" };
        return s;
    }
    s.verified = config->Fsr4Model.has_value() ? *determined == config->Fsr4Model.value() : (*determined != FSR4Model::INT8Lite || arch.liteIsFull);
    s.mark = s.verified ? Mark::Ok : Mark::Wait;
    s.title = StrFmt("Modelo %s: %s", s.verified ? "verificado" : "determinado", FSR4Models::Name(*determined));
    s.detail = "Pedido: " + asked + " · determinado por FFX al crear el contexto" + (s.verified ? "" : " · NO coincide con lo pedido");
    return s;
}

// ---- Detección y medición ------------------------------------------------------------------------------------------
static std::string ArchName(const GpuInformation& gpu) { return ArchOf(gpu).name; }

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

// FPS reales con FG: una muestra por fotograma del menú, los últimos 4 s. Se vacía cuando el FG deja de generar
struct BaseSample
{
    double t, fps;
};
static std::deque<BaseSample> fgBase;

static void SampleFgBase()
{
    const auto status = IlionDynamicMfg::Get();
    const double now = ImGui::GetTime();
    if (!Generating(status) || status.baseFps <= 0.0f)
    {
        fgBase.clear();
        return;
    }
    fgBase.push_back({ now, status.baseFps });
    while (!fgBase.empty() && now - fgBase.front().t > 4.0)
        fgBase.pop_front();
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
        m.limited = status.limitFps > 0.0f || IlionDynamicMfg::DynamicEnabled() || status.backendDynamic;
        // La media de los últimos segundos (2 s como mínimo), nunca una muestra suelta: con XeFG una muestra puede ser
        // un solo fotograma
        m.frames = (int) fgBase.size();
        m.seconds = fgBase.size() > 1 ? fgBase.back().t - fgBase.front().t : 0.0;
        if (m.seconds >= 2.0)
        {
            double sum = 0.0;
            for (const auto& b : fgBase)
                sum += b.fps;
            m.mean = sum / fgBase.size();
        }
        m.source = m.limited             ? StrFmt("con FG y un límite activo (%.0f FPS finales): los FPS reales los fija el límite", status.limitFps)
                   : m.seconds < 2.0     ? StrFmt("pendiente de medición: %.1f de 2 s de FPS reales con FG", m.seconds)
                   : status.baseMeasured ? StrFmt("media de %.1f s de fotogramas reales con FG; la estabilidad no se mide con FG", m.seconds)
                                         : StrFmt("media de %.1f s, estimada a partir de los FPS finales", m.seconds);
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

// El veredicto que muestra el monitor: un estado nuevo se muestra tras 2 s seguidos, para que la fila no parpadee
static int StableJudge(int raw)
{
    static int shown = -3, candidate = -3;
    static double since = 0.0;
    const double now = ImGui::GetTime();
    if (shown == -3)
        shown = candidate = raw;
    if (raw == shown)
    {
        candidate = shown;
        return shown;
    }
    if (raw != candidate)
    {
        candidate = raw;
        since = now;
    }
    if (now - since >= 2.0)
        shown = candidate;
    return shown;
}
static Step& Last() { return steps.back(); }

static void Begin()
{
    steps.clear();
    changes.clear();
    plan = {};
    measure = {};
    before = {};
    litePending = false;
    optJudge = -2;
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
    ini.SetValue("perfil", "ruta_proporcion", plan.viaMenu ? "menu" : "consulta");
    if (!plan.scaler.why.empty())
        ini.SetValue("perfil", "motivo_escalador", plan.scaler.why.c_str());
    if (!plan.presetWhy.empty())
        ini.SetValue("perfil", "motivo_preset", plan.presetWhy.c_str());
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
    int& judge = optJudge;

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
            StrFmt("%s · %.0f GB · monitor %s%s", ArchSummary(ArchOf(gpu)).c_str(), gpu.dedicatedVramInBytes / (1024.0 * 1024.0 * 1024.0),
                   hz > 0.0 ? StrFmt("%.0f Hz", hz).c_str() : "desconocido",
                   state.api == API::DX12 ? "" : " · Perfiles solo está validado en DirectX 12"));

        upscaling = feature != nullptr && feature->IsInited();
        const auto catalog = GatherUpscalers(feature);
        const UpscalerVariant* current = Current(catalog);
        const auto status = IlionDynamicMfg::Get();
        before.scaler = current != nullptr ? current->name + (IsFsr4Full(*current) && DriverLiteSelected() ? " · modelo INT8Lite" : "") : "ninguno";
        before.scalerKey = current != nullptr ? KeyOf(*current) : "";
        before.model = current != nullptr && IsFsr4Full(*current) ? SelectedModel() : Model::Any;
        // DLSS en NVIDIA: la versión efectiva de la DLL que ejecuta, sin deducir más de ella
        if (upscaling && feature->GetUpscalerType() == Upscaler::DLSS)
            Add(Mark::Info, StrFmt("DLSS Super Resolution en ejecución: versión %d.%d.%d", feature->Version().major, feature->Version().minor,
                                   feature->Version().patch),
                "La generación del modelo (DLSS 4 / 4.5) depende de esa DLL y de su preset; Perfiles no la cambia ni la deduce del nombre");
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
        // Con FG hacen falta 2 s de FPS reales: se espera hasta 3 s, sin cambiar nada
        if (measure.withFg && !measure.limited && measure.mean <= 0.0 && elapsed < 3.0)
        {
            if (!entered)
            {
                entered = true;
                Add(Mark::Wait, "Midiendo FPS reales con FG...", "Hacen falta 2 s de fotogramas reales");
            }
            break;
        }
        if (entered)
            steps.pop_back(); // la fila «Midiendo...»
        judge = Judge(measure, policy);
        if (measure.verified && !before.scalerKey.empty() && before.preset >= 0)
        {
            const std::string key = EvidenceKey(before.scalerKey, before.model, before.preset, before.th);
            const Evidence e = RecordEvidence(key, measure.mean, measure.p10);
            Add(Mark::Info, StrFmt("Historial: medición registrada (%d de esta combinación)", e.count),
                key + StrFmt(" · media acumulada %.0f, el 90%% del tiempo a %.0f", e.mean, e.p10));
        }
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
        if (judge == -2)
            plan.presetWhy = "Pendiente de medición: sin FPS reales fiables no se cambia el preset";
        else
        {
            // El preset de más calidad del modo que llega a lo que el modo busca: lo medido en este juego si lo hay (historial,
            // mismo escalador, modelo y salida), si no una estimación por píxeles internos. Si ninguno llega, el más rápido
            const double want = WantFps(mode, RefreshHz());
            const double ref = measure.verified ? measure.p10 : measure.mean;
            std::string table;
            target = -1;
            for (int p = policy.best; p <= policy.fastest; p++)
            {
                bool measured = p == cur;
                double fps = p == cur ? ref : EstimateAt(ref, cur, p);
                if (p != cur && measure.verified)
                    if (const Evidence e = LoadEvidence(EvidenceKey(before.scalerKey, before.model, p, before.th)); e.count >= 1)
                        measured = true, fps = e.p10;
                table += StrFmt("%s%s %.0f%s", table.empty() ? "" : " · ", kPresets[p].name, fps, measured ? " (medido)" : " (est.)");
                if (target < 0 && fps >= want)
                {
                    target = p;
                    plan.presetWhy = StrFmt("%s es el preset de más calidad del modo %s que llega a %.0f FPS reales (%s: %.0f)",
                                            kPresets[p].name, ModeName(mode), want, measured ? "medido" : "estimado", fps);
                }
            }
            if (target < 0)
            {
                target = policy.fastest;
                plan.belowWant = true;
                plan.presetWhy = StrFmt("Ningún preset del modo %s llega a %.0f FPS reales: se usa el más rápido (%s)", ModeName(mode), want,
                                        kPresets[target].name);
            }
            Add(Mark::Info, StrFmt("FPS reales por preset (%s)", measure.verified ? "el 90 % del tiempo" : "media, con FG"),
                table + " · est.: píxeles internos elevados a 0,7, supone que limita la GPU");
        }
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
        // La base no llega en el modo de escalado más rápido del modo: lo que justifica un modelo más ligero
        const bool insufficient = plan.belowWant;
        const auto catalog = GatherUpscalers(feature);
        const Arch arch = ArchOf(gpu);
        const int atPreset = plan.preset >= 0 ? plan.preset : before.preset;
        std::string excluded;
        const auto options = Alternatives(catalog, arch, atPreset, before.th, &excluded);
        const Choice decided = options.empty() ? Choice {} : Decide(options, mode, insufficient);
        if (decided.key.empty())
        {
            Add(Mark::Bad, "Ningún escalador compatible disponible", "Se mantiene el actual");
            Next();
            break;
        }
        plan.scaler = decided;
        const char* presetName = atPreset >= 0 ? kPresets[atPreset].name : "el preset actual";
        plan.scalerEval = StrFmt("%d alternativas compatibles comparadas en %s (%s)", (int) options.size(), presetName, arch.name);
        Add(Mark::Info, StrFmt("Escaladores compatibles · %s · %s", arch.name, presetName),
            OptionsText(options) + (excluded.empty() ? std::string() : " · No entran: " + excluded));
        const UpscalerVariant* current = Current(catalog);
        const bool sameScaler = current != nullptr && KeyOf(*current) == plan.scaler.key;
        const bool same = sameScaler && (plan.scaler.model == Model::Any || SelectedModel() == plan.scaler.model);
        if (insufficient && (mode == Mode::Optimo || mode == Mode::Calidad))
        {
            std::string lighter;
            for (const auto& o : options)
                if (o.choice.tier < plan.scaler.tier && o.choice.tier >= 2 && !(o.needsEvidence && o.e.count < 2))
                    lighter += (lighter.empty() ? "" : " · ") + o.choice.name + (o.e.count > 0 ? StrFmt(" (%.0f medidos)", o.e.p10) : " (sin medir)");
            Add(Mark::Wait, StrFmt("Limitación: con %s no se alcanza el objetivo de %s", plan.scaler.name.c_str(), ModeName(mode)),
                std::string("Este modo no baja de fidelidad por su cuenta.") +
                    (lighter.empty() ? std::string() : " Opciones con menos fidelidad (no aplicadas): " + lighter + ". Equilibrado o Rendimiento las comparan."));
        }
        // Cómo medir una alternativa: el historial solo compara lo que se ha medido aquí
        bool unmeasured = false;
        for (const auto& o : options)
            unmeasured = unmeasured || (o.choice.tier >= 2 && o.e.count < 2);
        if (unmeasured && (mode == Mode::Equilibrado || mode == Mode::Rendimiento))
            Add(Mark::Info, "Para comparar una alternativa sin medir",
                "Actívala en ESCALADO, desactiva el FG, juega 15 s en la misma escena y pulsa OPTIMIZAR: cada pasada registra una "
                "medición; hacen falta dos de cada una en el mismo preset.");

        if (same)
        {
            plan.scalerStage = Stage::Verified;
            Add(Mark::Ok, "Escalador: " + plan.scaler.name + " (ya activo)", plan.scaler.why);
        }
        else
        {
            plan.changeScaler = true;
            plan.scalerStage = Stage::Selected;
            Add(Mark::Wait,
                sameScaler ? std::string("Modelo FSR 4: ") + (SelectedModel() == Model::Lite ? "INT8Lite → completo" : "completo → INT8Lite")
                           : "Escalador elegido: " + plan.scaler.name,
                plan.scaler.why + " · antes: " + before.scaler);
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
        plan.fgEvalGenerating = Generating(status);
        plan.fgEvalProvider = status.provider;
        // Sin FG la base aún no paga el coste del FG: se descuenta un 10 % (coste sin medir, se dice). Si el preset cambia,
        // la base estimada en el preset elegido
        const double measuredBase = Generating(status) ? measure.mean : measure.mean * 0.9;
        const bool presetMoves = plan.preset >= 0 && before.preset >= 0 && plan.preset != before.preset;
        const double base = presetMoves ? EstimateAt(measuredBase, before.preset, plan.preset) : measuredBase;

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
        const int desired = count; // antes de lo que admite el proveedor
        const bool useful = hz <= 0.0 || base * 2.0 <= hz * 1.1;

        if (rule.found && rule.Avoids("fg"))
            plan.fgWhy = "la matriz de compatibilidad lo excluye para este juego";
        else if (state.api != API::DX12)
            plan.fgWhy = "solo DirectX 12 está verificado";
        else if (judge == -2)
            plan.fgWhy = "pendiente de medición: sin FPS reales fiables no se decide el FG";
        else if (base < policy.fgBase)
        {
            plan.fgStage = Generating(status) ? Stage::Recommended : Stage::None;
            plan.fgWhy = StrFmt("la base (%.0f reales%s%s) no llega a %.0f", base, Generating(status) ? ", media medida con FG" : " tras el coste estimado",
                                presetMoves ? StrFmt(", estimada en %s", kPresets[plan.preset].name).c_str() : "", policy.fgBase);
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
            const Arch arch = ArchOf(gpu);
            if (plan.fg == FgRoute::DlssgGame && gpu.vendorId == VendorId::Nvidia && !arch.dlssMFG)
                count = 1; // DLSSG de NVIDIA: más de un fotograma generado solo en RTX 50
            plan.count = count;

            // Los multiplicadores fijos evaluados y por qué se descarta cada uno
            const int providerMax = plan.fg == FgRoute::FsrFg || plan.fg == FgRoute::XeFgNew ? 1
                                    : plan.fg == FgRoute::DlssgGame ? std::max(state.dlssgMfgMax.value_or(1), 1)
                                    : plan.fg == FgRoute::None      ? 0
                                                                    : std::max(IlionDynamicMfg::MaxCountLimit(), 1);
            if (plan.fg != FgRoute::None)
            {
                std::string table;
                for (int c = 1; c <= 3; c++)
                {
                    const char* no = c > policy.fgMax - 1                       ? "fuera del modo"
                                     : c > providerMax                         ? "el proveedor no lo admite"
                                     : c > maxCount                            ? "la base no lo sostiene"
                                     : ceiling > 0.0 && base * (c + 1) > ceiling ? "supera el monitor"
                                     : c > count                               ? "no hace falta"
                                                                               : nullptr;
                    table += StrFmt("%s%dX ≈ %.0f%s", table.empty() ? "" : " · ", c + 1, base * (c + 1),
                                    c == count ? " (elegido)" : no != nullptr ? StrFmt(" (no: %s)", no).c_str() : "");
                }
                Add(Mark::Info, "Multiplicadores fijos evaluados (FPS finales previstos)",
                    table + StrFmt(" · base %.0f reales%s", base, Generating(status) ? ", medida con FG" : ", coste del FG estimado en un 10 %"));
            }
            // FSR FG solo genera 2X: si el modo pediría más y XeFG está comprobado en esta GPU, se dice (cambia la salida
            // de FG: reinicio; no se aplica solo)
            const bool xefgAvailable = xefgDll && arch.xefgTested && !(rule.found && rule.Avoids("xefg"));
            if (plan.fg == FgRoute::FsrFg && desired > 1 && xefgAvailable)
                plan.fgAlt = StrFmt("XeFG admitiría %dX fijo para este modo; cambiar la salida de FG requiere reiniciar (no se aplica solo)", desired + 1);
            if (plan.fg != FgRoute::None && plan.fg != FgRoute::XeFgNew)
                plan.fgWhy = before.multiplier == count + 1 && Generating(status)
                                 ? StrFmt("se mantiene: %.0f reales × %d ≈ %.0f finales cumple el modo %s", base, count + 1, base * (count + 1), ModeName(mode))
                                 : StrFmt("%.0f reales × %d ≈ %.0f finales para el modo %s", base, count + 1, base * (count + 1), ModeName(mode));
            if (plan.fg == FgRoute::None)
                plan.fgWhy = "no hay una ruta fija verificada para esta combinación";
            else if (plan.fg == FgRoute::XeFgNew && !arch.xefgTested)
            {
                // Posible, pero sin comprobar por ILION en esta arquitectura: se recomienda, no se activa
                plan.fgStage = Stage::Recommended;
                plan.fgWhy = StrFmt("XeFG %dX fijo es posible, pero ILION no lo ha comprobado en %s", count + 1, arch.name);
            }
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
            Add(Mark::Wait, StrFmt("Frame Generation: %s · %dX fijo%s", route, count + 1,
                                   before.multiplier == count + 1 && Generating(status) ? " (mantenido)" : ""),
                StrFmt("%.0f FPS reales × %d ≈ %.0f finales%s · sin MFG dinámico%s", base, count + 1, base * (count + 1),
                       hz > 0.0 ? StrFmt(" · monitor %.0f Hz", hz).c_str() : "", Generating(status) ? "" : " · coste del FG sin medir"));
        }
        else
            Add(plan.fgStage == Stage::Blocked || plan.fgStage == Stage::Recommended ? Mark::Wait : Mark::Info,
                plan.fgStage == Stage::Blocked                                  ? "Frame Generation: pendiente de tu decisión"
                : plan.fgStage == Stage::Recommended && plan.fg == FgRoute::XeFgNew ? "Frame Generation: XeFG recomendado, no activado"
                : plan.fgStage == Stage::Recommended                            ? "Frame Generation: recomendado desactivarlo (no se cambia)"
                                                                                : "Frame Generation: no se cambia",
                plan.fgWhy);
        if (!plan.fgAlt.empty())
            Add(Mark::Info, "Alternativa de Frame Generation (no aplicada)", plan.fgAlt);
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
            // El modelo completo: el selector vuelve a automático (FFX elige el de la GPU), como lo hace el menú. Quita
            // también un INT8Lite heredado de otro modo
            modelChanged = false;
            if (plan.scaler.model == Model::Full && config->Fsr4Model.has_value())
            {
                const std::optional<FSR4Model> previous = config->Fsr4Model.value();
                config->Fsr4Model.reset();
                FfxApiProxy::ArmSrModelRevert(previous);
                State::Instance().newBackend = currentBackend != Upscaler::Reset ? currentBackend : Upscaler::FFX;
                MARK_ALL_BACKENDS_CHANGED();
                modelChanged = true;
            }
            plan.scalerStage = Stage::Applied;
            Add(Mark::Wait, "Aplicando " + plan.scaler.name, "Esperando a que el escalador se ejecute");
            break;
        }
        if (v != nullptr && v->status.active && !litePending && (!modelChanged || elapsed > 1.5))
        {
            if (plan.scaler.model == Model::Lite && !DriverLiteSelected())
            {
                const std::optional<FSR4Model> previous =
                    config->Fsr4Model.has_value() ? std::optional<FSR4Model>(config->Fsr4Model.value()) : std::nullopt;
                config->Fsr4Model = FSR4Model::INT8Lite;
                FfxApiProxy::ArmSrModelRevert(previous);
                State::Instance().newBackend = currentBackend != Upscaler::Reset ? currentBackend : Upscaler::FFX;
                MARK_ALL_BACKENDS_CHANGED();
                litePending = true;
                phaseStart = ImGui::GetTime();
                Last().detail = "Ejecutándose; cambiando al modelo INT8Lite del selector";
                break;
            }
            // Con FSR 4, verificado solo si el modelo que FFX determinó es el pedido
            const ModelState ms = plan.scaler.model == Model::Any ? ModelState {} : ModelNow(feature);
            plan.scalerStage = plan.scaler.model == Model::Any || ms.verified ? Stage::Verified : Stage::Applied;
            Last() = { plan.scalerStage == Stage::Verified ? Mark::Ok : Mark::Wait,
                       (plan.scalerStage == Stage::Verified ? "Escalador verificado: " : "Escalador aplicado: ") + plan.scaler.name,
                       plan.scaler.model == Model::Any ? std::string("Es el que ejecuta el último escalado") : ms.title + " · " + ms.detail };
            Next();
            break;
        }
        if (litePending && elapsed > 1.5 && v != nullptr && v->status.active && DriverLiteSelected())
        {
            const ModelState ms = ModelNow(feature);
            plan.scalerStage = ms.verified ? Stage::Verified : Stage::Applied;
            Last() = { ms.verified ? Mark::Ok : Mark::Wait, (ms.verified ? "Escalador verificado: " : "Escalador aplicado: ") + plan.scaler.name,
                       ms.title + " · " + ms.detail };
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
            std::string why;
            plan.viaMenu = RatioNeedsRestart(rule, &why);
            if (plan.viaMenu)
            {
                // El juego construye su tabla de calidades al arrancar con las respuestas de ILION y su menú elige la fila
                // (comprobado en Dawnwalker: la tabla la responde ILION, la fila la elige el juego). La anulación única
                // iguala todas las filas: se quita para que el menú del juego vuelva a cambiar la resolución
                if (owner != RatioOwner::None)
                {
                    config->UpscaleRatioOverrideEnabled = false;
                    Add(Mark::Info, StrFmt("Anulación única %.2f desactivada", previous),
                        owner == RatioOwner::External ? "Era externa: queda en los puntos de restauración"
                                                      : "Era de Perfiles: igualaba todos los presets del menú del juego");
                }
                plan.restartRatio = RatioConfigChangedThisSession();
                plan.presetStage = plan.restartRatio ? Stage::Restart : Stage::GameMenu;
                if (plan.restartRatio)
                    Add(Mark::Restart, "Tabla de calidades: requiere reiniciar el juego una vez",
                        StrFmt("El juego lee su tabla al arrancar (%s). Después, elige «%s» en su menú gráfico: el cambio de preset es "
                               "inmediato y se verifica con la resolución real.", why.c_str(), GameRowName(plan.preset)));
                else
                    Add(Mark::Wait, StrFmt("Acción en el juego: elige «%s» en su menú gráfico", GameRowName(plan.preset)),
                        "El juego ya usa su tabla de calidades: el cambio en su menú es inmediato. ILION no puede elegir el preset "
                        "del juego; lo verifica con la resolución real.");
            }
            else
            {
                config->UpscaleRatioOverrideEnabled = true;
                config->UpscaleRatioOverrideValue = kPresets[plan.preset].ratio;
                plan.presetStage = Stage::Pending;
                if (owner == RatioOwner::External)
                    Add(Mark::Info, StrFmt("Anulación externa %.2f sustituida", previous), "La original queda en los puntos de restauración");
                Add(Mark::Wait, StrFmt("Modo de escalado %s: pendiente de aplicación", kPresets[plan.preset].name),
                    StrFmt("Proporción %.2f guardada. Se aplica cuando el juego vuelva a consultar su resolución interna; si no "
                           "cambia, cambia la calidad en el menú del juego o reinícialo.",
                           kPresets[plan.preset].ratio));
            }
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
        // Resumen de decisiones: qué se mantuvo, qué cambió, qué queda pendiente y por qué. Lo extenso queda en el registro
        const Arch arch = ArchOf(gpu);
        // Escalador
        const bool scalerKeyChanged = !plan.scaler.key.empty() && before.scalerKey != plan.scaler.key;
        const std::string variant = plan.scaler.variant.empty() ? plan.scaler.name : plan.scaler.variant;
        if (plan.scaler.key.empty())
            changes.push_back({ Mark::Info, "Escalador: sin evaluar", "El juego no envía escalado" });
        else if (plan.scalerStage == Stage::Failed)
            changes.push_back({ Mark::Bad, "Escalador: " + variant + " — no llegó a ejecutarse", "Sigue " + before.scaler });
        else if (!scalerKeyChanged)
            changes.push_back({ Mark::Ok, "Escalador: " + variant + " — mantenido", plan.scalerEval });
        else
            changes.push_back({ plan.scalerStage == Stage::Verified ? Mark::Ok : Mark::Wait,
                                "Escalador: " + variant + (plan.scalerStage == Stage::Verified ? " — cambiado y verificado" : " — aplicado, sin verificar"),
                                "Antes: " + before.scaler + " · " + plan.scalerEval });
        // Modelo (FSR 4)
        if (plan.scaler.model != Model::Any && plan.scalerStage != Stage::Failed)
        {
            const std::string want = plan.scaler.model == Model::Lite ? std::string("INT8Lite") : StrFmt("%s completo", arch.fsr4Full);
            const bool modelMoved = scalerKeyChanged || before.model != plan.scaler.model;
            changes.push_back({ !modelMoved || plan.scalerStage == Stage::Verified ? Mark::Ok : Mark::Wait,
                                "Modelo: " + want +
                                    (!modelMoved                                ? " — mantenido"
                                     : plan.scalerStage == Stage::Verified ? " — cambiado y verificado por FFX"
                                                                           : " — aplicado, sin verificar"),
                                plan.scaler.why });
        }
        else if (!plan.scaler.key.empty())
            changes.push_back({ Mark::Info, "Modelo: no aplica", plan.scaler.why });
        // Preset y resolución interna
        if (plan.presetStage == Stage::Pending || plan.presetStage == Stage::Restart || plan.presetStage == Stage::GameMenu)
        {
            const unsigned nw = (unsigned) std::lround(before.tw / kPresets[plan.preset].ratio);
            const unsigned nh = (unsigned) std::lround(before.th / kPresets[plan.preset].ratio);
            changes.push_back({ plan.presetStage == Stage::Restart ? Mark::Restart : Mark::Wait,
                                StrFmt("Preset: %s — recomendado, PENDIENTE de aplicar (ahora %s)", kPresets[plan.preset].name, kPresets[before.preset].name),
                                plan.presetWhy });
            changes.push_back({ Mark::Info, "Resolución interna esperada: " + ResText(nw, nh) + " (ahora " + ResText(before.rw, before.rh) + ")",
                                plan.presetStage == Stage::GameMenu ? StrFmt("Elige «%s» en el menú gráfico del juego; ILION lo verifica solo", GameRowName(plan.preset))
                                : plan.presetStage == Stage::Restart && plan.viaMenu
                                    ? StrFmt("Reinicia una vez; después elige «%s» en el menú del juego", GameRowName(plan.preset))
                                : plan.presetStage == Stage::Restart ? std::string("Se aplica al reiniciar el juego")
                                                                     : std::string("Pendiente de que el juego consulte su resolución") });
        }
        else
            changes.push_back({ Mark::Ok, before.preset >= 0 ? StrFmt("Preset: %s — mantenido", kPresets[before.preset].name) : "Preset: sin cambios",
                                plan.presetWhy });
        // FG
        const std::string fgNow = status.provider == IlionDynamicMfg::Provider::None ? "sin FG" : status.name;
        const bool fgKept = before.multiplier == plan.count + 1 && before.fg == fgNow;
        if (plan.fgStage == Stage::Verified || plan.fgStage == Stage::Applied)
            changes.push_back({ plan.fgStage == Stage::Verified ? Mark::Ok : Mark::Wait,
                                StrFmt("FG: %s %dX fijo — %s", fgNow.c_str(), plan.count + 1,
                                       fgKept                                ? (plan.fgStage == Stage::Verified ? "mantenido y verificado" : "mantenido, sin verificar")
                                       : plan.fgStage == Stage::Verified ? "aplicado y verificado"
                                                                         : "aplicado, sin verificar"),
                                plan.fgWhy });
        else if (plan.fgStage == Stage::Restart)
            changes.push_back({ Mark::Restart, StrFmt("FG: XeFG %dX fijo — en el próximo inicio", plan.count + 1), "Antes: " + before.fg });
        else if (plan.fgStage == Stage::Blocked)
            changes.push_back({ Mark::Wait, StrFmt("FG: %dX fijo — NO aplicado", plan.count + 1), plan.fgWhy });
        else
            changes.push_back({ plan.fgStage == Stage::None ? Mark::Ok : Mark::Wait,
                                before.multiplier > 0 ? StrFmt("FG: %s %dX — mantenido", before.fg.c_str(), before.multiplier) : "FG: " + before.fg + " — mantenido",
                                plan.fgWhy.empty() ? std::string() : "Motivo: " + plan.fgWhy });
        // Reinicio
        const bool restart = plan.restartFg || plan.restartRatio;
        changes.push_back({ restart ? Mark::Restart : Mark::Ok, restart ? "Reinicio: NECESARIO" : "Reinicio: no necesario",
                            !restart                              ? std::string()
                            : plan.restartFg && plan.restartRatio ? "Frame Generation y resolución interna"
                            : plan.restartFg                      ? "Frame Generation"
                                                                  : "Resolución interna" });

        const bool pending = plan.presetStage == Stage::Pending || plan.presetStage == Stage::GameMenu || plan.fgStage == Stage::Applied ||
                             plan.fgStage == Stage::Blocked ||
                             plan.scalerStage == Stage::Applied || judge == -2;
        const std::string where = "ILION\\Perfiles\\" + ExeKey() + ".perfil.ini";
        if (restart)
            Add(Mark::Restart,
                plan.viaMenu && plan.presetStage == Stage::Restart
                    ? StrFmt("Optimización incompleta: reinicia el juego y después elige «%s» en su menú", GameRowName(plan.preset))
                    : std::string("Perfil guardado: pendiente de reinicio del juego"),
                where);
        else if (plan.presetStage == Stage::GameMenu)
            Add(Mark::Wait, StrFmt("Optimización incompleta: falta elegir «%s» en el menú gráfico del juego", GameRowName(plan.preset)), where);
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
    const bool atStart = RatioNeedsRestart(LoadRule(), &why);
    const bool ratioRestart = atStart && RatioConfigChangedThisSession();
    Add(ratioRestart ? Mark::Restart : atStart ? Mark::Ok : Mark::Wait,
        std::string(what) + ": proporción " +
            (config->UpscaleRatioOverrideEnabled.value_or_default() ? StrFmt("%.2f", config->UpscaleRatioOverrideValue.value_or_default())
                                                                    : std::string("del juego")),
        ratioRestart ? "Se aplica al reiniciar el juego (" + why + ")"
        : atStart    ? std::string("Igual que al arrancar el juego: no necesita reinicio")
                     : std::string("Se aplica cuando el juego vuelva a consultar su resolución interna"));
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

// lines: altura reservada en líneas, para que un cambio de texto no mueva lo que hay debajo
static void Row(Mark mark, const std::string& title, const std::string& detail, int lines = 0)
{
    const float h = ImGui::GetTextLineHeight();
    const float top = ImGui::GetCursorPosY();
    Pilot(mark, h);
    ImGui::BeginGroup();
    Coloured(mark == Mark::Info ? IlionTheme::ShellPal::Text : MarkColour(mark), title.c_str(), true);
    if (!detail.empty())
        Coloured(IlionTheme::ShellPal::Dim, detail.c_str(), true);
    ImGui::EndGroup();
    if (lines > 0)
    {
        const float used = ImGui::GetCursorPosY() - top;
        const float want = lines * ImGui::GetTextLineHeightWithSpacing();
        if (used < want)
            ImGui::Dummy(ImVec2(0.0f, want - used));
    }
}

// Lo que el perfil guardado dejó pendiente para este inicio
struct Saved
{
    bool exists = false;
    std::string modeName, date, session, preset, fgOutput, fgCount, prevRes, restart, route, whyScaler, whyPreset;
    std::string scalerKey, scalerName, model;
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
    cached.route = ini.GetValue("perfil", "ruta_proporcion", "");
    cached.whyScaler = ini.GetValue("perfil", "motivo_escalador", "");
    cached.whyPreset = ini.GetValue("perfil", "motivo_preset", "");
    cached.scalerKey = ini.GetValue("perfil", "escalador", "");
    cached.scalerName = ini.GetValue("perfil", "escalador_nombre", "");
    cached.model = ini.GetValue("perfil", "fsr4_modelo", "auto");
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

// Lo que corre ahora frente a lo que busca el perfil (última optimización de esta sesión, o el perfil guardado)
struct Live
{
    bool upscaling = false;
    float ratio = 0.0f;
    int activePreset = -1;
    int target = -1;        // el preset que busca el perfil
    bool viaMenu = false;   // llega por el menú gráfico del juego (tabla leída al arrancar)
    bool reached = false;   // la resolución real corresponde al preset buscado
    bool fgWanted = false, fgLoaded = false, savedThisSession = false;
    bool fgRestart = false, ratioRestart = false;
};

template <typename Ctx> static Live ReadLive(Ctx& ctx, const Saved& saved)
{
    Live l;
    const auto& state = State::Instance();
    IFeature* feature = ctx.currentFeature;
    l.upscaling = feature != nullptr && feature->IsInited();
    l.ratio = l.upscaling && feature->RenderWidth() > 0 ? (float) feature->TargetWidth() / (float) feature->RenderWidth() : 0.0f;
    l.activePreset = PresetOf(l.ratio);
    // Reinicios pendientes: los de esta optimización, o los del perfil mientras no estén cargados
    l.fgWanted = !saved.fgOutput.empty() && saved.fgOutput != "auto" && saved.fgOutput != "NoFG";
    l.fgLoaded = l.fgWanted && std::string(magic_enum::enum_name(state.activeFgOutput)) == saved.fgOutput;
    l.savedThisSession = saved.session == SessionId();
    l.fgRestart = (plan.restartFg && state.activeFgOutput != FGOutput::XeFG) ||
                  (saved.restart.find("fg") != std::string::npos && l.fgWanted && !l.fgLoaded);
    l.target = finished && plan.preset >= 0 ? plan.preset : PresetByName(saved.preset);
    l.viaMenu = finished ? plan.viaMenu : saved.route == "menu";
    l.reached = l.target >= 0 && l.activePreset == l.target;
    // Reinicio de la proporción: mientras la configuración de esta sesión no sea la que el juego leyó al arrancar
    l.ratioRestart = l.upscaling && !l.reached &&
                     (plan.restartRatio || (l.savedThisSession && saved.restart.find("proporcion") != std::string::npos)) &&
                     RatioConfigChangedThisSession();
    return l;
}

// El aviso de la optimización, debajo de OPTIMIZAR JUEGO. Rojo mientras falte algo por aplicar (con la acción concreta y
// la resolución interna esperada), verde solo cuando todo lo que el perfil gestiona está comprobado con lo que corre de
// verdad, ámbar cuando ahora no se puede comprobar. Se recalcula en cada fotograma: ILION lo verifica solo
template <typename Ctx> static void OutcomeBanner(Ctx& ctx, const Saved& saved, const Live& lv)
{
    if (running || (!finished && !saved.exists))
        return;
    IFeature* feature = ctx.currentFeature;
    const auto status = IlionDynamicMfg::Get();
    const auto catalog = GatherUpscalers(feature);
    const UpscalerVariant* current = Current(catalog);
    std::vector<std::string> todo;
    std::vector<std::string> verified;
    std::string unverifiable, expected;

    if (finished && optJudge == -2)
        todo.push_back("Medición pendiente: juega 15 s (sin FG si hay un límite o un Dynamic) y vuelve a pulsar OPTIMIZAR JUEGO");

    if (lv.fgRestart || lv.ratioRestart)
        todo.push_back(lv.fgRestart && lv.ratioRestart ? "Reinicia el juego: el nuevo Frame Generation y la tabla de resolución se cargan al arrancar"
                       : lv.fgRestart                ? "Reinicia el juego: el nuevo Frame Generation se carga al arrancar"
                                                     : "Reinicia el juego: la tabla de resolución se lee al arrancar");

    // Escalador y modelo
    const std::string wantKey = finished ? plan.scaler.key : saved.scalerKey;
    const std::string wantName = finished ? (plan.scaler.variant.empty() ? plan.scaler.name : plan.scaler.variant) : saved.scalerName;
    const bool scalerOk = current != nullptr && KeyOf(*current) == wantKey;
    if (!wantKey.empty())
    {
        if (!scalerOk)
            todo.push_back(StrFmt("Escalador: activa %s (ahora: %s). Pulsa OPTIMIZAR de nuevo o elígelo en ESCALADO", wantName.c_str(),
                                  current != nullptr ? current->name.c_str() : "ninguno"));
        else if (!current->status.active)
            unverifiable = "el escalador elegido todavía no ejecuta el escalado";
        else
            verified.push_back(current->name);
    }
    if (scalerOk && IsFsr4Full(*current))
    {
        const bool wantLite = finished ? plan.scaler.model == Model::Lite : saved.model == std::to_string((int) FSR4Model::INT8Lite);
        if (DriverLiteSelected() != wantLite)
            todo.push_back(StrFmt("Modelo FSR 4: elige %s en ESCALADO, o pulsa OPTIMIZAR de nuevo", wantLite ? "INT8Lite" : "Automático (modelo completo)"));
        else
        {
            const ModelState ms = ModelNow(feature);
            if (ms.verified)
                verified.push_back(ms.title);
            else if (ms.mark == Mark::Wait)
                todo.push_back(ms.title + ": no es el pedido. Revisa el modelo en ESCALADO");
            else if (unverifiable.empty())
                unverifiable = "FFX todavía no ha determinado el modelo FSR 4";
        }
    }

    // Preset y resolución interna
    if (lv.target >= 0)
    {
        if (!lv.upscaling)
        {
            if (unverifiable.empty())
                unverifiable = "el juego no está escalando ahora (menú principal o pantalla de carga)";
        }
        else if (!lv.reached)
        {
            const unsigned tw = feature->TargetWidth(), th = feature->TargetHeight();
            const std::string row = GameRowName(lv.target);
            todo.push_back(lv.ratioRestart ? StrFmt("Después del reinicio, selecciona %s en las opciones gráficas del juego", row.c_str())
                           : lv.viaMenu    ? StrFmt("Selecciona %s en las opciones gráficas del juego", row.c_str())
                                           : std::string("Cambia la calidad en el menú del juego (o reinícialo) para que vuelva a pedir su resolución interna"));
            expected = StrFmt("Resolución interna esperada: %s (ahora %s · %s)",
                              ResText((unsigned) std::lround(tw / kPresets[lv.target].ratio), (unsigned) std::lround(th / kPresets[lv.target].ratio)).c_str(),
                              ResText(feature->RenderWidth(), feature->RenderHeight()).c_str(),
                              lv.activePreset >= 0 ? kPresets[lv.activePreset].name : "?");
        }
        else
            verified.push_back(StrFmt("%s %s", kPresets[lv.target].name, ResText(feature->RenderWidth(), feature->RenderHeight()).c_str()));
    }

    // Frame Generation fijo
    if (finished && plan.fgStage == Stage::Blocked)
        todo.push_back(StrFmt("Frame Generation %dX fijo no aplicado: hay un MFG dinámico externo. Perfiles no lo cambia; desactívalo en "
                              "FRAME GEN si quieres el fijo y vuelve a optimizar",
                              plan.count + 1));
    int wantCount = 0;
    if (finished && (plan.fgStage == Stage::Applied || plan.fgStage == Stage::Verified || plan.fgStage == Stage::Restart))
        wantCount = plan.count + 1;
    else if (!finished && !saved.fgCount.empty())
        wantCount = atoi(saved.fgCount.c_str());
    if (wantCount > 0 && !lv.fgRestart)
    {
        if (status.provider == IlionDynamicMfg::Provider::None)
            todo.push_back(StrFmt("Frame Generation %dX: no hay FG cargado. Revisa FRAME GEN", wantCount));
        else if (!Generating(status))
        {
            if (unverifiable.empty())
                unverifiable = "el FG no genera ahora";
        }
        else if (status.multiplier != wantCount)
            todo.push_back(StrFmt("Frame Generation: el perfil pide %dX y genera %dX. Revisa FRAME GEN", wantCount, status.multiplier));
        else
            verified.push_back(StrFmt("%s %dX", status.name, wantCount));
    }

    const int level = !todo.empty() ? 2 : (!unverifiable.empty() || verified.empty()) ? 1 : 0;
    const ImVec4 edge = level == 2 ? IlionTheme::ShellPal::Red
                        : level == 1 ? IlionTheme::ShellPilot(IlionTheme::ShellPal::Yellow)
                                     : IlionTheme::ShellPilot(IlionTheme::ShellPal::Green);
    const ImVec4 fill = level == 2 ? ImVec4(0.30f, 0.04f, 0.03f, 1.0f) : level == 1 ? ImVec4(0.22f, 0.16f, 0.02f, 1.0f) : ImVec4(0.04f, 0.19f, 0.07f, 1.0f);
    const float font = ImGui::GetFontSize();
    ImGui::PushStyleColor(ImGuiCol_ChildBg, toneMapColor(fill));
    ImGui::PushStyleColor(ImGuiCol_Border, toneMapColor(edge));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, level == 2 ? 3.0f : 2.0f);
    if (ImGui::BeginChild("##ProfilesOutcome", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY))
    {
        Pilot(level == 2 ? Mark::Restart : level == 1 ? Mark::Wait : Mark::Ok, std::floor(font * 1.5f));
        Big(UiFont(), 1.3f, edge,
            level == 2   ? (finished ? "OPTIMIZACIÓN PENDIENTE" : "PERFIL PENDIENTE DE APLICAR")
            : level == 1 ? "VERIFICACIÓN PENDIENTE"
                         : (finished ? "OPTIMIZACIÓN APLICADA CORRECTAMENTE" : "PERFIL APLICADO CORRECTAMENTE"),
            true);
        if (level == 2)
        {
            Coloured(IlionTheme::ShellPal::Text, "Para completar el perfil:");
            for (const auto& t : todo)
                Coloured(IlionTheme::ShellPal::Text, ("→ " + t).c_str(), true);
            if (!expected.empty())
                Coloured(IlionTheme::ShellPal::Orange, expected.c_str(), true);
            Coloured(IlionTheme::ShellPal::Text2, "ILION verificará automáticamente el cambio.", true);
        }
        else if (level == 1)
            Coloured(IlionTheme::ShellPal::Text,
                     (unverifiable.empty() ? std::string("No hay nada que comprobar todavía") : "No se puede comprobar ahora: " + unverifiable).c_str(), true);
        else
        {
            std::string all;
            for (const auto& v : verified)
                all += (all.empty() ? "" : " · ") + v;
            Coloured(IlionTheme::ShellPal::Text, ("Comprobado con lo que ejecuta el juego: " + all).c_str(), true);
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
    ImGui::Spacing();
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
    const Live lv = ReadLive(ctx, saved);
    const float ratio = lv.ratio;
    const int activePreset = lv.activePreset;
    const bool overrideOn = config->UpscaleRatioOverrideEnabled.value_or_default();
    const float overrideValue = config->UpscaleRatioOverrideValue.value_or_default();
    const bool fgWanted = lv.fgWanted;
    const bool fgLoaded = lv.fgLoaded;
    const bool savedThisSession = lv.savedThisSession;
    const bool fgRestart = lv.fgRestart;
    const int target = lv.target;
    const bool targetViaMenu = lv.viaMenu;
    const bool targetReached = lv.reached;
    const bool ratioRestart = lv.ratioRestart;

    const ImVec4 panel = IlionTheme::shell ? IlionTheme::ShellPal::Panel : ImGui::GetStyleColorVec4(ImGuiCol_FrameBg);
    // El reinicio necesario y el resto de lo pendiente se avisan en rojo debajo de OPTIMIZAR JUEGO (OutcomeBanner)

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
            if (current != nullptr && IsFsr4Full(*current))
            {
                const ModelState ms = ModelNow(feature);
                Row(ms.mark, ms.title, ms.detail);
            }
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

            // Preset: recomendado, solicitado, activo, acción pendiente y reinicio, cada uno por separado
            ImGui::Spacing();
            Row(Mark::Info, std::string("Recomendado: ") + (target >= 0 ? kPresets[target].name : "—"),
                target < 0 ? std::string() : finished ? StrFmt("Última optimización, modo %s", ModeName(mode)) : std::string("Perfil guardado"));
            if (targetViaMenu && target >= 0)
                Row(Mark::Info, "Solicitado: el preset que elija el menú del juego",
                    config->QualityRatioOverrideEnabled.value_or_default() ? "Tabla de calidades con filas redefinidas en ILION; la fila la elige el juego"
                                                                            : "Tabla de calidades estándar; ILION no puede elegir la fila");
            else if (overrideOn)
            {
                const int wanted = PresetOf(overrideValue);
                Row(Mark::Info, StrFmt("Solicitado: anulación %.2f (%s)", overrideValue, wanted >= 0 ? kPresets[wanted].name : "?"),
                    OwnerOfOverride() == RatioOwner::Perfiles ? "Anulación de Perfiles" : "Anulación externa (no la gestiona el perfil)");
            }
            else
                Row(Mark::Info, "Solicitado: nada", "El juego usa el preset de su menú");
            Row(!upscaling ? Mark::Info : targetReached || target < 0 ? Mark::Ok : Mark::Wait,
                upscaling ? StrFmt("Activo: %s · %s", activePreset >= 0 ? kPresets[activePreset].name : "?",
                                   ResText(feature->RenderWidth(), feature->RenderHeight()).c_str())
                          : std::string("Activo: sin escalado ahora"),
                !upscaling     ? std::string()
                : targetReached ? std::string("Verificado con la resolución real: es el recomendado")
                : target >= 0   ? std::string("Resolución real del juego: todavía NO es el recomendado")
                                : std::string("Resolución real del juego"));
            const bool actionPending = upscaling && target >= 0 && !targetReached;
            Row(!actionPending ? Mark::Ok : ratioRestart ? Mark::Restart : Mark::Wait,
                !actionPending ? std::string("Acción pendiente: ninguna")
                : ratioRestart ? StrFmt("Acción pendiente: reinicia el juego y después elige «%s» en su menú", GameRowName(target))
                : targetViaMenu ? StrFmt("Acción pendiente: elige «%s» en el menú gráfico del juego", GameRowName(target))
                                : std::string("Acción pendiente: que el juego consulte de nuevo su resolución (cambia la calidad en su menú o reinícialo)"),
                actionPending ? "La optimización del preset no está aplicada hasta entonces" : std::string());
            Row(ratioRestart ? Mark::Restart : Mark::Ok,
                ratioRestart ? "Reinicio: necesario (la tabla de calidades cambió en esta sesión)" : "Reinicio: no necesario para el preset",
                std::string());
            for (const auto& e : resEvents)
                Row(Mark::Ok, e, "Detectado en esta sesión del juego, sin reiniciar");
            {
                const std::string whyScaler = finished ? plan.scaler.why : saved.whyScaler;
                const std::string whyPreset = finished ? plan.presetWhy : saved.whyPreset;
                if (!whyScaler.empty())
                    Coloured(IlionTheme::ShellPal::Dim, ("Por qué este escalador: " + whyScaler).c_str(), true);
                if (!whyPreset.empty())
                    Coloured(IlionTheme::ShellPal::Dim, ("Por qué este preset: " + whyPreset).c_str(), true);
            }
            if (!saved.prevRes.empty() && !savedThisSession && upscaling)
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
            const bool fgStale = finished && (generating != plan.fgEvalGenerating || status.provider != plan.fgEvalProvider);
            if (fgStale && (plan.fgStage == Stage::Blocked || plan.fgStage == Stage::Recommended))
                Row(Mark::Info, "La recomendación de FG anterior ya no aplica", "El FG cambió después de optimizar: vuelve a optimizar para evaluarlo");
            else if (plan.fgStage == Stage::Blocked)
                Row(Mark::Wait, "Recomendado: FG fijo, pendiente de tu decisión", "Desactiva el Dynamic en FRAME GEN y vuelve a optimizar");
            else if (plan.fgStage == Stage::Recommended)
                Row(plan.fg == FgRoute::XeFgNew ? Mark::Wait : Mark::Bad,
                    plan.fg == FgRoute::XeFgNew ? "Recomendado (no activado): XeFG" : "Recomendado: desactivar el FG", plan.fgWhy);
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
        // Sobre la ventana de 500 ms (con FG, la media de los FPS reales de la ventana) y con histéresis de 2 s
        Measure window = live;
        if (live.withFg && shown.base > 0.0)
            window.mean = shown.base;
        const int judge = StableJudge(Judge(window, policy));
        if (judge == -2)
            Row(Mark::Wait, StrFmt("Objetivo %s (%.0f FPS reales): sin datos fiables", ModeName(mode), policy.base), live.source, 3);
        else if (live.verified)
            Row(judge < 0 ? Mark::Bad : Mark::Ok,
                StrFmt("Objetivo %s (%.0f FPS reales): %s · medido", ModeName(mode), policy.base, judge < 0 ? "NO cumplido" : "cumplido"),
                StrFmt("El 90%% del tiempo a %.0f o más · %.0f%% del tiempo con 60 o más · sin FG", live.p10, live.share60 * 100.0), 3);
        else
            Row(judge < 0 ? Mark::Bad : Mark::Wait,
                StrFmt("Objetivo %s (%.0f FPS reales): %s · estimado", ModeName(mode), policy.base, judge < 0 ? "NO cumplido" : "probable"),
                "Con FG activo la estabilidad no se puede comprobar", 3);

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

// La resolución interna real que el juego envía: un cambio en la misma sesión del juego (sin reiniciar) se anota, con su
// hora, y se escribe en el registro de OptiScaler. Es la prueba de un cambio de preset en caliente. Se mira mientras el
// menú está abierto: un cambio hecho con el menú cerrado se ve al abrirlo. Diferencias de menos del 3 % no cuentan
static void WatchResolution(IFeature* feature)
{
    static unsigned lastW = 0, lastH = 0;
    if (feature == nullptr || !feature->IsInited() || feature->RenderWidth() == 0)
        return;
    const unsigned w = feature->RenderWidth(), h = feature->RenderHeight();
    if (lastW > 0 && std::abs((int) w - (int) lastW) > (int) lastW * 3 / 100)
    {
        resEvents.push_back(StrFmt("La resolución interna cambió sin reiniciar: %s → %s (%s)", ResText(lastW, lastH).c_str(), ResText(w, h).c_str(),
                                   NowText().substr(11).c_str()));
        if (resEvents.size() > 3)
            resEvents.erase(resEvents.begin());
        LOG_INFO("ILION Perfiles: internal resolution changed in the same game session {}x{} -> {}x{} (output {}x{})", lastW, lastH, w, h,
                 feature->TargetWidth(), feature->TargetHeight());
    }
    lastW = w;
    lastH = h;
}

template <typename Ctx> static void Panel(Ctx& ctx)
{
    SampleFgBase();
    WatchResolution(ctx.currentFeature);

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
    {
        // El aviso de lo pendiente o aplicado, siempre a la vista debajo del botón
        const Saved saved = ReadSaved();
        OutcomeBanner(ctx, saved, ReadLive(ctx, saved));
    }

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
