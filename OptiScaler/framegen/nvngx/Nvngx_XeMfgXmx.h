#pragma once
#include "Nvngx_DllProxy.h"

#include <filesystem>
#include <string>

// ILION v1.4.0 fase 2: XeMFG XMX ([FrameGen] FGNvngxReplacement = XeMFG XMX). realdody's dlssg-to-xefg-xmx
// (github.com/realdody/dlssg-to-xefg-xmx, GPL-3.0; its MFG-Unlocker port is MIT): a DLSSG replacement with the same
// NVSDK_NGX_D3D12_* exports as Nukem's, that runs Intel XeSS frame generation (libxess_fg.dll 1.3.1 / 1.3.4) with the
// ported XMX multi frame model, up to 6X. DirectX 12 only. ILION ships none of it: the user extracts its release into
// OptiScaler\custom\XeMFG XMX (dlssg\dlssg_to_xefg_intel_is_better.dll, its xefg_mfg_programs folder, libxess_fg.dll).
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

    // XeFG creates its heaps and checks the adapter inside these calls: as for ILION's own XeFG, without the vendor
    // spoofing and the heap capture
    NVSDK_NGX_Result D3D12_Init(unsigned long long InApplicationId, const wchar_t* InApplicationDataPath,
                                ID3D12Device* InDevice, const NVSDK_NGX_FeatureCommonInfo* InFeatureInfo,
                                NVSDK_NGX_Version InSDKVersion) override;
    NVSDK_NGX_Result D3D12_Init_Ext(unsigned long long InApplicationId, const wchar_t* InApplicationDataPath,
                                    ID3D12Device* InDevice, NVSDK_NGX_Version InSDKVersion,
                                    const NVSDK_NGX_FeatureCommonInfo* InFeatureInfo) override;
    NVSDK_NGX_Result D3D12_CreateFeature(ID3D12GraphicsCommandList* InCmdList, NVSDK_NGX_Feature InFeatureID,
                                         NVSDK_NGX_Parameter* InParameters, NVSDK_NGX_Handle** OutHandle) override;
    NVSDK_NGX_Result D3D12_EvaluateFeature(ID3D12GraphicsCommandList* InCmdList, const NVSDK_NGX_Handle* InFeatureHandle,
                                           NVSDK_NGX_Parameter* InParameters,
                                           PFN_NVSDK_NGX_ProgressCallback InCallback) override;

    // What is installed, read from the files only (nothing is loaded by it)
    struct Install
    {
        std::filesystem::path dll;   // dlssg_to_xefg_intel_is_better.dll, empty when none was found
        bool programs = false;       // its xefg_mfg_programs folder (the ported XMX programs) next to it
        bool libxessFg = false;      // a libxess_fg.dll next to it (else it looks up to three folders above)
    };
    static Install FindInstall();

    // What its own log says about this session (dlssg_to_xefg_intel_is_better.log next to the DLL, written by it)
    struct Backend
    {
        bool logThisSession = false; // the log was written after this game started
        std::string xefgVersion;     // "XeFG version 1.3.1"
        std::string libxessFg;       // the libxess_fg.dll it tried last
        std::string networkPath;     // "XMX (native), model 16"
        int contextModel = -1;       // model of the last "Creating XeFG context", -1 none
        bool xmxModel = false;       // "Ported XMX MFG model installed"
        std::string lastError;       // the last [error] after the last context creation, empty when none
        bool XmxConfirmed() const { return logThisSession && xmxModel && contextModel == 16 && lastError.empty(); }
    };
    // Re-read at most every two seconds, and only when the file changed
    static const Backend& ReadBackend();

    // The folder of the DLL being loaded or loaded (empty before). Files under it load as they are (LibraryLoad_Hooks):
    // its libxess_fg.dll and programs are its own
    static std::filesystem::path LoadedFolder();
};
