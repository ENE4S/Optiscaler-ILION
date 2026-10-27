// Copyright (c) 2026 3zwr1 (AMDNR). Part of AMDNR (GPL-3.0; see Licenses/AMDNR_NOTICE.txt).
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// danielblnc edit shaper limit rule ([DlssNr] AmdEditShaperLimit, AmdPreSr.cpp shape mode). Header-only and free of
// D3D12 and Config so the unit tests (tests\034) build it without the DLL.
//
// The shaper (AmdPreSr.cpp EditShapeShader, opt-in with [DlssNr] AmdEditShaper=true) bounds the model's edit per pixel
// by `limit x max channel` of the pre-model copy. Which limit it uses is the A/B question of the 0.3.4 style jump:
//   literal (0) the Residual limit as it is - the 0.3.3.2 opt-in shaper, and every out-of-range mode;
//   F1 (1)      no cap at any NR size: the edit is what the model gave, times strength;
//   F2 (2)      continuous at 100%: no cap at or above 100% NR, then limit / t with t = (1 - scale) / 0.5, so the cap
//               tightens as the NR size drops and reaches the Residual limit at 50% (and stays there below).
// A Residual limit of 0 (or less, or not a number) means "no cap" in every mode, as it does in the shader.
#include <algorithm>

namespace AmdPreSr
{
// [DlssNr] AmdEditShaperLimit values.
inline constexpr int kEditShapeLiteral = 0; // Residual limit caps the edit (0.3.3.2)
inline constexpr int kEditShapeF1 = 1;      // the edit is never capped
inline constexpr int kEditShapeF2 = 2;      // the cap ramps from none at 100% NR to Residual limit at 50%

// The limit the edit shaper uses for this frame. `mode` is Settings::editShaperLimit (0..2), `scale` the requested NR
// scale (1 = 100% NR resolution), `residualLimit` Settings::residualLimit. 0 means "no cap".
inline float EditShapeLimit(int mode, float scale, float residualLimit)
{
    if (mode == kEditShapeF1)
        return 0.f;
    if (mode != kEditShapeF2)
        return residualLimit; // literal, and any value outside 0..2 (the bridge already maps those to 0)
    // F2. t is 0 at or above 100% and 1 at 50% and below; a scale that is not a number gives no cap.
    const float t = std::clamp((1.f - scale) / 0.5f, 0.f, 1.f);
    if (!(t > 0.f) || !(residualLimit > 0.f))
        return 0.f;
    return residualLimit / t;
}
} // namespace AmdPreSr
