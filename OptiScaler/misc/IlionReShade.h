#pragma once

// ILION <-> ReShade bridge. ILION registers itself as a ReShade add-on through ReShade's official add-on API
// (exports ReShadeRegisterAddon / ReShadeRegisterEvent, API version of external/reshade, v6.8.0) and drives the
// effect runtime of the ReShade that OptiScaler loads with [Plugins] LoadReShade. No second program, window or hook.
//
// Threading: the menu only queues requests and reads a snapshot. Requests are applied, and the snapshot is read back
// from ReShade, inside ReShade's own reshade_present event on the render thread. So what the panel shows is what
// ReShade reports after applying, never what was asked for.

#include <cstdint>
#include <string>
#include <vector>

namespace IlionReShade
{
// The effects of the panel: the files of ReShade's official shader repository the installer brings
inline constexpr const char* kEffects[] = { "FakeHDR.fx", "Levels.fx", "Tonemap.fx", "Vibrance.fx", "LUT.fx", "Layer.fx" };

struct Uniform
{
    std::string name;
    std::string label;
    int type = 0; // 0 float, 1 int, 2 bool
    int count = 1;
    bool hasRange = false;
    float min = 0.0f, max = 1.0f, step = 0.0f;
    float values[4] {};
};

struct Effect
{
    std::string file;
    std::string technique;
    bool loaded = false; // ReShade has it compiled and listed
    bool enabled = false;
    std::vector<Uniform> uniforms;
};

struct Snapshot
{
    bool moduleFound = false;  // a ReShade dll with the add-on exports is in the process
    std::string version;       // its file version
    bool registered = false;   // ReShade accepted ILION as an add-on
    std::string error;         // why not, in English (translated by the menu)
    bool runtime = false;      // an effect runtime presented a frame recently
    bool effectsOn = false;    // ReShade's own effects on/off state
    std::vector<Effect> effects;
    uint64_t revision = 0;     // grows with every read back
    size_t pending = 0;        // requests not applied yet
};

// Looks for ReShade and registers ILION (rate-limited, cheap once registered). Called by the menu, never per frame
// from the game.
void Poll();

Snapshot Get();

void SetEffectsState(bool enabled);
void SetTechnique(const std::string& file, bool enabled);
void SetUniform(const std::string& file, const std::string& name, const float* values, int count);
void ResetUniform(const std::string& file, const std::string& name);
void ResetEffect(const std::string& file);

// Unregisters from ReShade; before ReShade is unloaded and at process detach
void Shutdown();
} // namespace IlionReShade
