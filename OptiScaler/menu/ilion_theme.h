#pragma once

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>

#include <initializer_list>
#include <string>

// Industrial look of ILION: the palette, the style built from it and the two controls that are drawn by hand.
// Only what things look like lives here. The controls take and return exactly what the ImGui ones they stand in
// for do, with the same ids, so whatever is wired to them doesn't know the difference.
namespace IlionTheme
{
// Set by MenuCommon::ApplyThemeStyle from the config. Off is the classic theme with its own color settings.
inline bool industrial = true;

// Painted steel
inline constexpr ImVec4 GraphiteDeep { 0.070f, 0.075f, 0.070f, 1.0f };
inline constexpr ImVec4 Graphite { 0.105f, 0.110f, 0.103f, 1.0f };
inline constexpr ImVec4 Panel { 0.140f, 0.150f, 0.135f, 1.0f };
inline constexpr ImVec4 OliveDim { 0.185f, 0.205f, 0.150f, 1.0f };
inline constexpr ImVec4 Olive { 0.255f, 0.285f, 0.195f, 1.0f };
inline constexpr ImVec4 OliveLit { 0.340f, 0.380f, 0.250f, 1.0f };
inline constexpr ImVec4 OlivePressed { 0.150f, 0.170f, 0.120f, 1.0f };

// Bare metal of edges and levers
inline constexpr ImVec4 MetalDark { 0.270f, 0.285f, 0.260f, 1.0f };
inline constexpr ImVec4 Metal { 0.430f, 0.450f, 0.410f, 1.0f };
inline constexpr ImVec4 MetalLight { 0.640f, 0.660f, 0.610f, 1.0f };
inline constexpr ImVec4 Shadow { 0.020f, 0.022f, 0.020f, 1.0f };

// Engraved text
inline constexpr ImVec4 Text { 0.870f, 0.885f, 0.810f, 1.0f };
inline constexpr ImVec4 TextDim { 0.560f, 0.590f, 0.490f, 1.0f };
inline constexpr ImVec4 TextTitle { 0.760f, 0.800f, 0.620f, 1.0f };

// Lamps
inline constexpr ImVec4 LampGreen { 0.224f, 1.000f, 0.078f, 1.0f }; // #39FF14
inline constexpr ImVec4 LampGreenDim { 0.150f, 0.520f, 0.090f, 1.0f };
inline constexpr ImVec4 LampAmber { 1.000f, 0.690f, 0.000f, 1.0f };
inline constexpr ImVec4 LampRed { 0.800f, 0.000f, 0.000f, 1.0f }; // #CC0000
inline constexpr ImVec4 RedDim { 0.400f, 0.060f, 0.050f, 1.0f };
inline constexpr ImVec4 RedLit { 0.580f, 0.080f, 0.060f, 1.0f };

inline ImVec4 Alpha(const ImVec4& color, float alpha) { return ImVec4(color.x, color.y, color.z, alpha); }

// Every color and measure of the style. Frames get a light edge and a dark one a pixel below it, which is what
// gives buttons, drop-downs and sliders their relief without drawing anything extra.
inline void ApplyStyle(ImGuiStyle& style, float backgroundAlpha)
{
    style.WindowRounding = 0.0f;
    style.ChildRounding = 0.0f;
    style.FrameRounding = 1.0f;
    style.PopupRounding = 0.0f;
    style.ScrollbarRounding = 0.0f;
    style.GrabRounding = 1.0f;
    style.TabRounding = 0.0f;

    // The title is a name plate between the corners of the chassis
    style.WindowTitleAlign = ImVec2(0.5f, 0.5f);

    style.WindowBorderSize = 1.0f;
    style.ChildBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;
    style.FrameBorderSize = 1.0f;
    style.TabBorderSize = 1.0f;

    style.ScrollbarSize = 12.0f;
    style.GrabMinSize = 12.0f;

    const float panelAlpha = backgroundAlpha >= 0.5f ? backgroundAlpha : 0.5f;

    ImVec4* c = style.Colors;

    c[ImGuiCol_Text] = Text;
    c[ImGuiCol_TextDisabled] = TextDim;
    c[ImGuiCol_TextLink] = LampGreen;

    c[ImGuiCol_WindowBg] = Alpha(Graphite, backgroundAlpha);
    c[ImGuiCol_ChildBg] = Alpha(Panel, panelAlpha * 0.55f);
    c[ImGuiCol_PopupBg] = Alpha(GraphiteDeep, 0.98f);
    c[ImGuiCol_MenuBarBg] = GraphiteDeep;
    c[ImGuiCol_DockingEmptyBg] = GraphiteDeep;

    c[ImGuiCol_Border] = Metal;
    c[ImGuiCol_BorderShadow] = Alpha(Shadow, 0.85f);

    c[ImGuiCol_FrameBg] = Alpha(GraphiteDeep, panelAlpha);
    c[ImGuiCol_FrameBgHovered] = OliveDim;
    c[ImGuiCol_FrameBgActive] = Olive;

    c[ImGuiCol_TitleBg] = OliveDim;
    c[ImGuiCol_TitleBgActive] = Olive;
    c[ImGuiCol_TitleBgCollapsed] = Alpha(OliveDim, 0.80f);

    c[ImGuiCol_ScrollbarBg] = Alpha(GraphiteDeep, panelAlpha);
    c[ImGuiCol_ScrollbarGrab] = MetalDark;
    c[ImGuiCol_ScrollbarGrabHovered] = Metal;
    c[ImGuiCol_ScrollbarGrabActive] = MetalLight;

    c[ImGuiCol_CheckMark] = LampGreen;
    c[ImGuiCol_SliderGrab] = Metal;
    c[ImGuiCol_SliderGrabActive] = MetalLight;
    c[ImGuiCol_InputTextCursor] = LampGreen;

    c[ImGuiCol_Button] = Olive;
    c[ImGuiCol_ButtonHovered] = OliveLit;
    c[ImGuiCol_ButtonActive] = OlivePressed;

    c[ImGuiCol_Header] = OliveDim;
    c[ImGuiCol_HeaderHovered] = Olive;
    c[ImGuiCol_HeaderActive] = OliveLit;

    c[ImGuiCol_Separator] = MetalDark;
    c[ImGuiCol_SeparatorHovered] = Metal;
    c[ImGuiCol_SeparatorActive] = MetalLight;

    c[ImGuiCol_ResizeGrip] = Alpha(MetalDark, 0.60f);
    c[ImGuiCol_ResizeGripHovered] = Metal;
    c[ImGuiCol_ResizeGripActive] = MetalLight;

    c[ImGuiCol_Tab] = GraphiteDeep;
    c[ImGuiCol_TabHovered] = OliveLit;
    c[ImGuiCol_TabSelected] = Olive;
    c[ImGuiCol_TabSelectedOverline] = LampGreen;
    c[ImGuiCol_TabDimmed] = GraphiteDeep;
    c[ImGuiCol_TabDimmedSelected] = OliveDim;
    c[ImGuiCol_TabDimmedSelectedOverline] = MetalDark;

    c[ImGuiCol_DockingPreview] = Alpha(OliveLit, 0.70f);

    c[ImGuiCol_PlotLines] = LampGreen;
    c[ImGuiCol_PlotLinesHovered] = LampAmber;
    c[ImGuiCol_PlotHistogram] = LampGreenDim;
    c[ImGuiCol_PlotHistogramHovered] = LampGreen;

    c[ImGuiCol_TableHeaderBg] = Olive;
    c[ImGuiCol_TableBorderStrong] = Metal;
    c[ImGuiCol_TableBorderLight] = MetalDark;
    c[ImGuiCol_TableRowBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    c[ImGuiCol_TableRowBgAlt] = ImVec4(1.0f, 1.0f, 1.0f, 0.035f);

    c[ImGuiCol_TreeLines] = MetalDark;
    c[ImGuiCol_TextSelectedBg] = Alpha(OliveLit, 0.60f);
    c[ImGuiCol_DragDropTarget] = LampAmber;
    c[ImGuiCol_NavCursor] = LampGreen;
    c[ImGuiCol_NavWindowingHighlight] = Alpha(LampGreen, 0.70f);
    c[ImGuiCol_NavWindowingDimBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.25f);
    c[ImGuiCol_ModalWindowDimBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.60f);
}

// --- Drawn parts ---------------------------------------------------------------------------------------------

inline constexpr ImVec4 TitleAmber { 0.800f, 0.690f, 0.400f, 1.0f };
inline constexpr ImVec4 EdgeAmber { 0.720f, 0.580f, 0.200f, 1.0f };

inline ImVec4 Shade(const ImVec4& color, float factor)
{
    return ImVec4(ImMin(color.x * factor, 1.0f), ImMin(color.y * factor, 1.0f), ImMin(color.z * factor, 1.0f), color.w);
}

// Text as it is drawn on name plates and main keys: upper case, Spanish accents included. Only what is shown,
// never what identifies the control. Stops where the visible part of a label ends.
inline std::string Upper(const char* text)
{
    const char* end = ImGui::FindRenderedTextEnd(text);
    std::string out(text, end);

    for (size_t i = 0; i < out.size(); i++)
    {
        const unsigned char c = (unsigned char) out[i];

        if (c >= 'a' && c <= 'z')
        {
            out[i] = (char) (c - 32);
        }
        else if (c == 0xC3 && i + 1 < out.size())
        {
            // U+00E0..U+00FE are the lower case of U+00C0..U+00DE, except the division sign
            const unsigned char next = (unsigned char) out[i + 1];

            if (next >= 0xA0 && next <= 0xBE && next != 0xB7)
                out[i + 1] = (char) (next - 0x20);

            i++;
        }
    }

    return out;
}

// What a pilot lamp tells with its colour
enum class Lamp
{
    Green,   // on, or an action that starts something
    Amber,   // an action that stores something
    Red,     // an action that stops or removes something
    Neutral, // any other action
};

inline ImVec4 LampColor(Lamp kind)
{
    switch (kind)
    {
    case Lamp::Amber:
        return LampAmber;
    case Lamp::Red:
        return ImVec4(1.0f, 0.13f, 0.10f, 1.0f);
    case Lamp::Neutral:
        return ImVec4(0.80f, 0.83f, 0.76f, 1.0f);
    default:
        return LampGreen;
    }
}

// A round pilot lamp behind glass in a metal ring. Lit: its colour with the light gathered in the middle and a
// faint halo. Out: the same glass dark. Only drawn for a state that exists.
inline void DrawLamp(ImDrawList* draw, const ImVec2& center, float radius, bool lit, bool hovered = false,
                     bool held = false, Lamp kind = Lamp::Green)
{
    const ImVec4 color = LampColor(kind);

    // Light spilled on the panel around a lit lamp
    if (lit)
    {
        draw->AddCircleFilled(center, radius * 1.35f, ImGui::GetColorU32(Alpha(color, 0.07f)));
        draw->AddCircleFilled(center, radius * 1.18f, ImGui::GetColorU32(Alpha(color, 0.12f)));
    }

    // Ring: its seat, the metal lit from above, the dark gap between metal and glass
    draw->AddCircleFilled(center, radius, ImGui::GetColorU32(Shadow));
    draw->AddCircleFilled(center, radius - 1.0f, ImGui::GetColorU32(hovered ? MetalLight : Metal));
    draw->AddCircleFilled(ImVec2(center.x, center.y + radius * 0.10f), radius - 1.5f, ImGui::GetColorU32(MetalDark));
    draw->AddCircleFilled(center, radius * 0.74f, ImGui::GetColorU32(GraphiteDeep));

    // Glass, darker at its rim
    const float glass = radius * 0.64f;
    const ImVec2 shine(center.x - glass * 0.30f, center.y - glass * 0.30f);

    if (lit)
    {
        const float power = held ? 0.70f : 1.0f;

        draw->AddCircleFilled(center, glass, ImGui::GetColorU32(Shade(color, 0.50f * power)));
        draw->AddCircleFilled(center, glass * 0.86f, ImGui::GetColorU32(Shade(color, 0.78f * power)));
        draw->AddCircleFilled(center, glass * 0.68f, ImGui::GetColorU32(Shade(color, power)));
        draw->AddCircleFilled(center, glass * 0.42f,
                              ImGui::GetColorU32(ImVec4(color.x * 0.5f + 0.5f, color.y * 0.5f + 0.5f, color.z * 0.5f + 0.5f, 0.80f * power)));
        draw->AddCircleFilled(shine, glass * 0.18f, ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 0.95f, 0.85f)));
    }
    else
    {
        const ImVec4 dark = kind == Lamp::Green ? OliveDim : Shade(color, 0.22f);

        draw->AddCircleFilled(center, glass, ImGui::GetColorU32(Shade(dark, held ? 1.3f : 0.75f)));
        draw->AddCircleFilled(center, glass * 0.72f, ImGui::GetColorU32(Shade(dark, held ? 1.5f : 1.0f)));
        draw->AddCircleFilled(shine, glass * 0.18f, ImGui::GetColorU32(Alpha(MetalLight, 0.20f)));
    }
}

