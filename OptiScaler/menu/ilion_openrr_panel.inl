// ILION 1.5.0: Mejoras > Ray Reconstruction, the settings and the telemetry of OpenRR-ASI (misc/IlionOpenRr.h).
// Only the Industrial interface in its Full presentation has it (not Clean Olive, Compact or Sputnik). Included by
// menu_common.cpp after ilion_community.inl.

// The interface that has the technology: Industrial, Full presentation
static bool OpenRrUiAllowed(Config* config) { return UiThemeIndex(config) == 0 && !UiCompact(config) && !UiSputnik(config); }

// When a setting takes effect, shown at the right of its name
enum class RrApply
{
    Live,
    Recreate,
    Restart
};

static const char* RrApplyText(RrApply apply)
{
    switch (apply)
    {
    case RrApply::Live:
        return ShellText("live", "en vivo", "ao vivo", "сразу");
    case RrApply::Recreate:
        return ShellText("RR recreated", "recrear RR", "recriar RR", "пересоздание RR");
    default:
        return ShellText("restart", "reinicio", "reinício", "перезапуск");
    }
}

static const char* RrApplyTip(RrApply apply)
{
    switch (apply)
    {
    case RrApply::Live:
        return ShellText("Applied on the next frame.", "Se aplica en el siguiente fotograma.", "Aplicado no próximo quadro.",
                         "Применяется в следующем кадре.");
    case RrApply::Recreate:
        return ShellText("Applied when the game creates Ray Reconstruction again: turn RR off and on in the game, change the "
                         "resolution or restart.",
                         "Se aplica cuando el juego vuelve a crear Ray Reconstruction: apaga y enciende RR en el juego, cambia la "
                         "resolución o reinicia.",
                         "Aplicado quando o jogo cria o Ray Reconstruction de novo: desligue e ligue o RR no jogo, mude a resolução "
                         "ou reinicie.",
                         "Применяется, когда игра заново создаёт Ray Reconstruction: выключите и включите RR в игре, смените "
                         "разрешение или перезапустите.");
    default:
        return ShellText("Applied after restarting the game.", "Se aplica tras reiniciar el juego.", "Aplicado depois de reiniciar o jogo.",
                         "Применяется после перезапуска игры.");
    }
}

// The tag at the right edge of the line just drawn; on the next line when the name leaves no room for it
static void RrApplyTag(RrApply apply)
{
    const char* tag = RrApplyText(apply);
    const float tagW = ImGui::CalcTextSize(tag).x;
    const float gap = ImGui::GetStyle().ItemSpacing.x * 2.0f;
    ImGui::SameLine();
    const float avail = ImGui::GetContentRegionAvail().x;
    if (avail >= tagW + gap)
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + avail - tagW);
    else
        ImGui::NewLine();
    ImGui::TextDisabled("%s", tag);
    if (ImGui::IsItemHovered())
        WrappedTooltip("%s", RrApplyTip(apply));
}

// A name above its control, with when it applies at the right
static void RrName(const char* name, RrApply apply)
{
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(name);
    RrApplyTag(apply);
}

