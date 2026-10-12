# Third-party notices

OptiScaler, and ILION through it, uses the components below. They belong to their authors and keep their own
licences; the GPL-3.0 of this repository does not replace them. Every copyright notice inside those files is kept
as it came.

ILION itself adds no third-party code and no binary: everything listed here comes from OptiScaler at commit
`f6f75ddf0118ac45f447e2babbe18dbda3451edd`. Eighteen files of that commit are left out of this repository; section 4
lists them and says where to get the ones that are still needed.

This list was put together by reading the files and the licence texts found in the repository and in the pinned
submodules. It is not legal advice. Where a licence could not be confirmed from those files, the entry says so.

## 1. Git submodules (not stored in this repository)

A submodule is a reference to another repository at a fixed commit. Their files, including the pre-built DLLs some
of them carry (AMD FidelityFX, Intel XeSS, NVAPI), are downloaded from their owners when cloning with
`--recurse-submodules`; this repository does not contain or redistribute them.

| Path | Repository | Pinned commit | Licence, as stated in that repository |
|---|---|---|---|
| `external/FidelityFX-SDK` | GPUOpen-LibrariesAndSDKs/FidelityFX-SDK | `c6efa6bf` (v1.1.4) | MIT (AMD), `LICENSE.txt` |
| `external/FidelityFX-SDK-v2` | GPUOpen-LibrariesAndSDKs/FidelityFX-SDK | `60f4ea81` (v2.3.0) | AMD licence in `docs/license.md`: redistribution in binary form only, no reverse engineering. The code under `Kits/OpenSource` is MIT. |
| `external/xess` | intel/xess | `8fe81bdb` (v3.0.2) | Intel Simplified Software License (October 2022), `LICENSE.txt` |
| `external/nvapi` | NVIDIA/nvapi | `9b181ea5` | MIT (NVIDIA), `License.txt` |
| `external/vulkan` | KhronosGroup/Vulkan-Headers | `d64e9e15` | Apache-2.0 / MIT, see `LICENSE.md` |
| `external/spdlog` | gabime/spdlog | `faa0a7a9` | MIT |
| `external/simpleini` | brofield/simpleini | `6048871e` | MIT |
| `external/magic_enum` | Neargye/magic_enum | `a733a2ea` | MIT |
| `external/unordered_dense` | martinus/unordered_dense | `73f3cbb2` | MIT |

## 2. Source code stored in this repository

| Path | What it is | Licence, from the file headers |
|---|---|---|
| `OptiScaler/include/imgui` | Dear ImGui, with changes by OptiScaler | MIT, `LICENSE.txt` in the folder |
| `OptiScaler/include/fsr2`, `fsr2_212`, `fsr3`, `fsr31` | AMD FidelityFX FSR headers | MIT (AMD) |
| `OptiScaler/include/device_info` | GPUOpen-Tools device_info | MIT (AMD) |
| `OptiScaler/include/detours` | Microsoft Detours headers | MIT (Microsoft), `LICENSE.md` in the folder |
| `OptiScaler/include/sl.param`, `external/streamline`, `external/streamline1` | NVIDIA Streamline headers (2.11.1 in `external/streamline`) | MIT (NVIDIA); notice in the files, and `external/streamline/license.txt`, the licence file of Streamline 2.11.1. The files that licence puts under other terms (`sl_nvperf`) are not here. |
| `external/AntiLag2-SDK` | AMD Anti-Lag 2 SDK | MIT (AMD) |
| `external/latencyflex` | LatencyFleX header | Apache-2.0 (Tatsuyuki Ishi), `LICENSE` in the folder |
| `external/nlohmann` | JSON for Modern C++ | MIT (Niels Lohmann) |
| `external/freetype` | FreeType 2.13.3 headers | FreeType Licence (FTL) or GPLv2, at the choice of the user; `LICENSE.TXT`, `docs/FTL.TXT` and `docs/GPLv2.TXT` in the folder |
| `OptiScaler/menu/font` | Hack typeface by Source Foundry, <https://github.com/source-foundry/Hack>, as a font file and as data in two headers | MIT for the work of the Hack project and Bitstream Vera Licence for Bitstream Vera Sans Mono, on which it is based; `LICENSE.md` in the folder |
| `OptiScaler/include/flag-set-cpp` | flag-set-cpp by Mart Somermaa, <https://github.com/mrts/flag-set-cpp> | MIT, `LICENCE` in the folder |
| `OptiScaler/include/sha1` | SHA-1 by Steve Reid and others | Public domain, as the file states |
| `OptiScaler/include/spdlog_sink` | Log sink by OptiScaler, adapted from spdlog | MIT (Gabi Melman); the `LICENSE` of spdlog is in the folder |
| `OptiScaler/include/d3dx/d3dx12.h` | D3D12 helper structures | MIT, notice in the file |
| `OptiScaler/inputs/NVNGX_Compat.h`, `NVNGX_Compat_Vk.h` | What OptiScaler needs on top of the official NVIDIA headers; written for this repository | GPL-3.0, as the rest |

