// ILION v1.4.0 R3: regression test for "Use target as cap" of Dynamic MFG. A fixed multiplier (Dynamic MFG off) must
// never be capped, whatever the saved cap setting, provider or target; Dynamic on keeps the cap; switching leaves no
// residual cap. Build: cl /std:c++latest /EHsc /I <OptiScaler> ilion_dmfg_cap_test.cpp
#include <misc/IlionDynamicCapRule.h>

#include <cstdio>

using namespace IlionDynamicCapRule;

static int failures = 0, checks = 0;

static void Expect(bool ok, const char* what)
{
    checks++;
    if (!ok)
    {
        failures++;
        std::printf("FALLO: %s\n", what);
    }
}

static Inputs Base(float target)
{
    Inputs in;
    in.targetAsCap = true;
    in.target = target;
    in.ownRunning = true;
    in.externalGenerating = true;
    in.xefgControllerCap = target;
    return in;
}

int main()
{
    const Provider all[] = { Provider::None, Provider::XeFG, Provider::DlssgGame, Provider::DlssgOutput, Provider::FsrFg };
    const float targets[] = { 0.0f, 60.0f, 116.0f, 165.0f, 240.0f };

    // 1. Dynamic MFG off: no cap from it, for every provider, target and saved cap setting
    for (Provider p : all)
        for (float t : targets)
            for (int saved = 0; saved < 2; saved++)
            {
                Inputs in = Base(t);
                in.targetAsCap = saved == 1;
                Expect(Cap(p, in) == 0.0f, "Dynamic MFG off: a fixed multiplier is capped");
            }

    // 2. Dynamic MFG on: the cap holds where the provider runs Dynamic
    {
        Inputs in = Base(165.0f);
        in.xefgDynamic = true;
        Expect(Cap(Provider::XeFG, in) == 165.0f, "XeFG Dynamic: the controller's cap");
        in.xefgControllerCap = 0.0f;
        Expect(Cap(Provider::XeFG, in) == 0.0f, "XeFG Dynamic before its controller sets a cap");
    }
    {
        Inputs in = Base(165.0f);
        in.dlssgOutputDynamic = true;
        Expect(Cap(Provider::DlssgOutput, in) == 165.0f, "ILION's DLSSG in Dynamic: the target");
    }
    {
        Inputs in = Base(165.0f);
        in.ilionDrivesExternal = true;
        Expect(Cap(Provider::DlssgGame, in) == 165.0f, "ILION Dynamic on the game's DLSSG: the target");
        in.externalGenerating = false;
        Expect(Cap(Provider::DlssgGame, in) == 0.0f, "ILION Dynamic, the game's DLSSG not generating");
    }
    {
        Inputs in = Base(165.0f);
        in.xefgDynamic = in.dlssgOutputDynamic = in.ilionDrivesExternal = true;
        Expect(Cap(Provider::FsrFg, in) == 0.0f, "FSR FG has no Dynamic: never capped");
        in.targetAsCap = false;
        for (Provider p : all)
            Expect(Cap(p, in) == 0.0f, "Dynamic on but \"Use target as cap\" off: no cap");
        in.targetAsCap = true;
        in.vulkanBridge = true;
        for (Provider p : all)
            Expect(Cap(p, in) == 0.0f, "Vulkan bridge keeps its own limiter");
    }

    // 3. On -> off -> on: the cap comes and goes with Dynamic, nothing left behind
    {
        Inputs in = Base(144.0f);
        for (int round = 0; round < 3; round++)
        {
            in.xefgDynamic = true;
            Expect(Cap(Provider::XeFG, in) == 144.0f, "XeFG: Dynamic on applies the cap");
            in.xefgDynamic = false;
            Expect(Cap(Provider::XeFG, in) == 0.0f, "XeFG: Dynamic off removes the cap");
            in.ilionDrivesExternal = true;
            Expect(Cap(Provider::DlssgGame, in) == 144.0f, "game's DLSSG: ILION Dynamic on applies the cap");
            in.ilionDrivesExternal = false;
            Expect(Cap(Provider::DlssgGame, in) == 0.0f, "game's DLSSG: fixed multiplier, no cap");
        }
    }

    std::printf("ILION DYNAMIC MFG CAP: %d comprobaciones, %d fallos\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