// A combo as wide as its column; a short preview when closed, the full names when open. A disabled item is drawn as
// such with the reason in its tooltip
static bool RrCombo(const char* id, const char* const* previews, const char* const* items, int count, int& index,
                    const bool* disabled = nullptr, const char* disabledTip = nullptr)
{
    bool changed = false;
    index = std::clamp(index, 0, count - 1);
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (IlionTheme::BeginCombo(id, previews[index]))
    {
        for (int i = 0; i < count; i++)
        {
            const bool off = disabled != nullptr && disabled[i];
            ImGui::BeginDisabled(off);
            if (ImGui::Selectable(items[i], i == index) && i != index)
            {
                index = i;
                changed = true;
            }
            ImGui::EndDisabled();
            if (off && disabledTip != nullptr && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                WrappedTooltip("%s", disabledTip);
            if (i == index)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    return changed;
}

// A switch of the ini, with when it applies at the right
static void RrSwitch(const char* label, const char* key, bool fallback, RrApply apply, const char* tip = nullptr)
{
    bool value = IlionOpenRr::GetBool(key, fallback);
    if (IlionTheme::Checkbox(label, &value))
        IlionOpenRr::SetBool(key, value);
    if (tip != nullptr && ImGui::IsItemHovered())
        WrappedTooltip("%s", tip);
    RrApplyTag(apply);
}

// Name and value: the name as wide as it needs, the value takes the rest of the card and wraps inside it
static bool RrBeginTable(const char* id)
{
    if (!ImGui::BeginTable(id, 2, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoSavedSettings))
        return false;
    ImGui::TableSetupColumn("##name", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn("##value", ImGuiTableColumnFlags_WidthStretch);
    return true;
}

static void RrRow(const ImVec4& colour, bool lit, const char* name, const std::string& value, const ImVec4* valueColour = nullptr)
{
    ImGui::TableNextColumn();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float h = ImGui::GetTextLineHeight();
    IlionTheme::ShellLed(ImGui::GetWindowDrawList(), ImVec2(p.x + h * 0.4f, p.y + h * 0.5f), h * 0.2f, IlionTheme::ShellPilot(colour), lit);
    ImGui::Dummy(ImVec2(h, h));
    ImGui::SameLine();
    ImGui::TextDisabled("%s", name);
    ImGui::TableNextColumn();
    ImGui::PushTextWrapPos(0.0f);
    if (valueColour != nullptr)
        ImGui::TextColored(*valueColour, "%s", value.c_str());
    else
        ImGui::TextUnformatted(value.c_str());
    ImGui::PopTextWrapPos();
}

static const char* RrArchName(IlionOpenRr::Arch arch)
{
    using IlionOpenRr::Arch;
    switch (arch)
    {
    case Arch::Rdna4:
        return "RDNA 4";
    case Arch::Rdna3:
        return "RDNA 3";
    case Arch::AmdOther:
        return ShellText("AMD without cooperative matrices", "AMD sin matrices cooperativas", "AMD sem matrizes cooperativas",
                         "AMD без кооперативных матриц");
    case Arch::Nvidia:
        return "NVIDIA";
    case Arch::Other:
        return ShellText("other vendor", "otro fabricante", "outro fabricante", "другой производитель");
    default:
        return ShellText("not identified", "sin identificar", "não identificada", "не определена");
    }
}

// The mode the plugin really runs, from its log (the ini says what was asked)
static std::string RrModeInUse(const IlionOpenRr::Telemetry& t)
{
    if (!t.haveModeLine)
        return ShellText("no data yet", "sin datos aún", "sem dados ainda", "данных пока нет");
    if (!t.fastModeOn)
        return "Legacy";
    return t.experimentalOn ? "Fast" : "New";
}

static std::string RrPrecision(const IlionOpenRr::Install& in, const IlionOpenRr::Telemetry& t)
{
    using IlionOpenRr::Arch;
    if (in.arch == Arch::Rdna3)
        return "FP16";
    if (in.arch == Arch::Rdna4)
        return t.haveModeLine && !t.fastModeOn ? "FP16" : "FP8";
    return "-";
}

// Not installed or not usable: what is missing, nothing else
static void RenderRayReconstructionMissing(const IlionOpenRr::Snapshot& snap)
{
    const auto& in = snap.install;
    {
        ScopedCard card;
        SectionTitle("Ray Reconstruction");
        WrappedText(IlionTheme::TextDim,
                    ShellText("OpenRR-ASI runs the DLSS Ray Reconstruction model of the game's nvngx_dlssd.dll on Radeon RX 9000 "
                              "and, experimentally, RX 7000. ILION doesn't include it: install it yourself from its official "
                              "page and ILION manages its settings and telemetry here.",
                              "OpenRR-ASI ejecuta el modelo DLSS Ray Reconstruction del nvngx_dlssd.dll del juego en Radeon RX 9000 "
                              "y, de forma experimental, en RX 7000. ILION no lo incluye: instálalo tú desde su página oficial y "
                              "ILION gestiona aquí su configuración y su telemetría.",
                              "O OpenRR-ASI executa o modelo DLSS Ray Reconstruction do nvngx_dlssd.dll do jogo em Radeon RX 9000 "
                              "e, de forma experimental, em RX 7000. O ILION não o inclui: instale-o você mesmo a partir da página "
                              "oficial e o ILION gere aqui as suas configurações e telemetria.",
                              "OpenRR-ASI запускает модель DLSS Ray Reconstruction из nvngx_dlssd.dll игры на Radeon RX 9000 и "
                              "экспериментально на RX 7000. ILION его не содержит: установите его сами с официальной страницы, а "
                              "ILION будет управлять здесь его настройками и телеметрией."));
        ImGui::Spacing();
        WrappedText(IlionTheme::TextDim, "github.com/TheAutomatic/OpenRR-ASI");
    }
    {
        ScopedCard card;
        SectionTitle(ShellText("Requirements", "Requisitos", "Requisitos", "Требования"));
        if (!RrBeginTable("##RrNeeds"))
            return;
        const ImVec4& green = IlionTheme::ShellPal::Green;
        const ImVec4& red = IlionTheme::ShellPal::Red;
        const auto need = [&](bool ok, const char* name, const std::string& value) { RrRow(ok ? green : red, true, name, value); };
        const bool archOk = in.arch == IlionOpenRr::Arch::Rdna4 || in.arch == IlionOpenRr::Arch::Rdna3;
        need(archOk, ShellText("GPU", "GPU", "GPU", "GPU"),
             in.gpuName.empty() ? std::string(RrArchName(in.arch)) : in.gpuName + " (" + RrArchName(in.arch) + ")");
        need(in.loaded, "openrr-amd.asi",
             in.loaded ? std::string(ShellText("loaded", "cargado", "carregado", "загружен"))
                       : std::string(ShellText("not installed: add it with ILION Setup (Add mods)",
                                               "no instalado: añádelo con ILION Setup (Añadir mods)",
                                               "não instalado: adicione-o com o ILION Setup (Adicionar mods)",
                                               "не установлен: добавьте через ILION Setup (Добавить моды)")));
        need(in.iniFound, "openrr-amd.ini",
             in.iniFound ? std::string(ShellText("found", "encontrado", "encontrado", "найден")) : std::string(ShellText("missing", "falta", "falta", "нет")));
        const bool kernels = in.arch == IlionOpenRr::Arch::Rdna3 ? in.shadersGfx11 : in.shadersBase && in.acoRdna4;
        need(kernels, ShellText("Kernels", "Kernels", "Kernels", "Ядра"),
             in.arch == IlionOpenRr::Arch::Rdna3 ? std::string("openrr-amd\\shaders-gfx11") : std::string("openrr-amd\\shaders + aco\\gfx120x"));
        need(in.dlssdFound && in.dlssdSupported, "nvngx_dlssd.dll",
             !in.dlssdFound ? std::string(ShellText("missing (the game's own copy, 310.9.1)", "falta (la copia del propio juego, 310.9.1)",
                                                    "falta (a cópia do próprio jogo, 310.9.1)", "нет (копия самой игры, 310.9.1)"))
                            : in.dlssdVersion + (in.dlssdSupported ? std::string() : std::string(ShellText(": needs 310.9.1", ": requiere 310.9.1",
                                                                                                             ": requer 310.9.1", ": нужна 310.9.1"))));
        ImGui::EndTable();
    }
}

// Three settings side by side in equal columns, each name above its combo with when it applies
static bool RrBeginTriple(const char* id) { return ImGui::BeginTable(id, 3, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings); }

void MenuCommon::RenderRayReconstructionTab(RenderMenuContext& ctx)
{
    using IlionOpenRr::Arch;
    using IlionOpenRr::RunState;
    auto& cfg = *ctx.config;
    const IlionOpenRr::Snapshot snap = IlionOpenRr::Get();
    const auto& in = snap.install;
    const auto& t = snap.telemetry;

    if (!in.loaded || !snap.iniLoaded)
    {
        RenderRayReconstructionMissing(snap);
        return;
    }

    const bool archOk = in.arch == Arch::Rdna4 || in.arch == Arch::Rdna3;
    const bool rdna3 = in.arch == Arch::Rdna3;
    const bool running = IlionOpenRr::RrRunning(t);
    const ImVec4 yellowFigure(1.0f, 0.86f, 0.10f, 1.0f);

    // Left: the technology, its settings and its state, one card ---------------------------------------------------------
    {
        ScopedCard card;
        SectionTitle("Ray Reconstruction");

        // What can't be used here, or conflicts with the rest of ILION: only when it happens
        if (in.arch == Arch::Nvidia)
            WrappedText(IlionTheme::TextDim,
                        ShellText("NVIDIA GPU: the game runs the real DLSS Ray Reconstruction, OpenRR stays out.",
                                  "GPU NVIDIA: el juego usa el DLSS Ray Reconstruction real, OpenRR no interviene.",
                                  "GPU NVIDIA: o jogo usa o DLSS Ray Reconstruction real, o OpenRR não intervém.",
                                  "GPU NVIDIA: игра использует настоящий DLSS Ray Reconstruction, OpenRR не вмешивается."));
        else if (!archOk)
            WrappedText(IlionTheme::LampAmber,
                        ShellText("This GPU can't run OpenRR: it needs cooperative matrices (RDNA 3 or RDNA 4).",
                                  "Esta GPU no puede ejecutar OpenRR: necesita matrices cooperativas (RDNA 3 o RDNA 4).",
                                  "Esta GPU não pode executar o OpenRR: precisa de matrizes cooperativas (RDNA 3 ou RDNA 4).",
                                  "Эта GPU не может запустить OpenRR: нужны кооперативные матрицы (RDNA 3 или RDNA 4)."));
        if (cfg.DlssNrEnabled.value_or_default())
            WrappedText(IlionTheme::LampAmber,
                        ShellText("Neural Rendering is on: don't use it with Ray Reconstruction.",
                                  "Neural Rendering está activado: no lo uses con Ray Reconstruction.",
                                  "O Neural Rendering está ativado: não o use com o Ray Reconstruction.",
                                  "Neural Rendering включён: не используйте его с Ray Reconstruction."));
        if (cfg.CustomRRUpscaler.has_value() || cfg.RRRegularUpscaler.has_value() || cfg.RRAllowRegularUpscalers.value_or_default())
            WrappedText(IlionTheme::LampAmber,
                        ShellText("ILION's own RR route (RRUpscaler) is set: it competes with OpenRR. Clear it in the ini.",
                                  "La ruta RR propia de ILION (RRUpscaler) está configurada: compite con OpenRR. Quítala en el ini.",
                                  "A rota RR própria do ILION (RRUpscaler) está configurada: concorre com o OpenRR. Remova-a no ini.",
                                  "Задан собственный путь RR в ILION (RRUpscaler): он конкурирует с OpenRR. Уберите его в ini."));

        // ILION 1.5.0: tested in Cyberpunk 2077 (RX 7900 XTX, DLSS Enabler and DLSSG-XeFG): a game started with Ray
        // Reconstruction already on never shows the generated frames for that whole session (the game's DLSSG runs, its
        // frames don't reach the screen; turning RR off doesn't bring them back). Turned on during play, both work
        // ILION 1.5.0: OptiFG takes its inputs from ILION's upscaler, which doesn't run while OpenRR denoises and upscales
        if (cfg.FGInput.value_or_default() == FGInput::Upscaler)
            WrappedText(IlionTheme::LampAmber,
                        ShellText("OptiFG (Upscaler input) generates nothing with Ray Reconstruction on: RR replaces the upscaler it takes its data "
                                  "from. Use the game's DLSSG input (via Nvngx or Streamline).",
                                  "OptiFG (entrada Upscaler) no genera con Ray Reconstruction activo: RR sustituye al escalador del que toma "
                                  "sus datos. Usa la entrada DLSSG del juego (vía Nvngx o Streamline).",
                                  "O OptiFG (entrada Upscaler) não gera com o Ray Reconstruction ativo: o RR substitui o escalador de onde "
                                  "tira os dados. Use a entrada DLSSG do jogo (via Nvngx ou Streamline).",
                                  "OptiFG (вход Upscaler) не генерирует при включённом Ray Reconstruction: RR заменяет масштабировщик, из "
                                  "которого он берёт данные. Используйте вход DLSSG игры (через Nvngx или Streamline)."));
        if (IlionOpenRr::GameDlssgFgConfigured() && IlionOpenRr::GetUserBackend() == "d3d12")
            WrappedText(IlionTheme::LampAmber,
                        ShellText("D3D12 Direct forced with frame generation: if the game starts with Ray Reconstruction on, its generated "
                                  "frames won't show. Auto uses the Vulkan bridge for that.",
                                  "D3D12 Direct forzado con generación de fotogramas: si el juego arranca con Ray Reconstruction activo, "
                                  "sus fotogramas generados no se verán. Auto usa el puente Vulkan para eso.",
                                  "D3D12 Direct forçado com geração de quadros: se o jogo iniciar com o Ray Reconstruction ativo, os "
                                  "quadros gerados não aparecerão. Auto usa a ponte Vulkan para isso.",
                                  "D3D12 Direct принудительно при генерации кадров: если игра запущена с включённым Ray Reconstruction, "
                                  "сгенерированные кадры не появятся. Авто использует мост Vulkan."));

        const char* backNow = ShellText("To go back to the game's own denoisers now, turn Ray Reconstruction off in the game's graphics menu.",
                                        "Para volver ahora a los denoisers del juego, apaga Ray Reconstruction en el menú gráfico del juego.",
                                        "Para voltar agora aos denoisers do jogo, desligue o Ray Reconstruction no menu gráfico do jogo.",
                                        "Чтобы сейчас вернуть шумодавы игры, выключите Ray Reconstruction в графическом меню игры.");
        ImGui::BeginDisabled(!archOk);
        RrSwitch(ShellText("Offer OpenRR to the game (Ray Reconstruction on Radeon)", "Ofrecer OpenRR al juego (Ray Reconstruction en Radeon)",
                           "Oferecer o OpenRR ao jogo (Ray Reconstruction em Radeon)", "Предлагать OpenRR игре (Ray Reconstruction на Radeon)"),
                 "Enabled", true, RrApply::Restart,
                 (std::string(ShellText("The game asks whether Ray Reconstruction exists when it starts: this switch decides it for the next "
                                        "start. Off: no Ray Reconstruction, the game uses its own denoisers and the upscaler of ILION.",
                                        "El juego pregunta si existe Ray Reconstruction al arrancar: este interruptor lo decide para el "
                                        "próximo arranque. Desactivado: sin Ray Reconstruction, el juego usa sus denoisers y el escalador de ILION.",
                                        "O jogo pergunta se o Ray Reconstruction existe ao iniciar: este interruptor decide isso para o "
                                        "próximo início. Desativado: sem Ray Reconstruction, o jogo usa os seus denoisers e o escalador do ILION.",
                                        "Игра спрашивает о Ray Reconstruction при запуске: переключатель решает это для следующего запуска. "
                                        "Выключено: без Ray Reconstruction, игра использует свои шумодавы и масштабирование ILION.")) +
                  "\n" + backNow)
                     .c_str());
        if (running && !IlionOpenRr::GetBool("Enabled", true))
            WrappedText(IlionTheme::LampAmber, backNow);

        ImGui::Spacing();
        if (RrBeginTriple("##RrMain"))
        {
            ImGui::TableNextColumn();
            RrName(ShellText("Network mode", "Modo de red", "Modo da rede", "Режим сети"), RrApply::Live);
            {
                const char* values[] = { "legacy", "new", "fast" };
                const char* previews[] = { "Legacy", "New", "Fast" };
                const char* items[] = { ShellText("Legacy: full precision reference", "Legacy: referencia de precisión completa",
                                                  "Legacy: referência de precisão completa", "Legacy: эталон полной точности"),
                                        ShellText("New: fast kernels (recommended)", "New: kernels rápidos (recomendado)", "New: kernels rápidos (recomendado)",
                                                  "New: быстрые ядра (рекомендуется)"),
                                        rdna3 ? ShellText("Fast: experimental, not measured on RDNA 3", "Fast: experimental, sin medir en RDNA 3",
                                                          "Fast: experimental, não medido em RDNA 3", "Fast: экспериментально, не измерено на RDNA 3")
                                              : ShellText("Fast: experimental", "Fast: experimental", "Fast: experimental", "Fast: экспериментально") };
                const std::string current = IlionOpenRr::Lower(IlionOpenRr::GetValue("Mode", "new"));
                int index = current == "legacy" ? 0 : current == "fast" ? 2 : 1;
                if (RrCombo("##RrMode", previews, items, 3, index))
                    IlionOpenRr::SetValue("Mode", values[index]);
            }
            ImGui::TableNextColumn();
            RrName(ShellText("Depth", "Profundidad", "Profundidade", "Глубина"), RrApply::Live);
            {
                const char* values[] = { "auto", "true", "false" };
                const char* previews[] = { ShellText("Auto", "Auto", "Auto", "Авто"), ShellText("Hardware", "Hardware", "Hardware", "Аппаратная"),
                                           ShellText("Linear", "Lineal", "Linear", "Линейная") };
                const char* items[] = { ShellText("Auto (what the game says)", "Auto (lo que indica el juego)", "Auto (o que o jogo indica)",
                                                  "Авто (как сообщает игра)"),
                                        ShellText("Hardware depth (NDC)", "Profundidad hardware (NDC)", "Profundidade hardware (NDC)", "Аппаратная глубина (NDC)"),
                                        ShellText("Linear depth", "Profundidad lineal", "Profundidade linear", "Линейная глубина") };
                const std::string current = IlionOpenRr::Lower(IlionOpenRr::GetValue("HardwareDepth", "auto"));
                int index = (current == "true" || current == "1") ? 1 : (current == "false" || current == "0") ? 2 : 0;
                if (RrCombo("##RrDepth", previews, items, 3, index))
                    IlionOpenRr::SetValue("HardwareDepth", values[index]);
            }
            ImGui::TableNextColumn();
            RrName("Backend", RrApply::Recreate);
            {
                // ILION 1.5.0: ILION's choice (IlionBackend); OpenRR's Backend follows it at every start (misc/IlionOpenRr.h)
                const char* values[] = { "auto", "d3d12", "vulkan" };
                const char* previews[] = { "Auto", "D3D12 Direct", ShellText("Vulkan bridge", "Puente Vulkan", "Ponte Vulkan", "Мост Vulkan") };
                const char* items[] = { ShellText("Auto: Vulkan bridge with the game's frame generation, D3D12 Direct without it (recommended)",
                                                  "Auto: puente Vulkan con la generación de fotogramas del juego, D3D12 Direct sin ella (recomendado)",
                                                  "Auto: ponte Vulkan com a geração de quadros do jogo, D3D12 Direct sem ela (recomendado)",
                                                  "Авто: мост Vulkan с генерацией кадров игры, D3D12 Direct без неё (рекомендуется)"),
                                        ShellText("D3D12 Direct: in the game's command list (fastest)", "D3D12 Direct: en la lista de comandos del juego (más rápido)",
                                                  "D3D12 Direct: na lista de comandos do jogo (mais rápido)", "D3D12 Direct: в списке команд игры (быстрее)"),
                                        ShellText("Vulkan bridge: shared resources", "Puente Vulkan: recursos compartidos", "Ponte Vulkan: recursos compartilhados",
                                                  "Мост Vulkan: общие ресурсы") };
                const std::string current = IlionOpenRr::GetUserBackend();
                int index = current == "d3d12" ? 1 : current == "vulkan" ? 2 : 0;
                if (RrCombo("##RrBackend", previews, items, 3, index))
                    IlionOpenRr::SetUserBackend(values[index]);
                if (ImGui::IsItemHovered())
                {
                    const auto why = IlionOpenRr::backendReason.load();
                    const char* now = why == IlionOpenRr::BackendReason::AutoForFg
                                          ? ShellText("This start: Vulkan bridge, because the game's frame generation is set (with D3D12 Direct at start its generated frames don't show). About 0.5 ms more.",
                                                      "En este arranque: puente Vulkan, porque está configurada la generación de fotogramas del juego (con D3D12 Direct al arrancar sus fotogramas generados no se ven). Unos 0,5 ms más.",
                                                      "Neste início: ponte Vulkan, porque a geração de quadros do jogo está configurada (com D3D12 Direct ao iniciar os quadros gerados não aparecem). Cerca de 0,5 ms a mais.",
                                                      "В этом запуске: мост Vulkan, так как настроена генерация кадров игры (с D3D12 Direct при запуске её кадры не видны). Около 0,5 мс больше.")
                                      : why == IlionOpenRr::BackendReason::AutoNoFg
                                          ? ShellText("This start: D3D12 Direct (no frame generation of the game).", "En este arranque: D3D12 Direct (sin generación de fotogramas del juego).",
                                                      "Neste início: D3D12 Direct (sem geração de quadros do jogo).", "В этом запуске: D3D12 Direct (без генерации кадров игры).")
                                          : ShellText("This start: the backend you chose.", "En este arranque: el backend que elegiste.", "Neste início: o backend que você escolheu.",
                                                      "В этом запуске: выбранный вами бэкенд.");
                    WrappedTooltip("%s", now);
                }
            }
            ImGui::EndTable();
        }
        ImGui::EndDisabled();

        // The state, under a plate of the same card
        ImGui::Spacing();
        SectionTitle(ShellText("State", "Estado", "Estado", "Состояние"));
        if (RrBeginTable("##RrState"))
        {
            const ImVec4& green = IlionTheme::ShellPal::Green;
            const ImVec4& yellow = IlionTheme::ShellPal::Yellow;
            const ImVec4& red = IlionTheme::ShellPal::Red;
            const std::string none = ShellText("no data", "sin datos", "sem dados", "нет данных");

            std::string stateValue;
            ImVec4 stateLed = yellow;
            bool stateLit = true;
            // An RR feature still alive while ILION's upscaler runs: the game went back to its normal upscaling
            if (t.featureAlive && IlionOpenRr::IlionUpscalerRunning())
                stateValue = ShellText("paused: the game uses its normal upscaling now", "en pausa: el juego usa ahora el escalado normal",
                                       "em pausa: o jogo usa agora o escalonamento normal", "на паузе: игра сейчас использует обычное масштабирование");
            else
            switch (t.state)
            {
            case RunState::Ready:
                stateValue = ShellText("network running", "red en ejecución", "rede em execução", "сеть работает");
                stateLed = green;
                break;
            case RunState::Building:
                stateValue = ShellText("building the network (no denoising yet)", "compilando la red (aún sin eliminar ruido)",
                                       "compilando a rede (ainda sem remover ruído)", "сборка сети (пока без шумоподавления)");
                break;
            case RunState::Failed:
                stateValue = ShellText("failed", "error", "erro", "ошибка");
                stateLed = red;
                break;
            case RunState::Idle:
                if (IlionOpenRr::NeedsRestartToOffer(t, IlionOpenRr::GetBool("Enabled", true)))
                {
                    // Started while it was off: the game's RR option stays grey until the game restarts
                    stateValue = ShellText("not offered at start: restart the game", "no se ofreció al arrancar: reinicia el juego",
                                           "não foi oferecido ao iniciar: reinicie o jogo", "не предложен при запуске: перезапустите игру");
                    stateLed = IlionTheme::ShellPal::Orange;
                }
                else
                {
                    stateValue = ShellText("idle: turn on RR in the game", "en reposo: activa RR en el juego", "em repouso: ative o RR no jogo",
                                           "ожидание: включите RR в игре");
                    stateLit = false;
                }
                break;
            default:
                stateValue = ShellText("no log this session", "sin registro en esta sesión", "sem registro nesta sessão", "нет журнала в этой сессии");
                stateLit = false;
                break;
            }
            const std::string build = t.build.empty() ? in.fileVersion : t.build.substr(0, t.build.find(' '));
            RrRow(stateLed, stateLit, ShellText("State", "Estado", "Estado", "Состояние"), "OpenRR " + build + " · " + stateValue);
            if (ImGui::IsItemHovered())
                WrappedTooltip("%s", ShellText("OpenRR-ASI © TheAutomatic (PolyForm Noncommercial 1.0.0) · engine OpenRR (OpenNR, MIT) · "
                                               "github.com/TheAutomatic/OpenRR-ASI. Installed by the user; ILION manages it.",
                                               "OpenRR-ASI © TheAutomatic (PolyForm Noncommercial 1.0.0) · motor OpenRR (OpenNR, MIT) · "
                                               "github.com/TheAutomatic/OpenRR-ASI. Lo instala el usuario; ILION lo gestiona.",
                                               "OpenRR-ASI © TheAutomatic (PolyForm Noncommercial 1.0.0) · motor OpenRR (OpenNR, MIT) · "
                                               "github.com/TheAutomatic/OpenRR-ASI. Instalado pelo usuário; o ILION o gere.",
                                               "OpenRR-ASI © TheAutomatic (PolyForm Noncommercial 1.0.0) · движок OpenRR (OpenNR, MIT) · "
                                               "github.com/TheAutomatic/OpenRR-ASI. Устанавливает пользователь; ILION управляет им."));

            // What runs: the mode really in use (the fast kernels may still be building), backend, compiler, GPU, precision
            std::string execution = RrModeInUse(t);
            if (t.fastBuilding)
                execution += std::string(" (") + ShellText("fast kernels building", "kernels rápidos compilando", "kernels rápidos compilando", "сборка быстрых ядер") + ")";
            else if (t.fastUnavailable)
                execution += std::string(" (") + ShellText("fast kernels unavailable", "kernels rápidos no disponibles", "kernels rápidos indisponíveis",
                                                           "быстрые ядра недоступны") + ")";
            if (!t.backend.empty())
                execution += " · " + t.backend;
            if (!t.compiler.empty())
                execution += " · " + t.compiler;
            execution += std::string(" · ") + RrArchName(in.arch) + " " + RrPrecision(in, t);
            if (t.coexistMochi)
                execution += std::string(" · ") + ShellText("with dlssnr-amd", "con dlssnr-amd", "com dlssnr-amd", "с dlssnr-amd");
            RrRow(t.fastUnavailable ? yellow : green, t.haveModeLine, ShellText("Execution", "Ejecución", "Execução", "Выполнение"), execution);
            if (ImGui::IsItemHovered())
            {
                std::string tip;
                if (!t.backendNote.empty())
                    tip += std::string(ShellText("Backend note (OpenRR): ", "Nota del backend (OpenRR): ", "Nota do backend (OpenRR): ", "Заметка бэкенда (OpenRR): ")) +
                           t.backendNote;
                if (t.fastUnavailable && !t.fastUnavailableWhy.empty())
                    tip += (tip.empty() ? "" : "\n") + t.fastUnavailableWhy;
                if (t.coexistMochi)
                    tip += std::string(tip.empty() ? "" : "\n") +
                           ShellText("dlssnr-amd.asi detected: OpenRR uses D3D12 Direct and doesn't split the command lists.",
                                     "dlssnr-amd.asi detectado: OpenRR usa D3D12 Direct y no divide las listas de comandos.",
                                     "dlssnr-amd.asi detectado: o OpenRR usa D3D12 Direct e não divide as listas de comandos.",
                                     "Обнаружен dlssnr-amd.asi: OpenRR использует D3D12 Direct и не разделяет списки команд.");
                if (rdna3)
                    tip += std::string(tip.empty() ? "" : "\n") +
                           ShellText("RDNA 3: FP16 kernels compiled by the driver (LLPC), experimental in OpenRR. FP8 and the offline ACO kernels are RDNA 4 only.",
                                     "RDNA 3: kernels FP16 compilados por el driver (LLPC), experimental en OpenRR. FP8 y los kernels ACO offline son solo de RDNA 4.",
                                     "RDNA 3: kernels FP16 compilados pelo driver (LLPC), experimental no OpenRR. FP8 e os kernels ACO offline são só da RDNA 4.",
                                     "RDNA 3: ядра FP16, компилируемые драйвером (LLPC), экспериментально в OpenRR. FP8 и офлайн-ядра ACO только для RDNA 4.");
                if (!tip.empty())
                    WrappedTooltip("%s", tip.c_str());
            }

            // Who upscales: with RR the network denoises and upscales, the upscaler of ILION waits
            RrRow(yellow, running, ShellText("Upscaling", "Escalado", "Escala", "Масштабирование"),
                  running ? StrFmt(ShellText("RR %ux%u -> %ux%u · ILION's upscaler paused", "RR %ux%u -> %ux%u · escalador de ILION en pausa",
                                             "RR %ux%u -> %ux%u · escalador do ILION em pausa", "RR %ux%u -> %ux%u · масштабирование ILION на паузе"),
                                   t.renderW, t.renderH, t.outputW, t.outputH)
                          : std::string(ShellText("ILION's upscaler", "escalador de ILION", "escalador do ILION", "масштабирование ILION")));

            // The plugin's own figures: its last frame and the p50 / p95 of its 64 samples, with their age
            if (t.haveStats)
            {
                RrRow(green, true, ShellText("Network (GPU)", "Red (GPU)", "Rede (GPU)", "Сеть (GPU)"),
                      StrFmt(ShellText("%.2f ms · p50 %.2f · p95 %.2f · %d s ago", "%.2f ms · p50 %.2f · p95 %.2f · hace %d s",
                                       "%.2f ms · p50 %.2f · p95 %.2f · há %d s", "%.2f мс · p50 %.2f · p95 %.2f · %d с назад"),
                             t.rawLast, t.rawP50, t.rawP95, IlionOpenRr::StatAgeSeconds(t)),
                      &yellowFigure);
                if (ImGui::IsItemHovered())
                    WrappedTooltip("%s", StrFmt(ShellText("OpenRR writes one figure every 600 network frames (this one: frame %llu, %u samples). "
                                                          "Without async compute (its estimate): %.2f ms, average %.2f ms.",
                                                          "OpenRR escribe una cifra cada 600 fotogramas de red (esta: fotograma %llu, %u muestras). "
                                                          "Sin cómputo asíncrono (su estimación): %.2f ms, media %.2f ms.",
                                                          "O OpenRR escreve um valor a cada 600 quadros da rede (este: quadro %llu, %u amostras). "
                                                          "Sem computação assíncrona (a sua estimativa): %.2f ms, média %.2f ms.",
                                                          "OpenRR пишет значение каждые 600 кадров сети (это: кадр %llu, %u выборок). "
                                                          "Без асинхронных вычислений (его оценка): %.2f мс, среднее %.2f мс."),
                                                t.statFrame, t.statSamples, t.cleanLast, t.cleanAverage)
                                             .c_str());
            }
            else
                RrRow(yellow, false, ShellText("Network (GPU)", "Red (GPU)", "Rede (GPU)", "Сеть (GPU)"),
                      ShellText("no figure yet (one every 600 network frames)", "aún sin cifra (una cada 600 fotogramas de red)",
                                "ainda sem valor (um a cada 600 quadros da rede)", "пока нет значения (раз в 600 кадров сети)"));

            RrRow(in.dlssdSupported ? green : red, t.vramMb >= 0 || in.dlssdFound, ShellText("VRAM · weights", "VRAM · pesos", "VRAM · pesos", "VRAM · веса"),
                  (t.vramMb >= 0 ? StrFmt("%d MB", t.vramMb) : none) + " · nvngx_dlssd " +
                      (!t.weightsVersion.empty() ? t.weightsVersion : in.dlssdFound ? in.dlssdVersion : none));
            if (ImGui::IsItemHovered() && !t.weightsPath.empty())
                WrappedTooltip("%s", t.weightsPath.c_str());

            if (!t.lastProblem.empty())
                RrRow(red, true, ShellText("Last problem", "Último problema", "Último problema", "Последняя проблема"), t.lastProblem);
            else if (!t.checkPassed && !t.checkFailure.empty())
                RrRow(red, true, ShellText("Check", "Comprobación", "Verificação", "Проверка"), t.checkFailure);
            ImGui::EndTable();
        }
    }

    // Right: the guides and the advanced execution, one card --------------------------------------------------------------
    {
        ScopedCard card;
        SectionTitle(ShellText("Guides", "Guías", "Guias", "Направляющие"));
        ImGui::BeginDisabled(!archOk);
        const auto given = [&](const char* guide) -> int
        {
            if (!t.guidesSeen)
                return -1;
            const auto it = t.guides.find(guide);
            return it == t.guides.end() ? -1 : it->second;
        };
        const char* guideTip = ShellText("Each game decides which optional inputs it gives to Ray Reconstruction: OpenRR reads them from "
                                         "the game and writes it in its log. A guide the game doesn't give changes nothing; the switch is "
                                         "for games that give it. All apply live.",
                                         "Cada juego decide qué entradas opcionales da a Ray Reconstruction: OpenRR las lee del juego y lo "
                                         "anota en su registro. Una guía que el juego no aporta no cambia nada; el interruptor es para los "
                                         "juegos que sí la aportan. Todas se aplican en vivo.",
                                         "Cada jogo decide quais entradas opcionais dá ao Ray Reconstruction: o OpenRR as lê do jogo e anota "
                                         "no seu registro. Uma guia que o jogo não fornece não muda nada; o interruptor é para os jogos que a "
                                         "fornecem. Todas se aplicam ao vivo.",
                                         "Каждая игра решает, какие дополнительные входы даёт Ray Reconstruction: OpenRR читает их из игры и "
                                         "пишет в журнал. Направляющая, которую игра не даёт, ничего не меняет; переключатель для игр, которые "
                                         "её дают. Все применяются сразу.");
        const auto guideRow = [&](const char* label, const char* key, bool fallback, const char* guide) -> int
        {
            bool value = IlionOpenRr::GetBool(key, fallback);
            if (IlionTheme::Checkbox(label, &value))
                IlionOpenRr::SetBool(key, value);
            if (ImGui::IsItemHovered())
                WrappedTooltip("%s", guideTip);
            const int g = given(guide);
            const char* text = g < 0   ? ShellText("no data", "sin datos", "sem dados", "нет данных")
                               : g > 0 ? ShellText("the game gives it", "el juego la aporta", "o jogo a fornece", "игра её даёт")
                                       : ShellText("not given by the game", "el juego no la aporta", "o jogo não a fornece", "игра её не даёт");
            const float w = ImGui::CalcTextSize(text).x;
            ImGui::SameLine();
            const float avail = ImGui::GetContentRegionAvail().x;
            if (avail >= w + ImGui::GetStyle().ItemSpacing.x * 2.0f)
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + avail - w);
            else
                ImGui::NewLine();
            if (g > 0)
                ImGui::TextColored(IlionTheme::LampGreen, "%s", text);
            else
                ImGui::TextDisabled("%s", text);
            if (ImGui::IsItemHovered())
                WrappedTooltip("%s", guideTip);
            return g;
        };

        // The disocclusion mask's own settings: disabled while the game doesn't give the mask (they change nothing then)
        const int dmGiven =
            guideRow(ShellText("Disocclusion mask", "Máscara de desoclusión", "Máscara de desoclusão", "Маска раскрытия"), "UseDisocclusionMask", true, "dm");
        ImGui::BeginDisabled(dmGiven == 0);
        if (RrBeginTriple("##RrDm"))
        {
            ImGui::TableNextColumn();
            {
                bool value = IlionOpenRr::GetBool("InvertDisocclusionMask", false);
                if (IlionTheme::Checkbox(ShellText("Invert", "Invertir", "Inverter", "Инверсия"), &value))
                    IlionOpenRr::SetBool("InvertDisocclusionMask", value);
            }
            ImGui::TableNextColumn();
            {
                bool value = IlionOpenRr::GetBool("ClampDisocclusionMask", true);
                if (IlionTheme::Checkbox(ShellText("Clamp 0..1", "Limitar 0..1", "Limitar 0..1", "Предел 0..1"), &value))
                    IlionOpenRr::SetBool("ClampDisocclusionMask", value);
            }
            ImGui::TableNextColumn();
            {
                float scale = IlionOpenRr::GetFloat("DisocclusionMaskScale", 1.0f);
                ImGui::SetNextItemWidth(-FLT_MIN);
                const std::string format = std::string(ShellText("Scale %.2f", "Escala %.2f", "Escala %.2f", "Масштаб %.2f"));
                IlionTheme::SliderFloat("##RrDmScale", &scale, 0.1f, 4.0f, format.c_str(), ImGuiSliderFlags_AlwaysClamp);
                if (ImGui::IsItemDeactivatedAfterEdit())
                    IlionOpenRr::SetValue("DisocclusionMaskScale", StrFmt("%.2f", scale));
            }
            ImGui::EndTable();
        }
        ImGui::EndDisabled();
        guideRow(ShellText("SSS guide (colour before fog)", "Guía SSS (color antes de la niebla)", "Guia SSS (cor antes da névoa)",
                           "Направляющая SSS (цвет до тумана)"),
                 "UseSssGuide", true, "sss");
        guideRow(ShellText("Responsivity mask", "Máscara de respuesta", "Máscara de resposta", "Маска отклика"), "UseResponsivityMask", false, "resp");
        guideRow(ShellText("Colour before transparency", "Color antes de transparencias", "Cor antes das transparências", "Цвет до прозрачности"),
                 "UseColorBeforeTransparency", false, "cbt");
        guideRow(ShellText("Depth of field guide", "Guía de profundidad de campo", "Guia de profundidade de campo", "Направляющая глубины резкости"),
                 "UseDepthOfFieldGuide", false, "dof");
        guideRow(ShellText("Reflection motion vectors", "Vectores de movimiento de reflejos", "Vetores de movimento dos reflexos",
                           "Векторы движения отражений"),
                 "UseReflectionMotionVectors", false, "smv");
        guideRow(ShellText("Transparency opacity and motion", "Opacidad y movimiento de transparencias", "Opacidade e movimento das transparências",
                           "Непрозрачность и движение прозрачности"),
                 "UseLayerOpacityAndMotion", false, "tl_op");

        // The advanced execution, under a plate of the same card
        ImGui::Spacing();
        SectionTitle(ShellText("Advanced execution", "Ejecución avanzada", "Execução avançada", "Расширенное выполнение"));
        if (RrBeginTriple("##RrExec"))
        {
            ImGui::TableNextColumn();
            RrName(ShellText("Split lists", "Dividir listas", "Dividir listas", "Делить списки"), RrApply::Recreate);
            {
                const char* previews[] = { "Auto", ShellText("Off", "Desactivada", "Desativada", "Выкл.") };
                const char* items[] = { ShellText("Auto (off when dlssnr-amd is present)", "Auto (desactivada si está dlssnr-amd)",
                                                  "Auto (desativada se houver dlssnr-amd)", "Авто (выкл. при наличии dlssnr-amd)"),
                                        ShellText("Off: never split the command lists", "Desactivada: nunca dividir las listas de comandos",
                                                  "Desativada: nunca dividir as listas de comandos", "Выкл.: никогда не разделять списки команд") };
                const std::string current = IlionOpenRr::Lower(IlionOpenRr::GetValue("CoexistSplit", "auto"));
                int index = (current == "off" || current == "0" || current == "false") ? 1 : 0;
                if (RrCombo("##RrSplit", previews, items, 2, index))
                    IlionOpenRr::SetValue("CoexistSplit", index == 1 ? "off" : "auto");
            }
            ImGui::TableNextColumn();
            RrName(ShellText("Vulkan inputs", "Entradas Vulkan", "Entradas Vulkan", "Входы Vulkan"), RrApply::Live);
            {
                const char* previews[] = { "Shader read", "General" };
                const char* items[] = { ShellText("Shader read optimal (Vulkan games and bridge)", "Shader read optimal (juegos Vulkan y puente)",
                                                  "Shader read optimal (jogos Vulkan e ponte)", "Shader read optimal (игры Vulkan и мост)"),
                                        "General" };
                const std::string current = IlionOpenRr::Lower(IlionOpenRr::GetValue("VkInputLayout", "shader_read"));
                int index = current == "general" ? 1 : 0;
                if (RrCombo("##RrVkLayout", previews, items, 2, index))
                    IlionOpenRr::SetValue("VkInputLayout", index == 1 ? "general" : "shader_read");
            }
            ImGui::TableNextColumn();
            // OptiFG host: OpenRR patches the code of the OptiScaler dll in memory to reach it. ILION doesn't allow it
            RrName("OptiFG Host", RrApply::Restart);
            {
                const bool host = IlionOpenRr::GetBool("OptiFGHost", false);
                const char* previews[] = { ShellText("Blocked", "Bloqueado", "Bloqueado", "Заблокировано") };
                int index = 0;
                ImGui::BeginDisabled(!host);
                if (host)
                {
                    const char* on[] = { ShellText("On: turn off", "Activo: desactivar", "Ativo: desativar", "Вкл.: выключить") };
                    const char* onItems[] = { ShellText("Turn off OptiFGHost (restart the game)", "Desactivar OptiFGHost (reinicia el juego)",
                                                        "Desativar OptiFGHost (reinicie o jogo)", "Выключить OptiFGHost (перезапустите игру)") };
                    ImGui::PushID("RrHostOn");
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    if (IlionTheme::BeginCombo("##RrHost", on[0], 0, &IlionTheme::LampAmber))
                    {
                        if (ImGui::Selectable(onItems[0], false))
                            IlionOpenRr::SetBool("OptiFGHost", false);
                        ImGui::EndCombo();
                    }
                    ImGui::PopID();
                }
                else
                    RrCombo("##RrHost", previews, previews, 1, index);
                ImGui::EndDisabled();
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                    WrappedTooltip("%s", ShellText("OpenRR's frame generation compatibility with OptiFG. Blocked in ILION: it patches ILION's code "
                                                   "in memory. Frame generation through the game's DLSSG works without it.",
                                                   "Compatibilidad de OpenRR con la generación de fotogramas de OptiFG. Bloqueada en ILION: "
                                                   "parchea en memoria el código de ILION. La generación de fotogramas a través del DLSSG del "
                                                   "juego funciona sin ella.",
                                                   "Compatibilidade do OpenRR com a geração de quadros do OptiFG. Bloqueada no ILION: altera na "
                                                   "memória o código do ILION. A geração de quadros pelo DLSSG do jogo funciona sem ela.",
                                                   "Совместимость OpenRR с генерацией кадров OptiFG. Заблокирована в ILION: правит код ILION в "
                                                   "памяти. Генерация кадров через DLSSG игры работает без неё."));
            }
            ImGui::EndTable();
        }
        ImGui::EndDisabled();
    }
}
