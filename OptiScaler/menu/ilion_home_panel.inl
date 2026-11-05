// ILION AL DÍA + ILION TERMINAL: the box of Home that ILION Advisor used (Full: the centre of the header; Compact: the
// second cell of its header). Advisor's suggestion was replaced by PERFILES; its facts are still gathered as before.
// Only this box changes: nothing here touches the game, a profile, the upscaler or FG. Its preferences live in
// %LOCALAPPDATA%\ILION\inicio.json, never in OptiScaler.ini.

#include <shellapi.h>

#include <climits>
#include <cmath>

#include <misc/IlionNews.h>
#include <misc/IlionTerminal.h>

#pragma execution_character_set(push, "utf-8")

namespace IlionShell
{
namespace HomePanel
{
// ---- Preferences --------------------------------------------------------------------------------------------------
struct Prefs
{
    int mode = 0;       // 0 AL DÍA (default), 1 TERMINAL
    bool rotate = true; // AL DÍA: the blocks change every 15 s
    bool animate = true; // TERMINAL: the typewriter start
    int view = 0;       // TERMINAL: 0 GPU, 1 CPU, 2 MEM, 3 SYS
};

static Prefs prefs;
static bool prefsLoaded = false;

static std::filesystem::path PrefsPath() { return IlionCommunity::Root() / L"inicio.json"; }

static void LoadPrefs()
{
    prefsLoaded = true;
    std::ifstream f(PrefsPath(), std::ios::binary);
    if (!f)
        return;
    const auto j = IlionCommunity::Json::parse(f, nullptr, false);
    if (!j.is_object())
        return;
    prefs.mode = std::clamp(j.value("modo", 0), 0, 1);
    prefs.rotate = j.value("rotacion", true);
    prefs.animate = j.value("animacion", true);
    prefs.view = std::clamp(j.value("vista", 0), 0, 3);
}

static void SavePrefs()
{
    const IlionCommunity::Json j { { "modo", prefs.mode }, { "rotacion", prefs.rotate }, { "animacion", prefs.animate }, { "vista", prefs.view } };
    IlionCommunity::WriteAtomic(PrefsPath(), j.dump(1));
}

// ---- Drawing helpers (screen coordinates) ----------------------------------------------------------------------------
static float TextW(ImFont* f, float size, const std::string& s) { return f->CalcTextSizeA(size, FLT_MAX, 0.0f, s.c_str()).x; }

// The text cut to a width, with an ellipsis
static std::string Fit(ImFont* f, float size, const std::string& s, float width)
{
    if (TextW(f, size, s) <= width)
        return s;
    std::string cut = s;
    while (!cut.empty() && TextW(f, size, cut + "…") > width)
    {
        // One whole UTF-8 character at a time: continuation bytes, then the byte that starts it
        unsigned char last = 0;
        do
        {
            last = (unsigned char) cut.back();
            cut.pop_back();
        } while (!cut.empty() && (last & 0xC0) == 0x80);
    }
    return cut + "…";
}

// A small clickable text: true when pressed
static bool Link(const char* id, ImVec2 pos, ImFont* f, float size, const std::string& text, ImU32 colour, ImU32 hover, ImDrawList* dl)
{
    const ImVec2 sz(TextW(f, size, text), size);
    ImGui::SetCursorScreenPos(pos);
    const bool pressed = ImGui::InvisibleButton(id, ImVec2((std::max)(sz.x, 1.0f), sz.y));
    dl->AddText(f, size, pos, ImGui::IsItemHovered() ? hover : colour, text.c_str());
    return pressed;
}

static std::string Ago(long long t)
{
    const long long d = (long long) std::time(nullptr) - t;
    if (d < 0)
        return "ahora";
    if (d < 3600)
        return StrFmt("hace %lld min", (std::max)(1LL, d / 60));
    if (d < 86400)
        return StrFmt("hace %lld h", d / 3600);
    return StrFmt("hace %lld d", d / 86400);
}

static std::string DayMonth(long long t)
{
    std::tm tm {};
    const time_t tt = (time_t) t;
    if (localtime_s(&tm, &tt) != 0)
        return std::string();
    return StrFmt("%02d/%02d", tm.tm_mday, tm.tm_mon + 1);
}

static void OpenLink(const std::string& url)
{
    if (url.rfind("https://", 0) != 0 && url.rfind("http://", 0) != 0)
        return;
    std::wstring w(url.begin(), url.end()); // validated ASCII link from the feed
    for (wchar_t& c : w)
        if (c < 0x20)
            return;
    ShellExecuteW(nullptr, L"open", w.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

// ---- ILION AL DÍA ------------------------------------------------------------------------------------------------------
static int shownBlock = 0, nextBlock = 0;
static double blockSince = 0.0, fadeStart = -10.0;
static constexpr double kBlockSeconds = 15.0, kFadeSeconds = 0.4;

static void GoToBlock(int b, double now)
{
    nextBlock = (b + IlionNews::BlockCount) % IlionNews::BlockCount;
    fadeStart = now;
}

static void DrawNews(ImDrawList* dl, ImVec2 a, ImVec2 b, bool hovered)
{
    auto& news = IlionNews::Service::Instance();
    news.KeepAlive();
    const auto snap = news.Get();
    const double now = ImGui::GetTime();
    const float s = U(1.0f);
    ImFont* f = fUi;

    // Rotation: 15 s per block, paused under the mouse, 400 ms cross-fade
    if (blockSince == 0.0)
        blockSince = now;
    if (fadeStart < 0.0 && prefs.rotate && !hovered && now - blockSince >= kBlockSeconds)
        GoToBlock(shownBlock + 1, now);
    float alpha = 1.0f;
    if (fadeStart >= 0.0)
    {
        const double t = now - fadeStart;
        if (t >= kFadeSeconds * 0.5 && shownBlock != nextBlock)
            shownBlock = nextBlock;
        if (t >= kFadeSeconds)
            fadeStart = -10.0, blockSince = now;
        alpha = t < kFadeSeconds * 0.5 ? (float) (1.0 - t / (kFadeSeconds * 0.5)) : (float) ((t - kFadeSeconds * 0.5) / (kFadeSeconds * 0.5));
        alpha = std::clamp(alpha, 0.0f, 1.0f);
    }

    // Block row: name, indicators, arrows, auto rotation, freshness
    const float y0 = a.y;
    const float fs = std::floor(12.f * s);
    dl->AddText(f, fs, ImVec2(a.x, y0), Col(pal::Yellow), IlionNews::BlockName(shownBlock));
    float x = a.x + TextW(f, fs, IlionNews::BlockName(shownBlock)) + 8 * s;
    for (int i = 0; i < IlionNews::BlockCount; i++)
    {
        const ImVec2 c(x + 4 * s + i * 11 * s, y0 + fs * 0.55f);
        if (i == shownBlock)
            dl->AddCircleFilled(c, 3.0f * s, Col(pal::Green));
        else
            dl->AddCircle(c, 3.0f * s, Col(pal::Dim), 0, 1.0f);
    }
    x += 3 * 11 * s + 10 * s;
    if (Link("##NewsPrev", ImVec2(x, y0 - 1 * s), f, fs + 2 * s, "‹", Col(pal::Text2), Col(pal::Green), dl))
        GoToBlock(shownBlock - 1, now);
    x += 14 * s;
    if (Link("##NewsNext", ImVec2(x, y0 - 1 * s), f, fs + 2 * s, "›", Col(pal::Text2), Col(pal::Green), dl))
        GoToBlock(shownBlock + 1, now);
    x += 16 * s;
    if (Link("##NewsAuto", ImVec2(x, y0), f, fs, prefs.rotate ? "AUTO" : "PAUSA", Col(prefs.rotate ? pal::Green : pal::Orange), Col(pal::Text), dl))
    {
        prefs.rotate = !prefs.rotate;
        SavePrefs();
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", prefs.rotate ? "Rotación automática cada 15 s (se detiene con el ratón encima). Pulsa para pausarla"
                                             : "Rotación pausada: usa las flechas. Pulsa para reanudarla");
    const std::string fresh = snap.busy && snap.fetched == 0 ? std::string("conectando…")
                              : snap.fetched == 0           ? std::string(snap.offline ? "sin conexión, sin caché" : "sin datos todavía")
                              : snap.offline                ? "sin conexión · caché " + Ago(snap.fetched)
                                                            : "act. " + Ago(snap.fetched);
    const float fs2 = std::floor(10.5f * s);
    dl->AddText(f, fs2, ImVec2(b.x - TextW(f, fs2, fresh), y0 + 1 * s), Col(snap.offline ? pal::Orange : pal::Dim), fresh.c_str());
    if (ImGui::IsMouseHoveringRect(ImVec2(b.x - TextW(f, fs2, fresh), y0), ImVec2(b.x, y0 + fs2)))
        ImGui::SetTooltip("Titulares de fuentes públicas (RSS): %d de %d fuentes respondieron. Enlaces al artículo original. Control editorial: %s",
                          snap.sourcesOk, snap.sourcesTried, snap.editorial.c_str());

    // Three headlines: category · title (one line), then outlet · date
    const auto& items = snap.blocks[shownBlock];
    float y = y0 + fs + 7 * s;
    const float rowH = std::floor((b.y - y) / 3.0f);
    if (items.empty())
    {
        const char* msg = snap.fetched == 0 && snap.busy ? "Buscando titulares en las fuentes…"
                          : snap.fetched == 0            ? "Sin conexión con las fuentes y sin titulares guardados"
                                                         : "Sin novedades verificadas en este bloque";
        dl->AddText(f, fs, ImVec2(a.x, y + 4 * s), Col(pal::Dim, alpha), msg);
        return;
    }
    for (size_t i = 0; i < items.size() && i < 3; i++)
    {
        const auto& it = items[i];
        const float ty = y + i * rowH;
        const float titleSize = std::floor(13.f * s);
        std::string prefix = it.sponsored ? "PATROCINADO · " : it.pinned ? "DESTACADO · " : it.rumor ? "RUMOR · " : "";
        const std::string title = Fit(f, titleSize, prefix + it.title, b.x - a.x);
        ImGui::SetCursorScreenPos(ImVec2(a.x, ty));
        const bool clicked = ImGui::InvisibleButton(StrFmt("##NewsItem%zu", i).c_str(), ImVec2(b.x - a.x, (std::max)(rowH - 2 * s, 1.0f)));
        const bool over = ImGui::IsItemHovered();
        dl->AddText(f, titleSize, ImVec2(a.x, ty), Col(over ? pal::Green : pal::Text, alpha), title.c_str());
        if (!prefix.empty())
            dl->AddText(f, titleSize, ImVec2(a.x, ty), Col(it.rumor && !it.pinned ? pal::Orange : pal::Yellow, alpha), prefix.c_str());
        const std::string meta = StrFmt("%s · %s · %s · %s", IlionNews::BlockName(it.block), it.source.c_str(), DayMonth(it.time).c_str(), Ago(it.time).c_str());
        dl->AddText(f, std::floor(10.5f * s), ImVec2(a.x, ty + titleSize + 1 * s), Col(pal::Dim, alpha), Fit(f, std::floor(10.5f * s), meta, b.x - a.x).c_str());
        if (over)
        {
            ImGui::SetTooltip("%s\n%s · %s\n%s", it.title.c_str(), it.source.c_str(), DayMonth(it.time).c_str(), it.link.c_str());
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        }
        if (clicked)
            OpenLink(it.link);
    }
}

// ---- ILION TERMINAL ----------------------------------------------------------------------------------------------------
static double typeStart = -10.0;
static int typedView = -1;

namespace crt
{
static const ImVec4 Bg(0.010f, 0.022f, 0.013f, 1.0f);
static const ImVec4 Green(0.36f, 1.00f, 0.46f, 1.0f);
static const ImVec4 Dim(0.16f, 0.52f, 0.24f, 1.0f);
static const ImVec4 Amber(1.00f, 0.72f, 0.20f, 1.0f);
static const ImVec4 Red(1.00f, 0.28f, 0.20f, 1.0f);
} // namespace crt

struct Cell
{
    std::string label, value;
    int level = 0; // 0 normal, 1 warning (amber), 2 critical (red), -1 not available
};

static std::string Num(const IlionTerminal::Reading& r, const char* fmt, double scale = 1.0)
{
    return r.valid ? StrFmt(fmt, r.value * scale) : std::string("N/A");
}

static Cell C1(const char* label, const IlionTerminal::Reading& r, const char* fmt, double scale = 1.0, double warn = 1e9, double crit = 1e9)
{
    Cell c { label, Num(r, fmt, scale), 0 };
    if (!r.valid)
        c.level = -1;
    else if (r.value >= crit)
        c.level = 2;
    else if (r.value >= warn)
        c.level = 1;
    return c;
}

// The lines of each view: a title line and rows of two cells
static void BuildView(int view, const IlionTerminal::Sensors& s, double frameMs, std::string& title, std::vector<std::pair<Cell, Cell>>& rows, std::string& foot)
{
    rows.clear();
    if (view == 0)
    {
        title = "GPU " + (s.gpuName.empty() ? std::string("N/A") : s.gpuName);
        rows.push_back({ C1("TEMP", s.gpuTemp, "%.0f°C", 1.0, 85, 95), C1("LOAD", s.gpuUsage, "%.0f%%") });
        rows.push_back({ C1("HOTSPOT", s.gpuHotspot, "%.0f°C", 1.0, 100, 108), C1("CLOCK", s.gpuClock, "%.0fMHz") });
        rows.push_back({ C1("INTAKE", s.gpuIntake, "%.0f°C"), C1("MEM CLK", s.gpuMemClock, "%.0fMHz") });
        rows.push_back({ C1("PWR CHIP", s.gpuChipPower, "%.0fW"), C1("VOLTAGE", s.gpuVoltage, "%.0fmV") });
        rows.push_back({ C1("PWR BOARD", s.gpuBoardPower, "%.0fW"), C1("FAN", s.fanRpm, "%.0fRPM") });
        Cell vram { "VRAM", s.vramUsed.valid && s.vramTotal.valid ? StrFmt("%.1f/%.1fG", s.vramUsed.value / 1024.0, s.vramTotal.value / 1024.0) : "N/A", 0 };
        if (!s.vramUsed.valid)
            vram.level = -1;
        else if (s.vramTotal.valid && s.vramUsed.value >= s.vramTotal.value * 0.95)
            vram.level = 1;
        rows.push_back({ vram, C1("FAN %", s.fanPercent, "%.0f%%") });
        foot = StrFmt("VRAM FREE %s · SRC %s", Num(s.vramFree, "%.1fG", 1.0 / 1024.0).c_str(), s.gpuSource.c_str());
    }
    else if (view == 1)
    {
        title = "CPU " + (s.cpuName.empty() ? std::string("N/A") : s.cpuName);
        rows.push_back({ C1("LOAD", s.cpuUsage, "%.0f%%"), C1("TEMP", s.cpuTemp, "%.0f°C", 1.0, 90, 98) });
        rows.push_back({ C1("CLOCK", s.cpuClock, "%.2fGHz", 0.001), C1("POWER", s.cpuPower, "%.0fW") });
        rows.push_back({ C1("BASE", s.cpuBase, "%.2fGHz", 0.001), { "CORES", s.cores > 0 ? StrFmt("%d/%dT", s.cores, s.threads) : "N/A", s.cores > 0 ? 0 : -1 } });
        foot = s.cpuTemp.valid ? StrFmt("TEMP/PWR SRC %s · LOAD/CLOCK SRC WINDOWS", s.cpuTemp.source) : "TEMP/PWR: N/A SIN HWINFO (OPCIONAL)";
    }
    else if (view == 2)
    {
        title = "MEMORIA FISICA";
        rows.push_back({ C1("TOTAL", s.ramTotal, "%.1fG", 1.0 / 1024.0), C1("LOAD", s.ramLoad, "%.0f%%", 1.0, 90, 97) });
        rows.push_back({ C1("USED", s.ramUsed, "%.1fG", 1.0 / 1024.0), C1("FREE", s.ramFree, "%.1fG", 1.0 / 1024.0) });
        rows.push_back({ C1("HW RSVD", s.ramReserved, "%.2fG", 1.0 / 1024.0), C1("CACHE", s.cache, "%.1fG", 1.0 / 1024.0) });
        rows.push_back({ C1("COMMIT", s.commitUsed, "%.1fG", 1.0 / 1024.0), C1("LIMIT", s.commitLimit, "%.1fG", 1.0 / 1024.0) });
        foot = "SRC WINDOWS · RSVD = INSTALADA - UTILIZABLE";
    }
    else
    {
        title = "SISTEMA";
        IlionTerminal::Reading fps, ft;
        if (frameMs > 0.0 && std::isfinite(frameMs))
            IlionTerminal::detail::Set(fps, 1000.0 / frameMs, "ILION"), IlionTerminal::detail::Set(ft, frameMs, "ILION");
        rows.push_back({ C1("GAME FPS", fps, "%.0f"), C1("FRAME", ft, "%.1fms") });
        rows.push_back({ { "UPTIME", s.uptimeS.valid ? StrFmt("%lldh%02lldm", (long long) s.uptimeS.value / 3600, ((long long) s.uptimeS.value / 60) % 60) : "N/A", 0 },
                         C1("PROCS", s.processes, "%.0f") });
        rows.push_back({ C1("THREADS", s.threadsAll, "%.0f"), C1("HANDLES", s.handles, "%.0f") });
        foot = StrFmt("ADLX:%s NVML:%s PDH:%s HWINFO:%s AB:%s", s.adlx ? "OK" : "--", s.nvml ? "OK" : "--", s.pdh ? "OK" : "--", s.hwinfo ? "OK" : "--",
                      s.afterburner ? "OK" : "--");
    }
}

static void DrawTerminal(ImDrawList* dl, ImVec2 a, ImVec2 b, double frameMs)
{
    auto& mon = IlionTerminal::Monitor::Instance();
    mon.KeepAlive();
    const auto sens = mon.Get();
    const double now = ImGui::GetTime();
    const float s = U(1.0f);
    ImFont* f = IlionTheme::crtFont != nullptr ? IlionTheme::crtFont : fUi;
    const float fs = std::floor(11.5f * s);
    const float lh = std::floor(fs * 1.12f);
    const float cw = TextW(f, fs, "0");

    // View tabs: GPU CPU MEM SYS
    static const char* views[] = { "GPU", "CPU", "MEM", "SYS" };
    float x = a.x;
    for (int i = 0; i < 4; i++)
    {
        const std::string t = i == prefs.view ? StrFmt("[%s]", views[i]) : StrFmt(" %s ", views[i]);
        if (Link(StrFmt("##TermView%d", i).c_str(), ImVec2(x, a.y), f, fs, t, Col(i == prefs.view ? crt::Green : crt::Dim), Col(crt::Green), dl) && i != prefs.view)
        {
            prefs.view = i;
            SavePrefs();
        }
        x += TextW(f, fs, t) + cw;
    }
    const std::string anim = prefs.animate ? "ANIM ON" : "ANIM OFF";
    if (Link("##TermAnim", ImVec2(b.x - TextW(f, fs, anim), a.y), f, fs, anim, Col(crt::Dim), Col(crt::Green), dl))
    {
        prefs.animate = !prefs.animate;
        SavePrefs();
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", "Animación de escritura al entrar en TERMINAL (pulsa para activarla o desactivarla)");

    // The typewriter: on entering TERMINAL or changing the view, lines revealed one after another, left to right
    if (typedView != prefs.view)
        typedView = prefs.view, typeStart = now;
    const double t = prefs.animate ? now - typeStart : 1e9;
    const float flicker = t < 0.18 ? 0.55f + 0.45f * (float) std::fabs(std::sin(t * 90.0)) : 1.0f;

    std::string title, foot;
    std::vector<std::pair<Cell, Cell>> rows;
    BuildView(prefs.view, sens, frameMs, title, rows, foot);
    {
        const float tx = x + cw, tw = b.x - TextW(f, fs, anim) - cw - tx;
        if (tw > cw * 6)
            dl->AddText(f, fs, ImVec2(tx, a.y), Col(crt::Dim), Fit(f, fs, title, tw).c_str());
    }
    const int cols = (std::max)(20, (int) ((b.x - a.x) / cw));
    const int half = cols / 2;
    const auto pad = [](std::string l, int n)
    {
        if ((int) l.size() < n)
            l += std::string(n - l.size(), ' ');
        return l;
    };

    // Lines as fixed-width text, with the colour of each value
    struct Seg
    {
        std::string text;
        ImVec4 colour;
    };
    std::vector<std::vector<Seg>> lines;
    const auto cell = [&](const Cell& c, int width) -> std::vector<Seg>
    {
        const int valueW = (int) c.value.size();
        const int labelW = (std::max)(1, width - valueW - 1);
        std::string label = c.label.substr(0, (size_t) labelW);
        const ImVec4 col = c.level == -1 ? crt::Dim : c.level == 2 ? crt::Red : c.level == 1 ? crt::Amber : crt::Green;
        return { { pad(label, labelW), crt::Dim }, { c.value + " ", col } };
    };
    for (const auto& r : rows)
    {
        auto l = cell(r.first, half);
        auto rr = cell(r.second, cols - half);
        l.insert(l.end(), rr.begin(), rr.end());
        lines.push_back(l);
    }
    lines.push_back({ { Fit(f, fs, StrFmt("%s · UPD 1s", foot.c_str()), b.x - a.x), crt::Dim } });

    // A thin rule under the header and above the footer (lines, not text rows: the six rows and the footer fit)
    const float ruleY = a.y + lh + 1 * s;
    dl->AddLine(ImVec2(a.x, ruleY), ImVec2(b.x, ruleY), Col(crt::Dim, 0.7f), 1.0f);
    float y = ruleY + 2 * s;
    bool cursorDrawn = false;
    for (size_t i = 0; i < lines.size() && y + lh <= b.y + 1; i++, y += lh)
    {
        // Each line starts 0.11 s after the previous one and types 110 characters a second
        const double startAt = i * 0.11;
        if (i + 1 == lines.size())
            dl->AddLine(ImVec2(a.x, y - 1 * s), ImVec2(b.x, y - 1 * s), Col(crt::Dim, 0.5f), 1.0f);
        int budget = t >= 1e8 ? INT_MAX : (int) ((t - startAt) * 110.0);
        if (budget <= 0)
            break;
        float lx = a.x;
        for (const auto& seg : lines[i])
        {
            std::string shown = seg.text;
            if ((int) shown.size() > budget)
                shown = shown.substr(0, (size_t) (std::max)(0, budget));
            budget -= (int) seg.text.size();
            // A light phosphor glow under the letters, then the letters
            dl->AddText(f, fs, ImVec2(lx + 0.6f * s, y), Col(seg.colour, 0.22f * flicker), shown.c_str());
            dl->AddText(f, fs, ImVec2(lx, y), Col(seg.colour, flicker), shown.c_str());
            lx += TextW(f, fs, shown);
            if (budget < 0)
                break;
        }
        // The cursor follows the line being written; at the end it blinks after the last line
        const bool writing = budget < 0;
        const bool last = i + 1 == lines.size() || y + 2 * lh > b.y + 1;
        if (!cursorDrawn && (writing || (last && fmod(now, 1.0) < 0.5)))
        {
            dl->AddRectFilled(ImVec2(lx + 1 * s, y + 1 * s), ImVec2(lx + cw, y + lh - 1 * s), Col(crt::Green, writing ? 0.85f : 0.55f));
            cursorDrawn = true;
        }
        if (writing)
            break;
    }
}

// ---- The box -----------------------------------------------------------------------------------------------------------
// a, b: the box in screen coordinates. frameMs: ILION's frame time of the game (SYS view)
static void Draw(ImDrawList* dl, ImVec2 a, ImVec2 b, double frameMs)
{
    if (!prefsLoaded)
        LoadPrefs();
    const float s = U(1.0f);
    const bool terminal = prefs.mode == 1;
    ImGui::PushID("##IlionHomePanel");

    // A screen set into the console (as Advisor's): matte surface, thin frame, 1 px bevel; the terminal is a CRT
    const float r = 4.0f * s;
    dl->AddRectFilled(a, b, terminal ? Col(crt::Bg) : Col(Mix(pal::Bg, ImVec4(0, 0, 0, 1), 0.25f)), r);
    dl->AddRect(a, b, Col(terminal ? crt::Dim : pal::Line, terminal ? 0.7f : 1.0f), r, 0, 1.f);
    dl->AddLine(ImVec2(a.x + r, a.y + 1), ImVec2(b.x - r, a.y + 1), Col(ImVec4(0, 0, 0, 0.55f)), 1.f);
    if (terminal)
        for (float sy = a.y + 2; sy < b.y - 1; sy += 3.0f * s) // faint scan lines
            dl->AddLine(ImVec2(a.x + 2, sy), ImVec2(b.x - 2, sy), Col(ImVec4(0, 0, 0, 1), 0.18f), 1.0f);

    const ImVec2 in0(a.x + 10 * s, a.y + 6 * s), in1(b.x - 10 * s, b.y - 6 * s);
    dl->PushClipRect(a, b, true);

    // Top line: the name of the mode and the selector AL DÍA | TERMINAL
    const float hs = std::floor(12.f * s);
    if (terminal)
        dl->AddText(IlionTheme::crtFont != nullptr ? IlionTheme::crtFont : fUi, hs, in0, Col(crt::Green), "ILION // SYSTEM TERMINAL");
    else
        dl->AddText(fBrand, std::floor(13.f * s), in0, Col(pal::Green), "ILION AL DÍA");
    {
        const char* t1 = "AL DÍA";
        const char* t2 = "TERMINAL";
        const float w2 = TextW(fUi, hs, t2), w1 = TextW(fUi, hs, t1), sep = TextW(fUi, hs, "  |  ");
        float x = in1.x - w2;
        const ImU32 on = Col(terminal ? crt::Green : pal::Green), off = Col(terminal ? crt::Dim : pal::Dim);
        if (Link("##ModeTerminal", ImVec2(x, in0.y), fUi, hs, t2, terminal ? on : off, on, dl) && !terminal)
        {
            prefs.mode = 1;
            typedView = -1; // the terminal writes itself again when entered
            SavePrefs();
        }
        x -= sep;
        dl->AddText(fUi, hs, ImVec2(x, in0.y), off, "  |  ");
        x -= w1;
        if (Link("##ModeNews", ImVec2(x, in0.y), fUi, hs, t1, terminal ? off : on, on, dl) && terminal)
        {
            prefs.mode = 0;
            SavePrefs();
        }
    }
    dl->AddLine(ImVec2(in0.x, in0.y + hs + 4 * s), ImVec2(in1.x, in0.y + hs + 4 * s), Col(terminal ? crt::Dim : pal::Line, 0.6f), 1.0f);

    const ImVec2 c0(in0.x, in0.y + hs + 8 * s);
    const bool hovered = ImGui::IsMouseHoveringRect(a, b);
    if (prefs.mode == 1)
        DrawTerminal(dl, c0, in1, frameMs);
    else
        DrawNews(dl, c0, in1, hovered);

    dl->PopClipRect();
    ImGui::PopID();
}
} // namespace HomePanel
} // namespace IlionShell

#pragma execution_character_set(pop)
