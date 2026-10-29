// Copyright (c) 2026 3zwr1 (AMDNR)
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <d3d12.h>
#include <nvsdk_ngx.h>
#include <string>
#include "AmdPreSr.h"

namespace DlssNr::AmdBridge
{
// Completion-backed activity, for the NR lamp only. No waits or global frame pacing.
bool RecentlyActive();
void CancelActivity(ID3D12CommandList* list = nullptr);
bool HasFiles();
// The neural runtime the user chose ([DlssNr] NrBackend) and what is installed next to the
// game. Unchosen + both installed = the menu asks on launch (RuntimeChoiceNeeded); one
// installed = that one runs.
enum class NeuralRuntime { Unchosen, Daniel, Lmxxf };
NeuralRuntime ChosenRuntime();
bool LmxxfAssetsPresent();    // DLSS5-AMD\native-game-tiled-assets: weights, HLSL, HIP modules
bool LmxxfRuntimePresent();   // LmxxfNrRuntime.dll beside OptiScaler.dll (or in DLSS5-AMD\)
bool LmxxfReady();            // both: the lmxxf backend can be built
bool AnyRuntimePresent();     // danielblnc's files or the lmxxf pair: the AMD path has something to run
bool LmxxfWanted();           // the backend to build is lmxxf: chosen, or the only runtime installed
// hipRuntimeGetVersion of the driver's amdhip64_7.dll after hipInit, cached (working systems answer
// 70260201); -1 when HIP cannot be used at all (the DLL does not load, hipInit fails, or no version
// answer), 0 only when the runtime really answers 0. Informational, except that -1 sends the lmxxf
// choice to danielblnc's runtime. The "HIP runtime 0" in danielblnc's log is a field its DLL never
// fills under OptiScaler, not the driver's answer; the Indiana Jones freeze blamed on it was the
// Vulkan bridge's ordering (see AmdBridge.cpp).
int HipRuntimeVersion();
int HipDriverVersion();       // hipDriverGetVersion, cached; -1 when not exported or HIP is unavailable
// lmxxf_vk_launch.pending beside OptiScaler.dll: the last lmxxf session on a Vulkan title stopped
// before its first answer, so a Vulkan title runs danielblnc's runtime (or no pass) until the file
// is deleted. Cheap for the menu: the answer is cached for about a second.
bool LmxxfVkLaunchPending();
bool DanielWeightsPresent();  // dlssnr_on_amd_weights.bin beside the pass DLLs
bool RuntimeChoiceNeeded();
// The runtime the process actually built its backend for (Unchosen until one exists). A
// choice changed after that takes effect on the next game start.
NeuralRuntime ActiveRuntime();
// What the GPU NR runs on can run, from its gfx target (device_info table): the adapter the backend
// was built on once one exists (0.3.3.2: on a hybrid PC that can be the dGPU while the primary GPU is
// the iGPU, or the reverse), the primary GPU before that. danielblnc's runtime
// carries code objects for gfx1100/1101/1102 (RX 7000 desktop and mobile) and gfx1200/1201
// (RX 9000); lmxxf runs RDNA 4 natively and RDNA 3 through its gfx11 build (FP8 emulated, modules
// for gfx1100/1101/1102 and gfx1151 in the pak; 0.3.4 adds the APUs gfx1103 and gfx1150 with 12 or
// more compute units - Z1 Extreme / Z2 / 780M, Z2 Extreme / 890M - as experimental). The smaller
// APUs (Z1, 740M, 760M, gfx1152 860M / 840M) and RDNA 2 (RX 6000, Steam Deck, 680M) have no
// runtime; the menu says so instead of failing quietly. The table is dlssnr/amd/GpuSupportRules.h.
struct GpuSupport
{
    std::string name, target; // "AMD Radeon 780M Graphics", "gfx1103"
    bool amd = false, apu = false, known = false, danielOk = false, lmxxfOk = false;
    std::string note;
    // (0.3.4) Runs, but slow and untested on this chip: the text says so (lmxxf on the 12+ CU APUs; danielblnc's
    // APU label is not part of 0.3.4). An lmxxf APU (lmxxfExperimental) also gets the APU defaults: the model every
    // 4th frame while [DlssNr] AmdInterleave is unset (AmdBridge.cpp), and the 360p network cap as AmdLmxxfTierCap's
    // auto (LmxxfTierPolicy.h's "handheld"). computeUnits from the device table, 0 = unknown.
    bool lmxxfExperimental = false, danielExperimental = false;
    int computeUnits = 0;
};
const GpuSupport& GpuSupportInfo();
// (0.3.4) The vendor of this device's physical adapter (adapter spoofing skipped, as NR's own device check):
// true for AMD. One DXGI query per call; callers cache it (AMDNR Screen GI checks the game's device once).
bool DeviceIsAmd(ID3D12Device* device);
// (0.3.4) [DlssNr] AmdLmxxfTierSnap as it applies: the ini's true / false when it holds one, else on for gfx11 (RX
// 7000, Radeon 8060S / 8050S, the APUs above) and off elsewhere (RDNA 4 unchanged; LmxxfTierSnapDefault in
// dlssnr/lmxxf/LmxxfTierPolicy.h). lmxxf only. The value BuildSettings hands the backend (Settings::lmxxfTierSnap);
// the menu reads this, not the key (whose value_or_default() is false when unset).
bool LmxxfTierSnapOn();
// `epoch` (0.3.3.2): the presented-frame counter the call belongs to - the D3D11 / Vulkan bridge's own
// frame count when it passes its queue, else the swapchain's present count (the NVIDIA path's
// SubmissionEpoch rule). 0 = unknown. Only [DlssNr] AmdOneStreamPerFrame reads it.
bool Before(ID3D12GraphicsCommandList*, NVSDK_NGX_Parameter*, ID3D12CommandQueue*, unsigned long long epoch = 0);
// After a native Ray Reconstruction evaluate (FSR Ray Regeneration on this path): the model
// runs on the OUTPUT at display resolution and the backend writes the result back into it.
// Same return contract as Before: false = not ours (no files / not an AMD device).
bool After(ID3D12GraphicsCommandList*, NVSDK_NGX_Parameter*, ID3D12CommandQueue*, unsigned long long epoch = 0);
// (0.3.3.2, [DlssNr] AmdNeuralListRecovery) One of OptiScaler's own bridges closed `list` and will not
// execute it (the D3D11 bridge after a failed upscale, the Vulkan bridge after a failed Close or copy
// Wait): the backend treats it as discarded. Nothing happens with the key off or no backend built.
void ListDiscarded(ID3D12CommandList* list);
// Whether list recovery is active in this process: the key was on when the backend was built (the
// Reset hook is installed then, once). Read by the backends on their recording thread.
bool ListRecoveryOn();
void Restore(NVSDK_NGX_Parameter*);
bool HasReplacement(NVSDK_NGX_Parameter*);
// The colour Before put into this parameter block in place of the title's (the NR replacement), or nullptr when the
// block carries none on this thread. Contract: Record's result is in D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
// arrives there and must leave there - the upscaler wrappers apply no title colour barrier to it (C3-A, 0.3.3.2
// rebuild; FFX's FFX_API_RESOURCE_STATE_COMPUTE_READ relies on the same). Cleared by Restore.
// (0.3.4 preview) Colour chain: without an NR replacement this falls through to AMDNR Screen GI's output (same
// state contract), so HasReplacement/Replacement also cover GI with NR off; Restore puts the title's colour back
// after both links. With [AmdGi] Enabled=false GI holds nothing and all three behave as before.
ID3D12Resource* Replacement(const NVSDK_NGX_Parameter*);
void InvalidateHistory();
// The upscaler returned without writing this frame (C6-A, 0.3.3.2 rebuild; NVNGX_DLSS_Dx12.cpp): no neural pass runs
// after it, and NR history restarts on the next frame (both runtimes). One amd_bridge.log line per process. No-op
// before a backend exists.
void UpscalerSkipped();
void RequestCapture(unsigned delayMs = 0);
void TraceContextRelease(unsigned int handle, bool after);
std::string Status();
// Live counters for the menu readout; zeroed when the backend is not up.
AmdPreSr::Stats Stats();
// pass1 SHA name ("0.3.0" / "0.3.1" / …) or nullptr if missing/unknown.
// Cached for menu display until the DLL path, size, or write time changes.
// (0.3.4, danielblnc support) A known build whose row is named by its digest prefix returns the version its own file carries
// (read at run time), so it matches no row name: ask DanielKnobsMapped / DanielFastModeSupported, not the name.
const char* RuntimeName();
// (0.3.4, danielblnc support) Capabilities of the danielblnc build the bridge identified (the loaded pass DLL, else pass 1 of
// the folder identified once, as RuntimeName does), answered from its layout, never by name: -1 = no danielblnc pass
// DLL here yet, 0 = the build has not got it (an unknown build included), 1 = it has.
//   DanielKnobsMapped: the runtime knobs Style / ToneCurve / ToneLift / UseGameExposure (AmdLayout KnobsUnmapped).
//   DanielFastModeSupported: the quality mode Fast / Reference ([DlssNr] AmdDanielFastMode; AmdLayout QualityMapped).
// DanielFastModeRuntimeOwn: the runtime's own mode, read once when it loaded (before any host write): 1 Fast,
// 0 Reference, -1 not known yet (not loaded, or no such mode). With the key unset this is what runs.
// Any thread; RuntimeName's once-only probe is the only file access.
int DanielKnobsMapped();
int DanielFastModeSupported();
int DanielFastModeRuntimeOwn();
// The settings the bridge would hand the backend right now, for final image mode.
AmdPreSr::Settings CurrentSettings();
// Whether the bridge owns a backend. Final image mode stays idle while it does: the pass
// DLLs and the HIP runtime are process-global, so two backends is a conflict.
bool BackendActive();
// One line for the menu about the colour composition ([DlssNr] AmdComposition): why RenoDX mode
// is refused (display-referred input, Network output, an lmxxf override...) or what it is doing.
// Empty = nothing to say. Both backends set it from their recording thread when RenoDX mode is
// refused or active and clear it ("") in Classic; the menu reads it every frame. Thread-safe.
void SetCompositionNote(const std::string& note);
std::string CompositionNote();
// Memory figures for a backend's log line, both runtimes the same:
// " vramMB=<DXGI LOCAL CurrentUsage> budgetMB=<DXGI LOCAL Budget> privateMB=<process PrivateUsage>"
// (MiB, leading space, "?" for a figure that could not be read). Thread-safe; after the first call
// it costs one QueryVideoMemoryInfo and one GetProcessMemoryInfo. See AmdBridge.cpp for why DXGI.
std::string MemoryTelemetry(ID3D12Device* device);
// NR switched on or off (source "key" or "menu"): one line in amd_bridge.log and, with a backend
// built, in its own log (amd_presr.log / lmxxf_backend.log) next to the Record lines (0.3.3.2).
void NoteToggle(bool on, const char* source);
// The built backend's runtime stopped for this session (danielblnc refused or failed; lmxxf failed to
// load/create or poisoned). False with no backend built. For the On notice (0.3.3.2).
bool RuntimeStopped();
// (0.3.4, P1, [DlssNr] AmdStreamlineDeviceFix) danielblnc's backend was built on the device behind the game's proxy
// device (a Streamline interposer proxy; ComIdentity.h, ChooseNrDevice): its Submitting keeps the queue when the
// game submits through the proxy of it (AmdPreSr.cpp). False otherwise, and before a backend exists.
bool NrOnDeviceBehindProxy();
// (0.3.4, P1) Why danielblnc's runtime was not started in this session although its files are installed: the game's
// device and its queue's device differ and neither was shown to wrap the other. Empty otherwise. Thread-safe.
std::string NrDeviceRefusal();
// danielblnc's standalone DLSS-NR on AMD found beside the game (version.dll, winhttp.dll, dxgi.dll ...
// or his installer dlssnr_on_amd_setup*.exe, 0.3.4): what to tell the player, or empty. Checked once,
// off-thread, when the backend is built (0.3.3.2). Thread-safe.
std::string ForeignStandalone();
// This process's session header ("AMDNR session start: <local time>, tick, pid, exe, product"),
// the first line of OptiScaler.log; the AMD logs write it before their first line (0.3.3.2).
std::string SessionHeader();
// VER_PRODUCT_NAME: "AMD-NR v<ver> / OptiScaler v<ver> (<build stamp>)".
const char* ProductName();
// ILION dev.51: what the dynamic NR resolution controller (DynamicNr.h, [DlssNr] AmdDynamicRes) is doing, for the
// menu. A copy of its state after its last frame; reading it changes nothing. running = it ran in the last second.
struct DynamicNrReadout
{
    bool running = false;
    float scale = 1.f;       // the NR scale it hands the backend this frame
    float ceiling = 1.f;     // the highest scale it uses: the manual NR resolution, 100% at most (its steps start there)
    float configured = 1.f;  // the manual NR resolution as set (ILION dev.51-exp2: above 100% it is not reached)
    float floor = 0.5f;      // its lowest step (ILION dev.55: the profile's)
    bool fast = false;       // ILION dev.55: on Smooth's Fast step (lmxxf's Fast mode at the lowest resolution)
    bool fastAllowed = false; // ILION dev.55: the profile has that step (Smooth with lmxxf)
    double averageMs = 0.0;  // its frame time average (real frames, the ones NR runs on)
    double targetMs = 0.0;   // 1000 / target FPS
    unsigned changes = 0;    // scale changes this session
    bool held = false;       // lmxxf change cap reached: no more changes until the game restarts
    bool upBlocked = false;  // a step up could not hold: up-steps wait
    int targetFps = 0;       // the target it worked with on that frame
    int reason = 0;          // IlionDynamicNr::Reason (misc/IlionDynamicNrReason.h): why it keeps, lowers or raises
};
DynamicNrReadout DynamicNrState();
} // namespace DlssNr::AmdBridge

namespace AmdPreSr
{
// danielblnc's version of a DLL, or empty when it is not his: a known build by SHA256, or his
// overlay's "<x.y.z>   (End to close)" text in an unknown one. Reads the whole file (up to 64 MB):
// never on the render thread. Defined in AmdPreSr.cpp (0.3.3.2, the standalone check).
std::string DanielblncBuildOf(const std::filesystem::path& file);
} // namespace AmdPreSr

// Global-scope alias for translation units where `DlssNr` names the NVIDIA-path class.
void AmdNrRequestCapture(unsigned delayMs);
