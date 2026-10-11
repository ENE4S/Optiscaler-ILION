# Credits

## OptiScaler

ILION is built on **OptiScaler** and would not exist without it.

- Repository: <https://github.com/optiscaler/OptiScaler>
- Base commit: `f6f75ddf0118ac45f447e2babbe18dbda3451edd` ("Prevent NvngxFFX errors from causing a memory leak",
  9 October 2026), branch `master`
- Licence: GNU General Public License v3.0 ([LICENSE](LICENSE))

The first commit of this repository is the source tree of that commit without eighteen files of NVIDIA and Microsoft
that are not redistributed here ([THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md), section 4); everything else in it
is unchanged. OptiScaler is the work of its authors, not of the author of ILION. Its history up to that commit (4,561 commits) is not carried here; it is in the
original repository, which is the authoritative record of who wrote what.

Authors of OptiScaler as they appear in that history, with their number of commits:

| Author | Commits |
|---|---|
| cdozdil | 3216 |
| FakeMichau | 887 |
| TheRazerMD | 241 |
| Michał Lewandowski | 78 |
| cdozdila | 36 |
| nitec | 36 |
| Maple | 16 |
| Cetin Ozdil | 9 |
| PotatoOfDoom | 9 |
| Zach Hembree | 6 |
| Astyyyyy | 5 |
| cetin.ozdil | 3 |
| Adrian Thiele | 2 |
| Asty | 2 |
| Florian RICHER | 2 |
| Kurt Himebauch | 2 |
| realdody | 2 |
| wilsjo2 | 2 |
| Abyss | 1 |
| YiBoF | 1 |
| gnusenpai | 1 |
| imapotatook | 1 |
| kebas | 1 |
| levzzz | 1 |
| xHybred | 1 |

Names are listed as git records them; several may belong to the same person.

The acknowledgements of the OptiScaler authors themselves (CyberFSR2, DLSS Enabler, the DLSS2FSR community and
others) are in [README_OptiScaler.md](README_OptiScaler.md), under "Thanks", and are kept as they wrote them.

## ILION

- Modifications: **ENE4S**, October 2026.
- The code of the modifications was written with the assistance of Claude (Anthropic); the commits carry a
  `Co-Authored-By` line for it.

What ILION changes is every commit after the import commit. In short:

Files of OptiScaler that were modified:

- `OptiScaler.ini`
- `OptiScaler/Config.cpp`, `OptiScaler/Config.h`
- `OptiScaler/fsr4/FSR4ModelSelection.cpp`, `OptiScaler/fsr4/FSR4ModelSelection.h`, `OptiScaler/fsr4/FSR4Upgrade.cpp`
- `OptiScaler/menu/menu_common.cpp`, `OptiScaler/menu/menu_common.h`, `OptiScaler/menu/menu_overlay_dx.cpp`
- `OptiScaler/proxies/FfxApi_Proxy.h`
- `OptiScaler/upscalers/ffx/FFXFeature_Dx12.cpp`
- To build against the official NVIDIA headers and to stop bundling third-party DLLs, with no change in behaviour:
  `OptiScaler/OptiScaler.vcxproj`, `OptiScaler/inputs/NVNGX_DLSS_Dx11.cpp`, `NVNGX_DLSS_Dx12.cpp`, `FfxApi_Vk.cpp`,
  `FSR2_Vk.cpp`, `XeSS_Vulkan.cpp`, `.gitignore`
- `.github/workflows` was moved to `.github/workflows-disabled`, unchanged
- `README.md` and `Changelog.md` were renamed to `README_OptiScaler.md` and `Changelog_OptiScaler.md`, unchanged

Files added by ILION:

- `OptiScaler/fsr4/FSR4Versions.h`, `FSR4VisualVerify.h`, `FSR4Comparison.h`
- `OptiScaler/misc/HwTelemetry.h`, `IlionBenchmark.h`, `IlionCompare.h`, `IlionCapture.h`, `IlionVisual.h`
- `OptiScaler/menu/ilion_lang.h`, `ilion_theme.h`, `ilion_visual_panel.h`
- `OptiScaler/inputs/NVNGX_Compat.h`, `NVNGX_Compat_Vk.h`
- `.github/workflows-disabled/README.md`
- `OptiScaler/include/detours/LICENSE.md` and `OptiScaler/include/flag-set-cpp/LICENCE`, the MIT licence texts of
  those two components, taken from their repositories
- `README.md`, `CREDITS.md`, `THIRD_PARTY_NOTICES.md`, `CHANGELOG.md`

The ILION commits were made against OptiScaler's own copy of the NVIDIA headers, which is not in this repository.
The commits before "build against the official NVIDIA DLSS SDK headers" therefore do not build as they are; from
that commit on, the build uses the official headers described in the README.

## Third parties

Libraries, SDKs and tools OptiScaler uses, and ILION through it, are listed in
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

ILION reads hardware sensors from the shared memory of HWiNFO and MSI Afterburner when those programs are running.
It contains no code of either; both are the property of their authors.
