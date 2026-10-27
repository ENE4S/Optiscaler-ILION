// Copyright (c) 2026 3zwr1 (AMDNR). Part of AMDNR (GPL-3.0; see Licenses/AMDNR_NOTICE.txt).
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// THE SHADER COMPILER OUR OWN PASSES USE.
//
// OptiScaler.dll imports d3dcompiler_47.dll by name. Loaded from the game folder, that name is
// resolved against the game folder first, so a title that ships its own d3dcompiler_47.dll (older
// UE4 builds do, some from 2013-2015) compiles OUR shaders with ITS compiler. The danielblnc
// runtime module already has its import rebound to the System32 compiler for exactly this reason
// (AmdPreSr.cpp: "Older games ship a 2013 D3DCompiler that rejects the FP16 typed UAV load
// shader"); the host's own passes - the temporal pass, the resample/encode/mix/sharpen passes,
// the lmxxf backend's - still went through the import. The report from Where Winds Meet
// (yysls.exe; the reporter first called it Wuthering Waves): "AMD temporal D3D12 error 2147500037" (E_FAIL) at the
// first Run on every launch of 0.3.0 while 0.1.0 ran - the temporal shader grew a great deal
// between the two, the compiler on that machine did not, and the same source compiles here with
// both the 22621 and the 26100 compiler.
//
// So every runtime compile in the AMD path goes through this: the system's own d3dcompiler_47,
// loaded by full path (a bare name would hand back the game's already-loaded copy), the linked
// import only if the system one cannot be had. Header-only so the lmxxf runtime DLL, which sits
// beside the game's exe with the same exposure, can use it too.
#include <windows.h>
#include <d3dcompiler.h>
#include <string>

namespace DlssNr::SysCompiler
{
struct State
{
    pD3DCompile compile = nullptr;
    std::wstring path;    // the System32 compiler, when it loaded
    std::wstring foreign; // a d3dcompiler_47 the process already carried from somewhere else, if any
};
inline const State& Get()
{
    static const State state = [] {
        State s;
        if (HMODULE present = GetModuleHandleW(L"d3dcompiler_47.dll"))
        {
            wchar_t buf[MAX_PATH] {};
            if (GetModuleFileNameW(present, buf, MAX_PATH))
                s.foreign = buf;
        }
        wchar_t sys[MAX_PATH] {};
        const UINT n = GetSystemDirectoryW(sys, MAX_PATH);
        if (n && n < MAX_PATH)
        {
            const std::wstring full = std::wstring(sys) + L"\\d3dcompiler_47.dll";
            if (HMODULE m = LoadLibraryExW(full.c_str(), nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32))
            {
                s.compile = reinterpret_cast<pD3DCompile>(GetProcAddress(m, "D3DCompile"));
                if (s.compile)
                    s.path = full;
            }
        }
        if (!s.foreign.empty() && !s.path.empty() && _wcsicmp(s.foreign.c_str(), s.path.c_str()) == 0)
            s.foreign.clear(); // the one already present was the system's own
        return s;
    }();
    return state;
}
inline HRESULT Compile(LPCVOID src, SIZE_T size, LPCSTR name, const D3D_SHADER_MACRO* defines, ID3DInclude* include,
                       LPCSTR entry, LPCSTR target, UINT flags1, UINT flags2, ID3DBlob** code, ID3DBlob** errors)
{
    const State& s = Get();
    if (s.compile)
        return s.compile(src, size, name, defines, include, entry, target, flags1, flags2, code, errors);
    return D3DCompile(src, size, name, defines, include, entry, target, flags1, flags2, code, errors);
}
} // namespace DlssNr::SysCompiler
