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

// How the parts of the ILION themes are drawn. Same controls, same layout; only the drawing changes.
enum class Skin
{
    Industrial, // ILION: 70s Soviet instrument panel, aged metal, keys with relief, pilot lamps
    Terminal,   // Fallout: retro-futuristic terminal, phosphor text, amber keys and indicators, scanlines
    Flat,       // Light: bright, flat keys, thin lines, few frames
    Crt,        // Fallout 2: the terminal drawn on an old curved CRT, aged phosphor, glow and wear
};

inline Skin skin = Skin::Industrial;

// Fallout and Fallout 2 share the terminal way of drawing; Fallout 2 adds the glass of the screen
inline bool TerminalLike() { return skin == Skin::Terminal || skin == Skin::Crt; }

// Fonts of the themes. Fallout and Light take fonts Windows already has; Fallout 2 carries two OFL fonts in the
// dll (Share Tech Mono for text, Russo One for names), Consolas merged in for the signs they lack. Null: the menu's.
inline ImFont* terminalFont = nullptr;
inline ImFont* modernFont = nullptr;
inline ImFont* crtFont = nullptr;
inline ImFont* crtTitleFont = nullptr;

inline ImFont* SkinFont()
{
    if (!industrial)
        return nullptr;

    return skin == Skin::Terminal ? terminalFont : skin == Skin::Crt ? crtFont : skin == Skin::Flat ? modernFont : nullptr;
}

// ILION in the window name: a radioactive yellow, darker on light backgrounds so it can still be read
inline ImVec4 IlionYellow(const ImVec4& background)
{
    const float light = background.x * 0.299f + background.y * 0.587f + background.z * 0.114f;
    return light > 0.55f ? ImVec4(0.62f, 0.46f, 0.00f, 1.0f) : ImVec4(1.00f, 0.86f, 0.08f, 1.0f);
}

// The window name drawn in three pieces: what comes before ILION, ILION in its yellow, the rest
inline void AddWindowName(ImDrawList* draw, ImFont* font, float size, const ImVec2& pos, ImU32 colour, ImU32 ilionColour,
                          const char* name, const char* end = nullptr)
{
    if (end == nullptr)
        end = name + strlen(name);

    const char* at = nullptr;
    for (const char* p = name; p + 5 <= end; p++)
    {
        if (strncmp(p, "ILION", 5) == 0)
        {
            at = p;
            break;
        }
    }

    if (at == nullptr)
    {
        draw->AddText(font, size, pos, colour, name, end);
        return;
    }

    float x = pos.x;
    draw->AddText(font, size, ImVec2(x, pos.y), colour, name, at);
    x += font->CalcTextSizeA(size, FLT_MAX, 0.0f, name, at).x;
    draw->AddText(font, size, ImVec2(x, pos.y), ilionColour, at, at + 5);
    x += font->CalcTextSizeA(size, FLT_MAX, 0.0f, at, at + 5).x;
    draw->AddText(font, size, ImVec2(x, pos.y), colour, at + 5, end);
}

// Font of names (plates, card heads, tabs, the title bar). Only Fallout 2 has one; null keeps the current font.
inline ImFont* TitleFont() { return industrial && skin == Skin::Crt ? crtTitleFont : nullptr; }

// Text drawn with the title font when there is one, at the size in use
inline void AddTitleText(ImDrawList* draw, const ImVec2& pos, ImU32 colour, const char* text, const char* end = nullptr)
{
    if (ImFont* font = TitleFont())
        draw->AddText(font, ImGui::GetFontSize(), pos, colour, text, end);
    else
        draw->AddText(pos, colour, text, end);
}

inline ImVec2 TitleTextSize(const char* text, const char* end = nullptr)
{
    if (ImFont* font = TitleFont())
        return font->CalcTextSizeA(ImGui::GetFontSize(), FLT_MAX, 0.0f, text, end);

    return ImGui::CalcTextSize(text, end);
}

// Colours of states written in the menu (fine, warning, error). The ILION and classic themes keep the exact colour
// written where it is used; the other two get one they can be read on.
enum class Role
{
    Good,
    Warn,
    Bad,
    Active, // the name of a technology that is running (upscaler in use, FG and its multiplier)
};

inline ImVec4 Themed(const ImVec4& original, Role role)
{
    if (!industrial || skin == Skin::Industrial)
        return original;

    // Running technologies: radioactive orange in Light and Fallout, the colour of "good" in the other terminal
    if (role == Role::Active)
    {
        if (skin == Skin::Flat)
            return ImVec4(0.82f, 0.34f, 0.00f, 1.0f);
        if (skin != Skin::Crt)
            role = Role::Good;
        else
            return ImVec4(1.00f, 0.62f, 0.18f, 1.0f);
    }

    // Fallout 2: aged phosphor and amber, softer than the other terminal
    // Active, running: a bright retro orange (FG multipliers, the upscaler in use); warnings in aged yellow
    if (skin == Skin::Crt)
        return role == Role::Good ? ImVec4(1.00f, 0.62f, 0.18f, 1.0f)
               : role == Role::Warn ? ImVec4(0.90f, 0.80f, 0.34f, 1.0f)
                                    : ImVec4(0.95f, 0.30f, 0.24f, 1.0f);

    if (TerminalLike())
        return role == Role::Good ? ImVec4(0.30f, 1.00f, 0.50f, 1.0f)
               : role == Role::Warn ? ImVec4(1.00f, 0.72f, 0.15f, 1.0f)
                                    : ImVec4(1.00f, 0.32f, 0.22f, 1.0f);

    // Light: a livelier phosphor green, still dark enough to read on the grey
    return role == Role::Good ? ImVec4(0.02f, 0.56f, 0.18f, 1.0f)
           : role == Role::Warn ? ImVec4(0.70f, 0.40f, 0.00f, 1.0f)
                                : ImVec4(0.76f, 0.12f, 0.10f, 1.0f);
}

// Painted steel
inline ImVec4 GraphiteDeep { 0.070f, 0.075f, 0.070f, 1.0f };
inline ImVec4 Graphite { 0.105f, 0.110f, 0.103f, 1.0f };
inline ImVec4 Panel { 0.140f, 0.150f, 0.135f, 1.0f };
inline ImVec4 OliveDim { 0.185f, 0.205f, 0.150f, 1.0f };
inline ImVec4 Olive { 0.255f, 0.285f, 0.195f, 1.0f };
inline ImVec4 OliveLit { 0.340f, 0.380f, 0.250f, 1.0f };
inline ImVec4 OlivePressed { 0.150f, 0.170f, 0.120f, 1.0f };

// Bare metal of edges and levers
inline ImVec4 MetalDark { 0.270f, 0.285f, 0.260f, 1.0f };
inline ImVec4 Metal { 0.430f, 0.450f, 0.410f, 1.0f };
inline ImVec4 MetalLight { 0.640f, 0.660f, 0.610f, 1.0f };
inline ImVec4 Shadow { 0.020f, 0.022f, 0.020f, 1.0f };

// Engraved text
inline ImVec4 Text { 0.870f, 0.885f, 0.810f, 1.0f };
inline ImVec4 TextDim { 0.560f, 0.590f, 0.490f, 1.0f };
inline ImVec4 TextTitle { 0.760f, 0.800f, 0.620f, 1.0f };

// Lamps
inline ImVec4 LampGreen { 0.224f, 1.000f, 0.078f, 1.0f }; // #39FF14
inline ImVec4 LampGreenDim { 0.150f, 0.520f, 0.090f, 1.0f };
inline ImVec4 LampAmber { 1.000f, 0.690f, 0.000f, 1.0f };
inline ImVec4 LampRed { 0.800f, 0.000f, 0.000f, 1.0f }; // #CC0000
inline ImVec4 RedDim { 0.400f, 0.060f, 0.050f, 1.0f };
inline ImVec4 RedLit { 0.580f, 0.080f, 0.060f, 1.0f };

inline ImVec4 Alpha(const ImVec4& color, float alpha) { return ImVec4(color.x, color.y, color.z, alpha); }

inline ImVec4 Shade(const ImVec4& color, float factor)
{
    return ImVec4(ImMin(color.x * factor, 1.0f), ImMin(color.y * factor, 1.0f), ImMin(color.z * factor, 1.0f), color.w);
}

// Strength of the brushed metal grain drawn over panels, lower on palettes where it would be too visible
inline float metalOpacity = 1.0f;

inline ImVec4 TitleAmber { 0.800f, 0.690f, 0.400f, 1.0f };
inline ImVec4 EdgeAmber { 0.224f, 1.000f, 0.078f, 1.0f };
inline ImVec4 TrimGold { 0.560f, 0.575f, 0.590f, 1.0f };
inline ImVec4 TrimTitanium { 0.560f, 0.575f, 0.590f, 1.0f };
inline ImVec4 StripeYellow { 0.800f, 0.670f, 0.110f, 1.0f };

