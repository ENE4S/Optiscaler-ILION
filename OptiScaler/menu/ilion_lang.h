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
    // Universal upscaler selector (v1.2-dev.5)
    { "Upscaler##IlionUpscaler", "Escalador" },
    { "Conservative", "Conservador" },
    { "The safest, but might not reduce latency well", "El más seguro, aunque puede reducir poco la latencia" },
    { "Aggressive", "Agresivo" },
    { "Improves latency, but in some cases will lower FPS more than expected",
      "Mejora la latencia, pero a veces baja los FPS más de lo esperado" },
    { "Reflex ID", "Id. Reflex" },
    { "Best when can be used, some games are not compatible (e.g. Cyberpunk)\n"
      "and will fallback to Aggressive",
      "El mejor cuando se puede usar; algunos juegos no son compatibles (p. ej. Cyberpunk)\n"
      "y pasan a Agresivo" },
    { "Follow in-game", "Según el juego" },
    { "Force Disable", "Forzar desactivado" },
    { "Force Enable", "Forzar activado" },
    // dev.10: Graphics tab
    { "Colour, sharpness, textures and detail of the picture.", "Color, nitidez, texturas y detalle de la imagen." },
    { "Mipmap Bias", "Mipmap LOD Bias" },
    { "Reset", "Restablecer" },
    { "Override", "Sobrescribir" },
    { "Strength", "Intensidad" },
    { "Method", "Método" },
    { "(Current: %.3f)", "(Actual: %.3f)" },
    { "(Current: disabled)", "(Actual: desactivada)" },
    { "Upscaler's default", "Predeterminado del escalador" },
    { "Depth Aware (RCAS)", "Por profundidad (RCAS)" },
    { "Depth Aware (DAS)", "Por profundidad (DAS)" },
    { "Enable Motion Adaptive Sharpness", "Activar nitidez adaptativa al movimiento" },
    { "DA + MAS Debug", "Depuración DA + MAS" },
    { "MAS Debug", "Depuración MAS" },
    { "Advanced DA Parameters", "Parámetros DA avanzados" },
    { "Clamp Output", "Limitar salida" },
    { "Depth Bias", "Sesgo de profundidad" },
    { "Depth Scale", "Escala de profundidad" },
    { "Reset Depth Values", "Restablecer valores de profundidad" },
    { "Contrast Enabled", "Contraste activado" },
    { "Motion Adaptive Sharpness##2", "Nitidez adaptativa al movimiento (MAS)" },
    { "MotionSharpness", "Nitidez en movimiento" },
    { "MotionThreshod", "Umbral de movimiento" },
    { "MotionRange", "Rango de movimiento" },
    { "Ignores the value sent by the game\n"
      "and uses the value set below",
      "Ignora el valor que envía el juego\n"
      "y usa el valor de abajo" },
    { "Select the sharpening filter to apply.\n\n"
      "Upscaler's default: Uses the sharpening provided by the upscaler.\n"
      "RCAS: AMD's RCAS, modified for Contrast and MAS.\n"
      "Depth Aware: Smarter sharpening with fewer artifacts (heavier).\n\n"
      "By default uses a sharpening value provided by the game.\n"
      "Select 'Override' under 'Sharpness' and adjust the slider to change it.",
      "Elige el filtro de nitidez.\n\n"
      "Predeterminado del escalador: la nitidez que trae el escalador.\n"
      "RCAS: el RCAS de AMD, modificado para Contraste y MAS.\n"
      "Por profundidad: nitidez más inteligente con menos artefactos (más pesada).\n\n"
      "Por defecto usa el valor de nitidez que envía el juego.\n"
      "Marca «Sobrescribir» en «Nitidez» y mueve el deslizador para cambiarlo." },
    { "Enables sharpness adjustments according to the motion", "Ajusta la nitidez según el movimiento" },
    { "Enable DA + MAS debug views\n"
      "Blue tint for DA detected edges\n\n"
      "More red areas will have more sharpness applied\n"
      "Green areas will get reduced sharpness",
      "Vistas de depuración DA + MAS\n"
      "Tono azul en los bordes que detecta DA\n\n"
      "Las zonas más rojas reciben más nitidez\n"
      "Las zonas verdes reciben menos" },
    { "Clamps the final image to the [0, 1] range.\n\n"
      "Prevents overshoot artifacts such as bright halos or negative colors.\n"
      "Recommended for LDR pipelines; optional for HDR depending on tone-mapping.\n\n"
      "When not set OptiScaler controls it via upscalers HDR flag",
      "Limita la imagen final al rango [0, 1].\n\n"
      "Evita excesos como halos brillantes o colores negativos.\n"
      "Recomendado en SDR; opcional en HDR según el tonemapping.\n\n"
      "Sin marcar, OptiScaler lo decide con la marca HDR del escalador" },
    { "Ignores small depth differences before edge detection.\n\n"
      "Higher values reduce flickering and noise from minor depth changes, but may "
      "soften real geometry edges.\n"
      "Lower values preserve fine detail but can cause unstable or noisy edge "
      "detection.",
      "Ignora pequeñas diferencias de profundidad antes de detectar bordes.\n\n"
      "Valores altos reducen parpadeo y ruido, pero pueden suavizar bordes reales.\n"
      "Valores bajos conservan el detalle fino, pero la detección puede ser inestable." },
    { "Controls how strongly sharpening is reduced across depth edges.\n\n"
      "Higher values more aggressively prevent sharpening across object boundaries "
      "(reduces halos).\n"
      "Lower values allow more sharpening to pass across edges (sharper but "
      "riskier).",
      "Cuánto se reduce la nitidez en los bordes de profundidad.\n\n"
      "Valores altos evitan más la nitidez entre objetos (menos halos).\n"
      "Valores bajos dejan pasar más nitidez por los bordes (más nítido, más riesgo)." },
    { "Controls sharpness at high contrast areas.", "Controla la nitidez en zonas de alto contraste." },
    { "Positive values decrease sharpness at high contrast areas.\n"
      "Negative values increase sharpness at high contrast areas.",
      "Valores positivos reducen la nitidez en zonas de alto contraste.\n"
      "Valores negativos la aumentan." },
    { "Areas that are more red will have more sharpness applied\n"
      "Green areas will get reduced sharpness",
      "Las zonas más rojas reciben más nitidez\n"
      "Las zonas verdes reciben menos" },
    { "Maximum amount of sharpness that motion can add or remove.\n\n"
      "Negative values reduce sharpening in motion (recommended).\n"
      "Positive values increase sharpening in motion.\n\n"
      "The final adjustment scales with motion and is capped at this value.",
      "Nitidez máxima que el movimiento puede añadir o quitar.\n\n"
      "Valores negativos reducen la nitidez en movimiento (recomendado).\n"
      "Valores positivos la aumentan.\n\n"
      "El ajuste crece con el movimiento y no pasa de este valor." },
    { "Minimum motion required before motion-based sharpening adjustment begins.\n\n"
      "Higher values ignore small movements (more stable).\n"
      "Lower values react to subtle motion (more sensitive).",
      "Movimiento mínimo para que empiece el ajuste.\n\n"
      "Valores altos ignoran movimientos pequeños (más estable).\n"
      "Valores bajos reaccionan a movimientos sutiles (más sensible)." },
    { "Defines the motion range over which the effect ramps from zero to full strength.\n\n"
      "Values above the threshold are mapped into this range.\n"
      "Larger values make the response smoother and more gradual.\n"
      "Smaller values make the effect react more quickly and aggressively.",
      "Rango de movimiento en el que el efecto pasa de cero a su máximo.\n\n"
      "Lo que supera el umbral se reparte en este rango.\n"
      "Valores grandes: respuesta más suave y gradual.\n"
      "Valores pequeños: respuesta más rápida y brusca." },
    { "Mipmap Bias##2", "Mipmap LOD Bias" },
    { "MB Fixed Override", "Valor fijo" },
    { "MB Scale Override", "Escala" },
    { "MB Override All Textures", "Aplicar a todas las texturas" },
    { "Set", "Aplicar" },
    { "Calculate Mipmap Bias", "Calcular Mipmap LOD Bias" },
    { "Current : %.3f / %.3f", "Actual: %.3f / %.3f" },
    { "Current : %.3f / %.3f, Target: %.3f", "Actual: %.3f / %.3f, objetivo: %.3f" },
    { "Current : %.3f / %.3f, Target: Base * %.3f", "Actual: %.3f / %.3f, objetivo: base * %.3f" },
    { "Current : %.3f / %.3f, Target: Base + %.3f", "Actual: %.3f / %.3f, objetivo: base + %.3f" },
    { "Will be applied after RESOLUTION/PRESET change !!!", "Se aplica tras cambiar la RESOLUCIÓN o el PRESET" },
    { "Will might be applied after RESOLUTION/PRESET change !!!", "Puede requerir cambiar la RESOLUCIÓN o el PRESET" },
    { "Can help with blurry textures in broken games\n"
      "Negative values will make textures sharper\n"
      "Positive values will make textures more blurry\n\n"
      "Has a small performance impact",
      "Ayuda con texturas borrosas en juegos defectuosos\n"
      "Valores negativos: texturas más nítidas\n"
      "Valores positivos: texturas más borrosas\n\n"
      "Tiene un pequeño coste de rendimiento" },
    { "Apply same override value to all textures", "Aplica el mismo valor a todas las texturas" },
    { "Apply override value as scale multiplier\n"
      "When using scale mode, please use positive\n"
      "override values to increase sharpness!",
      "Aplica el valor como multiplicador\n"
      "En modo escala usa valores positivos\n"
      "para aumentar la nitidez" },
    { "Override all textures mipmap values\n"
      "Normally OptiScaler only overrides\n"
      "below zero mipmap values!",
      "Sustituye el mipmap de todas las texturas\n"
      "Normalmente OptiScaler solo cambia\n"
      "los valores por debajo de cero" },
    { "Force Anisotropic Filtering", "Forzar filtrado anisotrópico" },
    { "Modify Compare", "Modificar comparación" },
    { "Modify Min/Max", "Modificar mín./máx." },
    { "Skip Point Filters", "Omitir filtros puntuales" },
    { "Update comparison filters", "Actualiza los filtros de comparación" },
    { "Update min/max filters", "Actualiza los filtros mín./máx." },
    { "Skip updating of point filters", "No cambia los filtros puntuales" },
    { "Enable Magnifier", "Activar lupa" },
    { "%.1f%% of screen", "%.1f%% de la pantalla" },
    { "%.2f%% of screen", "%.2f%% de la pantalla" },
    { "Zoom Factor", "Aumento" },
    { "Border Size", "Grosor del borde" },
    { "Positioning", "Posición" },
    { "Static Pos X", "Posición fija X" },
    { "Static Pos Y", "Posición fija Y" },
    { "Reset Static Position (Follow Cursor)", "Quitar posición fija (seguir el puntero)" },
    { "Set Static Position", "Fijar posición" },
    { "(Currently following cursor)", "(Ahora sigue al puntero)" },
    { "Cursor Offset X", "Desplazamiento X" },
    { "Cursor Offset Y", "Desplazamiento Y" },
    { "With Depth Aware (DAS) sharpening halos can add up", "Con la nitidez por profundidad (DAS) los halos pueden sumarse" },
    // dev.10: fakenvapi card
    { "Force LatencyFlex", "Forzar LatencyFleX" },
    { "Force XeLL", "Forzar XeLL" },
    { "LatencyFlex mode", "Modo LatencyFleX" },
    { "Force Reflex", "Forzar Reflex" },
    { "By default, FSR Anti-Lag 2.0/XeLL is used when available.\n"
      "This setting lets you force LatencyFlex instead",
      "Por defecto se usa Anti-Lag 2 o XeLL si están disponibles.\n"
      "Esta opción fuerza LatencyFleX en su lugar" },
    { "Allows XeLL to work without FG on non-Intel cards.\n\nDisables FG "
      "options\n\nRequires a restart",
      "Permite XeLL sin FG en GPU que no son Intel.\n\nDesactiva las opciones "
      "de FG\n\nRequiere reiniciar" },
    { "Save INI and restart to apply the changes", "Guarda los ajustes y reinicia para aplicar los cambios" },
    // dev.10: Intelligent Activation
    { "Intelligent Activation", "Activación inteligente" },
    { "Automatic##IlionLat", "Automático" },
    { "Manual##IlionLat", "Manual" },
    { "Off##IlionLat", "Desactivado" },
    { "ILION lets OptiScaler pick the compatible technology", "ILION deja que OptiScaler elija la tecnología compatible" },
    { "The original options below stay in your hands", "Las opciones originales de abajo quedan en tus manos" },
    { "Asks OptiScaler to turn latency reduction off", "Pide a OptiScaler que desactive la reducción de latencia" },
    { "State", "Estado" },
    { "Reason", "Motivo" },
    { "Off at your request", "Desactivada por ti" },
    { "Reflex requests of the game are ignored", "Se ignoran las peticiones Reflex del juego" },
    { "Available", "Disponible" },
    { "Not compatible", "No compatible" },
    { "Enabled, no frame markers yet", "Activada, aún sin marcadores de fotograma" },
    { "Running", "En ejecución" },
    { "Native Reflex, handled by the game; OptiScaler doesn't select it",
      "Reflex nativo, lo gestiona el juego; OptiScaler no lo elige" },
    { "OptiScaler's nvapi is not in use: no Reflex input from the game",
      "La nvapi de OptiScaler no está en uso: no hay entrada Reflex del juego" },
    { "Off while more than one frame is generated", "Desactivada mientras se genera más de un fotograma" },
    { "Waiting for the game's Reflex request", "Esperando la petición Reflex del juego" },
    { "Chosen for AMD GPU, %s and Reflex input", "Elegido por GPU AMD, %s y entrada Reflex" },
    { "Chosen for AMD GPU, Vulkan and Reflex input", "Elegido por GPU AMD, Vulkan y entrada Reflex" },
    { "Forced with Force XeLL", "Forzado con «Forzar XeLL»" },
    { "Chosen for %s: Anti-Lag 2 not available", "Elegido en %s: Anti-Lag 2 no disponible" },
    { "Forced with Force LatencyFlex", "Forzado con «Forzar LatencyFleX»" },
    { "Anti-Lag 2 and XeLL are not used with multi frame generation",
      "Anti-Lag 2 y XeLL no se usan con generación multifotograma" },
    { "General fallback: Anti-Lag 2 and XeLL not available", "Alternativa general: Anti-Lag 2 y XeLL no disponibles" },
    { "Latency not measured here", "La latencia no se mide aquí" },
    { "Diagnostics", "Diagnóstico" },
    { "yes", "sí" },
    { "no", "no" },
    { "moving", "avanza" },
    { "still", "parado" },
    { "OptiScaler nvapi (fakenvapi): %s", "nvapi de OptiScaler (fakenvapi): %s" },
    { "Low latency enabled: %s", "Baja latencia activada: %s" },
    { "Reflex frame id: %llu (%s)", "Id. de fotograma Reflex: %llu (%s)" },
    { "More than one generated frame: %s", "Más de un fotograma generado: %s" },
    { "Graphics", "Mejoras gráficas" },
    { "Backend and output scaling.", "Motor y escalado de salida." },
    { "Colour, sharpness and detail of the upscaled picture.", "Color, nitidez y detalle de la imagen escalada." },
    { "Colour and image", "Color e imagen" },
    { "Advanced adjustments", "Ajustes avanzados" },
    { "Enable graphics enhancements", "Activar mejoras gráficas" },
    { "One colour pass after the upscaler and the sharpening, before the game's HUD.\n"
      "Off, or with every value neutral, the pass is not run: the picture is untouched.",
      "Un pase de color tras el escalador y la nitidez, antes de la HUD del juego.\n"
      "Desactivado, o con todos los valores neutros, el pase no se ejecuta: la imagen queda intacta." },
    { "Applied only while OptiScaler upscales the game", "Solo se aplica mientras OptiScaler escala el juego" },
    { "Showing the original picture", "Mostrando la imagen original" },
    { "Colour pass running", "Pase de color en ejecución" },
    { "Colour pass not running: off or every value neutral", "Pase de color sin ejecutar: desactivado o todo neutro" },
    { "HDR picture: brightness and contrast work on linear light", "Imagen HDR: brillo y contraste actúan sobre luz lineal" },
    { "Switches this adjustment on or off, its value is kept", "Activa o desactiva este ajuste, su valor se conserva" },
    { "Temperature", "Temperatura" },
    { "Brightness", "Brillo" },
    { "Contrast", "Contraste" },
    { "Saturation", "Saturación" },
    { "Vibrance", "Vibrancia" },
    { "Below 0 cooler (blue), above 0 warmer (orange). Brightness is kept.",
      "Por debajo de 0 más frío (azul), por encima más cálido (naranja). El brillo se mantiene." },
    { "Exposure, up to one stop darker or brighter.", "Exposición, hasta un paso más oscuro o más claro." },
    { "Curve around mid grey. In SDR black and white stay where they are.",
      "Curva alrededor del gris medio. En SDR el negro y el blanco no se mueven." },
    { "Strength of every colour alike. -1 is black and white.", "Intensidad de todos los colores por igual. -1 es blanco y negro." },
    { "Raises dull colours more than the ones already strong, so skin and skies don't burn.",
      "Realza más los colores apagados que los ya intensos, para no quemar pieles ni cielos." },
    { "Comparison", "Comparación" },
    { "Split screen (left half original)", "Pantalla dividida (mitad izquierda original)" },
    { "Show the original picture", "Mostrar la imagen original" },
    { "Reset all", "Restablecer todo" },
    { "Every value back to neutral; saved with Save Settings, for this game",
      "Todos los valores a neutro; se guarda con Guardar ajustes, para este juego" },
    { "Local contrast (experimental)", "Contraste local (experimental)" },
    { "Raises detail against its surroundings. Strong values can draw halos round edges.",
      "Realza el detalle frente a su entorno. Valores altos pueden crear halos en los bordes." },
    { "Uses the switch of Colour and image; off by default", "Usa el interruptor de Color e imagen; desactivado por defecto" },
    { "Fallout 2", "Fallout 2" },
    { "API not compatible", "API no compatible" },
    { "Hardware not compatible", "Hardware no compatible" },
    { "DLL not available", "DLL no disponible" },
    { "Dependency not available", "Dependencia no disponible" },
    { "d4r only works in DirectX 12", "d4r solo funciona en DirectX 12" },
    { "d4r runs DLSS on AMD GPUs", "d4r ejecuta DLSS en GPU AMD" },
    { "FSR 4 needs an AMD GPU", "FSR 4 necesita una GPU AMD" },
    { "Hardware not compatible: XeSS in DirectX 11 runs natively only on Intel GPUs",
      "Hardware no compatible: XeSS en DirectX 11 solo funciona de forma nativa en GPU Intel" },
    { "the FFX dll in use lists no FSR 3.1 provider", "la dll FFX en uso no ofrece el proveedor FSR 3.1" },
    { "Runs through OptiScaler's DirectX 12 interop.", "Funciona mediante la interoperabilidad DirectX 12 de OptiScaler." },
    { "FSR 2.1.2 (DX12 interop)", "FSR 2.1.2 (interop. DX12)" },
    { "XeSS (DX12 interop)", "XeSS (interop. DX12)" },
    { "layers fell back to translated code", "capas pasaron a código traducido" },
    { "no layer fell back to translated code; launches are only logged with Log = verbose",
      "ninguna capa pasó a código traducido; los lanzamientos solo se registran con Log = verbose" },
    { "GPU work of every accepted evaluation finished without CUDA errors",
      "el trabajo GPU de cada evaluación aceptada terminó sin errores CUDA" },
    { "Every accepted output was finite (d4r checks NaN/Inf)",
      "cada salida aceptada fue finita (d4r comprueba NaN/Inf)" },
    { "Upscaler##IlionFamily", "Escalador" },
    { " official (AMD)", " oficial de AMD" },
    { "Built into OptiScaler, switches while the game runs.", "Incluido en OptiScaler, cambia sin reiniciar el juego." },
    { "Active, evaluating frames", "Activo, evaluando fotogramas" },
    { "Rescan##IlionUpscaler", "Actualizar lista" },
    { "FSR (default dll)", "FSR (dll por defecto)" },
    { "AMD's FSR dll next to OptiScaler, or the game's own.", "La dll de FSR de AMD junto a OptiScaler, o la propia del juego." },
    { "FSR 4 official (AMD)", "FSR 4 oficial de AMD" },
    { "DLSS 4 - model K", "DLSS 4 — modelo K" },
    { "active", "activo" },
    { "loaded", "cargado" },
    { "selected", "seleccionado" },
    { "File version", "Versión del archivo" },
    { "Switches while the game runs.", "Cambia sin reiniciar el juego." },
    { "Needs a restart of the game: d4r is loaded when the game starts.",
      "Requiere reiniciar el juego: d4r se carga al arrancar el juego." },
    { "AMD's FSR is next to it: HelixSR lists it after itself. ILION always starts HelixSR's own provider.",
      "Tiene al lado la FSR de AMD: HelixSR la ofrece después de sí mismo. ILION arranca siempre el proveedor propio de HelixSR." },
    { "Upscalers ILION found in ILION\\Escaladores (next to the game exe):\n"
      "  FSR4\\<name>\\amd_fidelityfx_upscaler_dx12.dll   FSR 4 builds, switch while playing\n"
      "  HelixSR\\helixsr_upscaler.dll                    HelixSR, switches while playing\n"
      "  DLSS4\\<name>\\ (d4r folder)                     DLSS 4 model K, needs a restart\n"
      "Older ILION\\FSR_Versions folders keep working. What a dll is comes from the file, not from the\n"
      "name of its folder. Add one and press Rescan.",
      "Escaladores que ILION encontró en ILION\\Escaladores (junto al exe del juego):\n"
      "  FSR4\\<nombre>\\amd_fidelityfx_upscaler_dx12.dll   versiones de FSR 4, cambian sin reiniciar\n"
      "  HelixSR\\helixsr_upscaler.dll                      HelixSR, cambia sin reiniciar\n"
      "  DLSS4\\<nombre>\\ (carpeta de d4r)                 DLSS 4 modelo K, requiere reiniciar\n"
      "Las carpetas antiguas ILION\\FSR_Versions siguen funcionando. Qué es cada dll se sabe por el archivo, no\n"
      "por el nombre de su carpeta. Añade una y pulsa Actualizar lista." },
    { "It can't be changed while a benchmark is running.", "No se puede cambiar mientras se ejecuta una prueba." },
    { "DLSS 4 is saved as the upscaler. Restart the game to load it (d4r is loaded when the game starts).",
      "DLSS 4 queda guardado como escalador. Reinicia el juego para cargarlo (d4r se carga al arrancar el juego)." },
    { "this is AMD's FSR, not HelixSR (HelixSR's own dll is amd_fidelityfx_dx12.dll, built by its setup)",
      "esto es la FSR de AMD, no HelixSR (la dll propia de HelixSR es amd_fidelityfx_dx12.dll, generada por su instalador)" },
    { "HelixSR without its network: run HelixSR's setup and copy the dll it builds",
      "HelixSR sin su red neuronal: ejecuta el instalador de HelixSR y copia la dll que genera" },
    { "rename it to helixsr_upscaler.dll", "cámbiale el nombre a helixsr_upscaler.dll" },
    { "this file is not HelixSR", "este archivo no es HelixSR" },
    { "d4r.ini asks for another model; this version of ILION only uses model K",
      "d4r.ini pide otro modelo; esta versión de ILION solo usa el modelo K" },
    { "NVIDIA's nvngx_dlss.dll and ngx\\_nvngx.dll are missing in the d4r folder",
      "faltan nvngx_dlss.dll y ngx\\_nvngx.dll de NVIDIA en la carpeta de d4r" },
    { "Can't find the dll of the selected upscaler", "No se encuentra la dll del escalador seleccionado" },
    { "Can't load the dll of the selected upscaler", "No se puede cargar la dll del escalador seleccionado" },
    { "The selected upscaler is not a valid dll", "El escalador seleccionado no es una dll válida" },
    { "The selected upscaler isn't a compatible dll", "El escalador seleccionado no es una dll compatible" },
    { "The selected upscaler failed to start, the previous one was restored.",
      "El escalador seleccionado no pudo arrancar; se ha restaurado el anterior." },

    // Benchmark, universal labels
    { "Implementation", "Implementación" },
    { "Upscaling mode", "Modo de escalado" },
    { "Status", "Estado" },
    { "No upscaler running", "Ningún escalador en marcha" },
    { "Waiting for the first upscale", "Esperando el primer escalado" },
    { "Active, confirmed by its upscale calls", "Activo, confirmado por sus llamadas de escalado" },
    { "Active, NGX accepted its evaluations", "Activo, NGX aceptó sus evaluaciones" },
    { "Loaded, no evaluation confirmed yet", "Cargado, aún sin evaluaciones confirmadas" },
    { "Change upscaler", "Cambiar escalador" },
    { "Opens ILION's upscaler selector here, without leaving Benchmark.\n"
      "It can't be changed while a test is running.",
      "Abre aquí el selector de escaladores de ILION, sin salir de Benchmark.\n"
      "No se puede cambiar mientras se ejecuta una prueba." },

    // Upscaler comparison mode
    { "Upscaler comparison mode", "Modo comparación de escaladores" },
    { "Upscaler comparison: waiting for an upscale...", "Comparación de escaladores: esperando un escalado..." },
    { "More upscalers than boxes in this session, the last one isn't measured",
      "Hay más escaladores que recuadros en esta sesión; el último no se mide" },
    { "Measures every upscaler dll while it's the one executing the upscaling and shows\n"
      "the results under the performance overlay, one box per upscaler, named as ILION's list\n"
      "knows it. Switch the upscaler to measure the next one. Live only for FFX upscalers\n"
      "(FSR 4 builds, HelixSR); DLSS is compared through saved Benchmark tests.\n\n"
      "AVG = counted frames / their total time\n"
      "MIN / MAX = slowest / fastest single frame, as fps (not 1%% lows)\n\n"
      "The first 2 seconds after a switch aren't counted.\n"
      "Enabling it starts a new session and shows the performance overlay.",
      "Mide cada dll de escalado mientras es la que ejecuta el escalado y muestra\n"
      "los resultados bajo el overlay de rendimiento, un recuadro por escalador, con el nombre\n"
      "que le da la lista de ILION. Cambia de escalador para medir el siguiente. En directo solo\n"
      "para escaladores FFX (versiones de FSR 4, HelixSR); DLSS se compara con pruebas guardadas.\n\n"
      "MEDIA = fotogramas contados / su tiempo total\n"
      "MÍN / MÁX = fotograma más lento / más rápido, en fps (no son 1%% bajos)\n\n"
      "No se cuentan los 2 primeros segundos tras un cambio.\n"
      "Activarlo empieza una sesión nueva y muestra el overlay de rendimiento." },
    { "UPSCALER HOT-SWAP DIAGNOSTICS", "DIAGNÓSTICO DE CAMBIO EN CALIENTE" },
    { "UPSCALER VISUAL VERIFICATION", "VERIFICACIÓN VISUAL DEL ESCALADOR" },
    { "Writes color bars into the output of the upscale dispatch, before the game's post\n"
      "processing. Top and bottom bars: the dll below recorded the compute work of the frame.\n"
      "Blue = FSR 4 official, Green = FSR 4.1.1B, Magenta = FSR 4 Lite, White = any other dll (HelixSR...).\n"
      "Yellow top bar only = could not be verified.\n\n"
      "The bars are drawn by OptiScaler, not by the dll. Not saved, off on next launch.",
      "Escribe barras de color en la salida del escalado, antes del posprocesado del juego.\n"
      "Barras arriba y abajo: la dll de abajo registró el trabajo de cómputo del fotograma.\n"
      "Azul = FSR 4 oficial, Verde = FSR 4.1.1B, Magenta = FSR 4 Lite, Blanco = cualquier otra dll (HelixSR...).\n"
      "Solo barra amarilla arriba = no se pudo verificar.\n\n"
      "Las barras las dibuja OptiScaler, no la dll. No se guarda; desactivado al volver a arrancar." },

    // Tabs and sidebar
    { "Custom", "Personal" },
    { "Upscaling", "Escalado" },
    // DLSS indicator of the d4r experiment
    { "Model", "Modelo" },
    { "Model not verified", "Modelo no verificado" },
    { "Not verified", "No verificado" },
    { "The upscaler is failing", "El escalador está fallando" },
    { "Requested", "Solicitado" },
    { "Initialised", "Inicializado" },
    { "Evaluations", "Evaluaciones" },
    { "Native execution", "Ejecución nativa" },
    { "d4r has not written its configuration", "d4r no ha escrito su configuración" },
    { "d4r log not found", "no encuentro el registro de d4r" },
    { "d4r keeps its log locked while the game runs", "d4r mantiene bloqueado su registro mientras el juego está abierto" },
    { "the d4r log is from an earlier session", "el registro de d4r es de una sesión anterior" },
    { "Preset", "Preset" },
    { "requested", "solicitado" },
    { "in the NGX parameters", "en los parámetros NGX" },
    { "set by d4r on each evaluation", "puesto por d4r en cada evaluación" },
    { "presets differ between quality modes", "los presets difieren entre modos de calidad" },
    { "layers bound to native kernels", "capas vinculadas a kernels nativos" },
    { "accepted", "aceptadas" },
    { "rejected", "rechazadas" },
    { "d4r errors", "errores de d4r" },
    { "layers launched native", "capas lanzadas en nativo" },
    { "not native", "no nativas" },
    { "needs Log = verbose in d4r.ini", "requiere Log = verbose en d4r.ini" },
    { "d4r inference", "Inferencia d4r" },
    { "Outputs checked", "Salidas comprobadas" },
    { "d4r imposes model", "d4r impone el modelo" },
    { "These preset controls do not change it.", "Estos controles de preset no lo cambian." },
    { "d4r imposes its own model (d4r.ini). These preset controls do not change it.",
      "d4r impone su propio modelo (d4r.ini). Estos controles de preset no lo cambian." },
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
    { "Default", "Predeterminada" },
    { "Custom path", "Ruta personalizada" },
    { "calculating...", "calculando..." },
    { "Can't be used: %s", "No se puede usar: %s" },
    { "file can't be opened", "no se puede abrir el archivo" },
    { "not a dll", "no es una dll" },
    { "not a 64 bit dll", "no es una dll de 64 bits" },
    { "damaged dll", "dll dañada" },
    { "dll has no exports", "la dll no tiene exportaciones" },
    { "not an FFX upscaler dll (ffx exports missing)",
      "no es una dll de escalado FFX (faltan las exportaciones ffx)" },

    // Notifications of the versions switch

    // Hot-swap diagnostics
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

    // Comparison mode
    { "WAITING", "ESPERANDO" },
    { "STABILIZING", "ESTABILIZANDO" },
    { "RECORDING", "MIDIENDO" },
    { "SAVED", "GUARDADO" },
    { "min --  max --", "mín --  máx --" },
    { "min %.0f  max %.0f", "mín %.0f  máx %.0f" },
    { "min --", "mín --" },
    { "max --", "máx --" },
    { "min %.0f", "mín %.0f" },
    { "max %.0f", "máx %.0f" },
    { "Includes frames presented by frame generation", "Incluye fotogramas presentados por frame generation" },

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
    { "Performance and stability comparison. Images only count when captures were made for these tests.",
      "Comparación de rendimiento y estabilidad. Las imágenes solo cuentan si se hicieron capturas para estas pruebas." },
    { "Captures of these tests", "Capturas de estas pruebas" },
    { "A capture made now is linked to the saved test %s.", "Una captura hecha ahora queda vinculada a la prueba guardada %s." },
    { "A capture made now is not linked to any test. To link it, save a test and capture before clearing its result.",
      "Una captura hecha ahora no queda vinculada a ninguna prueba. Para vincularla, guarda una prueba y captura antes "
      "de limpiar su resultado." },
    { "Off (%s loaded, the game isn't generating frames)", "Apagado (%s cargado, el juego no está generando fotogramas)" },
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
    { "The tests used different upscaling technologies: performance and stability are compared, image "
      "quality is not.",
      "Las pruebas usaron tecnologías de escalado distintas: se comparan rendimiento y estabilidad, no la "
      "calidad de imagen." },
    { "The tests used different models of the same technology.", "Las pruebas usaron modelos distintos de la misma tecnología." },
    { "Technology", "Tecnología" },
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

    // Visual comparison of captures
    { "Compare captures", "Comparar capturas" },
    { "Opens the saved captures. Pick two or more to see them side by side,\n"
      "zoom into the same spot of each and measure them against a reference.",
      "Abre las capturas guardadas. Elige dos o más para verlas lado a lado,\n"
      "ampliar el mismo punto de cada una y medirlas frente a una referencia." },
    { "Visual comparison of captures", "Comparación visual de capturas" },
    { "Still captures against a reference you pick. What happens in motion is not measured.",
      "Capturas fijas frente a una referencia que eliges tú. Lo que ocurre en movimiento no se mide." },
    { "Delete captures", "Eliminar capturas" },
    { "Deletes the selected captures for good: their PNG, their data and their saved measurements.\n"
      "Clear selection only unticks them.",
      "Elimina definitivamente las capturas seleccionadas: su PNG, sus datos y sus mediciones guardadas.\n"
      "Limpiar selección solo las desmarca." },
    { "Delete %d selected captures for good?", "¿Eliminar definitivamente %d capturas seleccionadas?" },
    { "Their files are removed from disk. This can't be undone.",
      "Sus archivos se borran del disco. No se puede deshacer." },
    { "%d captures, %d selected (up to %d)", "%d capturas, %d seleccionadas (máximo %d)" },
    { "No captures yet. Use Capture image in the Benchmark tab, or copy PNG files into ILION\\Captures.",
      "Aún no hay capturas. Usa Capturar imagen en la pestaña Benchmark, o copia archivos PNG en ILION\\Captures." },
    { "Capture", "Captura" },
    { "Type", "Tipo" },
    { "Size", "Tamaño" },
    { "Native", "Nativa" },
    { "Upscaled", "Escalada" },
    { "External", "Externa" },
    { "Select two or more captures to compare them.", "Selecciona dos o más capturas para compararlas." },
    { "Native image: the measurements tell how close each capture is to it.",
      "Imagen nativa: las mediciones indican cuánto se acerca cada captura a ella." },
    { "Not a native image without upscaler: the measurements only tell how much the captures differ.",
      "No es una imagen nativa sin escalador: las mediciones solo indican cuánto difieren las capturas." },
    { "Comparator", "Comparador" },
    { "Side by side", "Lado a lado" },
    { "Difference", "Diferencia" },
    { "The right picture shows where the two captures differ: black is equal,\n"
      "brighter is more different. The amplification makes small differences visible.",
      "La imagen derecha muestra dónde difieren las dos capturas: negro es igual,\n"
      "más brillante es más diferente. La amplificación hace visibles las diferencias pequeñas." },
    { "Fit", "Ajustar" },
    { "Go to the most different area", "Ir a la zona más diferente" },
    { "This PNG can't be read", "No se puede leer este PNG" },
    { "Loading the captures...", "Cargando las capturas..." },
    { "The two captures don't have the same size", "Las dos capturas no tienen el mismo tamaño" },
    { "Zoom %.0f%%. Wheel: zoom, drag: move.", "Zoom %.0f%%. Rueda: zoom, arrastrar: mover." },
    { "Difference with %s", "Diferencia con %s" },
    { "Visual summary", "Resumen visual" },

    // Magnifier of the comparator
    { "Magnifier", "Lupa" },
    { "Shows the same small area of both pictures enlarged, every pixel as a square,\n"
      "nothing smoothed. The captures themselves are not changed.",
      "Muestra ampliada la misma zona pequeña de las dos imágenes, cada píxel como un cuadrado,\n"
      "sin suavizar nada. Las capturas no se modifican." },
    { "Area pinned. Right click on a picture: release it.",
      "Zona fijada. Clic derecho sobre una imagen: soltarla." },
    { "Move the pointer over a picture. Right click: pin the area.",
      "Mueve el puntero sobre una imagen. Clic derecho: fijar la zona." },
    { "Magnifier %dx: %d x %d pixels around %d, %d", "Lupa %dx: %d x %d píxeles alrededor de %d, %d" },
    { "The two captures don't have the same size: the magnifier shows the same "
      "coordinates, which may not be the same part of the picture.",
      "Las dos capturas no tienen el mismo tamaño: la lupa muestra las mismas coordenadas, que pueden no ser la "
      "misma parte de la imagen." },
    { "These captures don't seem to show the same scene: the magnifier may show "
      "different parts in each one.",
      "Estas capturas no parecen mostrar la misma escena: la lupa puede mostrar partes distintas en cada una." },
    { "These captures are displaced %d, %d pixels from each other: the magnifier may "
      "show different parts in each one.",
      "Estas capturas están desplazadas %d, %d píxeles entre sí: la lupa puede mostrar partes distintas en cada "
      "una." },
    { "Whether these two captures are aligned hasn't been measured: the "
      "magnifier shows the same coordinates in both.",
      "No se ha medido si estas dos capturas están alineadas: la lupa muestra las mismas coordenadas en ambas." },
    { "Pixel in the middle: R %d  G %d  B %d", "Píxel central: R %d  G %d  B %d" },
    { "Difference, amplified", "Diferencia, amplificada" },

    // What the measurements mean
    { "Visual interpretation", "Interpretación visual" },
    { "%s against %s", "%s frente a %s" },
    { "It can't be measured against the reference, see above.",
      "No se puede medir frente a la referencia, ver arriba." },
    { "1. Similarity: the two pictures are identical.", "1. Similitud: las dos imágenes son idénticas." },
    { "1. Similarity: very close in structure (SSIM %.4f). A high SSIM is similar "
      "structure, not a better picture.",
      "1. Similitud: muy parecidas en estructura (SSIM %.4f). Un SSIM alto indica estructura parecida, no una "
      "imagen mejor." },
    { "1. Similarity: close, with differences that can be seen (SSIM %.4f).",
      "1. Similitud: parecidas, con diferencias apreciables (SSIM %.4f)." },
    { "1. Similarity: clearly different in structure (SSIM %.4f).",
      "1. Similitud: claramente distintas en estructura (SSIM %.4f)." },
    { "1. Similarity: very different (SSIM %.4f). Check that both show the same scene.",
      "1. Similitud: muy distintas (SSIM %.4f). Comprueba que ambas muestran la misma escena." },
    { "2. Where: concentrated in one area (column %d, row %d, %.1f times the average). The "
      "magnifier shows it pixel by pixel.",
      "2. Dónde: concentradas en una zona (columna %d, fila %d, %.1f veces la media). La lupa permite verla píxel a "
      "píxel." },
    { "2. Where: spread over the picture, no area stands out much.",
      "2. Dónde: repartidas por la imagen, sin que una zona destaque mucho." },
    { "2. Where: no area stands out.", "2. Dónde: ninguna zona destaca." },
    { " Edges %+.1f%% stronger with overshoot next to them (%.1f%%): it may be "
      "over-sharpening or halos, not more fidelity.",
      " Bordes un %+.1f%% más marcados y con sobreimpulso junto a ellos (%.1f%%): puede ser sobreenfoque o halos, "
      "no más fidelidad." },
    { " Edges %+.1f%% stronger: looking sharper is not the same as being more faithful.",
      " Bordes un %+.1f%% más marcados: verse más nítida no es lo mismo que ser más fiel." },
    { " Edges %+.1f%% softer.", " Bordes un %+.1f%% más suaves." },
    { " %.1f%% of the edges of the reference are kept.", " Se conserva el %.1f%% de los bordes de la referencia." },
    { "3. Possible causes: the camera or the scene changed between the captures, which explains "
      "differences by itself.",
      "3. Causas posibles: la cámara o la escena cambiaron entre las capturas, lo que por sí solo explica "
      "diferencias." },
    { "3. Possible causes: %.1f%% of the pixels differ but only %.1f%% clearly. Many small "
      "differences are typical of light, water, vegetation, particles, TAA or what changes "
      "from one frame to the next; they can't be put down to the upscaler.",
      "3. Causas posibles: difiere el %.1f%% de los píxeles, pero solo el %.1f%% con claridad. Muchas diferencias "
      "pequeñas son propias de la iluminación, el agua, la vegetación, las partículas, el TAA o lo que cambia de un "
      "fotograma al siguiente; no pueden atribuirse al escalador." },
    { "3. Possible causes: none, nothing differs.", "3. Causas posibles: ninguna, no hay diferencias." },
    { "3. Possible causes: these captures alone don't separate the upscaler from what changes in "
      "the scene between two moments.",
      "3. Causas posibles: estas capturas por sí solas no separan el escalador de lo que cambia en la escena entre "
      "dos momentos." },
    { " Two captures of one same version differ here by %.4f of SSIM.",
      " Dos capturas de una misma versión difieren aquí en %.4f de SSIM." },
    { " Frame generation was on: the frame may be a generated one.",
      " La generación de fotogramas estaba activa: el fotograma puede ser uno generado." },
    { "the camera moved", "la cámara se movió" },
    { "they don't seem to show the same scene", "no parecen mostrar la misma escena" },
    { "a frame may be a generated one", "un fotograma puede ser generado" },
    { "they are from different games", "son de juegos distintos" },
    { "4. Conclusion: inconclusive (%s).", "4. Conclusión: no concluyente (%s)." },
    { "4. Conclusion: roughly equivalent in these still captures.",
      "4. Conclusión: equivalencia aproximada en estas capturas fijas." },
    { "4. Conclusion: a difference that can be detected in these still captures.",
      "4. Conclusión: diferencia detectable en estas capturas fijas." },
    { " It is measured against a native picture: being closer to it is not by itself "
      "looking better.",
      " Se mide frente a una imagen nativa: parecerse más a ella no es por sí solo verse mejor." },
    { " Without a native reference this doesn't tell which one is more faithful.",
      " Sin una referencia nativa esto no dice cuál es más fiel." },
    { "no native reference", "sin referencia nativa" },
    { "displacement of %d, %d px", "desplazamiento de %d, %d px" },
    { "frame generation on", "generación de fotogramas activa" },
    { "HDR10 values reduced to 8 bit", "valores HDR10 reducidos a 8 bits" },
    { "no second capture of one same version to know how much the picture "
      "changes by itself",
      "falta una segunda captura de una misma versión para saber cuánto cambia la imagen por sí sola" },
    { "still pictures only, motion isn't measured", "solo imágenes fijas, el movimiento no se mide" },
    { "Limits of this comparison: %s.", "Límites de esta comparación: %s." },
    { "Measuring...", "Midiendo..." },
    { "VISUAL RESULT INCONCLUSIVE", "RESULTADO VISUAL NO CONCLUYENTE" },
    { "None of the selected captures can be measured against the reference.",
      "Ninguna de las capturas seleccionadas se puede medir frente a la referencia." },
    { "At least one capture doesn't show the same picture as the reference: the scene or the camera changed "
      "between them. Capture again without moving.",
      "Al menos una captura no muestra la misma imagen que la referencia: la escena o la cámara cambiaron entre "
      "ellas. Vuelve a capturar sin moverte." },
    { "Only captures of the same version as the reference were selected. Their SSIM of %.4f is how much the "
      "picture changes by itself from one capture to the next.",
      "Solo se han seleccionado capturas de la misma versión que la referencia. Su SSIM de %.4f es lo que la "
      "imagen cambia por sí sola de una captura a la siguiente." },
    { "VISUAL DIFFERENCES, without a fidelity verdict", "DIFERENCIAS VISUALES, sin veredicto de fidelidad" },
    { "The reference isn't a native image without upscaler: the values tell how much each capture differs from %s, "
      "not which one is more faithful.",
      "La referencia no es una imagen nativa sin escalador: los valores indican cuánto difiere cada captura de %s, "
      "no cuál es más fiel." },
    { " The differences are no bigger than those between two captures of one same version.",
      " Las diferencias no son mayores que las que hay entre dos capturas de una misma versión." },
    { "%s (average of %d captures)", "%s (media de %d capturas)" },
    { "VISUAL: one version measured", "VISUAL: una sola versión medida" },
    { "%s has an SSIM of %.4f against the native reference. With one version there is nothing to rank.",
      "%s tiene un SSIM de %.4f frente a la referencia nativa. Con una sola versión no hay nada que ordenar." },
    { "CLOSEST TO THE NATIVE REFERENCE: %s", "MÁS CERCANA A LA REFERENCIA NATIVA: %s" },
    { "SSIM of %.4f against %.4f for %s, the margin used is %.4f.",
      "SSIM de %.4f frente a %.4f de %s; el margen usado es %.4f." },
    { "%s and %s are %.4f of SSIM apart, within the margin of %.4f.",
      "%s y %s están a %.4f de SSIM una de otra, dentro del margen de %.4f." },
    { " Without a second capture of one same version, how much the picture changes by itself is unknown.",
      " Sin una segunda captura de una misma versión no se sabe cuánto cambia la imagen por sí sola." },
    { "%s has stronger edges than the reference (%+.1f%%). ",
      "%s tiene bordes más marcados que la referencia (%+.1f%%). " },
    { "%s has softer edges than the reference (%+.1f%%). ",
      "%s tiene bordes más suaves que la referencia (%+.1f%%). " },
    { "Its overshoot next to edges (%.1f%%) may be over-sharpening or halos. ",
      "Su sobreimpulso junto a los bordes (%.1f%%) puede ser sobreenfoque o halos. " },
    { "EDGES AND SHARPNESS (exploratory)", "BORDES Y NITIDEZ (exploratorio)" },
    { "%s: %dx%d against the %dx%d of the reference, it can't be measured.",
      "%s: %dx%d frente a los %dx%d de la referencia, no se puede medir." },
    { "%s: one is HDR10 and the other isn't, it can't be measured.",
      "%s: una es HDR10 y la otra no, no se puede medir." },
    { "%s: the picture can't be read or is too small.", "%s: la imagen no se puede leer o es demasiado pequeña." },
    { "%s: doesn't seem to show the same scene as the reference.",
      "%s: no parece mostrar la misma escena que la referencia." },
    { "%s: displaced %d, %d pixels from the reference, the camera moved between captures.",
      "%s: desplazada %d, %d píxeles respecto a la referencia; la cámara se movió entre capturas." },
    { "The captures are from different games.", "Las capturas son de juegos distintos." },
    { "Frame generation was on in at least one capture, its frame may be a generated one.",
      "Frame generation estaba activo en al menos una captura; su fotograma puede ser uno generado." },
    { "HDR10 captures are shown and measured over their PQ values reduced to 8 bit.",
      "Las capturas HDR10 se muestran y se miden sobre sus valores PQ reducidos a 8 bits." },
    { "Measurements against the reference", "Mediciones frente a la referencia" },
    { "SSIM (structure)", "SSIM (estructura)" },
    { "Structural similarity of the brightness, in windows of 8x8 pixels.\n"
      "1 is the same structure as the reference.\n"
      "A high value is similar structure, not a better picture.",
      "Similitud estructural del brillo, en ventanas de 8x8 píxeles.\n"
      "1 es la misma estructura que la referencia.\n"
      "Un valor alto indica estructura parecida, no una imagen mejor." },
    { "Signal to noise ratio of the brightness against the reference, higher is closer.\n"
      "It is a difference of signal, not how good the picture looks.",
      "Relación señal/ruido del brillo frente a la referencia; más alto es más parecido.\n"
      "Expresa diferencia de señal, no lo bien que se ve la imagen." },
    { "identical", "idéntica" },
    { "Mean difference", "Diferencia media" },
    { "Mean difference of the brightness, in levels out of 255.", "Diferencia media del brillo, en niveles sobre 255." },
    { "Pixels that differ", "Píxeles que difieren" },
    { "Pixels with a color channel more than 2 levels out of 255 apart.",
      "Píxeles con algún canal de color a más de 2 niveles sobre 255." },
    { "Pixels that clearly differ", "Píxeles que difieren claramente" },
    { "Pixels with a color channel more than 16 levels out of 255 apart.",
      "Píxeles con algún canal de color a más de 16 niveles sobre 255." },
    { "Edge strength", "Intensidad de bordes" },
    { "Mean step of brightness between neighbouring pixels, against the one of the reference.\n"
      "Higher is a sharper or noisier picture, lower is a softer one.",
      "Salto medio de brillo entre píxeles vecinos, frente al de la referencia.\n"
      "Más alto es una imagen más nítida o con más ruido; más bajo, una más suave." },
    { "Edges of the reference kept", "Bordes de la referencia conservados" },
    { "Edges of the reference that are still at least half as strong in the capture.",
      "Bordes de la referencia que en la captura conservan al menos la mitad de su intensidad." },
    { "Overshoot next to edges (exploratory)", "Sobreimpulso junto a bordes (exploratorio)" },
    { "Pixels next to an edge that are brighter or darker than everything around them in the reference.\n"
      "Halos and the ringing of sharpening read like this, but so does fine detail the reference lacks.",
      "Píxeles junto a un borde más claros o más oscuros que todo lo que los rodea en la referencia.\n"
      "Los halos y el anillado del enfoque dan esta lectura, pero también el detalle fino que la referencia no tiene." },
    { "Most different area", "Zona más diferente" },
    { "Where the mean difference is biggest, in a grid of 6 columns and 4 rows.",
      "Dónde es mayor la diferencia media, en una cuadrícula de 6 columnas y 4 filas." },
    { "column %d, row %d (%.2f)", "columna %d, fila %d (%.2f)" },
    { "Displacement", "Desplazamiento" },
    { "Movement of the whole picture that matches the reference best. It should be none.",
      "Movimiento de toda la imagen que mejor encaja con la referencia. Debería ser ninguno." },
    { "Margin used: %.4f of SSIM, or the difference between captures of one same version when it is bigger. "
      "No overall score is given.",
      "Margen usado: %.4f de SSIM, o la diferencia entre capturas de una misma versión cuando es mayor. "
      "No se da ninguna puntuación global." },

    // Visual category of the benchmark comparison
    { "VISUAL QUALITY: not analyzed", "CALIDAD VISUAL: sin analizar" },
    { "No saved visual analysis against a native capture covers these versions. Capture them and a native image "
      "in the same spot, then compare them in Visual analysis.",
      "Ningún análisis visual guardado frente a una captura nativa cubre estas versiones. Captúralas junto con una "
      "imagen nativa en el mismo punto y compáralas en Análisis visual." },
    { "VISUAL QUALITY: one version analyzed", "CALIDAD VISUAL: una sola versión analizada" },
    { "%s has an SSIM of %.4f against the native capture %s. The other versions have no analysis against that "
      "capture.",
      "%s tiene un SSIM de %.4f frente a la captura nativa %s. Las demás versiones no tienen análisis frente a esa "
      "captura." },
    { "VISUALLY CLOSEST TO NATIVE: %s", "VISUALMENTE MÁS CERCANA A NATIVA: %s" },
    { "SSIM of %.4f against %.4f for %s, from still captures measured against %s.",
      "SSIM de %.4f frente a %.4f de %s, a partir de capturas fijas medidas frente a %s." },
    { " It is also the performance winner.", " También es la ganadora en rendimiento." },
    { " %s is faster: which one suits better depends on the priority.",
      " %s es más rápida: cuál conviene más depende de la prioridad." },
    { "VISUAL QUALITY: no clear difference", "CALIDAD VISUAL: sin diferencia clara" },
    { "%s and %s are %.4f of SSIM apart against the native capture %s, within the margin of %.4f.",
      "%s y %s están a %.4f de SSIM una de otra frente a la captura nativa %s, dentro del margen de %.4f." },
    { " Not every version has an analysis against that capture.",
      " No todas las versiones tienen un análisis frente a esa captura." },
    { " ILION can't check that the captures are from the scene of the tests.",
      " ILION no puede comprobar que las capturas sean de la escena de las pruebas." },

    // Captures of the compared DLLs against each other
    { " Without a saved capture: %s.", " Sin captura guardada: %s." },
    { "VISUAL QUALITY: captures missing", "CALIDAD VISUAL: faltan capturas" },
    { "A capture of at least two of these DLLs, taken in the same spot with the configuration of the tests, is "
      "needed to compare their pictures.",
      "Para comparar sus imágenes hace falta una captura de al menos dos de estas DLL, tomada en el mismo punto y "
      "con la configuración de las pruebas." },
    { "%d pairs of captures are being measured.", "Se están midiendo %d parejas de capturas." },
    { " Not comparable, another scene, camera moved or another size: %s.",
      " No comparables (otra escena, cámara movida u otro tamaño): %s." },
    { "VISUAL QUALITY: captures not equivalent", "CALIDAD VISUAL: capturas no equivalentes" },
    { "The saved captures of these DLLs don't show the same picture, they can't be compared. Capture every DLL in "
      "the same spot without moving.",
      "Las capturas guardadas de estas DLL no muestran la misma imagen y no se pueden comparar. Captura cada DLL en "
      "el mismo punto sin moverte." },
    { "%s / %s: SSIM %.4f, %.1f%% of the pixels differ. ", "%s / %s: SSIM %.4f, difiere el %.1f%% de los píxeles. " },
    { "Most alike: ", "Más parecidas: " },
    { "Most different: ", "Más diferentes: " },
    { "Strongest edges: %s, %.1f%% more than %s. ", "Bordes más marcados: %s, un %.1f%% más que %s. " },
    { "These are differences between the pictures: without a native reference they don't show which DLL looks "
      "better.",
      "Son diferencias entre las imágenes: sin una referencia nativa no demuestran qué DLL se ve mejor." },
    { "VISUAL DIFFERENCES between the captures of the DLLs", "DIFERENCIAS VISUALES entre las capturas de las DLL" },
    { " The captures differ between the DLLs but, without a native reference, none is shown to look better: visual "
      "quality doesn't change this result.",
      " Las capturas difieren entre las DLL pero, sin referencia nativa, no se demuestra que ninguna se vea mejor: "
      "la calidad visual no cambia este resultado." },
    { " There are no comparable captures of these DLLs, visual quality isn't part of this result.",
      " No hay capturas comparables de estas DLL; la calidad visual no forma parte de este resultado." },
    { " The captures of these DLLs are still being measured.", " Las capturas de estas DLL aún se están midiendo." },
    { "GLOBAL CONCLUSION: %s", "CONCLUSIÓN GLOBAL: %s" },
    { "Best balance of performance and stability, and the closest to the native capture.",
      "Mejor equilibrio de rendimiento y estabilidad, y la más cercana a la captura nativa." },
    { "GLOBAL CONCLUSION: depends on the priority", "CONCLUSIÓN GLOBAL: depende de la prioridad" },
    { "%s has the best balance of performance and stability, %s is the closest to the native capture.",
      "%s tiene el mejor equilibrio de rendimiento y estabilidad; %s es la más cercana a la captura nativa." },
    { "Best balance of performance and stability.", "Mejor equilibrio de rendimiento y estabilidad." },
    { "GLOBAL CONCLUSION: no overall winner", "CONCLUSIÓN GLOBAL: sin ganadora global" },
    { "Performance and stability don't single out a DLL. %s is the closest to the native capture.",
      "El rendimiento y la estabilidad no destacan a ninguna DLL. %s es la más cercana a la captura nativa." },
    { "Performance and stability don't single out a DLL.", "El rendimiento y la estabilidad no destacan a ninguna DLL." },

    // Integrated final report
    { "Final report", "Informe final" },
    { "Integrated final report: performance, stability and visual quality of every DLL\n"
      "that has saved tests, with one conclusion.",
      "Informe final integrado: rendimiento, estabilidad y calidad visual de cada DLL\n"
      "que tenga pruebas guardadas, con una conclusión." },
    { "Integrated final report", "Informe final integrado" },
    { "Saved benchmarks and captures of every DLL, joined by the DLL and the configuration of the test.",
      "Benchmarks y capturas guardados de cada DLL, unidos por la DLL y la configuración de la prueba." },
    { "%d saved tests, %d captures", "%d pruebas guardadas, %d capturas" },
    { "Configuration", "Configuración" },
    { "Game, render and output resolution, upscaler and frame generation.\n"
      "Only tests made under the same configuration are compared.",
      "Juego, resolución de render y de salida, escalador y frame generation.\n"
      "Solo se comparan pruebas hechas con la misma configuración." },
    { "no upscaling", "sin escalado" },
    { "tests", "pruebas" },
    { "This configuration has one saved test. Save tests of two or more DLLs made under the same configuration to "
      "get a report.",
      "Esta configuración tiene una sola prueba guardada. Guarda pruebas de dos o más DLL hechas con la misma "
      "configuración para obtener un informe." },
    { "Native reference", "Referencia nativa" },
    { "Capture of the game at native resolution without upscaler.\n"
      "The captures of every DLL are measured against it. Use the one of the same spot.",
      "Captura del juego a resolución nativa sin escalador.\n"
      "Las capturas de cada DLL se miden frente a ella. Usa la del mismo punto." },
    { "VISUAL QUALITY: measuring", "CALIDAD VISUAL: midiendo" },
    { "%d captures are being measured against the native reference.",
      "Se están midiendo %d capturas frente a la referencia nativa." },
    { "VISUAL QUALITY: no native reference", "CALIDAD VISUAL: sin referencia nativa" },
    { "There is no native capture without upscaler of this game at this output size. Without one, how close each "
      "DLL is to native can't be measured.",
      "No hay ninguna captura nativa sin escalador de este juego con este tamaño de salida. Sin ella no se puede "
      "medir cuánto se acerca cada DLL a la imagen nativa." },
    { "VISUAL QUALITY: no captures measured", "CALIDAD VISUAL: sin capturas medidas" },
    { "None of these DLLs has a capture of this configuration with a reliable measurement against the native "
      "reference.",
      "Ninguna de estas DLL tiene una captura de esta configuración con una medición fiable frente a la referencia "
      "nativa." },
    { "VISUAL QUALITY: incomplete", "CALIDAD VISUAL: incompleta" },
    { "Without a reliable capture: %s. The values of the others are in the table.",
      "Sin captura fiable: %s. Los valores de las demás están en la tabla." },
    { "%s are within %.4f of SSIM of each other, the margin used here.",
      "%s están a menos de %.4f de SSIM entre sí, el margen usado aquí." },
    { "INTEGRATED RESULT: not enough data", "RESULTADO INTEGRADO: datos insuficientes" },
    { "Performance and stability can't be compared between these tests, no overall winner is named.",
      "El rendimiento y la estabilidad no se pueden comparar entre estas pruebas; no se declara ganadora global." },
    { "Visual quality isn't measured for every DLL, so no overall winner is named.",
      "La calidad visual no está medida para todas las DLL, así que no se declara ganadora global." },
    { " In performance and stability alone, %s has the best balance.",
      " Solo en rendimiento y estabilidad, %s tiene el mejor equilibrio." },
    { "INTEGRATED RESULT: no overall winner", "RESULTADO INTEGRADO: sin ganadora global" },
    { "Performance: %.1f average FPS, %.1f%% more than %s. ",
      "Rendimiento: %.1f FPS promedio, un %.1f%% más que %s. " },
    { "Performance: among the fastest, within %.1f%% of average FPS. ",
      "Rendimiento: entre las más rápidas, a menos de un %.1f%% en FPS promedio. " },
    { "Stability: frame time variation of %.1f%% against %.1f%% for %s. ",
      "Estabilidad: variación de frametime del %.1f%% frente al %.1f%% de %s. " },
    { "Stability: inconclusive between the tests. ", "Estabilidad: no concluyente entre las pruebas. " },
    { "Stability: among the steadiest, without a clear difference. ",
      "Estabilidad: entre las más estables, sin diferencia clara. " },
    { "Visual quality: SSIM of %.4f against native, %.4f for %s.",
      "Calidad visual: SSIM de %.4f frente a nativa, %.4f para %s." },
    { "Visual quality: SSIM of %.4f against native, among the closest (margin %.4f).",
      "Calidad visual: SSIM de %.4f frente a nativa, entre las más cercanas (margen %.4f)." },
    { "BEST INTEGRATED BALANCE: %s", "MEJOR EQUILIBRIO INTEGRADO: %s" },
    { "%s are among the best in performance, stability and visual quality. No difference bigger than the margins "
      "separates them.",
      "%s están entre las mejores en rendimiento, estabilidad y calidad visual. Ninguna diferencia mayor que los "
      "márgenes las separa." },
    { "INTEGRATED RESULT: depends on the priority", "RESULTADO INTEGRADO: depende de la prioridad" },
    { "Fastest: %s. Steadiest: %s. Closest to native: %s. No DLL is among the best of the three categories.",
      "Más rápida: %s. Más estable: %s. Más cercana a nativa: %s. Ninguna DLL está entre las mejores de las tres "
      "categorías." },
    { "inconclusive", "no concluyente" },
    { "Margins used: %.1f%% in average FPS, %.1f%% in 1%% low, %.4f in SSIM. No score mixes the categories.",
      "Márgenes usados: %.1f%% en FPS promedio, %.1f%% en 1%% low, %.4f en SSIM. Ninguna puntuación mezcla las "
      "categorías." },
    { "ILION can't check that the captures are from the scene of the tests.",
      "ILION no puede comprobar que las capturas sean de la escena de las pruebas." },
    { "Results by DLL", "Resultados por DLL" },
    { "Differences against", "Diferencias respecto a" },
    { "Performance", "Rendimiento" },
    { "Stability and frame times", "Estabilidad y frametimes" },
    { "Visual quality against the native reference", "Calidad visual frente a la referencia nativa" },
    { "Data used", "Datos utilizados" },
    { "Saved tests", "Pruebas guardadas" },
    { "Captures of this configuration", "Capturas de esta configuración" },
    { "Captures measured", "Capturas medidas" },
    { "Left out: another scene or camera moved", "Descartadas: otra escena o cámara movida" },
    { "Left out: can't be measured", "Descartadas: no se pueden medir" },
    { "Values of a DLL with several tests or captures are their mean. Visual values come from still captures, what "
      "happens in motion is not measured.",
      "Los valores de una DLL con varias pruebas o capturas son su media. Los valores visuales proceden de capturas "
      "fijas; lo que ocurre en movimiento no se mide." },

    // FSR version selector of the Benchmark tab
    { "Rescan", "Actualizar lista" },
    { "Switching...", "Cambiando..." },
    { "The DLL that executes is not the selected one", "La DLL que se ejecuta no es la seleccionada" },
    { "Active", "Activa" },

    // Vertical overlay
    { "PERFORMANCE", "RENDIMIENTO" },
    { "PROCESSING TIMES", "TIEMPOS DE PROCESO" },
    { "Frame time", "Frametime" },
    { "FPS, average", "FPS promedio" },
    { "Frame time, average", "Frametime promedio" },
    { "Upscaler, average", "Escalador, promedio" },
    { "Whole frame", "Fotograma completo" },

    // Theme
    { "Theme", "Tema" },
    { "Classic", "Clásico" },
    { "ILION (original)", "ILION (original)" },
    { "Fallout", "Fallout" },
    { "Light", "Claro" },
    { "ILION (original), Fallout, Light and Fallout 2: the instrument panel look of ILION in four styles.\n"
      "Classic: the original look, with the colour settings below.",
      "ILION (original), Fallout, Claro y Fallout 2: el aspecto de panel de instrumentación de ILION en cuatro estilos.\n"
      "Clásico: el aspecto original, con los ajustes de color de abajo." },

    // Overlay tab and extended readings
    { "Overlay", "Overlay" },
    { "Real-time metrics over the game: FPS, frame times and hardware sensors.",
      "Métricas en tiempo real sobre el juego: FPS, tiempos de fotograma y sensores de hardware." },
    { "Menu scale, theme and keybinds.", "Escala del menú, tema y atajos." },
    { "GPU power", "Consumo de GPU" },
    { "CPU power", "Consumo de CPU" },
    { "RAM used", "RAM usada" },
    { "Power", "Consumo" },
    { "1% low", "1% bajo" },
    { "1% low (last 300 frames)", "1% bajo (últimos 300 fotogramas)" },
    { "FPS of the slowest 1% of the frames the overlay keeps. Shown in the ILION layouts.",
      "FPS del 1% más lento de los fotogramas que guarda el overlay. Se muestra en los diseños de ILION." },
    { "Native AA", "AA nativo" },
    { "Ultra Quality", "Ultra calidad" },
    { "Quality", "Calidad" },
    { "Balanced", "Equilibrado" },
    { "Ultra Performance", "Ultra rendimiento" },

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
