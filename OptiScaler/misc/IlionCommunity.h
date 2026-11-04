// ILION COMMUNITY — perfiles de configuración compartidos voluntariamente (rama experimental ilion-perfiles-exp)
//
// Núcleo sin interfaz y sin dependencias de OptiScaler, para poder probarlo fuera del juego (tests/ilion_community_test.cpp):
//   - el modelo de datos de un perfil, con campos técnicos de valores controlados (identificadores, nunca texto libre)
//   - validación estricta (longitudes, caracteres, listas cerradas, combinaciones coherentes)
//   - JSON de ida y vuelta (nlohmann, sin excepciones hacia fuera)
//   - almacenamiento local en %LOCALAPPDATA%\ILION\Community: escritura atómica, límites de tamaño, recuperación de
//     archivos dañados (se apartan como .danado-<fecha>.json y se empieza vacío)
//   - comprobación de compatibilidad con el equipo actual (lo que el equipo ofrece lo rellena quien llama)
//   - cliente del catálogo remoto: SIN red dentro del proceso del juego. Cada petición la hace curl.exe de Windows
//     (System32) como proceso aparte, desde un hilo de trabajo, con tiempo límite; el hilo del juego nunca espera
//
// Un perfil solo contiene identificadores de una lista cerrada y números acotados: no puede llevar rutas, DLL,
// ejecutables ni scripts. Nada se envía sin una acción explícita del usuario. Sin telemetría.
#pragma once

#include <windows.h>
#include <wincrypt.h>
#include <shlobj.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <json.hpp>

#pragma comment(lib, "crypt32.lib")

#pragma execution_character_set(push, "utf-8")