// The three ILION palettes. Controls, layout and drawn parts are the same for all of them, only colours change.
//   0 ILION: 70s Soviet industrial, aged metal, steel trims (no brown frames), radioactive green lamps and tabs
//   1 Fallout: retro-futuristic phosphor green terminal
//   2 Light: light grey and plain, dark text
inline void SetPalette(int palette)
{
    struct Palette
    {
        ImVec4 graphiteDeep, graphite, panel, oliveDim, olive, oliveLit, olivePressed;
        ImVec4 metalDark, metal, metalLight, shadow;
        ImVec4 text, textDim, textTitle;
        ImVec4 lampGreen, lampGreenDim, lampAmber, lampRed, redDim, redLit;
        ImVec4 titleAmber, edge, trimGold, trimTitanium, stripe;
        float metal_;
    };

    static const Palette palettes[] = {
        { { 0.070f, 0.075f, 0.070f, 1 }, { 0.105f, 0.110f, 0.103f, 1 }, { 0.140f, 0.150f, 0.135f, 1 },
          { 0.185f, 0.205f, 0.150f, 1 }, { 0.255f, 0.285f, 0.195f, 1 }, { 0.340f, 0.380f, 0.250f, 1 },
          { 0.150f, 0.170f, 0.120f, 1 }, { 0.270f, 0.285f, 0.260f, 1 }, { 0.430f, 0.450f, 0.410f, 1 },
          { 0.640f, 0.660f, 0.610f, 1 }, { 0.020f, 0.022f, 0.020f, 1 }, { 0.870f, 0.885f, 0.810f, 1 },
          { 0.560f, 0.590f, 0.490f, 1 }, { 0.760f, 0.800f, 0.620f, 1 }, { 0.224f, 1.000f, 0.078f, 1 },
          { 0.150f, 0.520f, 0.090f, 1 }, { 1.000f, 0.690f, 0.000f, 1 }, { 0.800f, 0.000f, 0.000f, 1 },
          { 0.400f, 0.060f, 0.050f, 1 }, { 0.580f, 0.080f, 0.060f, 1 }, { 0.950f, 0.820f, 0.220f, 1 },
          { 0.224f, 1.000f, 0.078f, 1 }, { 0.560f, 0.575f, 0.590f, 1 }, { 0.560f, 0.575f, 0.590f, 1 },
          { 0.800f, 0.670f, 0.110f, 1 }, 1.0f },
        { { 0.012f, 0.020f, 0.012f, 1 }, { 0.020f, 0.035f, 0.022f, 1 }, { 0.030f, 0.050f, 0.032f, 1 },
          { 0.060f, 0.075f, 0.035f, 1 }, { 0.150f, 0.110f, 0.030f, 1 }, { 0.300f, 0.215f, 0.040f, 1 },
          { 0.380f, 0.270f, 0.040f, 1 }, { 0.090f, 0.200f, 0.110f, 1 }, { 0.180f, 0.420f, 0.220f, 1 },
          { 0.300f, 0.700f, 0.380f, 1 }, { 0.000f, 0.000f, 0.000f, 1 }, { 0.260f, 1.000f, 0.460f, 1 },
          { 0.160f, 0.640f, 0.310f, 1 }, { 1.000f, 0.720f, 0.150f, 1 }, { 1.000f, 0.690f, 0.080f, 1 },
          { 0.520f, 0.350f, 0.040f, 1 }, { 1.000f, 0.850f, 0.350f, 1 }, { 1.000f, 0.300f, 0.200f, 1 },
          { 0.300f, 0.060f, 0.040f, 1 }, { 0.500f, 0.100f, 0.060f, 1 }, { 1.000f, 0.720f, 0.150f, 1 },
          { 1.000f, 0.690f, 0.080f, 1 }, { 0.180f, 0.500f, 0.260f, 1 }, { 0.120f, 0.340f, 0.180f, 1 },
          { 1.000f, 0.690f, 0.080f, 1 }, 0.0f },
        // 2 Light: greys toned down so the window doesn't glare, the text keeps its contrast
        { { 0.700f, 0.714f, 0.730f, 1 }, { 0.752f, 0.763f, 0.776f, 1 }, { 0.815f, 0.822f, 0.831f, 1 },
          { 0.772f, 0.781f, 0.795f, 1 }, { 0.733f, 0.746f, 0.766f, 1 }, { 0.668f, 0.703f, 0.756f, 1 },
          { 0.632f, 0.659f, 0.703f, 1 }, { 0.676f, 0.690f, 0.708f, 1 }, { 0.640f, 0.665f, 0.700f, 1 },
          { 0.470f, 0.500f, 0.545f, 1 }, { 0.300f, 0.320f, 0.360f, 1 }, { 0.070f, 0.080f, 0.100f, 1 },
          { 0.270f, 0.295f, 0.335f, 1 }, { 0.090f, 0.200f, 0.360f, 1 }, { 0.070f, 0.400f, 0.700f, 1 },
          { 0.420f, 0.580f, 0.780f, 1 }, { 0.700f, 0.390f, 0.000f, 1 }, { 0.720f, 0.120f, 0.090f, 1 },
          { 0.900f, 0.780f, 0.770f, 1 }, { 0.860f, 0.640f, 0.620f, 1 }, { 0.090f, 0.200f, 0.360f, 1 },
          { 0.850f, 0.400f, 0.000f, 1 }, { 0.640f, 0.665f, 0.700f, 1 }, { 0.770f, 0.785f, 0.805f, 1 },
          { 0.070f, 0.400f, 0.700f, 1 }, 0.0f },
        // 3 Fallout: faded phosphor on a dark green tube, keys in dim phosphor, radioactive orange for names and edges
        { { 0.016f, 0.026f, 0.018f, 1 }, { 0.026f, 0.040f, 0.028f, 1 }, { 0.034f, 0.052f, 0.036f, 1 },
          { 0.075f, 0.115f, 0.072f, 1 }, { 0.115f, 0.175f, 0.105f, 1 }, { 0.170f, 0.260f, 0.150f, 1 },
          { 0.055f, 0.085f, 0.052f, 1 }, { 0.140f, 0.220f, 0.140f, 1 }, { 0.260f, 0.400f, 0.250f, 1 },
          { 0.420f, 0.620f, 0.400f, 1 }, { 0.000f, 0.000f, 0.000f, 1 }, { 0.580f, 0.820f, 0.520f, 1 },
          { 0.340f, 0.520f, 0.320f, 1 }, { 1.000f, 0.640f, 0.200f, 1 }, { 0.480f, 0.860f, 0.420f, 1 },
          { 0.220f, 0.400f, 0.200f, 1 }, { 1.000f, 0.820f, 0.200f, 1 }, { 0.900f, 0.360f, 0.240f, 1 },
          { 0.280f, 0.080f, 0.050f, 1 }, { 0.460f, 0.120f, 0.070f, 1 }, { 1.000f, 0.640f, 0.200f, 1 },
          { 1.000f, 0.600f, 0.160f, 1 }, { 0.360f, 0.560f, 0.340f, 1 }, { 0.220f, 0.340f, 0.210f, 1 },
          { 1.000f, 0.600f, 0.160f, 1 }, 0.0f },
    };

    const auto& p = palettes[palette >= 0 && palette < (int) std::size(palettes) ? palette : 0];

    GraphiteDeep = p.graphiteDeep;
    Graphite = p.graphite;
    Panel = p.panel;
    OliveDim = p.oliveDim;
    Olive = p.olive;
    OliveLit = p.oliveLit;
    OlivePressed = p.olivePressed;
    MetalDark = p.metalDark;
    Metal = p.metal;
    MetalLight = p.metalLight;
    Shadow = p.shadow;
    Text = p.text;
    TextDim = p.textDim;
    TextTitle = p.textTitle;
    LampGreen = p.lampGreen;
    LampGreenDim = p.lampGreenDim;
    LampAmber = p.lampAmber;
    LampRed = p.lampRed;
    RedDim = p.redDim;
    RedLit = p.redLit;
    TitleAmber = p.titleAmber;
    EdgeAmber = p.edge;
    TrimGold = p.trimGold;
    TrimTitanium = p.trimTitanium;
    StripeYellow = p.stripe;
    metalOpacity = p.metal_;
    skin = palette == 1 ? Skin::Terminal : palette == 2 ? Skin::Flat : palette == 3 ? Skin::Crt : Skin::Industrial;
}

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

    style.WindowTitleAlign = ImVec2(0.0f, 0.5f);

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

    if (TerminalLike())
    {
        // Square everything, one fine line round each part, amber where something can be pressed
        style.WindowRounding = style.ChildRounding = style.FrameRounding = style.PopupRounding = 0.0f;
        style.ScrollbarRounding = style.GrabRounding = style.TabRounding = 0.0f;

        // Fallout 2: the glass of the screen is rounded, its parts a little softer
        if (skin == Skin::Crt)
            style.WindowRounding = 12.0f;

        c[ImGuiCol_Border] = Alpha(TrimGold, 0.90f);
        c[ImGuiCol_BorderShadow] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
        c[ImGuiCol_TitleBg] = c[ImGuiCol_TitleBgActive] = c[ImGuiCol_TitleBgCollapsed] = GraphiteDeep;
        c[ImGuiCol_Button] = OliveDim;
        c[ImGuiCol_ButtonHovered] = Olive;
        c[ImGuiCol_ButtonActive] = OliveLit;
        c[ImGuiCol_Header] = Alpha(LampGreen, 0.14f);
        c[ImGuiCol_HeaderHovered] = Alpha(LampGreen, 0.26f);
        c[ImGuiCol_HeaderActive] = Alpha(LampGreen, 0.40f);
        c[ImGuiCol_FrameBgHovered] = Alpha(LampGreen, 0.12f);
        c[ImGuiCol_FrameBgActive] = Alpha(LampGreen, 0.22f);
        c[ImGuiCol_SliderGrab] = LampGreenDim;
        c[ImGuiCol_SliderGrabActive] = LampGreen;
        c[ImGuiCol_TabSelected] = Alpha(LampGreen, 0.30f);
        c[ImGuiCol_TabHovered] = Alpha(LampGreen, 0.20f);
        c[ImGuiCol_TableHeaderBg] = Alpha(LampGreen, 0.16f);
        c[ImGuiCol_TableRowBgAlt] = Alpha(Text, 0.04f);
        c[ImGuiCol_TextSelectedBg] = Alpha(LampGreen, 0.35f);

        if (skin == Skin::Crt)
        {
            c[ImGuiCol_CheckMark] = ImVec4(1.00f, 0.60f, 0.16f, 1.0f);
            c[ImGuiCol_SliderGrabActive] = ImVec4(1.00f, 0.60f, 0.16f, 1.0f);
        }
    }
    else if (skin == Skin::Flat)
    {
        // Round, flat and borderless keys, thin lines, white panels on a light grey window
        style.WindowRounding = 8.0f;
        style.ChildRounding = 6.0f;
        style.FrameRounding = 4.0f;
        style.PopupRounding = 6.0f;
        style.GrabRounding = 4.0f;
        style.TabRounding = 4.0f;
        style.ScrollbarRounding = 6.0f;
        style.FrameBorderSize = 1.0f;
        style.TabBorderSize = 0.0f;

        // Grey surfaces in steps (window, cards, fields), every field with a fine border
        c[ImGuiCol_WindowBg] = Alpha(Graphite, backgroundAlpha >= 0.92f ? backgroundAlpha : 0.92f);
        c[ImGuiCol_ChildBg] = Panel;
        c[ImGuiCol_PopupBg] = Alpha(Panel, 0.99f);
        c[ImGuiCol_Border] = Metal;
        c[ImGuiCol_BorderShadow] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
        c[ImGuiCol_FrameBg] = ImVec4(0.965f, 0.970f, 0.976f, 1.0f);
        c[ImGuiCol_TitleBg] = c[ImGuiCol_TitleBgActive] = c[ImGuiCol_TitleBgCollapsed] = GraphiteDeep;
        c[ImGuiCol_ScrollbarBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
        c[ImGuiCol_Header] = Alpha(LampGreen, 0.10f);
        c[ImGuiCol_HeaderHovered] = Alpha(LampGreen, 0.16f);
        c[ImGuiCol_HeaderActive] = Alpha(LampGreen, 0.24f);
        c[ImGuiCol_SliderGrab] = LampGreen;
        c[ImGuiCol_SliderGrabActive] = Shade(LampGreen, 0.85f);
        c[ImGuiCol_TabSelected] = Panel;
        c[ImGuiCol_TableHeaderBg] = GraphiteDeep;
        c[ImGuiCol_TableRowBgAlt] = ImVec4(0.0f, 0.0f, 0.0f, 0.03f);
        c[ImGuiCol_TextSelectedBg] = Alpha(LampGreen, 0.25f);
        c[ImGuiCol_ModalWindowDimBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.25f);

        // A few accents: the selected tab marked in radioactive orange, the graphs in a lively phosphor green
        c[ImGuiCol_TabSelectedOverline] = ImVec4(0.90f, 0.40f, 0.00f, 1.0f);
        c[ImGuiCol_PlotLines] = ImVec4(0.00f, 0.62f, 0.20f, 1.0f);
        c[ImGuiCol_PlotLinesHovered] = ImVec4(0.90f, 0.40f, 0.00f, 1.0f);
    }
}