// The head of a slotted screw
inline void DrawScrew(ImDrawList* draw, const ImVec2& center, float radius)
{
    draw->AddCircleFilled(center, radius + 1.0f, ImGui::GetColorU32(Alpha(Shadow, 0.85f)));
    draw->AddCircleFilled(center, radius, ImGui::GetColorU32(MetalDark));
    draw->AddCircleFilled(ImVec2(center.x - radius * 0.2f, center.y - radius * 0.2f), radius * 0.62f,
                          ImGui::GetColorU32(Alpha(Metal, 0.75f)));
    draw->AddLine(ImVec2(center.x - radius * 0.62f, center.y + radius * 0.62f),
                  ImVec2(center.x + radius * 0.62f, center.y - radius * 0.62f), ImGui::GetColorU32(Shadow), 1.0f);
}

// The edge of a main panel: a metal frame with a dark seam, and a screw in every corner
inline void DrawPanelFrame(ImDrawList* draw, const ImVec2& min, const ImVec2& max, bool screws, float scale = 1.0f)
{
    draw->AddRect(ImVec2(min.x + 1.0f, min.y + 1.0f), ImVec2(max.x + 1.0f, max.y + 1.0f),
                  ImGui::GetColorU32(Alpha(Shadow, 0.85f)));
    draw->AddRect(min, max, ImGui::GetColorU32(Metal));
    draw->AddRect(ImVec2(min.x + 2.0f, min.y + 2.0f), ImVec2(max.x - 2.0f, max.y - 2.0f), ImGui::GetColorU32(Alpha(Shadow, 0.70f)));

    if (!screws)
        return;

    const float radius = ImFloor(2.5f * scale + 0.5f);
    const float inset = ImFloor(6.0f * scale);

    DrawScrew(draw, ImVec2(min.x + inset, min.y + inset), radius);
    DrawScrew(draw, ImVec2(max.x - inset, min.y + inset), radius);
    DrawScrew(draw, ImVec2(min.x + inset, max.y - inset), radius);
    DrawScrew(draw, ImVec2(max.x - inset, max.y - inset), radius);
}

// Screws of the window being drawn, in the corners of its body. Not where a scrollbar takes the corner.
inline void DrawWindowScrews(float scale = 1.0f)
{
    if (!industrial)
        return;

    ImGuiWindow* window = ImGui::GetCurrentWindow();

    if (window->ScrollbarY || window->ScrollbarX || window->SkipItems)
        return;

    const float top = window->Pos.y + window->TitleBarHeight;
    const float radius = ImFloor(2.5f * scale + 0.5f);
    const float inset = ImFloor(5.0f * scale);

    ImDrawList* draw = window->DrawList;

    draw->PushClipRect(window->Pos, ImVec2(window->Pos.x + window->Size.x, window->Pos.y + window->Size.y), false);
    DrawScrew(draw, ImVec2(window->Pos.x + inset, top + inset), radius);
    DrawScrew(draw, ImVec2(window->Pos.x + window->Size.x - inset, top + inset), radius);
    DrawScrew(draw, ImVec2(window->Pos.x + inset, window->Pos.y + window->Size.y - inset), radius);
    DrawScrew(draw, ImVec2(window->Pos.x + window->Size.x - inset, window->Pos.y + window->Size.y - inset), radius);
    draw->PopClipRect();
}

