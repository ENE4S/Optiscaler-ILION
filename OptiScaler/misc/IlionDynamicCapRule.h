#pragma once

// ILION v1.4.0 R3: when "Use target as cap" of Dynamic MFG may hold the frames. Pure rule (no game state), used by
// IlionDynamicMfg::CapFps and by tests/ilion_dmfg_cap_test.cpp.
//
// The cap belongs to Dynamic MFG: it applies only while Dynamic MFG really runs for the provider in use. With Dynamic
// off (a fixed multiplier) it never touches the frames, whatever the saved "Use target as cap" says. R2 and earlier
// capped a fixed multiplier too (real frames held at target / multiplier). The user's own FPS limit is not this rule's.

namespace IlionDynamicCapRule
{
enum class Provider
{
    None,
    XeFG,        // ILION's XeFG output
    DlssgGame,   // the game's DLSSG (DLSS Enabler...)
    DlssgOutput, // ILION's DLSSG output
    FsrFg,       // ILION's FSR FG output (no Dynamic)
};

struct Inputs
{
    bool targetAsCap = false;        // the saved "Use target as cap"
    bool vulkanBridge = false;       // Vulkan -> D3D12 XeFG bridge: its limiter stays as it was
    float target = 0.0f;             // Dynamic MFG's final FPS target, 0 none
    bool ownRunning = false;         // ILION's own FG output generates
    bool xefgDynamic = false;        // XeFG Dynamic MFG R3 runs (XeFG_DynamicMFG::Allowed)
    float xefgControllerCap = 0.0f;  // the cap R3's controller holds (it accounts for the automatic target)
    bool dlssgOutputDynamic = false; // ILION's DLSSG output in Dynamic (FGDLSSGForceDMFG)
    bool ilionDrivesExternal = false; // ILION Dynamic drives the game's DLSSG (mode Ilion, not refused)
    bool externalGenerating = false; // the game's DLSSG generated in the last second
};

// The final FPS the limiters hold, 0 none
inline float Cap(Provider provider, const Inputs& in)
{
    if (!in.targetAsCap || in.vulkanBridge)
        return 0.0f;

    switch (provider)
    {
    case Provider::XeFG:
        return in.xefgDynamic && in.ownRunning && in.xefgControllerCap > 0.0f ? in.xefgControllerCap : 0.0f;
    case Provider::DlssgOutput:
        return in.dlssgOutputDynamic && in.ownRunning && in.target > 0.0f ? in.target : 0.0f;
    case Provider::DlssgGame:
        // Only ILION's own Dynamic: the backend's eDynamic holds the target itself, a fixed multiplier is never capped
        return in.ilionDrivesExternal && in.externalGenerating && in.target > 0.0f ? in.target : 0.0f;
    case Provider::FsrFg:
    default:
        return 0.0f;
    }
}
} // namespace IlionDynamicCapRule