## 3. Pre-built binaries stored in this repository (27 files)

All 27 were put there by OptiScaler. None is an upscaler DLL.

| Files | What they are | Needed to build | Licence and status |
|---|---|---|---|
| `OptiScaler/library/fsr2/*.lib` (8) | AMD FSR 2 static libraries | Yes | Built from AMD's MIT-licensed FSR 2 source. Identified. |
| `OptiScaler/library/fsr2_212/*.lib` (6) | AMD FSR 2.1.2 static libraries | Yes | As above. Identified. |
| `OptiScaler/library/fsr31/*.lib` (10) | AMD FidelityFX SDK 1.1 static libraries | Yes | Built from AMD's MIT-licensed SDK. Identified. |
| `OptiScaler/library/detours/detours.lib` | Microsoft Detours static library | Yes | MIT; the licence text is in `OptiScaler/include/detours/LICENSE.md`. Identified. |
| `OptiScaler/library/vulkan/vulkan-1.lib` | Import library of the Vulkan loader | Yes | Apache-2.0; the licence file of the Vulkan loader is next to it, `LICENSE.txt`. Identified. |
| `external/freetype/freetype.lib` | FreeType static library | Yes | FTL or GPLv2, texts in `external/freetype`. Identified; the FTL asks for a credit, which is given here and in the README of OptiScaler. |

## 4. Files of OptiScaler left out of this repository

These files are in OptiScaler at the base commit and are not in this repository or in its history, because their
terms do not clearly allow putting them in a public GPL-3.0 repository. Only the NVIDIA headers are needed to
build, and they are taken from NVIDIA.

| File | What it is | Where to get it, if wanted |
|---|---|---|
| `OptiScaler/shaders/shader_tools/fxc.exe` | Legacy HLSL compiler (Microsoft) | Windows SDK, `bin\<version>\x64\fxc.exe` |
| `OptiScaler/shaders/shader_tools/dxil.dll` | DXIL signing library (Microsoft) | Windows SDK, or a release of <https://github.com/microsoft/DirectXShaderCompiler> |
| `OptiScaler/shaders/shader_tools/dxc.exe`, `dxcompiler.dll`, `dxv.exe` | DirectX Shader Compiler 1.9.2602, Microsoft's binaries of an open source compiler | Windows SDK (`dxc.exe`, `dxcompiler.dll`), or a release of <https://github.com/microsoft/DirectXShaderCompiler> |
| `OptiScaler/library/d3dx/d3dx11.lib` | D3DX11 import library, legacy DirectX SDK (Microsoft) | Not used by the code; nothing to get |
| `OptiScaler/include/d3dx/D3DX11.h`, `D3DX11async.h`, `D3DX11core.h`, `D3DX11tex.h` | D3DX11 headers, legacy DirectX SDK (Microsoft) | Not used by the code; nothing to get |
| `external/directx_agility_sdk/lib/D3D12Core.dll` | DirectX 12 Agility SDK 1.615.1 runtime (Microsoft) | NuGet package `Microsoft.Direct3D.D3D12`; see below |
| `external/nvngx_dlss_sdk/nvsdk_ngx.h`, `nvsdk_ngx_defs.h`, `nvsdk_ngx_helpers_vk.h`, `nvsdk_ngx_params.h`, `nvsdk_ngx_vk.h` | NVIDIA NGX / DLSS SDK headers, changed by OptiScaler; marked `LicenseRef-NvidiaProprietary` | <https://github.com/NVIDIA/DLSS>, tag `v310.1.0`, folder `include`; see below |
| `external/nvngx_dlss_sdk/regs/EnableSignatureOverride.reg`, `DisableSignatureOverride.reg` | Registry files that switch the NGX signature override on and off; they came with the headers and carry no licence notice | The repository of OptiScaler; see below |

