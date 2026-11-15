#pragma once
#include "Nvngx_DllProxy.h"

class Nvngx_FakeMichau final : public Nvngx_DllProxy
{
    int _maxFakeFrames = 1;
    void LoadLibraries() override;

  public:
    Nvngx_FakeMichau() : Nvngx_DllProxy() { LoadLibraries(); }
    int getMaxFakeFramesCount() override { return _maxFakeFrames; }
    FGNvngxReplacement getType() override { return FGNvngxReplacement::FakeMichauXeFG; }
    static std::filesystem::path FindDll();
};