// --- Drawn parts ---------------------------------------------------------------------------------------------


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
        return skin == Skin::Flat ? MetalLight : TerminalLike() ? LampAmber : ImVec4(0.80f, 0.83f, 0.76f, 1.0f);
    default:
        // Fallout 2: what is on glows in retro orange, the rest keeps the faded phosphor
        return skin == Skin::Crt ? ImVec4(1.00f, 0.60f, 0.16f, 1.0f) : LampGreen;
    }
}

// A round pilot lamp behind glass in a metal ring. Lit: its colour with the light gathered in the middle and a
// faint halo. Out: the same glass dark. Only drawn for a state that exists.
inline void DrawLamp(ImDrawList* draw, const ImVec2& center, float radius, bool lit, bool hovered = false,
                     bool held = false, Lamp kind = Lamp::Green)
{
    const ImVec4 color = LampColor(kind);

    // Fallout 2: a round phosphor dot; lit it glows on the glass, out it is a dim ring
    if (skin == Skin::Crt)
    {
        const float dot = ImFloor(radius * 0.50f);

        if (lit)
        {
            draw->AddCircleFilled(center, dot * 2.1f, ImGui::GetColorU32(Alpha(color, 0.10f)));
            draw->AddCircleFilled(center, dot * 1.5f, ImGui::GetColorU32(Alpha(color, 0.18f)));
            draw->AddCircleFilled(center, dot, ImGui::GetColorU32(Alpha(color, held ? 0.70f : 1.0f)));
        }
        else
        {
            draw->AddCircle(center, dot, ImGui::GetColorU32(Alpha(color, hovered ? 0.95f : 0.50f)), 0, 1.2f);
        }

        return;
    }

    // Terminal: a square indicator, lit filled, out only its outline
    if (TerminalLike())
    {
        const float half = ImFloor(radius * 0.62f);
        const ImVec2 a(center.x - half, center.y - half), b(center.x + half, center.y + half);

        draw->AddRect(a, b, ImGui::GetColorU32(Alpha(color, hovered ? 1.0f : lit ? 0.95f : 0.55f)), 0.0f, 0, 1.0f);

        if (lit)
            draw->AddRectFilled(ImVec2(a.x + 2.0f, a.y + 2.0f), ImVec2(b.x - 2.0f, b.y - 2.0f),
                                ImGui::GetColorU32(Alpha(color, held ? 0.70f : 1.0f)));

        return;
    }

    // Flat: a small dot, filled when on, a thin ring when off
    if (skin == Skin::Flat)
    {
        const float dot = ImFloor(radius * 0.48f);

        if (lit)
            draw->AddCircleFilled(center, dot, ImGui::GetColorU32(Alpha(color, held ? 0.75f : 1.0f)));
        else
            draw->AddCircle(center, dot, ImGui::GetColorU32(hovered ? MetalLight : Metal), 0, 1.2f);

        return;
    }

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
    if (skin != Skin::Industrial)
        return;

    draw->AddCircleFilled(center, radius + 1.0f, ImGui::GetColorU32(Alpha(Shadow, 0.85f)));
    draw->AddCircleFilled(center, radius, ImGui::GetColorU32(MetalDark));
    draw->AddCircleFilled(ImVec2(center.x - radius * 0.2f, center.y - radius * 0.2f), radius * 0.62f,
                          ImGui::GetColorU32(Alpha(Metal, 0.75f)));
    draw->AddLine(ImVec2(center.x - radius * 0.62f, center.y + radius * 0.62f),
                  ImVec2(center.x + radius * 0.62f, center.y - radius * 0.62f), ImGui::GetColorU32(Shadow), 1.0f);
}

// The edge of a main panel: a metal frame with a dark seam, and a screw in every corner
inline void DrawChamferFrame(ImDrawList* draw, const ImVec2& min, const ImVec2& max, float cut, ImU32 color, float thickness);

