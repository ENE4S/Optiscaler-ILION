#include "pch.h"
#include "IlionGpuTuning.h"

#include "../external/adlx/ADLX.h"
#include "../external/adlx/IGPUTuning.h"
#include "../external/adlx/IGPUManualFanTuning.h"
#include "../external/adlx/IGPUManualGFXTuning.h"
#include "../external/adlx/IGPUManualPowerTuning.h"
#include "../external/adlx/IGPUManualVRAMTuning.h"
#include "../external/adlx/IPerformanceMonitoring.h"

#include <atomic>
#include <cmath>
#include <map>
#include "../external/adlx/IGPUAutoTuning.h"
#include <filesystem>
#include <fstream>
#include <json.hpp>
#include <shlobj.h>

namespace IlionGpuTuning
{
bool Range::contains(int value) const
{
    return step > 0 && value >= min && value <= max &&
           (static_cast<long long>(value) - min) % step == 0;
}

std::optional<std::array<Field, FieldCount>> WriteOrder(const Values& current, const Values& desired)
{
    std::array<Field, FieldCount> order{CoreMin, CoreMax, Voltage, Power, Memory};
    const auto min = desired.fields[CoreMin], max = desired.fields[CoreMax];
    if (min && max && *min > *max)
        return std::nullopt;
    if (min != current.fields[CoreMin] && max != current.fields[CoreMax] &&
        min && max && current.fields[CoreMax] && *min > *current.fields[CoreMax])
    {
        if (current.fields[CoreMin] && *max < *current.fields[CoreMin])
            return std::nullopt;
        std::swap(order[0], order[1]);
    }
    return order;
}

const char* ValidateDraft(const Snapshot& before, const Values& draft)
{
    const auto& old = before.live;
    if (old != draft && before.autoTuning)
        return "Hay un autoajuste activo. Desactívalo antes de editar.";
    for (int i = 0; i < FieldCount; ++i)
        if (old.fields[i] != draft.fields[i] &&
            (!old.fields[i] || !draft.fields[i] || before.ranges[i].min >= before.ranges[i].max ||
             !before.ranges[i].contains(*draft.fields[i])))
            return "Un ajuste no está disponible o está fuera del rango o paso admitido.";
    if ((old.fields[CoreMin] != draft.fields[CoreMin] || old.fields[CoreMax] != draft.fields[CoreMax]) &&
        !WriteOrder(old, draft))
        return "Las frecuencias mínima y máxima no admiten una secuencia válida.";
    if (old.zero != draft.zero && (!old.zero || !draft.zero))
        return "Zero RPM no está disponible.";
    if (old.curve != draft.curve)
    {
        if (!before.fanEditable || draft.curve.empty() || draft.curve.size() != old.curve.size())
            return "ADLX no ofrece una lista de escritura compatible con la curva actual.";
        for (const auto& point : draft.curve)
            if (!before.temperature.contains(point.temperature) || !before.speed.contains(point.speed))
                return "Temperatura o velocidad fuera del rango o paso admitido.";
    }
    return nullptr;
}

namespace
{
using namespace adlx;
struct CurveChange { std::vector<Point> before, applied; };
std::map<std::string, CurveChange> previousCurves;
using Json = nlohmann::json;
std::atomic<bool> busy{false};
std::atomic<std::shared_ptr<const Result>> latest;

IADLXSystem* System(std::string& error)
{
    // ADLX is process-wide. AFMF also uses it, so retain our module reference and
    // never terminate the shared runtime from this overlay.
    static HMODULE module = LoadLibraryExW(L"amdadlx64.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!module)
    {
        error = "AMD ADLX no está disponible en el controlador instalado.";
        return nullptr;
    }
    static IADLXSystem* system = nullptr;
    if (system)
        return system;
    const auto query = reinterpret_cast<ADLXQueryFullVersion_Fn>(GetProcAddress(module, ADLX_QUERY_FULL_VERSION_FUNCTION_NAME));
    const auto init = reinterpret_cast<ADLXInitialize_Fn>(GetProcAddress(module, ADLX_INIT_FUNCTION_NAME));
    adlx_uint64 version = 0;
    const auto status = query && init && ADLX_SUCCEEDED(query(&version)) ? init(version, &system) : ADLX_FAIL;
    if (!ADLX_SUCCEEDED(status) || !system)
    {
        system = nullptr;
        error = "Error al inicializar AMD ADLX (" + std::to_string(status) + ").";
    }
    return system;
}

struct Tuning
{
    IADLXGPUTuningServicesPtr service;
    IADLXGPUPtr gpu;
    IADLXManualGraphicsTuning2Ptr core;
    IADLXGPUAutoTuningPtr automatic;
    IADLXManualFanTuningPtr fan;
    IADLXManualPowerTuningPtr power;
    IADLXManualVRAMTuning2Ptr memory;

    Tuning(IADLXSystem* system, IADLXGPUPtr device) : gpu(device)
    {
        if (!ADLX_SUCCEEDED(system->GetGPUTuningServices(&service)) || !service)
            return;
        const auto get = [&](auto supported, auto read)
        {
            IADLXInterfacePtr ptr;
            adlx_bool yes = false;
            if (ADLX_SUCCEEDED((service.GetPtr()->*supported)(gpu, &yes)) && yes)
                if (!ADLX_SUCCEEDED((service.GetPtr()->*read)(gpu, &ptr)))
                    return IADLXInterfacePtr{};
            return ptr;
        };
        using S = IADLXGPUTuningServices;
        automatic = IADLXGPUAutoTuningPtr(get(&S::IsSupportedAutoTuning, &S::GetAutoTuning));
        core = IADLXManualGraphicsTuning2Ptr(get(&S::IsSupportedManualGFXTuning, &S::GetManualGFXTuning));
        fan = IADLXManualFanTuningPtr(get(&S::IsSupportedManualFanTuning, &S::GetManualFanTuning));
        power = IADLXManualPowerTuningPtr(get(&S::IsSupportedManualPowerTuning, &S::GetManualPowerTuning));
        memory = IADLXManualVRAMTuning2Ptr(get(&S::IsSupportedManualVRAMTuning, &S::GetManualVRAMTuning));
    }

    template<class F>
    void Fields(F&& field) const
    {
        using G = IADLXManualGraphicsTuning2;
        using P = IADLXManualPowerTuning;
        using M = IADLXManualVRAMTuning2;
        if (core)
        {
            field(CoreMin, core.operator->(), &G::GetGPUMinFrequency, &G::GetGPUMinFrequencyRange, &G::SetGPUMinFrequency);
            field(CoreMax, core.operator->(), &G::GetGPUMaxFrequency, &G::GetGPUMaxFrequencyRange, &G::SetGPUMaxFrequency);
            field(Voltage, core.operator->(), &G::GetGPUVoltage, &G::GetGPUVoltageRange, &G::SetGPUVoltage);
        }
        if (power)
            field(Power, power.operator->(), &P::GetPowerLimit, &P::GetPowerLimitRange, &P::SetPowerLimit);
        if (memory)
            field(Memory, memory.operator->(), &M::GetMaxVRAMFrequency, &M::GetMaxVRAMFrequencyRange, &M::SetMaxVRAMFrequency);
    }

    Snapshot Read() const
    {
        Snapshot state;
        adlx_bool flag = false;
        if (service && ADLX_SUCCEEDED(service->IsAtFactory(gpu, &flag)))
        {
            state.canReset = true;
            state.factory = flag;
        }
        if (automatic)
        {
            using A = IADLXGPUAutoTuning;
            for (const auto read : {&A::IsCurrentUndervoltGPU, &A::IsCurrentOverclockGPU, &A::IsCurrentOverclockVRAM})
            {
                adlx_bool active = false;
                if (ADLX_SUCCEEDED((automatic.operator->()->*read)(&active)) && active)
                    state.autoTuning = true;
            }
        }
        Fields([&](Field id, auto* device, auto get, auto getRange, auto)
        {
            ADLX_IntRange range{};
            int value = 0;
            if (ADLX_SUCCEEDED((device->*get)(&value)))
                state.live.fields[id] = value;
            if (ADLX_SUCCEEDED((device->*getRange)(&range)) && range.step > 0 && range.minValue <= range.maxValue)
                state.ranges[id] = {range.minValue, range.maxValue, range.step};
        });
        if (!fan)
            return state;

        if (ADLX_SUCCEEDED(fan->IsSupportedZeroRPM(&flag)) && flag && ADLX_SUCCEEDED(fan->GetZeroRPMState(&flag)))
            state.live.zero = flag;

        ADLX_IntRange speed{}, temperature{};
        IADLXManualFanTuningStateListPtr states;
        if (!ADLX_SUCCEEDED(fan->GetFanTuningRanges(&speed, &temperature)) ||
            !ADLX_SUCCEEDED(fan->GetFanTuningStates(&states)) || !states ||
            speed.step <= 0 || temperature.step <= 0 || speed.minValue >= speed.maxValue || temperature.minValue >= temperature.maxValue)
            return state;

        std::vector<Point> curve;
        const auto first = states->Begin(), end = states->End();
        if (end < first || end - first != states->Size())
            return state;
        curve.reserve(states->Size());
        for (adlx_uint i = first; i < end; ++i)
        {
            IADLXManualFanTuningStatePtr point;
            Point value;
            if (!ADLX_SUCCEEDED(states->At(i, &point)) || !point ||
                !ADLX_SUCCEEDED(point->GetTemperature(&value.temperature)) ||
                !ADLX_SUCCEEDED(point->GetFanSpeed(&value.speed)))
                return state;
            curve.push_back(value);
        }
        state.live.curve = std::move(curve);
        state.speed = {speed.minValue, speed.maxValue, speed.step};
        state.temperature = {temperature.minValue, temperature.maxValue, temperature.step};
        IADLXManualFanTuningStateListPtr writable;
        state.fanEditable = !state.live.curve.empty() &&
            ADLX_SUCCEEDED(fan->GetEmptyFanTuningStates(&writable)) && writable &&
            writable->Size() == state.live.curve.size();
        return state;
    }

    ADLX_RESULT PrepareCurve(const std::vector<Point>& curve, IADLXManualFanTuningStateListPtr& states) const
    {
        if (!fan)
            return ADLX_NOT_SUPPORTED;
        auto status = fan->GetEmptyFanTuningStates(&states);
        if (!ADLX_SUCCEEDED(status) || !states)
            return ADLX_FAIL;
        const auto first = states->Begin(), end = states->End();
        if (end < first || end - first != states->Size() || states->Size() != curve.size())
            return ADLX_INVALID_ARGS;
        for (adlx_uint i = first; i < end; ++i)
        {
            IADLXManualFanTuningStatePtr point;
            status = states->At(i, &point);
            if (!ADLX_SUCCEEDED(status) || !point)
                return ADLX_FAIL;
            status = point->SetTemperature(curve[i - first].temperature);
            if (!ADLX_SUCCEEDED(status))
                return status;
            status = point->SetFanSpeed(curve[i - first].speed);
            if (!ADLX_SUCCEEDED(status))
                return status;
        }
        adlx_int invalidPoint = -1;
        status = fan->IsValidFanTuningStates(states, &invalidPoint);
        return ADLX_SUCCEEDED(status) && invalidPoint >= 0 ? ADLX_INVALID_ARGS : status;
    }

    void ReadDefaults(Snapshot& state) const
    {
        const auto read = [&](Field field, auto* device, auto getter)
        {
            adlx_int value = 0;
            if (device && state.live.fields[field] && ADLX_SUCCEEDED((device->*getter)(&value)) &&
                state.ranges[field].contains(value))
                state.defaults.fields[field] = value;
        };
        using GfxDefaults = IADLXManualGraphicsTuning2_1;
        using MemoryDefaults = IADLXManualVRAMTuning2_1;
        using PowerDefaults = IADLXManualPowerTuning1;
        IADLXManualGraphicsTuning2_1Ptr graphics(core);
        IADLXManualVRAMTuning2_1Ptr vram(memory);
        IADLXManualPowerTuning1Ptr boardPower(power);
        read(CoreMin, graphics.operator->(), &GfxDefaults::GetGPUMinFrequencyDefault);
        read(CoreMax, graphics.operator->(), &GfxDefaults::GetGPUMaxFrequencyDefault);
        read(Voltage, graphics.operator->(), &GfxDefaults::GetGPUVoltageDefault);
        read(Power, boardPower.operator->(), &PowerDefaults::GetPowerLimitDefault);
        read(Memory, vram.operator->(), &MemoryDefaults::GetMaxVRAMFrequencyDefault);

        auto limits = state.live;
        for (Field field : {CoreMin, CoreMax})
            if (state.defaults.fields[field])
                limits.fields[field] = state.defaults.fields[field];
        if (!WriteOrder(state.live, limits))
        {
            state.defaults.fields[CoreMin].reset();
            state.defaults.fields[CoreMax].reset();
        }

        IADLXManualFanTuning1Ptr defaults(fan);
        if (!defaults)
            return;
        adlx_bool zero = false;
        if (state.live.zero.has_value() && ADLX_SUCCEEDED(defaults->GetDefaultZeroRPMState(&zero)))
            state.defaults.zero = zero;

        IADLXManualFanTuningStateListPtr list;
        if (!state.fanEditable || !ADLX_SUCCEEDED(defaults->GetDefaultFanTuningStates(&list)) || !list ||
            list->Size() != state.live.curve.size())
            return;
        const auto first = list->Begin(), end = list->End();
        if (end < first || end - first != list->Size())
            return;
        std::vector<Point> curve;
        for (auto i = first; i < end; ++i)
        {
            IADLXManualFanTuningStatePtr point;
            Point value;
            if (!ADLX_SUCCEEDED(list->At(i, &point)) || !point ||
                !ADLX_SUCCEEDED(point->GetTemperature(&value.temperature)) ||
                !ADLX_SUCCEEDED(point->GetFanSpeed(&value.speed)) ||
                !state.temperature.contains(value.temperature) || !state.speed.contains(value.speed))
                return;
            curve.push_back(value);
        }
        IADLXManualFanTuningStateListPtr writable;
        if (curve == state.live.curve || ADLX_SUCCEEDED(PrepareCurve(curve, writable)))
            state.defaults.curve = std::move(curve);
    }

    ADLX_RESULT WriteCurve(const IADLXManualFanTuningStateListPtr& states) const
    {
        return fan ? fan->SetFanTuningStates(states) : ADLX_NOT_SUPPORTED;
    }

    ADLX_RESULT WriteZero(bool value) const
    {
        return fan ? fan->SetZeroRPMState(value) : ADLX_NOT_SUPPORTED;
    }

    ADLX_RESULT WriteField(Field id, int value) const
    {
        ADLX_RESULT status = ADLX_NOT_SUPPORTED;
        Fields([&](Field field, auto* device, auto, auto, auto set)
        {
            if (field == id)
                status = (device->*set)(value);
        });
        return status;
    }
};

std::string Differences(const Values& a, const Values& b)
{
    std::string names;
    for (int i = 0; i < FieldCount; ++i)
        if (a.fields[i] != b.fields[i])
            names += std::string(" ") + fieldKeys[i];
    if (a.curve != b.curve)
        names += " Curva";
    if (a.zero != b.zero)
        names += " Zero RPM";
    return names;
}



// What APPLY checks after writing: the fields within one step of the driver (SameSetting), the curve and Zero RPM exact
std::string AppliedDifferences(const Snapshot& before, const Values& wanted, const Values& read)
{
    std::string names;
    for (int i = 0; i < FieldCount; ++i)
        if (!SameSetting(before.ranges[i], wanted.fields[i], read.fields[i]))
            names += std::string(" ") + fieldKeys[i];
    if (wanted.curve != read.curve)
        names += " Curva";
    if (wanted.zero != read.zero)
        names += " Zero RPM";
    return names;
}

template<class DeviceTuning>
std::string RestorePrevious(const DeviceTuning& tuning, const Snapshot& before, const Values* requested = nullptr)
{
    // Never perform a second global reset. Recover only settings we could read.
    const auto current = tuning.Read();
    auto restore = current.live;
    for (int i = 0; i < FieldCount; ++i)
        if (before.live.fields[i] && (!requested || requested->fields[i] != before.live.fields[i]))
            restore.fields[i] = before.live.fields[i];
    if (const auto order = WriteOrder(current.live, restore))
        for (Field i : *order)
            if (restore.fields[i] && restore.fields[i] != current.live.fields[i])
                tuning.WriteField(i, *restore.fields[i]);

    if (!before.live.curve.empty() && before.live.curve != current.live.curve &&
        (!requested || requested->curve != before.live.curve))
    {
        IADLXManualFanTuningStateListPtr states;
        if (ADLX_SUCCEEDED(tuning.PrepareCurve(before.live.curve, states)))
            tuning.WriteCurve(states);
    }
    if (before.live.zero && before.live.zero != current.live.zero &&
        (!requested || requested->zero != before.live.zero))
        tuning.WriteZero(*before.live.zero);

    const auto verified = tuning.Read();
    auto failed = Differences(before.live, verified.live);
    if (before.factory != verified.factory)
        failed += " Modo de ajuste";
    return failed;
}

template<class DeviceTuning>
std::string Apply(const DeviceTuning& tuning, const Snapshot& before, const Values& draft, bool& verified)
{
    const Values& old = before.live;
    verified = false;
    if (old == draft)
        return "El controlador ya tiene estos valores.";
    if (const auto* reason = ValidateDraft(before, draft))
        return reason;
    const auto order = WriteOrder(old, draft);
    if (!order)
        return "Las frecuencias mínima y máxima no admiten una secuencia válida.";

    const bool editFan = old.curve != draft.curve;
    const bool editZero = old.zero != draft.zero;
    if (editZero && (!old.zero || !draft.zero))
        return "Zero RPM no está disponible.";
    IADLXManualFanTuningStateListPtr curve;
    if (editFan)
    {

        const auto status = tuning.PrepareCurve(draft.curve, curve);
        if (!ADLX_SUCCEEDED(status))
            return "El controlador rechazó la curva (" + std::to_string(status) + ").";
    }

    ADLX_RESULT status = ADLX_OK;
    std::string field;
    const auto write = [&](const char* name, auto operation)
    {
        if (ADLX_SUCCEEDED(status))
        {
            field = name;
            status = operation();
            spdlog::info("[AJUSTE GPU] {} ADLX={}", field, static_cast<int>(status));
        }
    };
    auto attempted = old;
    for (Field id : *order)
        if (old.fields[id] != draft.fields[id])
            write(fieldKeys[id], [&]
            {
                attempted.fields[id] = draft.fields[id];
                return tuning.WriteField(id, *draft.fields[id]);
            });

    if (editFan)
        write("Curva", [&]
        {
            attempted.curve = draft.curve;
            return tuning.WriteCurve(curve);
        });
    if (editZero)
        write("Zero RPM", [&]
        {
            attempted.zero = draft.zero;
            return tuning.WriteZero(*draft.zero);
        });

    auto differences = AppliedDifferences(before, draft, tuning.Read().live);
    for (int retry = 0; retry < 2 && ADLX_SUCCEEDED(status) && !differences.empty(); ++retry)
    {
        Sleep(100);
        differences = AppliedDifferences(before, draft, tuning.Read().live);
    }
    if (ADLX_SUCCEEDED(status) && differences.empty())
    {
        verified = true;
        return "Ajustes comprobados mediante lectura del controlador.";
    }

    auto message = ADLX_SUCCEEDED(status) ? "Valores sin confirmar:" + differences
        : field + " rechazado (ADLX " + std::to_string(status) + ").";
    if (status == ADLX_RESET_NEEDED)
        message += " El controlador solicita restaurar el ajuste global antes de editar.";

    const auto failed = RestorePrevious(tuning, before, &attempted);
    message += failed.empty() ? " Se verificaron los valores anteriores." : " Sin recuperar:" + failed;
    return message;
}

void ReadMetrics(IADLXSystem* system, IADLXGPU* gpu, Snapshot& state)
{
    IADLXPerformanceMonitoringServicesPtr service;
    IADLXGPUMetricsSupportPtr support;
    IADLXGPUMetricsPtr sample;
    if (!ADLX_SUCCEEDED(system->GetPerformanceMonitoringServices(&service)) || !service ||
        !ADLX_SUCCEEDED(service->GetSupportedGPUMetrics(gpu, &support)) || !support ||
        !ADLX_SUCCEEDED(service->GetCurrentGPUMetrics(gpu, &sample)) || !sample)
        return;

    using S = IADLXGPUMetricsSupport;
    using M = IADLXGPUMetrics;
    const auto metric = [&]<class T>(int index, auto supported, ADLX_RESULT (ADLX_STD_CALL M::*read)(T*))
    {
        adlx_bool available = false;
        T value{};
        if (ADLX_SUCCEEDED((support.GetPtr()->*supported)(&available)) && available &&
            ADLX_SUCCEEDED((sample.GetPtr()->*read)(&value)) && std::isfinite(static_cast<double>(value)))
            state.metrics[index] = static_cast<double>(value);
    };
    metric(0, &S::IsSupportedGPUClockSpeed, &M::GPUClockSpeed);
    metric(1, &S::IsSupportedGPUVoltage, &M::GPUVoltage);
    metric(2, &S::IsSupportedGPUTotalBoardPower, &M::GPUTotalBoardPower);
    state.boardPower = state.metrics[2].has_value();
    if (!state.boardPower)
        metric(2, &S::IsSupportedGPUPower, &M::GPUPower);
    metric(3, &S::IsSupportedGPUTemperature, &M::GPUTemperature);
    metric(4, &S::IsSupportedGPUHotspotTemperature, &M::GPUHotspotTemperature);
    metric(5, &S::IsSupportedGPUFanSpeed, &M::GPUFanSpeed);
    metric(6, &S::IsSupportedGPUUsage, &M::GPUUsage);
    metric(7, &S::IsSupportedGPUVRAMClockSpeed, &M::GPUVRAMClockSpeed);
}

bool ValidProfileName(const std::string& name)
{
    return !name.empty() && name.size() <= 40 &&
           name.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-") == std::string::npos;
}

std::filesystem::path ProfilePath(const std::string& key, const std::string& name)
{
    if (!ValidProfileName(name))
        throw std::runtime_error("Usa de 1 a 40 letras, números, guiones o guiones bajos para el perfil.");
    PWSTR path = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &path)))
        throw std::runtime_error("No se pudo localizar LocalAppData.");
    const std::unique_ptr<wchar_t, decltype(&CoTaskMemFree)> folder(path, &CoTaskMemFree);
    std::string encoded;
    constexpr char hex[] = "0123456789ABCDEF";
    for (unsigned char c : key)
    {
        encoded += hex[c >> 4];
        encoded += hex[c & 15];
    }
    auto base = std::filesystem::path(folder.get()).make_preferred().wstring();
    if (!base.starts_with(L"\\\\?\\"))
        base = base.starts_with(L"\\\\") ? L"\\\\?\\UNC\\" + base.substr(2) : L"\\\\?\\" + base;
    return std::filesystem::path(base) / "ILION" / "GpuTuning" / encoded / ("profile-" + name + ".json");
}

