// ILION dev.57: the new interface of the menu, the approved prototype (ILION-V1.2-dev-trabajo/prototipo-ui) drawn
// inside the game. Included by menu_common.cpp only, it uses its helpers and the tabs it already has: every control
// shown is the real one of ILION, every state is read from what runs. Nothing here is sample data.
//
// Geometry: the prototype's own numbers, a window of 1600 x 1000 units, scaled to the screen. P() turns a point of the
// prototype into a point of the screen, U() a length; Q() is the way back.

#pragma execution_character_set(push, "utf-8")

namespace IlionShell
{
constexpr float W = 1600.0f;
constexpr float H = 1000.0f;

static float S = 1.0f;
static ImVec2 O;
static ImFont* fUi = nullptr;    // Bahnschrift, as the theme loads it
static ImFont* fBrand = nullptr; // Russo One, in the dll

inline float U(float v) { return v * S; }
inline ImVec2 P(float x, float y) { return ImVec2(O.x + x * S, O.y + y * S); }
inline ImVec2 P(const ImVec2& v) { return P(v.x, v.y); }
inline ImVec2 Q(const ImVec2& px) { return ImVec2((px.x - O.x) / S, (px.y - O.y) / S); }
inline ImVec2 Sz(float w, float h) { return ImVec2(U(w), U(h)); }
inline void At(float x, float y) { ImGui::SetCursorScreenPos(P(x, y)); }

// Text of the new interface in the four languages of the menu (the rest of the menu keeps Tr)
static const char* Lx(const char* en, const char* es, const char* pt, const char* ru)
{
    switch (Ilion::CurrentLang())
    {
    case Ilion::Lang::ES:
        return es;
    case Ilion::Lang::PT:
        return pt;
    case Ilion::Lang::RU:
        return ru;
    default:
        return en;
    }
}

namespace pal
{
const ImVec4 Bg { 0.035f, 0.041f, 0.038f, 1.f };
const ImVec4 Panel { 0.058f, 0.068f, 0.062f, 1.f };
const ImVec4 PanelHi { 0.078f, 0.092f, 0.083f, 1.f };
const ImVec4 Line { 0.15f, 0.19f, 0.16f, 1.f };
const ImVec4 Text { 0.93f, 0.95f, 0.92f, 1.f };
const ImVec4 Text2 { 0.62f, 0.67f, 0.62f, 1.f };
const ImVec4 Dim { 0.38f, 0.42f, 0.39f, 1.f };
const ImVec4 Green { 0.69f, 1.00f, 0.10f, 1.f };
const ImVec4 Orange { 1.00f, 0.48f, 0.10f, 1.f };
const ImVec4 Yellow { 1.00f, 0.84f, 0.10f, 1.f };
const ImVec4 Red { 1.00f, 0.26f, 0.20f, 1.f };
} // namespace pal

static ImVec4 Mix(const ImVec4& a, const ImVec4& b, float t)
{
    return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t);
}

static ImVec4 Tone(const ImVec4& c, float a = 1.f) { return toneMapColor(ImVec4(c.x, c.y, c.z, c.w * a)); }
static ImU32 Col(const ImVec4& c, float a = 1.f) { return ImGui::GetColorU32(Tone(c, a)); }

enum class Led
{
    Off,
    Green,
    Yellow,
    Orange,
    Red
};

enum class Apply
{
    Hot,
    Reinit,
    Restart
};

static Apply lastApply = Apply::Hot;
static double lastApplyAt = -10.0;

static void SetApplied(Apply a)
{
    lastApply = a;
    lastApplyAt = ImGui::GetTime();
}

// ---------------------------------------------------------------------------------------------------------------------
// Components, as in the prototype
// ---------------------------------------------------------------------------------------------------------------------
static ImVec4 LedColor(Led l)
{
    switch (l)
    {
    case Led::Green:
        return pal::Green;
    case Led::Yellow:
        return pal::Yellow;
    case Led::Orange:
        return pal::Orange;
    case Led::Red:
        return pal::Red;
    default:
        return pal::Dim;
    }
}

// A pilot lamp that looks lit: halo, core and highlight
static void LedAt(ImDrawList* dl, ImVec2 c, float r, Led l)
{
    const ImVec4 col = LedColor(l);
    const ImVec2 pc = P(c);

    if (l != Led::Off)
    {
        for (int i = 4; i >= 1; i--)
            dl->AddCircleFilled(pc, U(r + i * r * 0.55f), Col(col, 0.05f * (5 - i)));
        dl->AddCircleFilled(pc, U(r), Col(col));
        dl->AddCircleFilled(P(c.x - r * 0.3f, c.y - r * 0.3f), U(r * 0.38f), Col(ImVec4(1, 1, 1, 0.55f)));
    }
    else
    {
        dl->AddCircleFilled(pc, U(r), Col(ImVec4(0.12f, 0.14f, 0.13f, 1.f)));
        dl->AddCircle(pc, U(r), Col(pal::Line), 0, 1.f);
    }
}

static void Text(ImFont* f, float size, ImVec2 p, const ImVec4& c, const char* t)
{
    ImGui::GetWindowDrawList()->AddText(f, U(size), P(p), Col(c), t);
}

// Size in units of the prototype
static ImVec2 Measure(ImFont* f, float size, const char* t)
{
    const ImVec2 px = f->CalcTextSizeA(U(size), FLT_MAX, 0.f, t);
    return ImVec2(px.x / S, px.y / S);
}

static void PanelRect(ImVec2 a, ImVec2 b, bool hi = false, const ImVec4* accent = nullptr)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(P(a), P(b), Col(hi ? pal::PanelHi : pal::Panel), U(4.f));
    dl->AddRect(P(a), P(b), Col(accent ? *accent : pal::Line, accent ? 0.9f : 1.f), U(4.f), 0, accent ? U(1.5f) : 1.f);
}

// A key with a little relief and a lit edge; on = selected
static bool Button(const char* id, const char* label, ImVec2 size, bool on = false, const ImVec4& accent = pal::Green,
                   float fsize = 17.f)
{
    const ImVec2 p = Q(ImGui::GetCursorScreenPos());
    const bool pressed = ImGui::InvisibleButton(id, Sz(size.x, size.y));
    const bool hov = ImGui::IsItemHovered(), held = ImGui::IsItemActive();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 q(p.x + size.x, p.y + size.y);
    const ImVec4 top = on ? Mix(pal::Panel, accent, 0.22f) : (hov ? pal::PanelHi : ImVec4(0.07f, 0.082f, 0.075f, 1.f));
    const ImVec4 bot = Mix(top, ImVec4(0, 0, 0, 1), held ? 0.1f : 0.35f);
    dl->AddRectFilledMultiColor(P(p), P(q), Col(top), Col(top), Col(bot), Col(bot));
    if (on)
        for (int i = 3; i >= 1; i--)
            dl->AddRect(P(p.x - i, p.y - i), P(q.x + i, q.y + i), Col(accent, 0.06f * (4 - i)), U(5.f), 0, 1.f);
    dl->AddRect(P(p), P(q), Col(on ? accent : (hov ? Mix(pal::Line, accent, 0.4f) : pal::Line)), U(4.f), 0,
                on ? U(1.6f) : 1.f);
    dl->AddLine(P(p.x + 2, p.y + 1), P(q.x - 2, p.y + 1), Col(ImVec4(1, 1, 1, on ? 0.10f : 0.05f)));
    const ImVec2 ts = Measure(fUi, fsize, label);
    dl->AddText(fUi, U(fsize), P(p.x + (size.x - ts.x) * 0.5f, p.y + (size.y - ts.y) * 0.5f), Col(on ? pal::Text : pal::Text2),
                label);
    return pressed;
}

// Style of the drop-down lists of the system, around BeginCombo
static void PushComboStyle()
{
    ImGui::PushFont(fUi, U(18.f));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, Tone(pal::Panel));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, Tone(pal::PanelHi));
    ImGui::PushStyleColor(ImGuiCol_Border, Tone(pal::Line));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, Tone(ImVec4(0.05f, 0.06f, 0.055f, 0.98f)));
    ImGui::PushStyleColor(ImGuiCol_Header, Tone(Mix(pal::Panel, pal::Green, 0.25f)));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, Tone(Mix(pal::Panel, pal::Green, 0.15f)));
    ImGui::PushStyleColor(ImGuiCol_Button, Tone(Mix(pal::Panel, pal::Green, 0.10f)));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Tone(Mix(pal::Panel, pal::Green, 0.22f)));
    ImGui::PushStyleColor(ImGuiCol_Text, Tone(pal::Text));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, Sz(14.f, 12.f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, U(4.f));
}

static void PopComboStyle()
{
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(9);
    ImGui::PopFont();
}

static void Label(const char* t, const ImVec4& c = pal::Text2, float size = 17.f)
{
    ImGui::PushFont(fUi, U(size));
    ImGui::TextColored(Tone(c), "%s", t);
    ImGui::PopFont();
}

// The provider's own badge (a monogram or a sign, no brand logos)
static void Badge(ImVec2 p, float s, const char* mono, const ImVec4& tint)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(P(p), P(p.x + s, p.y + s), Col(Mix(ImVec4(0.03f, 0.035f, 0.033f, 1), tint, 0.10f)), U(4.f));
    dl->AddRect(P(p), P(p.x + s, p.y + s), Col(tint, 0.55f), U(4.f), 0, U(1.2f));
    const ImVec2 c(p.x + s * 0.5f, p.y + s * 0.5f);
    const float th = U((std::max)(1.5f, s * 0.045f));
    const auto centred = [&](const char* t, float fs, float dy = 0.f)
    {
        const ImVec2 ts = Measure(fBrand, fs, t);
        dl->AddText(fBrand, U(fs), P(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f + dy), Col(tint), t);
    };

    if (strcmp(mono, "F3") == 0) // FSR 3.1: a hexagon with "3.1"
    {
        ImVec2 h[6];
        for (int i = 0; i < 6; i++)
            h[i] = P(c.x + cosf(i * 1.0472f + 0.5236f) * s * 0.36f, c.y + sinf(i * 1.0472f + 0.5236f) * s * 0.36f);
        dl->AddPolyline(h, 6, Col(tint), ImDrawFlags_Closed, th);
        centred("3.1", s * 0.27f);
    }
    else if (strcmp(mono, "F2") == 0) // FSR 2: two rings with "2"
    {
        dl->AddCircle(P(c), U(s * 0.34f), Col(tint), 0, th);
        dl->AddCircle(P(c), U(s * 0.25f), Col(tint, 0.5f), 0, th * 0.7f);
        centred("2", s * 0.32f);
    }
    else if (strcmp(mono, "HX") == 0) // HelixSR: a double helix
    {
        for (int k = 0; k < 2; k++)
        {
            ImVec2 pts[24];
            for (int i = 0; i < 24; i++)
            {
                const float u = i / 23.f;
                pts[i] = P(c.x + sinf(u * 6.2832f * 1.25f + k * 3.1416f) * s * 0.22f, p.y + s * 0.16f + u * s * 0.68f);
            }
            dl->AddPolyline(pts, 24, Col(tint, k ? 0.6f : 1.f), 0, th);
        }
        for (int i = 1; i < 5; i++) // rungs
        {
            const float u = i / 5.f, y = p.y + s * 0.16f + u * s * 0.68f, x = sinf(u * 6.2832f * 1.25f) * s * 0.22f;
            dl->AddLine(P(c.x - x, y), P(c.x + x, y), Col(tint, 0.45f), th * 0.6f);
        }
    }
    else if (strcmp(mono, "XS") == 0) // XeSS: a cross with corner marks
    {
        const float r = s * 0.24f;
        dl->AddLine(P(c.x - r, c.y - r), P(c.x + r, c.y + r), Col(tint), th * 2.2f);
        dl->AddLine(P(c.x + r, c.y - r), P(c.x - r, c.y + r), Col(tint), th * 2.2f);
        const float m = s * 0.16f, l = s * 0.12f;
        dl->AddLine(P(p.x + m, p.y + m), P(p.x + m + l, p.y + m), Col(tint, 0.6f), th);
        dl->AddLine(P(p.x + m, p.y + m), P(p.x + m, p.y + m + l), Col(tint, 0.6f), th);
        dl->AddLine(P(p.x + s - m, p.y + s - m), P(p.x + s - m - l, p.y + s - m), Col(tint, 0.6f), th);
        dl->AddLine(P(p.x + s - m, p.y + s - m), P(p.x + s - m, p.y + s - m - l), Col(tint, 0.6f), th);
    }
    else
        centred(mono, s * 0.42f);
}

