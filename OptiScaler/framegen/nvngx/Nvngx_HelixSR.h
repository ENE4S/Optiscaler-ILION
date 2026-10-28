#pragma once
#include "Nvngx_DllProxy.h"

#include <filesystem>
#include <string>

// ILION dev.51-exp2: NVIDIA FG Lite (experimental). HelixSR 2.0.0's frame generation as a DLSSG provider ([FrameGen]
// FGNvngxReplacement = HelixSR): NVIDIA's DLSS frame generation network (310.9.16 light mode by default) run by
// HelixSR's own DLL on GPUs that are not NVIDIA's. ILION ships none of it: the user installs HelixSR 2.0.0, whose setup
// builds the NVIDIA parts on their PC (they are for that PC only). The loading follows HelixSR Scaler 1.1.0
// (github.com/lonewolf0622/HelixSR-Scaler, GPL-3.0): HelixSR-fsrfg\amd_fidelityfx_dx12.dll exporting
// HelixSR_NgxFgVersion (required: dev.52). DirectX 12 only, one generated frame.
class Nvngx_HelixSR : public Nvngx_DllProxy
{
  protected:
    void LoadLibraries() override final;

  public:
    Nvngx_HelixSR() { LoadLibraries(); }

    int getMaxFakeFramesCount() override { return 1; }
    FGNvngxReplacement getType() override { return FGNvngxReplacement::HelixSR; }

    // What the menu shows before anything is loaded: files only, no DLL is loaded by it
    struct Install
    {
        std::filesystem::path dll;  // the DLL that would be loaded, empty when none was found
        bool fgKernels = false;     // a helixsr-fg folder next to it (what the setup built for frame generation)
        std::string preset;         // helixsr.ini [DlssFrameGeneration] Preset as written ("" = default, light)
        bool liteFiles = false;     // helixsr-fg\310.9.16 has nvngx_dlssg.dll and hxfg_data.bin (the light mode's files)
        bool lite() const { return preset.empty() || _stricmp(preset.c_str(), "c") == 0; }
        // What HelixSR runs: the light mode (310.9.16) when asked for and its files are there, else the full one
        // (310.3), as HelixSR itself decides ("preset C not available ... 310.3 (full) instead" in helixsr.log)
        bool runsLite() const { return lite() && liteFiles; }
    };
    static Install FindInstall();
    // Writes [DlssFrameGeneration] Preset = c into the helixsr.ini next to that DLL (NVIDIA FG Lite). False on failure
    static bool UseLite(const Install& install);

    // The version the loaded DLL reports (HelixSR_NgxFgVersion), 0 when none is loaded
    static uint32_t LoadedVersion();

    // ILION dev.51-exp3: the folder of the HelixSR DLL being loaded or loaded (empty before). Files under it load as
    // they are (LibraryLoad_Hooks): HelixSR loads NVIDIA's NGX core (helixsr-fg\_nvngx.dll) by its full path, and
    // ILION answering that with itself made HelixSR call ILION in a loop, the game hung at start
    static std::filesystem::path LoadedFolder();
};
