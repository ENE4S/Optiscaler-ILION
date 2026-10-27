// Copyright (c) 2026 3zwr1 (AMDNR). Part of AMDNR (GPL-3.0; see Licenses/AMDNR_NOTICE.txt).
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// What an AMD GPU can run, by its gfx target and compute-unit count, and the APU defaults of AMDNR 0.3.4. Header-only
// and standard library only, so tests\034\gpu_support_test.cpp builds it without the DLL (and against the real
// device_info table). The lmxxf network size rules (the tier snap default on gfx11, [DlssNr] AmdLmxxfTierCap and its
// 360 auto on these APUs) are dlssnr/lmxxf/LmxxfTierPolicy.h.
//
// AmdBridge.cpp's DescribeGpu looks the adapter up in the device_info table (gfx target, compute units) and copies
// ClassifyGpu's answer into AmdBridge::GpuSupport. The note is the GPU line of amd_bridge.log, the Neural tab's GPU text
// and report.txt's. The flags drive text and defaults only: each runtime still decides for itself whether it loads
// (lmxxf's runtime refuses an APU with too few compute units on its own).
#include <string_view>

namespace AmdGpuRules
{
// Owner decision 2026-09-26: APUs with 12 or more compute units - gfx1103 (Z1 Extreme, Z2, Radeon 780M) and gfx1150
// (Z2 Extreme, Radeon 890M) - run lmxxf through AMDNR's RDNA 3 backend, labelled experimental and slow, at the 360p
// network size with the model every 4th frame by default. Verdict::lmxxfExperimental marks them; it is the "handheld"
// of LmxxfTierPolicy.h (gfx1103 / gfx1150 with lmxxfOk). false = 0.3.3.2's refusal of every gfx1103 / gfx115x but
// gfx1151 (the back-out if the small network sizes do not ship: without them such an APU pays the 720 tier).
inline constexpr bool kLmxxfApusExperimental = true;
// Fewer compute units than this: refused (Z1 and Radeon 740M have 4, Radeon 760M 8).
inline constexpr int kLmxxfMinApuComputeUnits = 12;
// [DlssNr] AmdInterleave on such an APU while the ini leaves the key unset (auto): the model every 4th frame.
inline constexpr float kApuInterleave = 4.f;

struct Verdict
{
    bool danielOk = false, lmxxfOk = false;
    // Runs, but untested on this chip and slow: the text says so (lmxxf on the 12+ CU APUs). danielblnc's APU label is
    // not part of 0.3.4 (it goes together with an NR size clamp), so danielExperimental stays false.
    bool lmxxfExperimental = false, danielExperimental = false;
    std::string_view note; // static text
};

// Whether the APU interleave default applies: an lmxxf APU and [DlssNr] AmdInterleave unset (a saved value wins).
inline bool UseApuInterleaveDefault(bool interleaveHasValue, bool lmxxfApu)
{
    return lmxxfApu && !interleaveHasValue;
}

// (r1 review 1) Model interleave Off on an lmxxf APU is kept as 1, not 0. 0 is the key's default, and Save Settings
// writes a value equal to the default as "auto", so a menu Off (or a hand-typed 0) was unset again at the next game
// start and the APU default above came back. 1 also means off (BuildSettings: 1 or less is off; the menu shows it as
// Off) and is saved. Only an explicit 0 on an lmxxf APU; desktop GPUs keep 0.3.3.2's 0 / auto.
inline constexpr float kApuInterleaveOff = 1.f;
inline bool KeepApuInterleaveOff(bool lmxxfApu, bool interleaveHasValue, float interleave)
{
    return lmxxfApu && interleaveHasValue && interleave == 0.f;
}

// The table. `computeUnits` from device_info (0 = unknown). `apusExperimental` is kLmxxfApusExperimental; a parameter
// so the tests can check the back-out too.
inline Verdict ClassifyGpu(std::string_view t, int computeUnits, bool apusExperimental = kLmxxfApusExperimental)
{
    Verdict v;
    if (t.starts_with("gfx12"))
    {
        v.danielOk = v.lmxxfOk = true;
        v.note = "RDNA 4: both runtimes";
    }
    else if (t == "gfx1100" || t == "gfx1101" || t == "gfx1102")
    {
        v.danielOk = v.lmxxfOk = true;
        // danielblnc's cost measured in GTA V Enhanced (0.3.3.2): network 26.8 ms at 320x180, 51.5 ms at 1280x720.
        // lmxxf (0.3.4): 70% of a 1080p render is 1344x756, which the tier snap (on by default on gfx11) feeds at
        // 1280x720, the 720 tier (34 ms on an RX 7800 XT); 0.3.3.2's "67%" was 1286x724, the 900 tier (about 52 ms).
        v.note = "RDNA 3 (RX 7000): both runtimes. lmxxf runs AMDNR's RDNA 3 backend by 3zwr1 (lmxxf's FP8 math "
                 "carried on the F16 matrix units), slower than on RDNA 4: start with NR resolution at 70% or lower (at a "
                 "1080p render the network then runs its 720 tier). danielblnc on RX 7000: about 27 ms per frame even "
                 "at 320x180, about 52 ms at 1280x720 (0.4.0 on an RX 7900 XTX); start at 50-65%";
    }
    else if (t == "gfx1151")
    {
        v.lmxxfOk = true;
        v.note = "Strix Halo (Radeon 8060S / 8050S): lmxxf through AMDNR's RDNA 3 backend by 3zwr1 (FP8 emulated, "
                 "slower than on RDNA 4; start with NR resolution at 70% or lower); danielblnc's runtime has no code "
                 "for this chip";
    }
    else if ((t == "gfx1103" || t == "gfx1150") && apusExperimental)
    {
        if (computeUnits >= kLmxxfMinApuComputeUnits)
        {
            v.lmxxfOk = v.lmxxfExperimental = true;
            // Estimates (no handheld tester yet): the RX 7800 XT's measured 720 tier scaled by compute units, clocks,
            // the smaller register file and the 360 tier's share of the 720 tier's work (the internal design note for it
            // section 7).
            v.note = t == "gfx1103"
                         ? "RDNA 3 APU with 12 compute units (Z1 Extreme, Z2, Radeon 780M): lmxxf through AMDNR's RDNA 3 "
                           "backend by 3zwr1, experimental and slow - about 60-125 ms per network run at the smallest "
                           "(360p) network size, an estimate; untested on this chip. AMDNR starts it at the 360p network "
                           "size with the model every 4th frame by default; the NR cost readout shows the real number. "
                           "danielblnc's runtime has no code for this chip"
                         // device_info maps every 0x150E revision to 16 CUs, the unnamed "AMD Radeon(TM) Graphics"
                         // ones too, so a 12-CU 880M may show as 16 here: the note names no count (the GPU line prints
                         // the table's).
                         : "RDNA 3.5 APU (Z2 Extreme / Radeon 890M, Radeon 880M): lmxxf through AMDNR's RDNA 3 "
                           "backend by 3zwr1, experimental and slow - about 45-95 ms per network run at the smallest "
                           "(360p) network size on a 16-CU 890M (more on a 12-CU 880M), an estimate; untested on this "
                           "chip. AMDNR starts it at the 360p network "
                           "size with the model every 4th frame by default; the NR cost readout shows the real number. "
                           "danielblnc's runtime has no code for this chip";
        }
        else if (computeUnits <= 0)
            v.note = "RDNA 3 APU with an unknown number of compute units: lmxxf needs at least 12 (Z1 Extreme / 780M "
                     "class), so it is not offered here; danielblnc's runtime has no code for this chip";
        else if (computeUnits <= 4)
            v.note = "RDNA 3 APU with 4 compute units (Z1, Radeon 740M): too small for lmxxf's network - about 200-370 ms "
                     "per network run even at the smallest (360p) size, an estimate; lmxxf needs at least 12 compute "
                     "units (Z1 Extreme / 780M class), and danielblnc's runtime has no code for this chip";
        else
            v.note = "RDNA 3 APU with 8 compute units (Radeon 760M): too small for lmxxf's network - about 90-190 ms per "
                     "network run even at the smallest (360p) size, an estimate; lmxxf needs at least 12 compute units "
                     "(Z1 Extreme / 780M class), and danielblnc's runtime has no code for this chip";
    }
    else if (t.starts_with("gfx115") && apusExperimental)
    {
        // gfx1152 (Krackan: Radeon 860M 8 CU, 840M 4 CU) and any later gfx115x: no lmxxf build.
        v.note = "RDNA 3.5 APU (Radeon 860M / 840M): not supported - lmxxf has no build for this chip and danielblnc's "
                 "runtime has no code for it";
    }
    else if (t == "gfx1103" || t.starts_with("gfx115"))
    {
        // 0.3.3.2's text (the back-out).
        v.note = "RDNA 3 / 3.5 handheld APU (Z1 Extreme, 780M, Z2 Extreme, 890M): neither runtime has code for "
                 "this chip - danielblnc's ships gfx1100-1102 and RDNA 4, lmxxf's RDNA 3 build covers desktop RX 7000 "
                 "and Strix Halo; these GPUs are too small for the network";
    }
    else if (t.starts_with("gfx103") || t.starts_with("gfx10"))
    {
        v.note = "RDNA 1 / 2 (RX 5000 / 6000, Steam Deck, 680M): no matrix (WMMA) units, which both runtimes "
                 "need; not supported";
    }
    else
    {
        v.note = "pre-RDNA GPU: not supported";
    }
    return v;
}
} // namespace AmdGpuRules