namespace IlionCommunity
{
constexpr int kSchema = 1;
constexpr size_t kMaxFileBytes = 1024 * 1024;       // listas locales
constexpr size_t kMaxCatalogBytes = 4 * 1024 * 1024; // caché del catálogo
constexpr size_t kMaxProfileBytes = 16 * 1024;      // un perfil importado
constexpr size_t kMaxEntries = 500;

// ---- Listas cerradas ------------------------------------------------------------------------------------------------
struct Item
{
    const char* id;
    const char* name;
};

inline const Item kModes[] = { { "optimo", "Óptimo" }, { "calidad", "Calidad" }, { "equilibrado", "Equilibrado" }, { "rendimiento", "Rendimiento" } };
inline const Item kApis[] = { { "DX12", "DirectX 12" }, { "DX11", "DirectX 11" }, { "Vulkan", "Vulkan" } };
inline const Item kArchs[] = {
    { "rdna4", "AMD RDNA 4" },          { "rdna3", "AMD RDNA 3" },         { "rdna2", "AMD RDNA 2" },
    { "amd_apu", "AMD APU / bajo consumo" }, { "amd_old", "AMD RDNA 1 o anterior" }, { "nv_gtx", "NVIDIA GTX (sin DLSS)" },
    { "rtx20", "NVIDIA RTX 20" },       { "rtx30", "NVIDIA RTX 30" },      { "rtx40", "NVIDIA RTX 40" },
    { "rtx50", "NVIDIA RTX 50" },       { "arc_a", "Intel Arc A" },        { "arc_b", "Intel Arc B" },
    { "intel_xe2", "Intel Xe2 integrada" }, { "intel_other", "Intel (sin XMX)" },
};
inline const Item kTechs[] = {
    { "fsr4_driver", "FSR 4 (driver AMD)" }, { "fsr4_dll", "FSR 4 (DLL)" },     { "fsr4_lite", "FSR 4 Lite comunitaria" },
    { "hlx_fsr4", "HLX FSR 4" },             { "helixsr", "HelixSR" },          { "dlss", "DLSS (nativo)" },
    { "xess", "XeSS" },                      { "fsr31", "FSR 3.1" },            { "fsr22", "FSR 2.2" },
};
inline const Item kModels[] = { { "completo", "Modelo completo" }, { "int8lite", "INT8Lite" }, { "no_aplica", "No aplica" } };
inline const Item kPresets[] = { { "nativa", "Nativa" },         { "ultra_calidad", "Ultra Calidad" }, { "calidad", "Calidad" },
                                 { "balanceado", "Balanceado" }, { "rendimiento", "Rendimiento" },     { "ultra_rendimiento", "Ultra Rendimiento" } };
inline const Item kFgs[] = { { "ninguno", "Sin FG" },           { "xefg", "XeFG" },  { "fsr_fg", "FSR FG" },
                             { "dlssg_juego", "DLSSG del juego" }, { "dlssg_salida", "Salida DLSSG de ILION" } };
inline const Item kReviews[] = { { "local", "Local, sin publicar" },
                                 { "pendiente", "Pendiente de revisión" },
                                 { "aprobado", "Aprobado por moderación" },
                                 { "rechazado", "Rechazado" } };
inline const struct
{
    int w, h;
} kResolutions[] = { { 1280, 720 }, { 1600, 900 }, { 1920, 1080 }, { 1920, 1200 }, { 2560, 1080 }, { 2560, 1440 },
                     { 2560, 1600 }, { 3440, 1440 }, { 3840, 1600 }, { 3840, 2160 }, { 5120, 1440 }, { 5120, 2160 } };

// Catálogo de GPU de referencia por arquitectura (ampliable con gpus.json en la carpeta de datos). La GPU detectada se
// añade siempre a su arquitectura
struct GpuRef
{
    const char* arch;
    const char* name;
};
inline const GpuRef kGpus[] = {
    { "rdna4", "AMD Radeon RX 9070 XT" },   { "rdna4", "AMD Radeon RX 9070" },      { "rdna4", "AMD Radeon RX 9060 XT" },
    { "rdna3", "AMD Radeon RX 7900 XTX" },  { "rdna3", "AMD Radeon RX 7900 XT" },   { "rdna3", "AMD Radeon RX 7900 GRE" },
    { "rdna3", "AMD Radeon RX 7800 XT" },   { "rdna3", "AMD Radeon RX 7700 XT" },   { "rdna3", "AMD Radeon RX 7600 XT" },
    { "rdna3", "AMD Radeon RX 7600" },      { "rdna2", "AMD Radeon RX 6950 XT" },   { "rdna2", "AMD Radeon RX 6900 XT" },
    { "rdna2", "AMD Radeon RX 6800 XT" },   { "rdna2", "AMD Radeon RX 6800" },      { "rdna2", "AMD Radeon RX 6750 XT" },
    { "rdna2", "AMD Radeon RX 6700 XT" },   { "rdna2", "AMD Radeon RX 6650 XT" },   { "rdna2", "AMD Radeon RX 6600 XT" },
    { "rdna2", "AMD Radeon RX 6600" },      { "amd_old", "AMD Radeon RX 5700 XT" }, { "amd_old", "AMD Radeon RX 5600 XT" },
    { "nv_gtx", "NVIDIA GeForce GTX 1660 SUPER" }, { "nv_gtx", "NVIDIA GeForce GTX 1080 Ti" },
    { "rtx20", "NVIDIA GeForce RTX 2080 Ti" }, { "rtx20", "NVIDIA GeForce RTX 2070 SUPER" }, { "rtx20", "NVIDIA GeForce RTX 2060" },
    { "rtx30", "NVIDIA GeForce RTX 3090" },   { "rtx30", "NVIDIA GeForce RTX 3080" },  { "rtx30", "NVIDIA GeForce RTX 3070" },
    { "rtx30", "NVIDIA GeForce RTX 3060 Ti" }, { "rtx30", "NVIDIA GeForce RTX 3060" },
    { "rtx40", "NVIDIA GeForce RTX 4090" },   { "rtx40", "NVIDIA GeForce RTX 4080 SUPER" }, { "rtx40", "NVIDIA GeForce RTX 4070 Ti SUPER" },
    { "rtx40", "NVIDIA GeForce RTX 4070 SUPER" }, { "rtx40", "NVIDIA GeForce RTX 4070" }, { "rtx40", "NVIDIA GeForce RTX 4060 Ti" },
    { "rtx40", "NVIDIA GeForce RTX 4060" },   { "rtx50", "NVIDIA GeForce RTX 5090" },  { "rtx50", "NVIDIA GeForce RTX 5080" },
    { "rtx50", "NVIDIA GeForce RTX 5070 Ti" }, { "rtx50", "NVIDIA GeForce RTX 5070" },  { "rtx50", "NVIDIA GeForce RTX 5060 Ti" },
    { "arc_a", "Intel Arc A770" },            { "arc_a", "Intel Arc A750" },           { "arc_b", "Intel Arc B580" },
    { "arc_b", "Intel Arc B570" },
};

template <size_t N> inline bool InList(const Item (&list)[N], const std::string& id)
{
    return std::any_of(std::begin(list), std::end(list), [&](const Item& i) { return id == i.id; });
}
template <size_t N> inline const char* NameOf(const Item (&list)[N], const std::string& id)
{
    for (const auto& i : list)
        if (id == i.id)
            return i.name;
    return "?";
}

inline bool IsAmdArch(const std::string& a) { return a == "rdna4" || a == "rdna3" || a == "rdna2" || a == "amd_apu" || a == "amd_old"; }
inline bool IsRtxArch(const std::string& a) { return a == "rtx20" || a == "rtx30" || a == "rtx40" || a == "rtx50"; }
inline bool HasFsr4(const std::string& a) { return a == "rdna4" || a == "rdna3" || a == "rdna2" || a == "amd_apu"; }

// Lo que una arquitectura puede ejecutar según lo que ILION sabe hoy. validated: comprobado por ILION en esa familia
struct Support
{
    bool possible = false;
    bool validated = false;
    const char* why = "";
};

inline Support TechSupport(const std::string& tech, const std::string& arch)
{
    if (tech == "fsr4_driver" || tech == "fsr4_dll")
        return HasFsr4(arch) ? Support { true, arch == "rdna3", "FSR 4 necesita una Radeon con FSR 4 (RDNA 2 con INT8, RDNA 3, RDNA 4)" }
                             : Support { false, false, "FSR 4 oficial necesita una Radeon compatible" };
    if (tech == "fsr4_lite" || tech == "hlx_fsr4" || tech == "helixsr")
        return { true, arch == "rdna3", "DLL comunitaria: decide ella misma si funciona; ILION solo la ha comprobado en RDNA 3" };
    if (tech == "dlss")
        return IsRtxArch(arch) ? Support { true, false, "DLSS nativo en NVIDIA RTX" } : Support { false, false, "DLSS nativo necesita una NVIDIA RTX" };
    if (tech == "xess" || tech == "fsr31" || tech == "fsr22")
        return { true, true, "Funciona en cualquier GPU" };
    return {};
}

inline Support ModelSupport(const std::string& model, const std::string& tech, const std::string& arch)
{
    const bool fsr4 = tech == "fsr4_driver" || tech == "fsr4_dll";
    if (model == "no_aplica")
        return fsr4 ? Support { false, false, "FSR 4 necesita un modelo (completo o INT8Lite)" } : Support { true, true, "" };
    if (!fsr4)
        return { false, false, "Solo FSR 4 tiene modelos que elegir" };
    if (model == "int8lite")
        return arch == "amd_apu" ? Support { false, false, "En esta GPU INT8Lite ya es el modelo completo" }
                                 : Support { true, arch == "rdna3", "INT8Lite lo ofrece el selector de modelos de FSR 4" };
    return { true, arch == "rdna3", "" };
}

// Multiplicadores fijos admitidos (2 = 2X). El MFG dinámico no forma parte de los perfiles
inline int FgMaxMultiplier(const std::string& fg, const std::string& arch)
{
    if (fg == "ninguno")
        return 0;
    if (fg == "fsr_fg")
        return 2;
    if (fg == "dlssg_juego")
        return arch == "rtx50" ? 4 : arch == "rtx40" ? 2 : IsRtxArch(arch) || arch == "nv_gtx" ? 0 : 4;
    return 4; // xefg, dlssg_salida: lo que admita el proveedor al cargarse, hasta 4X
}

// ---- Perfil ---------------------------------------------------------------------------------------------------------
struct Profile
{
    int schema = kSchema;
    std::string id;       // local-<hex> o el identificador del servidor
    int version = 1;      // sube con cada actualización de su autor
    std::string author;   // alias público
    std::string authorId; // identidad del servidor (vacío en local)
    std::string name;
    std::string gameExe, gameName, api, gpu, arch;
    int outW = 0, outH = 0;
    std::string mode, tech, model, preset, fg;
    int multiplier = 0; // 0 sin FG, 2..4
    std::string created, updated;
    std::string review = "local";
    long long downloads = -1; // -1: sin dato real
    std::string origin = "local"; // local | comunidad | importado
};

inline std::string NowUtc()
{
    const std::time_t t = std::time(nullptr);
    std::tm tm {};
    gmtime_s(&tm, &t);
    char buf[32] = {};
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}

inline std::string NewLocalId()
{
    std::random_device rd;
    char buf[40] = {};
    snprintf(buf, sizeof(buf), "local-%08x%08x%08x", rd(), rd(), rd());
    return buf;
}

// Caracteres de un texto visible: letras (también acentuadas, UTF-8), dígitos, espacio y . _ - ( ) ' + #
inline bool CleanText(const std::string& s, size_t minChars, size_t maxChars, std::string* err, const char* what)
{
    size_t chars = 0;
    for (size_t i = 0; i < s.size(); i++)
    {
        const unsigned char c = (unsigned char) s[i];
        if (c >= 0x80)
        {
            // Secuencia UTF-8 de 2 bytes (latín acentuado, cirílico); se rechazan las de 3 o 4 (emoji, símbolos)
            if ((c & 0xE0) != 0xC0 || i + 1 >= s.size() || (((unsigned char) s[i + 1]) & 0xC0) != 0x80)
            {
                if (err)
                    *err = std::string(what) + ": carácter no admitido";
                return false;
            }
            i++;
        }
        else if (!(isalnum(c) || c == ' ' || c == '.' || c == '_' || c == '-' || c == '(' || c == ')' || c == '\'' || c == '+' || c == '#'))
        {
            if (err)
                *err = std::string(what) + ": carácter no admitido «" + std::string(1, (char) c) + "»";
            return false;
        }
        chars++;
    }
    if (chars < minChars || chars > maxChars || (!s.empty() && (s.front() == ' ' || s.back() == ' ')))
    {
        if (err)
            *err = std::string(what) + ": entre " + std::to_string(minChars) + " y " + std::to_string(maxChars) +
                   " caracteres, sin espacios al principio ni al final";
        return false;
    }
    return true;
}

inline bool ValidExe(const std::string& exe)
{
    if (exe.size() < 5 || exe.size() > 64 || exe.substr(exe.size() - 4) != ".exe")
        return false;
    return std::all_of(exe.begin(), exe.end(), [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.' || c == ' '; });
}

// Validación completa. Devuelve el primer problema
inline bool Validate(const Profile& p, std::string* err)
{
    const auto fail = [&](const std::string& e)
    {
        if (err)
            *err = e;
        return false;
    };
    if (p.schema != kSchema)
        return fail("Versión de formato no admitida (" + std::to_string(p.schema) + ")");
    if (p.id.empty() || p.id.size() > 64 ||
        !std::all_of(p.id.begin(), p.id.end(), [](char c) { return isalnum((unsigned char) c) || c == '-'; }))
        return fail("Identificador no válido");
    if (p.version < 1 || p.version > 100000)
        return fail("Versión del perfil no válida");
    if (!CleanText(p.author, 3, 24, err, "Alias"))
        return false;
    if (!CleanText(p.name, 3, 40, err, "Nombre del perfil"))
        return false;
    if (!ValidExe(p.gameExe))
        return fail("Juego: ejecutable no válido");
    if (!CleanText(p.gameName, 1, 80, err, "Nombre del juego"))
        return false;
    if (!InList(kApis, p.api))
        return fail("API no válida");
    if (!InList(kArchs, p.arch))
        return fail("Arquitectura no válida");
    if (!CleanText(p.gpu, 3, 64, err, "GPU"))
        return false;
    if (p.outW < 640 || p.outW > 7680 || p.outH < 480 || p.outH > 4320)
        return fail("Resolución de salida fuera de rango");
    if (!InList(kModes, p.mode))
        return fail("Modo no válido");
    if (!InList(kTechs, p.tech))
        return fail("Tecnología de escalado no válida");
    if (!InList(kModels, p.model))
        return fail("Modelo no válido");
    if (!InList(kPresets, p.preset))
        return fail("Preset no válido");
    if (!InList(kFgs, p.fg))
        return fail("Frame Generation no válido");
    if (!InList(kReviews, p.review))
        return fail("Estado de revisión no válido");
    if (const auto s = TechSupport(p.tech, p.arch); !s.possible)
        return fail(std::string("Combinación incompatible: ") + s.why);
    if (const auto s = ModelSupport(p.model, p.tech, p.arch); !s.possible)
        return fail(std::string("Modelo incompatible: ") + s.why);
    const int maxMult = FgMaxMultiplier(p.fg, p.arch);
    if (p.fg == "ninguno" ? p.multiplier != 0 : (p.multiplier < 2 || p.multiplier > maxMult))
        return fail(maxMult == 0 && p.fg != "ninguno" ? "Frame Generation incompatible con la arquitectura"
                                                      : "Multiplicador fijo no admitido por ese Frame Generation");
    if (p.downloads < -1)
        return fail("Descargas no válidas");
    return true;
}

// Huella de la configuración (sin autor ni fechas): detecta duplicados
inline std::string ContentHash(const Profile& p)
{
    const std::string s = p.gameExe + "|" + p.api + "|" + p.arch + "|" + p.gpu + "|" + std::to_string(p.outW) + "x" + std::to_string(p.outH) + "|" +
                          p.mode + "|" + p.tech + "|" + p.model + "|" + p.preset + "|" + p.fg + "|" + std::to_string(p.multiplier);
    uint64_t h = 1469598103934665603ull;
    for (const unsigned char c : s)
        h = (h ^ c) * 1099511628211ull;
    char buf[20] = {};
    snprintf(buf, sizeof(buf), "%016llx", (unsigned long long) h);
    return buf;
}

using Json = nlohmann::json;

inline Json ToJson(const Profile& p)
{
    return Json { { "schema", p.schema },   { "id", p.id },         { "version", p.version },     { "author", p.author },
                  { "author_id", p.authorId }, { "name", p.name },   { "game_exe", p.gameExe },    { "game_name", p.gameName },
                  { "api", p.api },         { "gpu", p.gpu },       { "arch", p.arch },           { "out_w", p.outW },
                  { "out_h", p.outH },      { "mode", p.mode },     { "tech", p.tech },           { "model", p.model },
                  { "preset", p.preset },   { "fg", p.fg },         { "multiplier", p.multiplier }, { "created", p.created },
                  { "updated", p.updated }, { "review", p.review }, { "downloads", p.downloads },  { "origin", p.origin },
                  { "content_hash", ContentHash(p) } };
}

// Lectura estricta: tipos exactos; lo que falte o no cuadre invalida el perfil
inline bool FromJson(const Json& j, Profile& p, std::string* err)
{
    if (!j.is_object())
    {
        if (err)
            *err = "No es un objeto JSON";
        return false;
    }
    bool ok = true;
    const auto str = [&](const char* k, std::string& out, bool required = true)
    {
        const auto it = j.find(k);
        if (it == j.end() || it->is_null())
        {
            if (required)
                ok = false;
            return;
        }
        if (!it->is_string() || it->get_ref<const std::string&>().size() > 256)
        {
            ok = false;
            return;
        }
        out = it->get<std::string>();
    };
    const auto num = [&](const char* k, auto& out, bool required = true)
    {
        const auto it = j.find(k);
        if (it == j.end() || it->is_null())
        {
            if (required)
                ok = false;
            return;
        }
        if (!it->is_number_integer())
        {
            ok = false;
            return;
        }
        out = it->get<std::decay_t<decltype(out)>>();
    };
    Profile r;
    num("schema", r.schema);
    str("id", r.id);
    num("version", r.version);
    str("author", r.author);
    str("author_id", r.authorId, false);
    str("name", r.name);
    str("game_exe", r.gameExe);
    str("game_name", r.gameName);
    str("api", r.api);
    str("gpu", r.gpu);
    str("arch", r.arch);
    num("out_w", r.outW);
    num("out_h", r.outH);
    str("mode", r.mode);
    str("tech", r.tech);
    str("model", r.model);
    str("preset", r.preset);
    str("fg", r.fg);
    num("multiplier", r.multiplier);
    str("created", r.created, false);
    str("updated", r.updated, false);
    str("review", r.review, false);
    num("downloads", r.downloads, false);
    str("origin", r.origin, false);
    if (!ok)
    {
        if (err)
            *err = "Faltan campos o tienen un tipo incorrecto";
        return false;
    }
    if (r.review.empty())
        r.review = "local";
    if (r.origin != "local" && r.origin != "comunidad" && r.origin != "importado")
        r.origin = "importado";
    if (r.created.size() > 32 || r.updated.size() > 32)
    {
        if (err)
            *err = "Fechas no válidas";
        return false;
    }
    if (!Validate(r, err))
        return false;
    p = r;
    return true;
}

inline bool ParseProfileText(const std::string& text, Profile& p, std::string* err)
{
    if (text.size() > kMaxProfileBytes)
    {
        if (err)
            *err = "El perfil supera el tamaño máximo (16 KB)";
        return false;
    }
    const Json j = Json::parse(text, nullptr, false);
    if (j.is_discarded())
    {
        if (err)
            *err = "JSON no válido";
        return false;
    }
    return FromJson(j, p, err);
}

// ---- Almacenamiento local ---------------------------------------------------------------------------------------------
inline std::filesystem::path& RootOverride()
{
    static std::filesystem::path p; // solo para las pruebas
    return p;
}

inline std::filesystem::path Root()
{
    if (!RootOverride().empty())
        return RootOverride();
    PWSTR base = nullptr;
    std::filesystem::path p;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &base)) && base != nullptr)
        p = std::filesystem::path(base) / L"ILION" / L"Community";
    if (base != nullptr)
        CoTaskMemFree(base);
    return p;
}