// A key: a cap seated in a dark metal frame, lit from above. Raised, its lower edge shows its thickness; under
// the pointer the cap brightens; held, it sinks into the frame with the shade of the frame falling on it.
inline void DrawKey(ImDrawList* draw, const ImRect& bounds, const ImVec4& face, bool hovered, bool down)
{
    // Frame and the gap the cap moves in
    draw->AddRectFilled(bounds.Min, bounds.Max, ImGui::GetColorU32(Shadow), 2.0f);
    draw->AddRect(bounds.Min, bounds.Max, ImGui::GetColorU32(hovered ? MetalLight : Metal), 2.0f);
    draw->AddRect(ImVec2(bounds.Min.x + 1.0f, bounds.Min.y + 1.0f), ImVec2(bounds.Max.x - 1.0f, bounds.Max.y - 1.0f),
                  ImGui::GetColorU32(MetalDark), 1.0f);

    const ImVec2 capMin(bounds.Min.x + 3.0f, bounds.Min.y + 3.0f);
    const ImVec2 capMax(bounds.Max.x - 3.0f, bounds.Max.y - 3.0f);

    const ImVec4 lit = hovered && !down ? Shade(face, 1.12f) : face;
    const ImU32 light = ImGui::GetColorU32(Alpha(MetalLight, hovered ? 0.75f : 0.55f));
    const ImU32 shade = ImGui::GetColorU32(Alpha(Shadow, 0.85f));

    if (down)
    {
        // Sunk: darker, the shade of the frame along its upper and left edges
        const ImU32 upper = ImGui::GetColorU32(Shade(lit, 0.70f));
        const ImU32 lower = ImGui::GetColorU32(Shade(lit, 0.95f));

        draw->AddRectFilledMultiColor(capMin, capMax, upper, upper, lower, lower);
        draw->AddRectFilled(capMin, ImVec2(capMax.x, capMin.y + 2.0f), shade);
        draw->AddRectFilled(capMin, ImVec2(capMin.x + 2.0f, capMax.y), shade);
        draw->AddLine(ImVec2(capMin.x, capMax.y - 1.0f), ImVec2(capMax.x, capMax.y - 1.0f), light);
        return;
    }

    // Raised: lighter towards the top, a light edge above and at the left, its thickness below and at the right
    const ImU32 upper = ImGui::GetColorU32(Shade(lit, 1.25f));
    const ImU32 lower = ImGui::GetColorU32(Shade(lit, 0.76f));

    draw->AddRectFilledMultiColor(capMin, capMax, upper, upper, lower, lower);
    draw->AddLine(capMin, ImVec2(capMax.x - 1.0f, capMin.y), light);
    draw->AddLine(capMin, ImVec2(capMin.x, capMax.y - 1.0f), light);
    draw->AddRectFilled(ImVec2(capMin.x, capMax.y - 2.0f), capMax, shade);
    draw->AddLine(ImVec2(capMax.x - 1.0f, capMin.y), ImVec2(capMax.x - 1.0f, capMax.y), shade);
}

// A recess in the panel: dark, with the shade at the top and the light at the bottom
inline void DrawWell(ImDrawList* draw, const ImVec2& min, const ImVec2& max, bool hovered = false)
{
    draw->AddRectFilled(min, max, ImGui::GetColorU32(Alpha(GraphiteDeep, 0.92f)), 1.0f);
    draw->AddLine(min, ImVec2(max.x - 1.0f, min.y), ImGui::GetColorU32(Shadow));
    draw->AddLine(min, ImVec2(min.x, max.y - 1.0f), ImGui::GetColorU32(Shadow));
    draw->AddLine(ImVec2(min.x, max.y - 1.0f), ImVec2(max.x, max.y - 1.0f), ImGui::GetColorU32(hovered ? MetalLight : Metal));
    draw->AddLine(ImVec2(max.x - 1.0f, min.y), ImVec2(max.x - 1.0f, max.y), ImGui::GetColorU32(hovered ? Metal : MetalDark));
}

// --- Controls ------------------------------------------------------------------------------------------------
// Each one takes and returns what the ImGui control it stands in for does, with the same id.

// On/off option: a pilot lamp in a recessed plate with the name of the option. The lamp is the state.
inline bool Switch(const char* label, bool* value)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();

    if (window->SkipItems)
        return false;

    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;
    const ImGuiID id = window->GetID(label);
    const ImVec2 labelSize = ImGui::CalcTextSize(label, nullptr, true);

    const float height = ImGui::GetFrameHeight();
    const ImVec2 pos = window->DC.CursorPos;
    const float pad = ImFloor(height * 0.22f);
    const float width = height + (labelSize.x > 0.0f ? style.ItemInnerSpacing.x + labelSize.x + pad * 2.0f : 0.0f);

    const ImRect bounds(pos, ImVec2(pos.x + width, pos.y + height));

    ImGui::ItemSize(bounds, style.FramePadding.y);

    if (!ImGui::ItemAdd(bounds, id))
        return false;

    bool hovered = false, held = false;
    const bool pressed = ImGui::ButtonBehavior(bounds, id, &hovered, &held);

    if (pressed)
    {
        *value = !*value;
        ImGui::MarkItemEdited(id);
    }

    ImGui::RenderNavCursor(bounds, id);

    if (labelSize.x > 0.0f)
        DrawWell(window->DrawList, bounds.Min, bounds.Max, hovered);

    DrawLamp(window->DrawList, ImVec2(ImFloor(pos.x + height * 0.5f), ImFloor(pos.y + height * 0.5f)),
             ImFloor(height * 0.5f) - 2.0f, *value, hovered, held);

    if (labelSize.x > 0.0f)
        ImGui::RenderText(ImVec2(pos.x + height + style.ItemInnerSpacing.x, pos.y + style.FramePadding.y), label);

    return pressed;
}

// The on/off control of the theme in use
inline bool Checkbox(const char* label, bool* value)
{
    return industrial ? Switch(label, value) : ImGui::Checkbox(label, value);
}

// The button of the theme in use. Same size as ImGui::Button, the colours pushed for a button apply to its cap.
inline bool Button(const char* label, const ImVec2& sizeArg = ImVec2(0.0f, 0.0f))
{
    if (!industrial)
        return ImGui::Button(label, sizeArg);

    ImGuiWindow* window = ImGui::GetCurrentWindow();

    if (window->SkipItems)
        return false;

    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;
    const ImGuiID id = window->GetID(label);
    const ImVec2 labelSize = ImGui::CalcTextSize(label, nullptr, true);

    const ImVec2 pos = window->DC.CursorPos;
    const ImVec2 size = ImGui::CalcItemSize(sizeArg, labelSize.x + style.FramePadding.x * 2.0f,
                                            labelSize.y + style.FramePadding.y * 2.0f);
    const ImRect bounds(pos, ImVec2(pos.x + size.x, pos.y + size.y));

    ImGui::ItemSize(size, style.FramePadding.y);

    if (!ImGui::ItemAdd(bounds, id))
        return false;

    bool hovered = false, held = false;
    const bool pressed = ImGui::ButtonBehavior(bounds, id, &hovered, &held);
    const bool down = held && hovered;

    ImGui::RenderNavCursor(bounds, id);

    DrawKey(window->DrawList, bounds,
            ImGui::GetStyleColorVec4(down ? ImGuiCol_ButtonActive : hovered ? ImGuiCol_ButtonHovered : ImGuiCol_Button),
            hovered, down);

    const float sink = down ? 1.0f : 0.0f;

    ImGui::RenderTextClipped(ImVec2(bounds.Min.x + style.FramePadding.x + sink, bounds.Min.y + style.FramePadding.y + sink),
                             ImVec2(bounds.Max.x - style.FramePadding.x + sink, bounds.Max.y - style.FramePadding.y + sink),
                             label, nullptr, &labelSize, style.ButtonTextAlign, &bounds);

    return pressed;
}

// Width a main key takes, to know whether it still fits on the line
inline float LampButtonWidth(const char* label)
{
    const ImGuiStyle& style = ImGui::GetStyle();

    if (!industrial)
        return ImGui::CalcTextSize(label, nullptr, true).x + style.FramePadding.x * 2.0f;

    const float height = ImFloor(ImGui::GetFrameHeight() * 1.45f);

    return height + ImGui::CalcTextSize(Upper(label).c_str()).x + style.FramePadding.x * 2.0f + style.ItemInnerSpacing.x;
}

