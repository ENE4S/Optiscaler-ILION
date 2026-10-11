#pragma once

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>

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

// A lever switch for an on/off option. Same arguments, same id and same result as ImGui::Checkbox: the lever is
// at the right with the green lamp lit when the option is on, at the left with the lamp out when it is off.
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
    const float width = ImFloor(height * 1.8f);
    const ImVec2 pos = window->DC.CursorPos;

    const ImRect bounds(pos, ImVec2(pos.x + width + (labelSize.x > 0.0f ? style.ItemInnerSpacing.x + labelSize.x : 0.0f),
                                    pos.y + labelSize.y + style.FramePadding.y * 2.0f));

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

    ImDrawList* draw = window->DrawList;
    const bool on = *value;

    // Housing, sunk into the panel
    const ImVec2 housingMin = pos;
    const ImVec2 housingMax(pos.x + width, pos.y + height);

    draw->AddRectFilled(housingMin, housingMax, ImGui::GetColorU32(GraphiteDeep), 1.0f);
    draw->AddRect(housingMin, housingMax, ImGui::GetColorU32(hovered ? MetalLight : Metal), 1.0f);

    // Lamp in the half the lever leaves uncovered
    const float inset = ImMax(2.0f, ImFloor(height * 0.16f));
    const float half = ImFloor(width * 0.5f);
    const ImVec2 lampMin(on ? housingMin.x + inset : housingMin.x + half, housingMin.y + inset);
    const ImVec2 lampMax(on ? housingMin.x + half : housingMax.x - inset, housingMax.y - inset);

    draw->AddRectFilled(lampMin, lampMax, ImGui::GetColorU32(on ? LampGreen : OliveDim));

    // Lever, with a light edge above and a dark one below
    const ImVec2 leverMin(on ? housingMin.x + half : housingMin.x + 1.0f, housingMin.y + 1.0f);
    const ImVec2 leverMax(on ? housingMax.x - 1.0f : housingMin.x + half, housingMax.y - 1.0f);

    draw->AddRectFilled(leverMin, leverMax, ImGui::GetColorU32(held ? MetalDark : hovered ? MetalLight : Metal));
    draw->AddLine(leverMin, ImVec2(leverMax.x, leverMin.y), ImGui::GetColorU32(Alpha(MetalLight, held ? 0.3f : 0.9f)));
    draw->AddLine(ImVec2(leverMin.x, leverMax.y - 1.0f), ImVec2(leverMax.x, leverMax.y - 1.0f),
                  ImGui::GetColorU32(Alpha(Shadow, 0.8f)));

    // Grip lines of the lever
    const float middle = ImFloor((leverMin.x + leverMax.x) * 0.5f);

    for (int i = -1; i <= 1; i++)
    {
        draw->AddLine(ImVec2(middle + i * 3.0f, leverMin.y + inset), ImVec2(middle + i * 3.0f, leverMax.y - inset),
                      ImGui::GetColorU32(Alpha(Shadow, 0.55f)));
    }

    if (labelSize.x > 0.0f)
        ImGui::RenderText(ImVec2(housingMax.x + style.ItemInnerSpacing.x, pos.y + style.FramePadding.y), label);

    return pressed;
}

// The on/off control of the theme in use
inline bool Checkbox(const char* label, bool* value)
{
    return industrial ? Switch(label, value) : ImGui::Checkbox(label, value);
}

// A button of something that can't be undone, red in the industrial theme. The same button otherwise.
inline bool DangerButton(const char* label)
{
    if (!industrial)
        return ImGui::Button(label);

    ImGui::PushStyleColor(ImGuiCol_Button, RedDim);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, RedLit);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, LampRed);

    const bool pressed = ImGui::Button(label);

    ImGui::PopStyleColor(3);

    return pressed;
}
} // namespace IlionTheme