// Icons drawn by hand
static void Icon(int kind, ImVec2 c, float s, ImU32 col)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float t = U(2.2f), h = s * 0.5f;
    const auto p = [&](float x, float y) { return P(c.x + x, c.y + y); };

    switch (kind)
    {
    case 0: // home: a house
        dl->AddLine(p(-h, -h * 0.05f), p(0, -h), col, t);
        dl->AddLine(p(0, -h), p(h, -h * 0.05f), col, t);
        dl->AddRect(p(-h * 0.72f, -h * 0.2f), p(h * 0.72f, h), col, U(2.f), 0, t);
        dl->AddRect(p(-h * 0.22f, h * 0.3f), p(h * 0.22f, h), col, U(1.f), 0, t);
        break;
    case 1: // upscaling: layers
        for (int i = 0; i < 3; i++)
        {
            const float y = -h * 0.55f + i * h * 0.5f;
            dl->AddQuad(p(0, y - h * 0.38f), p(h, y), p(0, y + h * 0.38f), p(-h, y), col, t);
        }
        break;
    case 2: // frame generation: frames
        dl->AddRect(p(-h, -h * 0.62f), p(h, h * 0.62f), col, U(3.f), 0, t);
        dl->AddRect(p(-h * 0.42f, -h * 0.28f), p(h * 0.42f, h * 0.28f), col, U(2.f), 0, t);
        for (int i = -1; i <= 1; i += 2)
            for (int j = -1; j <= 1; j += 2)
                dl->AddCircleFilled(p(i * h * 0.75f, j * h * 0.42f), U(1.6f), col);
        break;
    case 3: // latency: a pulse
    {
        ImVec2 pts[] = { p(-h, 0), p(-h * 0.45f, 0), p(-h * 0.2f, -h * 0.75f), p(h * 0.15f, h * 0.75f), p(h * 0.4f, -h * 0.2f),
                         p(h * 0.55f, 0), p(h, 0) };
        dl->AddPolyline(pts, 7, col, 0, t);
        break;
    }
    case 4: // enhancements: an eye
        dl->AddBezierQuadratic(p(-h, 0), p(0, -h * 1.15f), p(h, 0), col, t);
        dl->AddBezierQuadratic(p(-h, 0), p(0, h * 1.15f), p(h, 0), col, t);
        dl->AddCircle(p(0, 0), U(h * 0.3f), col, 0, t);
        break;
    case 5: // overlay: a monitor
        dl->AddRect(p(-h, -h * 0.75f), p(h, h * 0.4f), col, U(2.f), 0, t);
        dl->AddLine(p(0, h * 0.4f), p(0, h * 0.75f), col, t);
        dl->AddLine(p(-h * 0.45f, h * 0.78f), p(h * 0.45f, h * 0.78f), col, t);
        break;
    case 6: // personal: sliders
        for (int i = 0; i < 3; i++)
        {
            const float y = -h * 0.6f + i * h * 0.6f, k = (i == 1 ? h * 0.3f : -h * 0.35f);
            dl->AddLine(p(-h, y), p(h, y), col, t);
            dl->AddCircleFilled(p(k, y), U(3.6f), Col(pal::Bg));
            dl->AddCircle(p(k, y), U(3.6f), col, 0, t);
        }
        break;
    case 7: // help: a question mark
        dl->AddCircle(p(0, 0), U(h), col, 0, t);
        dl->AddBezierCubic(p(-h * 0.35f, -h * 0.25f), p(-h * 0.35f, -h * 0.75f), p(h * 0.4f, -h * 0.75f), p(h * 0.25f, -h * 0.15f),
                           col, t);
        dl->AddLine(p(h * 0.25f, -h * 0.15f), p(0, h * 0.15f), col, t);
        dl->AddCircleFilled(p(0, h * 0.5f), U(2.f), col);
        break;
    case 8: // info
        dl->AddCircle(p(0, 0), U(h), col, 0, t);
        dl->AddLine(p(0, -h * 0.05f), p(0, h * 0.5f), col, t);
        dl->AddCircleFilled(p(0, -h * 0.42f), U(2.f), col);
        break;
    case 9: // chevron
        dl->AddLine(p(-h * 0.25f, -h * 0.5f), p(h * 0.25f, 0), col, t);
        dl->AddLine(p(h * 0.25f, 0), p(-h * 0.25f, h * 0.5f), col, t);
        break;
    case 10: // gear
        dl->AddCircle(p(0, 0), U(h * 0.55f), col, 0, t);
        for (int i = 0; i < 8; i++)
        {
            const float a = i * 3.14159f / 4.f;
            dl->AddLine(p(cosf(a) * h * 0.6f, sinf(a) * h * 0.6f), p(cosf(a) * h, sinf(a) * h), col, t + U(1.f));
        }
        dl->AddCircle(p(0, 0), U(h * 0.22f), col, 0, t);
        break;
    }
}