inline bool ReadFileLimited(const std::filesystem::path& path, size_t limit, std::string& out, bool* tooBig = nullptr)
{
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    if (ec)
        return false;
    if (size > limit)
    {
        if (tooBig)
            *tooBig = true;
        return false;
    }
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return false;
    out.assign((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return true;
}

// Escritura atómica: archivo temporal, vaciado a disco y sustitución en un paso. Un corte deja el anterior intacto
inline bool WriteAtomic(const std::filesystem::path& path, const std::string& data)
{
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    const auto tmp = path.wstring() + L".tmp";
    HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return false;
    DWORD written = 0;
    const bool ok = WriteFile(h, data.data(), (DWORD) data.size(), &written, nullptr) && written == data.size() && FlushFileBuffers(h);
    CloseHandle(h);
    if (!ok || !MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        DeleteFileW(tmp.c_str());
        return false;
    }
    return true;
}

// Un archivo que no se puede leer se aparta (nunca se borra) y se empieza vacío
inline std::filesystem::path QuarantineFile(const std::filesystem::path& path)
{
    std::string stamp = NowUtc();
    std::replace(stamp.begin(), stamp.end(), ':', '-');
    auto target = path;
    target.replace_extension(L".danado-" + std::wstring(stamp.begin(), stamp.end()) + L".json");
    std::error_code ec;
    std::filesystem::rename(path, target, ec);
    return ec ? std::filesystem::path() : target;
}

struct LoadResult
{
    std::vector<Profile> profiles;
    std::string warning; // archivo dañado, perfiles descartados...
};

inline LoadResult LoadList(const std::filesystem::path& path, size_t limit = kMaxFileBytes)
{
    LoadResult r;
    std::error_code ec;
    if (!std::filesystem::exists(path, ec))
        return r;
    std::string text;
    bool tooBig = false;
    if (!ReadFileLimited(path, limit, text, &tooBig))
    {
        if (tooBig)
        {
            const auto moved = QuarantineFile(path);
            r.warning = "Archivo demasiado grande, apartado: " + moved.filename().string();
        }
        else
            r.warning = "No se pudo leer " + path.filename().string();
        return r;
    }
    const Json j = Json::parse(text, nullptr, false);
    if (j.is_discarded() || !j.is_object() || !j.contains("profiles") || !j["profiles"].is_array())
    {
        const auto moved = QuarantineFile(path);
        r.warning = "Archivo dañado, apartado como " + moved.filename().string() + "; se empieza vacío";
        return r;
    }
    int bad = 0;
    for (const auto& item : j["profiles"])
    {
        if (r.profiles.size() >= kMaxEntries)
            break;
        Profile p;
        if (FromJson(item, p, nullptr))
            r.profiles.push_back(p);
        else
            bad++;
    }
    if (bad > 0)
        r.warning = std::to_string(bad) + " perfiles no válidos descartados de " + path.filename().string();
    return r;
}

inline bool SaveList(const std::filesystem::path& path, const std::vector<Profile>& list)
{
    Json arr = Json::array();
    for (size_t i = 0; i < list.size() && i < kMaxEntries; i++)
        arr.push_back(ToJson(list[i]));
    const Json j { { "schema", kSchema }, { "saved", NowUtc() }, { "profiles", arr } };
    const std::string text = j.dump(1);
    return text.size() <= kMaxFileBytes && WriteAtomic(path, text);
}

inline std::set<std::string> LoadIds(const std::filesystem::path& path, std::string* warning)
{
    std::set<std::string> ids;
    std::error_code ec;
    if (!std::filesystem::exists(path, ec))
        return ids;
    std::string text;
    const Json j = ReadFileLimited(path, 64 * 1024, text) ? Json::parse(text, nullptr, false) : Json(nullptr);
    if (j.is_discarded() || !j.is_object() || !j.contains("ids") || !j["ids"].is_array())
    {
        const auto moved = QuarantineFile(path);
        if (warning)
            *warning = "Favoritos dañados, apartados como " + moved.filename().string();
        return ids;
    }
    for (const auto& v : j["ids"])
        if (v.is_string() && v.get_ref<const std::string&>().size() <= 64 && ids.size() < kMaxEntries)
            ids.insert(v.get<std::string>());
    return ids;
}

inline bool SaveIds(const std::filesystem::path& path, const std::set<std::string>& ids)
{
    const Json j { { "schema", kSchema }, { "ids", Json(std::vector<std::string>(ids.begin(), ids.end())) } };
    return WriteAtomic(path, j.dump(1));
}

// Alta en una lista con control de duplicados: mismo identificador = actualización del mismo perfil (solo de su
// autor); misma configuración del mismo autor = duplicado
enum class AddResult
{
    Added,
    Updated,
    Duplicate,
    NotOwner,
    Full,
};

inline AddResult AddOrUpdate(std::vector<Profile>& list, const Profile& p)
{
    for (auto& e : list)
    {
        if (e.id == p.id)
        {
            if (e.author != p.author || e.authorId != p.authorId)
                return AddResult::NotOwner;
            e = p;
            return AddResult::Updated;
        }
    }
    const std::string hash = ContentHash(p);
    for (const auto& e : list)
        if (e.author == p.author && ContentHash(e) == hash)
            return AddResult::Duplicate;
    if (list.size() >= kMaxEntries)
        return AddResult::Full;
    list.push_back(p);
    return AddResult::Added;
}

// ---- Compatibilidad con el equipo actual --------------------------------------------------------------------------------
struct Machine
{
    std::string api, arch, gpu, gameExe;
    int outW = 0, outH = 0;
    std::set<std::string> techs; // tecnologías instaladas y utilizables ahora (del catálogo de escaladores)
    bool liteOffered = false;    // el selector de FSR 4 ofrece INT8Lite
    std::set<std::string> fgs;   // salidas de FG posibles ahora
};

enum class Level
{
    Ok,
    Warn,
    Bad,
};

struct Check
{
    Level level;
    std::string text;
    bool note = false; // aviso general (no verificado técnicamente): no cuenta para el grado de compatibilidad
};

// Grado de compatibilidad: confirmada (todo coincide), parcial (utilizable, con diferencias) o no compatible
enum class Fit
{
    Confirmed,
    Partial,
    Incompatible,
};

inline std::vector<Check> Compatibility(const Profile& p, const Machine& m)
{
    std::vector<Check> out;
    out.push_back(p.gameExe == m.gameExe ? Check { Level::Ok, "Juego: el que está en marcha" }
                                         : Check { Level::Bad, "Juego: es de " + p.gameName + " (" + p.gameExe + ")" });
    out.push_back(p.api == m.api ? Check { Level::Ok, "API: " + p.api } : Check { Level::Bad, "API: el perfil es para " + p.api + ", el juego usa " + m.api });
    if (p.gpu == m.gpu)
        out.push_back({ Level::Ok, "GPU: la misma (" + p.gpu + ")" });
    else if (p.arch == m.arch)
        out.push_back({ Level::Warn, "GPU distinta de la misma arquitectura (" + p.gpu + "): el rendimiento no es transferible" });
    else
        out.push_back({ Level::Warn, std::string("Otra arquitectura (") + NameOf(kArchs, p.arch) + "): la configuración se comprueba con la tuya" });
    out.push_back(p.outW == m.outW && p.outH == m.outH
                      ? Check { Level::Ok, "Resolución de salida: la misma" }
                      : Check { Level::Warn, "Resolución de salida distinta (" + std::to_string(p.outW) + "×" + std::to_string(p.outH) +
                                                 "): el preset se aplica igual, la resolución interna será otra" });
    const Support ts = TechSupport(p.tech, m.arch);
    if (!ts.possible)
        out.push_back({ Level::Bad, std::string(NameOf(kTechs, p.tech)) + ": incompatible con tu GPU. " + ts.why });
    else if (m.techs.count(p.tech) == 0)
        out.push_back({ Level::Bad, std::string(NameOf(kTechs, p.tech)) + ": no está instalada o no se puede usar ahora" });
    else
        out.push_back({ ts.validated ? Level::Ok : Level::Warn,
                        std::string(NameOf(kTechs, p.tech)) + (ts.validated ? ": disponible" : ": disponible, sin validar en tu arquitectura") });
    if (p.model == "int8lite" && !m.liteOffered)
        out.push_back({ Level::Bad, "INT8Lite: el selector de FSR 4 no lo ofrece en tu equipo" });
    const int maxMult = FgMaxMultiplier(p.fg, m.arch);
    if (p.fg != "ninguno")
    {
        if (maxMult == 0 || m.fgs.count(p.fg) == 0)
            out.push_back({ Level::Bad, std::string(NameOf(kFgs, p.fg)) + ": no disponible en tu equipo o en este juego" });
        else if (p.multiplier > maxMult)
            out.push_back({ Level::Bad, std::string(NameOf(kFgs, p.fg)) + " " + std::to_string(p.multiplier) + "X: tu GPU admite hasta " + std::to_string(maxMult) + "X" });
        else
            out.push_back({ Level::Ok, std::string(NameOf(kFgs, p.fg)) + " " + std::to_string(p.multiplier) + "X fijo (el proveedor confirma el máximo al cargarse)" });
    }
    out.push_back({ Level::Warn,
                    p.review == "aprobado" ? "Revisado por moderación, no verificado técnicamente en tu equipo"
                                           : "Perfil no verificado técnicamente: publicarlo no lo valida",
                    true });
    return out;
}

inline Fit Overall(const std::vector<Check>& checks)
{
    bool partial = false;
    for (const auto& c : checks)
    {
        if (c.level == Level::Bad)
            return Fit::Incompatible;
        partial = partial || (c.level == Level::Warn && !c.note);
    }
    return partial ? Fit::Partial : Fit::Confirmed;
}

inline const char* FitText(Fit f)
{
    return f == Fit::Confirmed ? "Compatibilidad confirmada" : f == Fit::Partial ? "Compatibilidad parcial" : "No compatible con este equipo";
}

inline bool Blocking(const std::vector<Check>& checks)
{
    return std::any_of(checks.begin(), checks.end(), [](const Check& c) { return c.level == Level::Bad; });
}

// ---- Ranking por descargas -------------------------------------------------------------------------------------------
// Las posiciones del catálogo ordenadas por las descargas que registra el servidor, de más a menos (los empates
// conservan el orden del catálogo; sin dato, -1, al final). El podio (TOP 3) solo admite perfiles con descargas reales:
// sin descargas no hay puesto, y nada se rellena
inline std::vector<size_t> RankByDownloads(const std::vector<Profile>& list)
{
    std::vector<size_t> order(list.size());
    for (size_t i = 0; i < order.size(); i++)
        order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return list[a].downloads > list[b].downloads; });
    return order;
}

