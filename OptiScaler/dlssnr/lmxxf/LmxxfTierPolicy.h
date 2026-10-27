// Copyright (c) 2026 3zwr1 (AMDNR). Part of AMDNR (GPL-3.0; see Licenses/AMDNR_NOTICE.txt).
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// lmxxf network size tiers: the host's rules (AMDNR 0.3.4). Header-only and pure (no D3D12, Config or LmxxfNrApi.h),
// so tests\034 builds it without the DLL.
//
// The runtime runs its network at fixed size tiers and picks the smallest box the fed picture fits in
// (runtime/native_network_geometry.h ForInput): the 720 tier (up to 1280x720), 900 (up to 1600x900), else 1080 (up to
// 1920x1080). A tier costs the same whatever part of its box the picture fills. An AMDNR 0.3.4 runtime that the host
// asks for them (the optional export LmxxfNrSetTierPolicy, bit 0) also has two small tiers below those: 360 (up to
// 640x360; network 640x448, 80 tokens) and 576 (up to 1024x576; network 1024x640, 160 tokens). Unasked, the runtime
// has only the three, so every existing size runs as in 0.3.3.2.
//
//   LmxxfTierSnapDefault   [DlssNr] AmdLmxxfTierSnap when the ini has no value: on for gfx11 (RX 7000, Radeon 8060S,
//                          the handheld APUs), off elsewhere (RDNA 4 unchanged). An explicit ini value wins (caller).
//   LmxxfHandheldTarget    the handheld APU targets lmxxf runs on (experimental): gfx1103 (Z1 Extreme / Z2 / 780M)
//                          and gfx1150 (Z2 Extreme / 890M). The caller also needs the GPU's lmxxfOk (the CU floor:
//                          the 4-CU Z1 and the 8-CU 760M are gfx1103 too).
//   NormalizeLmxxfTierCap  [DlssNr] AmdLmxxfTierCap: 0 auto, 360, 576, 720, 900, 1080; anything else = 0.
//   DecideLmxxfTierPolicy  whether the host asks the runtime for the small tiers, and the cap it sizes with.
//   PlanLmxxfSize          the size the network is fed under that policy.
//   LmxxfNetworkTier       the tier a fed size runs at (the menu's cost readout).
//   LmxxfFastCap           Fast mode ([DlssNr] AmdLmxxfFastMode): the tier one below the one the default sizing feeds.
//   PlanLmxxfFastSize      the size Fast mode feeds: the frame planned again with that cap.
#include <algorithm>
#include <cmath>
#include <string_view>

