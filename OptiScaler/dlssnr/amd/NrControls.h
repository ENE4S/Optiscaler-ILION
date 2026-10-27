// Copyright (c) 2026 3zwr1 (AMDNR). Part of AMDNR (GPL-3.0; see Licenses/AMDNR_NOTICE.txt).
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// lmxxf native character mask controls: the control values the host sends to the runtime (plan item AUTOMASK).
// Header-only and free of D3D12, Config and LmxxfNrApi.h, so the unit tests (tests\034) build it without the DLL.
//
// lmxxf's first layer takes four constant features that the shipped network fixes at 1: tone, structure, skin and
// other (lmxxf packed input slots 7/10/11/14). skin and other carry the model's own character/scene split, which is
// what [DlssNr] AutoMask switches. The runtime (LmxxfNrRuntime, LMXXF_NR_FRAME_FLAG_CONTROLS) scales each feature's
// column of the network's input mix by the value it gets, so 1 leaves the shipped picture. This header holds only the
// host policy: which values follow from the AMD settings.
//
//   inputs   autoMask   Settings::autoMask ([DlssNr] AutoMask, default true)
//            structure  S = Settings::structure ([DlssNr] LocalStructure, "Structure intensity", default 1)
//            skin       K = Settings::skin ([DlssNr] SkinStructure, "Character structure"; the bridge already
//                       resolves a negative value to S, AmdBridge.cpp BuildSettings)
//            S and K are clamped to [0, 4] (a value that is not a number counts as 1, the shipped strength).
//   mask on  tone 1, structure 1, skin K, other S
//   mask off tone 1, structure S, skin -1, other -1   (-1, not 0: in the W3-C lab 0 moved the picture about five
//            times more than -1, so -1 is taken as the off state; see the internal lab note for it)
//   tone stays 1 in 0.3.4; the style feature (slot 6, shipped 1/128) is not a host control in 0.3.4 and is sent as 1
//   (= keep the shipped value) whenever the flag is sent.
//
// The defaults (AutoMask true, S 1, K -1 -> 1) give (1, 1, 1, 1) = BuiltIn(): lmxxf's shipped vector. Then the host
// sends the 0.3.3.2 frame (struct_size 128, no LMXXF_NR_FRAME_FLAG_CONTROLS) and the weights are not touched, so a
// runtime that predates the controls (0.3.3.2, 1343bbc5) keeps working. Only a value other than the built-in one
// needs the flag and the grown struct (sizeof(LmxxfNrFrameInfo), 144).
#include <algorithm>
#include <cmath>

namespace AmdPreSr
{
// Range of S and K the host accepts (the AMD sliders' range); the runtime itself accepts [-1, 4].
inline constexpr float kNrControlMin = 0.f;
inline constexpr float kNrControlMax = 4.f;
// skin and other with the character mask off.
inline constexpr float kNrControlMaskOff = -1.f;

struct NrControls
{
    float tone = 1.f;
    float structure = 1.f;
    float skin = 1.f;
    float other = 1.f;
    // Not a host control in 0.3.4: 1 keeps the shipped style feature. Never send 0 here by accident (a value-
    // initialised LmxxfNrFrameInfo holds 0, which the runtime takes as a real value); WriteNrControls copies this 1.
    float style = 1.f;

    // lmxxf's shipped vector: the host sends no flag and the 128-byte frame.
    constexpr bool BuiltIn() const
    {
        return tone == 1.f && structure == 1.f && skin == 1.f && other == 1.f && style == 1.f;
    }
    constexpr bool operator==(const NrControls& o) const
    {
        return tone == o.tone && structure == o.structure && skin == o.skin && other == o.other && style == o.style;
    }
    constexpr bool operator!=(const NrControls& o) const { return !(*this == o); }
};

// S or K as the runtime gets it: [0, 4]; not a number (or infinite) -> 1.
inline float NrControlStrength(float v)
{
    if (!std::isfinite(v))
        return 1.f;
    return std::clamp(v, kNrControlMin, kNrControlMax);
}

// The control values for the AMD settings. `structure` = Settings::structure (S), `skin` = Settings::skin (K, a
// negative value already resolved to S by the bridge).
inline NrControls ResolveNrControls(bool autoMask, float structure, float skin)
{
    const float s = NrControlStrength(structure);
    NrControls c;
    if (autoMask)
    {
        c.structure = 1.f;
        c.skin = NrControlStrength(skin);
        c.other = s;
    }
    else
    {
        c.structure = s;
        c.skin = kNrControlMaskOff;
        c.other = kNrControlMaskOff;
    }
    return c;
}

// Copies the controls into a LmxxfNrFrameInfo (template, so this header does not need LmxxfNrApi.h). The caller sets
// struct_size = sizeof(LmxxfNrFrameInfo) and LMXXF_NR_FRAME_FLAG_CONTROLS only when !c.BuiltIn(); for the built-in
// values it sends 128 bytes and no flag, and need not call this.
template <class FrameInfo> inline void WriteNrControls(FrameInfo& fi, const NrControls& c)
{
    fi.control_tone = c.tone;
    fi.control_structure = c.structure;
    fi.control_skin = c.skin;
    fi.control_other = c.other;
    fi.control_style = c.style;
}
} // namespace AmdPreSr