inline void DrawPanelFrame(ImDrawList* draw, const ImVec2& min, const ImVec2& max, bool screws, float scale = 1.0f)
{
    // Fallout 2: a double line with cut corners
    if (skin == Skin::Crt)
    {
        DrawChamferFrame(draw, min, max, 6.0f, ImGui::GetColorU32(Alpha(TrimGold, 0.90f)), 1.0f);
        DrawChamferFrame(draw, ImVec2(min.x + 3.0f, min.y + 3.0f), ImVec2(max.x - 3.0f, max.y - 3.0f), 4.0f,
                         ImGui::GetColorU32(Alpha(TrimTitanium, 0.70f)), 1.0f);
        return;
    }

    if (TerminalLike())
    {
        draw->AddRect(min, max, ImGui::GetColorU32(Alpha(TrimGold, 0.90f)));
        return;
    }

    if (skin == Skin::Flat)
    {
        draw->AddRect(min, max, ImGui::GetColorU32(MetalDark), 6.0f);
        return;
    }

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
    if (!industrial || skin != Skin::Industrial)
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
inline void ChamferPoints(ImVec2 points[8], const ImVec2& min, const ImVec2& max, float cut);

inline void DrawKey(ImDrawList* draw, const ImRect& bounds, const ImVec4& face, bool hovered, bool down)
{
    // Fallout 2: a key with cut corners, dim phosphor fill, its edge turns amber under the pointer
    if (skin == Skin::Crt)
    {
        ImVec2 points[8];
        const float cut = ImMin(4.0f, ImFloor((bounds.Max.y - bounds.Min.y) * 0.25f));

        ChamferPoints(points, bounds.Min, bounds.Max, cut);
        draw->AddConvexPolyFilled(points, 8, ImGui::GetColorU32(down ? Shade(face, 1.50f) : hovered ? Shade(face, 1.22f) : face));
        draw->AddPolyline(points, 8, ImGui::GetColorU32(hovered || down ? LampAmber : Alpha(TrimGold, 0.85f)), ImDrawFlags_Closed,
                          hovered ? 1.5f : 1.0f);
        return;
    }

    // Terminal: a dark box with an amber line, filled more under the pointer and when held
    if (TerminalLike())
    {
        draw->AddRectFilled(bounds.Min, bounds.Max, ImGui::GetColorU32(down ? Shade(face, 1.45f) : hovered ? Shade(face, 1.20f) : face));
        draw->AddRect(bounds.Min, bounds.Max, ImGui::GetColorU32(Alpha(LampGreen, hovered || down ? 1.0f : 0.60f)), 0.0f, 0,
                      hovered ? 1.5f : 1.0f);
        return;
    }

    // Flat: a filled rounded rectangle, a little darker under the pointer and when held, no relief
    if (skin == Skin::Flat)
    {
        draw->AddRectFilled(bounds.Min, bounds.Max, ImGui::GetColorU32(down ? Shade(face, 0.90f) : hovered ? Shade(face, 0.95f) : face), 4.0f);
        draw->AddRect(bounds.Min, bounds.Max, ImGui::GetColorU32(hovered || down ? LampGreen : Metal), 4.0f);
        return;
    }

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
    // Fallout 2: a dark field between two brackets
    if (skin == Skin::Crt)
    {
        const ImU32 edge = ImGui::GetColorU32(Alpha(hovered ? LampAmber : TrimGold, hovered ? 0.95f : 0.80f));
        const float arm = ImMin(5.0f, (max.x - min.x) * 0.2f);

        draw->AddRectFilled(min, max, ImGui::GetColorU32(Alpha(GraphiteDeep, 0.95f)));
        draw->AddLine(ImVec2(min.x, min.y), ImVec2(min.x, max.y), edge);
        draw->AddLine(ImVec2(min.x, min.y), ImVec2(min.x + arm, min.y), edge);
        draw->AddLine(ImVec2(min.x, max.y - 1.0f), ImVec2(min.x + arm, max.y - 1.0f), edge);
        draw->AddLine(ImVec2(max.x - 1.0f, min.y), ImVec2(max.x - 1.0f, max.y), edge);
        draw->AddLine(ImVec2(max.x - arm, min.y), ImVec2(max.x, min.y), edge);
        draw->AddLine(ImVec2(max.x - arm, max.y - 1.0f), ImVec2(max.x, max.y - 1.0f), edge);
        return;
    }

    // Terminal: an input line, dark with an amber underline
    if (TerminalLike())
    {
        draw->AddRectFilled(min, max, ImGui::GetColorU32(Alpha(GraphiteDeep, 0.95f)));
        draw->AddLine(ImVec2(min.x, max.y - 1.0f), ImVec2(max.x, max.y - 1.0f), ImGui::GetColorU32(Alpha(LampGreen, hovered ? 0.95f : 0.50f)));
        return;
    }

    // Flat: a white field with a thin border
    if (skin == Skin::Flat)
    {
        draw->AddRectFilled(min, max, ImGui::GetColorU32(ImVec4(0.965f, 0.970f, 0.976f, 1.0f)), 4.0f);
        draw->AddRect(min, max, ImGui::GetColorU32(hovered ? LampGreen : Metal), 4.0f);
        return;
    }

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

    if (skin == Skin::Flat)
    {
        // A switch: a rounded track, the accent colour when on, with a white knob at its end
        const float trackHeight = ImFloor(height * 0.56f);
        const float trackWidth = ImFloor(height * 0.96f);
        const ImVec2 trackMin(pos.x + ImFloor((height - trackWidth) * 0.5f), pos.y + ImFloor((height - trackHeight) * 0.5f));
        const ImVec2 trackMax(trackMin.x + trackWidth, trackMin.y + trackHeight);
        const float knob = trackHeight * 0.5f - 2.0f;
        const float knobX = *value ? trackMax.x - trackHeight * 0.5f : trackMin.x + trackHeight * 0.5f;

        window->DrawList->AddRectFilled(trackMin, trackMax,
                                        ImGui::GetColorU32(*value ? (hovered ? Shade(LampGreen, 0.92f) : LampGreen) : (hovered ? Metal : MetalDark)),
                                        trackHeight * 0.5f);
        window->DrawList->AddCircleFilled(ImVec2(knobX, (trackMin.y + trackMax.y) * 0.5f), knob, ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, 1.0f)));
    }
    else
    {
        if (labelSize.x > 0.0f)
            DrawWell(window->DrawList, bounds.Min, bounds.Max, hovered);

        DrawLamp(window->DrawList, ImVec2(ImFloor(pos.x + height * 0.5f), ImFloor(pos.y + height * 0.5f)),
                 ImFloor(height * 0.5f) - 2.0f, *value, hovered, held);
    }

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

    // Terminal: a dotted track, amber up to the value, a block for the cursor
    if (TerminalLike())
    {
        for (float x = min.x + 2.0f; x < max.x - 2.0f; x += 4.0f)
            draw->AddRectFilled(ImVec2(x, middle), ImVec2(x + 1.0f, middle + 1.0f), ImGui::GetColorU32(Alpha(TextDim, 0.80f)));

        draw->AddRectFilled(ImVec2(min.x + 2.0f, middle - 1.0f), ImVec2(center, middle + 2.0f), ImGui::GetColorU32(active ? LampGreen : LampGreenDim));
        draw->AddRectFilled(ImVec2(center - 3.0f, min.y + 3.0f), ImVec2(center + 3.0f, max.y - 3.0f),
                            ImGui::GetColorU32(hovered || active ? LampGreen : Alpha(LampGreen, 0.80f)));
        return;
    }

    // Flat: a thin rounded track, the accent up to the value, a round white knob
    if (skin == Skin::Flat)
    {
        const float knob = ImFloor((max.y - min.y) * 0.30f);

        draw->AddRectFilled(ImVec2(min.x + 2.0f, middle - 2.0f), ImVec2(max.x - 2.0f, middle + 2.0f), ImGui::GetColorU32(MetalDark), 2.0f);
        draw->AddRectFilled(ImVec2(min.x + 2.0f, middle - 2.0f), ImVec2(center, middle + 2.0f), ImGui::GetColorU32(LampGreen), 2.0f);
        draw->AddCircleFilled(ImVec2(center, middle), knob, ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, 1.0f)));
        draw->AddCircle(ImVec2(center, middle), knob, ImGui::GetColorU32(hovered || active ? LampGreen : Metal), 0, 1.5f);
        return;
    }
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
        const bool green = skin == Skin::Industrial;
        ImGui::PushStyleColor(ImGuiCol_Button, green ? ImVec4(0.10f, 0.42f, 0.08f, 1.0f) : OliveDim);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, green ? ImVec4(0.14f, 0.60f, 0.10f, 1.0f) : Olive);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, green ? LampGreenDim : OliveLit);

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
            skin != Skin::Industrial            ? (hovered ? Olive : OliveDim)
            : hovered ? ImVec4(0.14f, 0.55f, 0.10f, 1.0f) : ImVec4(0.10f, 0.40f, 0.08f, 1.0f),
            hovered, false);

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

    // Fallout 2: the name in inverse video (dark letters on a phosphor block) and a double rule to the edge
    if (skin == Skin::Crt)
    {
        const std::string name = Upper(text);
        const ImVec2 nameSize = TitleTextSize(name.c_str());
        const ImVec2 blockMax(pos.x + nameSize.x + pad * 2.0f, pos.y + height);
        const float y = ImFloor(pos.y + height * 0.5f);

        draw->AddRectFilled(pos, blockMax, ImGui::GetColorU32(Alpha(LampGreen, 0.88f)));
        AddTitleText(draw, ImVec2(pos.x + pad, pos.y + 3.0f), ImGui::GetColorU32(GraphiteDeep), name.c_str());

        if (right > blockMax.x + 6.0f)
        {
            draw->AddLine(ImVec2(blockMax.x + 6.0f, y - 1.0f), ImVec2(right, y - 1.0f), ImGui::GetColorU32(Alpha(TrimGold, 0.85f)));
            draw->AddLine(ImVec2(blockMax.x + 6.0f, y + 2.0f), ImVec2(right, y + 2.0f), ImGui::GetColorU32(Alpha(TrimTitanium, 0.75f)));
        }

        ImGui::Dummy(ImVec2(ImMax(1.0f, right - pos.x), height));
        return;
    }

    // Terminal: "> NAME" in amber and a dashed line; Flat: the name in the title colour over a thin line
    if (skin != Skin::Industrial)
    {
        const bool terminal = TerminalLike();
        const std::string name = terminal ? "> " + Upper(text) : std::string(text, textEnd);
        const ImVec2 nameSize = ImGui::CalcTextSize(name.c_str());
        const float y = ImFloor(pos.y + height * 0.5f);

        draw->AddText(ImVec2(pos.x, pos.y + 3.0f), ImGui::GetColorU32(TextTitle), name.c_str());

        if (terminal)
        {
            for (float x = pos.x + nameSize.x + 8.0f; x < right; x += 6.0f)
                draw->AddLine(ImVec2(x, y), ImVec2(ImMin(x + 3.0f, right), y), ImGui::GetColorU32(Alpha(TrimGold, 0.85f)));
        }
        else if (right > pos.x + nameSize.x + 8.0f)
        {
            draw->AddLine(ImVec2(pos.x + nameSize.x + 8.0f, y), ImVec2(right, y), ImGui::GetColorU32(Metal));
        }

        ImGui::Dummy(ImVec2(ImMax(1.0f, right - pos.x), height));
        return;
    }

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
    if (skin != Skin::Industrial || !MetalTileReady() || max.x <= min.x || max.y <= min.y)
        return;

    const ImU32 tint = ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, opacity * metalOpacity));
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
// Compact technical panel: a dark chassis with rounded corners and a thick rim, a fine line of yellow trim running
// inside it, warning stripes on the top bar, and every module a dark plate framed in the same trim with its
// corners cut off. All of it is drawing behind what the window holds; nothing here takes a click.
// Yellow is for what stands out; the secondary lines (the second line inside a plate, the joints of the chassis)
// are bare titanium, a neutral grey that reads on the dark steel without competing with the trim.


// Outline of a rectangle with its four corners cut off
inline void ChamferPoints(ImVec2 points[8], const ImVec2& min, const ImVec2& max, float cut)
{
    points[0] = ImVec2(min.x + cut, min.y);
    points[1] = ImVec2(max.x - cut, min.y);
    points[2] = ImVec2(max.x, min.y + cut);
    points[3] = ImVec2(max.x, max.y - cut);
    points[4] = ImVec2(max.x - cut, max.y);
    points[5] = ImVec2(min.x + cut, max.y);
    points[6] = ImVec2(min.x, max.y - cut);
    points[7] = ImVec2(min.x, min.y + cut);
}

inline void DrawChamferFrame(ImDrawList* draw, const ImVec2& min, const ImVec2& max, float cut, ImU32 color, float thickness = 1.0f)
{
    ImVec2 points[8];
    ChamferPoints(points, min, max, cut);
    draw->AddPolyline(points, 8, color, ImDrawFlags_Closed, thickness);
}

// Bracket of trim around one corner of a chamfered frame: the cut and a stretch of both edges, heavier than the
// line of the frame. cornerX/cornerY: -1 left or top, 1 right or bottom.
inline void DrawCornerBracket(ImDrawList* draw, const ImVec2& min, const ImVec2& max, float cut, float length, int cornerX,
                              int cornerY, ImU32 color)
{
    const float x = cornerX < 0 ? min.x : max.x, y = cornerY < 0 ? min.y : max.y;
    const float dx = (float) -cornerX, dy = (float) -cornerY;

    const ImVec2 points[4] = { ImVec2(x + dx * (cut + length), y), ImVec2(x + dx * cut, y), ImVec2(x, y + dy * cut),
                               ImVec2(x, y + dy * (cut + length)) };

    draw->AddPolyline(points, 4, color, ImDrawFlags_None, 2.0f);
}