namespace Lmxxf
{
struct TierBox
{
    unsigned w, h;
};
// Ascending. The first kLmxxfSmallTierCount boxes exist only while the runtime has the small tiers on.
inline constexpr TierBox kLmxxfTierBoxes[5] = { { 640u, 360u }, { 1024u, 576u }, { 1280u, 720u }, { 1600u, 900u }, { 1920u, 1080u } };
inline constexpr unsigned kLmxxfSmallTierCount = 2;
inline constexpr unsigned kLmxxfTierCount = 5;

inline bool LmxxfTierSnapDefault(std::string_view gfxTarget)
{
    return gfxTarget.size() >= 5 && gfxTarget.compare(0, 5, "gfx11") == 0;
}

inline bool LmxxfHandheldTarget(std::string_view gfxTarget)
{
    return gfxTarget == "gfx1103" || gfxTarget == "gfx1150";
}

inline int NormalizeLmxxfTierCap(int v)
{
    switch (v)
    {
    case 360:
    case 576:
    case 720:
    case 900:
    case 1080:
        return v;
    default:
        return 0;
    }
}

struct TierPolicy
{
    bool smallTiers = false; // ask the runtime for the small tiers (LmxxfNrSetTierPolicy bit 0). Not `small`: windows.h
                             // (rpcndr.h) defines small as char
    int cap = 0;             // the largest tier the host feeds; 0 = none (the runtime's 1920x1080 ceiling)
    bool autoCap = false;    // the cap is the handheld default (the key was 0 = auto)
};

// capKey = [DlssNr] AmdLmxxfTierCap as read; handheld = an lmxxf-capable handheld APU (LmxxfHandheldTarget and the
// GPU's lmxxfOk); gfx11Desktop = any other gfx11 GPU; gfx11DesktopSmallTiers = the host's switch for the small tiers on
// RX 7000 desktop (off until the owner's look check). The small tiers are asked for:
//   - on a handheld (cap 360 when the key is 0: auto);
//   - on any GPU whose key caps at 360 or 576 (the desktop test hook, and an RX 7000 opt-in);
//   - on gfx11 desktop with the switch on: then no cap, so they are reached only when the NR size itself is that small.
// 720 / 900 / 1080 are a cap alone. Nothing asked and cap 0 = 0.3.3.2's sizing.
inline TierPolicy DecideLmxxfTierPolicy(int capKey, bool handheld, bool gfx11Desktop, bool gfx11DesktopSmallTiers)
{
    TierPolicy p;
    p.cap = NormalizeLmxxfTierCap(capKey);
    if (handheld)
    {
        p.smallTiers = true;
        if (p.cap == 0)
        {
            p.cap = 360;
            p.autoCap = true;
        }
    }
    else if (p.cap == 360 || p.cap == 576)
        p.smallTiers = true;
    else if (gfx11Desktop && gfx11DesktopSmallTiers)
        p.smallTiers = true;
    return p;
}

struct TierPlan
{
    unsigned askedW = 0, askedH = 0; // the NR size fitted into the ceiling box (ModelSize's rule), before any move
    unsigned w = 0, h = 0;           // the size the network is fed
    unsigned from = 0, to = 0;       // tier heights: the one the asked size lands in, the one it is fed in
    bool grown = false;              // stayed in its tier and was grown toward its box
    unsigned ceiling = 1080;         // the largest tier this plan uses (the cap's box, else 1080)
};

// The fed size. renderW x renderH = the frame, nrScale = NR resolution, cap = [DlssNr] AmdLmxxfTierCap (as
// DecideLmxxfTierPolicy returns it), smallTiers = the runtime has the small tiers on, snap = [DlssNr] AmdLmxxfTierSnap.
//   1. The NR size is fitted into the ceiling box, aspect kept: LmxxfBackend.cpp's ModelSize with the cap's box in
//      place of 1920x1080 (the same arithmetic). A cap of 360 / 576 without the small tiers (a runtime without them)
//      caps at 720, the smallest tier such a runtime has: a smaller feed would cost the same with less detail.
//   2. It lands in the smallest box it fits (360 and 576 first with smallTiers).
//   3. Lowered one tier, aspect kept, into the next smaller box:
//      - a step with a small box below it (720 -> 576, 576 -> 360; smallTiers only): when the size is within 110% of that
//        box on both axes (lmxxf 0.31's rule; 576 -> 360 only up to 704x396). The midpoint rule of the step below
//        would lower 832 wide sizes to 640 there (130%): too much detail lost for the cost;
//      - 1080 -> 900 and 900 -> 720: with snap only, when the size's fill of its own box (the larger side ratio) is
//        below the middle of the two boxes (LmxxfBackend.cpp's SnapToNetworkTier, landed FPS-Q1).
//   4. Else grown to fill its box when it fills 90% or more of it (never past the frame): in the small tiers always,
//      in 720 / 900 / 1080 with snap only (SnapToNetworkTier's rule).
// With smallTiers false and no cap (0 or 1080) this is ModelSize, plus SnapToNetworkTier with snap, size for size
// (tests\034\lmxxf_size_plan_test.cpp checks it against LmxxfBackend.cpp's own functions).
inline TierPlan PlanLmxxfSize(unsigned renderW, unsigned renderH, float nrScale, int cap, bool smallTiers, bool snap)
{
    cap = NormalizeLmxxfTierCap(cap);
    if (!smallTiers && (cap == 360 || cap == 576))
        cap = 720;
    const unsigned first = smallTiers ? 0u : kLmxxfSmallTierCount;
    unsigned last = kLmxxfTierCount - 1;
    if (cap != 0)
        while (last > first && kLmxxfTierBoxes[last].h != static_cast<unsigned>(cap))
            --last;
    const TierBox ceil = kLmxxfTierBoxes[last];
    TierPlan r;
    r.ceiling = ceil.h;
    // 1. ModelSize's arithmetic (float), the ceiling box in place of kNetMaxW x kNetMaxH.
    const float s = std::clamp(nrScale, 0.25f, 2.f);
    float fw = renderW * s, fh = renderH * s;
    if (fw > ceil.w || fh > ceil.h)
    {
        const float k = (std::min)(float(ceil.w) / fw, float(ceil.h) / fh);
        fw *= k;
        fh *= k;
    }
    unsigned mw = std::clamp(static_cast<unsigned>(std::lround(fw)), 64u, ceil.w);
    unsigned mh = std::clamp(static_cast<unsigned>(std::lround(fh)), 64u, ceil.h);
    r.askedW = mw;
    r.askedH = mh;
    // 2. Its tier.
    unsigned t = first;
    while (t < last && !(mw <= kLmxxfTierBoxes[t].w && mh <= kLmxxfTierBoxes[t].h))
        ++t;
    const TierBox box = kLmxxfTierBoxes[t];
    r.from = r.to = box.h;
    // 3. Lowered (SnapToNetworkTier's arithmetic, double).
    const double fill = (std::max)(double(mw) / box.w, double(mh) / box.h);
    if (t > first)
    {
        const TierBox below = kLmxxfTierBoxes[t - 1];
        const bool smallStep = t - 1 < kLmxxfSmallTierCount;
        const bool lower = smallStep ? (mw * 10u <= below.w * 11u && mh * 10u <= below.h * 11u)
                                     : snap && fill < 0.5 * (1.0 + double(below.h) / box.h);
        if (lower)
        {
            const double k = (std::min)(double(below.w) / mw, double(below.h) / mh);
            r.w = (std::clamp)(static_cast<unsigned>(std::lround(mw * k)), 64u, below.w);
            r.h = (std::clamp)(static_cast<unsigned>(std::lround(mh * k)), 64u, below.h);
            r.to = below.h;
            return r;
        }
    }
    // 4. Grown.
    r.w = mw;
    r.h = mh;
    if (!(t < kLmxxfSmallTierCount || snap) || fill < 0.9)
        return r;
    const double kBox = (std::min)(double(box.w) / mw, double(box.h) / mh);
    const double kFrame = (std::max)(1.0, (std::min)(double(renderW) / mw, double(renderH) / mh));
    const double k = (std::min)(kBox, kFrame);
    r.w = (std::clamp)(static_cast<unsigned>(std::lround(mw * k)), mw, box.w);
    r.h = (std::clamp)(static_cast<unsigned>(std::lround(mh * k)), mh, box.h);
    r.grown = r.w != mw || r.h != mh;
    return r;
}

// The tier a fed w x h runs at: 360 / 576 only while the runtime has the small tiers on (the menu's readout).
inline int LmxxfNetworkTier(unsigned w, unsigned h, bool smallTiers)
{
    for (unsigned t = smallTiers ? 0u : kLmxxfSmallTierCount; t + 1 < kLmxxfTierCount; ++t)
        if (w <= kLmxxfTierBoxes[t].w && h <= kLmxxfTierBoxes[t].h)
            return static_cast<int>(kLmxxfTierBoxes[t].h);
    return 1080;
}

// Fast mode ([DlssNr] AmdLmxxfFastMode, AMDNR 0.3.4, default off): the network runs one size tier lower than the default
// sizing feeds it. Host only: the runtime, its blocks and its kernels are the default's; only the fed size changes, and
// the edit is lifted to the frame as below 100% NR resolution. Measured on an RX 9070 XT (network hip_ms,
// our measured results): 1080 -> 900 tier 14.87 -> 10.55 ms (-29%), 900 -> 720 -35%, 720 -> 576 -28%.
// The cap one tier below `tier` (the tier the default sizing feeds, a tier height): 1080 -> 900, 900 -> 720, and with
// the small tiers 720 -> 576, 576 -> 360. 0 = no tier below it on this runtime (720 without the small tiers, 360):
// Fast changes nothing there. Anything that is not a tier = 0.
inline int LmxxfFastCap(unsigned tier, bool smallTiers)
{
    for (unsigned t = (smallTiers ? 0u : kLmxxfSmallTierCount) + 1u; t < kLmxxfTierCount; ++t)
        if (kLmxxfTierBoxes[t].h == tier)
            return static_cast<int>(kLmxxfTierBoxes[t - 1].h);
    return 0;
}

// The size Fast mode feeds. defaultTier = the tier the default sizing feeds this frame (LmxxfNetworkTier of its size:
// ModelSize / SnapToNetworkTier, or PlanLmxxfSize with the cap, the small tiers and the snap as they apply); the rest
// as PlanLmxxfSize. The frame is planned again with LmxxfFastCap(defaultTier) as the cap (the same arithmetic as
// [DlssNr] AmdLmxxfTierCap one tier lower: at 1920x1080 and 100% Fast is exactly AmdLmxxfTierCap=900), so the fed size
// lands in exactly that tier (tests\034\lmxxf_size_plan_test.cpp checks it). A user cap needs no rule here: the
// default tier is already at or below it. Returns false, `out` untouched, when LmxxfFastCap is 0.
inline bool PlanLmxxfFastSize(unsigned renderW, unsigned renderH, float nrScale, unsigned defaultTier, bool smallTiers,
                              bool snap, TierPlan& out)
{
    const int cap = LmxxfFastCap(defaultTier, smallTiers);
    if (cap == 0)
        return false;
    out = PlanLmxxfSize(renderW, renderH, nrScale, cap, smallTiers, snap);
    return true;
}
} // namespace Lmxxf
