#pragma once

// This file is saved as UTF-8 with BOM and its literals have to stay UTF-8 in the binary, ImGui expects that.
// Popped at the end of the file so the files that include this one keep their own setting.
#pragma execution_character_set(push, "utf-8")

#include <atomic>
#include <string>
#include <string_view>
#include <unordered_map>

// Menu language of ILION.
//
// The English text is the key. Tr("Close") gives the Spanish text when Spanish is selected and a translation
// exists, otherwise the English text comes back unchanged. So a text without a translation just stays English,
// and adding a language entry never needs a code change besides the table below.
//
// TrL is for widget labels: the translated label keeps the English text as its ImGui id ("Cerrar###Close"),
// so widgets don't lose their state when the language changes.
//
// Names of technologies, dlls and identifiers aren't translated.
namespace Ilion
{
enum class Lang : int
{
    ES = 0,
    EN = 1,
};

inline std::atomic<int> g_lang { (int) Lang::ES };

inline Lang CurrentLang() { return (Lang) g_lang.load(std::memory_order_relaxed); }
inline void SetLang(Lang lang) { g_lang.store((int) lang, std::memory_order_relaxed); }

// Spanish unless the config says English
inline Lang ParseLang(const std::string& code) { return _stricmp(code.c_str(), "en") == 0 ? Lang::EN : Lang::ES; }
inline const char* LangCode(Lang lang) { return lang == Lang::EN ? "en" : "es"; }

struct Translation
{
    const char* en;
    const char* es;
};

// Format specifiers (%s, %llu, %.0f, %%) have to stay the same and in the same order as in the English text
inline constexpr Translation kSpanish[] = {
    // Tabs and sidebar
    { "Custom", "Personal" },
    { "Upscaling", "Escalado" },
    { "Latency & FPS", "Latencia y FPS" },
    { "Textures", "Texturas" },
    { "Compatibility", "Compatibilidad" },
    { "Menu", "Menú" },
    { "Help / Status", "Ayuda / Estado" },
    { "MAIN", "PRINCIPAL" },
    { "SYSTEM", "SISTEMA" },
    { "ADVANCED", "AVANZADO" },
    { "None", "Ninguno" },
    { "Idle", "En espera" },
    { "Off", "Apagado" },
    { "Yes", "Sí" },

    // Section and card titles
    { "Upscalers", "Escaladores" },
    { "Low Latency", "Baja latencia" },
    { "Sharpness", "Nitidez" },
    { "Upscale Ratio Override", "Forzar relación de escalado" },
    { "Output Scaling", "Escalado de salida" },
    { "Init Flags", "Flags de inicialización" },
    { "Active Quirks", "Quirks activos" },
    { "Logging", "Registro (log)" },
    { "Accent Colour", "Color de acento" },
    { "Background Colour", "Color de fondo" },
    { "FPS Overlay", "Overlay de FPS" },
    { "Upscaler Inputs", "Entradas del escalador" },
    { "V-Sync Settings", "Ajustes de V-Sync" },
    { "Keybinds", "Atajos de teclado" },
    { "Custom Tab", "Pestaña personal" },
    { "Per shader GPU times", "Tiempos de GPU por shader" },
    { "FSR FG Settings", "Ajustes de FSR FG" },
    { "XeFG Settings", "Ajustes de XeFG" },
    { "DLSSG Settings", "Ajustes de DLSSG" },
    { "Reprojection Settings", "Ajustes de reproyección" },
    { "OptiFG Settings", "Ajustes de OptiFG" },
    { "Plugins / Startup", "Plugins / Inicio" },
    { "Shaders / Limits", "Shaders / Límites" },
    { "Resource Barriers", "Barreras de recursos" },
    { "Anisotropic Filtering", "Filtrado anisotrópico" },
    { "Detected", "Detectado" },
    { "System", "Sistema" },
    { "Hooks / Config", "Hooks / Configuración" },

    // Bottom bar
    { "Open Wiki", "Abrir Wiki" },
    { "Save Settings", "Guardar ajustes" },
    { "Close", "Cerrar" },
    { "Click to open the OptiScaler Wiki page\nin your default browser\n\n"
      "Compatibility list with known game issues\nand workarounds, FG options explained\n"
      "and other useful info",
      "Abre la Wiki de OptiScaler\nen tu navegador predeterminado\n\n"
      "Lista de compatibilidad con problemas conocidos\ny soluciones, explicación de las opciones de FG\n"
      "y otra información útil" },
    { "\n\nAfter changing this option, please Save Settings.\nIt will be applied on next launch.",
      "\n\nDespués de cambiar esta opción, pulsa Guardar ajustes.\nSe aplicará en el próximo inicio." },

    { "Loaded DLL path", "Ruta de DLL cargada" },

    // FSR versions selector
    { "FSR Upscaler DLL", "DLL del escalador FSR" },
    { "Default", "Predeterminada" },
    { "Custom path", "Ruta personalizada" },
    { "Rescan##SrVariants", "Actualizar lista" },
    { "calculating...", "calculando..." },
    { "Can't be used: %s", "No se puede usar: %s" },
    { "file can't be opened", "no se puede abrir el archivo" },
    { "not a dll", "no es una dll" },
    { "not a 64 bit dll", "no es una dll de 64 bits" },
    { "damaged dll", "dll dañada" },
    { "dll has no exports", "la dll no tiene exportaciones" },
    { "not an FSR upscaler dll (ffx exports missing)",
      "no es una dll de escalado FSR (faltan las exportaciones ffx)" },
    { "the dll has to be named amd_fidelityfx_upscaler_dx12.dll",
      "la dll tiene que llamarse amd_fidelityfx_upscaler_dx12.dll" },
    { "Switches amd_fidelityfx_upscaler_dx12.dll without restarting the game.\n"
      "Every subfolder of ILION\\FSR_Versions (next to the game exe) that has its own\n"
      "amd_fidelityfx_upscaler_dx12.dll is listed here, the folder name is the version name.\n"
      "Add a folder and press Rescan, no restart needed.\n\n"
      "Dlls stay loaded once used. Only affects upscaling, not frame generation.",
      "Cambia amd_fidelityfx_upscaler_dx12.dll sin reiniciar el juego.\n"
      "Aquí aparece cada subcarpeta de ILION\\FSR_Versions (junto al exe del juego) que tenga su propia\n"
      "amd_fidelityfx_upscaler_dx12.dll; el nombre de la carpeta es el nombre de la versión.\n"
      "Añade una carpeta y pulsa Actualizar lista, no hace falta reiniciar.\n\n"
      "Las dll siguen cargadas una vez usadas. Solo afecta al escalado, no a frame generation." },

    // Notifications of the versions switch
    { "Can't find the dll of the selected FSR variant", "No se encuentra la dll de la versión FSR seleccionada" },
    { "Can't load the dll of the selected FSR variant", "No se puede cargar la dll de la versión FSR seleccionada" },
    { "Selected FSR variant is not a valid upscaler dll",
      "La versión FSR seleccionada no es una dll de escalado válida" },
    { "Selected FSR version isn't a compatible upscaler dll",
      "La versión FSR seleccionada no es una dll de escalado compatible" },
    { "Selected FSR version failed to start, the previous one was restored.\n"
      "Select FSR again to keep using it.",
      "La versión FSR seleccionada no pudo iniciarse y se restauró la anterior.\n"
      "Vuelve a seleccionar FSR para seguir usándola." },

    // Hot-swap diagnostics
    { "FSR 4 HOT-SWAP DIAGNOSTICS", "DIAGNÓSTICO DE HOT-SWAP FSR 4" },
    { "Selected: %s", "Seleccionada: %s" },
    { "Active: %s", "Activa: %s" },
    { "DLL Path: %s", "Ruta de la DLL: %s" },
    { "Status: %s", "Estado: %s" },
    { "Dispatch Count: %llu (errors: %llu)", "Dispatch ejecutados: %llu (errores: %llu)" },
    { "Last Switch: %s", "Último cambio: %s" },
    { "Request: %s", "Solicitud: %s" },
    { "Context: %s", "Contexto: %s" },
    { "Traced by: %s", "Rastreado por: %s" },
    { "Driver provider: %s", "Proveedor del driver: %s" },
    { "not requested", "no solicitado" },
    { "code address", "dirección de código" },
    { "hook trampoline target", "destino del trampolín del hook" },
    { "ACTIVE", "ACTIVA" },
    { "MISMATCH", "NO COINCIDE" },
    { "UNVERIFIED (no upscale dispatch seen yet)", "SIN VERIFICAR (aún no se ha visto ningún dispatch de escalado)" },
    { "UNVERIFIED (dispatch function can't be traced to a dll)",
      "SIN VERIFICAR (la función de dispatch no se puede rastrear hasta una dll)" },
    { "UNVERIFIED (no upscale dispatch in the last second)",
      "SIN VERIFICAR (ningún dispatch de escalado en el último segundo)" },
    { "UNVERIFIED (selected dll not found)", "SIN VERIFICAR (no se encuentra la dll seleccionada)" },
    { "SWITCHING (waiting for a dispatch with the selected dll)",
      "CAMBIANDO (esperando un dispatch con la dll seleccionada)" },
    { "Dll that owns the ffxDispatch function called by the last upscale dispatch",
      "Dll a la que pertenece la función ffxDispatch llamada en el último dispatch de escalado" },
    { "Change of the dll that executes the dispatch, as seen in the dispatch calls",
      "Cambio de la dll que ejecuta el dispatch, observado en las propias llamadas" },
    { "Result of amdxcffx64's provider update for the upscaler dll that asked for it.\n"
      "When it succeeds the dll may run the driver's FSR 4 instead of its own,\n"
      "that can't be told apart from the dispatch call.",
      "Resultado de la petición de proveedor a amdxcffx64 hecha por la dll de escalado.\n"
      "Si tiene éxito, la dll puede ejecutar el FSR 4 del driver en lugar del suyo;\n"
      "eso no se puede distinguir desde la llamada de dispatch." },

    // Visual verification
    { "FSR 4 VISUAL VERIFICATION", "VERIFICACIÓN VISUAL FSR 4" },
    { "Color marker on upscaler output", "Marcador de color en la salida del escalador" },
    { "Disabled", "Desactivado" },
    { "Verdict: NO DATA (no upscale dispatch in the last second)",
      "Veredicto: SIN DATOS (ningún dispatch de escalado en el último segundo)" },
    { "Executing DLL: %s", "DLL que ejecuta: %s" },
    { "Context created by: %s", "Contexto creado por: %s" },
    { "unknown", "desconocido" },
    { "the same dll", "la misma dll" },
    { "A DIFFERENT DLL", "OTRA DLL DISTINTA" },
    { "Compute dispatches: %u by this dll, %u by others", "Dispatch de cómputo: %u de esta dll, %u de otros" },
    { "Other module: %s", "Otro módulo: %s" },
    { "Marker: %s", "Marcador: %s" },
    { "Marker draw: %s", "Dibujo del marcador: %s" },
    { "Verdict: VERIFIED", "Veredicto: VERIFICADO" },
    { "Verdict: NOT VERIFIED (%s)", "Veredicto: NO VERIFICADO (%s)" },
    { "Frames: %llu verified of %llu", "Fotogramas: %llu verificados de %llu" },
    { "command list can't be observed", "no se puede observar la lista de comandos" },
    { "dispatch function not traced", "función de dispatch no rastreada" },
    { "context owner unknown", "dueño del contexto desconocido" },
    { "context belongs to another dll", "el contexto pertenece a otra dll" },
    { "GPU work recorded by another module", "trabajo de GPU emitido por otro módulo" },
    { "no compute dispatch seen", "no se observó ningún dispatch de cómputo" },
    { "dispatch failed", "el dispatch falló" },
    { "blue (Official)", "azul (Official)" },
    { "green (Community)", "verde (Community)" },
    { "magenta (Lite)", "magenta (Lite)" },
    { "white (other dll)", "blanco (otra dll)" },
    { "yellow, top only (not verified)", "amarillo, solo arriba (no verificado)" },
    { "none", "ninguno" },
    { "written to the upscaler output", "escrito en la salida del escalador" },
    { "skipped, dispatch failed", "omitido, el dispatch falló" },
    { "skipped, no command list or output", "omitido, sin lista de comandos o sin salida" },
    { "skipped, not a direct command list", "omitido, no es una lista de comandos directa" },
    { "skipped, output is not in UAV state", "omitido, la salida no está en estado UAV" },
    { "skipped, output format isn't supported", "omitido, formato de salida no admitido" },
    { "skipped, can't create the marker texture", "omitido, no se puede crear la textura del marcador" },
    { "not run", "no ejecutado" },
    { "Writes color bars into the output of the upscale dispatch, before the game's post\n"
      "processing. Top and bottom bars: the dll below recorded the compute work of the frame.\n"
      "Blue = Official, Green = Community, Magenta = Lite, White = any other dll.\n"
      "Yellow top bar only = could not be verified.\n\n"
      "The bars are drawn by OptiScaler, not by the dll. Not saved, off on next launch.",
      "Escribe barras de color en la salida del dispatch de escalado, antes del posprocesado\n"
      "del juego. Barras arriba y abajo: la dll indicada emitió el trabajo de cómputo del fotograma.\n"
      "Azul = Official, Verde = Community, Magenta = Lite, Blanco = cualquier otra dll.\n"
      "Solo barra amarilla arriba = no se pudo verificar.\n\n"
      "Las barras las dibuja OptiScaler, no la dll. No se guarda; desactivado en el próximo inicio." },

    // Comparison mode
    { "FSR 4 Comparison Mode", "Modo de comparación FSR 4" },
    { "WAITING", "ESPERANDO" },
    { "STABILIZING", "ESTABILIZANDO" },
    { "RECORDING", "MIDIENDO" },
    { "SAVED", "GUARDADO" },
    { "min --  max --", "mín --  máx --" },
    { "min %.0f  max %.0f", "mín %.0f  máx %.0f" },
    { "Active dll isn't Official, Community or Lite, not measured",
      "La dll activa no es Official, Community ni Lite; no se mide" },
    { "Includes frames presented by frame generation", "Incluye fotogramas presentados por frame generation" },
    { "Measures every upscaler dll while it's the one executing the upscaling and shows\n"
      "the results under the performance overlay. Switch the dll above to measure the next one.\n\n"
      "AVG = counted frames / their total time\n"
      "MIN / MAX = slowest / fastest single frame, as fps (not 1%% lows)\n\n"
      "The first 2 seconds after a switch aren't counted.\n"
      "Enabling it starts a new session and shows the performance overlay.",
      "Mide cada dll de escalado mientras es la que ejecuta el escalado y muestra\n"
      "los resultados bajo el overlay de rendimiento. Cambia la dll arriba para medir la siguiente.\n\n"
      "AVG = fotogramas contados / su tiempo total\n"
      "MIN / MAX = fotograma más lento / más rápido, en fps (no son 1%% lows)\n\n"
      "Los primeros 2 segundos tras un cambio no se cuentan.\n"
      "Al activarlo empieza una sesión nueva y se muestra el overlay de rendimiento." },

    // Interface mode and the Basic Mode panel
    { "Basic", "Básico" },
    { "Full", "Completo" },
    { "Current Status", "Estado actual" },
    { "Upscaler", "Escalador" },
    { "FSR version", "Versión FSR" },
    { "Latency", "Latencia" },

    // Hardware readings of the overlay
    { "N/A", "N/D" },
    { "Usage", "Uso" },
    { "Hardware monitoring", "Monitorización de hardware" },
    { "GPU usage", "Uso de GPU" },
    { "GPU temperature", "Temperatura de GPU" },
    { "GPU hotspot", "Hotspot de GPU" },
    { "VRAM used / total", "VRAM usada / total" },
    { "VRAM used", "VRAM usada" },
    { "CPU usage", "Uso de CPU" },
    { "CPU temperature", "Temperatura de CPU" },
    { "Source", "Fuente" },
    { "Auto (HWiNFO, then MSI Afterburner)", "Auto (HWiNFO, luego MSI Afterburner)" },
    { "detected", "detectado" },
    { "not detected", "no detectado" },
    { "Readings come from HWiNFO or MSI Afterburner while they are running, about once per second.\n"
      "HWiNFO needs Shared Memory Support enabled in its settings (12 hours per run in the free version).\n"
      "A reading that no program provides is shown as N/A, nothing is estimated.",
      "Las lecturas vienen de HWiNFO o MSI Afterburner mientras están en ejecución, una vez por segundo aprox.\n"
      "HWiNFO necesita Shared Memory Support activado en sus ajustes (12 horas por sesión en la versión gratuita).\n"
      "Una lectura que ningún programa ofrece se muestra como N/D; no se estima nada." },

    // Benchmark
    { "Change FSR version", "Cambiar versión FSR" },
    { "Opens the Upscaling tab, the FSR version is selected there.\nIt can't be changed while a test is running.",
      "Abre la pestaña Escalado; la versión FSR se selecciona allí.\nNo se puede cambiar mientras hay una prueba en curso." },
    { "Duration", "Duración" },
    { "Start", "Iniciar" },
    { "Stop", "Detener" },
    { "Cancel", "Cancelar" },
    { "Save", "Guardar" },
    { "Starts a test of what's in use right now. Recording begins after 2 seconds.\n"
      "The test keeps running with the menu closed, the performance overlay shows its progress.",
      "Inicia una prueba de lo que está en uso ahora. La medición empieza a los 2 segundos.\n"
      "La prueba sigue con el menú cerrado; el overlay de rendimiento muestra su progreso." },
    { "Ends the test now and keeps what was recorded", "Termina la prueba ahora y conserva lo medido" },
    { "Ends the test and discards it", "Termina la prueba y la descarta" },
    { "Writes the result to ILION\\Benchmarks next to the game exe:\n"
      "one json per test, the frame times, and a line in benchmarks.csv.\n"
      "Existing files are never overwritten.",
      "Escribe el resultado en ILION\\Benchmarks junto al exe del juego:\n"
      "un json por prueba, los tiempos de fotograma y una línea en benchmarks.csv.\n"
      "Nunca se sobrescriben archivos existentes." },
    { "Calculating...", "Calculando..." },
    { "No test yet", "Aún no hay ninguna prueba" },
    { "The upscaler dll changed during the test", "La dll de escalado cambió durante la prueba" },
    { "Result", "Resultado" },
    { "Test time", "Tiempo de prueba" },
    { "frames", "fotogramas" },
    { ", stopped early", ", detenida antes" },
    { "Average FPS", "FPS promedio" },
    { "Median FPS", "FPS mediano" },
    { "Frame time mean / median", "Frametime medio / mediano" },
    { "Frame time P95 / P99", "Frametime P95 / P99" },
    { "Frame time min / max", "Frametime mín / máx" },
    { "Variability", "Variabilidad" },
    { "Frames without upscaling", "Fotogramas sin escalado" },
    { "Hardware values: average / maximum during the test", "Valores de hardware: media / máximo durante la prueba" },
    { "Frame generation was active (x%d):", "Frame generation estuvo activo (x%d):" },
    { "FPS values are presented frames and include generated ones.",
      "Los FPS son fotogramas presentados e incluyen los generados." },
    { "Saved:", "Guardado:" },
    { "A result with this id already exists", "Ya existe un resultado con este identificador" },
    { "Can't write to the benchmarks folder", "No se puede escribir en la carpeta de benchmarks" },
    { "Tools", "Herramientas" },
    { "Performance overlay", "Overlay de rendimiento" },
    { "Overlay and hardware settings", "Ajustes del overlay y del hardware" },

    // Benchmark comparison
    { "Compare results", "Comparar resultados" },
    { "Opens the saved tests. Pick two or more to see them side by side,\n"
      "with their differences and an automatic conclusion.",
      "Abre las pruebas guardadas. Elige dos o más para verlas lado a lado,\n"
      "con sus diferencias y una conclusión automática." },
    { "Benchmark results comparison", "Comparación de resultados de benchmark" },
    { "Performance and stability comparison. Image quality is not evaluated.",
      "Comparación de rendimiento y estabilidad. La calidad de imagen no se evalúa." },
    { "Back to Benchmark", "Volver a Benchmark" },
    { "Reload", "Recargar" },
    { "Clear selection", "Quitar selección" },
    { "%d saved tests, %d selected", "%d pruebas guardadas, %d seleccionadas" },
    { "No saved tests yet. Run a benchmark and press Save.",
      "Aún no hay pruebas guardadas. Ejecuta un benchmark y pulsa Guardar." },
    { "Select two or more tests to compare them.", "Selecciona dos o más pruebas para compararlas." },
    { "Date and time", "Fecha y hora" },
    { "Game", "Juego" },
    { "Resolution", "Resolución" },
    { "Reference", "Referencia" },
    { "  [reference]", "  [referencia]" },
    { "Conclusions", "Conclusiones" },
    { "Results", "Resultados" },
    { "DLL file version", "Versión de archivo de la DLL" },
    { "Frame time mean", "Frametime medio" },
    { "Frame time P95", "Frametime P95" },
    { "Frame time P99", "Frametime P99" },
    { "Stutter events", "Eventos de stutter" },
    { "Stutters per minute", "Stutters por minuto" },
    { "Frames", "Fotogramas" },
    { "Hardware (informative)", "Hardware (informativo)" },
    { "GPU usage, average", "Uso de GPU, media" },
    { "CPU usage, average", "Uso de CPU, media" },
    { "GPU temperature, average", "Temperatura de GPU, media" },
    { "GPU temperature, maximum", "Temperatura de GPU, máxima" },
    { "GPU hotspot, maximum", "Hotspot de GPU, máximo" },
    { "CPU temperature, average", "Temperatura de CPU, media" },
    { "CPU temperature, maximum", "Temperatura de CPU, máxima" },
    { "VRAM used, average", "VRAM usada, media" },
    { "VRAM used, maximum", "VRAM usada, máxima" },
    { "Margins used: %.1f%% in average FPS, %.1f%% in 1%% low.%s",
      "Márgenes usados: %.1f%% en FPS promedio, %.1f%% en 1%% low.%s" },
    { " Taken from the spread between tests of the same dll.",
      " Tomados de la variación entre pruebas de la misma dll." },
    { "%s (average of %d tests)", "%s (media de %d pruebas)" },
    { "INCONCLUSIVE RESULT", "RESULTADO NO CONCLUYENTE" },
    { "The tests were made under different conditions, no winner is named. "
      "The differences below are shown with that limitation.",
      "Las pruebas se hicieron en condiciones distintas; no se nombra ganador. "
      "Las diferencias de abajo se muestran con esa limitación." },
    { "All the selected tests were made with the same dll. Their average FPS differ "
      "by %.1f%%, which is the variation between runs.",
      "Todas las pruebas seleccionadas se hicieron con la misma dll. Sus FPS promedio difieren "
      "un %.1f%%, que es la variación entre ejecuciones." },
    { "Some of the tests lack the values needed to compare.",
      "A alguna de las pruebas le faltan los valores necesarios para comparar." },
    { "PERFORMANCE WINNER: %s", "GANADOR EN RENDIMIENTO: %s" },
    { "%s reached %.1f average FPS, %.1f%% more than %s (%.1f).",
      "%s alcanzó %.1f FPS promedio, un %.1f%% más que %s (%.1f)." },
    { " Its 1%% low is clearly lower than the one of %s, the higher average comes with worse lows.",
      " Su 1%% low es claramente inferior al de %s: la media más alta viene con peores mínimos." },
    { " Its 1%% low is %.1f%% higher.", " Su 1%% low es un %.1f%% superior." },
    { " Its 1%% low is %.1f%% lower.", " Su 1%% low es un %.1f%% inferior." },
    { "PERFORMANCE: no clear winner", "RENDIMIENTO: sin ganador claro" },
    { "%s are within %.1f%% of each other in average FPS, the margin used here.",
      "%s están a menos de un %.1f%% entre sí en FPS promedio, el margen usado aquí." },
    { "STABILITY WINNER: %s", "GANADOR EN ESTABILIDAD: %s" },
    { "Frame time variation of %.1f%% against %.1f%% for %s.",
      "Variación del frametime del %.1f%% frente al %.1f%% de %s." },
    { " Its P99 is %.2fx the median against %.2fx.", " Su P99 es %.2fx la mediana frente a %.2fx." },
    { " Stutters per minute: %.1f against %.1f.", " Stutters por minuto: %.1f frente a %.1f." },
    { "STABILITY: inconclusive", "ESTABILIDAD: no concluyente" },
    { "%s has the steadiest frame times but clearly more stutters than %s.",
      "%s tiene los frametimes más regulares pero claramente más stutters que %s." },
    { "STABILITY: no clear winner", "ESTABILIDAD: sin ganador claro" },
    { "Frame time variation differs by less than the margin (%.1f%% against %.1f%%).",
      "La variación del frametime difiere menos que el margen (%.1f%% frente a %.1f%%)." },
    { "BEST OVERALL BALANCE: %s", "MEJOR EQUILIBRIO GENERAL: %s" },
    { "It leads in performance and in stability.", "Es la mejor en rendimiento y en estabilidad." },
    { "It is the fastest and as stable as the best.", "Es la más rápida y tan estable como la mejor." },
    { "It is the most stable and as fast as the best.", "Es la más estable y tan rápida como la mejor." },
    { "BALANCE: depends on the priority", "EQUILIBRIO: depende de la prioridad" },
    { "%s is faster, %s is more stable.", "%s es más rápida, %s es más estable." },
    { "%s is faster but is not among the most stable.", "%s es más rápida pero no está entre las más estables." },
    { "%s is more stable but is not among the fastest.", "%s es más estable pero no está entre las más rápidas." },
    { "No difference bigger than the margin was measured between these tests.",
      "No se midió ninguna diferencia mayor que el margen entre estas pruebas." },
    { "A best balance can't be named from these results.",
      "Con estos resultados no se puede nombrar un mejor equilibrio." },
    { "The tests are from different games.", "Las pruebas son de juegos distintos." },
    { "Render or output resolution differs between the tests.",
      "La resolución de render o de salida difiere entre las pruebas." },
    { "The tests used different upscaling technologies.", "Las pruebas usaron tecnologías de escalado distintas." },
    { "Frame generation was not in the same state in all the tests.",
      "Frame generation no estaba en el mismo estado en todas las pruebas." },
    { "The tests don't last the same (more than a tenth apart).",
      "Las pruebas no duran lo mismo (más de una décima parte de diferencia)." },
    { "At least one test was stopped before its time.", "Al menos una prueba se detuvo antes de tiempo." },
    { "At least one test has many frames without upscaling (menus, loading).",
      "Al menos una prueba tiene muchos fotogramas sin escalado (menús, cargas)." },
    { "At least one test has fewer than 1000 frames, its lowest percentile is not available.",
      "Al menos una prueba tiene menos de 1000 fotogramas; su percentil más bajo no está disponible." },
    { "ILION can't check that the same scene was played in every test.",
      "ILION no puede comprobar que en todas las pruebas se jugó la misma escena." },

    // Managing results
    { "Clear result", "Limpiar resultado" },
    { "Removes the last result from this panel.\nSaved tests are not deleted.",
      "Quita de este panel el último resultado.\nNo se borran las pruebas guardadas." },
    { "Delete tests", "Eliminar pruebas" },
    { "Deletes the selected tests for good: their saved result and their frame times.\n"
      "Clear selection only unticks them.",
      "Elimina definitivamente las pruebas seleccionadas: su resultado guardado y sus tiempos de fotograma.\n"
      "Quitar selección solo las desmarca." },
    { "Delete %d selected tests for good?", "¿Eliminar definitivamente %d pruebas seleccionadas?" },
    { "Their saved results and frame times are removed from disk. This can't be undone.",
      "Sus resultados guardados y tiempos de fotograma se borran del disco. No se puede deshacer." },
    { "Delete", "Eliminar" },
    { "Invalid test id", "Identificador de prueba no válido" },
    { "Can't read the saved tests", "No se pueden leer las pruebas guardadas" },
    { "Can't update the saved tests", "No se pueden actualizar las pruebas guardadas" },

    // Image captures
    { "Visual analysis", "Análisis visual" },
    { "Capture image", "Capturar imagen" },
    { "Saves the game's image as it is presented, without the ILION menu, as a PNG in ILION\\Captures.\n"
      "Stand still in the same spot, capture, switch the FSR version and capture again.\n"
      "With frame generation on, the captured frame may be a generated one.",
      "Guarda la imagen del juego tal como se presenta, sin el menú de ILION, como PNG en ILION\\Captures.\n"
      "Quédate quieto en el mismo punto, captura, cambia la versión FSR y vuelve a capturar.\n"
      "Con frame generation activo, el fotograma capturado puede ser uno generado." },
    { "Wait until the selected FSR version is the one that's active",
      "Espera a que la versión FSR seleccionada sea la que está activa" },
    { "Capturing...", "Capturando..." },
    { "No capture yet", "Aún no hay ninguna captura" },
    { "The format of the game's image isn't supported for captures",
      "El formato de la imagen del juego no está admitido para capturas" },
    { "Can't create the buffers for the capture", "No se pueden crear los búferes de la captura" },
    { "The game's image couldn't be reached, captures need DX12 with the overlay menu",
      "No se pudo acceder a la imagen del juego; las capturas necesitan DX12 con el menú en overlay" },
    { "The GPU didn't finish the copy in time", "La GPU no terminó la copia a tiempo" },
    { "Can't read the capture back from the GPU", "No se puede leer la captura desde la GPU" },
    { "Can't write the PNG file", "No se puede escribir el archivo PNG" },

    // Status tab
    { "Unofficial modified build, not an OptiScaler release",
      "Compilación modificada no oficial, no es una versión de OptiScaler" },
    { "Based on %s", "Basado en %s" },
};

struct TranslationEntry
{
    const char* es = nullptr;
    std::string label;
};

inline const std::unordered_map<std::string_view, TranslationEntry>& SpanishTable()
{
    static const std::unordered_map<std::string_view, TranslationEntry> table = []
    {
        std::unordered_map<std::string_view, TranslationEntry> result;
        result.reserve(std::size(kSpanish));

        for (const auto& translation : kSpanish)
            result[translation.en] = { translation.es, std::string(translation.es) + "###" + translation.en };

        return result;
    }();

    return table;
}

// Text in the selected language, the English text itself when there's no translation
inline const char* Tr(const char* english)
{
    if (english == nullptr || CurrentLang() == Lang::EN)
        return english;

    const auto& table = SpanishTable();
    const auto it = table.find(english);

    return it != table.end() ? it->second.es : english;
}

// Same for widget labels, the ImGui id stays the English label
inline const char* TrL(const char* english)
{
    if (english == nullptr || CurrentLang() == Lang::EN)
        return english;

    const auto& table = SpanishTable();
    const auto it = table.find(english);

    return it != table.end() ? it->second.label.c_str() : english;
}
} // namespace Ilion

#pragma execution_character_set(pop)