// Slanted warning stripes inside a rectangle
inline void DrawStripes(ImDrawList* draw, const ImVec2& min, const ImVec2& max, const ImVec4& color, float width)
{
    if (max.x - min.x < width * 2.0f)
        return;

    const float height = max.y - min.y;
    const ImU32 paint = ImGui::GetColorU32(color);

    draw->PushClipRect(min, max, true);

    for (float x = min.x - height; x < max.x; x += width * 2.0f)
    {
        draw->AddQuadFilled(ImVec2(x + height, min.y), ImVec2(x + height + width, min.y), ImVec2(x + width, max.y),
                            ImVec2(x, max.y), paint);
    }

    draw->PopClipRect();
}

// A reinforcement plate in a corner of the chassis, with its screw. cornerX/cornerY say which way the corner of
// the chassis is from the plate (-1 left or up, 1 right or down).
inline void DrawCornerPlate(ImDrawList* draw, const ImVec2& corner, float size, int cornerX, int cornerY)
{
    if (skin != Skin::Industrial)
        return;

    const float cut = ImFloor(size * 0.38f);
    const float dx = (float) -cornerX, dy = (float) -cornerY;

    // Along both edges from just inside the round corner of the chassis, with the inner corner cut off
    const ImVec2 points[6] = { ImVec2(corner.x + dx * cut, corner.y),
                               ImVec2(corner.x + dx * size, corner.y),
                               ImVec2(corner.x + dx * size, corner.y + dy * (size - cut)),
                               ImVec2(corner.x + dx * (size - cut), corner.y + dy * size),
                               ImVec2(corner.x, corner.y + dy * size),
                               ImVec2(corner.x, corner.y + dy * cut) };

    draw->AddConvexPolyFilled(points, 6, ImGui::GetColorU32(Shade(MetalDark, 1.05f)));
    draw->AddPolyline(points, 6, ImGui::GetColorU32(Shadow), ImDrawFlags_Closed, 1.0f);
    draw->AddLine(points[cornerY < 0 ? 0 : 3], points[cornerY < 0 ? 1 : 4], ImGui::GetColorU32(Alpha(MetalLight, cornerY < 0 ? 0.50f : 0.25f)));

    DrawScrew(draw, ImVec2(ImFloor(corner.x + dx * size * 0.46f), ImFloor(corner.y + dy * size * 0.46f)),
              ImMax(2.0f, ImFloor(size * 0.15f)));
}

// What a window needs around its Begin to be drawn as a chassis: round corners, a wider margin for the rim to
// live in, and a taller title bar for the top bar of the machine. Popped right after Begin, so nothing inside the
// window is laid out with them.
inline void PushChassisStyle()
{
    const ImGuiStyle& style = ImGui::GetStyle();

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,
                        skin == Skin::Crt ? 12.0f : TerminalLike() ? 0.0f : skin == Skin::Flat ? 8.0f : ImFloor(style.WindowPadding.x * 1.25f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(ImFloor(style.WindowPadding.x * 1.5f), ImFloor(style.WindowPadding.y * 1.5f)));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(style.FramePadding.x, ImFloor(style.FramePadding.y * 3.6f)));
}

inline void PopChassisStyle() { ImGui::PopStyleVar(3); }