inline int PodiumCount(const std::vector<Profile>& list, const std::vector<size_t>& order)
{
    int n = 0;
    while (n < 3 && n < (int) order.size() && list[order[n]].downloads > 0)
        n++;
    return n;
}

// ---- Servicio remoto -------------------------------------------------------------------------------------------------
// servicio.json en la carpeta de datos: { "url": "https://<proyecto>.supabase.co", "anon_key": "<clave pública>" }.
// Solo la clave pública (anon): las operaciones privilegiadas (moderación) se hacen en el servidor
struct ServiceConfig
{
    std::string url, anonKey;
    bool valid = false;
    std::string problem;
};

inline bool SafeToken(const std::string& s, size_t maxLen)
{
    return !s.empty() && s.size() <= maxLen &&
           std::all_of(s.begin(), s.end(), [](char c) { return isalnum((unsigned char) c) || c == '.' || c == '_' || c == '-'; });
}

// El servicio de ILION COMMUNITY (proyecto Supabase «ILION Community»): solo la dirección y la clave pública anon, que
// son públicas por diseño (los permisos los aplica el servidor con RLS). Un servicio.json en la carpeta de datos la sustituye
inline constexpr const char* kDefaultServiceUrl = "https://kynhydceunbqhyyafgyi.supabase.co";
inline constexpr const char* kDefaultAnonKey =
    "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJzdXBhYmFzZSIsInJlZiI6Imt5bmh5ZGNldW5icWh5eWFmZ3lpIiwicm9sZSI6ImFub24iLCJpYXQiOjE3OTM3NjM3NzcsImV4cCI6MjEwOTMzOTc3N30."
    "ja11m3dsTU3calvhzwB1Rg0EqTew1AVVwz85bXjuTcg";
