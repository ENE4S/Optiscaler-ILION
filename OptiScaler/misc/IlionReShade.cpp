#include "pch.h"
#include "IlionReShade.h"

// Only the types of ReShade's add-on API: registration goes through its exports directly. reshade.hpp's
// register_addon is left out on purpose: with Dear ImGui included it also demands ReShade's own ImGui version.
#include "../../external/reshade/include/reshade_events.hpp"

#include <Psapi.h>

#include <Util.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <format>
#include <map>
#include <mutex>

namespace IlionReShade
{
namespace
{
// RESHADE_API_VERSION of external/reshade/include/reshade.hpp (v6.8.0), not included here (see above)
constexpr uint32_t kApiVersion = 20;

using RegisterAddonFn = bool (*)(void*, uint32_t);
using UnregisterAddonFn = void (*)(void*);
using RegisterEventFn = void (*)(reshade::addon_event, void*);

struct Request
{
    enum Kind
    {
        EffectsState,
        Technique,
        Uniform,
        ResetOne,
        ResetAll,
    } kind;
    std::string file;
    std::string name;
    float values[4] {};
    int count = 0;
    bool flag = false;
    std::string technique; // Technique: the one picked, empty for the first of the file
};

std::mutex lock;                    // guards everything below except the atomics
Snapshot snapshot;
std::vector<Request> requests;
std::vector<EffectRef> panelEffects = [] { std::vector<EffectRef> list; for (const char* file : kCatalogue) list.push_back({ file, "" }); return list; }();
bool dirty = true;                  // read the effects back at the next present
uint64_t lastRead = 0;              // also read back once a second: the first load of the effects is asynchronous
uint64_t lastPoll = 0;

HMODULE reshadeModule = nullptr;
RegisterEventFn registerEvent = nullptr;
RegisterEventFn unregisterEvent = nullptr;
UnregisterAddonFn unregisterAddon = nullptr;
std::atomic<reshade::api::effect_runtime*> runtime { nullptr };
std::atomic<uint64_t> lastPresent { 0 };

// The ReShade module: ReShade64.dll by name, else any loaded module with the add-on exports
HMODULE FindReShade()
{
    if (HMODULE module = GetModuleHandleW(L"ReShade64.dll"); module != nullptr &&
        GetProcAddress(module, "ReShadeRegisterAddon") != nullptr)
        return module;

    HMODULE modules[1024];
    DWORD bytes = 0;

    if (!K32EnumProcessModules(GetCurrentProcess(), modules, sizeof(modules), &bytes))
        return nullptr;

    const DWORD count = std::min<DWORD>(bytes, sizeof(modules)) / sizeof(HMODULE);

    for (DWORD i = 0; i < count; i++)
    {
        if (GetProcAddress(modules[i], "ReShadeRegisterAddon") != nullptr &&
            GetProcAddress(modules[i], "ReShadeRegisterEvent") != nullptr)
            return modules[i];
    }

    return nullptr;
}

std::string ModuleVersion(HMODULE module)
{
    wchar_t path[MAX_PATH] {};
    if (GetModuleFileNameW(module, path, MAX_PATH) == 0)
        return "";

    version_t version;
    if (!Util::GetFileVersion(path, &version))
        return "";

    return std::format("{}.{}.{}.{}", version.major, version.minor, version.patch, version.reserved);
}

// The technique of a panel entry: the one picked by name, or the first of its file
reshade::api::effect_technique FirstTechnique(reshade::api::effect_runtime* rt, const char* file);

reshade::api::effect_technique FindTechnique(reshade::api::effect_runtime* rt, const EffectRef& ref)
{
    return ref.technique.empty() ? FirstTechnique(rt, ref.file.c_str()) : rt->find_technique(ref.file.c_str(), ref.technique.c_str());
}

// The first technique of an effect file
reshade::api::effect_technique FirstTechnique(reshade::api::effect_runtime* rt, const char* file)
{
    reshade::api::effect_technique found { 0 };

    rt->enumerate_techniques(
        file,
        [](reshade::api::effect_runtime*, reshade::api::effect_technique technique, void* user)
        {
            auto* out = static_cast<reshade::api::effect_technique*>(user);
            if (out->handle == 0)
                *out = technique;
        },
        &found);

    return found;
}

void ReadUniform(reshade::api::effect_runtime* rt, reshade::api::effect_uniform_variable variable, Uniform& u)
{
    if (u.type == 1)
    {
        int32_t values[4] {};
        rt->get_uniform_value_int(variable, values, (size_t) u.count);
        for (int i = 0; i < u.count; i++)
            u.values[i] = (float) values[i];
    }
    else if (u.type == 2)
    {
        bool values[4] {};
        rt->get_uniform_value_bool(variable, values, (size_t) u.count);
        for (int i = 0; i < u.count; i++)
            u.values[i] = values[i] ? 1.0f : 0.0f;
    }
    else
    {
        rt->get_uniform_value_float(variable, u.values, (size_t) u.count);
    }
}

// Reads every effect of the panel back from ReShade: loaded or not, its technique state and its user uniforms
// (the ones ReShade itself feeds, with a "source" annotation, are left out)
std::vector<Effect> ReadEffects(reshade::api::effect_runtime* rt, const std::vector<EffectRef>& list)
{
    std::vector<Effect> effects;

    for (const auto& ref : list)
    {
        Effect effect;
        effect.ref = ref;
        const char* file = ref.file.c_str();

        const auto technique = FindTechnique(rt, ref);

        if (technique.handle != 0)
        {
            effect.loaded = true;
            effect.enabled = rt->get_technique_state(technique);

            char name[128] {};
            size_t size = sizeof(name);
            rt->get_technique_name(technique, name, &size);
            effect.technique = name;

            struct Context
            {
                reshade::api::effect_runtime* rt;
                std::vector<Uniform>* out;
                int* skipped;
            } context { rt, &effect.uniforms, &effect.skipped };

            rt->enumerate_uniform_variables(
                file,
                [](reshade::api::effect_runtime* runtime, reshade::api::effect_uniform_variable variable, void* user)
                {
                    auto* ctx = static_cast<Context*>(user);

                    char text[256] {};
                    size_t size = sizeof(text);
                    if (runtime->get_annotation_string_from_uniform_variable(variable, "source", text, &size))
                        return;

                    reshade::api::format base = reshade::api::format::unknown;
                    uint32_t rows = 1, columns = 1, arrayLength = 0;
                    runtime->get_uniform_variable_type(variable, &base, &rows, &columns, &arrayLength);

                    // Scalars and vectors only; matrices and arrays aren't offered
                    if (columns > 1 || arrayLength > 1 || rows < 1 || rows > 4)
                    {
                        (*ctx->skipped)++;
                        return;
                    }

                    Uniform u;
                    u.count = (int) rows;
                    u.type = base == reshade::api::format::r32_float                                         ? 0
                             : base == reshade::api::format::r32_sint || base == reshade::api::format::r32_uint ? 1
                                                                                                                : 2;

                    size = sizeof(text);
                    runtime->get_uniform_variable_name(variable, text, &size);
                    u.name = text;

                    size = sizeof(text);
                    u.label = runtime->get_annotation_string_from_uniform_variable(variable, "ui_label", text, &size)
                                  ? std::string(text)
                                  : u.name;

                    float range = 0.0f;
                    if (runtime->get_annotation_float_from_uniform_variable(variable, "ui_min", &range, 1))
                    {
                        u.min = range;
                        u.hasRange = runtime->get_annotation_float_from_uniform_variable(variable, "ui_max", &range, 1);
                        u.max = range;
                    }

                    if (runtime->get_annotation_float_from_uniform_variable(variable, "ui_step", &range, 1))
                        u.step = range;

                    ReadUniform(runtime, variable, u);
                    ctx->out->push_back(u);
                },
                &context);
        }

        effects.push_back(std::move(effect));
    }

    return effects;
}

// Every technique ReShade has loaded, with its effect file, for "Add effect"
std::vector<Technique> ReadAvailable(reshade::api::effect_runtime* rt)
{
    std::vector<Technique> list;

    rt->enumerate_techniques(
        nullptr,
        [](reshade::api::effect_runtime* runtime, reshade::api::effect_technique technique, void* user)
        {
            char file[256] {}, name[128] {};
            size_t fileSize = sizeof(file), nameSize = sizeof(name);
            runtime->get_technique_effect_name(technique, file, &fileSize);
            runtime->get_technique_name(technique, name, &nameSize);
            static_cast<std::vector<Technique>*>(user)->push_back({ file, name });
        },
        &list);

    return list;
}

void ApplyRequest(reshade::api::effect_runtime* rt, const Request& r)
{
    switch (r.kind)
    {
    case Request::EffectsState:
        rt->set_effects_state(r.flag);
        break;

    case Request::Technique:
        if (const auto technique = FindTechnique(rt, { r.file, r.technique }); technique.handle != 0)
            rt->set_technique_state(technique, r.flag);
        break;

    case Request::Uniform:
    {
        const auto variable = rt->find_uniform_variable(r.file.c_str(), r.name.c_str());
        if (variable.handle == 0)
            break;

        reshade::api::format base = reshade::api::format::unknown;
        rt->get_uniform_variable_type(variable, &base);

        if (base == reshade::api::format::r32_float)
        {
            rt->set_uniform_value_float(variable, r.values, (size_t) r.count);
        }
        else if (base == reshade::api::format::r32_sint || base == reshade::api::format::r32_uint)
        {
            int32_t values[4] {};
            for (int i = 0; i < r.count; i++)
                values[i] = (int32_t) std::lround(r.values[i]);
            rt->set_uniform_value_int(variable, values, (size_t) r.count);
        }
        else
        {
            bool values[4] {};
            for (int i = 0; i < r.count; i++)
                values[i] = r.values[i] != 0.0f;
            rt->set_uniform_value_bool(variable, values, (size_t) r.count);
        }
        break;
    }

    case Request::ResetOne:
        if (const auto variable = rt->find_uniform_variable(r.file.c_str(), r.name.c_str()); variable.handle != 0)
            rt->reset_uniform_value(variable);
        break;

    case Request::ResetAll:
        rt->enumerate_uniform_variables(
            r.file.c_str(),
            [](reshade::api::effect_runtime* runtime, reshade::api::effect_uniform_variable variable, void*)
            {
                char text[8] {};
                size_t size = sizeof(text);
                if (!runtime->get_annotation_string_from_uniform_variable(variable, "source", text, &size))
                    runtime->reset_uniform_value(variable);
            },
            nullptr);
        break;
    }
}

// ReShade's events, on the render thread
void OnPresent(reshade::api::effect_runtime* rt)
{
    runtime.store(rt);
    lastPresent.store(GetTickCount64());

    std::vector<Request> todo;
    bool read = false;
    {
        std::scoped_lock guard(lock);
        todo.swap(requests);
        const uint64_t now = GetTickCount64();
        read = dirty || !todo.empty() || now - lastRead > 1000;
        dirty = false;
        if (read)
            lastRead = now;
    }

    if (!read)
        return;

    for (const auto& r : todo)
        ApplyRequest(rt, r);

    std::vector<EffectRef> list;
    {
        std::scoped_lock guard(lock);
        list = panelEffects;
    }

    auto effects = ReadEffects(rt, list);
    auto available = ReadAvailable(rt);
    const bool effectsOn = rt->get_effects_state();

    std::scoped_lock guard(lock);
    snapshot.effects = std::move(effects);
    snapshot.available = std::move(available);
    snapshot.effectsOn = effectsOn;
    snapshot.revision++;
}

void OnDestroyRuntime(reshade::api::effect_runtime* rt)
{
    if (runtime.load() == rt)
        runtime.store(nullptr);

    std::scoped_lock guard(lock);
    snapshot.effects.clear();
    dirty = true;
}

void OnReloadedEffects(reshade::api::effect_runtime*)
{
    std::scoped_lock guard(lock);
    dirty = true;
}

void Queue(Request r)
{
    std::scoped_lock guard(lock);

    // A newer value of the same uniform replaces the one still waiting
    if (r.kind == Request::Uniform)
    {
        for (auto& waiting : requests)
        {
            if (waiting.kind == Request::Uniform && waiting.file == r.file && waiting.name == r.name)
            {
                waiting = r;
                return;
            }
        }
    }

    requests.push_back(std::move(r));
}
} // namespace

void Poll()
{
    const uint64_t now = GetTickCount64();

    {
        std::scoped_lock guard(lock);

        if (snapshot.registered || now - lastPoll < 2000)
            return;

        lastPoll = now;
    }

    HMODULE module = FindReShade();
    std::string error;
    bool registered = false;

    if (module != nullptr)
    {
        const auto registerAddon = reinterpret_cast<RegisterAddonFn>(GetProcAddress(module, "ReShadeRegisterAddon"));
        const auto unregister = reinterpret_cast<UnregisterAddonFn>(GetProcAddress(module, "ReShadeUnregisterAddon"));
        const auto onEvent = reinterpret_cast<RegisterEventFn>(GetProcAddress(module, "ReShadeRegisterEvent"));
        const auto offEvent = reinterpret_cast<RegisterEventFn>(GetProcAddress(module, "ReShadeUnregisterEvent"));

        if (registerAddon == nullptr || onEvent == nullptr || offEvent == nullptr || unregister == nullptr)
        {
            error = "This ReShade has no add-on support";
        }
        else if (!registerAddon(dllModule, kApiVersion))
        {
            error = "ReShade refused the add-on: it needs ReShade with full add-on support, 6.8 or newer";
        }
        else
        {
            onEvent(reshade::addon_event::reshade_present, reinterpret_cast<void*>(&OnPresent));
            onEvent(reshade::addon_event::destroy_effect_runtime, reinterpret_cast<void*>(&OnDestroyRuntime));
            onEvent(reshade::addon_event::reshade_reloaded_effects, reinterpret_cast<void*>(&OnReloadedEffects));

            registered = true;
            LOG_INFO("ILION registered as a ReShade add-on (API {})", kApiVersion);

            std::scoped_lock guard(lock);
            reshadeModule = module;
            registerEvent = onEvent;
            unregisterEvent = offEvent;
            unregisterAddon = unregister;
            dirty = true;
        }
    }

    std::scoped_lock guard(lock);
    snapshot.moduleFound = module != nullptr;
    snapshot.version = module != nullptr ? ModuleVersion(module) : "";
    snapshot.registered = registered;
    snapshot.error = error;

    if (!error.empty())
        LOG_WARN("ReShade bridge: {}", error);
}

Snapshot Get()
{
    std::scoped_lock guard(lock);
    Snapshot copy = snapshot;
    copy.pending = requests.size();
    copy.runtime = runtime.load() != nullptr && GetTickCount64() - lastPresent.load() < 2000;
    return copy;
}

void SetEffectsState(bool enabled) { Queue({ Request::EffectsState, "", "", {}, 0, enabled }); }

void SetTechnique(const EffectRef& effect, bool enabled)
{
    Request r { Request::Technique, effect.file };
    r.flag = enabled;
    r.technique = effect.technique;
    Queue(std::move(r));
}

void SetPanelEffects(const std::vector<EffectRef>& effects)
{
    std::scoped_lock guard(lock);
    panelEffects = effects;
    dirty = true;
}

void SetUniform(const std::string& file, const std::string& name, const float* values, int count)
{
    Request r { Request::Uniform, file, name };
    r.count = std::clamp(count, 1, 4);
    for (int i = 0; i < r.count; i++)
        r.values[i] = values[i];
    Queue(std::move(r));
}

void ResetUniform(const std::string& file, const std::string& name) { Queue({ Request::ResetOne, file, name }); }

void ResetEffect(const std::string& file) { Queue({ Request::ResetAll, file }); }

void Shutdown()
{
    std::scoped_lock guard(lock);

    if (!snapshot.registered || reshadeModule == nullptr)
        return;

    // Only while ReShade is still loaded (by name: at process detach no module list is walked)
    if (GetModuleHandleW(L"ReShade64.dll") == reshadeModule)
    {
        unregisterEvent(reshade::addon_event::reshade_present, reinterpret_cast<void*>(&OnPresent));
        unregisterEvent(reshade::addon_event::destroy_effect_runtime, reinterpret_cast<void*>(&OnDestroyRuntime));
        unregisterEvent(reshade::addon_event::reshade_reloaded_effects, reinterpret_cast<void*>(&OnReloadedEffects));
        unregisterAddon(dllModule);
    }

    snapshot.registered = false;
    runtime.store(nullptr);
}
} // namespace IlionReShade
