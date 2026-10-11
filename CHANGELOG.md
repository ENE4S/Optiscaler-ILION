# ILION changelog

Changes ILION makes to OptiScaler. The changelog of OptiScaler itself is
[Changelog_OptiScaler.md](Changelog_OptiScaler.md).

Base: OptiScaler at commit `f6f75ddf0118ac45f447e2babbe18dbda3451edd` (9 October 2026).

## V1.1 — October 2026

### Upscaling

- Hot swap between FSR upscaler DLLs (`amd_fidelityfx_upscaler_dx12.dll`) from the menu, without restarting the game.
  Super Resolution only; Frame Generation is not touched.
- Versions manager: every subfolder of `<game>\ILION\FSR_Versions\` with its own DLL is listed under the name of the
  folder. The file is validated (x64, required exports) and its version and SHA-256 are shown.
- Diagnostics of the DLL that really executes the upscaling, and an optional visual marker to verify it.
- FSR 4 comparison mode.
- FSR version selector inside the Benchmark tab, sharing the list and the swap with the Upscaling tab.

### Benchmark

- Benchmark tab: manual tests of 30, 60 or 120 seconds of what is in use. Average and median FPS, 1 % and 0.1 % low,
  frame time mean, P95 and P99, variability and stutter count. Results saved as JSON, frame times as CSV, and a line
  in `benchmarks.csv`.
- Comparison of saved tests: differences in percent and in the unit of each value, margins taken from repeated runs,
  and conclusions on performance, stability and their balance. Tests made under different conditions are not ranked.
  Differences are coloured by what is good for each metric.
- Integrated final report per configuration, for any number of DLLs.
- Clearing the last result and deleting saved tests, with confirmation.

### Visual analysis

- Capture of the game's image without the menu as a lossless PNG with a metadata file (DX12).
- Visual comparator: two captures side by side with synchronised zoom and position, or their amplified difference.
- Still-image measurements against a reference picked by the user: SSIM, pixel differences, edge strength, edges
  kept, overshoot next to edges (exploratory), displacement and scene checks.
- A capture is linked to a benchmark only when taken while that saved test is on screen. Only linked captures enter
  a benchmark comparison; without them nothing about images is shown.

### Telemetry

- GPU and CPU usage and temperatures, GPU hotspot and VRAM in the overlay, read from HWiNFO or MSI Afterburner. A
  reading no program provides is shown as not available.

### Interface

- Spanish and English.
- Basic and Full modes over the same settings.
- Industrial theme: chassis, module plates, keys, pilot lamps, sliders with a value display, selector drop-downs. The
  classic OptiScaler theme stays available.
- Vertical overlay in blocks of aligned rows, and a matching horizontal one.
- Frame generation state in the sidebar, in Help / Status and in the Basic panel also reflects the DLSSG of the game
  when it is served by a replacement such as DLSS Enabler.
- Hardware monitoring settings fold like the other sections.

### Build and repository

- Built against the official NVIDIA DLSS SDK headers (tag `v310.1.0`), which each user gets from NVIDIA; the copy
  OptiScaler carried is not stored here. What OptiScaler had added to it is in `OptiScaler/inputs/NVNGX_Compat.h` and
  `NVNGX_Compat_Vk.h`. The Release x64 DLL is the same as with the old headers, byte for byte, except for the build
  time and commit it records.
- Eighteen files of OptiScaler from NVIDIA and Microsoft are left out; see
  [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md), section 4.
- The build no longer copies AMD, Intel or Microsoft DLLs next to `OptiScaler.dll`, unless asked to with
  `/p:BundleVendorBinaries=true`.
- OptiScaler's GitHub Actions workflows are kept in `.github/workflows-disabled` and do not run.
- The licence texts of Detours, flag-set-cpp, FreeType, Hack, LatencyFleX, the Vulkan loader, spdlog and Streamline
  are stored next to those components.

### Known limits

- Developed and tested on one machine and mostly one game; see the README.
- DLL hot swap and capture are DirectX 12 only.
- Visual measurements are of still images; motion artefacts are not measured.
- DLSS and DLSS Frame Generation on NVIDIA hardware, DirectX 11 and Vulkan were not tested with ILION.
