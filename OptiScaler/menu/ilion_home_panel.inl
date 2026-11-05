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
    for (unsigned char c : url)
        if (c < 0x20)
            return;
    // UTF-8 to UTF-16 (a link may carry accents): not byte by byte
    const int len = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, url.c_str(), (int) url.size(), nullptr, 0);
    if (len <= 0)
        return;
    std::wstring w((size_t) len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, url.c_str(), (int) url.size(), w.data(), len);
    ShellExecuteW(nullptr, L"open", w.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

// ---- ILION AL DÍA: one news item at a time, its picture on the left ------------------------------------------------
// Two textures of the picture size, made once and reused (the next picture is uploaded into the free one ahead of time),
// released when TERMINAL is chosen. Pixels come decoded from the news thread: nothing is downloaded or decoded here
struct PictureSlot
{
    ImTextureData* tex = nullptr;
    std::string key; // the link of the item whose picture it holds
};

static PictureSlot slots[2];
static std::vector<ImTextureData*> retiredTextures;
static int current = 0, previous = -1; // indexes in the rotation
static int slotOfCurrent = 0;          // the slot that shows the current item
static double shownSince = 0.0, fadeStart = -10.0;
static std::string currentLink;        // the item shown, kept across a refresh of the list
static constexpr double kItemSeconds = 11.0, kFadeSeconds = 0.5;

static void RetireTexture(ImTextureData*& tex)
{
    if (tex == nullptr)
        return;
    if (tex->TexID == ImTextureID_Invalid && tex->BackendUserData == nullptr)
    {
        ImGui::UnregisterUserTexture(tex);
        IM_DELETE(tex);
    }
    else
    {
        tex->SetStatus(ImTextureStatus_WantDestroy); // the renderer releases it once no frame uses it
        tex->UnusedFrames = 0;
        retiredTextures.push_back(tex);
    }
    tex = nullptr;
}

static void ReleasePictures()
{
    for (auto& s : slots)
        RetireTexture(s.tex), s.key.clear();
}

// Every frame: registration kept, a texture the renderer dropped (device reset) is created again from its pixels,
// retired ones freed once destroyed
static void MaintainPictures()
{
    auto& registered = ImGui::GetCurrentContext()->UserTextures;
    for (auto it = retiredTextures.begin(); it != retiredTextures.end();)
    {
        if ((*it)->Status == ImTextureStatus_Destroyed)
        {
            ImGui::UnregisterUserTexture(*it);
            IM_DELETE(*it);
            it = retiredTextures.erase(it);
            continue;
        }
        if (!registered.contains(*it))
            ImGui::RegisterUserTexture(*it);
        (*it)->UnusedFrames++;
        ++it;
    }
    for (auto& s : slots)
    {
        if (s.tex == nullptr)
            continue;
        if (!registered.contains(s.tex))
            ImGui::RegisterUserTexture(s.tex);
        if (s.tex->Status == ImTextureStatus_Destroyed)
            s.tex->SetStatus(ImTextureStatus_WantCreate);
    }
}

// The item's picture into a slot (once per item; 320x180 RGBA copied, then uploaded by the renderer)
static void Upload(PictureSlot& s, const IlionNews::Item& it)
{
    if (s.key == it.link)
        return;
    s.key = it.link;
    if (!it.image)
        return;
    if (s.tex == nullptr)
    {
        s.tex = IM_NEW(ImTextureData)();
        s.tex->Create(ImTextureFormat_RGBA32, IlionNews::kImageW, IlionNews::kImageH);
        s.tex->UseColors = true;
        s.tex->RefCount = 1;
        s.tex->UsedRect = { 0, 0, (unsigned short) IlionNews::kImageW, (unsigned short) IlionNews::kImageH };
        s.tex->SetStatus(ImTextureStatus_WantCreate);
        ImGui::RegisterUserTexture(s.tex);
    }
    for (int y = 0; y < IlionNews::kImageH; y++)
        memcpy(s.tex->GetPixelsAt(0, y), it.image->data() + (size_t) y * IlionNews::kImageW * 4, (size_t) IlionNews::kImageW * 4);
    if (s.tex->Status == ImTextureStatus_OK || s.tex->Status == ImTextureStatus_WantUpdates)
    {
        const ImTextureRect rect { 0, 0, (unsigned short) IlionNews::kImageW, (unsigned short) IlionNews::kImageH };
        s.tex->UpdateRect = rect;
        s.tex->Updates.resize(0);
        s.tex->Updates.push_back(rect);
        s.tex->SetStatus(ImTextureStatus_WantUpdates);
    }
}

// Words wrapped into at most maxLines lines of a width; the last one ends with an ellipsis at a word if it doesn't fit
static std::vector<std::string> WrapWords(ImFont* f, float size, const std::string& text, float width, int maxLines)
{
    std::vector<std::string> lines;
    std::string line;
    size_t i = 0;
    while (i < text.size())
    {
        size_t j = text.find(' ', i);
        if (j == std::string::npos)
            j = text.size();
        const std::string word = text.substr(i, j - i);
        const std::string tryLine = line.empty() ? word : line + " " + word;
        if (TextW(f, size, tryLine) <= width || line.empty())
            line = tryLine;
        else
        {
            lines.push_back(line);
            line = word;
            if ((int) lines.size() == maxLines)
            {
                // Out of room: the last line takes an ellipsis after its last whole word
                std::string& last = lines.back();
                while (!last.empty() && TextW(f, size, last + "…") > width)
                {
                    const size_t sp = last.rfind(' ');
                    last = sp == std::string::npos ? Fit(f, size, last, width) : last.substr(0, sp);
                    if (sp == std::string::npos)
                        return lines;
                }
                last += "…";
                return lines;
            }
        }
        i = j + 1;
    }
    if (!line.empty())
        lines.push_back(TextW(f, size, line) <= width ? line : Fit(f, size, line, width));
    return lines;
}

// One item: picture | headline (up to three lines), excerpt (one or two), outlet · age · category
static void DrawItem(ImDrawList* dl, ImVec2 a, ImVec2 b, const IlionNews::Item& it, const PictureSlot& slot, float alpha, bool hovered)
{
    const float s = U(1.0f);
    ImFont* f = fUi;
    const float h = b.y - a.y, w = b.x - a.x;
    // The picture: 16:9 at the full height, unless that takes more than 40 % of the width (then cropped from the centre)
    const float pw = (std::min)(std::floor(h * 16.f / 9.f), std::floor(w * 0.40f));
    const ImVec2 p0 = a, p1(a.x + pw, b.y);
    const float aspect = pw / h, source = (float) IlionNews::kImageW / IlionNews::kImageH;
    ImVec2 uv0(0, 0), uv1(1, 1);
    if (aspect < source)
    {
        const float keep = aspect / source;
        uv0.x = (1.0f - keep) * 0.5f, uv1.x = 1.0f - uv0.x;
    }
    else
    {
        const float keep = source / aspect;
        uv0.y = (1.0f - keep) * 0.5f, uv1.y = 1.0f - uv0.y;
    }
    if (it.image && slot.tex != nullptr && slot.key == it.link)
        dl->AddImageRounded(slot.tex->GetTexRef(), p0, p1, uv0, uv1, ImGui::GetColorU32(ImVec4(1, 1, 1, alpha)), 3.0f * s);
    else
    {
        // No valid picture: a discreet plate with the outlet's name, never an invented image
        dl->AddRectFilled(p0, p1, Col(Mix(pal::Panel, ImVec4(0, 0, 0, 1), 0.35f), alpha), 3.0f * s);
        const std::string name = Fit(f, std::floor(12.f * s), it.source, pw - 12 * s);
        const float nw = TextW(f, std::floor(12.f * s), name);
        dl->AddText(f, std::floor(12.f * s), ImVec2(p0.x + (pw - nw) * 0.5f, p0.y + h * 0.5f - 7 * s), Col(pal::Dim, alpha), name.c_str());
    }
    dl->AddRect(p0, p1, Col(pal::Line, 0.9f * alpha), 3.0f * s, 0, 1.0f);

    // Text column
    const float tx = p1.x + 10 * s, tw = b.x - tx;
    if (tw < 60 * s)
        return;
    const float metaSize = std::floor(10.5f * s), metaY = b.y - metaSize - 1 * s;
    float titleSize = std::floor(15.f * s);
    std::vector<std::string> title = WrapWords(f, titleSize, it.title, tw, 3);
    if (title.size() == 3 && (title.back().size() >= 3 && title.back().compare(title.back().size() - 3, 3, "…") == 0))
    {
        // A smaller size when three lines don't hold the headline
        titleSize = std::floor(13.5f * s);
        title = WrapWords(f, titleSize, it.title, tw, 3);
    }
    float y = a.y;
    const float tl = std::floor(titleSize * 1.12f);
    for (const auto& line : title)
    {
        dl->AddText(f, titleSize, ImVec2(tx, y), Col(hovered ? pal::Green : pal::Text, alpha), line.c_str());
        y += tl;
    }
    // The excerpt, in the room left above the meta line
    const float sumSize = std::floor(11.5f * s), sl = std::floor(sumSize * 1.15f);
    const int room = (int) ((metaY - 2 * s - (y + 2 * s)) / sl);
    if (room > 0 && !it.summary.empty())
    {
        y += 2 * s;
        for (const auto& line : WrapWords(f, sumSize, it.summary, tw, (std::min)(room, 2)))
        {
            dl->AddText(f, sumSize, ImVec2(tx, y), Col(pal::Text2, alpha), line.c_str());
            y += sl;
        }
    }
    std::string tag = it.sponsored ? "PATROCINADO" : it.pinned ? "DESTACADO" : it.rumor ? "RUMOR" : IlionNews::BlockName(it.block);
    const std::string meta = Fit(f, metaSize, StrFmt("%s · %s · %s", it.source.c_str(), Ago(it.time).c_str(), tag.c_str()), tw - 70 * s);
    dl->AddText(f, metaSize, ImVec2(tx, metaY), Col(it.rumor && !it.pinned ? pal::Orange : pal::Dim, alpha), meta.c_str());
}

static void DrawNews(ImDrawList* dl, ImVec2 a, ImVec2 b, bool hovered)
{
    auto& news = IlionNews::Service::Instance();
    news.KeepAlive();
    const auto snap = news.Get();
    const double now = ImGui::GetTime();
    const float s = U(1.0f);
    ImFont* f = fUi;
    const auto& list = snap.rotation;
    const int n = (int) list.size();

    if (n == 0)
    {
        const char* msg = snap.fetched == 0 && snap.busy ? "Buscando noticias en las fuentes…"
                          : snap.fetched == 0            ? "Sin conexión con las fuentes y sin noticias guardadas"
                                                         : "Sin novedades verificadas";
        dl->AddText(f, std::floor(12.f * s), ImVec2(a.x, a.y + 4 * s), Col(pal::Dim), msg);
        return;
    }

    // Keep the item shown across a refresh of the list; else start from the first
    if (current >= n || list[current].link != currentLink)
    {
        int found = 0;
        for (int i = 0; i < n; i++)
            if (list[i].link == currentLink)
                found = i;
        current = found, previous = -1, fadeStart = -10.0, shownSince = now;
        slots[slotOfCurrent].key.clear();
    }
    currentLink = list[current].link;

    // Automatic rotation (paused while the mouse is on the item), manual arrows, a cross-fade without empty frames
    const auto goTo = [&](int index)
    {
        if (n < 2)
            return;
        previous = current;
        current = (index % n + n) % n;
        currentLink = list[current].link;
        slotOfCurrent ^= 1;
        Upload(slots[slotOfCurrent], list[current]); // normally preloaded already: then nothing to do
        fadeStart = now;
        shownSince = now;
    };
    if (fadeStart < 0.0 && !hovered && now - shownSince >= kItemSeconds)
        goTo(current + 1);

    Upload(slots[slotOfCurrent], list[current]);
    float t = 1.0f;
    if (fadeStart >= 0.0)
    {
        t = (float) std::clamp((now - fadeStart) / kFadeSeconds, 0.0, 1.0);
        if (t >= 1.0f)
            fadeStart = -10.0, previous = -1;
    }
    // The next picture waits in the free slot while this one is on screen
    if (previous < 0 && n > 1)
        Upload(slots[slotOfCurrent ^ 1], list[(current + 1) % n]);

    // The whole item opens its article
    const ImVec2 navSize(70 * s, 14 * s);
    ImGui::SetCursorScreenPos(a);
    const bool clicked = ImGui::InvisibleButton("##NewsItem", ImVec2(b.x - a.x, (std::max)(b.y - a.y - navSize.y, 1.0f)));
    const bool over = ImGui::IsItemHovered();
    if (previous >= 0 && previous < n)
        DrawItem(dl, a, b, list[previous], slots[slotOfCurrent ^ 1], 1.0f - t, false);
    DrawItem(dl, a, b, list[current], slots[slotOfCurrent], t, over);
    if (over)
    {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        ImGui::SetTooltip("%s\n%s · %s\nPulsa para abrir el artículo original", list[current].title.c_str(), list[current].source.c_str(),
                          DayMonth(list[current].time).c_str());
    }
    if (clicked)
        OpenLink(list[current].link);

    // ‹ n/total › at the bottom right, and the state of the feed on hover
    const float ns = std::floor(11.f * s);
    const std::string count = StrFmt("%d/%d", current + 1, n);
    float x = b.x - TextW(f, ns + 2 * s, "›");
    if (Link("##NewsNext", ImVec2(x, b.y - ns - 3 * s), f, ns + 2 * s, "›", Col(pal::Text2), Col(pal::Green), dl))
        goTo(current + 1);
    x -= TextW(f, ns, count) + 6 * s;
    dl->AddText(f, ns, ImVec2(x, b.y - ns - 1 * s), Col(pal::Dim), count.c_str());
    if (ImGui::IsMouseHoveringRect(ImVec2(x, b.y - ns - 2 * s), ImVec2(x + TextW(f, ns, count), b.y)))
        ImGui::SetTooltip("Noticias de medios en español (RSS): %d de %d fuentes respondieron · %s%s. Control editorial: %s", snap.sourcesOk,
                          snap.sourcesTried, snap.fetched > 0 ? ("actualizado " + Ago(snap.fetched)).c_str() : "sin actualizar",
                          snap.offline ? " (sin conexión: se muestra lo guardado)" : "", snap.editorial.c_str());
    x -= TextW(f, ns + 2 * s, "‹") + 6 * s;
    if (Link("##NewsPrev", ImVec2(x, b.y - ns - 3 * s), f, ns + 2 * s, "‹", Col(pal::Text2), Col(pal::Green), dl))
        goTo(current - 1);
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

// A reading: its label and its value in one or more coloured parts (VRAM: used in amber, total in green)
struct Cell
{
    std::string label;
    std::vector<std::pair<std::string, ImVec4>> parts;
};

static std::string Num(const IlionTerminal::Reading& r, const char* fmt, double scale = 1.0)
{
    return r.valid ? StrFmt(fmt, r.value * scale) : std::string("N/A");
}

// base: the colour of a normal reading (green; the hotspot is amber); warn / crit: amber / red thresholds
static Cell C1(const char* label, const IlionTerminal::Reading& r, const char* fmt, double scale = 1.0, double warn = 1e9, double crit = 1e9,
               const ImVec4& base = crt::Green)
{
    const ImVec4 colour = !r.valid ? crt::Dim : r.value >= crit ? crt::Red : r.value >= warn ? crt::Amber : base;
    return { label, { { Num(r, fmt, scale), colour } } };
}

// The rows of each view (two cells each), its title and its footer
static void BuildView(int view, const IlionTerminal::Sensors& s, double frameMs, std::string& title, std::vector<std::pair<Cell, Cell>>& rows, std::string& foot)
{
    rows.clear();
    if (view == 0)
    {
        title = "GPU " + (s.gpuName.empty() ? std::string("N/A") : s.gpuName);
        rows.push_back({ C1("TEMP", s.gpuTemp, "%.0f°C", 1.0, 85, 95), C1("LOAD", s.gpuUsage, "%.0f%%") });
        rows.push_back({ C1("HOTSPOT", s.gpuHotspot, "%.0f°C", 1.0, 1e9, 108, crt::Amber), C1("CLOCK", s.gpuClock, "%.0fMHz") });
        rows.push_back({ C1("INTAKE", s.gpuIntake, "%.0f°C"), C1("MEM CLK", s.gpuMemClock, "%.0fMHz") });
        rows.push_back({ C1("PWR CHIP", s.gpuChipPower, "%.0fW"), C1("VOLTAGE", s.gpuVoltage, "%.0fmV") });
        rows.push_back({ C1("PWR BOARD", s.gpuBoardPower, "%.0fW"), C1("FAN", s.fanRpm, "%.0fRPM") });
        // VRAM: the used part in amber, the card's total in green (red when it is almost full)
        Cell vram { "VRAM", {} };
        if (s.vramUsed.valid && s.vramTotal.valid)
        {
            const bool full = s.vramUsed.value >= s.vramTotal.value * 0.95;
            vram.parts = { { StrFmt("%.1f", s.vramUsed.value / 1024.0), full ? crt::Red : crt::Amber }, { "/", crt::Dim },
                           { StrFmt("%.1fG", s.vramTotal.value / 1024.0), crt::Green } };
        }
        else
            vram.parts = { { "N/A", crt::Dim } };
        rows.push_back({ vram, C1("FAN %", s.fanPercent, "%.0f%%") });
        foot = StrFmt("VRAM FREE %s · SRC %s", Num(s.vramFree, "%.1fG", 1.0 / 1024.0).c_str(), s.gpuSource.c_str());
    }
    else if (view == 1)
    {
        title = "CPU " + (s.cpuName.empty() ? std::string("N/A") : s.cpuName);
        rows.push_back({ C1("LOAD", s.cpuUsage, "%.0f%%"), C1("TEMP", s.cpuTemp, "%.0f°C", 1.0, 90, 98) });
        rows.push_back({ C1("CLOCK", s.cpuClock, "%.2fGHz", 0.001), C1("POWER", s.cpuPower, "%.0fW") });
        rows.push_back({ C1("BASE", s.cpuBase, "%.2fGHz", 0.001),
                         { "CORES", { { s.cores > 0 ? StrFmt("%d/%dT", s.cores, s.threads) : std::string("N/A"), s.cores > 0 ? crt::Green : crt::Dim } } } });
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
        rows.push_back({ { "UPTIME",
                           { { s.uptimeS.valid ? StrFmt("%lldh%02lldm", (long long) s.uptimeS.value / 3600, ((long long) s.uptimeS.value / 60) % 60) : std::string("N/A"),
                               s.uptimeS.valid ? crt::Green : crt::Dim } } },
                         C1("PROCS", s.processes, "%.0f") });
        rows.push_back({ C1("THREADS", s.threadsAll, "%.0f"), C1("HANDLES", s.handles, "%.0f") });
        foot = StrFmt("ADLX:%s NVML:%s PDH:%s HWINFO:%s AB:%s", s.adlx ? "OK" : "--", s.nvml ? "OK" : "--", s.pdh ? "OK" : "--", s.hwinfo ? "OK" : "--",
                      s.afterburner ? "OK" : "--");
    }
}

// A piece of text at a fixed place, for the typewriter (revealed in order, each at its final position)
struct Piece
{
    float x;
    std::string text;
    ImVec4 colour;
};

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

    // Two columns at fixed places: each label at the start of its column, its value ending at the column's right edge
    const float gap = cw * 2.0f;
    const float colW = std::floor((b.x - a.x - gap) * 0.5f);
    const float colX[2] = { a.x, a.x + colW + gap };
    std::vector<std::vector<Piece>> lines;
    for (const auto& r : rows)
    {
        std::vector<Piece> line;
        for (int c = 0; c < 2; c++)
        {
            const Cell& cell = c == 0 ? r.first : r.second;
            float valueW = 0.0f;
            for (const auto& p : cell.parts)
                valueW += TextW(f, fs, p.first);
            const float right = colX[c] + colW;
            // The label never runs into its value: cut if the column is too narrow
            line.push_back({ colX[c], Fit(f, fs, cell.label, (std::max)(cw, colW - valueW - cw)), crt::Dim });
            float vx = right - valueW;
            for (const auto& p : cell.parts)
            {
                line.push_back({ vx, p.first, p.second });
                vx += TextW(f, fs, p.first);
            }
        }
        lines.push_back(line);
    }
    lines.push_back({ { a.x, Fit(f, fs, StrFmt("%s · UPD 1s", foot.c_str()), b.x - a.x), crt::Dim } });

    // Thin rules under the header and above the footer (lines, not text rows)
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
        float endX = a.x;
        bool writing = false;
        for (const auto& piece : lines[i])
        {
            std::string shown = piece.text;
            if ((int) shown.size() > budget)
                shown = shown.substr(0, (size_t) (std::max)(0, budget)), writing = true;
            budget -= (int) piece.text.size();
            // A light phosphor glow under the letters, then the letters
            dl->AddText(f, fs, ImVec2(piece.x + 0.6f * s, y), Col(piece.colour, 0.22f * flicker), shown.c_str());
            dl->AddText(f, fs, ImVec2(piece.x, y), Col(piece.colour, flicker), shown.c_str());
            endX = piece.x + TextW(f, fs, shown);
            if (writing)
                break;
        }
        // The cursor follows the text being written; at the end it blinks after the last line
        const bool last = i + 1 == lines.size() || y + 2 * lh > b.y + 1;
        if (!cursorDrawn && (writing || (last && fmod(now, 1.0) < 0.5)))
        {
            dl->AddRectFilled(ImVec2(endX + 1 * s, y + 1 * s), ImVec2(endX + cw, y + lh - 1 * s), Col(crt::Green, writing ? 0.85f : 0.55f));
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
    MaintainPictures();
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
            ReleasePictures(); // the pictures' textures are not kept while TERMINAL is shown
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
    if (prefs.mode == 1 && (slots[0].tex != nullptr || slots[1].tex != nullptr))
        ReleasePictures(); // TERMINAL restored from the preferences
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