// The window being drawn as the chassis of the console. Its title bar becomes the top bar of the machine: the
// name on a recessed plate at the left, warning stripes after it. Needs the window to have been begun with
// ChassisRounding as its rounding and without a collapse or a close button, which would end up under the bar.
inline void DrawChassis()
{
    if (!industrial)
    {
        ImGuiWindow* window = ImGui::GetCurrentWindow();

        if (window->SkipItems || window->TitleBarHeight <= 0.0f)
            return;

        // Where ImGui writes the title: after the frame padding, centred in the bar's height
        const ImGuiStyle& style = ImGui::GetStyle();
        const char* nameEnd = ImGui::FindRenderedTextEnd(window->Name);
        const char* at = strstr(window->Name, "ILION");

        if (at == nullptr || at >= nameEnd)
            return;

        const ImRect bar = window->TitleBarRect();
        const float x = bar.Min.x + style.FramePadding.x + ImGui::CalcTextSize(window->Name, at).x;
        const float y = bar.Min.y + ImFloor((bar.GetHeight() - ImGui::GetFontSize()) * 0.5f);
        const ImVec4 background = ImGui::GetStyleColorVec4(ImGuiCol_TitleBgActive);

        window->DrawList->PushClipRect(bar.Min, bar.Max, false);
        window->DrawList->AddText(ImVec2(x, y), ImGui::GetColorU32(IlionYellow(background)), at, at + 5);
        window->DrawList->PopClipRect();
        return;
    }

    ImGuiWindow* window = ImGui::GetCurrentWindow();

    if (window->SkipItems)
        return;

    const ImGuiStyle& style = ImGui::GetStyle();
    const ImVec2 min = window->Pos;
    const ImVec2 max(window->Pos.x + window->Size.x, window->Pos.y + window->Size.y);
    const float rim = ImMax(4.0f, ImFloor(window->WindowPadding.x * 0.66f));
    const float rounding = window->WindowRounding;
    const float title = window->TitleBarHeight;

    ImDrawList* draw = window->DrawList;

    draw->PushClipRect(min, max, false);

    if (skin != Skin::Industrial)
    {
        const bool terminal = TerminalLike();
        const char* nameEnd = ImGui::FindRenderedTextEnd(window->Name);
        const ImVec2 nameSize = ImGui::CalcTextSize(window->Name, nameEnd);

        // Title bar painted over, the name drawn again in the skin's way
        if (title > 0.0f)
        {
            draw->AddRectFilled(ImVec2(min.x + 1.0f, min.y + 1.0f), ImVec2(max.x - 1.0f, min.y + title),
                                ImGui::GetColorU32(terminal ? GraphiteDeep : Graphite), terminal ? 0.0f : rounding,
                                ImDrawFlags_RoundCornersTop);

            const ImVec2 namePos(ImFloor(min.x + style.WindowPadding.x), ImFloor(min.y + (title - nameSize.y) * 0.5f));

            if (skin == Skin::Crt)
            {
                // Fallout 2: the name on an amber plate, dark letters, and a double phosphor rule under the bar
                const std::string name = Upper(window->Name);
                const ImVec2 size = TitleTextSize(name.c_str());
                const float pad = style.FramePadding.x + 2.0f;

                draw->AddRectFilled(ImVec2(namePos.x - pad, namePos.y - 2.0f), ImVec2(namePos.x + size.x + pad, namePos.y + size.y + 2.0f),
                                    ImGui::GetColorU32(Alpha(TitleAmber, 0.90f)));

                const size_t at = name.find("ILION");
                ImFont* titleFont = TitleFont() != nullptr ? TitleFont() : ImGui::GetFont();

                if (at != std::string::npos)
                {
                    const float start = namePos.x + TitleTextSize(name.c_str(), name.c_str() + at).x;
                    const float width = TitleTextSize(name.c_str() + at, name.c_str() + at + 5).x;
                    draw->AddRectFilled(ImVec2(start - 2.0f, namePos.y - 1.0f), ImVec2(start + width + 2.0f, namePos.y + size.y + 1.0f),
                                        ImGui::GetColorU32(GraphiteDeep));
                }

                AddWindowName(draw, titleFont, ImGui::GetFontSize(), namePos, ImGui::GetColorU32(GraphiteDeep),
                              ImGui::GetColorU32(IlionYellow(GraphiteDeep)), name.c_str());
                draw->AddLine(ImVec2(min.x + 8.0f, min.y + title - 3.0f), ImVec2(max.x - 8.0f, min.y + title - 3.0f),
                              ImGui::GetColorU32(Alpha(LampGreen, 0.70f)));
                draw->AddLine(ImVec2(min.x + 8.0f, min.y + title - 1.0f), ImVec2(max.x - 8.0f, min.y + title - 1.0f),
                              ImGui::GetColorU32(Alpha(LampGreen, 0.35f)));
            }
            else if (terminal)
            {
                const std::string name = Upper(window->Name);
                AddWindowName(draw, ImGui::GetFont(), ImGui::GetFontSize(), namePos, ImGui::GetColorU32(TitleAmber),
                              ImGui::GetColorU32(IlionYellow(GraphiteDeep)), name.c_str());

                // A cursor block after the name, and a double rule under the bar
                const float x = namePos.x + ImGui::CalcTextSize(name.c_str()).x + 6.0f;
                draw->AddRectFilled(ImVec2(x, namePos.y + 1.0f), ImVec2(x + ImFloor(nameSize.y * 0.5f), namePos.y + nameSize.y - 1.0f),
                                    ImGui::GetColorU32(TitleAmber));
                draw->AddLine(ImVec2(min.x + 2.0f, min.y + title - 3.0f), ImVec2(max.x - 2.0f, min.y + title - 3.0f),
                              ImGui::GetColorU32(Alpha(LampGreen, 0.85f)));
                draw->AddLine(ImVec2(min.x + 2.0f, min.y + title - 1.0f), ImVec2(max.x - 2.0f, min.y + title - 1.0f),
                              ImGui::GetColorU32(Alpha(LampGreen, 0.45f)));
            }
            else
            {
                AddWindowName(draw, ImGui::GetFont(), ImGui::GetFontSize(), namePos, ImGui::GetColorU32(TextTitle),
                              ImGui::GetColorU32(IlionYellow(Graphite)), window->Name, nameEnd);
                draw->AddLine(ImVec2(min.x + 1.0f, min.y + title - 1.0f), ImVec2(max.x - 1.0f, min.y + title - 1.0f),
                              ImGui::GetColorU32(Alpha(LampGreen, 0.65f)));
            }
        }

        if (terminal)
        {
            // Scanlines of a phosphor screen and a double frame
            const float step = skin == Skin::Crt ? 2.0f : 3.0f;

            for (float y = min.y + title; y < max.y; y += step)
                draw->AddLine(ImVec2(min.x + 2.0f, y), ImVec2(max.x - 2.0f, y), ImGui::GetColorU32(Alpha(Text, skin == Skin::Crt ? 0.035f : 0.025f)));

            draw->AddRect(ImVec2(min.x + 1.0f, min.y + 1.0f), ImVec2(max.x - 1.0f, max.y - 1.0f), ImGui::GetColorU32(Alpha(TrimGold, 0.95f)),
                          skin == Skin::Crt ? rounding : 0.0f);
            draw->AddRect(ImVec2(min.x + 4.0f, min.y + 4.0f), ImVec2(max.x - 4.0f, max.y - 4.0f), ImGui::GetColorU32(Alpha(TrimTitanium, 0.80f)),
                          skin == Skin::Crt ? ImMax(0.0f, rounding - 3.0f) : 0.0f);

            if (skin == Skin::Crt)
            {
                // The bezel of the set: a band of aged amber plastic round the glass
                draw->AddRect(ImVec2(min.x + 1.5f, min.y + 1.5f), ImVec2(max.x - 1.5f, max.y - 1.5f),
                              ImGui::GetColorU32(Alpha(Shade(TitleAmber, 0.45f), 0.95f)), rounding, 0, 3.0f);
                draw->AddRect(ImVec2(min.x + 0.5f, min.y + 0.5f), ImVec2(max.x - 0.5f, max.y - 0.5f),
                              ImGui::GetColorU32(Alpha(TitleAmber, 0.55f)), rounding, 0, 1.0f);

                const float width = max.x - min.x, height = max.y - min.y;
                const float band = ImMin(width, height) * 0.12f;
                const ImU32 dark = ImGui::GetColorU32(ImVec4(0.0f, 0.0f, 0.0f, 0.42f));
                const ImU32 clear = ImGui::GetColorU32(ImVec4(0.0f, 0.0f, 0.0f, 0.0f));

                // The tube darkens towards its edges: the depth of curved glass
                draw->AddRectFilledMultiColor(ImVec2(min.x, min.y + title), ImVec2(max.x, min.y + title + band), dark, dark, clear, clear);
                draw->AddRectFilledMultiColor(ImVec2(min.x, max.y - band), max, clear, clear, dark, dark);
                draw->AddRectFilledMultiColor(ImVec2(min.x, min.y + title), ImVec2(min.x + band, max.y), dark, clear, clear, dark);
                draw->AddRectFilledMultiColor(ImVec2(max.x - band, min.y + title), max, clear, dark, dark, clear);

                // A soft reflection across the upper part of the glass, and the diffuse light of the phosphor
                const ImU32 shine = ImGui::GetColorU32(ImVec4(0.75f, 1.0f, 0.70f, 0.045f));
                draw->AddRectFilledMultiColor(ImVec2(min.x + band, min.y + title), ImVec2(max.x - band, min.y + title + height * 0.20f), shine, shine,
                                              clear, clear);
                draw->AddRect(ImVec2(min.x + 6.0f, min.y + title + 2.0f), ImVec2(max.x - 6.0f, max.y - 6.0f),
                              ImGui::GetColorU32(Alpha(Text, 0.05f)), ImMax(0.0f, rounding - 4.0f), 0, 6.0f);

                // Worn phosphor: faint specks, always in the same places
                for (unsigned i = 0; i < 180; i++)
                {
                    const unsigned h = MetalHash(i, 31u, 7u);
                    const float x = min.x + band * 0.5f + (float) (h % 1000u) / 1000.0f * (width - band);
                    const float y = min.y + title + (float) ((h >> 10) % 1000u) / 1000.0f * (height - title - band * 0.5f);
                    draw->AddRectFilled(ImVec2(x, y), ImVec2(x + 1.0f, y + 1.0f), ImGui::GetColorU32(Alpha(Text, 0.05f + (float) ((h >> 22) & 7u) * 0.01f)));
                }
            }
        }
        else
        {
            draw->AddRect(min, max, ImGui::GetColorU32(Metal), rounding);
        }

        draw->PopClipRect();
        return;
    }

    // Paint of the cabinet
    DrawMetal(draw, ImVec2(min.x + rim, min.y + title), ImVec2(max.x - rim, max.y - rim), 0.10f);

    // Top bar
    if (title > 0.0f)
    {
        const ImVec2 barMin(min.x + rim, min.y + rim * 0.5f), barMax(max.x - rim, min.y + title);

        draw->AddRectFilledMultiColor(ImVec2(min.x + 2.0f, min.y + 2.0f), ImVec2(max.x - 2.0f, barMax.y),
                                      ImGui::GetColorU32(Shade(Graphite, 1.35f)), ImGui::GetColorU32(Shade(Graphite, 1.35f)),
                                      ImGui::GetColorU32(GraphiteDeep), ImGui::GetColorU32(GraphiteDeep));
        DrawMetal(draw, ImVec2(min.x + 2.0f, min.y + 2.0f), ImVec2(max.x - 2.0f, barMax.y), 0.22f);

        // What is left of the bar between the rim above and the joint below is where its parts go
        const float bandTop = min.y + rim + 2.0f, bandBottom = barMax.y - 3.0f;
        const float bandHeight = bandBottom - bandTop;

        // Name plate: a raised plate of dark steel with a line of trim, the name engraved on it a little
        // larger than the text of the menu, as far as the bar has room for
        const char* nameEnd = ImGui::FindRenderedTextEnd(window->Name);
        ImFont* font = ImGui::GetFont();
        const float fontSize = ImFloor(ImMin(ImGui::GetFontSize() * 1.12f, bandHeight - 6.0f));
        const ImVec2 nameSize = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, window->Name, nameEnd);
        const float platePad = ImFloor(style.FramePadding.x * 3.0f);
        const float plateHeight = ImMin(bandHeight, nameSize.y + 8.0f);
        const float plateLeft = ImFloor(min.x + rim * 3.0f + rim);
        const float plateTop = ImFloor(bandTop + (bandHeight - plateHeight) * 0.5f);
        const ImVec2 plateMin(plateLeft, plateTop), plateMax(plateLeft + nameSize.x + platePad * 2.0f, plateTop + plateHeight);

        if (fontSize >= 8.0f && plateMax.x < barMax.x - rim * 4.0f)
        {
            draw->AddRectFilled(ImVec2(plateMin.x - 1.0f, plateMin.y - 1.0f), ImVec2(plateMax.x + 1.0f, plateMax.y + 2.0f),
                                ImGui::GetColorU32(Shadow), 2.0f);
            draw->AddRectFilledMultiColor(plateMin, plateMax, ImGui::GetColorU32(Shade(Olive, 0.95f)), ImGui::GetColorU32(Shade(Olive, 0.95f)),
                                          ImGui::GetColorU32(Shade(OliveDim, 0.70f)), ImGui::GetColorU32(Shade(OliveDim, 0.70f)));
            DrawMetal(draw, plateMin, plateMax, 0.18f);
            draw->AddRect(plateMin, plateMax, ImGui::GetColorU32(Alpha(TrimGold, 0.75f)));
            draw->AddLine(ImVec2(plateMin.x + 1.0f, plateMin.y + 1.0f), ImVec2(plateMax.x - 2.0f, plateMin.y + 1.0f),
                          ImGui::GetColorU32(Alpha(MetalLight, 0.45f)));
            draw->AddLine(ImVec2(plateMin.x + 1.0f, plateMax.y - 2.0f), ImVec2(plateMax.x - 1.0f, plateMax.y - 2.0f),
                          ImGui::GetColorU32(Alpha(Shadow, 0.70f)));

            // Engraved: the shade of the cut below and to the right of every letter
            const ImVec2 textPos(ImFloor(plateMin.x + platePad), ImFloor(plateMin.y + (plateHeight - nameSize.y) * 0.5f));

            draw->AddText(font, fontSize, ImVec2(textPos.x + 1.0f, textPos.y + 1.0f), ImGui::GetColorU32(Alpha(Shadow, 0.90f)), window->Name,
                          nameEnd);
            AddWindowName(draw, font, fontSize, textPos, ImGui::GetColorU32(ImVec4(0.96f, 0.95f, 0.86f, 1.0f)),
                          ImGui::GetColorU32(IlionYellow(Olive)), window->Name, nameEnd);
        }

        // Warning stripes: a worn grey stretch after the name, a yellow one towards the right end
        const float stripe = ImMax(3.0f, ImFloor(title * 0.16f));
        const float stripesTop = ImFloor(bandTop + bandHeight * 0.22f), stripesBottom = ImFloor(bandBottom - bandHeight * 0.22f);
        const float free = barMax.x - rim * 4.0f - plateMax.x;

        if (free > title * 5.0f)
        {
            const float greyLeft = plateMax.x + title * 0.6f;
            const float yellowRight = barMax.x - rim * 4.0f;

            DrawStripes(draw, ImVec2(greyLeft, stripesTop), ImVec2(greyLeft + ImMin(free * 0.22f, title * 3.0f), stripesBottom),
                        Alpha(Metal, 0.55f), stripe);
            DrawStripes(draw, ImVec2(yellowRight - ImMin(free * 0.30f, title * 4.5f), stripesTop), ImVec2(yellowRight, stripesBottom),
                        Alpha(StripeYellow, 0.85f), stripe);
        }

        // Where the top bar meets the cabinet
        draw->AddLine(ImVec2(barMin.x, barMax.y - 1.0f), ImVec2(barMax.x, barMax.y - 1.0f), ImGui::GetColorU32(Shadow));
        draw->AddLine(ImVec2(barMin.x, barMax.y), ImVec2(barMax.x, barMax.y), ImGui::GetColorU32(Alpha(TrimTitanium, 0.55f)));
    }

    // Rim: a thick band of dark steel following the round corners, lit along its upper and left sides
    const float half = rim * 0.5f;

    draw->AddRect(ImVec2(min.x + half, min.y + half), ImVec2(max.x - half, max.y - half), ImGui::GetColorU32(Shade(MetalDark, 1.18f)),
                  rounding, ImDrawFlags_None, rim);

    // Its inner half falls away from the light: the ridge of a bevel
    draw->AddRect(ImVec2(min.x + rim * 0.75f, min.y + rim * 0.75f), ImVec2(max.x - rim * 0.75f, max.y - rim * 0.75f),
                  ImGui::GetColorU32(Shade(MetalDark, 0.78f)), ImMax(0.0f, rounding - rim * 0.5f), ImDrawFlags_None, rim * 0.5f);
    draw->AddRect(min, max, ImGui::GetColorU32(Shadow), rounding, ImDrawFlags_None, 1.5f);
    draw->AddRect(ImVec2(min.x + 1.0f, min.y + 1.0f), ImVec2(max.x - 1.0f, max.y - 1.0f), ImGui::GetColorU32(Alpha(MetalLight, 0.22f)),
                  ImMax(0.0f, rounding - 1.0f));
    draw->AddLine(ImVec2(min.x + rounding, min.y + 1.0f), ImVec2(max.x - rounding, min.y + 1.0f), ImGui::GetColorU32(Alpha(MetalLight, 0.65f)));
    draw->AddLine(ImVec2(min.x + 1.0f, min.y + rounding), ImVec2(min.x + 1.0f, max.y - rounding), ImGui::GetColorU32(Alpha(MetalLight, 0.40f)));
    draw->AddLine(ImVec2(min.x + rounding, max.y - 2.0f), ImVec2(max.x - rounding, max.y - 2.0f), ImGui::GetColorU32(Alpha(Shadow, 0.85f)));

    // Trim: the fine yellow line inside the rim, with a dark seam on its outer side
    const float trimRounding = ImMax(0.0f, rounding - rim);

    draw->AddRect(ImVec2(min.x + rim - 1.0f, min.y + rim - 1.0f), ImVec2(max.x - rim + 1.0f, max.y - rim + 1.0f),
                  ImGui::GetColorU32(Shadow), trimRounding + 1.0f);
    draw->AddRect(ImVec2(min.x + rim, min.y + rim), ImVec2(max.x - rim, max.y - rim), ImGui::GetColorU32(Alpha(TrimGold, 0.70f)),
                  trimRounding);

    // Corner plates. The upper ones are as high as the top bar lets them, the lower ones stay inside the margin
    // so they never sit under a control.
    const float upperPlate = title > 0.0f ? ImMin(ImFloor(rim * 3.0f), title - 2.0f) : window->WindowPadding.x;
    const float lowerPlate = window->WindowPadding.x + 1.0f;

    DrawCornerPlate(draw, min, upperPlate, -1, -1);
    DrawCornerPlate(draw, ImVec2(max.x, min.y), upperPlate, 1, -1);
    DrawCornerPlate(draw, ImVec2(min.x, max.y), lowerPlate, -1, 1);
    DrawCornerPlate(draw, max, lowerPlate, 1, 1);

    draw->PopClipRect();
}