Json ReadProfile(const std::filesystem::path& path, const std::string& key)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file || file.tellg() > 65536)
        throw std::runtime_error("No se pudo leer el perfil de GPU.");
    file.seekg(0);
    auto json = Json::parse(file);
    if (json.at("schema") != 1 || json.at("gpu") != key || !json.at("curve").is_array())
        throw std::runtime_error("El perfil no corresponde a esta GPU o versión.");
    return json;
}

void ReadProfileNames(Snapshot& state)
{
    std::error_code error;
    try
    {
        const auto folder = ProfilePath(state.key, "Predeterminado").parent_path();
        for (std::filesystem::directory_iterator entry(folder, error), end; !error && entry != end; entry.increment(error))
        {
            const std::string file = entry->path().filename().string();
            if (file.starts_with("profile-") && file.ends_with(".json") && entry->is_regular_file(error))
            {
                try
                {
                    const int kind = ReadProfile(entry->path(), state.key).value("kind", 2);
                    const auto name = file.substr(8, file.size() - 13);
                    if (kind >= 0 && kind <= 2 && ValidProfileName(name))
                        state.profiles.push_back({name, static_cast<ProfileKind>(kind)});
                }
                catch (const std::exception&) {} // Skip malformed files.
            }
        }
        std::ranges::sort(state.profiles, {}, &ProfileInfo::name);
    }
    catch (const std::exception&)
    {
        // Profile enumeration is optional. Manual names still work.
    }
}