// Status pill ("Loaded and active"...)
static void Pill(const char* text, Led l)
{
    const ImVec2 p = Q(ImGui::GetCursorScreenPos());
    const ImVec2 ts = Measure(fUi, 18.f, text);
    const ImVec2 q(p.x + ts.x + 46.f, p.y + 32.f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec4 c = LedColor(l);
    dl->AddRectFilled(P(p), P(q), Col(Mix(pal::Bg, c, 0.08f)), U(4.f));
    dl->AddRect(P(p), P(q), Col(c, 0.45f), U(4.f));
    LedAt(dl, ImVec2(p.x + 17.f, p.y + 16.f), 6.f, l);
    dl->AddText(fUi, U(18.f), P(p.x + 32.f, p.y + 6.f), Col(c), text);
    ImGui::Dummy(Sz(q.x - p.x, q.y - p.y));
}

// A section that folds (advanced options, diagnostics)
static bool Fold(const char* id, const char* title, bool* open, int icon, float w)
{
    const ImVec2 p = Q(ImGui::GetCursorScreenPos());
    if (ImGui::InvisibleButton(id, Sz(w, 50.f)))
        *open = !*open;
    PanelRect(p, ImVec2(p.x + w, p.y + 50.f), ImGui::IsItemHovered());
    Icon(icon, ImVec2(p.x + 30.f, p.y + 25.f), 22.f, Col(pal::Text2));
    Text(fUi, 20.f, ImVec2(p.x + 60.f, p.y + 13.f), pal::Text, title);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 c(p.x + w - 28.f, p.y + 25.f);
    if (*open)
    {
        dl->AddLine(P(c.x - 6, c.y - 3), P(c.x, c.y + 3), Col(pal::Text2), U(2.f));
        dl->AddLine(P(c.x, c.y + 3), P(c.x + 6, c.y - 3), Col(pal::Text2), U(2.f));
    }
    else
        Icon(9, c, 22.f, Col(pal::Text2));
    return *open;
}

// The palette over the controls ILION already has, drawn inside the panels of the new interface
static void PushContentStyle()
{
    const ImVec4 frame(0.07f, 0.082f, 0.075f, 1.f);
    const std::pair<ImGuiCol, ImVec4> colors[] = {
        { ImGuiCol_Text, pal::Text },
        { ImGuiCol_TextDisabled, pal::Text2 },
        { ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0) },
        { ImGuiCol_FrameBg, frame },
        { ImGuiCol_FrameBgHovered, pal::PanelHi },
        { ImGuiCol_FrameBgActive, Mix(pal::Panel, pal::Green, 0.15f) },
        { ImGuiCol_Border, pal::Line },
        { ImGuiCol_Button, frame },
        { ImGuiCol_ButtonHovered, pal::PanelHi },
        { ImGuiCol_ButtonActive, Mix(pal::Panel, pal::Green, 0.22f) },
        { ImGuiCol_Header, Mix(pal::Panel, pal::Green, 0.18f) },
        { ImGuiCol_HeaderHovered, Mix(pal::Panel, pal::Green, 0.12f) },
        { ImGuiCol_HeaderActive, Mix(pal::Panel, pal::Green, 0.25f) },
        { ImGuiCol_CheckMark, pal::Green },
        { ImGuiCol_SliderGrab, pal::Green },
        { ImGuiCol_SliderGrabActive, Mix(pal::Green, ImVec4(1, 1, 1, 1), 0.3f) },
        { ImGuiCol_Separator, pal::Line },
        { ImGuiCol_PopupBg, ImVec4(0.05f, 0.06f, 0.055f, 0.98f) },
        { ImGuiCol_ScrollbarBg, ImVec4(0, 0, 0, 0) },
        { ImGuiCol_ScrollbarGrab, pal::Line },
        { ImGuiCol_ScrollbarGrabHovered, Mix(pal::Line, pal::Green, 0.3f) },
        { ImGuiCol_PlotLines, pal::Green },
        { ImGuiCol_PlotHistogram, pal::Green },
        { ImGuiCol_TextSelectedBg, Mix(pal::Panel, pal::Green, 0.35f) },
    };

    for (const auto& [id, color] : colors)
        ImGui::PushStyleColor(id, Tone(color));

    // Sizes of the new interface: no small keys or cramped rows
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, Sz(10.f, 7.f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, Sz(10.f, 9.f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, Sz(8.f, 6.f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, Sz(16.f, 14.f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, U(4.f));
    ImGui::PushStyleVar(ImGuiStyleVar_GrabRounding, U(4.f));
    ImGui::PushStyleVar(ImGuiStyleVar_GrabMinSize, U(14.f));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, U(4.f));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, U(4.f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.f);
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, U(10.f));
    ImGui::PushStyleVar(ImGuiStyleVar_IndentSpacing, U(18.f));
}

static void PopContentStyle()
{
    ImGui::PopStyleVar(12);
    ImGui::PopStyleColor(24);
}

// Area that scrolls inside a panel, without its own background
static bool BeginArea(const char* id, ImVec2 a, ImVec2 b)
{
    ImGui::SetCursorScreenPos(P(a));
    return ImGui::BeginChild(id, Sz(b.x - a.x, b.y - a.y), ImGuiChildFlags_NavFlattened);
}

// The state of the last change, for a few seconds: applied, the upscaler created again, or waiting for a restart
static void ApplyState(ImVec2 p)
{
    if (ImGui::GetTime() - lastApplyAt > 6.0)
        return;

    const char* t = lastApply == Apply::Hot      ? Lx("Applied while playing", "Aplicado en caliente", "Aplicado em tempo real",
                                                      "Применено на лету")
                    : lastApply == Apply::Reinit ? Lx("Applied: the upscaler is created again",
                                                      "Aplicado: reinicializando el escalador",
                                                      "Aplicado: reinicializando o upscaler",
                                                      "Применено: апскейлер создаётся заново")
                                                 : Lx("Saved: needs a restart of the game", "Pendiente: requiere reiniciar el juego",
                                                      "Pendente: requer reiniciar o jogo", "Сохранено: нужен перезапуск игры");
    const Led l = lastApply == Apply::Hot ? Led::Green : lastApply == Apply::Reinit ? Led::Yellow : Led::Orange;
    ImGui::SetCursorScreenPos(P(p));
    Pill(t, l);
}

// ---------------------------------------------------------------------------------------------------------------------
// Sections
// ---------------------------------------------------------------------------------------------------------------------
struct Section
{
    const char* caps[4]; // EN ES PT RU
    const char* name[4];
    const char* sub[4];
    const char* tabs[3]; // tabs of the menu shown in it, by their label
};

// clang-format off
static const Section kSections[] = {
    { { "HOME", "INICIO", "INÍCIO", "ГЛАВНАЯ" }, { "Home", "Inicio", "Início", "Главная" },
      { "Summary of the game and of ILION's modules", "Resumen del juego y de los módulos de ILION",
        "Resumo do jogo e dos módulos do ILION", "Сводка по игре и модулям ILION" }, {} },
    { { "UPSCALING", "ESCALADO", "ESCALA", "МАСШТАБ" }, { "Upscaling", "Escalado", "Escala", "Масштабирование" },
      { "Choose the upscaling technology and set its options", "Selecciona la tecnología de escalado y configura sus opciones",
        "Escolha a tecnologia de escala e ajuste suas opções", "Выберите технологию масштабирования и настройте её" },
      { "Upscaling" } },
    { { "FRAME GEN", "FRAME GEN", "FRAME GEN", "FRAME GEN" },
      { "Frame Generation", "Frame Generation", "Frame Generation", "Генерация кадров" },
      { "Frame generation: provider, input and multiplier", "Generación de fotogramas: proveedor, entrada y multiplicador",
        "Geração de quadros: provedor, entrada e multiplicador", "Генерация кадров: поставщик, вход и множитель" },
      { "Frame Gen", "External FG", "Anti-Ghosting" } },
    { { "LATENCY", "LATENCIA", "LATÊNCIA", "ЗАДЕРЖКА" }, { "Latency & FPS", "Latencia y FPS", "Latência e FPS", "Задержка и FPS" },
      { "Limiter, low latency, regulator and V-Sync", "Limitador, baja latencia, regulador y V-Sync",
        "Limitador, baixa latência, regulador e V-Sync", "Ограничитель, низкая задержка, регулятор и V-Sync" },
      { "Latency & FPS" } },
    { { "IMAGE", "MEJORAS", "MELHORIAS", "ГРАФИКА" },
      { "Graphics enhancements", "Mejoras gráficas", "Melhorias gráficas", "Улучшения графики" },
      { "Neural Rendering, ReShade and image adjustments", "Neural Rendering, ReShade y ajustes de imagen",
        "Neural Rendering, ReShade e ajustes de imagem", "Neural Rendering, ReShade и настройки изображения" },
      { "Neural Rendering", "Graphics" } },
    { { "OVERLAY", "OVERLAY", "OVERLAY", "ОВЕРЛЕЙ" }, { "Overlay", "Overlay", "Overlay", "Оверлей" },
      { "Performance overlay, sensors and benchmark", "Overlay de rendimiento, sensores y benchmark",
        "Overlay de desempenho, sensores e benchmark", "Оверлей производительности, датчики и бенчмарк" },
      { "Overlay", "Benchmark" } },
    { { "PERSONAL", "PERSONAL", "PESSOAL", "ЛИЧНОЕ" }, { "Personal", "Personal", "Pessoal", "Личное" },
      { "Theme, interface scale, menu and keys", "Tema, escala de la interfaz, menú y teclas", "Tema, escala da interface, menu e teclas",
        "Тема, масштаб интерфейса, меню и клавиши" },
      { "Custom", "Menu" } },
    { { "STATUS", "ESTADO", "ESTADO", "СОСТОЯНИЕ" },
      { "Status & compatibility", "Estado y compatibilidad", "Estado e compatibilidade", "Состояние и совместимость" },
      { "Read-only status, compatibility settings of the game and the log", "Estado de solo lectura, ajustes de compatibilidad del juego y registro",
        "Estado somente leitura, ajustes de compatibilidade do jogo e registro", "Состояние (только чтение), настройки совместимости и журнал" },
      { "Help / Status", "Compatibility" } },
};
// clang-format on

constexpr int kSectionCount = (int) std::size(kSections);

static int LangIndex()
{
    switch (Ilion::CurrentLang())
    {
    case Ilion::Lang::ES:
        return 1;
    case Ilion::Lang::PT:
        return 2;
    case Ilion::Lang::RU:
        return 3;
    default:
        return 0;
    }
}

static int section = 0;
static int subTab[kSectionCount] = {};
static int upFamily = -1;      // family shown in the upscaling panel
static int upFilter = 0;       // filter of the catalogue
static bool upAdvanced = false; // folds of the upscaling panel
static bool upDiagnostics = false;

// Family of the selector: badge, colour, where it comes from, filter of the catalogue (1 AMD, 2 NVIDIA, 3 Intel, 4 community)
struct FamilyLook
{
    const char* badge;
    ImVec4 tint;
    const char* subtitle;
    int filter;
};

static FamilyLook LookOf(int family)
{
    switch (family)
    {
    case FamFsr4:
        return { "F4", pal::Red, "AMD FidelityFX", 1 };
    case FamFsr31:
        return { "F3", pal::Orange, "AMD FidelityFX 3", 1 };
    case FamFsr2:
        return { "F2", ImVec4(0.80f, 0.30f, 0.42f, 1.f), "AMD FidelityFX 2", 1 };
    case FamXess:
        return { "XS", ImVec4(0.30f, 0.55f, 1.f, 1.f), "Intel Xe Super Sampling", 3 };
    case FamDlss4:
        return { "DL", ImVec4(0.46f, 0.80f, 0.20f, 1.f), "NVIDIA Deep Learning", 2 };
    case FamHelix:
        return { "HX", ImVec4(0.20f, 0.92f, 0.78f, 1.f), "lonewolf0622", 4 };
    default:
        return { "FX", pal::Text2, "FidelityFX API", 1 };
    }
}

// Best state of the upscalers of one family: 3 runs, 2 loaded, 1 chosen, 0 none
static int FamilyRank(const UpscalerCatalog& catalog, int family, const UpscalerVariant** best = nullptr)
{
    int rank = 0;
    for (const auto& v : catalog.variants)
    {
        if (v.family != family)
            continue;
        const int value = v.status.active ? 3 : v.status.loaded ? 2 : v.status.selected ? 1 : 0;
        if (best != nullptr && (*best == nullptr || value > rank))
            *best = &v;
        rank = (std::max)(rank, value);
    }
    return rank;
}

static void ShellLedAt(ImDrawList* dl, ImVec2 c, float r, Led l)
{
    IlionTheme::ShellLed(dl, c, r, Tone(LedColor(l)), l != Led::Off);
}

static Led LedOfRank(int rank) { return rank == 3 ? Led::Green : rank == 2 ? Led::Yellow : rank == 1 ? Led::Orange : Led::Off; }

// Scale of the window: the approved size (1600 x 1000) on a 1440p screen, larger in proportion above it, a larger share of
// a smaller screen; Menu Scale of the ini, when set, changes it in the same proportion as the classic menu
static float ScaleFor(const ImGuiIO& io, decltype(Config::Instance()) config)
{
    const float h = io.DisplaySize.y > 0 ? io.DisplaySize.y : 1080.0f;
    const float w = io.DisplaySize.x > 0 ? io.DisplaySize.x : 1920.0f;
    const float share = std::clamp(0.85f - (h - 1080.0f) / 360.0f * 0.15f, 0.70f, 0.85f);
    float s = (std::min)(h * share / H, w * 0.92f / W);

    if (config->MenuScale.has_value())
    {
        const float automatic = std::clamp(std::round((float) ((int) (h / 108.0f)) / 10.0f * 10.0f) / 10.0f, 0.5f, 2.0f);
        s *= config->MenuScale.value() / automatic;
    }

    return std::clamp((std::min)({ s, h * 0.97f / H, w * 0.98f / W }), 0.4f, 3.0f);
}
} // namespace IlionShell

// ILION dev.60: FG on/off, the multiplier of the loaded output and what is chosen, loaded and active. The same in Home
// and next to the selection of the Frame Gen module; on/off is the FG key's own function
void MenuCommon::RenderFgQuickControls(RenderMenuContext& ctx)
{
    using namespace IlionShell;
    auto& state = ctx.state;
    auto config = ctx.config;
    auto fg = state.currentFG;
    const bool running = fg != nullptr && fg->IsActive() && !fg->IsPaused();

    bool on = config->IsFGEnabled();
    ImGui::BeginDisabled(!CanToggleFrameGeneration(state));
    if (IlionTheme::Checkbox(Lx("Frame generation on###QuickFg", "Generación activada###QuickFg", "Geração ativada###QuickFg",
                                "Генерация включена###QuickFg"),
                             &on))
        ToggleFrameGeneration(state, config);
    ImGui::EndDisabled();

    // Multiplier: only with an output loaded, up to what it can generate
    ImGui::SameLine(0, U(24));
    if (fg != nullptr && (state.activeFgOutput == FGOutput::XeFG || state.activeFgOutput == FGOutput::DLSSG ||
                          state.activeFgOutput == FGOutput::FSRFG))
    {
        ImGui::SetNextItemWidth(U(130));
        if (state.activeFgOutput == FGOutput::XeFG)
            RenderFGModeCombo(ctx, "Frame Gen##QuickMult", fg->GetMaxInterpolationCount(), &config->FGXeFGInterpolationCount);
        else if (state.activeFgOutput == FGOutput::DLSSG)
            RenderFGModeCombo(ctx, "Frame Gen##QuickMult", fg->GetMaxInterpolationCount(), &config->FGDLSSGInterpolationCount);
        else
            RenderFGModeCombo(ctx, "Frame Gen##QuickMult", 1, nullptr);
    }
    else
    {
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", Lx("Multiplier: once an FG output is loaded", "Multiplicador: con una salida de FG cargada",
                                     "Multiplicador: com uma saída de FG carregada", "Множитель: после загрузки выхода FG"));
    }

    // Chosen, loaded and active, apart
    const auto row = [&](Led led, const char* name, const std::string& value)
    {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float h = ImGui::GetTextLineHeight();
        ShellLedAt(ImGui::GetWindowDrawList(), ImVec2(p.x + h * 0.5f, p.y + h * 0.5f), h * 0.22f, led);
        ImGui::Dummy(ImVec2(h, h));
        ImGui::SameLine(0, U(6));
        ImGui::TextColored(Tone(pal::Text2), "%s", name);
        ImGui::SameLine(0, U(8));
        ImGui::TextUnformatted(value.c_str());
    };

    const std::string chosen = StrFmt("%s / %s", std::string(magic_enum::enum_name(config->FGInput.value_or_default())).c_str(),
                                      std::string(magic_enum::enum_name(config->FGOutput.value_or_default())).c_str());
    const std::string loaded = StrFmt("%s / %s", std::string(magic_enum::enum_name(state.activeFgInput)).c_str(),
                                      std::string(magic_enum::enum_name(state.activeFgOutput)).c_str());
    const bool same = config->FGOutput.value_or_default() == state.activeFgOutput && config->FGInput.value_or_default() == state.activeFgInput;

    std::string active = Tr("Off");
    if (running)
    {
        const unsigned multiplier = (unsigned) fg->GetInterpolatedFrameCount() + 1;
        std::string name = fg->Name();
        const std::string suffix = StrFmt(" %ux", multiplier);
        if (name.ends_with(suffix))
            name.erase(name.size() - suffix.size());
        active = StrFmt("%s %ux", name.c_str(), multiplier);
    }

    row(Led::Off, Lx("Chosen:", "Elegido:", "Escolhido:", "Выбрано:"), chosen);
    row(same ? Led::Green : Led::Orange, Lx("Loaded:", "Cargado:", "Carregado:", "Загружено:"), loaded);
    row(running ? Led::Green : Led::Off, Lx("Generating:", "Generando:", "Gerando:", "Генерирует:"), active);
    if (!same)
        TextWrappedColored(Tone(pal::Orange), "%s",
                           Lx("The choice is not the one loaded yet: it applies when OptiScaler creates the FG again (in some games, after a restart).",
                              "La elección aún no es la cargada: se aplica cuando OptiScaler vuelve a crear la FG (en algunos juegos, tras reiniciar).",
                              "A escolha ainda não é a carregada: aplica-se quando o OptiScaler recria a FG (em alguns jogos, após reiniciar).",
                              "Выбор ещё не загружен: применится, когда OptiScaler заново создаст FG (в некоторых играх — после перезапуска)."));
}