inline bool& UseBuiltInService()
{
    static bool use = true; // las pruebas lo desactivan para comprobar el caso sin configurar
    return use;
}

inline ServiceConfig LoadService()
{
    ServiceConfig c;
    std::string text;
    const auto path = Root() / L"servicio.json";
    std::error_code ec;
    if (!std::filesystem::exists(path, ec))
    {
        if (UseBuiltInService())
        {
            c.url = kDefaultServiceUrl;
            c.anonKey = kDefaultAnonKey;
            c.valid = true;
            return c;
        }
        c.problem = "Catálogo remoto no configurado (falta servicio.json)";
        return c;
    }
    const Json j = ReadFileLimited(path, 8 * 1024, text) ? Json::parse(text, nullptr, false) : Json(nullptr);
    if (j.is_discarded() || !j.is_object() || !j.contains("url") || !j["url"].is_string() || !j.contains("anon_key") || !j["anon_key"].is_string())
    {
        c.problem = "servicio.json no es válido";
        return c;
    }
    c.url = j["url"].get<std::string>();
    c.anonKey = j["anon_key"].get<std::string>();
    while (!c.url.empty() && c.url.back() == '/')
        c.url.pop_back();
    const bool httpsOk = c.url.rfind("https://", 0) == 0 && c.url.size() < 200 &&
                         std::all_of(c.url.begin() + 8, c.url.end(), [](char ch) { return isalnum((unsigned char) ch) || ch == '.' || ch == '-' || ch == ':'; });
    if (!httpsOk)
        c.problem = "servicio.json: la dirección debe ser https://<proyecto>.supabase.co";
    else if (!SafeToken(c.anonKey, 1024))
        c.problem = "servicio.json: clave pública no válida";
    else
        c.valid = true;
    return c;
}

// El estado del servicio según la última respuesta real (nunca por existir servicio.json)
enum class ServiceState
{
    Unchecked,     // configurado, aún sin consultar
    NotConfigured, // falta servicio.json o no es válido
    Connected,     // el servidor respondió al catálogo
    Offline,       // sin respuesta: sin conexión, tiempo agotado o sin curl
    KeyRejected,   // 401/403: la clave pública no es la de este proyecto
    SchemaMissing, // 404: el proyecto existe pero falta ejecutar ilion_community_supabase.sql
    ServerError,   // otra respuesta o una respuesta que no se entiende
};

