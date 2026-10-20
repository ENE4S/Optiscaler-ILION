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
// The default catalogue: useful image effects of the official repositories Instalar_ReShade.bat brings
//   SweetFX (CeeJayDK, MIT): FakeHDR, Levels, Tonemap, Curves, CAS, LumaSharpen, LiftGammaGain
//   crosire/reshade-shaders: AdaptiveSharpen (legacy, bacondither), Deband (slim)
//   qUINT (Marty McFly): qUINT_lightroom, downloaded from its author on the user's machine
inline constexpr const char* kCatalogue[] = { "FakeHDR.fx",         "Levels.fx", "Tonemap.fx",     "Curves.fx",
                                              "LiftGammaGain.fx",   "qUINT_lightroom.fx", "CAS.fx", "LumaSharpen.fx",
                                              "AdaptiveSharpen.fx", "Deband.fx" };

// An entry of the panel: an effect file and, for effects added by the user, the technique they picked
struct EffectRef
{
    std::string file;
    std::string technique; // empty: the first technique of the file
};

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
    EffectRef ref;
    std::string technique; // the technique found in ReShade
    bool loaded = false;   // ReShade has it compiled and listed
    bool enabled = false;
    int skipped = 0;       // parameters the panel can't offer (matrices, arrays)
    std::vector<Uniform> uniforms;
};

// A technique ReShade has loaded, for "Add effect"
struct Technique
{
    std::string file;
    std::string name;
};

struct Snapshot
{
    bool moduleFound = false;  // a ReShade dll with the add-on exports is in the process
    std::string version;       // its file version
    bool registered = false;   // ReShade accepted ILION as an add-on
    std::string error;         // why not, in English (translated by the menu)
    bool runtime = false;      // an effect runtime presented a frame recently
    bool effectsOn = false;    // ReShade's own effects on/off state
    std::vector<Effect> effects;        // the panel's entries, in their order
    std::vector<Technique> available;   // every technique ReShade has loaded
    uint64_t revision = 0;     // grows with every read back
    size_t pending = 0;        // requests not applied yet
};

// Looks for ReShade and registers ILION (rate-limited, cheap once registered). Called by the menu, never per frame
// from the game.
void Poll();

Snapshot Get();

// The entries of the panel (catalogue first, then the ones the user added); read back at the next present
void SetPanelEffects(const std::vector<EffectRef>& effects);

void SetEffectsState(bool enabled);
void SetTechnique(const EffectRef& effect, bool enabled);
void SetUniform(const std::string& file, const std::string& name, const float* values, int count);
void ResetUniform(const std::string& file, const std::string& name);
void ResetEffect(const std::string& file);

// Asks ReShade to load an effect file again in the next frame; empty: every effect. A file that is new on disk is
// only picked up by the full reload (checked against ReShade 6.8.0), so the catalogue uses the empty form.
void ReloadEffect(const std::string& file);

// Unregisters from ReShade; before ReShade is unloaded and at process detach
void Shutdown();
} // namespace IlionReShade