// A main key of a module: taller, its name in upper case and a pilot lamp in the colour of what it does. The
// lamp is lit while the key can be pressed and out while it is disabled. The plain button in the classic theme.
inline bool LampButton(const char* label, Lamp kind = Lamp::Neutral)
{
    if (!industrial)
        return ImGui::Button(label);

    ImGuiWindow* window = ImGui::GetCurrentWindow();

    if (window->SkipItems)
        return false;

    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;
    const ImGuiID id = window->GetID(label);
    const std::string text = Upper(label);
    const ImVec2 textSize = ImGui::CalcTextSize(text.c_str());

    const float height = ImFloor(ImGui::GetFrameHeight() * 1.45f);
    const ImVec2 pos = window->DC.CursorPos;
    const ImVec2 size(LampButtonWidth(label), height);
    const ImRect bounds(pos, ImVec2(pos.x + size.x, pos.y + size.y));

    ImGui::ItemSize(size, (height - textSize.y) * 0.5f);

    if (!ImGui::ItemAdd(bounds, id))
        return false;

    const bool available = (g.LastItemData.ItemFlags & ImGuiItemFlags_Disabled) == 0;

    bool hovered = false, held = false;
    const bool pressed = ImGui::ButtonBehavior(bounds, id, &hovered, &held);
    const bool down = held && hovered;

    ImGui::RenderNavCursor(bounds, id);

    DrawKey(window->DrawList, bounds,
            ImGui::GetStyleColorVec4(down ? ImGuiCol_ButtonActive : hovered ? ImGuiCol_ButtonHovered : ImGuiCol_Button),
            hovered, down);

    const float sink = down ? 1.0f : 0.0f;

    DrawLamp(window->DrawList, ImVec2(ImFloor(pos.x + height * 0.5f) + sink, ImFloor(pos.y + height * 0.5f) + sink),
             ImFloor(height * 0.5f) - 5.0f, available, hovered, down, kind);

    window->DrawList->AddText(ImVec2(pos.x + height + sink, pos.y + (height - textSize.y) * 0.5f + sink),
                              ImGui::GetColorU32(ImGuiCol_Text), text.c_str());

    return pressed;
}

// A key of something that can't be undone: red, with a red lamp. The plain button in the classic theme.
inline bool DangerButton(const char* label)
{
    if (!industrial)
        return ImGui::Button(label);

    ImGui::PushStyleColor(ImGuiCol_Button, RedDim);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, RedLit);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, LampRed);

    const bool pressed = LampButton(label, Lamp::Red);

    ImGui::PopStyleColor(3);

    return pressed;
}

// Stays on the line when the next main key fits in what is left of it, starts a new line when it doesn't.
// In the classic theme it is ImGui::SameLine and nothing else.
inline void SameLineIfFits(const char* nextLabel, float spacing = -1.0f)
{
    if (!industrial)
    {
        ImGui::SameLine(0.0f, spacing);
        return;
    }

    const ImGuiWindow* window = ImGui::GetCurrentWindow();
    const float gap = spacing < 0.0f ? ImGui::GetStyle().ItemSpacing.x : spacing;

    if (window->DC.CursorPosPrevLine.x + gap + LampButtonWidth(nextLabel) <= window->WorkRect.Max.x)
        ImGui::SameLine(0.0f, spacing);
}

// Rail and cursor of a slider between min and max: a groove with a scale of marks, lit up to where the value is,
// and a metal cursor with a grip that rides on it. fraction is where the value is between its limits.
inline void DrawSliderRail(ImDrawList* draw, const ImVec2& min, const ImVec2& max, float fraction, float grabSize, bool hovered,
                           bool active)
{
    const ImGuiStyle& style = ImGui::GetStyle();

    fraction = ImClamp(fraction, 0.0f, 1.0f);
    grabSize = ImClamp(grabSize, style.GrabMinSize, ImMax(style.GrabMinSize, max.x - min.x - 4.0f));

    const float center = ImFloor(ImLerp(min.x + 2.0f + grabSize * 0.5f, max.x - 2.0f - grabSize * 0.5f, fraction));
    const float middle = ImFloor((min.y + max.y) * 0.5f);
    const float groove = ImMax(2.0f, ImFloor((max.y - min.y) * 0.13f));
    const float left = min.x + 2.0f + grabSize * 0.5f;
    const float right = max.x - 2.0f - grabSize * 0.5f;

    // Scale: a mark every tenth of the travel, longer at the ends and in the middle
    for (int i = 0; i <= 10; i++)
    {
        const float x = ImFloor(ImLerp(left, right, i / 10.0f));
        const float length = i % 5 == 0 ? groove + 4.0f : groove + 2.0f;

        draw->AddLine(ImVec2(x, middle - length), ImVec2(x, middle - groove - 1.0f), ImGui::GetColorU32(Alpha(Metal, 0.75f)));
    }

    // Groove, with the shade at its upper edge and the light at the lower one
    draw->AddRectFilled(ImVec2(min.x + 2.0f, middle - groove), ImVec2(max.x - 2.0f, middle + groove), ImGui::GetColorU32(Shadow), 1.0f);
    draw->AddLine(ImVec2(min.x + 2.0f, middle + groove), ImVec2(max.x - 2.0f, middle + groove),
                  ImGui::GetColorU32(Alpha(Metal, hovered ? 0.95f : 0.65f)));
    draw->AddRectFilled(ImVec2(min.x + 3.0f, middle - groove + 1.0f), ImVec2(center, middle + groove - 1.0f),
                        ImGui::GetColorU32(active ? LampGreen : LampGreenDim));

    // Cursor
    const ImVec2 cursorMin(center - grabSize * 0.5f, min.y + 1.0f);
    const ImVec2 cursorMax(center + grabSize * 0.5f, max.y - 1.0f);
    const ImVec4 body = active ? MetalLight : hovered ? Shade(Metal, 1.15f) : Metal;

    draw->AddRectFilled(ImVec2(cursorMin.x + 1.0f, cursorMin.y + 2.0f), ImVec2(cursorMax.x + 1.0f, cursorMax.y + 1.0f),
                        ImGui::GetColorU32(Alpha(Shadow, 0.70f)), 1.0f);
    draw->AddRectFilledMultiColor(cursorMin, cursorMax, ImGui::GetColorU32(Shade(body, 1.25f)), ImGui::GetColorU32(Shade(body, 1.25f)),
                                  ImGui::GetColorU32(Shade(body, 0.70f)), ImGui::GetColorU32(Shade(body, 0.70f)));
    draw->AddRect(cursorMin, cursorMax, ImGui::GetColorU32(Shadow), 1.0f);
    draw->AddLine(ImVec2(cursorMin.x + 1.0f, cursorMin.y + 1.0f), ImVec2(cursorMax.x - 2.0f, cursorMin.y + 1.0f),
                  ImGui::GetColorU32(Alpha(MetalLight, 0.90f)));

    // Grip, and the line that marks the value
    draw->AddLine(ImVec2(center, cursorMin.y + 2.0f), ImVec2(center, cursorMax.y - 2.0f), ImGui::GetColorU32(Shadow));

    if (grabSize >= 10.0f)
    {
        draw->AddLine(ImVec2(center - 3.0f, cursorMin.y + 4.0f), ImVec2(center - 3.0f, cursorMax.y - 4.0f),
                      ImGui::GetColorU32(Alpha(Shadow, 0.55f)));
        draw->AddLine(ImVec2(center + 3.0f, cursorMin.y + 4.0f), ImVec2(center + 3.0f, cursorMax.y - 4.0f),
                      ImGui::GetColorU32(Alpha(Shadow, 0.55f)));
    }
}