inline const char* ServiceStateText(ServiceState s)
{
    switch (s)
    {
    case ServiceState::NotConfigured:
        return "Servicio sin configurar";
    case ServiceState::Connected:
        return "Servicio conectado";
    case ServiceState::Offline:
        return "Sin conexión con el servicio";
    case ServiceState::KeyRejected:
        return "Clave pública rechazada por el servicio";
    case ServiceState::SchemaMissing:
        return "Servicio sin el esquema de ILION (falta ejecutar el SQL)";
    case ServiceState::ServerError:
        return "Error del servicio";
    default:
        return "Servicio sin comprobar";
    }
}

inline std::atomic<int>& LastServiceState()
{
    static std::atomic<int> state { (int) ServiceState::Unchecked };
    return state;
}

// La sesión del autor (Supabase Auth): el refresh token se guarda cifrado con DPAPI (solo este usuario de Windows)
struct Session
{
    std::string accessToken, refreshToken, userId, alias;
    long long expires = 0;
    bool Active() const { return !accessToken.empty() && !userId.empty(); }
};

inline bool SaveSession(const Session& s)
{
    const std::string plain = Json { { "refresh", s.refreshToken }, { "user", s.userId }, { "alias", s.alias } }.dump();
    DATA_BLOB in { (DWORD) plain.size(), (BYTE*) plain.data() }, out {};
    if (!CryptProtectData(&in, L"ILION Community", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out))
        return false;
    const bool ok = WriteAtomic(Root() / L"sesion.bin", std::string((const char*) out.pbData, out.cbData));
    LocalFree(out.pbData);
    return ok;
}

inline Session LoadSession()
{
    Session s;
    std::string blob;
    if (!ReadFileLimited(Root() / L"sesion.bin", 16 * 1024, blob))
        return s;
    DATA_BLOB in { (DWORD) blob.size(), (BYTE*) blob.data() }, out {};
    if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out))
        return s;
    const Json j = Json::parse(std::string((const char*) out.pbData, out.cbData), nullptr, false);
    LocalFree(out.pbData);
    if (j.is_object() && j.value("refresh", std::string()).size() < 4096)
    {
        s.refreshToken = j.value("refresh", std::string());
        s.userId = j.value("user", std::string());
        s.alias = j.value("alias", std::string());
    }
    return s;
}

inline void ForgetSession()
{
    std::error_code ec;
    std::filesystem::remove(Root() / L"sesion.bin", ec);
}

// Una petición HTTPS hecha por curl.exe (System32) en su propio proceso. Los datos y las cabeceras van en archivos
// temporales (nada sensible en la línea de órdenes), que se borran al terminar. Tiempo límite: 20 s
struct HttpResult
{
    int status = 0;       // 0: no hubo respuesta (sin conexión, sin curl, tiempo agotado)
    std::string body;
    std::string error;
};

inline std::wstring CurlPath()
{
    wchar_t dir[MAX_PATH] = {};
    GetSystemDirectoryW(dir, MAX_PATH);
    return std::wstring(dir) + L"\\curl.exe";
}

inline std::string CurlQuote(const std::string& s)
{
    std::string r = "\"";
    for (const char c : s)
    {
        if (c == '"' || c == '\\')
            r += '\\';
        if (c == '\r' || c == '\n')
            continue;
        r += c;
    }
    return r + "\"";
}

inline HttpResult HttpRequest(const std::string& method, const std::string& url, const std::vector<std::string>& headers, const std::string& body,
                              DWORD timeoutMs = 25000)
{
    HttpResult r;
    std::error_code ec;
    if (!std::filesystem::exists(CurlPath(), ec))
    {
        r.error = "curl.exe no está disponible en este Windows";
        return r;
    }
    if (url.rfind("https://", 0) != 0)
    {
        r.error = "Solo se admiten direcciones https";
        return r;
    }
    const auto dir = Root() / L"tmp";
    std::filesystem::create_directories(dir, ec);
    static std::atomic<unsigned> counter { 0 };
    const std::wstring tag = std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(++counter);
    const auto cfg = dir / (L"req-" + tag + L".cfg"), dataFile = dir / (L"req-" + tag + L".json"), outFile = dir / (L"res-" + tag + L".json"),
               hdrFile = dir / (L"res-" + tag + L".hdr");
    const auto fwd = [](const std::filesystem::path& p)
    {
        std::string s = p.string();
        std::replace(s.begin(), s.end(), '\\', '/');
        return s;
    };
    std::string config = "silent\nshow-error\nproto = \"=https\"\nmax-time = 20\nconnect-timeout = 8\nmax-filesize = 4194304\n";
    config += "request = " + CurlQuote(method) + "\nurl = " + CurlQuote(url) + "\n";
    for (const auto& h : headers)
        config += "header = " + CurlQuote(h) + "\n";
    if (!body.empty())
    {
        WriteAtomic(dataFile, body);
        config += "data-binary = " + CurlQuote("@" + fwd(dataFile)) + "\n";
    }
    config += "output = " + CurlQuote(fwd(outFile)) + "\ndump-header = " + CurlQuote(fwd(hdrFile)) + "\n";
    const auto cleanup = [&]()
    {
        std::error_code e;
        for (const auto& p : { cfg, dataFile, outFile, hdrFile })
            std::filesystem::remove(p, e);
    };
    if (!WriteAtomic(cfg, config))
    {
        r.error = "No se pudo preparar la petición";
        cleanup();
        return r;
    }
    std::wstring cmd = L"\"" + CurlPath() + L"\" -K \"" + cfg.wstring() + L"\"";
    STARTUPINFOW si { sizeof(si) };
    PROCESS_INFORMATION pi {};
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, dir.c_str(), &si, &pi))
    {
        r.error = "No se pudo iniciar curl.exe";
        cleanup();
        return r;
    }
    if (WaitForSingleObject(pi.hProcess, timeoutMs) != WAIT_OBJECT_0)
    {
        TerminateProcess(pi.hProcess, 1);
        WaitForSingleObject(pi.hProcess, 2000);
        r.error = "Tiempo de espera agotado";
    }
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    std::string hdr;
    if (ReadFileLimited(hdrFile, 64 * 1024, hdr))
    {
        // La última línea de estado (no se siguen redirecciones)
        size_t pos = 0;
        while ((pos = hdr.find("HTTP/", pos)) != std::string::npos)
        {
            const size_t sp = hdr.find(' ', pos);
            if (sp != std::string::npos)
                r.status = atoi(hdr.c_str() + sp + 1);
            pos = sp == std::string::npos ? hdr.size() : sp;
        }
    }
    ReadFileLimited(outFile, kMaxCatalogBytes, r.body);
    if (r.status == 0 && r.error.empty())
        r.error = code == 6 ? "Sin conexión (no se resuelve el servidor)" : code == 7 ? "No se pudo conectar con el servidor"
                  : code == 28 ? "Tiempo de espera agotado" : "Sin respuesta del servidor (curl " + std::to_string(code) + ")";
    cleanup();
    return r;
}