// A joint across the chassis at the height where drawing is now: the line where the bottom band of the menu
// meets the rest of the cabinet.
inline void DrawChassisJoint()
{
    if (!industrial)
        return;

    ImGuiWindow* window = ImGui::GetCurrentWindow();
    const ImGuiStyle& style = ImGui::GetStyle();
    const float rim = ImMax(4.0f, ImFloor(window->WindowPadding.x * 0.66f));
    const float y = ImFloor(window->DC.CursorPos.y - style.ItemSpacing.y * 0.5f) - 1.0f;
    const float left = window->Pos.x + rim + 1.0f, right = window->Pos.x + window->Size.x - rim - 1.0f;

    window->DrawList->PushClipRect(window->Pos, ImVec2(window->Pos.x + window->Size.x, window->Pos.y + window->Size.y), false);

    if (skin == Skin::Industrial)
    {
        window->DrawList->AddLine(ImVec2(left, y), ImVec2(right, y), ImGui::GetColorU32(Shadow));
        window->DrawList->AddLine(ImVec2(left, y + 1.0f), ImVec2(right, y + 1.0f), ImGui::GetColorU32(Alpha(TrimTitanium, 0.45f)));
    }
    else
    {
        window->DrawList->AddLine(ImVec2(left, y), ImVec2(right, y), ImGui::GetColorU32(TerminalLike() ? Alpha(TrimGold, 0.85f) : MetalDark));
    }

    window->DrawList->PopClipRect();
}

// How much of the corners of a module plate is cut off
inline float CardCut() { return ImFloor(ImGui::GetStyle().WindowPadding.x * 0.75f); }

// Body of a card: a module plate set into the chassis. Dark steel with its corners cut off, a seat around it,
// a frame of yellow trim with a second fainter line of titanium inside it, and a heavier bracket of trim at every
// corner.
inline void DrawCardBody(ImDrawList* draw, const ImVec2& min, const ImVec2& max)
{
    // Fallout 2: a plate with cut corners framed by a double line, an amber notch at two corners
    if (skin == Skin::Crt)
    {
        const float cut = ImMin(8.0f, ImMin(max.x - min.x, max.y - min.y) * 0.2f);
        ImVec2 points[8];

        ChamferPoints(points, min, max, cut);
        draw->AddConvexPolyFilled(points, 8, ImGui::GetColorU32(Alpha(Panel, 0.88f)));
        DrawChamferFrame(draw, min, max, cut, ImGui::GetColorU32(Alpha(TrimGold, 0.85f)), 1.0f);
        DrawChamferFrame(draw, ImVec2(min.x + 3.0f, min.y + 3.0f), ImVec2(max.x - 3.0f, max.y - 3.0f), ImMax(1.0f, cut - 1.5f),
                         ImGui::GetColorU32(Alpha(TrimTitanium, 0.55f)), 1.0f);

        const ImU32 amber = ImGui::GetColorU32(Alpha(LampAmber, 0.85f));
        draw->AddLine(ImVec2(min.x, min.y + cut), ImVec2(min.x + cut, min.y), amber, 2.0f);
        draw->AddLine(ImVec2(max.x - cut, max.y), ImVec2(max.x, max.y - cut), amber, 2.0f);
        return;
    }

    // Terminal: a box in the dim green line with amber corner ticks
    if (TerminalLike())
    {
        const float tick = ImMin(10.0f, ImMin(max.x - min.x, max.y - min.y) * 0.25f);
        const ImU32 amber = ImGui::GetColorU32(LampGreen);

        draw->AddRectFilled(min, max, ImGui::GetColorU32(Alpha(Panel, 0.85f)));
        draw->AddRect(min, max, ImGui::GetColorU32(Alpha(TrimGold, 0.80f)));

        for (int corner = 0; corner < 4; corner++)
        {
            const float x = corner % 2 == 0 ? min.x : max.x, y = corner < 2 ? min.y : max.y;
            const float dx = corner % 2 == 0 ? tick : -tick, dy = corner < 2 ? tick : -tick;

            draw->AddLine(ImVec2(x, y), ImVec2(x + dx, y), amber, 2.0f);
            draw->AddLine(ImVec2(x, y), ImVec2(x, y + dy), amber, 2.0f);
        }

        return;
    }

    // Flat: a light grey card with round corners, a soft shadow under it and a fine border
    if (skin == Skin::Flat)
    {
        draw->AddRectFilled(ImVec2(min.x + 1.0f, min.y + 2.0f), ImVec2(max.x + 1.0f, max.y + 3.0f), ImGui::GetColorU32(ImVec4(0.0f, 0.0f, 0.0f, 0.10f)), 7.0f);
        draw->AddRectFilled(min, max, ImGui::GetColorU32(Panel), 6.0f);
        draw->AddRect(min, max, ImGui::GetColorU32(Metal), 6.0f);
        return;
    }

    const float cut = CardCut();
    ImVec2 points[8];

    // Seat in the cabinet: a dark edge all around, light caught by its lower lip
    DrawChamferFrame(draw, ImVec2(min.x - 1.0f, min.y - 1.0f), ImVec2(max.x + 1.0f, max.y + 1.0f), cut, ImGui::GetColorU32(Shadow), 2.0f);
    draw->AddLine(ImVec2(min.x + cut, max.y + 2.0f), ImVec2(max.x - cut, max.y + 2.0f), ImGui::GetColorU32(Alpha(MetalLight, 0.14f)));

    // Plate
    ChamferPoints(points, min, max, cut);
    draw->AddConvexPolyFilled(points, 8, ImGui::GetColorU32(Alpha(Shade(Graphite, 0.92f), 0.90f)));
    draw->AddRectFilledMultiColor(ImVec2(min.x + cut, min.y), ImVec2(max.x - cut, max.y), ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, 0.035f)),
                                  ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, 0.035f)), ImGui::GetColorU32(ImVec4(0.0f, 0.0f, 0.0f, 0.12f)),
                                  ImGui::GetColorU32(ImVec4(0.0f, 0.0f, 0.0f, 0.12f)));
    DrawMetal(draw, ImVec2(min.x + cut, min.y + 1.0f), ImVec2(max.x - cut, max.y - 1.0f), 0.12f);

    // Trim: the frame, a fainter line inside it, and the brackets
    const ImU32 trim = ImGui::GetColorU32(Alpha(TrimGold, 0.85f));

    DrawChamferFrame(draw, min, max, cut, trim);
    DrawChamferFrame(draw, ImVec2(min.x + 3.0f, min.y + 3.0f), ImVec2(max.x - 3.0f, max.y - 3.0f), ImMax(1.0f, cut - 1.5f),
                     ImGui::GetColorU32(Alpha(TrimTitanium, 0.60f)));

    const float bracket = ImMin(cut * 2.2f, ImMin(max.x - min.x, max.y - min.y) * 0.25f);
    const ImU32 heavy = ImGui::GetColorU32(TrimGold);

    DrawCornerBracket(draw, min, max, cut, bracket, -1, -1, heavy);
    DrawCornerBracket(draw, min, max, cut, bracket, 1, -1, heavy);
    DrawCornerBracket(draw, min, max, cut, bracket, -1, 1, heavy);
    DrawCornerBracket(draw, min, max, cut, bracket, 1, 1, heavy);
}