// A slider as a physical control: rail and cursor at the left, the value in a small display of its own next to
// them, then the name. The ImGui slider passed in keeps doing all the work over the rail, with what it would draw
// made invisible, so dragging, the keyboard, typing a value in and what it returns are its own. The whole control
// takes the same room on its line as the stock one.
template <typename SliderCall>
inline bool PhysicalSlider(const char* label, float fraction, float stepCount, const char* valueText, SliderCall slider)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    ImGuiContext& g = *GImGui;
    const ImGuiID id = window->GetID(label);

    // While a value is being typed in, the field of ImGui is what has to be seen
    if (window->SkipItems || ImGui::TempInputIsActive(id))
        return slider();

    const ImGuiStyle& style = g.Style;
    const float width = ImGui::CalcItemWidth();
    const float height = ImGui::GetFrameHeight();
    const ImVec2 valueSize = ImGui::CalcTextSize(valueText);
    const float display = ImFloor(ImClamp(valueSize.x + style.FramePadding.x * 2.0f, height * 2.4f, width * 0.5f));
    const float rail = width - display - style.ItemInnerSpacing.x;

    // No room for the parts: the stock slider
    if (rail < height * 2.0f)
        return slider();

    const bool active = g.ActiveId == id;
    const bool hovered = active || g.HoveredIdPreviousFrame == id;

    const ImVec2 min = window->DC.CursorPos;
    const ImVec2 railMax(min.x + rail, min.y + height);
    const ImVec2 displayMin(railMax.x + style.ItemInnerSpacing.x, min.y);
    const ImVec2 displayMax(displayMin.x + display, min.y + height);

    const float grab = stepCount > 0.0f ? (rail - 4.0f) / stepCount : style.GrabMinSize;

    DrawSliderRail(window->DrawList, min, railMax, fraction, grab, hovered, active);

    // Display of the value
    DrawWell(window->DrawList, displayMin, displayMax, hovered);
    window->DrawList->PushClipRect(displayMin, displayMax, true);
    window->DrawList->AddText(ImVec2(ImFloor(displayMin.x + (display - valueSize.x) * 0.5f), min.y + style.FramePadding.y),
                              ImGui::GetColorU32(LampGreen), valueText);
    window->DrawList->PopClipRect();

    // Name, where the stock slider writes it
    const char* labelEnd = ImGui::FindRenderedTextEnd(label);

    if (labelEnd != label)
    {
        window->DrawList->AddText(ImVec2(displayMax.x + style.ItemInnerSpacing.x, min.y + style.FramePadding.y),
                                  ImGui::GetColorU32(ImGuiCol_Text), label, labelEnd);
    }

    bool changed = false;

    {
        const ImVec4 none(0.0f, 0.0f, 0.0f, 0.0f);

        for (const ImGuiCol color : { ImGuiCol_FrameBg, ImGuiCol_FrameBgHovered, ImGuiCol_FrameBgActive, ImGuiCol_SliderGrab,
                                      ImGuiCol_SliderGrabActive, ImGuiCol_Border, ImGuiCol_BorderShadow, ImGuiCol_Text })
            ImGui::PushStyleColor(color, none);

        ImGui::SetNextItemWidth(rail);
        changed = slider();

        ImGui::PopStyleColor(8);
    }

    // The display takes room on the line the stock slider doesn't know about. The slider stays the last item,
    // tooltips and whatever asks about it afterwards see it and not the filler.
    const ImGuiLastItemData item = g.LastItemData;

    ImGui::SameLine(0.0f, 0.0f);
    ImGui::Dummy(ImVec2(display + style.ItemInnerSpacing.x, height));

    g.LastItemData = item;

    return changed;
}

inline bool SliderFloat(const char* label, float* value, float min, float max, const char* format = "%.3f",
                        ImGuiSliderFlags flags = 0)
{
    // A logarithmic scale puts the cursor somewhere else, that one keeps the stock look
    if (!industrial || max <= min || (flags & ImGuiSliderFlags_Logarithmic) != 0)
        return ImGui::SliderFloat(label, value, min, max, format, flags);

    char text[64];
    ImFormatString(text, IM_ARRAYSIZE(text), format != nullptr ? format : "%.3f", *value);

    return PhysicalSlider(label, (*value - min) / (max - min), 0.0f, text,
                          [&]() { return ImGui::SliderFloat(label, value, min, max, format, flags); });
}

inline bool SliderInt(const char* label, int* value, int min, int max, const char* format = "%d", ImGuiSliderFlags flags = 0)
{
    if (!industrial || max <= min || (flags & ImGuiSliderFlags_Logarithmic) != 0)
        return ImGui::SliderInt(label, value, min, max, format, flags);

    char text[64];
    ImFormatString(text, IM_ARRAYSIZE(text), format != nullptr ? format : "%d", *value);

    // Whole numbers get a cursor as wide as one step, as ImGui does
    return PhysicalSlider(label, (float) (*value - min) / (float) (max - min), (float) (max - min + 1), text,
                          [&]() { return ImGui::SliderInt(label, value, min, max, format, flags); });
}

// The drop-down as a selector set into the panel: a recessed window with what is selected in green and a key
// with the arrow next to it. ImGui's own combo does the work with its field and its button made invisible; what
// it would write as the selected value is written here instead, the list it opens is its own.
inline bool BeginCombo(const char* label, const char* preview, ImGuiComboFlags flags = 0)
{
    if (!industrial)
        return ImGui::BeginCombo(label, preview, flags);

    ImGuiWindow* window = ImGui::GetCurrentWindow();

    // Variants without the field or the button keep the stock parts, in the colours of the theme
    if (window->SkipItems || flags != 0 || preview == nullptr)
    {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.10f, 0.42f, 0.08f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.14f, 0.60f, 0.10f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, LampGreenDim);

        const bool open = ImGui::BeginCombo(label, preview, flags);

        ImGui::PopStyleColor(3);
        return open;
    }

    const ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;
    const ImGuiID id = window->GetID(label);
    const bool hovered = g.HoveredIdPreviousFrame == id;

    const float height = ImGui::GetFrameHeight();
    const float width = ImGui::CalcItemWidth();
    const ImVec2 min = window->DC.CursorPos;
    const ImVec2 fieldMax(min.x + width - height, min.y + height);

    DrawWell(window->DrawList, min, fieldMax, hovered);

    window->DrawList->PushClipRect(ImVec2(min.x + 2.0f, min.y), ImVec2(fieldMax.x - 2.0f, fieldMax.y), true);
    window->DrawList->AddText(ImVec2(min.x + style.FramePadding.x, min.y + style.FramePadding.y), ImGui::GetColorU32(LampGreen),
                              preview);
    window->DrawList->PopClipRect();

    DrawKey(window->DrawList, ImRect(ImVec2(fieldMax.x, min.y), ImVec2(min.x + width, min.y + height)),
            hovered ? ImVec4(0.14f, 0.55f, 0.10f, 1.0f) : ImVec4(0.10f, 0.40f, 0.08f, 1.0f), hovered, false);

    const ImVec4 none(0.0f, 0.0f, 0.0f, 0.0f);

    for (const ImGuiCol color : { ImGuiCol_FrameBg, ImGuiCol_FrameBgHovered, ImGuiCol_FrameBgActive, ImGuiCol_Button,
                                  ImGuiCol_ButtonHovered, ImGuiCol_ButtonActive })
        ImGui::PushStyleColor(color, none);

    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);

    const bool open = ImGui::BeginCombo(label, "", flags);

    ImGui::PopStyleVar();
    ImGui::PopStyleColor(6);

    return open;
}

// One of several options of which one is chosen: a lamp without a plate, lit on the chosen one. Same arguments,
// id and result as ImGui::RadioButton.
inline bool RadioButton(const char* label, bool chosen)
{
    if (!industrial)
        return ImGui::RadioButton(label, chosen);

    ImGuiWindow* window = ImGui::GetCurrentWindow();

    if (window->SkipItems)
        return false;

    const ImGuiStyle& style = ImGui::GetStyle();
    const ImGuiID id = window->GetID(label);
    const ImVec2 labelSize = ImGui::CalcTextSize(label, nullptr, true);

    const float height = ImGui::GetFrameHeight();
    const ImVec2 pos = window->DC.CursorPos;
    const ImRect bounds(pos, ImVec2(pos.x + height + (labelSize.x > 0.0f ? style.ItemInnerSpacing.x + labelSize.x : 0.0f),
                                    pos.y + labelSize.y + style.FramePadding.y * 2.0f));

    ImGui::ItemSize(bounds, style.FramePadding.y);

    if (!ImGui::ItemAdd(bounds, id))
        return false;

    bool hovered = false, held = false;
    const bool pressed = ImGui::ButtonBehavior(bounds, id, &hovered, &held);

    if (pressed)
        ImGui::MarkItemEdited(id);

    ImGui::RenderNavCursor(bounds, id);

    DrawLamp(window->DrawList, ImVec2(ImFloor(pos.x + height * 0.5f), ImFloor(pos.y + height * 0.5f)),
             ImFloor(height * 0.5f) - 2.0f, chosen, hovered, held, Lamp::Amber);

    if (labelSize.x > 0.0f)
        ImGui::RenderText(ImVec2(pos.x + height + style.ItemInnerSpacing.x, pos.y + style.FramePadding.y), label);

    return pressed;
}

inline bool RadioButton(const char* label, int* value, int option)
{
    const bool pressed = RadioButton(label, *value == option);

    if (pressed)
        *value = option;

    return pressed;
}

