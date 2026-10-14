#pragma once
// ILION experiment: the DLSS indicator shown next to the upscaler selector when the external d4r backend is in use.
// Every value comes from IlionD4r (NGX's answers, the NGX parameters, the loaded shim and, when it can be read,
// the d4r log); nothing is assumed from what is selected in the menu.

#include <imgui/imgui.h>
#include <misc/IlionD4r.h>
#include "ilion_lang.h"
#include "ilion_theme.h"

#include <string>

namespace IlionD4rPanel
{
// Why there is no evidence from d4r's log
inline const char* LogReason(const IlionD4r::LogState& log)
{
    using Ilion::Tr;
    if (log.locked)
        return Tr("d4r keeps its log locked while the game runs");
    if (log.stale)
        return Tr("the d4r log is from an earlier session");
    if (log.readable)
        return Tr("d4r has not written its configuration");
    return Tr("d4r log not found");
}

// logPath: d4r's own log. version*: the DLSS library OptiScaler loaded and read the version from.
inline void Draw(const std::wstring& logPath, unsigned versionMajor, unsigned versionMinor, unsigned versionPatch)
{
    using namespace IlionD4r;
    using Ilion::Tr;

    LogState log;
    uint64_t failuresThisFeature = 0;
    const Evidence e = GetLogReader().Poll(logPath, &log, &failuresThisFeature);
    const auto& calls = GetCalls();
    const uint64_t ok = calls.evaluateOk.load();
    const uint64_t failed = calls.evaluateFailed.load();
    const bool evaluated = calls.evaluatedOnce.load();
    const uint32_t lastResult = calls.lastResult.load();
    const uint32_t presetBefore = calls.presetBefore.load();
    const uint32_t presetAfter = calls.presetAfter.load();
    const char presetLetter = PresetLetter(presetAfter);

    // Evidence written by d4r itself, only when its log is readable and belongs to this session
    const bool logKnown = log.readable && e.sawConfigLine && (e.requestedModel == "K" || e.requestedModel == "M");
    const bool isM = logKnown && e.requestedModel == "M";
    const bool isK = logKnown && !isM;
    const int total = isM ? kCountM : kCountK;
    const int bound = Evidence::Count(isM ? e.boundM : e.boundK);
    const int launchedNative = Evidence::Count(isM ? e.launchedNativeM : e.launchedNativeK);
    const int launchedOther = Evidence::Count(isM ? e.launchedOtherM : e.launchedOtherK);

    const bool stageBound = logKnown && bound == total;
    const bool failing = (evaluated && lastResult != 1) || failuresThisFeature > 0;
    const bool stageEvaluated = stageBound && ok > 0 && !failing;
    const bool stageExecuted = stageEvaluated && launchedNative == total && launchedOther == 0;
    const std::string architecture = LoadedShimArchitecture();

    // Headline. The generation name is only written once its own condition is met:
    // model K after error-free evaluations with its 11 layers bound; model M only with native execution confirmed.
    // Without that, only what is known is written: the preset that was requested, never a model as running.
    std::string headline = "DLSS";
    if (isK && stageEvaluated)
        headline += " 4";
    else if (isM && stageExecuted)
        headline += " 4.5";

    headline += " | ";
    if (stageBound)
        headline += std::string(Tr("Model")) + " " + e.requestedModel;
    else if (presetLetter)
        headline += std::string(Tr("Preset")) + " " + presetLetter + " " + Tr("requested");
    else
        headline += Tr("Model not verified");

    char text[96];
    snprintf(text, sizeof text, " | NGX %u.%u.%u", versionMajor, versionMinor, versionPatch);
    headline += text;

    if (!architecture.empty())
        headline += " | AMD " + architecture;

    headline += " | d4r";

    const ImVec4 lamp = failing ? IlionTheme::LampRed : stageEvaluated ? IlionTheme::LampGreen : IlionTheme::LampAmber;
    ImGui::TextColored(lamp, "%s", headline.c_str());

    if (failing)
        ImGui::TextColored(IlionTheme::LampRed, "%s", Tr("The upscaler is failing"));

    const char* notVerified = Tr("Not verified");

    // 1. Requested: the preset in the NGX parameters of the last evaluation, as d4r left it
    if (presetLetter)
        ImGui::TextColored(IlionTheme::Text, "1 %s: %s %c (%u) %s%s%s", Tr("Requested"), Tr("Preset"), presetLetter, presetAfter,
                           Tr("in the NGX parameters"), presetBefore != presetAfter ? ", " : "",
                           presetBefore != presetAfter ? Tr("set by d4r on each evaluation") : "");
    else if (presetAfter == kPresetMixed)
        ImGui::TextColored(IlionTheme::LampAmber, "1 %s: %s (%s)", Tr("Requested"), notVerified,
                           Tr("presets differ between quality modes"));
    else if (logKnown)
        ImGui::TextColored(IlionTheme::Text, "1 %s: %s %s (d4r.ini)", Tr("Requested"), Tr("Model"), e.requestedModel.c_str());
    else
        ImGui::TextColored(IlionTheme::LampAmber, "1 %s: %s", Tr("Requested"), notVerified);

    // 2. Initialised: layers of that model bound to native kernel files, which only d4r's log states
    if (logKnown)
        ImGui::TextColored(stageBound ? IlionTheme::Text : IlionTheme::LampAmber, "2 %s: %s %s, %d/%d %s", Tr("Initialised"),
                           Tr("Model"), e.requestedModel.c_str(), bound, total, Tr("layers bound to native kernels"));
    else
        ImGui::TextColored(IlionTheme::LampAmber, "2 %s: %s (%s)", Tr("Initialised"), notVerified, LogReason(log));

    // 3. Evaluations NGX accepted for the current feature, and what d4r reported while running them
    if (evaluated)
    {
        if (log.readable)
            ImGui::TextColored(failing ? IlionTheme::LampRed : IlionTheme::Text, "3 %s: %llu %s, %llu %s, %llu %s",
                               Tr("Evaluations"), (unsigned long long) ok, Tr("accepted"), (unsigned long long) failed,
                               Tr("rejected"), (unsigned long long) failuresThisFeature, Tr("d4r errors"));
        else
            ImGui::TextColored(failing ? IlionTheme::LampRed : IlionTheme::Text, "3 %s: %llu %s, %llu %s", Tr("Evaluations"),
                               (unsigned long long) ok, Tr("accepted"), (unsigned long long) failed, Tr("rejected"));

        if (failed > 0 && lastResult != 1)
            ImGui::TextColored(IlionTheme::LampRed, "  NGX 0x%08X", lastResult);
        if (failuresThisFeature > 0 && !e.lastFailure.empty())
            ImGui::TextColored(IlionTheme::LampRed, "  %.90s", e.lastFailure.c_str());
    }
    else
    {
        ImGui::TextColored(IlionTheme::LampAmber, "3 %s: %s", Tr("Evaluations"), notVerified);
    }

    // 4. Native execution: only launch lines prove it, and d4r writes them only with Log = verbose
    if (logKnown && launchedNative + launchedOther > 0)
    {
        if (launchedOther == 0 && launchedNative == total)
            ImGui::TextColored(IlionTheme::LampGreen, "4 %s: %d/%d %s", Tr("Native execution"), launchedNative, total,
                               Tr("layers launched native"));
        else
            ImGui::TextColored(IlionTheme::LampRed, "4 %s: %d/%d %s, %d %s", Tr("Native execution"), launchedNative, total,
                               Tr("layers launched native"), launchedOther, Tr("not native"));
    }
    else
    {
        // Without launch lines it stays not verified; what the normal log does show is said next to it
        const int missed = Evidence::Count(isM ? e.missedM : e.missedK);

        if (logKnown && missed > 0)
            ImGui::TextColored(IlionTheme::LampRed, "4 %s: %d/%d %s", Tr("Native execution"), missed, total,
                               Tr("layers fell back to translated code"));
        else if (stageBound)
            ImGui::TextColored(IlionTheme::TextDim, "4 %s: %s (%s)", Tr("Native execution"), notVerified,
                               Tr("no layer fell back to translated code; launches are only logged with Log = verbose"));
        else
            ImGui::TextColored(IlionTheme::TextDim, "4 %s: %s", Tr("Native execution"), notVerified);
    }

    // What every accepted evaluation proves with d4r's own settings: async=0 waits for the GPU before returning,
    // validate=1 fails the evaluation on any NaN or Inf. Accepted evaluations alone prove neither.
    if (logKnown && ok > 0 && !failing)
    {
        if (e.synchronous)
            ImGui::TextColored(IlionTheme::TextDim, "  %s", Tr("GPU work of every accepted evaluation finished without CUDA errors"));
        if (e.validateOutput)
            ImGui::TextColored(IlionTheme::TextDim, "  %s", Tr("Every accepted output was finite (d4r checks NaN/Inf)"));
    }

    // Only when d4r's log has them: its own timing of the inference and its output check
    if (log.readable && e.framesDone > 0 && e.lastInteropNgxMs >= 0.0)
        ImGui::TextColored(IlionTheme::TextDim, "  %s: %.2f ms (D4R_FRAME)", Tr("d4r inference"), e.lastInteropNgxMs);
    if (log.readable && e.validations > 0)
        ImGui::TextColored(e.validationsBad ? IlionTheme::LampRed : IlionTheme::TextDim, "  %s: %llu, NaN/Inf: %llu",
                           Tr("Outputs checked"), (unsigned long long) e.validations, (unsigned long long) e.validationsBad);
}

// Next to the DLSS preset controls: with d4r the model does not come from them
inline void PresetNote()
{
    using Ilion::Tr;
    const uint32_t preset = IlionD4r::GetCalls().presetAfter.load();
    if (const char letter = IlionD4r::PresetLetter(preset))
        ImGui::TextColored(IlionTheme::LampAmber, "%s %c (d4r.ini). %s", Tr("d4r imposes model"), letter,
                           Tr("These preset controls do not change it."));
    else
        ImGui::TextColored(IlionTheme::LampAmber, "%s", Tr("d4r imposes its own model (d4r.ini). These preset controls do not change it."));
}
} // namespace IlionD4rPanel