// Un trabajo en segundo plano: un solo hilo bajo demanda que termina al acabar. La interfaz consulta su estado; nunca
// espera
class Worker
{
  public:
    struct State
    {
        bool busy = false;
        std::string task;   // lo que se está haciendo
        std::string result; // mensaje del último trabajo
        bool ok = false;
        int serial = 0; // sube al terminar cada trabajo
    };

    bool Start(const std::string& task, std::function<std::pair<bool, std::string>()> job)
    {
        std::lock_guard<std::mutex> lock(_shared->m);
        if (_shared->s.busy)
            return false;
        _shared->s.busy = true;
        _shared->s.task = task;
        auto shared = _shared;
        std::thread(
            [shared, job]()
            {
                std::pair<bool, std::string> r { false, "Error interno" };
                try
                {
                    r = job();
                }
                catch (...)
                {
                    r = { false, "Error inesperado: la operación se ha cancelado" };
                }
                std::lock_guard<std::mutex> lock(shared->m);
                shared->s.busy = false;
                shared->s.ok = r.first;
                shared->s.result = r.second;
                shared->s.serial++;
            })
            .detach();
        return true;
    }

    State Get() const
    {
        std::lock_guard<std::mutex> lock(_shared->m);
        return _shared->s;
    }

  private:
    struct Shared
    {
        mutable std::mutex m;
        State s;
    };
    std::shared_ptr<Shared> _shared = std::make_shared<Shared>();
};

// Qué dice una respuesta del servicio sobre su estado (PostgREST: PGRST205 / 42P01 = la tabla no existe)
inline ServiceState Classify(const HttpResult& r)
{
    if (r.status == 0)
        return ServiceState::Offline;
    if (r.status >= 200 && r.status < 300)
        return ServiceState::Connected;
    if (r.status == 401 || r.status == 403)
        return ServiceState::KeyRejected;
    if (r.status == 404 || r.body.find("PGRST205") != std::string::npos || r.body.find("42P01") != std::string::npos)
        return ServiceState::SchemaMissing;
    return ServiceState::ServerError;
}

// ---- Cliente del catálogo (contrato con el esquema de ilion_community_supabase.sql) ----------------------------------------
inline std::vector<std::string> Headers(const ServiceConfig& c, const std::string& bearer, bool json = true)
{
    std::vector<std::string> h { "apikey: " + c.anonKey, "Authorization: Bearer " + (bearer.empty() ? c.anonKey : bearer), "Accept: application/json" };
    if (json)
        h.push_back("Content-Type: application/json");
    return h;
}

inline std::string ServerError(const HttpResult& r)
{
    if (r.status == 0)
        return r.error;
    const Json j = Json::parse(r.body, nullptr, false);
    std::string msg;
    if (j.is_object())
        for (const char* k : { "message", "msg", "error_description", "error" })
            if (j.contains(k) && j[k].is_string())
            {
                msg = j[k].get<std::string>().substr(0, 160);
                break;
            }
    return "El servidor respondió " + std::to_string(r.status) + (msg.empty() ? "" : ": " + msg);
}

// Fila del servidor -> perfil (el alias viene de la tabla de autores)
inline bool FromServerRow(const Json& row, Profile& p, std::string* err)
{
    if (!row.is_object())
        return false;
    Json j = row;
    j["schema"] = row.value("schema_version", 0);
    j["author"] = row.contains("ilion_authors") && row["ilion_authors"].is_object() ? row["ilion_authors"].value("alias", std::string()) : std::string();
    j["review"] = row.value("status", std::string("pendiente"));
    j["origin"] = "comunidad";
    j["created"] = row.value("created_at", std::string()).substr(0, 20);
    j["updated"] = row.value("updated_at", std::string()).substr(0, 20);
    return FromJson(j, p, err);
}

inline Json ToServerRow(const Profile& p)
{
    return Json { { "schema_version", p.schema }, { "name", p.name },     { "game_exe", p.gameExe }, { "game_name", p.gameName },
                  { "api", p.api },               { "gpu", p.gpu },       { "arch", p.arch },        { "out_w", p.outW },
                  { "out_h", p.outH },            { "mode", p.mode },     { "tech", p.tech },        { "model", p.model },
                  { "preset", p.preset },         { "fg", p.fg },         { "multiplier", p.multiplier }, { "content_hash", ContentHash(p) } };
}

inline std::string CatalogUrl(const ServiceConfig& c)
{
    return c.url + "/rest/v1/ilion_profiles?select=*,ilion_authors(alias)&status=eq.aprobado&order=downloads.desc&limit=500";
}

// Catálogo público: se guarda en la caché local (catalogo.json) para consultarlo sin conexión
inline std::pair<bool, std::string> FetchCatalog(const ServiceConfig& c, int* count)
{
    const HttpResult r = HttpRequest("GET", CatalogUrl(c), Headers(c, "", false), "");
    ServiceState state = Classify(r);
    const Json j = Json::parse(r.body, nullptr, false);
    if (r.status == 200 && (j.is_discarded() || !j.is_array()))
        state = ServiceState::ServerError;
    LastServiceState() = (int) state;
    if (state != ServiceState::Connected)
        return { false, std::string(ServiceStateText(state)) + ": " + (r.status == 200 ? std::string("respuesta no válida") : ServerError(r)) };
    std::vector<Profile> list;
    int bad = 0;
    for (const auto& row : j)
    {
        Profile p;
        if (list.size() < kMaxEntries && FromServerRow(row, p, nullptr))
            list.push_back(p);
        else
            bad++;
    }
    if (!SaveList(Root() / L"catalogo.json", list))
        return { false, "No se pudo guardar la caché del catálogo" };
    if (count)
        *count = (int) list.size();
    return { true, "Catálogo actualizado: " + std::to_string(list.size()) + " perfiles" + (bad ? " (" + std::to_string(bad) + " descartados por no ser válidos)" : "") };
}

// El contador de descargas (función del servidor): solo cuenta con sesión iniciada, una vez por cuenta y perfil, y nunca
// la del propio autor. El servidor decide; el cliente no envía ninguna cifra. Un fallo no impide la descarga local
inline std::pair<bool, std::string> RegisterDownload(const ServiceConfig& c, const Session& s, const std::string& id)
{
    if (!SafeToken(id, 64) || !s.Active())
        return { false, "Descarga no contada: inicia sesión para que cuente" };
    const HttpResult r = HttpRequest("POST", c.url + "/rest/v1/rpc/ilion_register_download", Headers(c, s.accessToken), Json { { "pid", id } }.dump(), 10000);
    if (r.status != 200)
        return { false, "Descarga no contada: " + ServerError(r) };
    return { true, r.body.find("true") != std::string::npos ? "Descarga contada en el servidor" : "Descarga ya contada antes (una por cuenta)" };
}

// Inicio de sesión del autor por código de un solo uso enviado por correo (Supabase Auth). El correo solo va al servicio
// de autenticación; ILION no lo guarda
inline std::pair<bool, std::string> RequestCode(const ServiceConfig& c, const std::string& email)
{
    const bool plausible = email.size() >= 5 && email.size() <= 254 && email.find('@') != std::string::npos && email.find(' ') == std::string::npos;
    if (!plausible)
        return { false, "Correo no válido" };
    const HttpResult r = HttpRequest("POST", c.url + "/auth/v1/otp", Headers(c, ""), Json { { "email", email }, { "create_user", true } }.dump());
    return r.status == 200 ? std::pair<bool, std::string> { true, "Código enviado: revisa tu correo" } : std::pair<bool, std::string> { false, ServerError(r) };
}

