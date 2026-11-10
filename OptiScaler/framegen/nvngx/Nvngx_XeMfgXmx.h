#pragma once
#include "Nvngx_DllProxy.h"

#include <filesystem>
#include <string>

// ILION v1.4.0: XeMFG XMX ([FrameGen] FGNvngxReplacement = XeMFG XMX). realdody's dlssg-to-xefg-xmx
// (github.com/realdody/dlssg-to-xefg-xmx, GPL-3.0; its MFG-Unlocker port is MIT): a DLSSG replacement with the same
// NVSDK_NGX_D3D12_* exports as Nukem's, that runs Intel XeSS frame generation with the ported XMX multi frame model,
// up to 6X. DirectX 12 only, NVIDIA, Intel and AMD GPUs. ILION ships libxess_fg.dll 1.3.4 (OptiScaler\libxess_fg.dll),
// the build realdody's direct XeFG context needs on AMD (with 1.3.1 its Init fails there: "Failed to create heap");
// realdody's release itself is not included: the user extracts it into OptiScaler\custom\XeMFG XMX
// (dlssg\dlssg_to_xefg_intel_is_better.dll and its xefg_mfg_programs folder).
// It is loaded and called through the same DLSSG replacement path as Nukem's / DLSS Enabler; its metrics are ILION's
// own for the game's DLSSG (IlionDynamicMfg), nothing parallel. Its log is read only to tell which XeFG path runs.
class Nvngx_XeMfgXmx : public Nvngx_DllProxy
{
  protected:
    void LoadLibraries() override final;

  public:
    Nvngx_XeMfgXmx() { LoadLibraries(); }

    int getMaxFakeFramesCount() override { return 5; }
    FGNvngxReplacement getType() override { return FGNvngxReplacement::XeMfgXmx; }

    // Called as OptiScaler v10 calls a custom DLSSG replacement (Nvngx_DllProxy): only CreateFeature is wrapped, for its
    // error in ILION's log
    NVSDK_NGX_Result D3D12_CreateFeature(ID3D12GraphicsCommandList* InCmdList, NVSDK_NGX_Feature InFeatureID,
                                         NVSDK_NGX_Parameter* InParameters, NVSDK_NGX_Handle** OutHandle) override;

    // Its plugin API name, as OptiScaler v10 names a custom replacement (its folder): its settings are in
    // [Plugin.XeMFG XMX] of OptiScaler.ini, the same section OptiScaler v10 uses
    static constexpr const char* PluginName = "XeMFG XMX";

    // realdody's network paths (its Plugin::Path, [Plugin.XeMFG XMX] NetworkPath). XmxNative is its default: on a GPU
    // without Intel's XMX it runs the ported model 16 (DXIL, or CUDA on an RTX); Fallback is XeFG's DP4a model 18
    enum class NetworkPath : int32_t
    {
        XmxPort = 0,
        Cuda = 1,
        Fallback = 2,
        XmxNative = 3,
    };
    // The saved one (its default when none): what it reads at its OptiPluginInit, at the next start
    static NetworkPath SavedNetworkPath();
    // Saved to the ini at once; applied at the next start (its own tab switches it live)
    static void SaveNetworkPath(NetworkPath path);
    static const char* NetworkPathName(NetworkPath path);
    // OptiPluginInit was called and it accepted the API (its tab is in the menu)
    static bool PluginApiAccepted();

    // What is installed, read from the files only (nothing is loaded by it)
    struct Install
    {
        std::filesystem::path dll;   // dlssg_to_xefg_intel_is_better.dll, empty when none was found
        bool programs = false;       // its xefg_mfg_programs folder (the ported XMX programs) next to it
        bool libxessFg = false;      // a libxess_fg.dll next to it (else it looks up to three folders above)
    };
    static Install FindInstall();

    // What its own log says about this session (dlssg_to_xefg_intel_is_better.log next to the DLL, written by it and
    // flushed line by line). Its code (FrameGenerator::CreateContext): an XeFG context is created on the first evaluation
    // and logs "Creating XeFG context"; when XeFG's Init fails it logs the error and makes no other context until the
    // output size, format or path changes, and its evaluations return without generating
    struct Backend
    {
        bool logThisSession = false; // the log was written after this game started
        std::string xefgVersion;     // "XeFG version 1.3.1"
        std::string libxessFg;       // the libxess_fg.dll it tried last
        std::string networkPath;     // "XMX (native), model 16"
        int contextModel = -1;       // model of the last "Creating XeFG context", -1 none
        bool xmxModel = false;       // "Ported XMX MFG model installed"
        std::string contextError;    // the first [error] after the last context creation (its cause), empty when none
        std::string lastError;       // the last [error] after it ("XeFG Init error: -1000"), empty when none
        // Another libxess_fg.dll loaded in the game, of another version than the one ILION uses ("path (1.1.0)"), empty
        // when none. A copy of the same version (a game folder updated by hand) is no conflict
        std::string otherLibxessFg;
        // XeFG 1.3.1 in use: on AMD realdody's context fails with it (1.3.4 needed)
        bool OldXefg() const { return xefgVersion.starts_with("1.3.1"); }
        bool pluginApi = false;      // "OptiScaler plugin API version N": it got the plugin API
        bool ContextFailed() const { return logThisSession && contextModel >= 0 && !lastError.empty(); }
        // A context made without an error after it: XeFG generates with it (model 16 XMX port, 18 DP4a, 17 Intel XMX)
        bool ContextOk() const { return logThisSession && contextModel >= 0 && lastError.empty(); }
        bool XmxConfirmed() const { return logThisSession && xmxModel && contextModel == 16 && lastError.empty(); }
    };
    // Re-read at most every two seconds, and only when the file changed. Thread safe: a copy
    static Backend ReadBackend();

    // The folder of the DLL being loaded or loaded (empty before). Files under it load as they are (LibraryLoad_Hooks):
    // its libxess_fg.dll and programs are its own
    static std::filesystem::path LoadedFolder();
};