What changes because of it:

- The NVIDIA headers are no longer stored here. The build takes the official ones, unchanged, from
  `external/nvidia_dlss_sdk/include`, a folder git ignores, or from wherever `NvidiaDlssSdkInclude` points; whoever
  builds gets them from NVIDIA under the NVIDIA RTX SDKs License. Tag `v310.1.0` is the one the code was checked
  against: its definitions are the same as in the headers OptiScaler carried, and the three things OptiScaler had
  added to them are now in `OptiScaler/inputs/NVNGX_Compat.h` and `NVNGX_Compat_Vk.h` and in the definitions of six
  functions in `NVNGX_DLSS_Dx11.cpp` and `NVNGX_DLSS_Dx12.cpp`. Tags `v3.5.10` and `v310.9.1` were tried and do not build.
- The two registry files are not used by the code, by any script or by the build. They only matter to someone
  with an NVIDIA GPU who wants the driver to load an unsigned `nvngx` DLL, and set or clear the value
  `{41FCC608-8496-4DEF-B43E-7D9BD675A6FF}` under `HKLM\SOFTWARE\NVIDIA Corporation\Global` and
  `HKLM\SYSTEM\ControlSet001\Services\nvlddmkm`.
- `fxc.exe`, `dxc.exe`, `dxcompiler.dll` and `dxil.dll` are only used by the scripts in
  `OptiScaler/shaders/shader_tools` that regenerate the pre-compiled shader headers. The headers are in the
  repository, so the build does not run those scripts. To run them, copy the four files from the Windows SDK
  (`bin\<version>\x64`) into that folder. No script uses `dxv.exe`.
- `d3dx11.lib` was on the link line of the Debug configuration and has been taken off it; no code calls D3DX11.
- `D3D12Core.dll` is only loaded when `FsrAgilitySDKUpgrade` is on. Whoever wants it places the file where
  OptiScaler's own packages have it, `<game>\OptiScaler\D3D12_OptiScaler\D3D12Core.dll`, under the licence in
  `external/directx_agility_sdk/LICENSE.txt`. Without it OptiScaler uses the Direct3D 12 of the system.

The build does not copy any DLL of AMD, Intel or Microsoft next to `OptiScaler.dll` any more. OptiScaler's own
release packages do; to get the same folder locally, build with `/p:BundleVendorBinaries=true`, which copies them
from the submodules (and `D3D12Core.dll` from `external/directx_agility_sdk/lib` if it was put there). That folder
is then subject to the licences of those files and is not meant to be published from this repository.

## 5. Open points before making this repository public

1. NVIDIA DLSS SDK: its headers are not distributed here any more, but OptiScaler is still compiled against them.
   The NVIDIA RTX SDKs License says the SDK may not be used in a way that makes it subject to an open source
   licence. Whether building a GPL-3.0 program against headers each user obtains from NVIDIA is within those terms
   is a legal question this repository does not settle. OptiScaler is in the same position.

## 6. Not included

No FSR, DLSS or XeSS upscaler DLL is stored in this repository or in its history. Upscaler DLLs are provided by the
user.

HWiNFO and MSI Afterburner are not part of this repository; ILION only reads the sensor data those programs publish
while they are running.

## 7. The distribution package

This file describes the repository. The distribution package of ILION V1.1 is put together apart from it and adds
files that are not stored here; its own `THIRD_PARTY_NOTICES.md` lists them one by one with their licences:

- The signed AMD FidelityFX libraries (AMD's official FSR 4.1.1 among them) and the signed Intel XeSS libraries,
  taken from the submodules of section 1, with their licences.
- `OptiPatcher.asi` (OptiScaler project, MIT) and `dlss-enabler-headless.dll` (DLSS Enabler, Artur Graniszewski,
  MIT), with their licences. These two files come from a third-party build of OptiScaler and were not checked
  against a publication of their authors.
- No DLSS or NVIDIA DLL; the Streamline files are downloaded from NVIDIA by OptiScaler's fetcher script when the
  user runs it. No FSR 4.1.1B, FSR 4 Lite, Nukem9's dlssg-to-fsr3, XeFGUnlock or `D3D12Core.dll`.