// A section that folds: its header is a key across the panel. ImGui's header does the work, with its own
// background made invisible over the key.
inline bool CollapsingHeader(const char* label, ImGuiTreeNodeFlags flags = 0)
{
    if (!industrial)
        return ImGui::CollapsingHeader(label, flags);

    ImGuiWindow* window = ImGui::GetCurrentWindow();

    if (!window->SkipItems)
    {
        const ImGuiContext& g = *GImGui;
        const ImGuiID id = window->GetID(label);
        const bool hovered = g.HoveredIdPreviousFrame == id;
        const float extend = IM_TRUNC(window->WindowPadding.x * 0.5f);
        const float height = g.FontSize + g.Style.FramePadding.y * 2.0f;

        DrawKey(window->DrawList,
                ImRect(ImVec2(window->WorkRect.Min.x - extend, window->DC.CursorPos.y),
                       ImVec2(window->WorkRect.Max.x + extend, window->DC.CursorPos.y + height)),
                hovered ? Olive : OliveDim, hovered, g.ActiveId == id);
    }

    const ImVec4 none(0.0f, 0.0f, 0.0f, 0.0f);

    ImGui::PushStyleColor(ImGuiCol_Header, none);
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, none);
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, none);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);

    const bool open = ImGui::CollapsingHeader(label, flags);

    ImGui::PopStyleVar();
    ImGui::PopStyleColor(3);

    return open;
}

// Name of a part of a panel: an engraved tag with a metal rule running from it to the edge. Takes the place of
// ImGui::SeparatorText, tooltips attach to it the same way.
inline void SectionPlate(const char* text)
{
    if (!industrial)
    {
        ImGui::SeparatorText(text);
        return;
    }

    ImGuiWindow* window = ImGui::GetCurrentWindow();

    if (window->SkipItems)
        return;

    const ImGuiStyle& style = ImGui::GetStyle();
    const char* textEnd = ImGui::FindRenderedTextEnd(text);
    const ImVec2 textSize = ImGui::CalcTextSize(text, textEnd);

    ImGui::Dummy(ImVec2(0.0f, ImFloor(style.ItemSpacing.y * 0.5f)));

    const ImVec2 pos = window->DC.CursorPos;
    const float pad = style.FramePadding.x + 2.0f;
    const float height = textSize.y + 6.0f;
    const ImVec2 tagMax(pos.x + textSize.x + pad * 2.0f, pos.y + height);
    const float right = window->WorkRect.Max.x;

    ImDrawList* draw = window->DrawList;

    // Tag: a small raised plate
    draw->AddRectFilledMultiColor(pos, tagMax, ImGui::GetColorU32(Shade(Olive, 1.10f)), ImGui::GetColorU32(Shade(Olive, 1.10f)),
                                  ImGui::GetColorU32(OliveDim), ImGui::GetColorU32(OliveDim));
    draw->AddRect(pos, tagMax, ImGui::GetColorU32(Shadow));
    draw->AddLine(ImVec2(pos.x + 1.0f, pos.y + 1.0f), ImVec2(tagMax.x - 2.0f, pos.y + 1.0f),
                  ImGui::GetColorU32(Alpha(MetalLight, 0.50f)));
    draw->AddText(ImVec2(pos.x + pad, pos.y + 3.0f), ImGui::GetColorU32(TitleAmber), text, textEnd);

    // Rule: a groove in the panel
    const float y = ImFloor(pos.y + height * 0.5f);

    if (right > tagMax.x + 4.0f)
    {
        draw->AddLine(ImVec2(tagMax.x + 4.0f, y), ImVec2(right, y), ImGui::GetColorU32(Shadow));
        draw->AddLine(ImVec2(tagMax.x + 4.0f, y + 1.0f), ImVec2(right, y + 1.0f), ImGui::GetColorU32(Alpha(Metal, 0.85f)));
    }

    ImGui::Dummy(ImVec2(ImMax(1.0f, right - pos.x), height));
}

// --- Worn metal ----------------------------------------------------------------------------------------------
// A small tile of grain, brushing, stains and scratches made here from numbers, no image file. It is laid over
// plates with little opacity: lighter and darker than the paint under it, never a colour of its own. The renderer
// of the menu makes the texture from these pixels the way it does with the font.

inline ImTextureData* metalTile = nullptr;
inline constexpr int MetalTileSize = 128;

inline unsigned MetalHash(unsigned x, unsigned y, unsigned seed)
{
    unsigned h = x * 374761393u + y * 668265263u + seed * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

// Smooth noise that repeats every `period` pixels, so the tile has no seams. -1 to 1.
inline float MetalNoise(float x, float y, int cell, int period, unsigned seed)
{
    const int cells = period / cell;
    const float fx = x / cell, fy = y / cell;
    const int x0 = (int) fx, y0 = (int) fy;
    const float tx = fx - x0, ty = fy - y0;
    const float sx = tx * tx * (3.0f - 2.0f * tx), sy = ty * ty * (3.0f - 2.0f * ty);

    const auto value = [&](int cx, int cy)
    { return (MetalHash((unsigned) (cx % cells), (unsigned) (cy % cells), seed) & 0xFFFF) / 32767.5f - 1.0f; };

    const float top = value(x0, y0) + (value(x0 + 1, y0) - value(x0, y0)) * sx;
    const float bottom = value(x0, y0 + 1) + (value(x0 + 1, y0 + 1) - value(x0, y0 + 1)) * sx;

    return top + (bottom - top) * sy;
}

inline void FillMetalTile(unsigned char* pixels)
{
    const int size = MetalTileSize;

    for (int y = 0; y < size; y++)
    {
        for (int x = 0; x < size; x++)
        {
            // Grain of the paint, brushing along the plate, and stains of wear
            float v = ((MetalHash((unsigned) x, (unsigned) y, 1) & 0xFF) / 127.5f - 1.0f) * 7.0f;

            {
                const float t = (x % 32) / 32.0f, s = t * t * (3.0f - 2.0f * t);
                const float a = (MetalHash((unsigned) (x / 32 % 4), (unsigned) y, 2) & 0xFFFF) / 32767.5f - 1.0f;
                const float b = (MetalHash((unsigned) ((x / 32 + 1) % 4), (unsigned) y, 2) & 0xFFFF) / 32767.5f - 1.0f;

                v += (a + (b - a) * s) * 9.0f;
            }
            v += MetalNoise((float) x, (float) y, 32, size, 3) * 16.0f;
            v += MetalNoise((float) x, (float) y, 8, size, 4) * 6.0f;

            unsigned char* p = pixels + (y * size + x) * 4;

            p[0] = p[1] = p[2] = v > 0.0f ? 255 : 0;
            p[3] = (unsigned char) ImClamp(ImAbs(v) * 5.0f, 0.0f, 255.0f);
        }
    }

    // A few fine scratches down to the bare metal, at a slight slant, wrapping around the tile
    for (int s = 0; s < 4; s++)
    {
        const unsigned h = MetalHash((unsigned) s, 17u, 5);
        const int length = 18 + (int) (h % 46);
        const float slope = ((int) ((h >> 8) % 9) - 4) / 14.0f;
        int x = (int) ((h >> 12) % size);
        float y = (float) ((h >> 20) % size);

        for (int i = 0; i < length; i++)
        {
            unsigned char* p = pixels + ((((int) y) % size + size) % size * size + (x + i) % size) * 4;
            const float fade = 1.0f - ImAbs(i - length * 0.5f) / (length * 0.5f);

            p[0] = p[1] = p[2] = 255;
            p[3] = (unsigned char) ImMax((float) p[3], 110.0f * fade);
            y += slope;
        }
    }
}

// Keeps the tile alive in the menu being drawn and tells whether it can be used in this frame
inline bool MetalTileReady()
{
    ImGuiContext* context = ImGui::GetCurrentContext();

    if (context == nullptr || (context->IO.BackendFlags & ImGuiBackendFlags_RendererHasTextures) == 0)
        return false;

    if (metalTile == nullptr)
    {
        metalTile = IM_NEW(ImTextureData)();
        metalTile->Create(ImTextureFormat_RGBA32, MetalTileSize, MetalTileSize);
        metalTile->UseColors = true;
        metalTile->RefCount = 1;
        metalTile->UsedRect = { 0, 0, (unsigned short) MetalTileSize, (unsigned short) MetalTileSize };
        FillMetalTile(metalTile->Pixels);
        metalTile->SetStatus(ImTextureStatus_WantCreate);
    }

    if (!context->UserTextures.contains(metalTile))
        ImGui::RegisterUserTexture(metalTile);

    // The renderer rebuilt its objects and dropped it, the pixels are still here
    if (metalTile->Status == ImTextureStatus_Destroyed)
        metalTile->SetStatus(ImTextureStatus_WantCreate);

    return metalTile->GetTexID() != ImTextureID_Invalid;
}

// Lays the worn metal over a rectangle, tile next to tile
inline void DrawMetal(ImDrawList* draw, const ImVec2& min, const ImVec2& max, float opacity)
{
    if (!MetalTileReady() || max.x <= min.x || max.y <= min.y)
        return;

    const ImU32 tint = ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, opacity));
    const float size = (float) MetalTileSize;

    // Tiles are counted from a fixed grid, the wear doesn't slide when a panel moves by a few pixels
    const float startX = ImFloor(min.x / size) * size;
    const float startY = ImFloor(min.y / size) * size;

    for (float y = startY; y < max.y; y += size)
    {
        for (float x = startX; x < max.x; x += size)
        {
            const ImVec2 a(ImMax(x, min.x), ImMax(y, min.y));
            const ImVec2 b(ImMin(x + size, max.x), ImMin(y + size, max.y));

            draw->AddImage(metalTile->GetTexRef(), a, b, ImVec2((a.x - x) / size, (a.y - y) / size),
                           ImVec2((b.x - x) / size, (b.y - y) / size), tint);
        }
    }
}

