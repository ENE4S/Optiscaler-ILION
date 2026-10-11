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

// A key: a cap seated in a dark metal frame, lit from above, that sinks while it is held
inline void DrawKey(ImDrawList* draw, const ImRect& bounds, const ImVec4& face, bool hovered, bool down)
{
    // Frame and the gap the cap moves in
    draw->AddRectFilled(bounds.Min, bounds.Max, ImGui::GetColorU32(Shadow), 2.0f);
    draw->AddRect(bounds.Min, bounds.Max, ImGui::GetColorU32(hovered ? MetalLight : Metal), 2.0f);
    draw->AddRect(ImVec2(bounds.Min.x + 1.0f, bounds.Min.y + 1.0f), ImVec2(bounds.Max.x - 1.0f, bounds.Max.y - 1.0f),
                  ImGui::GetColorU32(MetalDark), 1.0f);

    // Cap, lighter towards the top
    const ImVec2 capMin(bounds.Min.x + 3.0f, bounds.Min.y + 3.0f);
    const ImVec2 capMax(bounds.Max.x - 3.0f, bounds.Max.y - 3.0f);

    const ImU32 upper = ImGui::GetColorU32(Shade(face, down ? 0.80f : 1.22f));
    const ImU32 lower = ImGui::GetColorU32(Shade(face, down ? 1.00f : 0.78f));

    draw->AddRectFilledMultiColor(capMin, capMax, upper, upper, lower, lower);

    const ImU32 light = ImGui::GetColorU32(Alpha(MetalLight, 0.55f));
    const ImU32 shade = ImGui::GetColorU32(Alpha(Shadow, 0.80f));

    draw->AddLine(capMin, ImVec2(capMax.x - 1.0f, capMin.y), down ? shade : light);
    draw->AddLine(capMin, ImVec2(capMin.x, capMax.y - 1.0f), down ? shade : light);
    draw->AddLine(ImVec2(capMin.x, capMax.y - 1.0f), ImVec2(capMax.x, capMax.y - 1.0f), down ? light : shade);
    draw->AddLine(ImVec2(capMax.x - 1.0f, capMin.y), ImVec2(capMax.x - 1.0f, capMax.y), down ? light : shade);
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

// Rail and handle of a slider, drawn under the ImGui slider that keeps doing the work with its own frame and
// grab made invisible. fraction is where the value is between its limits.
inline void DrawSliderParts(const char* label, float fraction, float grabSize)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();

    if (window->SkipItems)
        return;

    const ImGuiContext& g = *GImGui;
    const ImGuiID id = window->GetID(label);
    const bool hovered = g.HoveredIdPreviousFrame == id || g.ActiveId == id;

    const ImVec2 min = window->DC.CursorPos;
    const ImVec2 max(min.x + ImGui::CalcItemWidth(), min.y + ImGui::GetFrameHeight());

    DrawWell(window->DrawList, min, max, hovered);

    fraction = ImClamp(fraction, 0.0f, 1.0f);
    grabSize = ImClamp(grabSize, g.Style.GrabMinSize, ImMax(g.Style.GrabMinSize, max.x - min.x - 4.0f));

    const float center = ImFloor(ImLerp(min.x + 2.0f + grabSize * 0.5f, max.x - 2.0f - grabSize * 0.5f, fraction));
    const float middle = ImFloor((min.y + max.y) * 0.5f);
    const float rail = ImMax(2.0f, ImFloor((max.y - min.y) * 0.14f));

    // Rail, lit up to where the handle is
    window->DrawList->AddRectFilled(ImVec2(min.x + 4.0f, middle - rail), ImVec2(max.x - 4.0f, middle + rail),
                                    ImGui::GetColorU32(Shadow));
    window->DrawList->AddRectFilled(ImVec2(min.x + 4.0f, middle - rail), ImVec2(center, middle + rail),
                                    ImGui::GetColorU32(Alpha(LampGreenDim, 0.85f)));

    // Handle, dark enough for the value written over it to stay readable
    const ImVec2 handleMin(center - grabSize * 0.5f, min.y + 2.0f);
    const ImVec2 handleMax(center + grabSize * 0.5f, max.y - 2.0f);

    window->DrawList->AddRectFilled(handleMin, handleMax, ImGui::GetColorU32(g.ActiveId == id ? Metal : MetalDark), 1.0f);
    window->DrawList->AddLine(handleMin, ImVec2(handleMax.x - 1.0f, handleMin.y), ImGui::GetColorU32(MetalLight));
    window->DrawList->AddLine(handleMin, ImVec2(handleMin.x, handleMax.y - 1.0f), ImGui::GetColorU32(Alpha(MetalLight, 0.7f)));
    window->DrawList->AddLine(ImVec2(handleMin.x, handleMax.y - 1.0f), ImVec2(handleMax.x, handleMax.y - 1.0f),
                              ImGui::GetColorU32(Shadow));
    window->DrawList->AddLine(ImVec2(handleMax.x - 1.0f, handleMin.y), ImVec2(handleMax.x - 1.0f, handleMax.y),
                              ImGui::GetColorU32(Shadow));
    window->DrawList->AddLine(ImVec2(center, handleMin.y + 3.0f), ImVec2(center, handleMax.y - 3.0f),
                              ImGui::GetColorU32(Alpha(Shadow, 0.8f)));
}

