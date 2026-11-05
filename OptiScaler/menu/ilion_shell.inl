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
// ILION dev.73: the Compact presentation's window, in the same design units (so the same scale and fonts)
constexpr float kCompactW = 720.0f;
constexpr float kCompactH = 960.0f;

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

// ILION dev.70: the palette of the theme in use (IlionTheme::SetShellTheme), with the same names as before
namespace pal
{
inline const ImVec4& Bg = IlionTheme::ShellPal::Bg;
inline const ImVec4& Panel = IlionTheme::ShellPal::Panel;
inline const ImVec4& PanelHi = IlionTheme::ShellPal::PanelHi;
inline const ImVec4& Line = IlionTheme::ShellPal::Line;
inline const ImVec4& Text = IlionTheme::ShellPal::Text;
inline const ImVec4& Text2 = IlionTheme::ShellPal::Text2;
inline const ImVec4& Dim = IlionTheme::ShellPal::Dim;
inline const ImVec4& Green = IlionTheme::ShellPal::Green;
inline const ImVec4& Orange = IlionTheme::ShellPal::Orange;
inline const ImVec4& Yellow = IlionTheme::ShellPal::Yellow;
inline const ImVec4& Red = IlionTheme::ShellPal::Red;
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
        return IlionTheme::ShellPilot(pal::Green); // ILION dev.72: lemon in Clean Olive
    case Led::Yellow:
        return IlionTheme::ShellPilot(pal::Yellow); // ILION dev.72: golden amber in Clean Olive
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