// --- Chassis and plates --------------------------------------------------------------------------------------

// A reinforcement plate in a corner of the chassis, with its screw. cornerX/cornerY say which way the corner of
// the chassis is from the plate (-1 left or up, 1 right or down).
inline void DrawCornerPlate(ImDrawList* draw, const ImVec2& corner, float size, int cornerX, int cornerY)
{
    const float cut = ImFloor(size * 0.38f);
    const float dx = (float) -cornerX, dy = (float) -cornerY;

    // From the corner of the chassis along both edges, with the inner corner cut off
    const ImVec2 points[5] = { corner,
                               ImVec2(corner.x + dx * size, corner.y),
                               ImVec2(corner.x + dx * size, corner.y + dy * (size - cut)),
                               ImVec2(corner.x + dx * (size - cut), corner.y + dy * size),
                               ImVec2(corner.x, corner.y + dy * size) };

    draw->AddConvexPolyFilled(points, 5, ImGui::GetColorU32(Shade(MetalDark, 1.20f)));
    draw->AddPolyline(points, 5, ImGui::GetColorU32(Shadow), ImDrawFlags_Closed, 1.0f);

    // Light on the edge that faces up, whichever corner this is
    const ImVec2 lightFrom = cornerY < 0 ? ImVec2(corner.x + dx * 1.0f, corner.y + 1.0f) : points[3];
    const ImVec2 lightTo = cornerY < 0 ? ImVec2(corner.x + dx * (size - 1.0f), corner.y + 1.0f) : points[4];

    draw->AddLine(lightFrom, lightTo, ImGui::GetColorU32(Alpha(MetalLight, cornerY < 0 ? 0.55f : 0.30f)));

    DrawScrew(draw, ImVec2(ImFloor(corner.x + dx * size * 0.42f), ImFloor(corner.y + dy * size * 0.42f)),
              ImMax(2.0f, ImFloor(size * 0.16f)));
}

// The window being drawn as a metal chassis: worn paint over all of it, a thick bevelled rim with a groove on
// its inner side, a joint under the title, and a reinforcement plate with its screw in every corner. Everything
// is drawn inside the margin of the window, behind what the window holds; nothing here takes a click.
inline void DrawChassis()
{
    if (!industrial)
        return;

    ImGuiWindow* window = ImGui::GetCurrentWindow();

    if (window->SkipItems)
        return;

    const ImGuiStyle& style = ImGui::GetStyle();
    const ImVec2 min = window->Pos;
    const ImVec2 max(window->Pos.x + window->Size.x, window->Pos.y + window->Size.y);
    const float rim = ImMax(4.0f, ImFloor(style.WindowPadding.x * 0.70f));
    const float title = window->TitleBarHeight;

    ImDrawList* draw = window->DrawList;

    draw->PushClipRect(min, max, false);

    // Paint of the cabinet
    DrawMetal(draw, ImVec2(min.x, min.y + title), max, 0.11f);

    // Title: a name plate between the upper corners
    if (title > 0.0f)
    {
        DrawMetal(draw, min, ImVec2(max.x, min.y + title), 0.18f);
        draw->AddLine(ImVec2(min.x, min.y + title - 1.0f), ImVec2(max.x, min.y + title - 1.0f), ImGui::GetColorU32(Shadow));
        draw->AddLine(ImVec2(min.x, min.y + title), ImVec2(max.x, min.y + title), ImGui::GetColorU32(Alpha(MetalLight, 0.35f)));
    }

    // Rim: four faces of a bevel, the upper and left ones catching the light
    const ImU32 faceTop = ImGui::GetColorU32(Shade(MetalDark, 1.45f));
    const ImU32 faceLeft = ImGui::GetColorU32(Shade(MetalDark, 1.20f));
    const ImU32 faceRight = ImGui::GetColorU32(Shade(MetalDark, 0.80f));
    const ImU32 faceBottom = ImGui::GetColorU32(Shade(MetalDark, 0.62f));

    // Over the title the rim is only its edge, the name has to stay readable
    const ImVec2 innerMin(min.x + rim, min.y + (title > 0.0f ? ImMin(rim, 2.0f) : rim)), innerMax(max.x - rim, max.y - rim);

    const ImVec2 top[4] = { min, ImVec2(max.x, min.y), ImVec2(innerMax.x, innerMin.y), innerMin };
    const ImVec2 left[4] = { min, innerMin, ImVec2(innerMin.x, innerMax.y), ImVec2(min.x, max.y) };
    const ImVec2 right[4] = { ImVec2(max.x, min.y), max, innerMax, ImVec2(innerMax.x, innerMin.y) };
    const ImVec2 bottom[4] = { ImVec2(min.x, max.y), ImVec2(innerMin.x, innerMax.y), innerMax, max };

    draw->AddConvexPolyFilled(top, 4, faceTop);
    draw->AddConvexPolyFilled(left, 4, faceLeft);
    draw->AddConvexPolyFilled(right, 4, faceRight);
    draw->AddConvexPolyFilled(bottom, 4, faceBottom);

    DrawMetal(draw, min, ImVec2(max.x, innerMin.y), 0.35f);
    DrawMetal(draw, ImVec2(min.x, innerMax.y), max, 0.35f);
    DrawMetal(draw, ImVec2(min.x, innerMin.y), ImVec2(innerMin.x, innerMax.y), 0.35f);
    DrawMetal(draw, ImVec2(innerMax.x, innerMin.y), ImVec2(max.x, innerMax.y), 0.35f);

    // Outer edge, worn to the metal along the top, and the groove where the rim meets the cabinet
    draw->AddRect(min, max, ImGui::GetColorU32(Shadow));
    draw->AddLine(ImVec2(min.x + 1.0f, min.y + 1.0f), ImVec2(max.x - 2.0f, min.y + 1.0f), ImGui::GetColorU32(Alpha(MetalLight, 0.70f)));
    draw->AddLine(ImVec2(min.x + 1.0f, min.y + 1.0f), ImVec2(min.x + 1.0f, max.y - 2.0f), ImGui::GetColorU32(Alpha(MetalLight, 0.40f)));
    draw->AddRect(innerMin, innerMax, ImGui::GetColorU32(Shadow));
    draw->AddLine(ImVec2(innerMin.x + 1.0f, innerMax.y), ImVec2(innerMax.x, innerMax.y), ImGui::GetColorU32(Alpha(MetalLight, 0.28f)));
    draw->AddLine(ImVec2(innerMax.x, innerMin.y + 1.0f), ImVec2(innerMax.x, innerMax.y), ImGui::GetColorU32(Alpha(MetalLight, 0.20f)));

    // Corners
    // Corners. The upper plates are as high as the title lets them, the lower ones stay inside the margin so
    // they never sit under a control.
    const float upperPlate = title > 0.0f ? ImMin(ImFloor(rim * 3.2f), title - 2.0f) : style.WindowPadding.x;
    const float lowerPlate = style.WindowPadding.x;

    DrawCornerPlate(draw, min, upperPlate, -1, -1);
    DrawCornerPlate(draw, ImVec2(max.x, min.y), upperPlate, 1, -1);
    DrawCornerPlate(draw, ImVec2(min.x, max.y), lowerPlate, -1, 1);
    DrawCornerPlate(draw, max, lowerPlate, 1, 1);

    draw->PopClipRect();
}