struct InvisibleSliderFrame
{
    InvisibleSliderFrame()
    {
        const ImVec4 none(0.0f, 0.0f, 0.0f, 0.0f);

        for (const ImGuiCol color : { ImGuiCol_FrameBg, ImGuiCol_FrameBgHovered, ImGuiCol_FrameBgActive, ImGuiCol_SliderGrab,
                                      ImGuiCol_SliderGrabActive, ImGuiCol_Border, ImGuiCol_BorderShadow })
            ImGui::PushStyleColor(color, none);
    }

    ~InvisibleSliderFrame() { ImGui::PopStyleColor(7); }
};

inline bool SliderFloat(const char* label, float* value, float min, float max, const char* format = "%.3f",
                        ImGuiSliderFlags flags = 0)
{
    // A logarithmic scale puts the handle somewhere else, that one keeps the stock look
    if (!industrial || max <= min || (flags & ImGuiSliderFlags_Logarithmic) != 0)
        return ImGui::SliderFloat(label, value, min, max, format, flags);

    DrawSliderParts(label, (*value - min) / (max - min), ImGui::GetStyle().GrabMinSize);

    InvisibleSliderFrame frame;
    return ImGui::SliderFloat(label, value, min, max, format, flags);
}

inline bool SliderInt(const char* label, int* value, int min, int max, const char* format = "%d", ImGuiSliderFlags flags = 0)
{
    if (!industrial || max <= min || (flags & ImGuiSliderFlags_Logarithmic) != 0)
        return ImGui::SliderInt(label, value, min, max, format, flags);

    // Whole numbers get a handle as wide as one step, as ImGui does
    const float usable = ImGui::CalcItemWidth() - 4.0f;

    DrawSliderParts(label, (float) (*value - min) / (float) (max - min), usable / (float) (max - min + 1));

    InvisibleSliderFrame frame;
    return ImGui::SliderInt(label, value, min, max, format, flags);
}

// The drop-down of the theme in use: the stock one with its field recessed and its arrow on a green key
inline bool BeginCombo(const char* label, const char* preview, ImGuiComboFlags flags = 0)
{
    if (!industrial)
        return ImGui::BeginCombo(label, preview, flags);

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.10f, 0.42f, 0.08f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.14f, 0.60f, 0.10f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, LampGreenDim);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, Alpha(GraphiteDeep, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_BorderShadow, Alpha(Shadow, 0.0f));

    const bool open = ImGui::BeginCombo(label, preview, flags);

    ImGui::PopStyleColor(5);

    return open;
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
