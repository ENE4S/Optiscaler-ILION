#pragma once
#include <proxies/Nvngx_DllProxy.h>

#include <filesystem>

// ILION v1.4.1.1: ANR FG ([FrameGen] FGNvngxReplacement = Apex Neural Rendering FG). Apex Compute's neural frame
// generation (github.com/apex-compute/ANR, its OptiScaler add-on), up to 6X: a DLSSG replacement with the same
// NVSDK_NGX_D3D12_* exports as Nukem's, loaded from OptiScaler\custom\Apex Neural Rendering FG\dlssg\nvngx_dlssg.dll,
// the folder its installer uses for OptiScaler v10's custom providers. ILION ships none of it. DirectX 12 in ILION:
// its Vulkan entry points are not taken (ILION's Vulkan FG paths stay as they are). Its metrics are ILION's own for the
// game's DLSSG (IlionDynamicMfg); it draws nothing and adds no keys
class Nvngx_ApexFg : public Nvngx_DllProxy
{
  protected:
    void LoadLibraries() override final;

  public:
    Nvngx_ApexFg() { LoadLibraries(); }

    int getMaxFakeFramesCount() override { return 5; }
    FGNvngxReplacement getType() override { return FGNvngxReplacement::ApexFg; }

    // The name its installer writes to OptiScaler.ini, the same as OptiScaler v10's custom provider folder
    static constexpr const char* ConfigName = "Apex Neural Rendering FG";

    // Its nvngx_dlssg.dll, read from the files only (nothing is loaded); empty when it isn't installed
    static std::filesystem::path FindDll();

    // What its newest log (nr_fg-*.log next to its DLL, written this session) says ran: the model size and the
    // convolution path order. Re-read at most every two seconds. Empty fields when it doesn't say
    struct Run
    {
        std::string model; // "large"
        std::string path;  // "portable"
    };
    static Run ReadLog();
};