void MenuCommon::RenderIlionShell(RenderMenuContext& ctx)
{
    using namespace IlionShell;
    auto& state = ctx.state;
    auto config = ctx.config;
    auto& io = ctx.io;
    IFeature* feature = ctx.currentFeature;

    ExternalFg(); // the search for MFG SUPER MOTION starts with the menu, as in the classic layout
    AdvanceReShadePanel();

    S = ScaleFor(io, config);

    // The controls of the menu take the look of the new interface while it draws, in its single mode
    struct ShellScope
    {
        ShellScope()
        {
            IlionTheme::shell = true;
            shellActive = true;
        }
        ~ShellScope()
        {
            IlionTheme::shell = false;
            shellActive = false;
        }
    } shellScope;
    fUi = IlionTheme::modernFont != nullptr ? IlionTheme::modernFont : ImGui::GetFont();
    fBrand = IlionTheme::crtTitleFont != nullptr ? IlionTheme::crtTitleFont : fUi;
    const int li = LangIndex();

    size_t tabCount = 0;
    const MenuTab* tabs = MenuTabs(tabCount);
    const bool essential = false; // a single mode for now

    const auto tabIndex = [&](const char* label)
    {
        for (int i = 0; i < (int) tabCount; i++)
        {
            if (strcmp(tabs[i].label, label) == 0)
                return i;
        }
        return -1;
    };

    // The same rules as the classic sidebar: Essential hides some tabs, Custom follows its setting
    const auto tabVisible = [&](int i)
    {
        if (i < 0)
            return false;
        if (essential && EssentialHidesTab(tabs[i].label))
            return false;
        if (strcmp(tabs[i].label, "Custom") == 0)
            return config->CustomTabEnabled.value_or_default();
        return tabs[i].visible == nullptr || tabs[i].visible(ctx);
    };

    // Sub-sections of the modules: a tab of the menu and the layout the new interface gives it --------------------
    // clang-format off
    static const ShellLayout lFrameGen { { "fg_selection", "@fg_quick", "fg_optifg", "fg_reprojection", "fg_nvngx", "fg_fsrfg_inputs",
                                           "fg_sl_inputs" },
                                         { "@fg_instruments", "fg_xefg", "fg_fsrfg", "fg_dlssg" } };
    static const ShellLayout lLatency { { "framerate", "vsync", "low_latency" }, { "ilion_latency", "ilion_regulator" } };
    static const ShellLayout lImage { { "gfx_color", "gfx_advanced" }, { "sharpness", "mipmap", "anisotropy" } };
    static const ShellLayout lReShade { { "reshade" }, {} };
    static const ShellLayout lCompare { { "gfx_compare" }, { "magnifier" } };
    static const ShellLayout lOverlay { { "fps_overlay" }, {} };
    static const ShellLayout lBenchmark { { "benchmark" }, {} };
    static const ShellLayout lMenu { { "theme" }, { "menu_scale", "keybinds" } };
    static const ShellLayout lStatus { { "status_summary", "status_system", "status_init" },
                                       { "@upscaler_diag", "status_detected", "status_hooks" } };
    static const ShellLayout lCompat { { "init_flags", "fsr_common", "upscaler_inputs" },
                                       { "spoofing", "plugins", "shaders_limits", "barriers", "root_signatures" }, true };
    static const ShellLayout lLog { { "logging" }, {}, true };

    struct Sub
    {
        int section;
        const char* name[4];
        const char* tab;
        const ShellLayout* layout;
        int flow; // 0: the layout; -1: the cards split at half height; n: the right column starts at the card n
    };

    static const Sub subs[] = {
        { 2, { "Generation", "Generación", "Geração", "Генерация" }, "Frame Gen", &lFrameGen, 0 },
        { 2, { "External FG", "FG externo", "FG externo", "Внешний FG" }, "External FG", nullptr, -1 },
        { 2, { "Anti-Ghosting", "Anti-Ghosting", "Anti-Ghosting", "Anti-Ghosting" }, "Anti-Ghosting", nullptr, -1 },
        { 3, { "Latency & FPS", "Latencia y FPS", "Latência e FPS", "Задержка и FPS" }, "Latency & FPS", &lLatency, 0 },
        { 4, { "Image", "Imagen", "Imagem", "Изображение" }, "Graphics", &lImage, 0 },
        { 4, { "Neural Rendering", "Neural Rendering", "Neural Rendering", "Neural Rendering" }, "Neural Rendering", nullptr, 2 },
        { 4, { "ReShade", "ReShade", "ReShade", "ReShade" }, "Graphics", &lReShade, 0 },
        { 4, { "Comparison", "Comparación", "Comparação", "Сравнение" }, "Graphics", &lCompare, 0 },
        { 5, { "Overlay", "Overlay", "Overlay", "Оверлей" }, "Overlay", &lOverlay, 0 },
        { 5, { "Benchmark", "Benchmark", "Benchmark", "Бенчмарк" }, "Benchmark", &lBenchmark, 0 },
        { 6, { "Appearance and menu", "Apariencia y menú", "Aparência e menu", "Внешний вид и меню" }, "Menu", &lMenu, 0 },
        { 6, { "Custom panel", "Panel personalizado", "Painel personalizado", "Своя панель" }, "Custom", nullptr, 0 },
        { 7, { "Status", "Estado", "Estado", "Состояние" }, "Help / Status", &lStatus, 0 },
        { 7, { "Compatibility", "Compatibilidad", "Compatibilidade", "Совместимость" }, "Compatibility", &lCompat, 0 },
        { 7, { "Log", "Registro", "Registro", "Журнал" }, "Help / Status", &lLog, 0 },
    };
    // clang-format on
    constexpr int subCount = (int) std::size(subs);
    constexpr int subReShade = 6, subCompare = 7, subBenchmark = 9;

    // A tab asked for by another part of the menu: the first sub-section that shows it
    if (requestedMenuTab != nullptr)
    {
        if (strcmp(requestedMenuTab, "Upscaling") == 0)
            section = 1;

        for (int k = 0; k < subCount; k++)
        {
            if (strcmp(subs[k].tab, requestedMenuTab) == 0)
            {
                section = subs[k].section;
                subTab[section] = k;
                break;
            }
        }

        requestedMenuTab = nullptr;
    }

    // Window -------------------------------------------------------------------------------------------------------
    const ImVec2 size = Sz(W, H);
    ImGui::SetNextWindowSize(size, ImGuiCond_Always);
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, U(6.0f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, Tone(pal::Bg));
    ImGui::PushStyleColor(ImGuiCol_Border, Tone(pal::Line));

    const bool open = ImGui::Begin("ILION###IlionShell", nullptr,
                                   ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                                       ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                                       ImGuiWindowFlags_NoSavedSettings);
    ImGui::PopStyleVar(3);

    if (!open)
    {
        ImGui::End();
        ImGui::PopStyleColor(2);
        return;
    }

    // Moved by the user: kept on the screen
    {
        const ImVec2 pos = ImGui::GetWindowPos();
        const ImVec2 fixed(std::clamp(pos.x, 0.0f, (std::max)(0.0f, io.DisplaySize.x - size.x)),
                           std::clamp(pos.y, 0.0f, (std::max)(0.0f, io.DisplaySize.y - size.y)));
        if (fixed.x != pos.x || fixed.y != pos.y)
            ImGui::SetWindowPos(fixed);
    }

    O = ImGui::GetWindowPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // States read once for the sidebar, the cards and the top strip ------------------------------------------------
    const bool upscalerOn = feature != nullptr && feature->IsInited() && !feature->IsFrozen();
    const bool upscalerIdle = feature != nullptr && feature->IsInited() && feature->IsFrozen();

    std::string fgText = Tr("Off");
    Led fgLed = Led::Off;
    {
        auto fg = state.currentFG;
        if (fg != nullptr && fg->IsActive() && !fg->IsPaused())
        {
            const unsigned multiplier = (unsigned) fg->GetInterpolatedFrameCount() + 1;
            std::string fgName = fg->Name();
            const std::string suffix = StrFmt(" %ux", multiplier);
            if (fgName.ends_with(suffix))
                fgName.erase(fgName.size() - suffix.size());
            fgText = StrFmt("%s %ux", fgName.c_str(), multiplier);
            fgLed = Led::Green;
        }
        else
        {
            const auto external = GetExternalFgStatus(state.dlssgDetectedInterpolationCount, state.activeFgNvngx,
                                                      state.swapchainApi == DX12);
            if (external.kind == ExternalFgStatus::Kind::Active)
            {
                fgText = external.text;
                fgLed = Led::Green;
            }
            else if (external.kind == ExternalFgStatus::Kind::Unknown)
            {
                fgText = external.text;
                fgLed = Led::Yellow;
            }
        }
    }

    const bool fakenvapiInUse = fakenvapi::isUsingAsMainNvapi();
    const char* latencyTech = fakenvapiInUse ? IlionLatency::TechName(fakenvapi::getCurrentMode()) : nullptr;
    const bool latencyOn = latencyTech != nullptr && fakenvapi::isLowLatencyActive();
    const float fpsLimit = config->FramerateLimit.value_or_default();

    const bool nrEnabled = config->DlssNrEnabled.value_or_default();
    const bool nrRunning = DlssNr::AmdBridge::RecentlyActive();

    const Led sectionLed[kSectionCount] = {
        Led::Off,
        upscalerOn ? Led::Green : upscalerIdle ? Led::Yellow : Led::Off,
        fgLed,
        latencyOn || fpsLimit > 0.0f ? Led::Green : Led::Off,
        nrRunning ? Led::Green : nrEnabled ? Led::Yellow : Led::Off,
        config->ShowFps.value_or_default() ? Led::Green : Led::Off,
        Led::Off,
        Led::Off,
    };

    // Sidebar ------------------------------------------------------------------------------------------------------
    const bool wide = config->MenuSidebarWide.value_or_default();
    const float side = wide ? 260.f : 112.f;
    {
        dl->AddRectFilled(P(0, 0), P(side, H), Col(ImVec4(0.025f, 0.03f, 0.028f, 1)), U(6.0f), ImDrawFlags_RoundCornersLeft);
        float y = 70;

        for (int i = 0; i < kSectionCount; i++)
        {
            const ImVec2 p(8, y), q(side - 8, y + 96);
            At(p.x, p.y);
            ImGui::PushID(i);
            if (ImGui::InvisibleButton("nav", Sz(q.x - p.x, q.y - p.y)))
                section = i;
            const bool hov = ImGui::IsItemHovered();
            if (hov && !wide)
            {
                ImGui::PushFont(fUi, U(17.f));
                ImGui::SetTooltip("%s", kSections[i].name[li]);
                ImGui::PopFont();
            }
            ImGui::PopID();

            const bool sel = section == i;
            const ImVec4 top = sel ? ImVec4(0.11f, 0.075f, 0.04f, 1) : (hov ? pal::PanelHi : pal::Panel);
            const ImVec4 bottom = Mix(top, ImVec4(0, 0, 0, 1), 0.4f);
            dl->AddRectFilledMultiColor(P(p), P(q), Col(top), Col(top), Col(bottom), Col(bottom));
            if (sel)
                for (int k = 4; k >= 1; k--)
                    dl->AddRect(P(p.x - k, p.y - k), P(q.x + k, q.y + k), Col(pal::Orange, 0.07f * (5 - k)), U(7.f), 0, 1.f);
            dl->AddRect(P(p), P(q), Col(sel ? pal::Orange : pal::Line), U(6.f), 0, sel ? U(2.f) : 1.f);
            const ImU32 ic = Col(sel ? pal::Orange : (hov ? pal::Text : pal::Text2));

            if (wide)
            {
                // The full name when it fits, else smaller, else the short one of the compact bar
                Icon(i, ImVec2(p.x + 40, p.y + 48), 34.f, ic);
                const float room = q.x - p.x - 74 - 10;
                const char* name = kSections[i].name[li];
                float fs = 18.f;
                while (fs > 14.f && Measure(fUi, fs, name).x > room)
                    fs -= 1.f;
                if (Measure(fUi, fs, name).x > room)
                {
                    name = kSections[i].caps[li];
                    fs = 16.f;
                    while (fs > 11.f && Measure(fUi, fs, name).x > room)
                        fs -= 1.f;
                }
                const ImVec2 ts = Measure(fUi, fs, name);
                Text(fUi, fs, ImVec2(p.x + 74, p.y + 48 - ts.y * 0.5f), sel ? pal::Orange : pal::Text, name);
            }
            else
            {
                Icon(i, ImVec2((p.x + q.x) * 0.5f, p.y + 38), 34.f, ic);
                const char* caps = kSections[i].caps[li];
                float fs = 16.f;
                while (fs > 11.f && Measure(fUi, fs, caps).x > q.x - p.x - 8)
                    fs -= 1.f;
                const ImVec2 ts = Measure(fUi, fs, caps);
                Text(fUi, fs, ImVec2((p.x + q.x - ts.x) * 0.5f, p.y + 66), sel ? pal::Orange : pal::Text, caps);
            }

            // Activity pilot, apart from the selection
            if (sectionLed[i] != Led::Off)
                LedAt(dl, ImVec2(q.x - 14, p.y + 14), 5.f, sectionLed[i]);
            y += 104;
        }

        // Expand / collapse: the user's choice, kept in the ini; it never opens by itself
        At(8, H - 54);
        if (Button("##wide", wide ? Lx("«  Collapse", "«  Contraer", "«  Recolher", "«  Свернуть") : "»", ImVec2(side - 16, 40)))
        {
            config->MenuSidebarWide = !wide;
            config->SaveMenuPreferences();
        }
    }

    const float x0 = side + 16, x1 = W - 18;

    // Top strip: brand, the game and what it runs at ---------------------------------------------------------------
    {
        Text(fBrand, 46.f, ImVec2(20 + side, 4), pal::Green, "ILION");
        for (int i = 1; i <= 3; i++)
            dl->AddText(fBrand, U(46.f), P(20 + side, 4), Col(pal::Green, 0.06f), "ILION");
        const float brandW = Measure(fBrand, 46.f, "ILION").x;
        Text(fUi, 14.f, ImVec2(24 + side, 54), pal::Text2, "C O N T R O L   S Y S T E M");
        Text(fUi, 16.f, ImVec2(20 + side + brandW + 22, 14), pal::Dim, (std::string("v") + BuildInfo::IlionVersion()).c_str());

        // Update and config notices of the classic header, in the free part of the strip
        PushContentStyle();
        ImGui::PushFont(fUi, U(15.f));
        if (BeginArea("##ShellNotices", ImVec2(x0 + 360, 6), ImVec2(x1 - 250, 74)))
            RenderMainMenuHeaderMessages(ctx);
        ImGui::EndChild();

        // Language of the menu, the same list as before
        if (BeginArea("##ShellLanguage", ImVec2(x1 - 240, 14), ImVec2(x1, 70)))
        {
            PushComboStyle();
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, Sz(10.f, 8.f));
            RenderLanguageSelector(config);
            ImGui::PopStyleVar();
            PopComboStyle();
        }
        ImGui::EndChild();
        ImGui::PopFont();
        PopContentStyle();

        const ImVec2 a(x0, 78), b(x1, 238);
        PanelRect(a, b);
        const float fx = (std::max)(a.x + 560, b.x - 600);
        for (float x = a.x + 420; x < fx - 40; x += 14)
            dl->AddLine(P(x, a.y + 1), P(x - 60, b.y - 1), Col(pal::Line, 0.45f), 1.f);

        Text(fUi, 16.f, ImVec2(a.x + 22, a.y + 18), pal::Yellow, Lx("CURRENT GAME", "JUEGO ACTUAL", "JOGO ATUAL", "ТЕКУЩАЯ ИГРА"));
        const std::string game = !state.gameName.empty() ? state.gameName : state.gameExe;
        dl->PushClipRect(P(a.x, a.y), P(fx - 30, b.y), true);
        Text(fUi, 44.f, ImVec2(a.x + 22, a.y + 40), pal::Text, game.empty() ? "-" : game.c_str());
        dl->PopClipRect();

        std::string api = ApiName(state.api);
        if (IdentifyGpu::gameUsesDxvk())
            api += " (DXVK)";
        else if (IdentifyGpu::gameUsesVkd3dProton())
            api += " (VKD3D)";
        Text(fUi, 20.f, ImVec2(a.x + 22, a.y + 108), pal::Text2, api.c_str());
        const float sep = a.x + 22 + Measure(fUi, 20.f, api.c_str()).x + 18;
        dl->AddLine(P(sep, a.y + 110), P(sep, a.y + 130), Col(pal::Line), 1.f);
        // The menu is drawn by ILION inside the game: ILION is running
        LedAt(dl, ImVec2(sep + 20, a.y + 120), 6.f, Led::Green);
        Text(fUi, 20.f, ImVec2(sep + 34, a.y + 108), pal::Green, Lx("ILION ACTIVE", "ILION ACTIVO", "ILION ATIVO", "ILION АКТИВЕН"));

        // FPS and frame time measured by ILION in this game
        const double ft = ctx.frameTime;
        const bool measured = std::isfinite(ft) && ft > 0.0;
        Text(fUi, 18.f, ImVec2(fx, a.y + 16), pal::Text2, "FPS");
        Text(fUi, 64.f, ImVec2(fx - 4, a.y + 34), pal::Text, measured ? StrFmt("%.0f", 1000.0 / ft).c_str() : "-");
        Text(fUi, 16.f, ImVec2(fx, a.y + 110), pal::Text2, "Frame Time");
        const std::string ftText = measured ? StrFmt("%.1f ms", ft) : std::string("-");
        Text(fUi, 16.f, ImVec2(fx + 92, a.y + 110), pal::Green, ftText.c_str());
        const float gauge = measured ? (float) std::clamp(ft / 33.3, 0.0, 1.0) : 0.0f;
        dl->AddRectFilled(P(fx, a.y + 138), P(fx + 130, a.y + 142), Col(pal::Line));
        dl->AddRectFilled(P(fx, a.y + 138), P(fx + 130 * gauge, a.y + 142), Col(pal::Green));
        Text(fUi, 13.f, ImVec2(fx, a.y + 146), pal::Dim, Lx("measured by ILION", "medido por ILION", "medido pelo ILION", "измерено ILION"));

        // Frame time graph: the last 120 frames
        const ImVec2 ga(fx + 190, a.y + 20), gb(b.x - 22, a.y + 108);
        dl->AddRect(P(ga), P(gb), Col(pal::Line));
        const auto& times = state.frameTimes;
        const size_t count = (std::min)(times.size(), (size_t) 120);
        double top = 33.3;
        for (size_t i = times.size() - count; i < times.size(); i++)
            top = (std::max)(top, times[i]);
        top = std::ceil(top);
        Text(fUi, 14.f, ImVec2(ga.x - 26, ga.y - 2), pal::Text2, StrFmt("%.0f", top).c_str());
        Text(fUi, 14.f, ImVec2(ga.x - 26, ga.y + 36), pal::Text2, StrFmt("%.0f", top * 0.5).c_str());
        Text(fUi, 14.f, ImVec2(ga.x - 18, gb.y - 14), pal::Text2, "0");
        Text(fUi, 22.f, ImVec2(gb.x - Measure(fUi, 22.f, ftText.c_str()).x, a.y - 1), pal::Green, ftText.c_str());

        if (count > 1)
        {
            ImVec2 pts[120];
            for (size_t i = 0; i < count; i++)
            {
                const double v = std::clamp(times[times.size() - count + i], 0.0, top);
                pts[i] = P(ga.x + (gb.x - ga.x) * i / (float) (count - 1), gb.y - (gb.y - ga.y) * (float) (v / top));
            }
            dl->AddPolyline(pts, (int) count, Col(pal::Green), 0, U(1.6f));
        }

        // Hardware: what HWiNFO or Afterburner report (the readings of the overlay), RAM from Windows
        const auto snapshot = HwTelemetry::Get();
        const bool stale = snapshot.polledTick == 0 || GetTickCount64() - snapshot.polledTick > 5000;
        const auto reading = [&](HwTelemetry::Metric metric) -> const HwTelemetry::Value*
        {
            const auto& v = snapshot.values[metric];
            return !stale && v.valid ? &v : nullptr;
        };

        static ULONGLONG ramTick = 0;
        static MEMORYSTATUSEX memory {};
        if (GetTickCount64() - ramTick > 1000)
        {
            memory.dwLength = sizeof(memory);
            if (!GlobalMemoryStatusEx(&memory))
                memory.ullTotalPhys = 0;
            ramTick = GetTickCount64();
        }

        const auto* gpu = reading(HwTelemetry::GpuUsage);
        const auto* cpu = reading(HwTelemetry::CpuUsage);
        const auto* vramUsed = reading(HwTelemetry::VramUsed);
        const auto* vramTotal = reading(HwTelemetry::VramTotal);
        const std::string vram = vramUsed == nullptr ? std::string("-")
                                 : vramTotal != nullptr
                                     ? StrFmt("%.1f/%.0f GB", vramUsed->value / 1024.0f, vramTotal->value / 1024.0f)
                                     : StrFmt("%.1f GB", vramUsed->value / 1024.0f);
        const std::string ram = memory.ullTotalPhys == 0 ? std::string("-")
                                                         : StrFmt("%.1f/%.0f GB",
                                                                  (memory.ullTotalPhys - memory.ullAvailPhys) / 1073741824.0,
                                                                  memory.ullTotalPhys / 1073741824.0);
        const std::string line = StrFmt("GPU %s  |  CPU %s  |  VRAM %s  |  RAM %s",
                                        gpu != nullptr ? StrFmt("%.0f%%", gpu->value).c_str() : "-",
                                        cpu != nullptr ? StrFmt("%.0f%%", cpu->value).c_str() : "-", vram.c_str(), ram.c_str());
        Text(fUi, 15.f, ImVec2(ga.x, a.y + 124), pal::Text2, line.c_str());
    }

    // Section header with the interface mode -----------------------------------------------------------------------
    const Section& sec = kSections[section];
    {
        // A low band: icon, name and what the section is for on one line
        const ImVec2 a(x0, 252), b(x1, 302);
        PanelRect(a, b);
        Icon(section, ImVec2(a.x + 30, a.y + 25), 28.f, Col(pal::Green));
        Text(fBrand, 26.f, ImVec2(a.x + 58, a.y + 9), pal::Green, sec.caps[li]);
        const float subX = a.x + 58 + Measure(fBrand, 26.f, sec.caps[li]).x + 22;
        dl->AddLine(P(subX - 11, a.y + 15), P(subX - 11, a.y + 35), Col(pal::Line), 1.f);
        dl->PushClipRect(P(subX, a.y), P(b.x - 12, b.y), true);
        Text(fUi, 17.f, ImVec2(subX, a.y + 15), pal::Text2, sec.sub[li]);
        dl->PopClipRect();

    }

    const ImVec2 a(x0, 316), b(x1, H - 16);

    // INFO ILION: the tips of the info line and the two buttons of the classic footer
    const auto infoBar = [&](ImVec2 ia, ImVec2 ib)
    {
        PanelRect(ia, ib);
        const float midY = ia.y + (ib.y - ia.y) * 0.5f;
        const bool on = config->IlionInfoLine.value_or_default();
        Icon(8, ImVec2(ia.x + 34, midY), 26.f, Col(pal::Yellow));

        At(ia.x + 60, ia.y + 12);
        if (ImGui::InvisibleButton("##InfoOnOff", Sz(Measure(fUi, 19.f, "INFO ILION").x + 50, 30)))
        {
            config->IlionInfoLine = !on;
            config->SaveMenuPreferences();
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::PushFont(fUi, U(16.f));
            WrappedTooltip("%s", Tr("INFO ILION: short tips about the technologies ILION uses.\nOFF stops the line and its animation."));
            ImGui::PopFont();
        }
        Text(fUi, 19.f, ImVec2(ia.x + 66, ia.y + 16), pal::Yellow, on ? "INFO ILION" : "INFO ILION  (OFF)");

        const float buttons = 110 + 12 + 160 + 12 + 130;
        IlionInfo::Draw(dl, P(ia.x + 66, ia.y + 46), P(ib.x - buttons - 40, ia.y + 82), on, fUi, U(17.f), &Ilion::Tr,
                        Tone(pal::Text), Tone(pal::Yellow), Tone(pal::Bg), Tone(pal::Line));

        // The Wiki of OptiScaler, as the footer of the classic menu had it
        At(ib.x - buttons - 20, midY - 22);
        if (Button("##ShellWiki", "Wiki", ImVec2(110, 44)))
            ImGui::GetPlatformIO().Platform_OpenInShellFn(ImGui::GetCurrentContext(), "https://github.com/optiscaler/OptiScaler/wiki");
        At(ib.x - 160 - 12 - 130 - 20, midY - 22);
        if (Button("##ShellSave", Tr("Save Settings"), ImVec2(160, 44)))
            config->SaveIni();
        At(ib.x - 130 - 20, midY - 22);
        if (Button("##ShellClose", Tr("Close"), ImVec2(130, 44)))
            CloseMainMenu(io);
    };

    // Content of a tab of the classic menu inside a panel, in the palette of the new interface
    const auto tabPanel = [&](int tab, ImVec2 c, ImVec2 d, const char* id, const ShellLayout* layout, int flow)
    {
        PanelRect(c, d);
        PushContentStyle();
        ImGui::PushFont(fUi, U(17.f));
        if (BeginArea(id, ImVec2(c.x + 12, c.y + 12), ImVec2(d.x - 12, d.y - 12)))
        {
            // Controls of a proportionate length where a tab keeps one column
            ImGui::PushItemWidth((std::min)(ImGui::GetContentRegionAvail().x * 0.55f, U(460.f)));
            RenderMenuTabBody(ctx, tab, true, layout, flow);
            ImGui::PopItemWidth();
        }
        ImGui::EndChild();
        ImGui::PopFont();
        PopContentStyle();
    };

    // Sections ------------------------------------------------------------------------------------------------------
    if (section == 0)
    {
        // Home: Upscaling and Frame Generation on top, latency and FPS under them; the same functions as the modules
        const float rowSplit = a.y + 318, rowBottom = b.y - 120;
        const float half = a.x + (b.x - a.x) * 0.5f;

        const auto card = [&](const char* id, int icon, const char* title, Led led, ImVec2 p, ImVec2 q, const auto& controls)
        {
            PanelRect(p, q);
            Icon(icon, ImVec2(p.x + 30, p.y + 26), 26.f, Col(pal::Green));
            Text(fUi, 19.f, ImVec2(p.x + 56, p.y + 14), pal::Text, title);
            LedAt(dl, ImVec2(q.x - 22, p.y + 24), 6.f, led);
            dl->AddLine(P(p.x + 14, p.y + 48), P(q.x - 14, p.y + 48), Col(pal::Line));

            PushContentStyle();
            ImGui::PushFont(fUi, U(17.f));
            if (BeginArea(id, ImVec2(p.x + 16, p.y + 58), ImVec2(q.x - 12, q.y - 8)))
                controls();
            ImGui::EndChild();
            ImGui::PopFont();
            PopContentStyle();
        };

        const auto smallLabel = [&](const char* text)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, Tone(pal::Text2));
            ImGui::TextUnformatted(text);
            ImGui::PopStyleColor();
        };

        // Upscaling: technology and implementation, the catalogue and selection of the module
        card("##HomeUp", 1, kSections[1].name[li], sectionLed[1], ImVec2(a.x, a.y), ImVec2(half - 8, rowSplit),
             [&]()
             {
                 static const int order[] = { FamFsr4, FamHelix, FamDlss4, FamXess, FamFsr31, FamFsr2, FamFfx };
                 const auto catalog = GatherUpscalers(feature);
                 if (upFamily < 0 || upFamily >= FamCount || !catalog.present[upFamily])
                     upFamily = catalog.current >= 0 ? catalog.current : FamFsr4;

                 const float gap = ImGui::GetStyle().ItemSpacing.x;
                 const float w = (ImGui::GetContentRegionAvail().x - gap) * 0.5f;
                 const float x0 = ImGui::GetCursorPosX();

                 smallLabel(Lx("Technology", "Tecnología", "Tecnologia", "Технология"));
                 ImGui::SameLine(x0 + w + gap);
                 smallLabel(Lx("Implementation", "Implementación", "Implementação", "Реализация"));

                 const bool locked = FfxApiProxy::SrSwitchLocked();
                 ImGui::BeginDisabled(locked);
                 ImGui::SetNextItemWidth(w);
                 if (IlionTheme::BeginCombo("##HomeFamily", familyNames[upFamily]))
                 {
                     for (const int family : order)
                     {
                         if (catalog.present[family] && ImGui::Selectable(familyNames[family], family == upFamily))
                             upFamily = family;
                     }
                     ImGui::EndCombo();
                 }

                 ImGui::SameLine(x0 + w + gap);
                 std::string preview = Lx("Choose one", "Elige uno", "Escolha um", "Выберите");
                 int previewRank = 0, available = 0;
                 for (const auto& v : catalog.variants)
                 {
                     if (v.family != upFamily)
                         continue;
                     available += v.usable ? 1 : 0;
                     const int value = v.status.active ? 3 : v.status.loaded ? 2 : v.status.selected ? 1 : 0;
                     if (value > previewRank)
                     {
                         previewRank = value;
                         preview = v.name;
                     }
                 }

                 ImGui::SetNextItemWidth(w);
                 if (IlionTheme::BeginCombo("##HomeVariant", preview.c_str()))
                 {
                     for (const auto& v : catalog.variants)
                     {
                         if (v.family != upFamily)
                             continue;
                         const char* word = StatusWord(v.status);
                         std::string label = v.name;
                         if (*word != 0)
                             label += StrFmt("  [%s]", Tr(word));
                         label += "###" + (v.entry != nullptr ? wstring_to_string(v.entry->key) : v.isDefault ? std::string("default") : v.name);
                         ImGui::BeginDisabled(!v.usable);
                         if (ImGui::Selectable(label.c_str(), v.status.selected || v.status.active) && !(v.status.selected && v.status.loaded))
                         {
                             const bool d4r = v.entry != nullptr && v.entry->kind == IlionUpscalers::Kind::D4r;
                             ChooseUpscalerVariant(v);
                             SetApplied(d4r && !State::Instance().dlssExternalBackend ? Apply::Restart : Apply::Reinit);
                         }
                         ImGui::EndDisabled();
                         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                             UpscalerVariantTip(v);
                     }
                     ImGui::EndCombo();
                 }
                 ImGui::EndDisabled();
                 DrawD4rWarning();

                 ImGui::Spacing();
                 const int rank = FamilyRank(catalog, upFamily);
                 Pill(rank == 3   ? Lx("Loaded and active", "Cargado y activo", "Carregado e ativo", "Загружен и активен")
                      : rank == 2 ? Lx("Loaded", "Cargado", "Carregado", "Загружен")
                      : rank == 1 ? Lx("Chosen, not running yet", "Elegido, aún sin ejecutarse", "Escolhido, ainda não em execução",
                                       "Выбран, ещё не работает")
                                  : Lx("Not in use", "Sin usar", "Sem uso", "Не используется"),
                      LedOfRank(rank));
                 ImGui::SameLine(0, U(16));
                 ImGui::SetCursorPosY(ImGui::GetCursorPosY() + U(6));
                 Label(StrFmt("%d %s", available, Lx("implementations available", "implementaciones disponibles", "implementações disponíveis",
                                                     "доступных реализаций"))
                           .c_str(),
                       pal::Text2, 15.f);

                 // What runs: resolution, factor and the upscaler's own time
                 if (upscalerOn && feature->RenderWidth() > 0)
                 {
                     const float factor = (float) feature->TargetWidth() / feature->RenderWidth();
                     Label(StrFmt("%s  ·  %ux%u -> %ux%u", UpscalerLabel(feature).c_str(), feature->RenderWidth(), feature->RenderHeight(),
                                  feature->TargetWidth(), feature->TargetHeight())
                               .c_str(),
                           pal::Text, 17.f);

                     double sum = 0.0;
                     int valid = 0;
                     const auto& times = state.upscaleTimes;
                     for (size_t k = times.size() > 60 ? times.size() - 60 : 0; k < times.size(); k++)
                     {
                         if (times[k] > 0.0)
                         {
                             sum += times[k];
                             valid++;
                         }
                     }
                     std::string line = StrFmt("%s %.2fx  ·  %.0f%% %s", Lx("Factor", "Factor", "Fator", "Коэффициент"), factor,
                                               100.0f / (factor * factor),
                                               Lx("of the pixels rendered", "de los píxeles renderizados", "dos pixels renderizados", "пикселей отрисовано"));
                     if (valid > 0)
                         line += StrFmt("  ·  %s %.2f ms", Lx("upscaler", "escalador", "upscaler", "апскейлер"), sum / valid);
                     Label(line.c_str(), pal::Text2, 15.f);
                 }
                 else
                     Label(Lx("No upscaler is running in this game right now.", "Ningún escalador se está ejecutando ahora en este juego.",
                              "Nenhum upscaler está em execução neste jogo agora.", "Сейчас в этой игре апскейлер не работает."),
                           pal::Text2, 15.f);
                 if (locked)
                     Label(Tr("It can't be changed while a benchmark is running."), pal::Yellow, 15.f);
                 ApplyState(Q(ImGui::GetCursorScreenPos()));
             });

        // Frame generation: input, output and variant (the module's selectors and their conditions), then on/off,
        // the multiplier and the states
        card("##HomeFg", 2, kSections[2].name[li], fgLed, ImVec2(half + 8, a.y), ImVec2(b.x, rowSplit),
             [&]()
             {
                 shellFgInstrumentsApart = true;
                 fgSelectorsOnly = true;
                 RenderFrameGenerationSelection(ctx);
                 fgSelectorsOnly = false;
                 shellFgInstrumentsApart = false;
                 ImGui::Spacing();
                 RenderFgQuickControls(ctx);
             });

        // Latency and FPS: the quick switches of fakenvapi (XeLL, LatencyFleX, Reflex), V-Sync, the FPS limit and the overlay
        card("##HomeLat", 3, kSections[3].name[li], sectionLed[3], ImVec2(a.x, rowSplit + 16), ImVec2(b.x, rowBottom),
             [&]()
             {
                 if (ImGui::BeginTable("##HomeLatCols", 3, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings))
                 {
                     ImGui::TableNextColumn();
                     ImGui::BeginDisabled(IlionLatency::Current() != IlionLatency::Manual);
#ifdef LOW_LATENCY_INPUTS
                     RenderLowLatencySettings(ctx);
#else
                     RenderFakenvapiSettings(ctx);
#endif
                     ImGui::EndDisabled();
                     if (IlionLatency::Current() != IlionLatency::Manual)
                         TextWrappedColored(Tone(pal::Text2), "%s",
                                            Lx("Intelligent activation chooses these: set it to Manual in Latency & FPS to change them.",
                                               "La activación inteligente los elige: ponla en Manual en Latencia y FPS para cambiarlos.",
                                               "A ativação inteligente os escolhe: coloque em Manual em Latência e FPS para alterá-los.",
                                               "Их выбирает умная активация: переключите её в Ручной режим в «Задержка и FPS»."));

                     ImGui::TableNextColumn();
                     RenderVsyncSettings(ctx);

                     ImGui::TableNextColumn();
                     // FPS limit: the same value and key as Latency & FPS
                     if (std::isinf(_limitFps))
                         _limitFps = config->FramerateLimit.value_or_default();
                     smallLabel(Lx("FPS limit (0 = none)", "Límite de FPS (0 = sin límite)", "Limite de FPS (0 = sem limite)", "Лимит FPS (0 = нет)"));
                     const float apply = ButtonWidth(TrL("Apply Limit"));
                     ImGui::SetNextItemWidth((std::max)(U(80), ImGui::GetContentRegionAvail().x - apply - ImGui::GetStyle().ItemSpacing.x));
                     ImGui::SliderFloat("##HomeFpsLimit", &_limitFps, 0, (std::max)(240.0f, _limitFps), "%.0f");
                     ImGui::SameLine();
                     if (IlionTheme::Button(TrL("Apply Limit")))
                         config->FramerateLimit = _limitFps;

                     ImGui::Spacing();
                     bool shown = config->ShowFps.value_or_default();
                     if (IlionTheme::Checkbox(Lx("Performance overlay###HomeOverlay", "Overlay de rendimiento###HomeOverlay",
                                                 "Overlay de desempenho###HomeOverlay", "Оверлей производительности###HomeOverlay"),
                                              &shown))
                         config->ShowFps = shown;
                     ImGui::EndTable();
                 }
             });

        infoBar(ImVec2(a.x, b.y - 104), b);
    }
    else if (section == 1)
    {
        // Upscaling: the catalogue of the families ILION has here, and the panel of the one picked
        const auto catalog = GatherUpscalers(feature);
        static const int order[] = { FamFsr4, FamHelix, FamDlss4, FamXess, FamFsr31, FamFsr2, FamFfx };

        if (upFamily < 0 || upFamily >= FamCount || !catalog.present[upFamily])
            upFamily = catalog.current >= 0 ? catalog.current : FamFsr4;

        const float split = a.x + (b.x - a.x) * 0.36f;
        const ImVec2 la = a, lb(split, b.y);
        PanelRect(la, lb);
        Text(fUi, 20.f, ImVec2(la.x + 20, la.y + 18), pal::Text,
             Lx("UPSCALER CATALOGUE", "CATÁLOGO DE ESCALADORES", "CATÁLOGO DE UPSCALERS", "КАТАЛОГ АПСКЕЙЛЕРОВ"));

        const char* filters[] = { Lx("All", "Todos", "Todos", "Все"), "AMD", "NVIDIA", "Intel",
                                  Lx("Community", "Comunidad", "Comunidade", "Сообщество") };
        At(lb.x - 20 - 170, la.y + 10);
        PushComboStyle();
        ImGui::SetNextItemWidth(U(170));
        if (ImGui::BeginCombo("##UpFilter", filters[upFilter], ImGuiComboFlags_HeightLarge))
        {
            for (int i = 0; i < 5; i++)
                if (ImGui::Selectable(filters[i], i == upFilter))
                    upFilter = i;
            ImGui::EndCombo();
        }
        PopComboStyle();

        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, Tone(pal::Line));
        if (BeginArea("##UpList", ImVec2(la.x + 12, la.y + 66), ImVec2(lb.x - 12, lb.y - 12)))
        {
            for (const int family : order)
            {
                if (!catalog.present[family])
                    continue;
                const FamilyLook look = LookOf(family);
                if (upFilter > 0 && look.filter != upFilter)
                    continue;

                const ImVec2 p = Q(ImGui::GetCursorScreenPos());
                const float w = ImGui::GetContentRegionAvail().x / S - 6;
                ImGui::PushID(family);
                if (ImGui::InvisibleButton("row", Sz(w, 66)))
                    upFamily = family;
                const bool hov = ImGui::IsItemHovered();
                ImGui::PopID();

                const bool on = upFamily == family;
                ImDrawList* ldl = ImGui::GetWindowDrawList();
                const ImVec2 q(p.x + w, p.y + 66);
                ldl->AddRectFilled(P(p), P(q), Col(on ? Mix(pal::Panel, pal::Green, 0.07f) : (hov ? pal::PanelHi : pal::Panel)), U(4.f));
                if (on)
                    for (int k = 3; k >= 1; k--)
                        ldl->AddRect(P(p.x - k + 1, p.y - k + 1), P(q.x + k - 1, q.y + k - 1), Col(pal::Green, 0.08f * (4 - k)), U(5.f));
                ldl->AddRect(P(p), P(q), Col(on ? pal::Green : pal::Line), U(4.f), 0, on ? U(1.8f) : 1.f);
                Badge(ImVec2(p.x + 12, p.y + 9), 48.f, look.badge, look.tint);
                Text(fUi, 21.f, ImVec2(p.x + 76, p.y + 10), pal::Text, familyNames[family]);
                Text(fUi, 16.f, ImVec2(p.x + 76, p.y + 38), pal::Text2, look.subtitle);
                LedAt(ldl, ImVec2(q.x - 56, p.y + 33), 7.f, LedOfRank(FamilyRank(catalog, family)));
                Icon(9, ImVec2(q.x - 24, p.y + 33), 22.f, Col(pal::Text2));
                ImGui::Dummy(Sz(0, 4));
            }
        }
        ImGui::EndChild();
        ImGui::PopStyleColor(3);

        // Panel of the family picked: its state and its upscalers; the selection runs through ILION's own selector
        const ImVec2 pa(split + 16, a.y), pb = b;
        const UpscalerVariant* best = nullptr;
        const int rank = FamilyRank(catalog, upFamily, &best);
        const FamilyLook look = LookOf(upFamily);
        bool usable = false;
        std::string reason;
        for (const auto& v : catalog.variants)
        {
            if (v.family != upFamily)
                continue;
            usable = usable || v.usable;
            if (!v.usable && reason.empty())
                reason = v.reason;
        }

        PanelRect(pa, ImVec2(pb.x, pa.y + 104));
        Badge(ImVec2(pa.x + 16, pa.y + 14), 76.f, look.badge, look.tint);
        dl->PushClipRect(P(pa.x, pa.y), P(pb.x - 190, pa.y + 104), true);
        Text(fUi, 32.f, ImVec2(pa.x + 112, pa.y + 14), pal::Text,
             (std::string(familyNames[upFamily]) + " - " + look.subtitle).c_str());
        dl->PopClipRect();

        At(pa.x + 112, pa.y + 58);
        const char* stateText = rank == 3   ? Lx("Loaded and active", "Cargado y activo", "Carregado e ativo", "Загружен и активен")
                                : rank == 2 ? Lx("Loaded", "Cargado", "Carregado", "Загружен")
                                : rank == 1 ? Lx("Chosen, not running yet", "Elegido, aún sin ejecutarse", "Escolhido, ainda não em execução",
                                                 "Выбран, ещё не работает")
                                : usable    ? Lx("Available", "Disponible", "Disponível", "Доступен")
                                            : Lx("Not available here", "No disponible aquí", "Indisponível aqui", "Здесь недоступен");
        Pill(stateText, usable || rank > 0 ? LedOfRank(rank) : Led::Red);
        ImGui::SameLine(0, U(18));
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + U(6));
        const float pillEnd = Q(ImGui::GetCursorScreenPos()).x;
        dl->PushClipRect(P(pillEnd, pa.y + 50), P(pb.x - 190, pa.y + 104), true);
        Label(usable ? Lx("Compatible with this game and GPU", "Compatible con este juego y GPU", "Compatível com este jogo e GPU",
                          "Совместим с этой игрой и GPU")
                     : reason.c_str(),
              usable ? pal::Text2 : pal::Red, 16.f);
        dl->PopClipRect();

        At(pb.x - 176, pa.y + 28);
        if (Button("##UpInfo", Lx("Information", "Información", "Informação", "Информация"), ImVec2(156, 46), upDiagnostics))
            upDiagnostics = !upDiagnostics;

        const ImVec2 c(pa.x, pa.y + 118), dFull(pb.x, pb.y - 118);
        const float mx = c.x + (dFull.x - c.x) * 0.57f;
        const ImVec2 d(mx - 8, dFull.y);
        PanelRect(c, d);

        // Upscaling monitor: what runs, from the running feature and the dispatch diagnostics
        {
            const ImVec2 m0(mx + 8, c.y), m1(dFull.x, dFull.y);
            PanelRect(m0, m1, false, &pal::Green);
            dl->PushClipRect(P(m0.x + 2, m0.y), P(m1.x - 10, m1.y), true);
            Text(fUi, 15.f, ImVec2(m0.x + 18, m0.y + 14), pal::Text2,
                 Lx("UPSCALING MONITOR", "MONITOR DE ESCALADO", "MONITOR DE ESCALA", "МОНИТОР МАСШТАБИРОВАНИЯ"));
            LedAt(dl, ImVec2(m1.x - 22, m0.y + 22), 6.f, upscalerOn ? Led::Green : upscalerIdle ? Led::Yellow : Led::Off);

            const std::string tech = upscalerOn || upscalerIdle ? UpscalerLabel(feature) : std::string(Tr("None"));
            Text(fUi, 30.f, ImVec2(m0.x + 18, m0.y + 40), pal::Text, tech.c_str());

            std::string impl = "-";
            if (upscalerOn && RunsSwitchedFfx(feature->GetUpscalerType()))
                impl = Tr(FfxApiProxy::SrDiagGetSnapshot().activeName.c_str());
            else if (best != nullptr && rank == 3)
                impl = best->name;
            Text(fUi, 17.f, ImVec2(m0.x + 18, m0.y + 82), pal::Text2,
                 (std::string(Lx("Implementation", "Implementación", "Implementação", "Реализация")) + ":  " + impl).c_str());

            const char* runState = upscalerOn     ? Lx("Running", "En ejecución", "Em execução", "Работает")
                                   : upscalerIdle ? Tr("Idle")
                                                  : Lx("Not running", "Sin ejecutar", "Sem executar", "Не работает");
            At(m0.x + 18, m0.y + 112);
            Pill(runState, upscalerOn ? Led::Green : upscalerIdle ? Led::Yellow : Led::Off);

            if (upscalerOn && feature->RenderWidth() > 0)
            {
                Text(fUi, 24.f, ImVec2(m0.x + 18, m0.y + 160), pal::Text,
                     StrFmt("%ux%u  ->  %ux%u", feature->RenderWidth(), feature->RenderHeight(), feature->TargetWidth(), feature->TargetHeight()).c_str());
                const float factor = (float) feature->TargetWidth() / feature->RenderWidth();
                Text(fUi, 16.f, ImVec2(m0.x + 18, m0.y + 194), pal::Text2,
                     StrFmt("%s %.2fx  ·  %s %.0f%%", Lx("Factor", "Factor", "Fator", "Коэффициент"), factor,
                            Lx("rendered pixels", "píxeles renderizados", "pixels renderizados", "отрисовано пикселей"), 100.0f / (factor * factor))
                         .c_str());
            }

            // Upscaler GPU time: the average of the last frames and their graph, as measured by OptiScaler
            const auto& times = state.upscaleTimes;
            const size_t count = (std::min)(times.size(), (size_t) 120);
            double sum = 0.0, top = 1.0;
            int valid = 0;
            for (size_t i = times.size() - count; i < times.size(); i++)
            {
                if (times[i] > 0.0)
                {
                    sum += times[i];
                    valid++;
                    top = (std::max)(top, times[i]);
                }
            }
            if (upscalerOn && valid > 0)
            {
                const float gy = m0.y + 228;
                Text(fUi, 16.f, ImVec2(m0.x + 18, gy), pal::Text2, Lx("Upscaler time", "Tiempo del escalador", "Tempo do upscaler", "Время апскейлера"));
                const std::string avg = StrFmt("%.2f ms", sum / valid);
                Text(fUi, 20.f, ImVec2(m1.x - 18 - Measure(fUi, 20.f, avg.c_str()).x, gy - 2), pal::Green, avg.c_str());
                const ImVec2 ga(m0.x + 18, gy + 28), gb(m1.x - 18, (std::min)(gy + 110, m1.y - 16));
                if (gb.y > ga.y + 20 && count > 1)
                {
                    dl->AddRect(P(ga), P(gb), Col(pal::Line));
                    ImVec2 pts[120];
                    for (size_t i = 0; i < count; i++)
                    {
                        const double v = std::clamp(times[times.size() - count + i], 0.0, top);
                        pts[i] = P(ga.x + (gb.x - ga.x) * i / (float) (count - 1), gb.y - (gb.y - ga.y) * (float) (v / top));
                    }
                    dl->AddPolyline(pts, (int) count, Col(pal::Green), 0, U(1.4f));
                }
            }
            dl->PopClipRect();
        }

        // Version / implementation: the upscalers of the family, with their state, chosen as the classic selector does
        At(c.x + 18, c.y + 18);
        Label(Lx("Version / Implementation", "Versión / Implementación", "Versão / Implementação", "Версия / реализация"), pal::Text, 18.f);

        const bool locked = FfxApiProxy::SrSwitchLocked();
        std::string preview = "-";
        int previewRank = -1;
        for (const auto& v : catalog.variants)
        {
            if (v.family != upFamily)
                continue;
            const int value = v.status.active ? 3 : v.status.loaded ? 2 : v.status.selected ? 1 : 0;
            if (value > previewRank)
            {
                previewRank = value;
                preview = value > 0 ? v.name : std::string(Lx("Choose one", "Elige uno", "Escolha um", "Выберите"));
            }
        }

        const float rescanW = 150;
        At(c.x + 18, c.y + 46);
        ImGui::BeginDisabled(locked);
        PushComboStyle();
        ImGui::SetNextItemWidth(U(d.x - c.x - 36 - rescanW - 12));
        if (ImGui::BeginCombo("##UpVariant", preview.c_str(), ImGuiComboFlags_HeightLarge))
        {
            for (const auto& v : catalog.variants)
            {
                if (v.family != upFamily)
                    continue;

                const char* word = StatusWord(v.status);
                std::string label = v.name;
                if (*word != 0)
                    label += StrFmt("  [%s]", Tr(word));
                label += "###" + (v.entry != nullptr ? wstring_to_string(v.entry->key) : v.isDefault ? std::string("default") : v.name);

                ImGui::BeginDisabled(!v.usable);
                if (ImGui::Selectable(label.c_str(), v.status.selected || v.status.active) && !(v.status.selected && v.status.loaded))
                {
                    const bool d4r = v.entry != nullptr && v.entry->kind == IlionUpscalers::Kind::D4r;
                    ChooseUpscalerVariant(v);
                    // d4r is loaded when the game starts; everything else is created again while playing
                    SetApplied(d4r && !State::Instance().dlssExternalBackend ? Apply::Restart : Apply::Reinit);
                }
                ImGui::EndDisabled();

                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                {
                    ImGui::PushFont(fUi, U(16.f));
                    UpscalerVariantTip(v);
                    ImGui::PopFont();
                }
            }
            ImGui::EndCombo();
        }
        PopComboStyle();
        ImGui::EndDisabled();
        DrawD4rWarning();

        At(d.x - 18 - rescanW, c.y + 46);
        if (Button("##UpRescan", Tr("Rescan"), ImVec2(rescanW, 46)))
            RescanSrVariants();

        float y = c.y + 104;
        if (locked)
        {
            At(c.x + 18, y);
            Label(Tr("It can't be changed while a benchmark is running."), pal::Yellow, 16.f);
            y += 26;
        }

        // Its own options (those of the Upscaling tab) and the diagnostics, in an area that scrolls
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, Tone(pal::Line));
        if (BeginArea("##UpFolds", ImVec2(c.x + 18, y), ImVec2(d.x - 6, d.y - 56)))
        {
            const float fw = ImGui::GetContentRegionAvail().x / S - 12;

            // The settings of the upscaler that runs (model, presets, options of each technology); the selector is the
            // one above, the name of the card is the panel's own
            PushContentStyle();
            ImGui::PushFont(fUi, U(17.f));
            shellHidesSelector = true;
            skipSectionTitle = "Upscalers";
            shellHidesUpscalerDiag = true;
            RenderUpscalerStateMessage(ctx);
            if (IsUpscalerActive(feature))
                RenderActiveUpscalerSettings(ctx);
            skipSectionTitle = nullptr;
            shellHidesSelector = false;
            shellHidesUpscalerDiag = false;
            ImGui::PopFont();
            PopContentStyle();

            ImGui::Dummy(Sz(0, 8));

            if (IsUpscalerActive(feature) &&
                Fold("##UpAdv", Lx("Advanced options", "Opciones avanzadas", "Opções avançadas", "Расширенные параметры"), &upAdvanced, 10, fw))
            {
                PushContentStyle();
                ImGui::PushFont(fUi, U(17.f));
                ImGui::Indent(U(10));
                RenderUpscaleRatioSettings(ctx);
                ImGui::Spacing();
                RenderOutputScalingSettings(ctx);
                ImGui::Unindent(U(10));
                ImGui::PopFont();
                PopContentStyle();
            }

            ImGui::Dummy(Sz(0, 6));

            if (Fold("##UpDiag", Lx("Technical diagnostics", "Diagnóstico técnico", "Diagnóstico técnico", "Техническая диагностика"),
                     &upDiagnostics, 8, fw))
            {
                ImGui::Indent(U(10));
                ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + U(fw - 30));
                if (best != nullptr && best->entry != nullptr && !best->entry->fileVersion.empty())
                    Label(StrFmt("%s: %s", Tr("File version"), best->entry->fileVersion.c_str()).c_str(), pal::Dim, 16.f);
                Label(Lx("DLL path, hash and hot-swap diagnostics: Status & compatibility > Status.",
                         "Ruta de la DLL, hash y diagnóstico del cambio en caliente: Estado y compatibilidad > Estado.",
                         "Caminho da DLL, hash e diagnóstico da troca a quente: Estado e compatibilidade > Estado.",
                         "Путь DLL, хеш и диагностика горячей замены: Состояние и совместимость > Состояние."),
                      pal::Dim, 15.f);

                if (upscalerOn)
                {
                    Label(StrFmt("%s  %ux%u -> %ux%u", UpscalerLabel(feature).c_str(), feature->RenderWidth(), feature->RenderHeight(),
                                 feature->TargetWidth(), feature->TargetHeight())
                              .c_str(),
                          pal::Dim, 16.f);

                    if (RunsSwitchedFfx(feature->GetUpscalerType()))
                    {
                        const auto diag = FfxApiProxy::SrDiagGetSnapshot();
                        std::string value = Tr(diag.activeName.c_str());
                        if (diag.statusLevel != 0)
                            value += std::string("  (") + Tr(diag.status.c_str()) + ")";
                        Label(StrFmt("%s: %s", Tr("Implementation"), value.c_str()).c_str(), diag.statusLevel == 0 ? pal::Dim : pal::Yellow,
                              16.f);
                    }
                }
                if (upscalerOn && !state.detailedGpuTimes.empty())
                {
                    PushContentStyle();
                    ImGui::PushFont(fUi, U(16.f));
                    RenderShaderTimes(ctx);
                    ImGui::PopFont();
                    PopContentStyle();
                }
                if (!upscalerOn)
                    Label(Lx("No upscaler is running in this game right now.", "Ningún escalador se está ejecutando ahora en este juego.",
                             "Nenhum upscaler está em execução neste jogo agora.", "Сейчас в этой игре апскейлер не работает."),
                          pal::Dim, 16.f);
                ImGui::PopTextWrapPos();
                ImGui::Unindent(U(10));
            }
        }
        ImGui::EndChild();
        ImGui::PopStyleColor(3);

        ApplyState(ImVec2(c.x + 18, d.y - 46));
        infoBar(ImVec2(pa.x, pb.y - 104), pb);
    }
    else
    {
        // The other modules: their sub-sections as the secondary navigation, each tab drawn by its own code in the
        // layout of the new interface
        int shown[8];
        int shownCount = 0;
        for (int k = 0; k < subCount && shownCount < 8; k++)
        {
            if (subs[k].section == section && tabVisible(tabIndex(subs[k].tab)))
                shown[shownCount++] = k;
        }

        if (shownCount > 0)
        {
            if (std::none_of(shown, shown + shownCount, [&](int k) { return k == subTab[section]; }))
                subTab[section] = shown[0];

            const Sub& sub = subs[subTab[section]];

            // The ReShade panel belongs to its sub-section: leaving it closes the panel
            if (subTab[section] != subReShade)
            {
                reshadePanelOpen = false;
                reshadePanelAmount = 0.0f;
            }

            float x = a.x;
            if (shownCount > 1)
            {
                for (int n = 0; n < shownCount; n++)
                {
                    const int k = shown[n];
                    const char* label = subs[k].name[li];
                    const float w = Measure(fUi, 18.f, label).x + 40;
                    At(x, a.y);
                    ImGui::PushID(k);
                    if (Button("##sub", label, ImVec2(w, 44), subTab[section] == k, pal::Yellow, 18.f))
                        subTab[section] = k;
                    ImGui::PopID();
                    x += w + 8;
                }
            }

            const float top = shownCount > 1 ? a.y + 60 : a.y;
            shellFgInstrumentsApart = sub.layout == &lFrameGen;
            tabPanel(tabIndex(sub.tab), ImVec2(a.x, top), ImVec2(b.x, b.y - 118), "##ShellTab", sub.layout, sub.flow);
            shellFgInstrumentsApart = false;
        }
        else
        {
            const ImVec2 d(b.x, b.y - 118);
            PanelRect(a, d);
            At(a.x + 18, a.y + 18);
            Label(Lx("Nothing to show in this section for this game.", "Nada que mostrar en esta sección para este juego.",
                     "Nada a mostrar nesta seção para este jogo.", "В этом разделе для этой игры ничего нет."),
                  pal::Text2, 18.f);
        }

        infoBar(ImVec2(a.x, b.y - 104), b);
    }

    ImGui::End();
    ImGui::PopStyleColor(2);
}

#pragma execution_character_set(pop)