// A joint across the chassis at the height where drawing is now: the line where two parts of the cabinet meet.
// Above the bottom band of the menu.
inline void DrawChassisJoint()
{
    if (!industrial)
        return;

    ImGuiWindow* window = ImGui::GetCurrentWindow();
    const ImGuiStyle& style = ImGui::GetStyle();
    const float rim = ImMax(4.0f, ImFloor(style.WindowPadding.x * 0.70f));
    const float y = ImFloor(window->DC.CursorPos.y - style.ItemSpacing.y * 0.5f) - 1.0f;
    const float left = window->Pos.x + rim + 1.0f, right = window->Pos.x + window->Size.x - rim - 1.0f;

    window->DrawList->PushClipRect(window->Pos, ImVec2(window->Pos.x + window->Size.x, window->Pos.y + window->Size.y), false);
    window->DrawList->AddLine(ImVec2(left, y), ImVec2(right, y), ImGui::GetColorU32(Shadow));
    window->DrawList->AddLine(ImVec2(left, y + 1.0f), ImVec2(right, y + 1.0f), ImGui::GetColorU32(Alpha(MetalLight, 0.30f)));
    window->DrawList->PopClipRect();
}

// Body of a card: a steel plate set into the cabinet. A dark seat around it, the plate lighter at the top, worn
// paint, a bevel of two pixels, and a small screw in every corner when the plate is big enough to need them.
inline void DrawCardBody(ImDrawList* draw, const ImVec2& min, const ImVec2& max)
{
    // Seat in the cabinet
    draw->AddRect(ImVec2(min.x - 1.0f, min.y - 1.0f), ImVec2(max.x + 1.0f, max.y + 1.0f), ImGui::GetColorU32(Alpha(Shadow, 0.90f)));
    draw->AddLine(ImVec2(min.x - 1.0f, max.y + 2.0f), ImVec2(max.x + 2.0f, max.y + 2.0f), ImGui::GetColorU32(Alpha(MetalLight, 0.16f)));
    draw->AddLine(ImVec2(max.x + 2.0f, min.y - 1.0f), ImVec2(max.x + 2.0f, max.y + 2.0f), ImGui::GetColorU32(Alpha(MetalLight, 0.10f)));

    // Plate
    const ImU32 upper = ImGui::GetColorU32(Alpha(Shade(Panel, 1.18f), 0.72f));
    const ImU32 lower = ImGui::GetColorU32(Alpha(Shade(Panel, 0.82f), 0.72f));

    draw->AddRectFilledMultiColor(min, max, upper, upper, lower, lower);
    DrawMetal(draw, min, max, 0.12f);

    // Bevel: light on the upper and left edges, shade on the lower and right ones
    draw->AddRect(min, max, ImGui::GetColorU32(Metal));
    draw->AddLine(ImVec2(min.x + 1.0f, min.y + 1.0f), ImVec2(max.x - 2.0f, min.y + 1.0f), ImGui::GetColorU32(Alpha(MetalLight, 0.45f)));
    draw->AddLine(ImVec2(min.x + 1.0f, min.y + 1.0f), ImVec2(min.x + 1.0f, max.y - 2.0f), ImGui::GetColorU32(Alpha(MetalLight, 0.25f)));
    draw->AddLine(ImVec2(min.x + 1.0f, max.y - 2.0f), ImVec2(max.x - 1.0f, max.y - 2.0f), ImGui::GetColorU32(Alpha(Shadow, 0.75f)));
    draw->AddLine(ImVec2(max.x - 2.0f, min.y + 1.0f), ImVec2(max.x - 2.0f, max.y - 1.0f), ImGui::GetColorU32(Alpha(Shadow, 0.60f)));
    draw->AddLine(ImVec2(min.x + 2.0f, max.y - 3.0f), ImVec2(max.x - 2.0f, max.y - 3.0f), ImGui::GetColorU32(Alpha(Shadow, 0.30f)));

    // Screws, in the margin the content doesn't use
    const float pad = ImGui::GetStyle().WindowPadding.x;
    const float radius = ImMax(1.5f, ImFloor(pad * 0.24f));
    const float inset = ImFloor(pad * 0.55f);

    if (max.y - min.y > pad * 9.0f && max.x - min.x > pad * 20.0f)
    {
        DrawScrew(draw, ImVec2(min.x + inset, max.y - inset), radius);
        DrawScrew(draw, ImVec2(max.x - inset, max.y - inset), radius);
        DrawScrew(draw, ImVec2(max.x - inset, min.y + inset), radius);
    }
}

// Name band across the top of a card: a strip of olive paint on the plate, worn, with a groove under it
inline void DrawCardBand(ImDrawList* draw, float left, float top, float right, float bottom)
{
    draw->AddRectFilledMultiColor(ImVec2(left, top), ImVec2(right, bottom), ImGui::GetColorU32(Shade(Olive, 1.12f)),
                                  ImGui::GetColorU32(Shade(Olive, 1.12f)), ImGui::GetColorU32(Shade(OliveDim, 0.95f)),
                                  ImGui::GetColorU32(Shade(OliveDim, 0.95f)));
    DrawMetal(draw, ImVec2(left, top), ImVec2(right, bottom), 0.20f);
    draw->AddLine(ImVec2(left, top), ImVec2(right, top), ImGui::GetColorU32(Alpha(MetalLight, 0.50f)));
    draw->AddLine(ImVec2(left, bottom - 1.0f), ImVec2(right, bottom - 1.0f), ImGui::GetColorU32(Alpha(Shadow, 0.60f)));
    draw->AddLine(ImVec2(left, bottom), ImVec2(right, bottom), ImGui::GetColorU32(Shadow));
    draw->AddLine(ImVec2(left, bottom + 1.0f), ImVec2(right, bottom + 1.0f), ImGui::GetColorU32(Alpha(MetalLight, 0.35f)));
}

// --- Sidebar -------------------------------------------------------------------------------------------------

// Name of a group of tabs: engraved text over a rule, no lamp, nothing to press
inline void GroupPlate(const char* text)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();

    ImGui::TextColored(TitleAmber, "%s", text);

    const float y = ImGui::GetItemRectMax().y + 1.0f;

    window->DrawList->AddLine(ImVec2(ImGui::GetItemRectMin().x, y), ImVec2(window->WorkRect.Max.x, y), ImGui::GetColorU32(Metal));
    window->DrawList->AddLine(ImVec2(ImGui::GetItemRectMin().x, y + 1.0f), ImVec2(window->WorkRect.Max.x, y + 1.0f),
                              ImGui::GetColorU32(Shadow));
    ImGui::Dummy(ImVec2(0.0f, 2.0f));
}

// A tab: a key with its pilot lamp. The open one is lit, with its cap in olive and an amber edge.
inline void DrawTabPlate(ImDrawList* draw, const ImVec2& min, const ImVec2& max, bool selected, bool hovered, bool held,
                         const char* text, float scale)
{
    const ImRect bounds(min, max);

    DrawKey(draw, bounds, selected ? OliveLit : hovered ? Olive : OliveDim, hovered, held);

    if (selected)
        draw->AddRect(min, max, ImGui::GetColorU32(EdgeAmber), 2.0f);

    const float height = max.y - min.y;

    DrawLamp(draw, ImVec2(ImFloor(min.x + height * 0.5f + 1.0f), ImFloor((min.y + max.y) * 0.5f)), ImFloor(height * 0.5f) - 4.0f,
             selected, hovered, held);

    draw->AddText(ImVec2(min.x + height + 4.0f * scale, min.y + (height - ImGui::GetTextLineHeight()) * 0.5f),
                  ImGui::GetColorU32(selected ? Text : hovered ? Text : TextDim), text);
}

// A small key of a selector drawn around its text (language, mode). The chosen one is lit.
inline void DrawSelectorKey(ImDrawList* draw, const ImVec2& textPos, const ImVec2& textSize, bool selected, bool hovered)
{
    const ImVec2 min(textPos.x - 4.0f, textPos.y - 2.0f);
    const ImVec2 max(textPos.x + textSize.x + 4.0f, textPos.y + textSize.y + 2.0f);

    DrawKey(draw, ImRect(min, max), selected ? ImVec4(0.12f, 0.52f, 0.08f, 1.0f) : hovered ? Olive : OliveDim, hovered, false);
}
} // namespace IlionTheme
