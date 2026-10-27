// Copyright (c) 2026 3zwr1 (AMDNR). Part of AMDNR (GPL-3.0; see Licenses/AMDNR_NOTICE.txt).
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// The lmxxf neural backend: hosts lmxxf's open-source DLSS 5 runtime (dlssnr/lmxxf/runtime,
// built as LmxxfNrRuntime.dll) behind the same interface the bridge uses for danielblnc's.
//
// How it fits the frame (the whole design, in one place):
//
//   frame N, pre-SR, on the game's command list:
//     1. consume frame N-1's answer: the runtime decodes it (RecordOutputs; the D3D12 queue wait
//        on the HIP result is issued right there, before this list runs) and a small shader
//        writes  edit = answer - what the network was fed  into the temporal pass's residual
//        history, still in frame N-1's pixel space;
//     2. copy this frame's colour to FP16 (and to the model's size when that differs);
//     3. hand that copy to the runtime (PrepareFrame + RecordInputs);
//     4. the temporal pass (preset 9) carries the edit onto THIS frame by motion and jitter,
//        fades it where the geometry changed, adds it to the raw colour: that is the upscaler's
//        input.
//   when the game submits that list: EnqueueHipAsync - HIP waits for the list on the GPU, runs
//   the network, signals. Nothing on the queue waits for it until step 1 of the next frame.
//
// So the network never stalls the frame that fed it, and the game's list is never split. The
// price is one frame of latency on the model's edit, paid by reprojecting the edit rather than
// a picture, which is what the interleave presets already do on their skipped frames.
//
// Colour composition ([DlssNr] AmdComposition). Classic (0) is exactly the above: the runtime
// composes its answer at Detail / Colour strength. RenoDX (1, experimental): step 3 asks the
// runtime for strengths 1/1, latched per job, so its decode hands back RenoDX's `upgraded`;
// step 1 then composes that against what the network was fed (dlssnr/amd/NrCompose.h, the tail
// danielblnc's runtime runs too; W = 1 in fed units) and the edit becomes (composed - fed) / e.
// Refused - Classic, one log line and a menu note - for Network output, display-referred colour,
// final image mode and the runtime's DLSS5_CODEC_SRGB / DLSS5_DEBUG_TINT overrides.
//
// Highlight colour (0.3.3, host-side; the runtime is untouched; the guard and the cap below are
// both off by default in 0.3.3, the self-heal is on). In step 1, where the copy the
// network was fed passed the codec's shoulder (max channel above 0.75, fully from 1.5), the
// answer keeps its light and takes the raw frame's colour, in both compositions ([DlssNr]
// AmdLmxxfHighlightChromaGuard, LmxxfBackend.cpp's ResidualShader): the shoulder squashes such a
// pixel toward white and the decode would hand it back grey. Auto-exposure stops raising the
// exposure while over 8% of the frame is fed past 0.75 ([DlssNr] AmdLmxxfAutoExposureHighlightCap,
// AutoExposureHlsl.h), and a title exposure that blows the feed out is replaced by auto-exposure
// (the self-heal, now also at exposure 1).
#include <d3d12.h>
#include <filesystem>
#include <string>
#include "../amd/AmdPreSr.h"

namespace Lmxxf
{
// Where the files live beside the game and whether they are there. Checked without loading
// anything; AssetsPresent caches a positive answer (a directory walk per frame is not free).
// Two layouts: lmxxf's package (DLSS5-AMD\native-game-tiled-assets with HIP\ inside) and the
// dlss-5-amd-project 1.9.0 one (native-game-tiled-assets + lmxxf-modules + shaders beside the exe).
std::filesystem::path RuntimePath(const std::filesystem::path& directory); // LmxxfNrRuntime.dll
std::filesystem::path AssetsPath(const std::filesystem::path& directory);  // the weights folder
bool RuntimePresent(const std::filesystem::path& directory);
bool AssetsPresent(const std::filesystem::path& directory);
// Full network ([DlssNr] LmxxfFullNetwork): true once the LmxxfNrRuntime.dll in use refused the
// flag in this process (a runtime older than it; the default network runs instead). Set by the
// backend on its recording thread, read by the menu for its note; any thread may call it.
bool FullNetworkRefused();
// R3: -1 runtime not loaded yet, 0 an older LmxxfNrRuntime.dll (keeps about 100 MB per rebuild), 1 buffers reused. Any thread.
int ImportPoolState();
// (0.3.4, AUTOMASK) true once the LmxxfNrRuntime.dll in use refused the character mask controls in this process (a
// runtime older than them, e.g. 0.3.3.2's): [DlssNr] AutoMask=false, Structure intensity and Character structure then
// do not act on lmxxf and its built-in mask runs. For the menu's note, like FullNetworkRefused; any thread.
bool ControlsRefused();
// (0.3.4, runtime work) true while the running lmxxf session has the small network tiers on (360: up to 640x360, 576:
// up to 1024x576; LmxxfTierPolicy.h): the handheld APUs, or [DlssNr] AmdLmxxfTierCap=360 / 576 with a runtime that has
// them. The menu names the tier with Lmxxf::LmxxfNetworkTier(w, h, SmallTierPolicy()). Any thread.
bool SmallTierPolicy();
// The network size cap the lmxxf sizing uses (LmxxfTierPolicy.h): 0 none, else 360 / 576 / 720 / 900 / 1080 (the
// handheld auto is 360; a 360 / 576 cap on a runtime without the small tiers reads 720). Any thread.
int TierCapInUse();
// (0.3.4, lmxxf Fast mode) [DlssNr] AmdLmxxfFastMode as the running lmxxf session applies it (LmxxfTierPolicy.h
// PlanLmxxfFastSize): 0 while it is off (or before the first frame), -1 while it is on but the default size already runs
// the smallest tier this runtime has (720, or 360 with the small tiers: nothing lower), else the tier the default sizing
// feeds (1080 / 900 / 720 / 576): the network then runs the next tier down (LmxxfNetworkTier of the working size). For
// the menu; any thread.
int FastModeFromTier();
// (0.3.4, P3) Late submission (the game hands a frame's list to ExecuteCommandLists only after the next frame's
// Evaluate, TLOU II): -1 while the last stats window (600 frames) saw none, else the share of that window's frames that
// got a fresh network answer, 0..1 (about 0.5 with [DlssNr] AmdLateSubmitGrace on in TLOU II, 0.185 without). For the
// menu's attention line; any thread.
float LateSubmitNrShare();

class Backend final : public AmdPreSr::NeuralBackend
{
    struct Impl;
    Impl* p;

  public:
    Backend(ID3D12Device*, ID3D12CommandQueue*, const std::filesystem::path& directory);
    ~Backend() override;
    ID3D12Resource* Record(ID3D12GraphicsCommandList*, const AmdPreSr::Frame&, const AmdPreSr::Settings&) override;
    AmdPreSr::Stats GetStats() const override;
    int PendingListIndex(UINT, ID3D12CommandList* const*) const override;
    void Submitting(ID3D12CommandQueue*, UINT, ID3D12CommandList* const*) override;
    void Submitted(ID3D12CommandQueue*, UINT, ID3D12CommandList* const*) override;
    void ListReset(ID3D12CommandList*, bool ownBridge) override;
    void TraceBoundary(const std::string&) override;
    bool Ready() override;
    bool Shutdown() override;
    void InvalidateHistory() override;
    void RequestCapture(unsigned delayMs = 0) override;
    std::string Status() const override;
    UINT64 RecordedFrames() const override;
    void NoteLine(const std::string&) override;
    bool Stopped() const override;
};
} // namespace Lmxxf