    if (l != Led::Off && IlionTheme::shellLemonPilots)
    {
        // ILION dev.73: Clean Olive: a short halo close to the lens, a thin dark rim against the olive, a brighter core
        for (int i = 3; i >= 1; i--)
            dl->AddCircleFilled(pc, U(r + i * r * 0.35f), Col(col, 0.10f * (4 - i)));
        dl->AddCircle(pc, U(r) + 1.0f, Col(ImVec4(0, 0, 0, 0.55f)), 0, 1.5f);
        dl->AddCircleFilled(pc, U(r), Col(col));
        dl->AddCircleFilled(pc, U(r * 0.5f), Col(Mix(col, ImVec4(1, 1, 1, 1), 0.55f)));
        dl->AddCircleFilled(P(c.x - r * 0.3f, c.y - r * 0.3f), U(r * 0.3f), Col(ImVec4(1, 1, 1, 0.65f)));
    }
    else if (l != Led::Off)
    {
        for (int i = 4; i >= 1; i--)
            dl->AddCircleFilled(pc, U(r + i * r * 0.55f), Col(col, 0.05f * (5 - i) * IlionTheme::shellGlow));
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
            dl->AddRect(P(p.x - i, p.y - i), P(q.x + i, q.y + i), Col(accent, 0.06f * (4 - i) * IlionTheme::shellGlow), U(5.f), 0, 1.f);
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

// ILION: one line of parts, each in its own colour (a reading's values apart from its words), as Label: no wrapping
static void LabelParts(std::initializer_list<std::pair<std::string, ImVec4>> parts, float size = 17.f)
{
    ImGui::PushFont(fUi, U(size));
    bool first = true;
    for (const auto& [text, colour] : parts)
    {
        if (text.empty())
            continue;
        if (!first)
            ImGui::SameLine(0.0f, 0.0f);
        first = false;
        ImGui::TextColored(Tone(colour), "%s", text.c_str());
    }
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
    case 11: // profiles: a card with a check
        dl->AddRect(p(-h * 0.8f, -h), p(h * 0.8f, h), col, U(3.f), 0, t);
        dl->AddLine(p(-h * 0.45f, -h * 0.5f), p(h * 0.45f, -h * 0.5f), col, t);
        dl->AddLine(p(-h * 0.45f, -h * 0.1f), p(h * 0.2f, -h * 0.1f), col, t);
        dl->AddLine(p(-h * 0.35f, h * 0.45f), p(-h * 0.08f, h * 0.7f), col, t);
        dl->AddLine(p(-h * 0.08f, h * 0.7f), p(h * 0.45f, h * 0.2f), col, t);
        break;
    case 12: // community: three people, the middle one in front
        dl->AddCircle(p(0, -h * 0.42f), h * 0.3f, col, 0, t);
        dl->AddBezierCubic(p(-h * 0.55f, h * 0.75f), p(-h * 0.5f, h * 0.05f), p(h * 0.5f, h * 0.05f), p(h * 0.55f, h * 0.75f), col, t);
        dl->AddCircle(p(-h * 0.72f, -h * 0.22f), h * 0.22f, col, 0, t);
        dl->AddCircle(p(h * 0.72f, -h * 0.22f), h * 0.22f, col, 0, t);
        dl->AddLine(p(-h * 1.0f, h * 0.55f), p(-h * 0.72f, h * 0.18f), col, t);
        dl->AddLine(p(h * 1.0f, h * 0.55f), p(h * 0.72f, h * 0.18f), col, t);
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
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, Sz(10.f, 7.f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, Sz(8.f, 6.f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, Sz(14.f, 11.f));
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
static void ApplyState(ImVec2 p, float wrapWidth = 0.0f)
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
    if (wrapWidth > 0.0f)
    {
        ImGui::PushFont(fUi, U(15.f));
        ImGui::PushStyleColor(ImGuiCol_Text, Col(lastApply == Apply::Hot ? pal::Green : pal::Yellow));
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + U(wrapWidth));
        ImGui::TextWrapped("%s", t);
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
        ImGui::PopFont();
    }
    else
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
    // ILION Perfiles (prototipo): appended, so every index above keeps its meaning; shown right after Home
    { { "PROFILES", "PERFILES", "PERFIS", "ПРОФИЛИ" }, { "Profiles", "Perfiles", "Perfis", "Профили" },
      { "Detect the game and hardware and apply a per-game profile (experimental)",
        "Detecta el juego y el hardware y aplica un perfil por juego (experimental)",
        "Detecta o jogo e o hardware e aplica um perfil por jogo (experimental)",
        "Определение игры и оборудования, профиль для игры (эксперимент)" },
      { "Profiles" } },
    // ILION Comunidad: its own module in the Full presentation only (Compact leaves it out of its tabs)
    { { "COMMUNITY", "COMUNIDAD", "COMUNIDADE", "СООБЩЕСТВО" },
      { "ILION Community", "Comunidad ILION", "Comunidade ILION", "Сообщество ILION" },
      { "Profiles shared by ILION users: ranking, explore and your contributions",
        "Perfiles compartidos por usuarios de ILION: clasificación, explorar y tus aportes",
        "Perfis compartilhados por usuários do ILION: ranking, explorar e suas contribuições",
        "Профили пользователей ILION: рейтинг, обзор и ваши публикации" },
      { "Community" } },
};
// clang-format on

constexpr int kSectionCount = (int) std::size(kSections);
constexpr int kProfilesSection = 8;
constexpr int kCommunitySection = 9; // Full presentation only
// The order of the modules in the sidebar and in Compact's tabs (Compact skips the community)
static const int kNavOrder[] = { 0, kProfilesSection, kCommunitySection, 1, 2, 3, 4, 5, 6, 7 };
static_assert(std::size(kNavOrder) == kSectionCount);
// Full presentation sidebar: the core technologies first, then PERFILES and COMUNIDAD as complementary tools (Compact keeps kNavOrder)
static const int kFullNavOrder[] = { 0, 1, 2, 3, 4, kProfilesSection, kCommunitySection, 5, 6, 7 };
static_assert(std::size(kFullNavOrder) == kSectionCount);

// The icon of a section (Profiles draws its own, kind 11)
static int IconOf(int sectionIndex) { return sectionIndex == kProfilesSection ? 11 : sectionIndex == kCommunitySection ? 12 : sectionIndex; }

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
static bool fgQuickHome = false; // ILION dev.67: RenderFgQuickControls drawn for the Home card
static float compactHomeGap = 0.0f; // ILION v1.3.0: Compact's Home: the room left at the bottom, shared above its sections
static float compactUpGap = 0.0f;   // ILION v1.3.0: the same for Compact's Upscaling
static bool compactHomeTight = false; // ILION dev.79: Compact's Home measured taller than its area: tighter spacing
static bool upSelectOnly = false; // ILION dev.74: homeUpscaling draws only its selectors (Compact's Upscaling, its monitor below)
static int fgHomePart = 0;            // ILION dev.70: Home FG card, 0 everything, 1 the left zone's controls, 2 the states
static float fgHomeStatesWidth = 0.0f; // ILION dev.70: width of the states' block, measured when drawn
static int subTab[kSectionCount] = {};
static int upFamily = -1;      // family shown in the upscaling panel
static int upFilter = 0;       // filter of the catalogue
static bool upAdvanced = false; // folds of the upscaling panel

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

// ---------------------------------------------------------------------------------------------------------------------
// ILION Advisor (misc/IlionAdvisor.h): its texts, its hold and the facts it is given
// ---------------------------------------------------------------------------------------------------------------------
namespace IlionShell
{
using IlionAdvisor::Note;

static const char* NoteText(Note n)
{
    switch (n)
    {
    case Note::NativeMeetsTarget:
        return Lx("Measured at native resolution: the FPS reach the target of your screen.", "Medido en resolución nativa: los FPS alcanzan el objetivo de tu pantalla.",
                  "Medido em resolução nativa: os FPS alcançam o alvo da tela.", "Измерено в нативном разрешении: FPS достигают цели экрана.");
    case Note::NativeBelowTarget:
        return Lx("Measured at native resolution: the FPS stay below the target of your screen.", "Medido en resolución nativa: los FPS quedan por debajo del objetivo de tu pantalla.",
                  "Medido em resolução nativa: os FPS ficam abaixo do alvo da tela.", "Измерено в нативном разрешении: FPS ниже цели экрана.");
    case Note::NativeUnknownScaled:
        return Lx("An upscaler runs: these FPS are those of this setting, native performance is unknown.", "Hay un escalador activo: estos FPS son de esta configuración, el rendimiento nativo es desconocido.",
                  "Há um upscaler ativo: estes FPS são desta configuração, o nativo é desconhecido.", "Работает апскейлер: эти FPS — этой настройки, нативная производительность неизвестна.");
    case Note::NoStableSamples:
        return Lx("No stable FPS yet (loading, pause or the menu just opened).", "Aún no hay FPS estables (carga, pausa o menú recién abierto).",
                  "Ainda não há FPS estáveis (carregamento, pausa ou menu recém-aberto).", "Ещё нет стабильных FPS (загрузка, пауза или меню только открыто).");
    case Note::PrudentByResolution:
        return Lx("Prudent start chosen by the output resolution.", "Inicio prudente elegido por la resolución de salida.",
                  "Início prudente escolhido pela resolução de saída.", "Осторожный старт по выходному разрешению.");
    case Note::ObservedHigh:
        return Lx("The FPS are well above the target: a higher quality mode has room.", "Los FPS superan bastante el objetivo: hay margen para un modo de más calidad.",
                  "Os FPS superam bem o alvo: há margem para um modo de mais qualidade.", "FPS заметно выше цели: есть запас для более качественного режима.");
    case Note::ObservedLow:
        return Lx("The FPS stay below the target: a faster mode would help.", "Los FPS quedan por debajo del objetivo: ayudaría un modo más rápido.",
                  "Os FPS ficam abaixo do alvo: um modo mais rápido ajudaria.", "FPS ниже цели: поможет более быстрый режим.");
    case Note::PreferredForArch:
        return Lx("Usable here and the first choice for this GPU's architecture (rule, not a measurement).", "Utilizable aquí y primera opción para la arquitectura de esta GPU (regla, no medición).",
                  "Utilizável aqui e primeira opção para a arquitetura desta GPU (regra, não medição).", "Доступно и первое для архитектуры этой GPU (правило, не измерение).");
    case Note::OnlyUsableFamily:
        return Lx("The only upscaler usable here.", "El único escalador utilizable aquí.", "O único upscaler utilizável aqui.", "Единственный доступный апскейлер.");
    case Note::NoUsableUpscaler:
        return Lx("No upscaler of ILION is usable in this game.", "Ningún escalador de ILION es utilizable en este juego.",
                  "Nenhum upscaler do ILION é utilizável neste jogo.", "В этой игре нет доступных апскейлеров ILION.");
    case Note::Fsr4Int8Cost:
        return Lx("RDNA 3 runs FSR 4 through INT8 builds: better image, higher cost than FSR 3.1.", "RDNA 3 ejecuta FSR 4 con versiones INT8: mejor imagen, más coste que FSR 3.1.",
                  "RDNA 3 executa FSR 4 com versões INT8: imagem melhor, custo maior que FSR 3.1.", "RDNA 3 запускает FSR 4 через INT8: лучше картинка, дороже FSR 3.1.");
    case Note::FgNotEvaluated:
        return Lx("FG options not worked out yet in this session (open Frame Generation once).", "Opciones de FG aún no evaluadas en esta sesión (abre Frame Generation una vez).",
                  "Opções de FG ainda não avaliadas nesta sessão (abra Frame Generation uma vez).", "Опции FG ещё не оценены (откройте Frame Generation).");
    case Note::FgNoOutput:
        return Lx("No FG output of OptiScaler is usable here.", "Ninguna salida de FG de OptiScaler es utilizable aquí.",
                  "Nenhuma saída de FG do OptiScaler é utilizável aqui.", "Нет доступного выхода FG OptiScaler.");
    case Note::FgNoBase:
        return Lx("The real (base) FPS are unknown: FG can't be judged yet.", "Los FPS reales (base) son desconocidos: aún no se puede valorar la FG.",
                  "Os FPS reais (base) são desconhecidos: a FG ainda não pode ser avaliada.", "Реальные (базовые) FPS неизвестны: FG оценить нельзя.");
    case Note::FgBaseTooLow:
        return Lx("Base under 45 FPS: generated frames would add too much latency.", "Base por debajo de 45 FPS: los fotogramas generados añadirían demasiada latencia.",
                  "Base abaixo de 45 FPS: os quadros gerados adicionariam latência demais.", "База ниже 45 FPS: сгенерированные кадры дадут слишком большую задержку.");
    case Note::FgBaseBelow60:
        return Lx("Base between 45 and 60 FPS: FG off as a prudent start (latency).", "Base entre 45 y 60 FPS: FG desactivada como inicio prudente (latencia).",
                  "Base entre 45 e 60 FPS: FG desligada como início prudente (latência).", "База 45–60 FPS: FG выключена для осторожного старта (задержка).");
    case Note::FgBaseOk:
        return Lx("Base of 60 FPS or more: FG can add smoothness with acceptable latency.", "Base de 60 FPS o más: la FG puede añadir fluidez con una latencia aceptable.",
                  "Base de 60 FPS ou mais: a FG pode dar fluidez com latência aceitável.", "База от 60 FPS: FG добавит плавность при приемлемой задержке.");
    case Note::FgCappedByOutput:
        return Lx("Multiplier limited to what this FG output can generate.", "Multiplicador limitado a lo que esta salida de FG puede generar.",
                  "Multiplicador limitado ao que esta saída de FG pode gerar.", "Множитель ограничен возможностями этого выхода FG.");
    case Note::FgCappedByRefresh:
        return Lx("Multiplier lowered: more frames would pass the refresh rate of the screen (reference only).", "Multiplicador reducido: más fotogramas superarían la frecuencia de la pantalla (solo referencia).",
                  "Multiplicador reduzido: mais quadros passariam a frequência da tela (só referência).", "Множитель снижен: больше кадров превысит частоту экрана (ориентир).");
    case Note::FgCappedEstimatedBase:
        return Lx("Base FPS estimated (presented / multiplier): no more than x2 is suggested.", "FPS base estimados (presentados / multiplicador): no se sugiere más de x2.",
                  "FPS base estimados (apresentados / multiplicador): não se sugere mais que x2.", "База оценена (показано / множитель): не более x2.");
    case Note::FgUnstable:
        return Lx("Unsteady frame times: a higher multiplier would show it more.", "Tiempos de fotograma irregulares: un multiplicador mayor lo haría más visible.",
                  "Tempos de quadro irregulares: um multiplicador maior deixaria mais visível.", "Неровное время кадра: больший множитель это усилит.");
    case Note::FgMaxUnknown:
        return Lx("The output's maximum is known once it is loaded: x2 until then.", "El máximo de la salida se conoce al cargarla: x2 hasta entonces.",
                  "O máximo da saída é conhecido ao carregá-la: x2 até lá.", "Максимум выхода известен после загрузки: до тех пор x2.");
    case Note::FgRefreshUnknown:
        return Lx("Refresh rate of the screen unknown.", "Frecuencia de la pantalla desconocida.", "Frequência da tela desconhecida.", "Частота экрана неизвестна.");
    case Note::NrNotSupported:
        return Lx("Neural Rendering can't run on this GPU.", "Neural Rendering no puede ejecutarse en esta GPU.", "Neural Rendering não roda nesta GPU.",
                  "Neural Rendering не работает на этой GPU.");
    case Note::NrNotReady:
        return Lx("No NR runtime ready, or a restart is pending.", "Ningún motor NR listo, o hay un reinicio pendiente.", "Nenhum motor NR pronto, ou reinício pendente.",
                  "Нет готового движка NR или ожидается перезапуск.");
    case Note::NrNoBase:
        return Lx("Base FPS unknown: NR can't be judged yet.", "FPS base desconocidos: aún no se puede valorar NR.", "FPS base desconhecidos: NR ainda não pode ser avaliado.",
                  "База FPS неизвестна: NR оценить нельзя.");
    case Note::NrHeadroom:
        return Lx("Base of 75 FPS or more: there is room for the model's cost.", "Base de 75 FPS o más: hay margen para el coste del modelo.",
                  "Base de 75 FPS ou mais: há margem para o custo do modelo.", "База от 75 FPS: есть запас на стоимость модели.");
    case Note::NrNoHeadroom:
        return Lx("Base under 75 FPS: the model's cost would be felt.", "Base por debajo de 75 FPS: el coste del modelo se notaría.",
                  "Base abaixo de 75 FPS: o custo do modelo seria sentido.", "База ниже 75 FPS: стоимость модели будет заметна.");
    case Note::LatencyAutomatic:
        return Lx("Intelligent activation picks the compatible technology (Reflex, Anti-Lag 2, XeLL, LatencyFleX).",
                  "La activación inteligente elige la tecnología compatible (Reflex, Anti-Lag 2, XeLL, LatencyFleX).",
                  "A ativação inteligente escolhe a tecnologia compatível (Reflex, Anti-Lag 2, XeLL, LatencyFleX).",
                  "Умная активация выбирает совместимую технологию (Reflex, Anti-Lag 2, XeLL, LatencyFleX).");
    case Note::LatencyXeFgUsesXeLL:
        return Lx("With XeFG, XeLL owns latency (Reflex is not used).", "Con XeFG, la latencia la lleva XeLL (Reflex no se usa).",
                  "Com XeFG, a latência é do XeLL (Reflex não é usado).", "С XeFG задержкой управляет XeLL (Reflex не используется).");
    }
    return "";
}

static bool NoteIsWarning(Note n)
{
    return n == Note::Fsr4Int8Cost || n == Note::FgUnstable || n == Note::FgCappedEstimatedBase || n == Note::FgMaxUnknown ||
           n == Note::FgRefreshUnknown || n == Note::NoStableSamples || n == Note::FgNotEvaluated || n == Note::NativeUnknownScaled;
}

static const char* QualityText(IlionAdvisor::Quality q)
{
    switch (q)
    {
    case IlionAdvisor::Quality::NativeAA:
        return Lx("Native AA", "AA nativo", "AA nativo", "Нативный AA");
    case IlionAdvisor::Quality::Quality:
        return Lx("Quality", "Calidad", "Qualidade", "Качество");
    case IlionAdvisor::Quality::Balanced:
        return Lx("Balanced", "Equilibrado", "Equilibrado", "Баланс");
    default:
        return Lx("Performance", "Rendimiento", "Desempenho", "Производительность");
    }
}

static const char* LevelText(IlionAdvisor::Level l)
{
    return l == IlionAdvisor::Level::Verified ? Lx("verified", "verificado", "verificado", "проверено")
           : l == IlionAdvisor::Level::Estimate ? Lx("estimate", "estimación", "estimativa", "оценка")
                                                : Lx("insufficient data", "datos insuficientes", "dados insuficientes", "мало данных");
}

static ImVec4 LevelColour(IlionAdvisor::Level l)
{
    return l == IlionAdvisor::Level::Verified ? pal::Green : l == IlionAdvisor::Level::Estimate ? pal::Yellow : pal::Orange;
}

static const char* FgOutName(int o)
{
    return o == IlionAdvisor::XeFG ? "XeFG" : o == IlionAdvisor::FsrFG ? "FSR FG" : o == IlionAdvisor::Dlssg ? "DLSSG" : "-";
}

// The CPU's own name (CPUID brand string), without the words every model repeats ("8-Core Processor", "(R)"...)
static const std::string& CpuName()
{
    static std::string name = []()
    {
        int regs[4] = {};
        char brand[49] = {};
        __cpuid(regs, 0x80000000);
        if ((unsigned) regs[0] >= 0x80000004u)
        {
            for (int i = 0; i < 3; i++)
            {
                __cpuid(regs, 0x80000002 + i);
                memcpy(brand + i * 16, regs, 16);
            }
        }
        std::string text = brand;
        for (const char* cut : { "(R)", "(TM)", "(tm)", " with Radeon Graphics", " w/ Radeon Graphics", " Processor", " CPU" })
            for (size_t at; (at = text.find(cut)) != std::string::npos;)
                text.erase(at, strlen(cut));
        if (const size_t core = text.find("-Core"); core != std::string::npos)
        {
            size_t start = text.rfind(' ', core);
            text.erase(start == std::string::npos ? 0 : start, core + 5 - (start == std::string::npos ? 0 : start));
        }
        if (const size_t at = text.find(" @ "); at != std::string::npos)
            text.erase(at);
        while (!text.empty() && text.back() == ' ')
            text.pop_back();
        while (!text.empty() && text.front() == ' ')
            text.erase(0, 1);
        for (size_t at; (at = text.find("  ")) != std::string::npos;)
            text.erase(at, 1);
        return text.empty() ? std::string("-") : text;
    }();
    return name;
}

// Refresh rate of the monitor that shows the game, read again only when the window moves to another monitor
static double RefreshHz(HWND window)
{
    static HMONITOR lastMonitor = nullptr;
    static double lastHz = 0.0;
    HMONITOR monitor = window != nullptr ? MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST) : nullptr;
    if (monitor == nullptr)
        return 0.0;
    if (monitor != lastMonitor)
    {
        lastMonitor = monitor;
        lastHz = 0.0;
        MONITORINFOEXW info {};
        info.cbSize = sizeof(info);
        if (GetMonitorInfoW(monitor, &info))
        {
            DEVMODEW mode {};
            mode.dmSize = sizeof(mode);
            if (EnumDisplaySettingsW(info.szDevice, ENUM_CURRENT_SETTINGS, &mode) && mode.dmDisplayFrequency > 1)
                lastHz = (double) mode.dmDisplayFrequency;
        }
    }
    return lastHz;
}

// The suggestion shown and the one waiting to hold for kHoldSeconds
static IlionAdvisor::Advice advShown;
static bool advHasShown = false;
static int advPending = -1;
static double advPendingSince = 0.0;
static double advLastRun = -10.0;
static IlionAdvisor::Facts advFacts;
} // namespace IlionShell

// ILION AL DÍA + ILION TERMINAL: the box of Home that ILION Advisor used
#include "ilion_home_panel.inl"

// ILION dev.60: FG on/off, the multiplier of the loaded output and what is chosen, loaded and active. The same in Home
// and next to the selection of the Frame Gen module; on/off is the FG key's own function
void MenuCommon::RenderFgQuickControls(RenderMenuContext& ctx)
{
    using namespace IlionShell;
    auto& state = ctx.state;
    auto config = ctx.config;
    auto fg = state.currentFG;
    const bool running = fg != nullptr && fg->IsActive() && !fg->IsPaused();
    const bool homeLayout = fgQuickHome; // ILION dev.67: the Home card's own arrangement (Generation keeps its own)

    // ILION: who generates and how sure it is (IlionDynamicMfg): ILION's own output, or the game's DLSSG through a
    // replacement such as DLSS Enabler (NvngxFG: no output of ILION, so currentFG stays empty)
    const auto fgStatus = IlionDynamicMfg::Get();
    const bool externalFg = fgStatus.provider == IlionDynamicMfg::Provider::DlssgGame && state.activeFgOutput == FGOutput::NoFG;

    // On / off, the FG key's own function. For the game's DLSSG (external) it turns that DLSSG off / back to the
    // game's own choice through Streamline, as "Override DLSSG Ratio" does
    const auto drawSwitch = [&](const char* label)
    {
        if (externalFg)
        {
            bool on = fgStatus.gen == IlionDynamicMfg::Gen::Detected || fgStatus.gen == IlionDynamicMfg::Gen::Confirmed;
            ImGui::BeginDisabled(!StreamlineHooks::isDlssgHooked());
            if (IlionTheme::Checkbox(label, &on))
            {
                if (on)
                    config->FGDLSSGOverrideInterpolationCount.reset(); // the game's own choice again
                else
                    config->FGDLSSGOverrideInterpolationCount = 0;     // DLSSG off through Streamline
                StreamlineHooks::requestDlssgOptionsUpdate(on ? "FG on (game's choice)" : "FG off");
            }
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                WrappedTooltip("%s", Lx("The game's DLSSG generates (external FG): off asks Streamline for no frame generation, on gives the game's choice back.",
                                        "La FG la genera el DLSSG del juego (FG externa): apagar pide a Streamline que no genere; encender devuelve la elección del juego.",
                                        "O DLSSG do jogo gera (FG externa): desligar pede ao Streamline que não gere; ligar devolve a escolha do jogo.",
                                        "Кадры генерирует DLSSG игры (внешняя FG): выкл. — Streamline не генерирует, вкл. — снова выбор игры."));
            return;
        }

        bool on = config->IsFGEnabled();
        ImGui::BeginDisabled(!CanToggleFrameGeneration(state));
        if (IlionTheme::Checkbox(label, &on))
            ToggleFrameGeneration(state, config);
        ImGui::EndDisabled();
    };

    // Multiplier: only with an output loaded, up to what it can generate
    const auto drawMultiplier = [&](float width, const char* label, bool shortNote)
    {
        if (fg != nullptr && (state.activeFgOutput == FGOutput::XeFG || state.activeFgOutput == FGOutput::DLSSG ||
                              state.activeFgOutput == FGOutput::FSRFG))
        {
            ImGui::SetNextItemWidth(width);
            if (state.activeFgOutput == FGOutput::XeFG)
                RenderFGModeCombo(ctx, label, fg->GetMaxInterpolationCount(), &config->FGXeFGInterpolationCount);
            else if (state.activeFgOutput == FGOutput::DLSSG)
                RenderFGModeCombo(ctx, label, fg->GetMaxInterpolationCount(), &config->FGDLSSGInterpolationCount);
            else
                RenderFGModeCombo(ctx, label, 1, nullptr);
        }
        else if (externalFg)
        {
            // The game's DLSSG (DLSS Enabler...): its multiplier is the "Override DLSSG Ratio" setting (Game = the
            // game's own choice); the preview shows what it generates now
            const int limit = IlionDynamicMfg::MaxCountLimit();
            const bool overridden = config->FGDLSSGOverrideInterpolationCount.has_value() &&
                                    config->FGDLSSGOverrideInterpolationCount.value() > 0;
            const std::string preview = fgStatus.multiplier > 0 ? StrFmt("%dX", fgStatus.multiplier)
                                        : overridden ? StrFmt("%dX", config->FGDLSSGOverrideInterpolationCount.value() + 1)
                                                     : std::string(Lx("Game", "Juego", "Jogo", "Игра"));
            ImGui::SetNextItemWidth(width);
            ImGui::BeginDisabled(limit < 1);
            if (IlionTheme::BeginCombo(label, preview.c_str()))
            {
                if (ImGui::Selectable(Lx("Game", "Juego", "Jogo", "Игра"), !overridden))
                {
                    config->FGDLSSGOverrideInterpolationCount.reset();
                    StreamlineHooks::requestDlssgOptionsUpdate("multiplier: game's choice");
                }
                // ILION v1.3.1 RC3: whether each count is the one set, read from the setting itself for every item.
                // "Game" above may have just emptied it in this same frame: the "overridden" taken before the list
                // was stale, and value() of the empty optional threw std::bad_optional_access on the present thread
                // of sl.dlss_g, which ended the game (abort, "2X" -> "Game" with DLSS Enabler)
                const auto& overrideCount = config->FGDLSSGOverrideInterpolationCount;
                for (int count = 1; count <= limit; ++count)
                    if (ImGui::Selectable(StrFmt("%dX", count + 1).c_str(),
                                          overrideCount.has_value() && overrideCount.value() == count))
                        IlionDynamicMfg::SetMaxCount(count);
                ImGui::EndCombo();
            }
            ImGui::EndDisabled();
        }
        else
        {
            const char* note = Lx("Multiplier: once an FG output is loaded", "Multiplicador: con una salida de FG cargada",
                                  "Multiplicador: com uma saída de FG carregada", "Множитель: после загрузки выхода FG");
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("%s", shortNote ? Lx("Multiplier: -", "Multiplicador: -", "Multiplicador: -", "Множитель: -") : note);
            if (shortNote && ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", note);
        }
    };

    // Chosen, loaded, generating and Streamline FG: labels in one column, their values starting at the same place
    const char* names[4] = { Lx("Chosen:", "Elegido:", "Escolhido:", "Выбрано:"), Lx("Loaded:", "Cargado:", "Carregado:", "Загружено:"),
                             Lx("Generating:", "Generando:", "Gerando:", "Генерирует:"), "Streamline FG:" };
    float labelWidth = 0.0f;
    // ILION: Home's card and Compact measure only the labels they draw («Streamline FG:» only with its row), so the
    // values sit next to «Elegido / Cargado / Generando» instead of far to the right
    const bool tightStates = homeLayout || shellCompact;
    const bool streamlineRow = state.currentFGSwapchain != nullptr && state.activeFgInput == FGInput::DLSSG;
    for (int n = 0; n < 4; n++)
        if (!tightStates || n < 3 || streamlineRow)
            labelWidth = (std::max)(labelWidth, ImGui::CalcTextSize(names[n]).x);

    const auto row = [&](Led led, const char* name, const std::string& value)
    {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float h = ImGui::GetTextLineHeight();
        ShellLedAt(ImGui::GetWindowDrawList(), ImVec2(p.x + h * 0.5f, p.y + h * 0.5f), h * 0.22f, led);
        ImGui::Dummy(ImVec2(h, h));
        ImGui::SameLine(0, U(6));
        ImGui::TextColored(Tone(pal::Text2), "%s", name);
        // ILION: the values' column in screen coordinates. SameLine(GetCursorPosX() + n) counted the offset of a table
        // column twice (SameLine adds ColumnsOffset to a position that already holds it): in a column away from the
        // window's left edge the values ran off to the right edge and were cut. They wrap inside their column now
        ImGui::SameLine();
        ImGui::SetCursorScreenPos(ImVec2(p.x + h + U(6) + labelWidth + (tightStates ? U(6) : U(10)), ImGui::GetCursorScreenPos().y));
        if (tightStates)
        {
            // Wrapped a little inside the card's edge, never against it
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + (std::max)(ImGui::GetContentRegionAvail().x - U(8), U(40)));
            ImGui::TextWrapped("%s", value.c_str());
            ImGui::PopTextWrapPos();
        }
        else
            ImGui::TextWrapped("%s", value.c_str());
    };

    const std::string chosen = StrFmt("%s / %s", std::string(magic_enum::enum_name(config->FGInput.value_or_default())).c_str(),
                                      std::string(magic_enum::enum_name(config->FGOutput.value_or_default())).c_str());
    // With the game's DLSSG generating there is no output of ILION ("NoFG"): the generator is named instead
    const std::string loaded =
        externalFg ? StrFmt("%s / %s (%s)", std::string(magic_enum::enum_name(state.activeFgInput)).c_str(), fgStatus.name,
                            Lx("external", "externa", "externa", "внешняя"))
                   : StrFmt("%s / %s", std::string(magic_enum::enum_name(state.activeFgInput)).c_str(),
                            std::string(magic_enum::enum_name(state.activeFgOutput)).c_str());
    const bool same = config->FGOutput.value_or_default() == state.activeFgOutput && config->FGInput.value_or_default() == state.activeFgInput;

    std::string active = Tr("Off");
    Led activeLed = Led::Off;
    if (running)
    {
        const unsigned multiplier = (unsigned) fg->GetInterpolatedFrameCount() + 1;
        std::string name = fg->Name();
        const std::string suffix = StrFmt(" %ux", multiplier);
        if (name.ends_with(suffix))
            name.erase(name.size() - suffix.size());
        active = StrFmt("%s %ux", name.c_str(), multiplier);
        activeLed = Led::Green;
    }
    else if (externalFg)
    {
        // The game's DLSSG: confirmed when its backend reports presented frames, detected when it evaluates with
        // frames to generate, unknown while a replacement is loaded and nothing tells
        if (fgStatus.gen == IlionDynamicMfg::Gen::Confirmed || fgStatus.gen == IlionDynamicMfg::Gen::Detected)
        {
            const bool confirmed = fgStatus.gen == IlionDynamicMfg::Gen::Confirmed;
            active = StrFmt("%s %dx (%s)", fgStatus.name, fgStatus.multiplier,
                            confirmed ? Lx("confirmed", "confirmado", "confirmado", "подтверждено")
                                      : Lx("detected", "detectado", "detectado", "обнаружено"));
            activeLed = confirmed ? Led::Green : Led::Yellow;
        }
        else if (fgStatus.gen == IlionDynamicMfg::Gen::Unknown)
        {
            active = StrFmt("%s ?", fgStatus.name);
        }
    }

    const auto drawStates = [&]()
    {
        row(Led::Off, names[0], chosen);
        row(same ? Led::Green : Led::Orange, names[1], loaded);
        row(activeLed, names[2], active);

        // Streamline FG (DLSSG of the game as input): its state, read the same way as its card of the classic menu
        if (state.currentFGSwapchain != nullptr && state.activeFgInput == FGInput::DLSSG)
        {
            if (!ReflexHooks::isReflexHooked())
                row(Led::Red, names[3], Tr("Reflex not hooked"));
            else if ((state.fgLastFrame - state.dlssgLastFrame) < 3)
                row(fg != nullptr && fg->IsActive() ? Led::Green : Led::Orange, names[3], fg != nullptr && fg->IsActive() ? Tr("ON") : Tr("ACTIVATE FG"));
            else if (homeLayout || shellCompact)
            {
                row(Led::Off, names[3], Tr("OFF"));
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", Lx("Select DLSS Frame Generation in the game", "Elige DLSS Frame Generation en el juego",
                                               "Escolha DLSS Frame Generation no jogo", "Выберите DLSS Frame Generation в игре"));
            }
            else
                row(Led::Off, names[3], std::string(Tr("OFF")) + "  ·  " + Lx("select DLSS Frame Generation in the game", "elige DLSS Frame Generation en el juego",
                                                                             "escolha DLSS Frame Generation no jogo", "выберите DLSS Frame Generation в игре"));
        }
    };

    const char* pendingNote = Lx("The choice is not the one loaded yet: it applies when OptiScaler creates the FG again (in some games, after a restart).",
                                 "La elección aún no es la cargada: se aplica cuando OptiScaler vuelve a crear la FG (en algunos juegos, tras reiniciar).",
                                 "A escolha ainda não é a carregada: aplica-se quando o OptiScaler recria a FG (em alguns jogos, após reiniciar).",
                                 "Выбор ещё не загружен: применится, когда OptiScaler заново создаст FG (в некоторых играх — после перезапуска).");

    if (homeLayout)
    {
        // Home (ILION dev.70): the card is split by the caller into two table columns that start on the same row.
        // Part 1 draws the left zone's controls, part 2 the states in the right column, part 0 both when the card is
        // too narrow for two zones
        float selectorLabel = 0.0f;
        for (const char* label : { "FG Input", "FG Output", "FG Nvngx", "FG Nvngx Replacement" })
            selectorLabel = (std::max)(selectorLabel, ImGui::CalcTextSize(Tr(label)).x);
        selectorLabel += ImGui::GetStyle().ItemInnerSpacing.x * 2.0f;
        const float width = fgHomeComboWidth > 0.0f ? fgHomeComboWidth : FgSelectorWidth(selectorLabel);

        // The states' block: pilot, label column and the longest value shown now (dev.71: not the longest possible)
        const float h = ImGui::GetTextLineHeight();
        float valueWidth = (std::max)({ ImGui::CalcTextSize(chosen.c_str()).x, ImGui::CalcTextSize(loaded.c_str()).x, ImGui::CalcTextSize(active.c_str()).x });
        if (state.currentFGSwapchain != nullptr && state.activeFgInput == FGInput::DLSSG)
        {
            const char* streamlineNow = !ReflexHooks::isReflexHooked()                     ? Tr("Reflex not hooked")
                                        : (state.fgLastFrame - state.dlssgLastFrame) >= 3 ? Tr("OFF")
                                        : fg != nullptr && fg->IsActive()                  ? Tr("ON")
                                                                                           : Tr("ACTIVATE FG");
            valueWidth = (std::max)(valueWidth, ImGui::CalcTextSize(streamlineNow).x);
        }
        const float blockWidth = std::ceil(h + U(6) + labelWidth + U(10) + valueWidth);
        fgHomeStatesWidth = blockWidth;

        if (fgHomePart == 2)
        {
            // Against the card's right edge: the column can be a frame behind a change of width
            const float indent = (std::max)(0.0f, ImGui::GetContentRegionAvail().x - blockWidth);
            if (indent > 0.0f)
                ImGui::Indent(indent);
            drawStates();
            if (indent > 0.0f)
                ImGui::Unindent(indent);
            return;
        }

        const ImVec2 leftStart = ImGui::GetCursorScreenPos();
        const float contentRight = leftStart.x + ImGui::GetContentRegionAvail().x;

        drawMultiplier(width, "##HomeMult", true);
        drawSwitch(Lx("On###QuickFg", "Activada###QuickFg", "Ativada###QuickFg", "Вкл.###QuickFg"));
        if (!same)
        {
            ImGui::TextColored(Tone(pal::Orange), "%s", Lx("Chosen FG not loaded yet", "FG elegida aún sin cargar", "FG escolhida ainda não carregada",
                                                           "Выбранная FG ещё не загружена"));
            if (ImGui::IsItemHovered())
                WrappedTooltip("%s", pendingNote);
        }

        if (fgHomePart == 1)
            return;

        // Narrow card (sidebar expanded, big scale): the states beside the multiplier and "On" when they leave room,
        // else under them. Never over the selectors
        const float leftBottom = ImGui::GetCursorScreenPos().y;
        const float statesX = contentRight - blockWidth - U(8);
        if (statesX >= leftStart.x + width + U(16))
        {
            ImGui::SetCursorScreenPos(ImVec2(statesX, leftStart.y));
            ImGui::BeginGroup();
            drawStates();
            ImGui::EndGroup();
            const float rightBottom = ImGui::GetItemRectMax().y + ImGui::GetStyle().ItemSpacing.y;
            ImGui::SetCursorScreenPos(ImVec2(leftStart.x, (std::max)(leftBottom, rightBottom)));
            ImGui::Dummy(ImVec2(0.0f, 0.0f));
        }
        else
            drawStates();
        return;
    }

    // ILION dev.76: the Compact presentation. dev.78: in the two columns RenderFrameGenerationSelection opens: part 1
    // the switch and under it the multiplier (left), part 2 the four states, 15% larger, against the right edge of
    // their column with a 20 px margin
    if (shellCompact)
    {
        if (fgCompactPart != 2)
        {
            drawSwitch(Lx("Frame generation on###QuickFg", "Generación activada###QuickFg", "Geração ativada###QuickFg", "Генерация включена###QuickFg"));
            drawMultiplier(U(130), "Frame Gen##QuickMult", true);
            if (!same)
                TextWrappedColored(Tone(pal::Orange), "%s", pendingNote);
            if (fgCompactPart == 1)
                return;
        }

        ImGui::PushFont(ImGui::GetFont(), std::floor(ImGui::GetFontSize() * 1.15f));
        labelWidth = 0.0f;
        for (const char* name : names)
            labelWidth = (std::max)(labelWidth, ImGui::CalcTextSize(name).x);
        const float lh = ImGui::GetTextLineHeight();
        float valueWidth = (std::max)({ ImGui::CalcTextSize(chosen.c_str()).x, ImGui::CalcTextSize(loaded.c_str()).x, ImGui::CalcTextSize(active.c_str()).x });
        if (state.currentFGSwapchain != nullptr && state.activeFgInput == FGInput::DLSSG)
        {
            const char* streamlineNow = !ReflexHooks::isReflexHooked()                     ? Tr("Reflex not hooked")
                                        : (state.fgLastFrame - state.dlssgLastFrame) >= 3 ? Tr("OFF")
                                        : fg != nullptr && fg->IsActive()                  ? Tr("ON")
                                                                                           : Tr("ACTIVATE FG");
            valueWidth = (std::max)(valueWidth, ImGui::CalcTextSize(streamlineNow).x);
        }
        const float blockWidth = std::ceil(lh + U(6) + labelWidth + U(10) + valueWidth);
        fgCompactStatesWidth = blockWidth;

        // ILION Perfiles EXP: 40 px from the right edge (20 px left them against it)
        const float indent = (std::max)(0.0f, std::floor(ImGui::GetContentRegionAvail().x - blockWidth - U(40)));
        if (indent > 0.0f)
            ImGui::Indent(indent);
        drawStates();
        if (indent > 0.0f)
            ImGui::Unindent(indent);
        ImGui::PopFont();
        return;
    }

    // Frame Gen > Generation: as approved (switch and multiplier on one row, the states under them)
    drawSwitch(Lx("Frame generation on###QuickFg", "Generación activada###QuickFg", "Geração ativada###QuickFg", "Генерация включена###QuickFg"));
    ImGui::SameLine(0, U(24));
    drawMultiplier(U(130), "Frame Gen##QuickMult", false);

    // Chosen, loaded and active, apart, a little below the switch row
    ImGui::Dummy(Sz(0.f, 5.f));
    drawStates();
    if (!same)
        TextWrappedColored(Tone(pal::Orange), "%s", pendingNote);
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
    const bool olive = UiThemeIndex(config) == 1;
    const bool compact = UiCompact(config); // ILION dev.73: the Compact presentation (Full is the default)
    IlionTheme::SetShellTheme(olive ? 1 : 0); // ILION dev.70: Industrial or Clean Olive

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
            shellSimple = false;
            shellCompact = false;
        }
    } shellScope;
    shellSimple = olive || compact; // ILION dev.71: Clean Olive's simpler pages; dev.73: also the Compact presentation
    shellCompact = compact;
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
    // Streamline FG's state is a line of the FG control card (its own card made the column scroll for one line)
    // ILION: two balanced columns. Left, what is set: the selection with its control, then the Dynamic MFG controls.
    // Right, what it does and the options: the two screens, the Dynamic MFG state with the UI and advanced settings folded
    // in it ("@fg_state", no card of their own any more), then each provider's own settings
    static const ShellLayout lFrameGen { { "fg_selection", "@fg_quick", "fg_dynamic" },
                                         { "@fg_instruments", "@fg_state", "fg_xefg", "fg_fsrfg", "fg_dlssg", "fg_nvngx", "fg_optifg",
                                           "fg_reprojection", "fg_fsrfg_inputs" } };
    static const ShellLayout lLatency { { "framerate", "vsync", "low_latency" },
                                        { "ilion_latency", "ilion_regulator", "@latency_combined" } };
    static const ShellLayout lImage { { "gfx_color", "gfx_advanced" }, { "sharpness", "mipmap", "anisotropy" } };
    static const ShellLayout lReShade { { "reshade", "@reshade_manage" }, {} };
    static const ShellLayout lCompare { { "@compare_info", "gfx_compare" }, { "magnifier" } };
    static const ShellLayout lOverlay { { "fps_overlay" }, {} };
    static const ShellLayout lBenchmark { { "benchmark" }, {} };
    static const ShellLayout lMenu { { "theme" }, { "menu_scale", "keybinds" } };
    static const ShellLayout lStatus { { "status_summary", "@fg_status", "@nr_status", "status_system", "status_init" },
                                       { "@upscaler_diag", "@latency_report", "status_detected", "status_hooks" } };
    static const ShellLayout lCompat { { "init_flags", "fsr_common", "upscaler_inputs", "shaders_limits" },
                                       { "spoofing", "plugins", "barriers", "root_signatures" }, true };
    static const ShellLayout lLog { { "logging" }, {}, true };
    // ILION dev.73: the Compact presentation's own lists (one column): Frame Gen without its monitors and MFG cost,
    // Latency with its short state (the meters and the report are in Diagnostics), and Diagnostics' parts
    static const ShellLayout lFrameGenC { { "fg_selection", "@fg_quick", "fg_dynamic", "fg_optifg", "fg_reprojection", "fg_nvngx", "fg_fsrfg_inputs" },
                                          { "fg_xefg", "fg_fsrfg", "fg_dlssg", "@fg_ui_advanced", "@fg_instruments" } };
    static const ShellLayout lLatencyC { { "framerate", "vsync", "low_latency" }, { "ilion_latency", "ilion_regulator", "@latency_monitor" } };
    static const ShellLayout dSummary { { "status_summary" }, {} };
    static const ShellLayout dUpscaler { { "@upscaler_diag" }, {} };
    static const ShellLayout dFrameGen { { "@fg_status", "@fg_instruments" }, {} };
    static const ShellLayout dNr { { "@nr_status" }, {} };
    static const ShellLayout dLatency { { "@latency_meters", "@latency_report" }, {} };
    static const ShellLayout dSystem { { "status_system" }, {} };
    static const ShellLayout dDetected { { "status_detected", "status_hooks", "status_init" }, {} };

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
        { 2, { "External FG", "FG externo", "FG externo", "Внешний FG" }, "External FG", nullptr, 0 },
        { 2, { "Anti-Ghosting", "Anti-Ghosting", "Anti-Ghosting", "Anti-Ghosting" }, "Anti-Ghosting", nullptr, -1 },
        { 3, { "Latency & FPS", "Latencia y FPS", "Latência e FPS", "Задержка и FPS" }, "Latency & FPS", &lLatency, 0 },
        { 4, { "Image", "Imagen", "Imagem", "Изображение" }, "Graphics", &lImage, 0 },
        { 4, { "Neural Rendering", "Neural Rendering", "Neural Rendering", "Neural Rendering" }, "Neural Rendering", nullptr, 4 },
        { 4, { "ReShade", "ReShade", "ReShade", "ReShade" }, "Graphics", &lReShade, 0 },
        { 4, { "Comparison", "Comparación", "Comparação", "Сравнение" }, "Graphics", &lCompare, 0 },
        { 5, { "Overlay", "Overlay", "Overlay", "Оверлей" }, "Overlay", &lOverlay, 0 },
        { 5, { "Benchmark", "Benchmark", "Benchmark", "Бенчмарк" }, "Benchmark", &lBenchmark, 0 },
        { 6, { "Appearance and menu", "Apariencia y menú", "Aparência e menu", "Внешний вид и меню" }, "Menu", &lMenu, 0 },
        { 6, { "Pinned cards", "Tarjetas ancladas", "Cartões fixados", "Закреплённые карточки" }, "Custom", nullptr, 0 },
        { 7, { "Status", "Estado", "Estado", "Состояние" }, "Help / Status", &lStatus, 0 },
        { 7, { "Compatibility", "Compatibilidad", "Compatibilidade", "Совместимость" }, "Compatibility", &lCompat, 0 },
        { 7, { "Log", "Registro", "Registro", "Журнал" }, "Help / Status", &lLog, 0 },
        { 8, { "Profiles", "Perfiles", "Perfis", "Профили" }, "Profiles", nullptr, 0 },
        { 9, { "Community", "Comunidad", "Comunidade", "Сообщество" }, "Community", nullptr, 0 },
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

    // ILION dev.71: Clean Olive has no Status & compatibility module (Industrial and Traditional keep it)
    constexpr int kStatusSection = 7;
    const auto sectionShown = [&](int i) { return !(olive && i == kStatusSection) && !(compact && i == kCommunitySection); };
    // PERFILES → COMPARTIR → COMUNIDAD y COMUNIDAD → DESCARGAR → PERFILES
    if (IlionCommunityUi::requestedSection >= 0)
    {
        section = IlionCommunityUi::requestedSection == 9 ? kCommunitySection : kProfilesSection;
        IlionCommunityUi::requestedSection = -1;
    }
    if (!sectionShown(section))
        section = 0;

    // Window -------------------------------------------------------------------------------------------------------
    // ILION dev.73: the Compact presentation has its own size; the window is centred again when the presentation changes
    static int lastCompact = -1;
    const bool presentationChanged = lastCompact != -1 && lastCompact != (int) compact;
    lastCompact = (int) compact;
    const ImVec2 size = compact ? Sz(kCompactW, kCompactH) : Sz(W, H);
    ImGui::SetNextWindowSize(size, ImGuiCond_Always);
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), presentationChanged ? ImGuiCond_Always : ImGuiCond_Appearing,
                            ImVec2(0.5f, 0.5f));
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
        Led::Off,
        Led::Off, // community
    };

    // ILION dev.72: ILION Advisor's parts as functions of the shell, so another presentation can show them. Each one is
    // called where its code was, in the same order: the facts are gathered once a second as before, nothing twice
    // The facts and the held suggestion (advFacts, advShown)
    const auto advisorUpdate = [&]()
    {
        if (ImGui::GetTime() - advLastRun >= 1.0)
        {
            advLastRun = ImGui::GetTime();
            IlionAdvisor::Facts f;
            const auto& gpu = IdentifyGpu::getPrimaryGpu();
            f.vendor = gpu.vendorId == VendorId::AMD ? IlionAdvisor::Vendor::Amd : gpu.vendorId == VendorId::Nvidia ? IlionAdvisor::Vendor::Nvidia
                       : gpu.vendorId == VendorId::Intel ? IlionAdvisor::Vendor::Intel : IlionAdvisor::Vendor::Unknown;
            f.vramGb = gpu.dedicatedVramInBytes / 1073741824.0;
            if (f.vendor == IlionAdvisor::Vendor::Amd)
                f.arch = gpu.amdHwGeneration == device_info::HwGeneration::kGfx12  ? IlionAdvisor::Arch::Rdna4
                         : gpu.amdHwGeneration == device_info::HwGeneration::kGfx11 ? IlionAdvisor::Arch::Rdna3
                         : gpu.amdHwGeneration == device_info::HwGeneration::kGfx10_3 ? IlionAdvisor::Arch::Rdna2
                                                                                     : IlionAdvisor::Arch::AmdOther;
            else if (f.vendor == IlionAdvisor::Vendor::Nvidia)
                f.arch = gpu.dlssCapable ? IlionAdvisor::Arch::NvidiaDlss : IlionAdvisor::Arch::NvidiaOther;
            else if (f.vendor == IlionAdvisor::Vendor::Intel)
                f.arch = f.vramGb >= 2.0 ? IlionAdvisor::Arch::IntelArc : IlionAdvisor::Arch::IntelOther;

            f.upscalerRunning = upscalerOn;
            f.outWidth = upscalerOn ? (int) feature->TargetWidth() : (int) io.DisplaySize.x;
            f.outHeight = upscalerOn ? (int) feature->TargetHeight() : (int) io.DisplaySize.y;
            f.renderRatio = upscalerOn && feature->RenderWidth() > 0 ? (double) feature->TargetWidth() / feature->RenderWidth() : 1.0;
            f.refreshHz = RefreshHz(_handle);

            // The last presented frames: their median and how steady they are
            {
                std::vector<double> recent;
                const auto& times = state.frameTimes;
                for (size_t k = times.size() > 180 ? times.size() - 180 : 0; k < times.size(); k++)
                    if (times[k] > 0.0)
                        recent.push_back(times[k]);
                if (recent.size() >= 90)
                {
                    double mean = 0.0;
                    for (double v : recent)
                        mean += v;
                    mean /= recent.size();
                    double var = 0.0;
                    for (double v : recent)
                        var += (v - mean) * (v - mean);
                    f.frametimeCv = mean > 0.0 ? std::sqrt(var / recent.size()) / mean : 1.0;
                    std::nth_element(recent.begin(), recent.begin() + recent.size() / 2, recent.end());
                    const double median = recent[recent.size() / 2];
                    f.presentedFps = median > 0.0 ? 1000.0 / median : 0.0;
                    f.samplesStable = f.presentedFps > 0.0 && f.frametimeCv < 0.5;
                }
            }

            // Frame generation: what generates now, the real frames MFG cost counts, the copy of the FG options
            auto fg = state.currentFG;
            const bool ownFg = fg != nullptr && fg->IsActive() && !fg->IsPaused();
            const bool dynamicMfg = state.dlssgLastSetMode == sl::DLSSGMode::eDynamic;
            if (ownFg)
            {
                f.fgGenerating = true;
                f.fgEffectiveMultiplier = (int) fg->GetInterpolatedFrameCount() + 1;
                f.fgMultiplierReliable = !dynamicMfg && (state.activeFgOutput == FGOutput::XeFG || state.activeFgOutput == FGOutput::FSRFG ||
                                                         state.activeFgOutput == FGOutput::DLSSG);
            }
            else if (state.dlssgDetectedInterpolationCount > 0)
            {
                f.fgGenerating = true;
                f.fgEffectiveMultiplier = state.dlssgDetectedInterpolationCount + 1;
                f.fgMultiplierReliable = !dynamicMfg;
            }
            if (mfgCost.generating && !mfgCost.unmeasurable && mfgCost.current > 0.0)
                f.realBaseFps = mfgCost.current;
            for (double r : mfgCost.reference)
                f.referenceBaseFps = (std::max)(f.referenceBaseFps, r > 0.0 ? r : 0.0);
            if (mfgCost.reference[MfgCostState::Presents] > 0.0)
                f.referenceBaseFps = mfgCost.reference[MfgCostState::Presents];

            f.fgLoaded = state.activeFgOutput == FGOutput::XeFG ? IlionAdvisor::XeFG : state.activeFgOutput == FGOutput::FSRFG ? IlionAdvisor::FsrFG
                         : state.activeFgOutput == FGOutput::DLSSG ? IlionAdvisor::Dlssg : IlionAdvisor::NoFg;
            f.fgEvaluated = fgOptionsSnapshot.evaluated;
            f.fgUsable[IlionAdvisor::XeFG] = fgOptionsSnapshot.outputUsable[(int) FGOutput::XeFG];
            f.fgUsable[IlionAdvisor::FsrFG] = fgOptionsSnapshot.outputUsable[(int) FGOutput::FSRFG];
            f.fgUsable[IlionAdvisor::Dlssg] = fgOptionsSnapshot.outputUsable[(int) FGOutput::DLSSG];
            f.fgMaxMultiplier[IlionAdvisor::FsrFG] = 2; // FSR FG makes one frame (its own combo offers no more)
            if (fg != nullptr && f.fgLoaded != IlionAdvisor::NoFg && f.fgLoaded != IlionAdvisor::FsrFG)
                f.fgMaxMultiplier[f.fgLoaded] = fg->GetMaxInterpolationCount() + 1;

            // Upscalers usable here, as the catalogue decides it
            const auto catalog = GatherUpscalers(feature);
            for (const auto& v : catalog.variants)
                if (v.usable && v.family >= 0 && v.family < IlionAdvisor::FamilyCount)
                    f.familyUsable[v.family] = true;

            // Neural Rendering
            const auto& nrGpu = DlssNr::AmdBridge::GpuSupportInfo();
            const auto nr = NrSwitchStateNow();
            f.nrGpuOk = nrGpu.danielOk || nrGpu.lmxxfOk;
            f.nrReady = nr.ready;
            f.nrRestart = nr.restart;
            f.fgXeFgChosen = config->FGOutput.value_or_default() == FGOutput::XeFG || state.activeFgOutput == FGOutput::XeFG;

            advFacts = f;
            const auto advice = IlionAdvisor::Evaluate(f);
            const int signature = IlionAdvisor::Signature(advice);
            if (!advHasShown || signature == IlionAdvisor::Signature(advShown))
            {
                advShown = advice;
                advHasShown = true;
                advPending = -1;
            }
            else if (signature != advPending)
            {
                advPending = signature;
                advPendingSince = ImGui::GetTime();
            }
            else if (ImGui::GetTime() - advPendingSince >= IlionAdvisor::kHoldSeconds)
            {
                advShown = advice;
                advPending = -1;
            }
        }
    };

    // The confidence of the suggestion: the least sure of scaling, FG and NR (green verified, yellow estimate, amber
    // insufficient), as the lamp shows it
    const auto advisorConfidenceColour = [&]() -> ImVec4
    {
        IlionAdvisor::Level level = IlionAdvisor::Level::Insufficient;
        if (advHasShown)
            level = (std::max)({ advShown.scalingLevel, advShown.fgLevel, advShown.nrLevel });
        return level == IlionAdvisor::Level::Verified   ? IlionTheme::ShellPilot(pal::Green)
               : level == IlionAdvisor::Level::Estimate ? IlionTheme::ShellPilot(pal::Yellow)
                                                        : pal::Orange;
    };

    // The suggestion in words and colours (not what is active now)
    struct AdvisorSuggestion
    {
        bool ready = false;
        std::string up;
        ImVec4 upColour;
        bool fg = false;
        const char* tech = "";
        ImVec4 techColour;
        std::string mult;
        std::string fgText;
        ImVec4 fgColour;
        std::string nrText;
        ImVec4 nrColour;
    };
    const auto advisorSuggestion = [&]() -> AdvisorSuggestion
    {
        AdvisorSuggestion r;
        const auto amber = pal::Orange;
        const auto off = pal::Dim;
        if (!advHasShown)
        {
            r.up = Lx("Working it out...", "Calculando...", "Calculando...", "Расчёт...");
            r.upColour = amber;
            return r;
        }
        r.ready = true;
        const auto& adv = advShown;

        r.upColour = pal::Text;
        if (adv.scaling)
        {
            r.up = (adv.family >= 0 ? std::string(familyNames[adv.family]) + " · " : std::string()) + QualityText(adv.quality);
            r.upColour = pal::Green;
        }
        else
            r.up = std::string(Lx("Native", "Nativo", "Nativo", "Натив")) +
                   (adv.family >= 0 && adv.quality == IlionAdvisor::Quality::NativeAA ? std::string(" + AA ") + familyNames[adv.family] : std::string());
        if (adv.scalingLevel == IlionAdvisor::Level::Insufficient)
            r.upColour = amber;

        r.fg = adv.fg;
        r.fgColour = off;
        if (adv.fg)
        {
            // Drawn in two parts: the technology in its colour (XeFG in Intel blue, the others green), the multiplier
            r.fgColour = pal::Green;
            r.tech = FgOutName(adv.fgOut);
            r.techColour = adv.fgOut == IlionAdvisor::XeFG ? ImVec4(0.30f, 0.62f, 1.00f, 1.f) : pal::Green;
            r.mult = StrFmt(" x%d", adv.multiplier);
        }
        else if (adv.fgLevel == IlionAdvisor::Level::Insufficient)
        {
            r.fgText = Lx("not determinable yet", "aún no determinable", "ainda não determinável", "пока не определить");
            r.fgColour = amber;
        }
        else
            r.fgText = Lx("off", "desactivada", "desligada", "выкл.");

        r.nrText = adv.nr ? Lx("on", "activado", "ligado", "вкл.") : Lx("off", "desactivado", "desligado", "выкл.");
        r.nrColour = adv.nr ? pal::Green : off;
        if (!adv.nr && adv.nrLevel == IlionAdvisor::Level::Insufficient)
        {
            r.nrText = Lx("not determinable yet", "aún no determinable", "ainda não determinável", "пока не определить");
            r.nrColour = amber;
        }
        return r;
    };

    // The diagnosis popup (opened with OpenPopup("##IlionAdvisorPopup") from the same ID stack). It changes nothing
    const auto advisorDiagnosis = [&]()
    {
        ImGui::SetNextWindowSize(Sz(700, 0), ImGuiCond_Always);
        PushContentStyle();
        ImGui::PushStyleColor(ImGuiCol_PopupBg, Tone(ImVec4(0.045f, 0.052f, 0.048f, 0.99f)));
        ImGui::PushFont(fUi, U(16.f));
        if (ImGui::BeginPopup("##IlionAdvisorPopup"))
        {
            const auto& adv = advShown;
            const auto& f = advFacts;
            ImGui::PushFont(fBrand, U(22.f));
            ImGui::TextColored(Tone(pal::Green), "ILION ADVISOR");
            ImGui::PopFont();
            ImGui::TextColored(Tone(pal::Text2), "%s", Lx("A starting suggestion from what ILION knows here. ILION changes no setting.",
                                                         "Sugerencia inicial a partir de lo que ILION sabe aquí. ILION no cambia ningún ajuste.",
                                                         "Sugestão inicial a partir do que o ILION sabe aqui. O ILION não muda nenhum ajuste.",
                                                         "Начальная рекомендация по данным ILION. ILION ничего не меняет."));
            IlionTheme::SectionPlate(Lx("Hardware and screen", "Hardware y pantalla", "Hardware e tela", "Оборудование и экран"));
            const auto& gpu = IdentifyGpu::getPrimaryGpu();
            ImGui::Text("GPU: %s  ·  %.0f GB", gpu.name.c_str(), f.vramGb);
            ImGui::Text("%s: %dx%d  ·  %s", Lx("Output", "Salida", "Saída", "Выход"), f.outWidth, f.outHeight,
                        f.refreshHz > 0.0 ? StrFmt("%.0f Hz", f.refreshHz).c_str() : Lx("refresh unknown", "frecuencia desconocida", "frequência desconhecida", "частота неизвестна"));
            const char* baseFrom = adv.base == IlionAdvisor::Base::Measured       ? Lx("measured now", "medidos ahora", "medidos agora", "измерено сейчас")
                                   : adv.base == IlionAdvisor::Base::MeasuredBefore ? Lx("measured before (MFG cost reference)", "medidos antes (referencia de Coste MFG)", "medidos antes (referência do Custo MFG)", "измерено ранее (эталон MFG)")
                                   : adv.base == IlionAdvisor::Base::Estimated      ? Lx("estimated: presented / multiplier", "estimados: presentados / multiplicador", "estimados: apresentados / multiplicador", "оценка: показано / множитель")
                                                                                    : Lx("unknown", "desconocidos", "desconhecidos", "неизвестно");
            if (adv.base != IlionAdvisor::Base::None)
                ImGui::Text("%s: %.0f FPS (%s)", Lx("Base FPS", "FPS base", "FPS base", "Базовые FPS"), adv.baseFps, baseFrom);
            else
                ImGui::Text("%s: %s", Lx("Base FPS", "FPS base", "FPS base", "Базовые FPS"), baseFrom);
            ImGui::Text("%s: %.0f FPS", Lx("Target used", "Objetivo usado", "Alvo usado", "Цель"), adv.targetFps);

            IlionTheme::SectionPlate(Lx("Suggested setting", "Configuración sugerida", "Configuração sugerida", "Рекомендуемая настройка"));
            if (ImGui::BeginTable("##AdvRows", 3, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings))
            {
                const auto row = [&](const char* name, const std::string& value, IlionAdvisor::Level level, bool prudent)
                {
                    ImGui::TableNextColumn();
                    ImGui::TextColored(Tone(pal::Text2), "%s", name);
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(value.c_str());
                    ImGui::TableNextColumn();
                    ImGui::TextColored(Tone(LevelColour(level)), "%s%s", LevelText(level),
                                       prudent ? Lx(" · prudent start", " · inicio prudente", " · início prudente", " · осторожный старт") : "");
                };
                row(Lx("Upscaling", "Escalado", "Escala", "Масштаб"), adv.scaling ? Lx("on", "activado", "ligado", "вкл.") : Lx("off (native)", "desactivado (nativo)", "desligado (nativo)", "выкл. (натив)"),
                    adv.scalingLevel, adv.scalingPrudent);
                row(Lx("Technology / mode", "Tecnología / modo", "Tecnologia / modo", "Технология / режим"),
                    (adv.family >= 0 ? std::string(familyNames[adv.family]) : std::string("-")) + "  ·  " + QualityText(adv.quality), adv.techLevel, false);
                row("Frame Generation", adv.fg ? StrFmt("%s (%s)", Lx("on", "activada", "ligada", "вкл."), FgOutName(adv.fgOut))
                                               : std::string(Lx("off", "desactivada", "desligada", "выкл.")), adv.fgLevel, adv.fgPrudent);
                row(Lx("Multiplier", "Multiplicador", "Multiplicador", "Множитель"), adv.fg ? StrFmt("x%d", adv.multiplier) : std::string("-"), adv.fgLevel, false);
                row("Neural Rendering", adv.nr ? Lx("on", "activado", "ligado", "вкл.") : Lx("off", "desactivado", "desligado", "выкл."), adv.nrLevel, false);
                row(Lx("Latency", "Latencia", "Latência", "Задержка"), Lx("Intelligent activation: Automatic", "Activación inteligente: Automática", "Ativação inteligente: Automática", "Умная активация: авто"),
                    IlionAdvisor::Level::Verified, false);
                ImGui::EndTable();
            }

            const auto notes = [&](bool warnings)
            {
                for (const auto* list : { &adv.scalingNotes, &adv.techNotes, &adv.fgNotes, &adv.nrNotes, &adv.latencyNotes })
                    for (const Note n : *list)
                        if (NoteIsWarning(n) == warnings)
                        {
                            ImGui::Bullet();
                            if (warnings)
                                ImGui::TextColored(Tone(pal::Yellow), "%s", NoteText(n));
                            else
                                ImGui::TextWrapped("%s", NoteText(n));
                        }
            };
            IlionTheme::SectionPlate(Lx("Why", "Motivos", "Motivos", "Причины"));
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + U(660));
            notes(false);
            IlionTheme::SectionPlate(Lx("Warnings and limits", "Advertencias y límites", "Avisos e limites", "Предупреждения"));
            notes(true);
            ImGui::TextColored(Tone(pal::Dim), "%s", Lx("Compatibility comes from ILION's own checks; a technology that runs is not proof that it is the best choice. No FPS are predicted.",
                                                       "La compatibilidad sale de las comprobaciones de ILION; que una tecnología funcione no demuestra que sea la mejor opción. No se predicen FPS.",
                                                       "A compatibilidade vem das verificações do ILION; uma tecnologia funcionar não prova que seja a melhor. Não se preveem FPS.",
                                                       "Совместимость — из проверок ILION; работающая технология не обязательно лучшая. FPS не прогнозируются."));
            ImGui::PopTextWrapPos();
            ImGui::EndPopup();
        }
        ImGui::PopFont();
        ImGui::PopStyleColor();
        PopContentStyle();
    };

    // ILION dev.72: the contents of Home's cards as functions of the shell (selection and state of upscaling, of frame
    // generation, latency, V-Sync and the FPS limit), so another presentation can draw them in its own frames. They are
    // drawn by the ImGui flow (no fixed coordinates): the Full presentation calls them in its cards, as before
    const auto smallLabel = [&](const char* text)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, Tone(pal::Text2));
        ImGui::TextUnformatted(text);
        ImGui::PopStyleColor();
    };

    const auto homeUpscaling = [&]()
             {
                 static const int order[] = { FamFsr4, FamHelix, FamDlss4, FamXess, FamFsr31, FamFsr2, FamFfx };
                 const auto catalog = GatherUpscalers(feature);
                 if (upFamily < 0 || upFamily >= FamCount || !catalog.present[upFamily])
                     upFamily = catalog.current >= 0 ? catalog.current : FamFsr4;

                 const float gap = ImGui::GetStyle().ItemSpacing.x;
                 const float w = (ImGui::GetContentRegionAvail().x - gap) * 0.5f;
                 // ILION: the second column in screen coordinates (SameLine(x) counts a table column's offset twice)
                 const float sx0 = ImGui::GetCursorScreenPos().x;
                 const auto secondColumn = [&]()
                 {
                     ImGui::SameLine();
                     ImGui::SetCursorScreenPos(ImVec2(sx0 + w + gap, ImGui::GetCursorScreenPos().y));
                 };

                 smallLabel(Lx("Technology", "Tecnología", "Tecnologia", "Технология"));
                 secondColumn();
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

                 secondColumn();
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
                 // ILION: the implementation that runs (loaded and active) named in yellow
                 const ImVec4 activeYellow = Tone(pal::Yellow);
                 if (IlionTheme::BeginCombo("##HomeVariant", preview.c_str(), 0, previewRank == 3 ? &activeYellow : nullptr))
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

                 if (upSelectOnly)
                 {
                     if (locked)
                         Label(Tr("It can't be changed while a benchmark is running."), pal::Yellow, 15.f);
                     return;
                 }

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
                 // ILION: Compact's Home: the pilot apart from the lines of what runs
                 if (compact)
                     ImGui::Dummy(ImVec2(0.0f, U(5.f)));

                 // What runs: resolution, factor and the upscaler's own time
                 if (upscalerOn && feature->RenderWidth() > 0)
                 {
                     const float factor = (float) feature->TargetWidth() / feature->RenderWidth();
                     // ILION: the implementation that runs in yellow, the internal resolution plain, the final one cyan
                     LabelParts({ { UpscalerLabel(feature), pal::Yellow },
                                  { "  ·  ", pal::Text2 },
                                  { StrFmt("%ux%u", feature->RenderWidth(), feature->RenderHeight()), pal::Text },
                                  { " -> ", pal::Text2 },
                                  { StrFmt("%ux%u", feature->TargetWidth(), feature->TargetHeight()), IlionTheme::InfoCyan } },
                                17.f);
                     if (compact)
                         ImGui::Dummy(ImVec2(0.0f, U(3.f)));

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
                     // ILION: the figures (factor, share of pixels, the upscaler's time) brighter than their words
                     LabelParts({ { std::string(Lx("Factor", "Factor", "Fator", "Коэффициент")) + " ", pal::Text2 },
                                  { StrFmt("%.2fx", factor), pal::Text },
                                  { "  ·  ", pal::Text2 },
                                  { StrFmt("%.0f%%", 100.0f / (factor * factor)), pal::Text },
                                  { std::string(" ") + Lx("of the pixels rendered", "de los píxeles renderizados", "dos pixels renderizados",
                                                          "пикселей отрисовано"),
                                    pal::Text2 },
                                  { valid > 0 ? std::string("  ·  ") + Lx("upscaler", "escalador", "upscaler", "апскейлер") + " " : std::string(),
                                    pal::Text2 },
                                  { valid > 0 ? StrFmt("%.2f ms", sum / valid) : std::string(), pal::Text } },
                                15.f);
                 }
                 else
                     Label(Lx("No upscaler is running in this game right now.", "Ningún escalador se está ejecutando ahora en este juego.",
                              "Nenhum upscaler está em execução neste jogo agora.", "Сейчас в этой игре апскейлер не работает."),
                           pal::Text2, 15.f);
                 if (locked)
                     Label(Tr("It can't be changed while a benchmark is running."), pal::Yellow, 15.f);
                 ApplyState(Q(ImGui::GetCursorScreenPos()));
             };

    const auto homeFrameGen = [&]()
             {
                 // Two zones as two columns of one table row: both start at the same top, the left one (selectors,
                 // multiplier, On) never pushes the right one (the states) down.
                 // dev.71: why dev.69 and dev.70 still put the states low. With their usual width the selectors take
                 // 17 font sizes (289 px at 17 px) plus their label (up to 171 px, "FG Nvngx Replacement" in Spanish,
                 // counted even when not shown), and the states' block was measured with "Reflex not hooked" (169 px)
                 // whenever the game's DLSSG is the input, as in Cyberpunk: ~316 px. 289 + 179 + 316 is more than
                 // the card's 691 px, so dev.70 never split the card and fell back to beside the multiplier.
                 // Now the states' block is measured with what it shows, the labels counted are only the ones shown,
                 // and in the two zones the selectors (and the variant and the multiplier, so their arrows stay in one
                 // line) take the left column's room minus their label: narrower, same font and height
                 const auto& style = ImGui::GetStyle();
                 const FGOutput outputNow = config->FGOutput.value_or_default();
                 float shownLabel = ImGui::CalcTextSize(Tr("FG Input")).x;
                 if (config->FGInput.value_or_default() == FGInput::NvngxFG)
                     shownLabel = (std::max)(shownLabel, ImGui::CalcTextSize(Tr("FG Nvngx")).x);
                 else
                     shownLabel = (std::max)(shownLabel, ImGui::CalcTextSize(Tr("FG Output")).x);
                 if (outputNow == FGOutput::DLSSG)
                     shownLabel = (std::max)(shownLabel, ImGui::CalcTextSize(Tr("FG Nvngx Replacement")).x);
                 if (outputNow == FGOutput::XeFG)
                     shownLabel = (std::max)(shownLabel, ImGui::CalcTextSize(Tr("XeMFG variant")).x);
                 shownLabel += style.ItemInnerSpacing.x * 2.0f;

                 const float statesWidth = fgHomeStatesWidth > 0.0f ? fgHomeStatesWidth : U(260);
                 const float gap = U(12);
                 const float leftRoom = ImGui::GetContentRegionAvail().x - statesWidth - gap - style.CellPadding.x * 4.0f;
                 const float comboMin = ImGui::GetFontSize() * 8.0f;
                 const bool zones = leftRoom - shownLabel >= comboMin &&
                                    ImGui::BeginTable("##HomeFgZones", 2, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoSavedSettings);

                 if (zones)
                 {
                     ImGui::TableSetupColumn("##left", ImGuiTableColumnFlags_WidthStretch);
                     ImGui::TableSetupColumn("##right", ImGuiTableColumnFlags_WidthFixed, statesWidth + gap);
                     ImGui::TableNextRow();
                     ImGui::TableSetColumnIndex(0);
                     fgHomeComboWidth = (std::min)(ImGui::GetFontSize() * 17.0f, std::floor(leftRoom - shownLabel));
                 }

                 shellFgInstrumentsApart = true;
                 fgSelectorsOnly = true;
                 RenderFrameGenerationSelection(ctx);
                 fgSelectorsOnly = false;
                 shellFgInstrumentsApart = false;

                 fgQuickHome = true;
                 fgHomePart = zones ? 1 : 0;
                 RenderFgQuickControls(ctx);
                 if (zones)
                 {
                     ImGui::TableSetColumnIndex(1);
                     fgHomePart = 2;
                     RenderFgQuickControls(ctx);
                     ImGui::EndTable();
                 }
                 fgHomePart = 0;
                 fgQuickHome = false;
                 fgHomeComboWidth = 0.0f;

                 // ILION: Dynamic MFG from Home (switch, final FPS target, maximum while Dynamic is on): the very settings
                 // of the Dynamic MFG card, no copy of its own (IlionDynamicMfg)
                 if (IlionDynamicMfg::Current() != IlionDynamicMfg::Provider::None)
                 {
                     ImGui::Spacing();
                     if (compact)
                         ImGui::Dummy(ImVec2(0.0f, U(4.f)));
                     ScopedID dynamicId { "home_dmfg" };
                     RenderDynamicMfgPanel(ctx, true);
                 }
             };

    const auto homeLatency = [&]()
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
             };


    // ILION dev.73: the Compact presentation. A window of its own size with tabs above; every control is the one the
    // Full presentation draws (the functions above, the module tabs, the cards), only arranged in one column. The
    // technical information goes to its last tab, Diagnostics. Nothing here changes a setting by itself
    if (compact)
    {
        constexpr float CW = kCompactW, CH = kCompactH;
        constexpr int kDiag = 7; // the place of Status & compatibility: Diagnostics in Compact
        static int compactSection = 0;
        static int compactSub[kSectionCount] = {};
        if (compactSection < 0 || compactSection >= kSectionCount)
            compactSection = 0;
        // A module of the sub-section kind: from Frame Gen on, Profiles included, Diagnostics apart
        const bool compactModule = compactSection >= 2 && compactSection != kDiag;

        // The Advisor's facts once a frame, as the Full header gathers them (the header is not drawn here)
        advisorUpdate();

        // Top bar: brand, version and the way back to the Full presentation
        dl->AddRectFilled(P(0, 0), P(CW, 58), Col(Mix(pal::Bg, ImVec4(0, 0, 0, 1), 0.35f)), U(6.0f), ImDrawFlags_RoundCornersTop);
        dl->AddLine(P(0, 58), P(CW, 58), Col(pal::Line));
        Text(fBrand, 30.f, ImVec2(16, 10), pal::Green, "ILION");
        const float brandW = Measure(fBrand, 30.f, "ILION").x;
        Text(fUi, 13.f, ImVec2(16 + brandW + 10, 24), pal::Dim, (std::string("v") + BuildInfo::IlionVersion()).c_str());
        {
            const char* toFull = Lx("Full view", "Vista completa", "Vista completa", "Полный вид");
            const float fw = Measure(fUi, 15.f, toFull).x + 30;
            At(CW - 16 - fw, 9);
            if (Button("##CompactToFull", toFull, ImVec2(fw, 40), false, pal::Green, 15.f))
            {
                config->MenuUiPresentation = std::string("full");
                config->SaveMenuPreferences();
            }
        }

        // Tabs: the modules, and Diagnostics last
        {
            const float gap = 4.f;
            constexpr int compactTabs = kSectionCount - 1; // the community is not part of Compact
            const float tabW = std::floor((CW - 24 - gap * (compactTabs - 1)) / compactTabs);
            int slot = 0;
            for (int n = 0; n < kSectionCount; n++)
            {
                const int i = kNavOrder[n];
                if (i == kCommunitySection)
                    continue;
                const int at = slot++;
                const ImVec2 p(12 + at * (tabW + gap), 66), q(12 + at * (tabW + gap) + tabW, 134);
                const char* caps = i == kDiag ? Lx("DIAGNOSTICS", "DIAGNÓSTICO", "DIAGNÓSTICO", "ДИАГНОСТИКА") : kSections[i].caps[li];
                At(p.x, p.y);
                ImGui::PushID(200 + i);
                if (ImGui::InvisibleButton("cnav", Sz(q.x - p.x, q.y - p.y)))
                    compactSection = i;
                const bool hov = ImGui::IsItemHovered();
                if (hov)
                {
                    ImGui::PushFont(fUi, U(16.f));
                    ImGui::SetTooltip("%s", i == kDiag ? Lx("Diagnostics", "Diagnóstico", "Diagnóstico", "Диагностика") : kSections[i].name[li]);
                    ImGui::PopFont();
                }
                ImGui::PopID();

                const bool sel = compactSection == i;
                const ImVec4 top = sel ? ImVec4(0.11f, 0.075f, 0.04f, 1) : (hov ? pal::PanelHi : pal::Panel);
                const ImVec4 bottom = Mix(top, ImVec4(0, 0, 0, 1), 0.4f);
                dl->AddRectFilledMultiColor(P(p), P(q), Col(top), Col(top), Col(bottom), Col(bottom));
                dl->AddRect(P(p), P(q), Col(sel ? pal::Orange : pal::Line), U(5.f), 0, sel ? U(2.f) : 1.f);
                const ImU32 ic = Col(sel ? pal::Orange : (hov ? pal::Text : pal::Text2));
                Icon(IconOf(i), ImVec2((p.x + q.x) * 0.5f, p.y + 24), 26.f, ic);
                float fs = 13.f;
                while (fs > 9.f && Measure(fUi, fs, caps).x > tabW - 6)
                    fs -= 1.f;
                const ImVec2 ts = Measure(fUi, fs, caps);
                Text(fUi, fs, ImVec2((p.x + q.x - ts.x) * 0.5f, p.y + 46), sel ? pal::Orange : pal::Text, caps);
                if (i != kDiag && sectionLed[i] != Led::Off)
                    LedAt(dl, ImVec2(q.x - 9, p.y + 9), 3.5f, sectionLed[i]);
            }
        }

        // Sub-sections of the module, as in Full (pinned cards are not in Clean Olive)
        float top = 144;
        int shown[8];
        int shownCount = 0;
        if (compactModule)
        {
            for (int k = 0; k < subCount && shownCount < 8; k++)
            {
                if (olive && strcmp(subs[k].tab, "Custom") == 0)
                    continue;
                if (subs[k].section == compactSection && tabVisible(tabIndex(subs[k].tab)))
                    shown[shownCount++] = k;
            }
            if (shownCount > 0 && std::none_of(shown, shown + shownCount, [&](int k) { return k == compactSub[compactSection]; }))
                compactSub[compactSection] = shown[0];

            if (shownCount > 1)
            {
                float fs = 15.f;
                const auto total = [&](float size)
                {
                    float w = 0.0f;
                    for (int n = 0; n < shownCount; n++)
                        w += Measure(fUi, size, subs[shown[n]].name[li]).x + 28 + 6;
                    return w;
                };
                while (fs > 12.f && total(fs) > CW - 24)
                    fs -= 1.f;
                float x = 12;
                for (int n = 0; n < shownCount; n++)
                {
                    const int k = shown[n];
                    const char* label = subs[k].name[li];
                    const float w = Measure(fUi, fs, label).x + 28;
                    At(x, top);
                    ImGui::PushID(300 + k);
                    if (Button("##csub", label, ImVec2(w, 36), compactSub[compactSection] == k, pal::Yellow, fs))
                        compactSub[compactSection] = k;
                    ImGui::PopID();
                    x += w + 6;
                }
                top += 44;
            }
        }

        // The ReShade panel belongs to its sub-section, as in Full
        if (!(compactSection == 4 && shownCount > 0 && compactSub[4] == subReShade))
        {
            reshadePanelOpen = false;
            reshadePanelAmount = 0.0f;
        }

        // Content: one column that scrolls
        bool openAdvisor = false;
        const ImVec2 ca(12, top), cb(CW - 12, CH - 108);
        PanelRect(ca, cb);
        PushContentStyle();
        ImGui::PushFont(fUi, U(16.f));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, Sz(8.f, 6.f));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, Sz(8.f, 4.f));
        if (BeginArea("##CompactBody", ImVec2(ca.x + 12, ca.y + 10), ImVec2(cb.x - 6, cb.y - 8)))
        {
            ImGui::PushItemWidth((std::min)(ImGui::GetContentRegionAvail().x * 0.55f, U(300.f)));

            // A state row: its pilot, its name and its value (the value wraps inside the window)
            const auto stateRow = [&](Led led, const char* name, const std::string& value, const ImVec4* colour = nullptr)
            {
                ImGui::TableNextColumn();
                const ImVec2 p = ImGui::GetCursorScreenPos();
                const float h = ImGui::GetTextLineHeight();
                ShellLedAt(ImGui::GetWindowDrawList(), ImVec2(p.x + h * 0.5f, p.y + h * 0.5f), h * 0.22f, led);
                ImGui::Dummy(ImVec2(h, h));
                ImGui::SameLine(0, U(6));
                ImGui::TextColored(Tone(pal::Text2), "%s", name);
                ImGui::TableNextColumn();
                ImGui::PushStyleColor(ImGuiCol_Text, Tone(colour != nullptr ? *colour : pal::Text));
                ImGui::TextWrapped("%s", value.c_str());
                ImGui::PopStyleColor();
            };
            const auto beginRows = [&](const char* id)
            {
                if (!ImGui::BeginTable(id, 2, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoSavedSettings))
                    return false;
                ImGui::TableSetupColumn("##name", ImGuiTableColumnFlags_WidthFixed);
                ImGui::TableSetupColumn("##value", ImGuiTableColumnFlags_WidthStretch);
                return true;
            };

            if (compactSection == 0)
            {
                // ILION dev.74: Home is the control centre. A small header (game, API, ILION, performance), then the
                // controls of Home's cards (the same functions: a change here is the change in their tabs) and the
                // Advisor's suggestion
                // ILION dev.76: the header in two zones: the game, its API, ILION and the performance at the left; ILION
                // Advisor's suggestion at the right (the facts are gathered once a frame above; nothing is computed here)
                const std::string game = !state.gameName.empty() ? state.gameName : state.gameExe;
                std::string api = ApiName(state.api);
                if (IdentifyGpu::gameUsesDxvk())
                    api += " (DXVK)";
                else if (IdentifyGpu::gameUsesVkd3dProton())
                    api += " (VKD3D)";
                const double ft = ctx.frameTime;
                const bool measured = std::isfinite(ft) && ft > 0.0;

                if (ImGui::BeginTable("##CompactHeader", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings))
                {
                    ImGui::TableSetupColumn("##game", ImGuiTableColumnFlags_WidthStretch, 1.0f);
                    ImGui::TableSetupColumn("##advisor", ImGuiTableColumnFlags_WidthStretch, 1.15f);
                    ImGui::TableNextRow();

                    ImGui::TableSetColumnIndex(0);
                    ImGui::PushFont(fUi, U(21.f));
                    ImGui::PushStyleColor(ImGuiCol_Text, Tone(pal::Text));
                    ImGui::TextWrapped("%s", game.empty() ? "-" : game.c_str());
                    ImGui::PopStyleColor();
                    ImGui::PopFont();
                    ImGui::TextColored(Tone(pal::Text2), "%s", api.c_str());
                    {
                        const ImVec2 p = ImGui::GetCursorScreenPos();
                        const float h = ImGui::GetTextLineHeight();
                        ShellLedAt(ImGui::GetWindowDrawList(), ImVec2(p.x + h * 0.5f, p.y + h * 0.5f), h * 0.22f, Led::Green);
                        ImGui::Dummy(ImVec2(h, h));
                    }
                    ImGui::SameLine(0, U(4));
                    ImGui::TextColored(Tone(pal::Green), "%s", Lx("ILION ACTIVE", "ILION ACTIVO", "ILION ATIVO", "ILION АКТИВЕН"));
                    if (measured)
                    {
                        // ILION: the FPS figure in its band colour (IlionTheme::FpsBand, as Full's Home), only the figure
                        ImGui::TextColored(Tone(IlionTheme::FpsBand(1000.0 / ft)), "%.0f", 1000.0 / ft);
                        ImGui::SameLine(0.0f, 0.0f);
                        ImGui::TextColored(Tone(pal::Text2), " FPS  ·  %.1f ms", ft);
                    }

                    // ILION AL DÍA / ILION TERMINAL in the cell Advisor used
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, Col(Mix(pal::Bg, ImVec4(0, 0, 0, 1), 0.25f)));
                    ImGui::Dummy(ImVec2(0.0f, U(4.f)));
                    ImGui::Indent(U(4.f));
                    {
                        const ImVec2 p0 = ImGui::GetCursorScreenPos();
                        const float w = ImGui::GetContentRegionAvail().x - U(4.f);
                        const float h = U(132.f);
                        HomePanel::Draw(ImGui::GetWindowDrawList(), p0, ImVec2(p0.x + w, p0.y + h), ctx.frameTime);
                        ImGui::SetCursorScreenPos(p0);
                        ImGui::Dummy(ImVec2(w, h));
                    }
                    ImGui::Unindent(U(4.f));
                    ImGui::Dummy(ImVec2(0.0f, U(4.f)));
                    ImGui::EndTable();
                }

                // ILION dev.79: Home fits its area with no scroll: under the header the vertical spacing is 4 (2, with
                // tighter frames, if it was measured taller than the area)
                ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, compactHomeTight ? Sz(8.f, 2.f) : Sz(8.f, 4.f));
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, compactHomeTight ? Sz(8.f, 3.f) : Sz(8.f, 4.f));

                // ILION: Upscaling sits higher: the gap above it takes 0.4 of the share of the two below
                ImGui::Spacing();
                ImGui::Dummy(ImVec2(0.0f, std::floor(compactHomeGap * 0.4f)));
                IlionTheme::SectionPlate(kSections[1].name[li]);
                homeUpscaling();

                ImGui::Spacing();
                ImGui::Dummy(ImVec2(0.0f, compactHomeGap));
                IlionTheme::SectionPlate(kSections[2].name[li]);
                ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, Sz(8.f, 4.f)); // labels apart from their controls
                homeFrameGen();
                ImGui::PopStyleVar();

                // Latency and Neural Rendering. ILION dev.79: in two columns, the latency methods (the very controls of
                // Latency & FPS) at the left, their state, NR's state and the note of the intelligent activation at the right
                ImGui::Spacing();
                ImGui::Dummy(ImVec2(0.0f, compactHomeGap));
                IlionTheme::SectionPlate(Lx("Latency and Neural Rendering", "Latencia y Neural Rendering", "Latência e Neural Rendering",
                                            "Задержка и Neural Rendering"));
                if (ImGui::BeginTable("##CompactHomeLatency", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings))
                {
                    ImGui::TableSetupColumn("##latControls", ImGuiTableColumnFlags_WidthStretch, 1.35f);
                    ImGui::TableSetupColumn("##latState", ImGuiTableColumnFlags_WidthStretch, 1.0f);
                    ImGui::TableNextRow();

                    ImGui::TableSetColumnIndex(0);
                    ImGui::PushItemWidth(std::floor(ImGui::GetContentRegionAvail().x * 0.5f));
                    ImGui::BeginDisabled(IlionLatency::Current() != IlionLatency::Manual);
#ifdef LOW_LATENCY_INPUTS
                    RenderLowLatencySettings(ctx);
#else
                    RenderFakenvapiSettings(ctx);
#endif
                    ImGui::EndDisabled();
                    ImGui::PopItemWidth();

                    ImGui::TableSetColumnIndex(1);
                    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, Sz(4.f, 3.f)); // ILION: rows of text that don't touch
                    const bool rows = beginRows("##CompactHome");
                    ImGui::PopStyleVar();
                    if (rows)
                    {
                        stateRow(latencyOn ? Led::Green : Led::Off, Lx("Latency", "Latencia", "Latência", "Задержка"),
                                 (latencyTech != nullptr ? std::string(latencyTech) : std::string("-")) +
                                     (fpsLimit > 0.0f ? StrFmt("  ·  %s %.0f FPS", Lx("limit", "límite", "limite", "лимит"), fpsLimit) : std::string()));
                        stateRow(sectionLed[4], "NR",
                                 nrRunning   ? Lx("Running", "En ejecución", "Em execução", "Работает")
                                 : nrEnabled ? Lx("On, not running yet", "Activado, aún sin ejecutarse", "Ativado, ainda sem executar", "Включён, ещё не работает")
                                             : Lx("Off", "Desactivado", "Desligado", "Выкл."));
                        ImGui::EndTable();
                    }
                    if (IlionLatency::Current() != IlionLatency::Manual)
                    {
                        ImGui::Dummy(ImVec2(0.0f, U(4.f)));
                        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, Sz(8.f, 4.f));
                        TextWrappedColored(Tone(pal::Text2), "%s",
                                           Lx("Intelligent activation chooses these: set it to Manual in Latency & FPS to change them.",
                                              "La activación inteligente los elige: ponla en Manual en Latencia y FPS para cambiarlos.",
                                              "A ativação inteligente os escolhe: coloque em Manual em Latência e FPS para alterá-los.",
                                              "Их выбирает умная активация: переключите её в Ручной режим в «Задержка и FPS»."));
                        ImGui::PopStyleVar();
                    }
                    ImGui::EndTable();
                }

                ImGui::PopStyleVar(2);

                // Measured. ILION v1.3.0: the room left under the content is shared between the three gaps above the
                // sections (up to 28 each, 16 kept free at the bottom). If the content reaches below the area, the gaps go
                // first and then the spacing tightens: never a scroll bar for Home
                {
                    const float content = ImGui::GetCursorPosY() - std::floor(compactHomeGap * 0.4f) - 2.0f * compactHomeGap;
                    const float spare = ImGui::GetWindowHeight() - content - U(16.f);
                    if (spare < 0.0f)
                    {
                        if (compactHomeGap <= 0.0f)
                            compactHomeTight = true;
                        compactHomeGap = 0.0f;
                    }
                    else
                        compactHomeGap = std::floor((std::min)(spare / 2.4f, U(28.f)));
                }
            }
            else if (compactSection == 1)
            {
                // Upscaling: technology, implementation, state, resolution and factor (Home's own controls), then the
                // settings of the upscaler that runs, without its DLL diagnostics and advanced selectors
                upSelectOnly = true;
                homeUpscaling();
                upSelectOnly = false;

                // ILION dev.74: the upscaling monitor of the Compact presentation: the same readings as Full's (running
                // feature, its resolutions, factor and measured time), laid out for this width. No path, no log
                ImGui::Spacing();
                ImGui::Dummy(ImVec2(0.0f, compactUpGap));
                ImGui::PushStyleColor(ImGuiCol_ChildBg, Tone(Mix(pal::Bg, ImVec4(0, 0, 0, 1), 0.25f)));
                ImGui::PushStyleColor(ImGuiCol_Border, Tone(pal::Line));
                if (ImGui::BeginChild("##CompactUpMonitor", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY))
                {
                    const bool running = upscalerOn || upscalerIdle;
                    {
                        const ImVec2 p = ImGui::GetCursorScreenPos();
                        const float h = ImGui::GetTextLineHeight();
                        ShellLedAt(ImGui::GetWindowDrawList(), ImVec2(p.x + h * 0.5f, p.y + h * 0.5f), h * 0.24f, sectionLed[1]);
                        ImGui::Dummy(ImVec2(h, h));
                    }
                    ImGui::SameLine(0, U(6));
                    ImGui::PushFont(fUi, U(20.f));
                    ImGui::TextColored(Tone(running ? pal::Yellow : pal::Text2), "%s",
                                       running ? UpscalerLabel(feature).c_str()
                                               : Lx("No upscaler is running", "Ningún escalador en ejecución", "Nenhum upscaler em execução", "Апскейлер не работает"));
                    ImGui::PopFont();

                    if (running)
                    {
                        const auto v = feature->Version();
                        ImGui::TextColored(Tone(pal::Text2), "%s  ·  %s %u.%u.%u  ·  %s",
                                           upscalerOn ? Lx("Running", "En ejecución", "Em execução", "Работает") : Lx("Paused", "En pausa", "Em pausa", "Пауза"),
                                           Lx("version", "versión", "versão", "версия"), v.major, v.minor, v.patch, ApiName(state.api));

                        if (feature->RenderWidth() > 0)
                        {
                            const float factor = (float) feature->TargetWidth() / feature->RenderWidth();
                            ImGui::TextColored(Tone(pal::Text), "%ux%u", feature->RenderWidth(), feature->RenderHeight());
                            ImGui::SameLine(0, U(8));
                            ImGui::TextColored(Tone(pal::Text2), "->");
                            ImGui::SameLine(0, U(8));
                            ImGui::TextColored(Tone(pal::Yellow), "%ux%u", feature->TargetWidth(), feature->TargetHeight());
                            ImGui::TextColored(Tone(pal::Text2), "%s %.2fx  ·  %.0f%% %s", Lx("Factor", "Factor", "Fator", "Коэффициент"), factor,
                                               100.0f / (factor * factor),
                                               Lx("of the pixels rendered", "de los píxeles renderizados", "dos pixels renderizados", "пикселей отрисовано"));
                        }

                        // The upscaler's GPU time, as OptiScaler measures it: its average and the last frames
                        static float graph[120];
                        int count = 0;
                        double sum = 0.0;
                        int valid = 0;
                        const auto& times = state.upscaleTimes;
                        for (size_t k = times.size() > 120 ? times.size() - 120 : 0; k < times.size(); k++)
                        {
                            graph[count++] = (float) (std::max)(0.0, (double) times[k]);
                            if (times[k] > 0.0 && k + 60 >= times.size())
                            {
                                sum += times[k];
                                valid++;
                            }
                        }
                        if (valid > 0)
                        {
                            ImGui::TextColored(Tone(pal::Text2), "%s", Lx("Upscaler time", "Tiempo del escalador", "Tempo do upscaler", "Время апскейлера"));
                            ImGui::SameLine(0, U(8));
                            ImGui::TextColored(Tone(pal::Green), "%.2f ms", sum / valid);
                            float peak = 0.0f;
                            for (int k = 0; k < count; k++)
                                peak = (std::max)(peak, graph[k]);
                            ImGui::PushStyleColor(ImGuiCol_PlotLines, Tone(pal::Green));
                            ImGui::PushStyleColor(ImGuiCol_FrameBg, Tone(ImVec4(0, 0, 0, 0.25f)));
                            ImGui::PlotLines("##CompactUpTimes", graph, count, 0, nullptr, 0.0f, (std::max)(0.1f, peak * 1.2f), ImVec2(-1.0f, U(96.f)));
                            ImGui::PopStyleColor(2);
                        }
                        else
                            ImGui::TextDisabled("%s", Lx("Upscaler time: not measured", "Tiempo del escalador: sin medir", "Tempo do upscaler: sem medição",
                                                         "Время апскейлера: не измерено"));
                    }
                }
                ImGui::EndChild();
                ImGui::PopStyleColor(2);
                ApplyState(Q(ImGui::GetCursorScreenPos()));

                // The settings of the upscaler that runs (model, presets and the options of its technology)
                ImGui::Spacing();
                ImGui::Dummy(ImVec2(0.0f, compactUpGap));
                if (IsUpscalerActive(feature))
                    IlionTheme::SectionPlate(Lx("Upscaler settings", "Ajustes del escalador", "Ajustes do upscaler", "Настройки апскейлера"));
                shellHidesSelector = true;
                skipSectionTitle = "Upscalers";
                shellHidesUpscalerDiag = true;
                RenderUpscalerStateMessage(ctx);
                if (IsUpscalerActive(feature))
                    RenderActiveUpscalerSettings(ctx);
                skipSectionTitle = nullptr;
                shellHidesSelector = false;
                shellHidesUpscalerDiag = false;

                ImGui::Dummy(ImVec2(0.0f, compactUpGap));

                // ILION dev.77: the capabilities of the implementation that runs, as the catalogue knows them (the facts its
                // tooltip states): whether it switches while playing, the DX12 interop, where an AMD driver dll comes from,
                // HLX's identification. Nothing shown above is repeated; with nothing to say the part is not drawn
                if (IsUpscalerActive(feature))
                {
                    const auto catalog = GatherUpscalers(feature);
                    const UpscalerVariant* running = nullptr;
                    for (const auto& v : catalog.variants)
                        if (v.status.active && running == nullptr)
                            running = &v;
                    for (const auto& v : catalog.variants)
                        if (v.status.loaded && running == nullptr)
                            running = &v;

                    std::vector<std::pair<Led, std::string>> facts;
                    if (running != nullptr)
                    {
                        const IlionUpscalers::Entry* entry = running->entry;
                        const bool hot = running->vkDriver || running->backend != Upscaler::Reset || running->ffxIndex >= 0 ||
                                         (entry != nullptr && IlionUpscalers::SwitchesAsFfx(*entry));
                        if (hot)
                            facts.push_back({ Led::Green, Lx("Switches while the game runs.", "Se cambia en caliente, con el juego en marcha.",
                                                             "Troca a quente, com o jogo em execução.", "Переключается на лету, во время игры.") });
                        else if (entry != nullptr && entry->kind == IlionUpscalers::Kind::D4r)
                            facts.push_back({ Led::Orange, Lx("d4r is loaded when the game starts: changing it needs a restart.",
                                                              "d4r se carga al iniciar el juego: cambiarlo requiere reiniciar.",
                                                              "O d4r é carregado ao iniciar o jogo: trocá-lo exige reiniciar.",
                                                              "d4r загружается при запуске игры: для смены нужен перезапуск.") });
                        if (entry != nullptr && IlionUpscalers::SwitchesAsFfx(*entry) && state.api != DX12)
                            facts.push_back({ Led::Green, Lx("Runs through OptiScaler's DirectX 12 interop.", "Funciona a través del interop DirectX 12 de OptiScaler.",
                                                             "Funciona pelo interop DirectX 12 do OptiScaler.", "Работает через интероп DirectX 12 OptiScaler.") });
                        if (entry != nullptr && entry->kind == IlionUpscalers::Kind::Provider)
                            facts.push_back({ Led::Off, entry->key == L"FSR4/AMD driver"
                                                            ? Lx("AMD driver dll, used from the installed driver.", "DLL del driver de AMD, usada desde el driver instalado.",
                                                                 "DLL do driver da AMD, usada a partir do driver instalado.", "DLL драйвера AMD из установленного драйвера.")
                                                            : Lx("Copy of an AMD driver dll in an ILION folder.", "Copia de una DLL del driver de AMD en una carpeta de ILION.",
                                                                 "Cópia de uma DLL do driver da AMD numa pasta do ILION.", "Копия DLL драйвера AMD в папке ILION.") });
                        if (entry != nullptr && entry->family == IlionUpscalers::Family::HelixSR && entry->hlx)
                            facts.push_back({ Led::Green, Lx("HLX FSR 4, identified by its file; ILION checks that it is the dll running.",
                                                             "HLX FSR 4, identificado por su archivo; ILION comprueba que es la DLL en ejecución.",
                                                             "HLX FSR 4, identificado pelo arquivo; o ILION verifica que é a DLL em execução.",
                                                             "HLX FSR 4, опознан по файлу; ILION проверяет, что работает именно эта DLL.") });
                        if (entry != nullptr && entry->forwardsToAmd)
                            facts.push_back({ Led::Off, Lx("AMD's FSR is next to it; ILION starts HelixSR's own provider.",
                                                           "El FSR de AMD está a su lado; ILION inicia el proveedor propio de HelixSR.",
                                                           "O FSR da AMD está ao lado; o ILION inicia o provedor próprio do HelixSR.",
                                                           "Рядом FSR от AMD; ILION запускает собственный провайдер HelixSR.") });
                    }

                    if (!facts.empty())
                    {
                        ImGui::Spacing();
                        IlionTheme::SectionPlate(Lx("Capabilities of the active provider", "Capacidades del proveedor activo",
                                                    "Capacidades do provedor ativo", "Возможности активного провайдера"));
                        for (const auto& [led, text] : facts)
                        {
                            const ImVec2 p = ImGui::GetCursorScreenPos();
                            const float h = ImGui::GetTextLineHeight();
                            ShellLedAt(ImGui::GetWindowDrawList(), ImVec2(p.x + h * 0.5f, p.y + h * 0.5f), h * 0.22f, led);
                            ImGui::Dummy(ImVec2(h, h));
                            ImGui::SameLine(0, U(6));
                            ImGui::PushStyleColor(ImGuiCol_Text, Tone(pal::Text2));
                            ImGui::TextWrapped("%s", text.c_str());
                            ImGui::PopStyleColor();
                        }
                    }

                    // ILION dev.78: the dll of the implementation that runs, on one discreet line: its file name and file
                    // version as the catalogue reads them (the whole path and the checks are in Diagnostics)
                    if (running != nullptr)
                    {
                        std::string dll;
                        if (running->entry != nullptr)
                        {
                            dll = wstring_to_string(std::filesystem::path(running->entry->path).filename().wstring());
                            if (!running->entry->fileVersion.empty())
                                dll += "  ·  " + running->entry->fileVersion;
                        }
                        else if (running->backend != Upscaler::Reset || running->ffxIndex >= 0 || running->vkDriver)
                            dll = Lx("Built into OptiScaler", "Integrado en OptiScaler", "Integrado ao OptiScaler", "Встроен в OptiScaler");
                        else
                            dll = Lx("AMD's FSR dll next to OptiScaler, or the game's own", "DLL de FSR de AMD junto a OptiScaler, o la del juego",
                                     "DLL do FSR da AMD junto ao OptiScaler, ou a do jogo", "DLL FSR от AMD рядом с OptiScaler или игровая");
                        ImGui::Spacing();
                        ImGui::Separator();
                        ImGui::TextColored(Tone(pal::Dim), "DLL");
                        ImGui::SameLine(0, U(8));
                        ImGui::PushStyleColor(ImGuiCol_Text, Tone(pal::Text2));
                        ImGui::TextWrapped("%s", dll.c_str());
                        ImGui::PopStyleColor();
                    }
                }

                // ILION v1.3.0: the room left at the bottom (measured) shared between the three gaps above, up to 24 each,
                // 16 kept free. A provider with more settings than the area holds gets no gaps (and scrolls, as before)
                {
                    const float content = ImGui::GetCursorPosY() - 3.0f * compactUpGap;
                    const float spare = ImGui::GetWindowHeight() - content - U(16.f);
                    compactUpGap = spare > 0.0f ? std::floor((std::min)(spare / 3.0f, U(24.f))) : 0.0f;
                }
            }
            else if (compactModule)
            {
                // The other modules: their sub-section, drawn by its own code in one column
                if (shownCount > 0)
                {
                    const Sub& sub = subs[compactSub[compactSection]];
                    const ShellLayout* layout = sub.layout == &lFrameGen ? &lFrameGenC : sub.layout == &lLatency ? &lLatencyC : sub.layout;
                    shellFgInstrumentsApart = layout == &lFrameGenC;
                    shellFgQuickInline = layout == &lFrameGenC;
                    RenderMenuTabBody(ctx, tabIndex(sub.tab), false, layout, sub.flow);

                    // ILION dev.79: under the FG monitors, the API FG works on and the FG outputs its selector offers here
                    // (as the selection itself enables them: selectable, not a promise that they generate)
                    if (layout == &lFrameGenC && fgOptionsSnapshot.evaluated)
                    {
                        std::string outputs;
                        const auto add = [&](FGOutput o, const char* name)
                        {
                            if (fgOptionsSnapshot.outputUsable[(int) o])
                                outputs += (outputs.empty() ? "" : ", ") + std::string(name);
                        };
                        add(FGOutput::XeFG, "XeFG");
                        add(FGOutput::FSRFG, "FSR FG");
                        add(FGOutput::DLSSG, "DLSSG");
                        std::string strip = StrFmt("API %s", ApiName(state.swapchainApi));
                        if (state.swapchainApi == Vulkan && state.activeFgOutput == FGOutput::XeFG)
                            strip += std::string("  ·  ") + Lx("XeFG through the Vulkan bridge", "XeFG por el puente Vulkan", "XeFG pela ponte Vulkan",
                                                               "XeFG через мост Vulkan");
                        strip += std::string("  ·  ") + Lx("Outputs selectable here: ", "Salidas seleccionables aquí: ", "Saídas selecionáveis aqui: ",
                                                           "Доступные выходы: ") +
                                 (outputs.empty() ? std::string("-") : outputs);
                        ImGui::Separator();
                        ImGui::PushStyleColor(ImGuiCol_Text, Tone(pal::Text2));
                        ImGui::TextWrapped("%s", strip.c_str());
                        ImGui::PopStyleColor();
                    }
                    shellFgInstrumentsApart = false;
                    shellFgQuickInline = false;
                    fgCompactComboWidth = 0.0f;
                }
                else
                    Label(Lx("Nothing to show in this section for this game.", "Nada que mostrar en esta sección para este juego.",
                             "Nada a mostrar nesta seção para este jogo.", "В этом разделе для этой игры ничего нет."),
                          pal::Text2, 16.f);
            }
            else
            {
                // Diagnostics: the technical information of the modules, as their own cards report it (read-only, except
                // Compatibility, whose settings are these of Full)
                Label(Lx("Technical information: what is chosen, loaded, active and generating, as ILION reads it.",
                         "Información técnica: lo elegido, cargado, activo y generando, tal como lo lee ILION.",
                         "Informação técnica: o escolhido, carregado, ativo e gerando, como o ILION lê.",
                         "Техническая информация: выбрано, загружено, активно и генерирует — как это читает ILION."),
                      pal::Text2, 15.f);
                const int statusTab = tabIndex("Help / Status");
                const auto part = [&](const char* label, const ShellLayout* layout, int tab)
                {
                    if (IlionTheme::CollapsingHeader(label))
                        RenderMenuTabBody(ctx, tab, false, layout, 0);
                };
                part(Lx("Summary###DiagSummary", "Resumen###DiagSummary", "Resumo###DiagSummary", "Сводка###DiagSummary"), &dSummary, statusTab);
                part(Lx("Upscaler: DLL, version and checks###DiagUp", "Escalador: DLL, versión y comprobaciones###DiagUp",
                        "Upscaler: DLL, versão e verificações###DiagUp", "Апскейлер: DLL, версия и проверки###DiagUp"), &dUpscaler, statusTab);
                part(Lx("Frame Generation: state and monitors###DiagFg", "Frame Generation: estado y monitores###DiagFg",
                        "Frame Generation: estado e monitores###DiagFg", "Генерация кадров: состояние и мониторы###DiagFg"), &dFrameGen, statusTab);
                if (IlionTheme::CollapsingHeader(Lx("Streamline###DiagSl", "Streamline###DiagSl", "Streamline###DiagSl", "Streamline###DiagSl")))
                {
                    const auto v = state.streamlineVersion;
                    const bool slFound = v.major != 0 || v.minor != 0 || v.patch != 0;
                    const char* yes = Lx("yes", "sí", "sim", "да");
                    const char* no = Lx("no", "no", "não", "нет");
                    ImGui::Text("%s: %s", Lx("Streamline version", "Versión de Streamline", "Versão do Streamline", "Версия Streamline"),
                                slFound ? StrFmt("%u.%u.%u", v.major, v.minor, v.patch).c_str() : Lx("not detected", "no detectada", "não detectada", "не обнаружена"));
                    ImGui::Text("%s: %s", Lx("Reflex hooked", "Reflex enganchado", "Reflex conectado", "Reflex перехвачен"), ReflexHooks::isReflexHooked() ? yes : no);
                    ImGui::Text("%s: %s", Lx("Game's DLSSG as FG input (loaded)", "DLSSG del juego como entrada de FG (cargada)",
                                             "DLSSG do jogo como entrada de FG (carregada)", "DLSSG игры как вход FG (загружен)"),
                                state.activeFgInput == FGInput::DLSSG ? yes : no);
                    ImGui::Text("%s: %s", Lx("DLSSG frames detected", "Fotogramas DLSSG detectados", "Quadros DLSSG detectados", "Обнаружены кадры DLSSG"),
                                state.dlssgDetectedInterpolationCount > 0 ? StrFmt("x%d", state.dlssgDetectedInterpolationCount + 1).c_str() : no);
                }
                part(Lx("Neural Rendering: state###DiagNr", "Neural Rendering: estado###DiagNr", "Neural Rendering: estado###DiagNr",
                        "Neural Rendering: состояние###DiagNr"), &dNr, statusTab);
                part(Lx("Latency: meters and report###DiagLat", "Latencia: medidores e informe###DiagLat", "Latência: medidores e relatório###DiagLat",
                        "Задержка: измерители и отчёт###DiagLat"), &dLatency, statusTab);
                part(Lx("System###DiagSys", "Sistema###DiagSys", "Sistema###DiagSys", "Система###DiagSys"), &dSystem, statusTab);
                part(Lx("Detected, hooks and injection###DiagDet", "Detectado, hooks e inyección###DiagDet", "Detectado, hooks e injeção###DiagDet",
                        "Обнаружено, хуки и внедрение###DiagDet"), &dDetected, statusTab);
                part(Lx("Compatibility settings###DiagCompat", "Ajustes de compatibilidad###DiagCompat", "Ajustes de compatibilidade###DiagCompat",
                        "Настройки совместимости###DiagCompat"), &lCompat, tabIndex("Compatibility"));
                part(Lx("Log###DiagLog", "Registro###DiagLog", "Registro###DiagLog", "Журнал###DiagLog"), &lLog, statusTab);
            }

            ImGui::PopItemWidth();
        }
        ImGui::EndChild();
        ImGui::PopStyleVar(2);
        ImGui::PopFont();
        PopContentStyle();

        // Footer: save, close and the state of the last change
        At(16, CH - 52);
        if (Button("##CompactSave", Tr("Save Settings"), ImVec2(160, 40), false, pal::Green, 15.f))
            config->SaveIni();
        At(CW - 16 - 130, CH - 52);
        if (Button("##CompactClose", Tr("Close"), ImVec2(130, 40), false, pal::Green, 15.f))
            CloseMainMenu(io);
        ApplyState(ImVec2(20, CH - 100), CW - 40); // Dedicated wrapped status row, away from Save/Close.

        // The Advisor's diagnosis, opened from the same ID stack it is drawn in
        if (openAdvisor)
            ImGui::OpenPopup("##IlionAdvisorPopup");
        advisorDiagnosis();

        ImGui::End();
        ImGui::PopStyleColor(2);
        return;
    }

    // Sidebar ------------------------------------------------------------------------------------------------------
    const bool wide = config->MenuSidebarWide.value_or_default();
    const float side = wide ? 260.f : 112.f;
    {
        dl->AddRectFilled(P(0, 0), P(side, H), Col(ImVec4(0.025f, 0.03f, 0.028f, 1)), U(6.0f), ImDrawFlags_RoundCornersLeft);
        float y = 70;
        // ILION Perfiles: nine modules; the items shrink to the room above the footer (104 apart while they fit)
        int navShown = 0;
        for (int i = 0; i < kSectionCount; i++)
            navShown += sectionShown(i) ? 1 : 0;
        const float step = (std::min)(104.f, std::floor((H - 84.f - 70.f + 8.f) / (float) (std::max)(navShown, 1)));
        const float itemH = step - 8.f;

        for (int n = 0; n < kSectionCount; n++)
        {
            const int i = compact ? kNavOrder[n] : kFullNavOrder[n];
            if (!sectionShown(i))
                continue; // the next one takes its place: no gap

            const ImVec2 p(8, y), q(side - 8, y + itemH);
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
                    dl->AddRect(P(p.x - k, p.y - k), P(q.x + k, q.y + k), Col(pal::Orange, 0.07f * (5 - k) * IlionTheme::shellGlow), U(7.f), 0, 1.f);
            dl->AddRect(P(p), P(q), Col(sel ? pal::Orange : pal::Line), U(6.f), 0, sel ? U(2.f) : 1.f);
            const ImU32 ic = Col(sel ? pal::Orange : (hov ? pal::Text : pal::Text2));

            if (wide)
            {
                // The full name when it fits, else smaller, else the short one of the compact bar
                Icon(IconOf(i), ImVec2(p.x + 40, p.y + itemH * 0.5f), 34.f, ic);
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
                Text(fUi, fs, ImVec2(p.x + 74, p.y + itemH * 0.5f - ts.y * 0.5f), sel ? pal::Orange : pal::Text, name);
            }
            else
            {
                Icon(IconOf(i), ImVec2((p.x + q.x) * 0.5f, p.y + itemH * 0.4f), 34.f, ic);
                const char* caps = kSections[i].caps[li];
                float fs = 16.f;
                while (fs > 11.f && Measure(fUi, fs, caps).x > q.x - p.x - 8)
                    fs -= 1.f;
                const ImVec2 ts = Measure(fUi, fs, caps);
                Text(fUi, fs, ImVec2((p.x + q.x - ts.x) * 0.5f, p.y + itemH * 0.69f), sel ? pal::Orange : pal::Text, caps);
            }

            // Activity pilot, apart from the selection
            if (sectionLed[i] != Led::Off)
                LedAt(dl, ImVec2(q.x - 14, p.y + 14), 5.f, sectionLed[i]);
            y += step;
        }

        // Expand / collapse: the user's choice, kept in the ini; it never opens by itself
        // Attribution: ILION is a fork of OptiScaler (its licence and credits are unchanged, see the package)
        {
            const char* fork = "OptiScaler Fork";
            const ImVec2 ts = Measure(fUi, 12.f, fork);
            Text(fUi, 12.f, ImVec2((side - ts.x) * 0.5f, H - 74), pal::Dim, fork);
        }

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
        for (float x = a.x + 420; IlionTheme::shellDecor && x < fx - 40; x += 14)
            dl->AddLine(P(x, a.y + 1), P(x - 60, b.y - 1), Col(pal::Line, 0.45f), 1.f);

        // ILION Advisor's module: the centre of the header, between the game and the figures
        const float advX0 = a.x + std::clamp((fx - 40 - a.x) * 0.40f, 300.f, 420.f);
        const float advX1 = fx - 34;

        const char* currentGameLabel = Lx("CURRENT GAME", "JUEGO ACTUAL", "JOGO ATUAL", "ТЕКУЩАЯ ИГРА");
        Text(fUi, 16.f, ImVec2(a.x + 22, a.y + 18), pal::Yellow, currentGameLabel);
        const std::string game = !state.gameName.empty() ? state.gameName : state.gameExe;
        // ILION v1.4.0: where the left column's content really ends (label, game name as drawn, API · ILION ACTIVE)
        float leftEnd = a.x + 22 + Measure(fUi, 16.f, currentGameLabel).x;
        {
            const char* shownGame = game.empty() ? "-" : game.c_str();
            const float room = advX0 - 14 - (a.x + 22);
            float gs = 44.f;
            while (gs > 26.f && Measure(fUi, gs, shownGame).x > room)
                gs -= 2.f;
            dl->PushClipRect(P(a.x, a.y), P(advX0 - 10, b.y), true);
            Text(fUi, gs, ImVec2(a.x + 22, a.y + 40 + (44.f - gs) * 0.5f), pal::Text, shownGame);
            dl->PopClipRect();
            leftEnd = (std::max)(leftEnd, (std::min)(a.x + 22 + Measure(fUi, gs, shownGame).x, advX0 - 10));
        }

        std::string api = ApiName(state.api);
        if (IdentifyGpu::gameUsesDxvk())
            api += " (DXVK)";
        else if (IdentifyGpu::gameUsesVkd3dProton())
            api += " (VKD3D)";
        Text(fUi, 20.f, ImVec2(a.x + 22, a.y + 108), pal::Text2, api.c_str());
        const float sep = a.x + 22 + Measure(fUi, 20.f, api.c_str()).x + 18;
        dl->AddLine(P(sep, a.y + 110), P(sep, a.y + 130), Col(pal::Line), 1.f);
        // The menu is drawn by ILION inside the game: ILION is running
        // ILION Advisor: facts once a second while the menu is open, the suggestion changes only after holding 5 s
        advisorUpdate();

        // ILION AL DÍA / ILION TERMINAL in the box Advisor used (its facts are still gathered above). ILION v1.4.0: the
        // same box (same width and height), centred in the real space between the end of the left column and the FPS
        // figure (drawn from fx - 4). It only moves left, and keeps at least 14 px from the left column's content
        const char* activeLabel = Lx("ILION ACTIVE", "ILION ACTIVO", "ILION ATIVO", "ILION АКТИВЕН");
        leftEnd = (std::max)(leftEnd, sep + 44 + Measure(fUi, 20.f, activeLabel).x);
        const float boxW = advX1 - advX0;
        const float rightStart = fx - 4;
        const float centred = leftEnd + ((rightStart - leftEnd) - boxW) * 0.5f;
        const float boxX0 = (std::min)(advX0, (std::max)(centred, leftEnd + 14));
        if (boxW > 200)
            HomePanel::Draw(dl, P(boxX0, a.y + 10), P(boxX0 + boxW, b.y - 10), ctx.frameTime);

        // The diagnosis: hardware, the six answers with their level, reasons and warnings. It changes nothing
        advisorDiagnosis();

        // The pilot centred on the line of its text, its glow clear of the letters
        const float activeY = a.y + 108 + Measure(fUi, 20.f, "I").y * 0.5f;
        LedAt(dl, ImVec2(sep + 24, activeY), 6.f, Led::Green);
        Text(fUi, 20.f, ImVec2(sep + 44, a.y + 108), pal::Green, activeLabel);

        // FPS and frame time measured by ILION in this game
        const double ft = ctx.frameTime;
        const bool measured = std::isfinite(ft) && ft > 0.0;
        Text(fUi, 18.f, ImVec2(fx, a.y + 16), pal::Text2, "FPS");
        // The FPS figure in the colour of its band (the same counter, no smoothing added)
        const double fpsNow = measured ? 1000.0 / ft : 0.0;
        const ImVec4 fpsColour = !measured ? pal::Text : IlionTheme::FpsBand(fpsNow); // the shared criterion
        Text(fUi, 64.f, ImVec2(fx - 4, a.y + 34), fpsColour, measured ? StrFmt("%.0f", fpsNow).c_str() : "-");
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
        const std::string ram = memory.ullTotalPhys == 0 ? std::string("-")
                                                         : StrFmt("%.1f/%.0f GB",
                                                                  (memory.ullTotalPhys - memory.ullAvailPhys) / 1073741824.0,
                                                                  memory.ullTotalPhys / 1073741824.0);
        const std::string head = StrFmt("GPU %s  |  CPU %s  |  VRAM ", gpu != nullptr ? StrFmt("%.0f%%", gpu->value).c_str() : "-",
                                        cpu != nullptr ? StrFmt("%.0f%%", cpu->value).c_str() : "-");
        const std::string usedText = vramUsed == nullptr ? std::string("-") : StrFmt("%.1f", vramUsed->value / 1024.0f);
        const std::string totalText = vramUsed == nullptr ? std::string() : vramTotal != nullptr ? StrFmt("/%.0f GB", vramTotal->value / 1024.0f) : std::string(" GB");
        const std::string tail = StrFmt("  |  RAM %s", ram.c_str());

        // The VRAM in use: white under 8 GB, yellow up to 12, electric blue up to 16, red above
        const float usedGb = vramUsed != nullptr ? vramUsed->value / 1024.0f : 0.0f;
        const ImVec4 vramColour = vramUsed == nullptr ? pal::Text2
                                  : usedGb < 8.0f     ? pal::Text
                                  : usedGb <= 12.0f   ? pal::Yellow
                                  : usedGb <= 16.0f   ? ImVec4(0.25f, 0.70f, 1.00f, 1.f)
                                                      : pal::Red;
        float hx = ga.x;
        Text(fUi, 15.f, ImVec2(hx, a.y + 124), pal::Text2, head.c_str());
        hx += Measure(fUi, 15.f, head.c_str()).x;
        Text(fUi, 15.f, ImVec2(hx, a.y + 124), vramColour, usedText.c_str());
        hx += Measure(fUi, 15.f, usedText.c_str()).x;
        Text(fUi, 15.f, ImVec2(hx, a.y + 124), pal::Text2, (totalText + tail).c_str());
    }

    // Section header with the interface mode -----------------------------------------------------------------------
    const Section& sec = kSections[section];
    {
        // A low band: icon, name and what the section is for on one line
        const ImVec2 a(x0, 252), b(x1, 302);
        PanelRect(a, b);
        Icon(IconOf(section), ImVec2(a.x + 30, a.y + 25), 28.f, Col(pal::Green));
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

        At(ia.x + 60, ia.y + 6);
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
        Text(fUi, 19.f, ImVec2(ia.x + 66, ia.y + 9), pal::Yellow, on ? "INFO ILION" : "INFO ILION  (OFF)");

        const float buttons = 110 + 12 + 160 + 12 + 130;
        // The line advances its own animation with every draw: once a frame, so two calls can't mix two messages
        static int infoFrame = -1;
        const bool drawLine = infoFrame != ImGui::GetFrameCount();
        infoFrame = ImGui::GetFrameCount();
        IlionInfo::flatText = true;
        if (drawLine)
            IlionInfo::Draw(dl, P(ia.x + 66, ia.y + 38), P(ib.x - buttons - 40, ia.y + 72), on, fUi, std::floor(U(17.f)), &Ilion::Tr,
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
            const bool dense = layout == &lStatus || layout == &lCompat || layout == &lLog;
            if (dense)
            {
                ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, Sz(10.f, 5.f));
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, Sz(9.f, 5.f));
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, Sz(14.f, 10.f));
            }
            RenderMenuTabBody(ctx, tab, true, layout, flow);
            if (dense)
                ImGui::PopStyleVar(3);
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
        const float rowSplit = a.y + 300, rowBottom = b.y - 100;
        const float half = a.x + (b.x - a.x) * 0.5f;

        const auto card = [&](const char* id, int icon, const char* title, Led led, ImVec2 p, ImVec2 q, const auto& controls)
        {
            PanelRect(p, q);
            Icon(icon, ImVec2(p.x + 30, p.y + 26), 26.f, Col(pal::Green));
            Text(fUi, 19.f, ImVec2(p.x + 56, p.y + 14), pal::Text, title);
            LedAt(dl, ImVec2(q.x - 22, p.y + 24), 6.f, led);
            dl->AddLine(P(p.x + 14, p.y + 46), P(q.x - 14, p.y + 46), Col(pal::Line));

            PushContentStyle();
            ImGui::PushFont(fUi, U(17.f));
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, Sz(10.f, 6.f));
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, Sz(10.f, 5.f));
            if (BeginArea(id, ImVec2(p.x + 16, p.y + 54), ImVec2(q.x - 12, q.y - 6)))
                controls();
            ImGui::PopStyleVar(2);
            ImGui::EndChild();
            ImGui::PopFont();
            PopContentStyle();
        };

        // Upscaling: technology and implementation, the catalogue and selection of the module
        card("##HomeUp", 1, kSections[1].name[li], sectionLed[1], ImVec2(a.x, a.y), ImVec2(half - 8, rowSplit),
             homeUpscaling);

        // Frame generation: input, output and variant (the module's selectors and their conditions), then on/off,
        // the multiplier and the states
        card("##HomeFg", 2, kSections[2].name[li], fgLed, ImVec2(half + 8, a.y), ImVec2(b.x, rowSplit),
             homeFrameGen);

        // Latency and FPS: the quick switches of fakenvapi (XeLL, LatencyFleX, Reflex), V-Sync, the FPS limit and the overlay
        card("##HomeLat", 3, kSections[3].name[li], sectionLed[3], ImVec2(a.x, rowSplit + 16), ImVec2(b.x, rowBottom),
             homeLatency);

        infoBar(ImVec2(a.x, b.y - 84), b);
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
        IlionTheme::ShellComboLimit("##UpFilter");
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
                        ldl->AddRect(P(p.x - k + 1, p.y - k + 1), P(q.x + k - 1, q.y + k - 1), Col(pal::Green, 0.08f * (4 - k) * IlionTheme::shellGlow), U(5.f));
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


        const ImVec2 c(pa.x, pa.y + 118), dFull(pb.x, pb.y - 98);
        const float mx = c.x + (dFull.x - c.x) * 0.57f;
        const ImVec2 d(mx - 8, dFull.y);
        PanelRect(c, d);

        // Upscaling monitor: what runs, from the running feature and the dispatch diagnostics
        {
            const ImVec2 m0(mx + 8, c.y), m1(dFull.x, dFull.y);
            PanelRect(m0, m1, false, &pal::Green);
            dl->PushClipRect(P(m0.x + 2, m0.y + 2), P(m1.x - 2, m1.y - 2), true);
            Text(fUi, 15.f, ImVec2(m0.x + 18, m0.y + 14), pal::Text2,
                 Lx("UPSCALING MONITOR", "MONITOR DE ESCALADO", "MONITOR DE ESCALA", "МОНИТОР МАСШТАБИРОВАНИЯ"));
            LedAt(dl, ImVec2(m1.x - 40, m0.y + 30), 6.f, upscalerOn ? Led::Green : upscalerIdle ? Led::Yellow : Led::Off);

            // The technology in the radioactive yellow of ILION
            const std::string tech = upscalerOn || upscalerIdle ? UpscalerLabel(feature) : std::string(Tr("None"));
            // "FSR 4.1.2" in yellow, what follows it ("· Driver (via SDK)") in white
            {
                const size_t cut = tech.find(" \xC2\xB7");
                const size_t paren = tech.find(" (");
                const size_t split = cut != std::string::npos ? cut : paren;
                const bool known = upscalerOn || upscalerIdle;
                const std::string first = split != std::string::npos ? tech.substr(0, split) : tech;
                Text(fUi, 28.f, ImVec2(m0.x + 18, m0.y + 38), known ? pal::Yellow : pal::Text2, first.c_str());
                if (split != std::string::npos)
                    Text(fUi, 28.f, ImVec2(m0.x + 18 + Measure(fUi, 28.f, first.c_str()).x, m0.y + 38), pal::Text, tech.substr(split).c_str());
            }

            std::string impl = "-";
            if (upscalerOn && RunsSwitchedFfx(feature->GetUpscalerType()))
                impl = Tr(FfxApiProxy::SrDiagGetSnapshot().activeName.c_str());
            else if (best != nullptr && rank == 3)
                impl = best->name;
            Text(fUi, 16.f, ImVec2(m0.x + 18, m0.y + 76), pal::Text2,
                 (std::string(Lx("Implementation", "Implementación", "Implementação", "Реализация")) + ":  " + impl).c_str());

            const char* runState = upscalerOn     ? Lx("Running", "En ejecución", "Em execução", "Работает")
                                   : upscalerIdle ? Tr("Idle")
                                                  : Lx("Not running", "Sin ejecutar", "Sem executar", "Не работает");
            At(m0.x + 18, m0.y + 102);
            Pill(runState, upscalerOn ? Led::Green : upscalerIdle ? Led::Yellow : Led::Off);

            // The file that runs: its version and its path, as the loader reports them
            std::wstring dllPath;
            std::string fileVersion;
            if (upscalerOn && RunsSwitchedFfx(feature->GetUpscalerType()))
                dllPath = FfxApiProxy::Dx12Module_SR_Path();
            if (best != nullptr && rank == 3 && best->entry != nullptr)
            {
                if (dllPath.empty())
                    dllPath = best->entry->path;
                fileVersion = best->entry->fileVersion;
            }
            // The version at the right of the state when both fit with 14 of room between them, else on its own line
            float ys = 0.f;
            if (!fileVersion.empty())
            {
                const std::string version = std::string(Tr("File version")) + ": " + fileVersion;
                const float versionW = Measure(fUi, 15.f, version.c_str()).x;
                const float stateEnd = m0.x + 18 + Measure(fUi, 18.f, runState).x + 46;
                if (stateEnd + 14 + versionW <= m1.x - 18)
                    Text(fUi, 15.f, ImVec2(m1.x - 18 - versionW, m0.y + 109), pal::Text2, version.c_str());
                else
                {
                    Text(fUi, 15.f, ImVec2(m0.x + 18, m0.y + 140), pal::Text2, version.c_str());
                    ys = 24.f;
                }
            }

            // Resolution: what the game renders in white, what reaches the screen in yellow
            if (upscalerOn && feature->RenderWidth() > 0)
            {
                const std::string in = StrFmt("%ux%u", feature->RenderWidth(), feature->RenderHeight());
                const std::string out = StrFmt("%ux%u", feature->TargetWidth(), feature->TargetHeight());
                float x = m0.x + 18;
                Text(fUi, 22.f, ImVec2(x, m0.y + 146 + ys), pal::Text, in.c_str());
                x += Measure(fUi, 22.f, in.c_str()).x;
                Text(fUi, 22.f, ImVec2(x, m0.y + 146 + ys), pal::Text2, "  ->  ");
                x += Measure(fUi, 22.f, "  ->  ").x;
                Text(fUi, 22.f, ImVec2(x, m0.y + 146 + ys), pal::Yellow, out.c_str());
                const float factor = (float) feature->TargetWidth() / feature->RenderWidth();
                Text(fUi, 15.f, ImVec2(m0.x + 18, m0.y + 176 + ys), pal::Text2,
                     StrFmt("%s %.2fx  ·  %s %.0f%%", Lx("Factor", "Factor", "Fator", "Коэффициент"), factor,
                            Lx("rendered pixels", "píxeles renderizados", "pixels renderizados", "отрисовано пикселей"), 100.0f / (factor * factor))
                         .c_str());
            }

            // Upscaler GPU time: its average and a short graph of the last frames, as measured by OptiScaler
            const auto& times = state.upscaleTimes;
            const size_t count = (std::min)(times.size(), (size_t) 120);
            double sum = 0.0, top = 1.0;
            int valid = 0;
            for (size_t k = times.size() - count; k < times.size(); k++)
            {
                if (times[k] > 0.0)
                {
                    sum += times[k];
                    valid++;
                    top = (std::max)(top, times[k]);
                }
            }

            float y = m0.y + 204 + ys;
            if (upscalerOn && valid > 0)
            {
                Text(fUi, 15.f, ImVec2(m0.x + 18, y), pal::Text2, Lx("Upscaler time", "Tiempo del escalador", "Tempo do upscaler", "Время апскейлера"));
                const std::string avg = StrFmt("%.2f ms", sum / valid);
                Text(fUi, 18.f, ImVec2(m1.x - 18 - Measure(fUi, 18.f, avg.c_str()).x, y - 2), pal::Green, avg.c_str());
                const ImVec2 ga(m0.x + 18, y + 24), gb(m1.x - 18, y + 60);
                dl->AddRect(P(ga), P(gb), Col(pal::Line));
                if (count > 1)
                {
                    ImVec2 pts[120];
                    for (size_t k = 0; k < count; k++)
                    {
                        const double v = std::clamp(times[times.size() - count + k], 0.0, top);
                        pts[k] = P(ga.x + (gb.x - ga.x) * k / (float) (count - 1), gb.y - (gb.y - ga.y) * (float) (v / top));
                    }
                    dl->AddPolyline(pts, (int) count, Col(pal::Orange), 0, U(1.3f));
                }
                y = gb.y + 8;

                // Its share of the frame time (two measured times, not a loss of FPS) and the shaders OptiScaler times
                const double frame = ctx.frameTime;
                if (std::isfinite(frame) && frame > 0.0)
                {
                    Text(fUi, 14.f, ImVec2(m0.x + 18, y), pal::Text2,
                         StrFmt("%s %.0f%%", Lx("Upscaler time / frame time:", "Tiempo del escalador / frametime:", "Tempo do upscaler / frametime:",
                                                "Время апскейлера / время кадра:"),
                                std::clamp(sum / valid / frame * 100.0, 0.0, 100.0))
                             .c_str());
                    y += 20;
                }
                int shaders = 0;
                double shaderMs = 0.0;
                for (const auto& [name, time, included] : state.detailedGpuTimes)
                {
                    if (included)
                    {
                        shaders++;
                        shaderMs += time;
                    }
                }
                if (shaders > 0)
                {
                    Text(fUi, 14.f, ImVec2(m0.x + 18, y), pal::Text2,
                         StrFmt("%s: %d  ·  %.2f ms", Lx("Shaders timed on the GPU", "Shaders medidos en GPU", "Shaders medidos na GPU", "Шейдеров измерено на GPU"),
                                shaders, shaderMs)
                             .c_str());
                    y += 20;
                }
            }

            // The path of the dll that runs: small, on up to two lines, whole in its tooltip
            if (!dllPath.empty())
            {
                y += 6;
                const std::string path = wstring_to_string(dllPath);
                Text(fUi, 13.f, ImVec2(m0.x + 18, y), pal::Dim, "DLL");
                const ImVec2 t0 = P(m0.x + 18, y + 16), t1 = P(m1.x - 18, m1.y - 34);
                const float lineHeight = U(15.f);
                const float room = t1.x - t0.x;
                float ly = t0.y;
                size_t from = 0;
                bool cut = false;
                while (from < path.size())
                {
                    if (ly + lineHeight > t1.y)
                    {
                        cut = true;
                        break;
                    }
                    size_t to = from + 1;
                    while (to < path.size() && fUi->CalcTextSizeA(U(13.f), FLT_MAX, 0.0f, path.c_str() + from, path.c_str() + to + 1).x <= room)
                        to++;
                    dl->AddText(fUi, U(13.f), ImVec2(t0.x, ly), Col(pal::Text2), path.c_str() + from, path.c_str() + to);
                    from = to;
                    ly += lineHeight;
                }
                if (cut && ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(t0, t1))
                    ImGui::SetTooltip("%s", path.c_str());
            }

            // What the game feeds the upscaler, and its API
            Text(fUi, 15.f, ImVec2(m0.x + 18, m1.y - 28), pal::Dim,
                 StrFmt("%s: %s  ·  %s", Lx("Input", "Entrada", "Entrada", "Вход"), ApiUpscalerInputName(state.currentInputApiName).c_str(),
                        ApiName(state.api))
                     .c_str());
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
        IlionTheme::ShellComboLimit("##UpVariant");
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

        }
        ImGui::EndChild();
        ImGui::PopStyleColor(3);

        ApplyState(ImVec2(c.x + 18, d.y - 46));
        infoBar(ImVec2(pa.x, pb.y - 84), pb);
    }
    else
    {
        // The other modules: their sub-sections as the secondary navigation, each tab drawn by its own code in the
        // layout of the new interface
        int shown[8];
        int shownCount = 0;
        for (int k = 0; k < subCount && shownCount < 8; k++)
        {
            if (olive && strcmp(subs[k].tab, "Custom") == 0)
                continue; // ILION dev.71: Clean Olive has no pinned cards page
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

            float top = shownCount > 1 ? a.y + 60 : a.y;

            // The pinned cards: say what they are (the same controls as in their modules, gathered on one page)
            if (strcmp(sub.tab, "Custom") == 0)
            {
                At(a.x + 4, top);
                Label(Lx("Cards you pin from the other modules, gathered on one page. They are the same controls, not copies: a change "
                         "here is the change there. Choose which ones with Customise.",
                         "Tarjetas que fijas desde los demás módulos, reunidas en una sola página. Son los mismos controles, no copias: "
                         "un cambio aquí es el cambio allí. Elige cuáles con Personalizar.",
                         "Cartões que você fixa a partir dos outros módulos, reunidos numa página. São os mesmos controles, não cópias: "
                         "uma mudança aqui é a mudança lá. Escolha quais em Personalizar.",
                         "Карточки, закреплённые из других модулей, на одной странице. Это те же элементы управления, а не копии: "
                         "изменение здесь — то же изменение там. Выберите их через «Настроить»."),
                      pal::Text2, 15.f);
                top += 30;
            }
            shellFgInstrumentsApart = sub.layout == &lFrameGen;
            shellFgQuickInline = sub.layout == &lFrameGen;
            tabPanel(tabIndex(sub.tab), ImVec2(a.x, top), ImVec2(b.x, b.y - 98), "##ShellTab", sub.layout, sub.flow);
            shellFgInstrumentsApart = false;
            shellFgQuickInline = false;
        }
        else
        {
            const ImVec2 d(b.x, b.y - 98);
            PanelRect(a, d);
            At(a.x + 18, a.y + 18);
            Label(Lx("Nothing to show in this section for this game.", "Nada que mostrar en esta sección para este juego.",
                     "Nada a mostrar nesta seção para este jogo.", "В этом разделе для этой игры ничего нет."),
                  pal::Text2, 18.f);
        }

        infoBar(ImVec2(a.x, b.y - 84), b);
    }

    ImGui::End();
    ImGui::PopStyleColor(2);
}

#pragma execution_character_set(pop)