// Name band across the top of a card: darker steel under the name, a line of trim below it, and a small mark of
// stripes at its right end.
inline void DrawCardBand(ImDrawList* draw, float left, float top, float right, float bottom)
{
    // Flat: a darker band under the name with an accent line, so every card has a clear head
    if (skin == Skin::Flat)
    {
        draw->AddRectFilled(ImVec2(left + 1.0f, top + 1.0f), ImVec2(right - 1.0f, bottom), ImGui::GetColorU32(Alpha(GraphiteDeep, 0.55f)), 6.0f,
                            ImDrawFlags_RoundCornersTop);
        draw->AddLine(ImVec2(left + 1.0f, bottom), ImVec2(right - 1.0f, bottom), ImGui::GetColorU32(Alpha(LampGreen, 0.60f)));
        return;
    }

    // Fallout 2: the head of the card in inverse video, following its cut corner
    if (skin == Skin::Crt)
    {
        const float cut = ImMin(8.0f, (bottom - top) * 0.6f);
        const ImVec2 points[5] = { ImVec2(left + cut, top), ImVec2(right - 1.0f, top), ImVec2(right - 1.0f, bottom),
                                   ImVec2(left, bottom), ImVec2(left, top + cut) };

        draw->AddConvexPolyFilled(points, 5, ImGui::GetColorU32(Alpha(LampGreen, 0.80f)));
        return;
    }

    // Terminal: an amber rule under the name
    if (skin != Skin::Industrial)
    {
        draw->AddLine(ImVec2(left + 6.0f, bottom), ImVec2(right - 6.0f, bottom),
                      ImGui::GetColorU32(TerminalLike() ? Alpha(LampGreen, 0.70f) : MetalDark));
        return;
    }

    const float cut = CardCut();

    // Follows the cut corners of the plate it sits on
    const ImVec2 points[6] = { ImVec2(left + cut, top), ImVec2(right - cut, top), ImVec2(right, top + cut),
                               ImVec2(right, bottom),   ImVec2(left, bottom),     ImVec2(left, top + cut) };

    draw->AddConvexPolyFilled(points, 6, ImGui::GetColorU32(Alpha(GraphiteDeep, 0.92f)));
    DrawMetal(draw, ImVec2(left + cut, top + 1.0f), ImVec2(right - cut, bottom), 0.16f);

    const float height = bottom - top;
    const float mark = ImFloor(height * 1.6f);

    if (right - left > mark * 8.0f)
    {
        DrawStripes(draw, ImVec2(right - cut - mark - 4.0f, top + ImFloor(height * 0.30f)),
                    ImVec2(right - cut - 4.0f, bottom - ImFloor(height * 0.30f)), Alpha(StripeYellow, 0.75f), ImMax(2.0f, ImFloor(height * 0.14f)));
    }

    draw->AddLine(ImVec2(left + 3.0f, bottom - 1.0f), ImVec2(right - 3.0f, bottom - 1.0f), ImGui::GetColorU32(Shadow));
    draw->AddLine(ImVec2(left + 3.0f, bottom), ImVec2(right - 3.0f, bottom), ImGui::GetColorU32(Alpha(TrimGold, 0.80f)));
}

// --- Sidebar -------------------------------------------------------------------------------------------------

// Name of a group of tabs: engraved text over a rule, no lamp, nothing to press
inline void GroupPlate(const char* text)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();

    if (skin == Skin::Crt)
    {
        // Fallout 2: the group name in amber over a double rule
        const std::string name = Upper(text);
        const ImVec2 pos = window->DC.CursorPos;
        const ImVec2 size = TitleTextSize(name.c_str());

        ImGui::Dummy(size);
        AddTitleText(window->DrawList, pos, ImGui::GetColorU32(TitleAmber), name.c_str());

        const float y = ImGui::GetItemRectMax().y + 1.0f;
        window->DrawList->AddLine(ImVec2(pos.x, y), ImVec2(window->WorkRect.Max.x, y), ImGui::GetColorU32(Alpha(TrimGold, 0.85f)));
        window->DrawList->AddLine(ImVec2(pos.x, y + 2.0f), ImVec2(window->WorkRect.Max.x, y + 2.0f),
                                  ImGui::GetColorU32(Alpha(TrimTitanium, 0.70f)));
        ImGui::Dummy(ImVec2(0.0f, 4.0f));
        return;
    }

    if (TerminalLike())
    {
        ImGui::TextColored(TitleAmber, "%s", ("> " + Upper(text)).c_str());
        ImGui::Dummy(ImVec2(0.0f, 2.0f));
        return;
    }

    if (skin == Skin::Flat)
    {
        ImGui::TextColored(TextDim, "%s", Upper(text).c_str());
        ImGui::Dummy(ImVec2(0.0f, 2.0f));
        return;
    }

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
    const float textY = min.y + (max.y - min.y - ImGui::GetTextLineHeight()) * 0.5f;

    // Fallout 2: the open tab as a phosphor block with cut corners and an amber bar, the others plain names
    if (skin == Skin::Crt)
    {
        ImVec2 points[8];
        const float cut = ImMin(5.0f, ImFloor((max.y - min.y) * 0.25f));
        ChamferPoints(points, min, max, cut);

        if (selected)
        {
            draw->AddConvexPolyFilled(points, 8, ImGui::GetColorU32(Alpha(LampGreen, held ? 0.70f : 0.85f)));
            draw->AddRectFilled(ImVec2(min.x + 1.0f, min.y + cut), ImVec2(min.x + 4.0f * scale, max.y - cut), ImGui::GetColorU32(LampAmber));
        }
        else if (hovered)
        {
            draw->AddPolyline(points, 8, ImGui::GetColorU32(Alpha(LampAmber, 0.85f)), ImDrawFlags_Closed, 1.0f);
        }

        AddTitleText(draw, ImVec2(min.x + 10.0f * scale, textY), ImGui::GetColorU32(selected ? GraphiteDeep : hovered ? Text : TextDim),
                     Upper(text).c_str());
        return;
    }

    // Terminal: the open tab is the line shown in reverse (amber with dark letters), "> " before the others
    if (TerminalLike())
    {
        if (selected)
            draw->AddRectFilled(min, max, ImGui::GetColorU32(LampGreen));
        else if (hovered)
            draw->AddRect(min, max, ImGui::GetColorU32(Alpha(LampGreen, 0.80f)));

        const std::string label = (selected ? "> " : "  ") + Upper(text);
        draw->AddText(ImVec2(min.x + 6.0f * scale, textY), ImGui::GetColorU32(selected ? GraphiteDeep : hovered ? Text : TextDim),
                      label.c_str());
        return;
    }

    // Flat: a light tint for the open tab with an accent bar at its left, a grey one under the pointer
    if (skin == Skin::Flat)
    {
        if (selected)
        {
            draw->AddRectFilled(min, max, ImGui::GetColorU32(Alpha(LampGreen, 0.12f)), 4.0f);
            draw->AddRectFilled(min, ImVec2(min.x + 3.0f, max.y), ImGui::GetColorU32(LampGreen), 2.0f);
        }
        else if (hovered)
        {
            draw->AddRectFilled(min, max, ImGui::GetColorU32(Alpha(Text, 0.05f)), 4.0f);
        }

        draw->AddText(ImVec2(min.x + 12.0f * scale, textY), ImGui::GetColorU32(selected ? TextTitle : Text), text);
        return;
    }

    DrawKey(draw, bounds, selected ? OliveLit : hovered ? Olive : OliveDim, hovered, held);

    if (selected)
        draw->AddRect(min, max, ImGui::GetColorU32(EdgeAmber), 2.0f);

    const float height = max.y - min.y;

    DrawLamp(draw, ImVec2(ImFloor(min.x + height * 0.5f + 1.0f), ImFloor((min.y + max.y) * 0.5f)), ImFloor(height * 0.5f) - 4.0f,
             selected, hovered, held);

    draw->AddText(ImVec2(min.x + height + 4.0f * scale, min.y + (height - ImGui::GetTextLineHeight()) * 0.5f),
                  ImGui::GetColorU32(selected ? Text : hovered ? Text : TextDim), text);
}

// Size of a key of a selector for words of up to textWidth: room around the word on every side. Square keys
// (the two letters of a language) take the larger of both measures for both.
inline ImVec2 SelectorKeySize(float textWidth, bool square)
{
    const ImGuiStyle& style = ImGui::GetStyle();
    const float height = ImFloor(ImGui::GetTextLineHeight() + style.FramePadding.y * 2.0f + 4.0f);
    const float width = ImFloor(textWidth + (square ? style.FramePadding.x * 2.0f + 2.0f : style.FramePadding.x * 5.0f));

    if (square)
        return ImVec2(ImMax(width, height), ImMax(width, height));

    return ImVec2(width, height);
}

// A key of a selector (language, mode) over its whole rectangle. The chosen one is lit.
inline void DrawSelectorKey(ImDrawList* draw, const ImRect& bounds, bool selected, bool hovered)
{
    const ImVec4 chosen = skin == Skin::Industrial ? ImVec4(0.12f, 0.52f, 0.08f, 1.0f)
                          : skin == Skin::Crt ? ImVec4(0.55f, 0.30f, 0.06f, 1.0f)
                          : TerminalLike() ? Alpha(LampGreen, 0.45f)
                                                   : Alpha(LampGreen, 0.22f);

    DrawKey(draw, bounds, selected ? chosen : hovered ? Olive : OliveDim, hovered, false);
}

// The bar between the two keys of a selector, in the middle of their height. With height 0 it is the plain
// text it always was.
inline void SelectorBar(float height)
{
    if (height <= 0.0f)
    {
        ImGui::TextDisabled("|");
        return;
    }

    const ImVec2 size = ImGui::CalcTextSize("|");
    const ImVec2 pos = ImGui::GetCursorScreenPos();

    ImGui::Dummy(ImVec2(size.x, height));
    ImGui::GetWindowDrawList()->AddText(ImVec2(pos.x, ImFloor(pos.y + (height - size.y) * 0.5f)),
                                        ImGui::GetColorU32(ImGuiCol_TextDisabled), "|");
}
} // namespace IlionTheme