void Profile(const Request& request, Result& result)
{
    const auto path = ProfilePath(request.key, request.profile);
    if (request.action == Action::Rename)
    {
        const auto destination = ProfilePath(request.key, request.newName);
        if (!MoveFileExW(path.c_str(), destination.c_str(), MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("No se pudo renombrar el perfil. Comprueba que el nombre esté libre.");
        result.message = "Perfil renombrado.";
        return;
    }
    if (request.action == Action::Delete)
    {
        if (!DeleteFileW(path.c_str()))
            throw std::runtime_error("No se pudo eliminar el perfil.");
        result.message = "Perfil eliminado.";
        return;
    }
    if (request.action == Action::Save)
    {
        auto capabilities = result.snapshot;
        capabilities.autoTuning = false;
        auto wanted = capabilities.live;
        if (request.kind != ProfileKind::Fans)
            wanted.fields = request.draft.fields;
        if (request.kind != ProfileKind::Undervolt)
        {
            wanted.curve = request.draft.curve;
            wanted.zero = request.draft.zero;
        }
        if (const auto* reason = ValidateDraft(capabilities, wanted))
            throw std::runtime_error(reason);

        Json json{{"schema", 1}, {"gpu", request.key}, {"curve", Json::array()}, {"kind", static_cast<int>(request.kind)}};
        for (int i = 0; i < FieldCount; ++i)
            if (request.kind != ProfileKind::Fans && request.draft.fields[i])
                json[fieldKeys[i]] = *request.draft.fields[i];
        if (request.kind != ProfileKind::Undervolt)
        {
            for (const auto& point : request.draft.curve)
                json["curve"].push_back({point.temperature, point.speed});
            if (request.draft.zero)
                json["zeroRPM"] = *request.draft.zero;
            json["customFan"] = request.customFan;
        }

        std::filesystem::create_directories(path.parent_path());
        auto temporary = path;
        temporary += "." + std::to_string(GetCurrentProcessId()) + ".tmp";
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        file << json.dump(2);
        file.close();
        if (!file || !MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_WRITE_THROUGH | (request.overwrite ? MOVEFILE_REPLACE_EXISTING : 0)))
        {
            const DWORD code = GetLastError();
            DeleteFileW(temporary.c_str());
            throw std::runtime_error(code == ERROR_ALREADY_EXISTS || code == ERROR_FILE_EXISTS
                ? "Ya existe un perfil con ese nombre." : "No se pudo guardar el perfil.");
        }
        result.message = "Perfil guardado.";
        return;
    }

    const auto json = ReadProfile(path, request.key);
    Values values = result.snapshot.live;
    const int storedKind = json.value("kind", 2);
    if (storedKind < 0 || storedKind > 2)
        throw std::runtime_error("Tipo de perfil no admitido.");
    const auto kind = static_cast<ProfileKind>(storedKind);
    if (kind != ProfileKind::Fans)
        for (int i = 0; i < FieldCount; ++i)
            if (json.contains(fieldKeys[i]))
                values.fields[i] = json.at(fieldKeys[i]).get<int>();
    if (kind != ProfileKind::Undervolt && json.contains("zeroRPM"))
        values.zero = json.at("zeroRPM").get<bool>();
    if (kind != ProfileKind::Undervolt && !json.at("curve").empty())
    {
        if (!json.at("curve").is_array() || json.at("curve").size() != values.curve.size())
            throw std::runtime_error("El perfil no coincide con la cantidad de puntos de esta GPU.");
        for (size_t i = 0; i < values.curve.size(); ++i)
        {
            const auto& point = json.at("curve").at(i);
            values.curve[i] = {point.at(0).get<int>(), point.at(1).get<int>()};
        }
    }
    auto capabilities = result.snapshot;
    capabilities.autoTuning = false;
    if (const auto* reason = ValidateDraft(capabilities, values))
        throw std::runtime_error(reason);
    if (json.contains("customFan"))
        result.loadedCustomFan = json.at("customFan").get<bool>();
    result.loadedKind = kind;
    result.loaded = std::move(values);
    result.message = "Perfil cargado como borrador. Revisa los valores y pulsa APLICAR en la pantalla correspondiente.";
}

void Execute(const Request& request, Result& result)
{
    auto& output = result.snapshot;
    auto* system = System(output.error);
    if (!system)
        return;
    IADLXGPUListPtr gpus;
    if (!ADLX_SUCCEEDED(system->GetGPUs(&gpus)) || !gpus)
    {
        output.error = "ADLX no pudo enumerar las GPU AMD.";
        return;
    }
    std::vector<IADLXGPUPtr> adapters;
    for (adlx_uint i = 0; i < gpus->Size(); ++i)
    {
        IADLXGPUPtr gpu;
        const char* name = nullptr;
        const char* pnp = nullptr;
        adlx_int id = 0;
        if (!ADLX_SUCCEEDED(gpus->At(i, &gpu)) || !gpu ||
            !ADLX_SUCCEEDED(gpu->UniqueId(&id)) || !ADLX_SUCCEEDED(gpu->PNPString(&pnp)) ||
            !pnp || !*pnp || !ADLX_SUCCEEDED(gpu->Name(&name)) || !name)
            continue;
        const auto key = std::to_string(id) + ":" + pnp;
        output.devices.push_back({id, name, key, std::string(name) + " [ADLX " + std::to_string(id) + "]"});
        adapters.push_back(gpu);
    }

    size_t selected = adapters.size();
    for (size_t i = 0; i < output.devices.size(); ++i)
        if (output.devices[i].key == request.key || (request.key.empty() && adapters.size() == 1 && request.action == Action::Read))
        {
            if (selected != adapters.size())
            {
                output.error = "ADLX devolvió una identidad de GPU duplicada.";
                return;
            }
            selected = i;
        }
    if (selected == adapters.size())
    {
        output.error = adapters.empty() ? "ADLX no detectó una GPU AMD identificable." :
            request.key.empty() ? "Selecciona la GPU AMD que quieres ajustar." : "La GPU seleccionada ya no está disponible.";
        return;
    }

    const auto key = output.devices[selected].key;
    const bool write = request.action == Action::Apply || request.action == Action::Restore ||
                       request.action == Action::RestoreCurve;
    const std::wstring mutexName = L"Local\\ILION_GpuTuning_" + std::to_wstring(output.devices[selected].id);
    struct Lock
    {
        HANDLE handle = nullptr;
        bool locked = false;
        ~Lock() { if (locked) ReleaseMutex(handle); if (handle) CloseHandle(handle); }
    } lock;
    if (write)
    {
        lock.handle = CreateMutexW(nullptr, FALSE, mutexName.c_str());
        const DWORD wait = lock.handle ? WaitForSingleObject(lock.handle, 0) : WAIT_FAILED;
        lock.locked = wait == WAIT_OBJECT_0 || wait == WAIT_ABANDONED;
        if (!lock.locked)
        {
            output.error = "Otra instancia de ILION está ajustando esta GPU o no se pudo obtener el bloqueo.";
            return;
        }
    }

    const Tuning tuning(system, adapters[selected]);
    auto state = tuning.Read();
    if (request.action == Action::Restore)
        tuning.ReadDefaults(state);
    if (const auto saved = previousCurves.find(key); saved != previousCurves.end() &&
        !state.live.curve.empty() && state.live.curve != saved->second.applied)
        previousCurves.erase(saved);
    if (write)
    {
        if (request.expected != state.live || request.atFactory != state.factory)
            result.message = "El controlador cambió desde la última lectura. Revisa los valores antes de volver a aplicar.";
        else if (request.action == Action::Apply || request.action == Action::RestoreCurve)
        {
            auto desired = request.draft;
            const bool restoring = request.action == Action::RestoreCurve;
            if (restoring)
            {
                desired = state.live;
                if (const auto saved = previousCurves.find(key); saved != previousCurves.end())
                    desired.curve = saved->second.before;
            }
            bool verified = false;
            if (restoring && desired.curve == state.live.curve)
                result.message = "No hay una curva anterior verificable para esta GPU.";
            else
                result.message = Apply(tuning, state, desired, verified);
            if (verified && desired.curve != state.live.curve)
            {
                if (restoring)
                    previousCurves.erase(key);
                else
                    previousCurves.insert_or_assign(key, CurveChange{state.live.curve, desired.curve});
            }
        }
        else if (request.draft != state.defaults)
            result.message = "El controlador cambió desde la última lectura. Revisa los valores antes de volver a aplicar.";
        else
        {
            auto desired = state.live;
            for (int i = 0; i < FieldCount; ++i)
                if (state.defaults.fields[i])
                    desired.fields[i] = state.defaults.fields[i];
            if (!state.defaults.curve.empty())
                desired.curve = state.defaults.curve;
            if (state.defaults.zero.has_value())
                desired.zero = state.defaults.zero;
            bool verified = false;
            result.message = Apply(tuning, state, desired, verified);
            if (verified && desired.curve != state.live.curve)
                previousCurves.erase(key);
        }
        spdlog::info("[AJUSTE GPU] ADLX GPU {}: {}", output.devices[selected].id, result.message);
        state = tuning.Read();
    }
    if (const auto saved = previousCurves.find(key); saved != previousCurves.end() &&
        state.live.curve == saved->second.applied)
    {
        auto desired = state.live;
        desired.curve = saved->second.before;
        state.canRestoreCurve = ValidateDraft(state, desired) == nullptr;
    }
    if (request.action == Action::Read && !request.draft.curve.empty() && request.draft.curve != state.live.curve)
    {
        state.checkedCurve = request.draft.curve;
        auto desired = state.live;
        desired.curve = state.checkedCurve;
        if (const auto* reason = ValidateDraft(state, desired))
            state.curveError = reason;
        else
        {
            IADLXManualFanTuningStateListPtr list;
            const auto status = tuning.PrepareCurve(desired.curve, list);
            if (!ADLX_SUCCEEDED(status))
                state.curveError = "ADLX rechazó la curva (" + std::to_string(status) + ").";
        }
    }
    tuning.ReadDefaults(state);
    ReadMetrics(system, tuning.gpu, state);
    state.key = key;
    state.devices = std::move(output.devices);
    output = std::move(state);
    if (request.action == Action::Save || request.action == Action::Load || request.action == Action::Rename || request.action == Action::Delete)
        Profile(request, result);
    ReadProfileNames(output);
}

struct Work
{
    Request request;
    HMODULE module;
};

void CALLBACK Run(PTP_CALLBACK_INSTANCE instance, void* context)
{
    std::unique_ptr<Work> work(static_cast<Work*>(context));
    FreeLibraryWhenCallbackReturns(instance, work->module);
    auto result = std::make_shared<Result>();
    result->action = work->request.action;
    try
    {
        Execute(work->request, *result);
    }
    catch (const std::exception& error)
    {
        spdlog::warn("[AJUSTE GPU] {}", error.what());
        result->message = work->request.action == Action::Save || work->request.action == Action::Load || work->request.action == Action::Rename || work->request.action == Action::Delete
            ? std::string(error.what()) : "Error al procesar el ajuste. Consulta OptiScaler.log.";
    }
    latest.store(std::move(result), std::memory_order_release);
    busy.store(false, std::memory_order_release);
}
}

bool Start(Request request)
{
    if (busy.load(std::memory_order_acquire))
        return false;
    auto work = std::make_unique<Work>(Work{std::move(request), nullptr});
    if (busy.exchange(true, std::memory_order_acq_rel))
        return false;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                          reinterpret_cast<LPCWSTR>(&Run), &work->module))
    {
        if (TrySubmitThreadpoolCallback(Run, work.get(), nullptr))
        {
            work.release();
            return true;
        }
        FreeLibrary(work->module);
    }
    busy.store(false, std::memory_order_release);
    return false;
}

std::shared_ptr<const Result> Latest()
{
    return latest.load(std::memory_order_acquire);
}
}

