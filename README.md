# OptiScaler - ILION V1.1

**[Español](#español) · [English](#english)**

ILION is a modified version of [OptiScaler](https://github.com/optiscaler/OptiScaler). It is not an official
OptiScaler release and is not affiliated with, or endorsed by, the OptiScaler project, AMD, Intel or NVIDIA.

- Original project: <https://github.com/optiscaler/OptiScaler>, at commit `f6f75ddf0118ac45f447e2babbe18dbda3451edd`
- Licence: [GNU General Public License v3.0](LICENSE), the licence of OptiScaler
- Original documentation: [README_OptiScaler.md](README_OptiScaler.md)
- Credits: [CREDITS.md](CREDITS.md) · Third-party components: [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) ·
  Changes: [CHANGELOG.md](CHANGELOG.md)

---

## Español

### Qué es

ILION es una modificación personal de OptiScaler centrada en comparar implementaciones de FSR 4 sin reiniciar el
juego. Todo lo que hace OptiScaler sigue estando; ILION añade herramientas alrededor del escalado.

Se ha desarrollado y probado en un único equipo (AMD Radeon RX 7900 XTX, Ryzen 7 7800X3D, Windows 11) y sobre todo en
un juego (The Witcher 3, DirectX 12). En otros equipos, juegos o API puede comportarse de otra manera.

### Qué añade

- **Cambio en caliente de la DLL de FSR.** Selecciona entre varias `amd_fidelityfx_upscaler_dx12.dll` desde el menú,
  sin reiniciar. Solo afecta a Super Resolution; no toca Frame Generation.
- **Gestor de versiones.** Cada subcarpeta de `<juego>\ILION\FSR_Versions\` con su propia DLL aparece en la lista con
  el nombre de la carpeta. Validación del archivo, versión y SHA-256.
- **Diagnóstico y verificación.** Qué DLL ejecuta realmente el escalado, con un marcador visual opcional.
- **Benchmark.** Pruebas manuales de 30, 60 o 120 s con FPS promedio, 1 % y 0,1 % low, percentiles de frametime,
  variabilidad y stutters; guardado en JSON y CSV.
- **Comparación de pruebas e informe.** Diferencias absolutas y porcentuales, márgenes y conclusiones de rendimiento
  y estabilidad. No hay puntuaciones globales.
- **Capturas y análisis visual.** Captura PNG sin el menú (DX12), comparador lado a lado con zoom sincronizado,
  lupa de píxeles sin suavizado sobre la misma zona de ambas capturas, y métricas de imagen fija (SSIM, diferencias
  de píxeles, bordes) con una interpretación breve y prudente: explica qué significan los valores y sus límites, y
  no declara ganador cuando las capturas no lo permiten. No mide lo que ocurre en movimiento.
- **Telemetría de hardware** en el overlay, leída de HWiNFO o MSI Afterburner. Lo que ningún programa proporciona se
  muestra como no disponible; nada se estima.
- **Interfaz.** Español e inglés, modo Básico y Completo, tema industrial con el tema clásico de OptiScaler
  disponible, overlay vertical y horizontal. Arranca en español y en modo Completo; el idioma y el modo elegidos se
  guardan al cambiarlos.

### Qué no incluye

Este repositorio contiene solo código fuente. **No incluye ninguna DLL de FSR, DLSS o XeSS**, ni las cabeceras del
SDK de DLSS de NVIDIA, ni las DLL de AMD, Intel o Microsoft que acompañan a los paquetes de OptiScaler. Cada usuario
las obtiene de su proveedor, de fuentes que tenga derecho a usar. Más abajo se explica cómo.

ILION V1.1 no incluye las DLL de FSR 4.1.1 oficial de AMD, FSR 4.1.1B ni FSR 4 Lite. Cada usuario deberá obtenerlas
por separado e instalarlas en las carpetas correspondientes, respetando las licencias y condiciones de sus autores.

### Compilar

1. Clona el repositorio **con todos sus submódulos**:
   `git clone --recurse-submodules https://github.com/ENE4S/Optiscaler-ILION.git`
2. Consigue las cabeceras del SDK de DLSS de NVIDIA, que **no forman parte de este repositorio**: copia la carpeta
   `include` de <https://github.com/NVIDIA/DLSS>, en la etiqueta **`v310.1.0`**, a `external\nvidia_dlss_sdk\include`
   (esa carpeta está ignorada por git). Su uso está sujeto a la licencia de ese SDK. Si las tienes en otro sitio,
   indícalo con la variable de entorno o la propiedad de MSBuild `NvidiaDlssSdkInclude`.
3. Abre `OptiScaler.sln` con **Visual Studio 2022** (carga de trabajo "Desarrollo para el escritorio con C++", con
   las herramientas de compilación **v143** y un SDK de Windows 10/11). Una versión posterior de Visual Studio también
   sirve si tiene instaladas las herramientas v143.
4. Compila la configuración **Release**, plataforma **x64**.
5. El resultado queda en `x64\Release\a\OptiScaler.dll`.

Hace falta tener `git` en el PATH: un paso previo a la compilación lo usa para anotar el commit. Por eso la carpeta tiene
que ser un clon de git: con el ZIP de código fuente que ofrece GitHub, la configuración Release no compila.

Las compilaciones hechas desde este repositorio no están firmadas.

La carpeta `x64\Release\a\` contiene solo `OptiScaler.dll`, `OptiScaler.ini` y los scripts de instalación. A
diferencia de los paquetes de OptiScaler, no se copian en ella las DLL de AMD, Intel o Microsoft: cada usuario las
obtiene de su proveedor. Dependencias externas y sus licencias:

| Qué | Para qué | De dónde | Licencia |
|---|---|---|---|
| Submódulos de `external/` | Compilar | Se descargan al clonar | Las de cada uno, ver [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) |
| Cabeceras del SDK de DLSS, `v310.1.0` | Compilar | <https://github.com/NVIDIA/DLSS>, carpeta `include` | NVIDIA RTX SDKs License (propietaria) |
| DLL de FSR (`amd_fidelityfx_*.dll`) | Ejecutar FSR 3.1 / FSR 4 | AMD FidelityFX SDK o el controlador de AMD | Licencia de AMD, solo en forma binaria |
| DLL de XeSS (`libxess*.dll`, `libxell.dll`) | Ejecutar XeSS | <https://github.com/intel/xess> | Intel Simplified Software License |
| `D3D12Core.dll` (opcional) | Opción `FsrAgilitySDKUpgrade` | Paquete NuGet `Microsoft.Direct3D.D3D12` | Licencia del Agility SDK de Microsoft |
| `fxc.exe`, `dxc.exe`, `dxcompiler.dll`, `dxil.dll` (opcional) | Regenerar los sombreadores precompilados | Windows SDK | Licencia del Windows SDK |

Para obtener en local la misma carpeta que arma OptiScaler, compila con `/p:BundleVendorBinaries=true`. Esa carpeta
queda sujeta a las licencias de esos archivos y no está pensada para publicarse desde este repositorio.

#### Instalar las dependencias a mano

**Para compilar: cabeceras de NVIDIA.** Desde la raíz del repositorio, esto descarga solo la carpeta `include` de la
versión que hace falta (menos de 1 MB) y la deja donde la compilación la busca:

```
git clone --depth 1 --branch v310.1.0 --filter=blob:none --sparse https://github.com/NVIDIA/DLSS.git external\nvidia_dlss_sdk
git -C external\nvidia_dlss_sdk sparse-checkout set include
```

También sirve copiar a mano los archivos de `include` a `external\nvidia_dlss_sdk\include`. Tiene que ser la etiqueta
`v310.1.0`: con `v3.5.10` y `v310.9.1` no compila. Si faltan, la compilación se detiene con un mensaje que lo dice.
Al descargarlas aceptas la licencia de NVIDIA (`LICENSE.txt` de ese repositorio).

**Para usar ILION en un juego.** `OptiScaler.dll` no trae ninguna DLL de terceros. Si ya tienes OptiScaler instalado
en el juego, basta con sustituir su DLL por la de ILION: el resto de archivos ya está. En una instalación nueva:

- FSR 3.1 / FSR 4: `amd_fidelityfx_loader_dx12.dll`, `amd_fidelityfx_upscaler_dx12.dll` y
  `amd_fidelityfx_framegeneration_dx12.dll` (y `amd_fidelityfx_vk.dll` para Vulkan), del FidelityFX SDK de AMD, en
  `<juego>\OptiScaler\`, que es donde los colocan los paquetes de OptiScaler. La ruta se puede cambiar en
  `OptiScaler.ini`, sección `[Libraries]`.
- XeSS: `libxess.dll` y las demás de <https://github.com/intel/xess> (`bin`), en la misma carpeta.
- Opcional, `D3D12Core.dll` del paquete NuGet `Microsoft.Direct3D.D3D12` (un `.nupkg` es un zip; está en
  `build\native\bin\x64`), en `<juego>\OptiScaler\D3D12_OptiScaler\`. Solo se usa con `FsrAgilitySDKUpgrade`.
- Las versiones de FSR que quieras comparar, cada una en su carpeta de `<juego>\ILION\FSR_Versions\`.

**Para regenerar los sombreadores precompilados** (no hace falta para compilar): copia `fxc.exe`, `dxc.exe`,
`dxcompiler.dll` y `dxil.dll` del Windows SDK (`bin\<versión>\x64`) a `OptiScaler\shaders\shader_tools`.

### Instalar

Igual que OptiScaler: consulta [README_OptiScaler.md](README_OptiScaler.md). ILION sustituye a `OptiScaler.dll` (o al
nombre que se le haya dado: `dxgi.dll`, `winmm.dll`...). Las versiones de FSR adicionales van en
`<juego>\ILION\FSR_Versions\<nombre>\amd_fidelityfx_upscaler_dx12.dll`.

### Estado y límites

- Proyecto personal, sin garantía y sin soporte.
- El cambio en caliente de DLL y la captura de imagen funcionan solo en DirectX 12.
- Usar OptiScaler o ILION en juegos en línea con sistemas antitrampas puede tener consecuencias. No lo hagas.
- Los problemas de ILION no deben comunicarse al proyecto OptiScaler.

### Estado de las licencias

Este repositorio no debe darse por autorizado legalmente para su publicación. Queda una cuestión sin aclarar:

- **SDK de DLSS de NVIDIA y GPL-3.0.** OptiScaler, y por tanto ILION, se compila contra las cabeceras del SDK de
  DLSS. Esas cabeceras ya no se distribuyen aquí, pero la licencia del SDK (NVIDIA RTX SDKs License) dice que no puede
  usarse de forma que quede sujeto a una licencia de código abierto. Si compilar un programa GPL-3.0 contra
  cabeceras que cada usuario obtiene de NVIDIA cabe en esos términos es una cuestión jurídica que este repositorio
  no resuelve; solo pueden aclararla NVIDIA o un abogado. OptiScaler está en la misma situación.

Está descrita también en [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md), sección 5.

### Licencia y créditos

OptiScaler es obra de sus autores y se distribuye bajo GPL-3.0. ILION, como obra derivada, se distribuye bajo la misma
licencia. Las modificaciones de ILION son de ENE4S, hechas en octubre de 2026 con la asistencia de Claude (Anthropic),
y están en los commits posteriores al commit de importación. Ver [CREDITS.md](CREDITS.md).

---

## English

### What it is

ILION is a personal modification of OptiScaler built around comparing FSR 4 implementations without restarting the
game. Everything OptiScaler does is still there; ILION adds tools around upscaling.

It was developed and tested on a single machine (AMD Radeon RX 7900 XTX, Ryzen 7 7800X3D, Windows 11) and mostly in
one game (The Witcher 3, DirectX 12). It may behave differently on other machines, games or APIs.

### What it adds

- **Hot swap of the FSR DLL.** Pick between several `amd_fidelityfx_upscaler_dx12.dll` files from the menu, without
  restarting. It only affects Super Resolution; Frame Generation is not touched.
- **Versions manager.** Every subfolder of `<game>\ILION\FSR_Versions\` that has its own DLL is listed under the name
  of the folder. File validation, version and SHA-256.
- **Diagnostics and verification.** Which DLL really executes the upscaling, with an optional visual marker.
- **Benchmark.** Manual tests of 30, 60 or 120 s with average FPS, 1 % and 0.1 % low, frame time percentiles,
  variability and stutters; saved as JSON and CSV.
- **Test comparison and report.** Absolute and percentage differences, margins, and conclusions on performance and
  stability. There are no overall scores.
- **Captures and visual analysis.** PNG capture without the menu (DX12), side-by-side viewer with synchronised zoom,
  an unsmoothed pixel magnifier over the same area of both captures, and still-image metrics (SSIM, pixel
  differences, edges) with a short, cautious interpretation: it says what the values mean and their limits, and
  names no winner when the captures don't allow it. It does not measure what happens in motion.
- **Hardware telemetry** in the overlay, read from HWiNFO or MSI Afterburner. What no program provides is shown as
  not available; nothing is estimated.
- **Interface.** Spanish and English, Basic and Full modes, an industrial theme with the classic OptiScaler theme
  still available, vertical and horizontal overlay. It starts in Spanish and in Full mode; the language and the
  mode that are picked are saved when they are changed.

### What it does not include

This repository holds source code only. **It does not include any FSR, DLSS or XeSS DLL**, nor the headers of the
NVIDIA DLSS SDK, nor the AMD, Intel or Microsoft DLLs that come with OptiScaler's packages. Each user gets them from
their vendor, from sources they are entitled to use. How is explained below.

ILION V1.1 does not include the DLLs for AMD's official FSR 4.1.1, FSR 4.1.1B, or FSR 4 Lite. Users must obtain them
separately and place them in the corresponding folders, respecting their respective licenses and terms.

### Building

1. Clone the repository **with all of its submodules**:
   `git clone --recurse-submodules https://github.com/ENE4S/Optiscaler-ILION.git`
2. Get the headers of the NVIDIA DLSS SDK, which are **not part of this repository**: copy the `include` folder of
   <https://github.com/NVIDIA/DLSS>, at tag **`v310.1.0`**, to `external\nvidia_dlss_sdk\include` (git ignores that
   folder). Their use is subject to the licence of that SDK. If you keep them elsewhere, say where with the
   environment variable or MSBuild property `NvidiaDlssSdkInclude`.
3. Open `OptiScaler.sln` with **Visual Studio 2022** ("Desktop development with C++" workload, with the **v143** build
   tools and a Windows 10/11 SDK). A later Visual Studio works too if it has the v143 build tools installed.
4. Build the **Release** configuration, **x64** platform.
5. The result is `x64\Release\a\OptiScaler.dll`.

`git` has to be in the PATH: a pre-build step uses it to record the commit. That is why the folder has to be a git
clone: with the source ZIP GitHub offers, the Release configuration does not build.

Builds made from this repository are not signed.

The `x64\Release\a\` folder holds only `OptiScaler.dll`, `OptiScaler.ini` and the setup scripts. Unlike OptiScaler's
packages, the DLLs of AMD, Intel or Microsoft are not copied into it: each user gets them from their vendor.
External dependencies and their licences:

| What | What for | Where from | Licence |
|---|---|---|---|
| Submodules in `external/` | Building | Downloaded when cloning | Each its own, see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) |
| DLSS SDK headers, `v310.1.0` | Building | <https://github.com/NVIDIA/DLSS>, `include` folder | NVIDIA RTX SDKs License (proprietary) |
| FSR DLLs (`amd_fidelityfx_*.dll`) | Running FSR 3.1 / FSR 4 | AMD FidelityFX SDK or the AMD driver | AMD licence, binary form only |
| XeSS DLLs (`libxess*.dll`, `libxell.dll`) | Running XeSS | <https://github.com/intel/xess> | Intel Simplified Software License |
| `D3D12Core.dll` (optional) | The `FsrAgilitySDKUpgrade` option | NuGet package `Microsoft.Direct3D.D3D12` | Microsoft Agility SDK licence |
| `fxc.exe`, `dxc.exe`, `dxcompiler.dll`, `dxil.dll` (optional) | Regenerating the pre-compiled shaders | Windows SDK | Windows SDK licence |

To get locally the same folder OptiScaler puts together, build with `/p:BundleVendorBinaries=true`. That folder is
subject to the licences of those files and is not meant to be published from this repository.

#### Installing the dependencies by hand

**To build: NVIDIA headers.** From the root of the repository, this downloads only the `include` folder of the
version that is needed (under 1 MB) and leaves it where the build looks for it:

```
git clone --depth 1 --branch v310.1.0 --filter=blob:none --sparse https://github.com/NVIDIA/DLSS.git external\nvidia_dlss_sdk
git -C external\nvidia_dlss_sdk sparse-checkout set include
```

Copying the files of `include` by hand to `external\nvidia_dlss_sdk\include` works too. It has to be tag `v310.1.0`:
`v3.5.10` and `v310.9.1` do not build. If they are missing, the build stops with a message saying so. By downloading
them you accept NVIDIA's licence (`LICENSE.txt` in that repository).

**To use ILION in a game.** `OptiScaler.dll` comes with no third-party DLL. If OptiScaler is already installed in the
game, replacing its DLL with ILION's is enough: the other files are already there. For a new installation:

- FSR 3.1 / FSR 4: `amd_fidelityfx_loader_dx12.dll`, `amd_fidelityfx_upscaler_dx12.dll` and
  `amd_fidelityfx_framegeneration_dx12.dll` (and `amd_fidelityfx_vk.dll` for Vulkan), from AMD's FidelityFX SDK, in
  `<game>\OptiScaler\`, which is where OptiScaler's packages put them. The path can be changed in `OptiScaler.ini`,
  section `[Libraries]`.
- XeSS: `libxess.dll` and the others from <https://github.com/intel/xess> (`bin`), in the same folder.
- Optional, `D3D12Core.dll` from the NuGet package `Microsoft.Direct3D.D3D12` (a `.nupkg` is a zip; it is in
  `build\native\bin\x64`), in `<game>\OptiScaler\D3D12_OptiScaler\`. Only used with `FsrAgilitySDKUpgrade`.
- The FSR versions you want to compare, each in its own folder of `<game>\ILION\FSR_Versions\`.

**To regenerate the pre-compiled shaders** (not needed to build): copy `fxc.exe`, `dxc.exe`, `dxcompiler.dll`
and `dxil.dll` from the Windows SDK (`bin\<version>\x64`) to `OptiScaler\shaders\shader_tools`.

### Installing

The same as OptiScaler: see [README_OptiScaler.md](README_OptiScaler.md). ILION takes the place of `OptiScaler.dll`
(or whatever it was renamed to: `dxgi.dll`, `winmm.dll`...). Additional FSR versions go in
`<game>\ILION\FSR_Versions\<name>\amd_fidelityfx_upscaler_dx12.dll`.

### Status and limits

- A personal project, with no warranty and no support.
- The DLL hot swap and the image capture work in DirectX 12 only.
- Using OptiScaler or ILION in online games with anti-cheat systems can have consequences. Don't.
- Problems with ILION should not be reported to the OptiScaler project.

### Licence status

This repository should not be taken as legally cleared for publication. One question is not settled:

- **NVIDIA DLSS SDK and GPL-3.0.** OptiScaler, and so ILION, is compiled against the headers of the DLSS SDK. Those
  headers are no longer distributed here, but the licence of the SDK (NVIDIA RTX SDKs License) says it may not be
  used in a way that makes it subject to an open source licence. Whether building a GPL-3.0 program against headers
  each user obtains from NVIDIA is within those terms is a legal question this repository does not settle; only
  NVIDIA or a lawyer can. OptiScaler is in the same position.

It is also described in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md), section 5.

### Licence and credits

OptiScaler is the work of its authors and is distributed under GPL-3.0. ILION, as a derivative work, is distributed
under the same licence. The ILION modifications are by ENE4S, made in October 2026 with the assistance of Claude
(Anthropic), and are the commits after the import commit. See [CREDITS.md](CREDITS.md).