inline bool ReadAuth(const HttpResult& r, Session& s)
{
    const Json j = Json::parse(r.body, nullptr, false);
    if (r.status != 200 || !j.is_object() || !j.contains("access_token") || !j["access_token"].is_string())
        return false;
    s.accessToken = j["access_token"].get<std::string>();
    s.refreshToken = j.value("refresh_token", std::string());
    s.expires = (long long) std::time(nullptr) + j.value("expires_in", 3600) - 60;
    if (j.contains("user") && j["user"].is_object())
        s.userId = j["user"].value("id", std::string());
    return SafeToken(s.accessToken, 4096) && SafeToken(s.userId, 64);
}

inline std::pair<bool, std::string> FetchAlias(const ServiceConfig& c, Session& s)
{
    const HttpResult r = HttpRequest("GET", c.url + "/rest/v1/ilion_authors?select=alias&user_id=eq." + s.userId, Headers(c, s.accessToken, false), "");
    const Json j = Json::parse(r.body, nullptr, false);
    if (r.status != 200 || !j.is_array())
        return { false, ServerError(r) };
    s.alias = j.empty() ? std::string() : j[0].value("alias", std::string());
    return { true, "" };
}

inline std::pair<bool, std::string> VerifyCode(const ServiceConfig& c, const std::string& email, const std::string& code, Session& s)
{
    if (code.size() < 6 || code.size() > 10 || !std::all_of(code.begin(), code.end(), ::isdigit))
        return { false, "El código son cifras" };
    const HttpResult r = HttpRequest("POST", c.url + "/auth/v1/verify", Headers(c, ""), Json { { "type", "email" }, { "email", email }, { "token", code } }.dump());
    if (!ReadAuth(r, s))
        return { false, ServerError(r) };
    FetchAlias(c, s);
    SaveSession(s);
    return { true, s.alias.empty() ? "Sesión iniciada. Reserva tu alias para publicar" : "Sesión iniciada como " + s.alias };
}

inline std::pair<bool, std::string> Refresh(const ServiceConfig& c, Session& s)
{
    if (s.refreshToken.empty())
        return { false, "Inicia sesión para publicar" };
    const HttpResult r =
        HttpRequest("POST", c.url + "/auth/v1/token?grant_type=refresh_token", Headers(c, ""), Json { { "refresh_token", s.refreshToken } }.dump());
    if (!ReadAuth(r, s))
        return { false, r.status == 0 ? r.error : std::string("La sesión ha caducado: inicia sesión de nuevo") };
    FetchAlias(c, s);
    SaveSession(s);
    return { true, "" };
}

// El alias queda ligado a la cuenta en el servidor (único, sin distinguir mayúsculas): nadie puede publicar como otro
inline std::pair<bool, std::string> ClaimAlias(const ServiceConfig& c, Session& s, const std::string& alias)
{
    std::string err;
    if (!CleanText(alias, 3, 24, &err, "Alias"))
        return { false, err };
    const HttpResult r = HttpRequest("POST", c.url + "/rest/v1/ilion_authors", Headers(c, s.accessToken), Json { { "user_id", s.userId }, { "alias", alias } }.dump());
    if (r.status == 201)
    {
        s.alias = alias;
        SaveSession(s);
        return { true, "Alias reservado: " + alias };
    }
    return { false, r.status == 409 ? std::string("Ese alias ya pertenece a otra cuenta") : ServerError(r) };
}

// Publica o actualiza (mismo id del servidor = nueva versión del mismo autor; el servidor lo comprueba con su identidad)
inline std::pair<bool, std::string> Publish(const ServiceConfig& c, Session& s, Profile& p)
{
    if (s.alias.empty())
        return { false, "Reserva primero tu alias" };
    if (p.author != s.alias)
        return { false, "El perfil está firmado como «" + p.author + "», tu alias es «" + s.alias + "»" };
    std::string err;
    if (!Validate(p, &err))
        return { false, err };
    std::vector<std::string> h = Headers(c, s.accessToken);
    h.push_back("Prefer: return=representation");
    const bool update = !p.authorId.empty() && p.authorId == s.userId && p.id.rfind("local-", 0) != 0;
    Json row = ToServerRow(p);
    if (!update)
        row["author_id"] = s.userId;
    const HttpResult r = update ? HttpRequest("PATCH", c.url + "/rest/v1/ilion_profiles?id=eq." + p.id, h, row.dump())
                                : HttpRequest("POST", c.url + "/rest/v1/ilion_profiles", h, row.dump());
    if (r.status == 409)
        return { false, "Ya has publicado un perfil con esta misma configuración" };
    if (r.status != 200 && r.status != 201)
        return { false, ServerError(r) };
    const Json j = Json::parse(r.body, nullptr, false);
    if (!j.is_array() || j.empty() || !j[0].contains("id") || !j[0]["id"].is_string())
        return { false, update ? "No se actualizó: el perfil no es tuyo o no existe" : "Respuesta del servidor no válida" };
    p.id = j[0]["id"].get<std::string>();
    p.authorId = s.userId;
    p.version = j[0].value("version", p.version);
    p.review = j[0].value("status", std::string("pendiente"));
    p.origin = "local";
    return { true, update ? "Perfil actualizado: queda pendiente de revisión" : "Perfil publicado: queda pendiente de revisión antes de aparecer en el catálogo" };
}

// Los perfiles publicados por la cuenta, con su estado de moderación y descargas (la política de lectura deja ver los
// propios aunque estén pendientes o rechazados)
inline std::pair<bool, std::string> FetchMine(const ServiceConfig& c, const Session& s, std::vector<Profile>& out)
{
    if (!s.Active() || !SafeToken(s.userId, 64))
        return { false, "Inicia sesión para consultar tus perfiles publicados" };
    const HttpResult r = HttpRequest("GET", c.url + "/rest/v1/ilion_profiles?select=*,ilion_authors(alias)&author_id=eq." + s.userId + "&limit=100",
                                     Headers(c, s.accessToken, false), "");
    const Json j = Json::parse(r.body, nullptr, false);
    if (r.status != 200 || !j.is_array())
        return { false, ServerError(r) };
    out.clear();
    for (const auto& row : j)
    {
        Profile p;
        if (out.size() < 100 && FromServerRow(row, p, nullptr))
        {
            p.authorId = s.userId;
            p.origin = "local";
            out.push_back(p);
        }
    }
    return { true, "Estado de tus perfiles publicados actualizado (" + std::to_string(out.size()) + ")" };
}

// Retira del catálogo un perfil propio (la política de borrado solo lo permite a su autor)
inline std::pair<bool, std::string> Withdraw(const ServiceConfig& c, const Session& s, const std::string& id)
{
    if (!s.Active() || !SafeToken(id, 64))
        return { false, "Inicia sesión para retirar tus perfiles" };
    std::vector<std::string> h = Headers(c, s.accessToken, false);
    h.push_back("Prefer: return=representation");
    const HttpResult r = HttpRequest("DELETE", c.url + "/rest/v1/ilion_profiles?id=eq." + id, h, "");
    const Json j = Json::parse(r.body, nullptr, false);
    if (r.status != 200 || !j.is_array())
        return { false, ServerError(r) };
    if (j.empty())
        return { false, "No se retiró: el perfil no existe o no es tuyo" };
    return { true, "Perfil retirado del catálogo" };
}
} // namespace IlionCommunity

#pragma execution_character_set(pop)
