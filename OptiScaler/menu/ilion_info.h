#pragma once

// ILION: the INFO line at the bottom of the menu. One message at a time, typed character by character like a
// teletype on an old green phosphor screen, the technical words in amber.
//
// Every message was checked against ILION's code and the documentation of the technology before going in (see the
// comment next to each one). A message that couldn't be backed up isn't here. Words between braces are drawn in amber;
// the Spanish text comes from the translation table and keeps the braces.

#include <imgui/imgui.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <string>
#include <vector>

namespace IlionInfo
{
inline constexpr const char* kMessages[] = {
    // Menu: OptiFG needs the upscaler active, is off in Vulkan, needs the FG output dll; its own tip says HUDfix
    "No native Frame Generation? With an upscaler active in ILION, try {OptiFG} as FG input. If the HUD smears, enable "
    "{HUDFix}.",
    // IlionUpscalers: the lossy Lite build names its provider "-lossy-"; no official documentation of the build
    "{FSR 4 Lite} is a community build trading some image quality for FPS; results vary per game. Compare it in "
    "{Benchmark}.",
    // IlionUpscalers: FFX dlls switch while the game runs, d4r is loaded at start
    "In DirectX 12, {FSR 4} and {HelixSR} versions switch without restarting; {DLSS 4} (d4r) needs a restart.",
    // d4r's documentation (Windows: DX12, models K and M, no FG); external backend installed by the user
    "RDNA 3: {DLSS 4} (model K) via {d4r} is experimental. Installed separately, needs a game restart. DirectX 12 "
    "only, no FG.",
    // HelixSR's documentation; ILION checks the file and the running dll, not the version
    "{HelixSR} poses as FSR 3.1 and runs a {DLSS 3.7} network, per its author. DirectX 12 only.",
    // XeFG_Dx12: XeFG makes fakenvapi create XeLL; if XeLL can't start, XeFG doesn't either
    "With {XeFG}, OptiScaler selects {XeLL} as latency reducer; Anti-Lag 2 and LatencyFleX aren't used meanwhile.",
    // HUDFix is in the OptiFG settings only; DispatchFlags are only sent to DLSS Enabler's DLSSG replacement
    "HUD trails with {OptiFG}? Try {HUDFix}. {Dispatch Flags} anti-ghosting only works with the {DLSS Enabler} FG.",
    // XeFG_Dx12: up to the runtime's maximum; FSR FG has no frame count setting; DLSSG count comes from the game / SL
    "Real multipliers: {XeFG} up to what its runtime allows; OptiScaler's {FSR FG} makes 1 frame (2x); multi {DLSSG} "
    "depends on the game and Streamline.",
    // IlionBenchmark / IlionCompare: average FPS, 1% low, stutters, noise margins; no image quality
    "Which FSR 4? ILION's {Benchmark} compares average FPS, 1% low and stutters between versions. Repeat each test "
    "under equal conditions. It doesn't measure image quality.",
};

inline constexpr int kCount = (int) (sizeof(kMessages) / sizeof(kMessages[0]));

// A piece of a message in one colour
struct Run
{
    std::string text;
    bool amber = false;
};

// Splits a message at its braces
inline std::vector<Run> Parse(const char* message)
{
    std::vector<Run> runs;
    Run run;

    for (const char* c = message; *c != 0; c++)
    {
        if (*c == '{' || *c == '}')
        {
            if (!run.text.empty())
                runs.push_back(run);

            run = Run {};
            run.amber = *c == '{';
            continue;
        }

        run.text += *c;
    }

    if (!run.text.empty())
        runs.push_back(run);

    return runs;
}

// Characters (not bytes) of a UTF-8 text
inline int CountChars(const std::string& text)
{
    int count = 0;
    for (const char c : text)
        count += ((unsigned char) c & 0xC0) != 0x80;
    return count;
}

// Bytes of the first `chars` characters
inline size_t BytesOf(const std::string& text, int chars)
{
    size_t i = 0;
    while (i < text.size() && chars > 0)
    {
        i++;
        while (i < text.size() && ((unsigned char) text[i] & 0xC0) == 0x80)
            i++;
        chars--;
    }
    return i;
}

// Where the line is: which message, since when it is shown
struct Clock
{
    int message = 0;
    double start = -1.0;
};

inline Clock line;

inline constexpr double kCharsPerSecond = 38.0;
inline constexpr double kPan = 70.0; // pixels per second a long message slides when it doesn't fit

// Draws the line in the rectangle given. `on` false: the glass stays, empty, and nothing is updated.
inline void Draw(ImDrawList* draw, const ImVec2& min, const ImVec2& max, bool on, ImFont* font, float fontSize,
                 const char* (*translate)(const char*), const ImVec4& phosphor, const ImVec4& amber,
                 const ImVec4& glass, const ImVec4& rim)
{
    const float rounding = 3.0f;

    // The glass: dark tube, a lighter band at the top (the curve), fine scan lines, the rim
    draw->AddRectFilled(min, max, ImGui::GetColorU32(glass), rounding);
    draw->AddRectFilledMultiColor(ImVec2(min.x + 1.0f, min.y + 1.0f), ImVec2(max.x - 1.0f, min.y + (max.y - min.y) * 0.45f),
                                  ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, 0.05f)), ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, 0.05f)),
                                  ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, 0.0f)), ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, 0.0f)));

    const ImU32 scan = ImGui::GetColorU32(ImVec4(0.0f, 0.0f, 0.0f, 0.22f));
    for (float y = min.y + 2.0f; y < max.y - 1.0f; y += 3.0f)
        draw->AddLine(ImVec2(min.x + 2.0f, y), ImVec2(max.x - 2.0f, y), scan);

    draw->AddRect(min, max, ImGui::GetColorU32(rim), rounding);

    if (!on)
    {
        line.start = -1.0; // starts again from its message when switched back on
        return;
    }

    const double now = ImGui::GetTime();
    if (line.start < 0.0)
        line.start = now;

    const auto runs = Parse(translate(kMessages[line.message % kCount]));

    int total = 0;
    float fullWidth = 0.0f;
    for (const auto& run : runs)
    {
        total += CountChars(run.text);
        fullWidth += font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, run.text.c_str()).x;
    }

    const float pad = std::floor(fontSize * 0.5f);
    const float inner = (max.x - min.x) - pad * 2.0f;
    const float overflow = std::max(0.0f, fullWidth - inner);

    // Typing, then the message stays: long enough to read it, plus the slide when it doesn't fit
    const double elapsed = now - line.start;
    const double typing = total / kCharsPerSecond;
    const double slide = overflow > 0.0f ? overflow / kPan : 0.0;
    const double hold = 6.0 + slide * 2.0 + 2.0;

    if (elapsed > typing + hold)
    {
        line.message = (line.message + 1) % kCount;
        line.start = now;
        return;
    }

    const int shown = std::min(total, (int) (elapsed * kCharsPerSecond));

    // Typed width, so the line can follow the carriage while it types
    float typedWidth = 0.0f;
    {
        int left = shown;
        for (const auto& run : runs)
        {
            if (left <= 0)
                break;

            const int chars = std::min(left, CountChars(run.text));
            const auto part = run.text.substr(0, BytesOf(run.text, chars));
            typedWidth += font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, part.c_str()).x;
            left -= chars;
        }
    }

    float offset = 0.0f;
    if (shown < total)
    {
        offset = std::max(0.0f, typedWidth + fontSize - inner); // keep the carriage in sight
    }
    else if (overflow > 0.0f)
    {
        // Shown whole: back to the start, then a slow slide to the end and a stop there
        const double t = elapsed - typing;
        if (t < 2.0)
            offset = overflow * (float) std::max(0.0, 1.0 - t / 1.0);
        else
            offset = overflow * (float) std::min(1.0, (t - 2.0 - 2.0) / std::max(slide, 0.001));

        offset = std::clamp(offset, 0.0f, overflow);
    }

    const float y = std::floor(min.y + ((max.y - min.y) - fontSize) * 0.5f);
    float x = std::floor(min.x + pad - offset);

    draw->PushClipRect(ImVec2(min.x + 1.0f, min.y + 1.0f), ImVec2(max.x - 1.0f, max.y - 1.0f), true);

    int left = shown;
    for (const auto& run : runs)
    {
        if (left <= 0)
            break;

        const int chars = std::min(left, CountChars(run.text));
        const auto part = run.text.substr(0, BytesOf(run.text, chars));
        const ImVec4& colour = run.amber ? amber : phosphor;

        // A faint glow under the letters, then the letters
        draw->AddText(font, fontSize, ImVec2(x, y + 0.5f), ImGui::GetColorU32(ImVec4(colour.x, colour.y, colour.z, 0.25f)),
                      part.c_str());
        draw->AddText(font, fontSize, ImVec2(x, y), ImGui::GetColorU32(colour), part.c_str());

        x += font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, part.c_str()).x;
        left -= chars;
    }

    // The carriage: a block that blinks while typing
    if (shown < total && std::fmod(now, 0.6) < 0.35)
    {
        const float w = std::floor(fontSize * 0.5f);
        draw->AddRectFilled(ImVec2(x + 1.0f, y + 1.0f), ImVec2(x + 1.0f + w, y + fontSize - 1.0f), ImGui::GetColorU32(phosphor));
    }

    draw->PopClipRect();
}
} // namespace IlionInfo
