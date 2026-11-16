#include "misc/IlionGpuTuning.h"
#include <map>
#include <filesystem>

namespace IlionShell
{
// Resolve messages when drawn so a language change also updates messages already displayed.
static std::string AmdGpuMessage(std::string_view source)
{
    struct Entry { const char* es; const char* en; const char* pt; const char* ru; };
    static constexpr Entry messages[] = {
        {"Captura interrumpida al salir de AJUSTE GPU. Se conserva la referencia anterior.", "Capture stopped when you left GPU Tuning. The previous baseline was kept.", "A captura foi interrompida ao sair do ajuste da GPU. A refer\303\252ncia anterior foi mantida.", "\320\227\320\260\320\277\320\270\321\201\321\214 \320\276\321\201\321\202\320\260\320\275\320\276\320\262\320\273\320\265\320\275\320\260 \320\277\321\200\320\270 \320\262\321\213\321\205\320\276\320\264\320\265 \320\270\320\267 \320\275\320\260\321\201\321\202\321\200\320\276\320\265\320\272 GPU. \320\237\321\200\320\265\320\266\320\275\320\270\320\265 \320\261\320\260\320\267\320\276\320\262\321\213\320\265 \320\264\320\260\320\275\320\275\321\213\320\265 \321\201\320\276\321\205\321\200\320\260\320\275\320\265\320\275\321\213."},
        {"Referencia capturada durante 30 segundos.", "Baseline captured over 30 seconds.", "Refer\303\252ncia capturada durante 30 segundos.", "\320\221\320\260\320\267\320\276\320\262\321\213\320\265 \320\264\320\260\320\275\320\275\321\213\320\265 \320\267\320\260\320\277\320\270\321\201\320\260\320\275\321\213 \320\267\320\260 30 \321\201\320\265\320\272\321\203\320\275\320\264."},
        {"La captura no obtuvo m\303\251tricas disponibles.", "No metrics were available during capture.", "Nenhuma m\303\251trica estava dispon\303\255vel durante a captura.", "\320\222\320\276 \320\262\321\200\320\265\320\274\321\217 \320\267\320\260\320\277\320\270\321\201\320\270 \320\275\320\265 \320\261\321\213\320\273\320\276 \320\264\320\276\321\201\321\202\321\203\320\277\320\275\321\213\321\205 \320\277\320\276\320\272\320\260\320\267\320\260\321\202\320\265\320\273\320\265\320\271."},
        {"Consulta en curso. Vuelve a intentarlo.", "A request is in progress. Try again.", "Uma consulta est\303\241 em andamento. Tente novamente.", "\320\222\321\213\320\277\320\276\320\273\320\275\321\217\320\265\321\202\321\201\321\217 \320\267\320\260\320\277\321\200\320\276\321\201. \320\237\320\276\320\262\321\202\320\276\321\200\320\270\321\202\320\265 \320\277\320\276\320\277\321\213\321\202\320\272\321\203."},
        {"Punto fuera del rango o paso admitido. Se conserva el valor anterior.", "The point is outside the supported range or step. The previous value was kept.", "O ponto est\303\241 fora da faixa ou do passo permitido. O valor anterior foi mantido.", "\320\242\320\276\321\207\320\272\320\260 \320\262\320\275\320\265 \320\264\320\276\320\277\321\203\321\201\321\202\320\270\320\274\320\276\320\263\320\276 \320\264\320\270\320\260\320\277\320\260\320\267\320\276\320\275\320\260 \320\270\320\273\320\270 \321\210\320\260\320\263\320\260. \320\237\321\200\320\265\320\266\320\275\320\265\320\265 \320\267\320\275\320\260\321\207\320\265\320\275\320\270\320\265 \321\201\320\276\321\205\321\200\320\260\320\275\320\265\320\275\320\276."},
        {"Hay un autoajuste activo. Desact\303\255valo antes de editar.", "Automatic tuning is active. Disable it before editing.", "O ajuste autom\303\241tico est\303\241 ativo. Desative-o antes de editar.", "\320\220\320\272\321\202\320\270\320\262\320\275\320\260 \320\260\320\262\321\202\320\276\320\275\320\260\321\201\321\202\321\200\320\276\320\271\320\272\320\260. \320\236\321\202\320\272\320\273\321\216\321\207\320\270\321\202\320\265 \320\265\321\221 \320\277\320\265\321\200\320\265\320\264 \321\200\320\265\320\264\320\260\320\272\321\202\320\270\321\200\320\276\320\262\320\260\320\275\320\270\320\265\320\274."},
        {"Un ajuste no est\303\241 disponible o est\303\241 fuera del rango o paso admitido.", "A setting is unavailable or outside the supported range or step.", "Um ajuste est\303\241 indispon\303\255vel ou fora da faixa ou do passo permitido.", "\320\237\320\260\321\200\320\260\320\274\320\265\321\202\321\200 \320\275\320\265\320\264\320\276\321\201\321\202\321\203\320\277\320\265\320\275 \320\270\320\273\320\270 \320\262\321\213\321\205\320\276\320\264\320\270\321\202 \320\267\320\260 \320\277\321\200\320\265\320\264\320\265\320\273\321\213 \320\264\320\276\320\277\321\203\321\201\321\202\320\270\320\274\320\276\320\263\320\276 \320\264\320\270\320\260\320\277\320\260\320\267\320\276\320\275\320\260 \320\273\320\270\320\261\320\276 \321\210\320\260\320\263\320\260."},
        {"Las frecuencias m\303\255nima y m\303\241xima no admiten una secuencia v\303\241lida.", "The minimum and maximum clocks cannot be set in a valid order.", "As frequ\303\252ncias m\303\255nima e m\303\241xima n\303\243o podem ser aplicadas em uma ordem v\303\241lida.", "\320\234\320\270\320\275\320\270\320\274\320\260\320\273\321\214\320\275\321\203\321\216 \320\270 \320\274\320\260\320\272\321\201\320\270\320\274\320\260\320\273\321\214\320\275\321\203\321\216 \321\207\320\260\321\201\321\202\320\276\321\202\321\213 \320\275\320\265\320\273\321\214\320\267\321\217 \320\267\320\260\320\264\320\260\321\202\321\214 \320\262 \320\264\320\276\320\277\321\203\321\201\321\202\320\270\320\274\320\276\320\274 \320\277\320\276\321\200\321\217\320\264\320\272\320\265."},
        {"Zero RPM no est\303\241 disponible.", "Zero RPM is unavailable.", "Zero RPM est\303\241 indispon\303\255vel.", "\320\240\320\265\320\266\320\270\320\274 Zero RPM \320\275\320\265\320\264\320\276\321\201\321\202\321\203\320\277\320\265\320\275."},
        {"ADLX no ofrece una lista de escritura compatible con la curva actual.", "ADLX provides no writable list compatible with the current curve.", "O ADLX n\303\243o oferece uma lista grav\303\241vel compat\303\255vel com a curva atual.", "ADLX \320\275\320\265 \320\277\321\200\320\265\320\264\320\276\321\201\321\202\320\260\320\262\320\273\321\217\320\265\321\202 \320\264\320\276\321\201\321\202\321\203\320\277\320\275\321\213\320\271 \320\264\320\273\321\217 \320\267\320\260\320\277\320\270\321\201\320\270 \321\201\320\277\320\270\321\201\320\276\320\272, \321\201\320\276\320\262\320\274\320\265\321\201\321\202\320\270\320\274\321\213\320\271 \321\201 \321\202\320\265\320\272\321\203\321\211\320\265\320\271 \320\272\321\200\320\270\320\262\320\276\320\271."},
        {"Temperatura o velocidad fuera del rango o paso admitido.", "Temperature or speed is outside the supported range or step.", "Temperatura ou velocidade fora da faixa ou do passo permitido.", "\320\242\320\265\320\274\320\277\320\265\321\200\320\260\321\202\321\203\321\200\320\260 \320\270\320\273\320\270 \321\201\320\272\320\276\321\200\320\276\321\201\321\202\321\214 \320\262\320\275\320\265 \320\264\320\276\320\277\321\203\321\201\321\202\320\270\320\274\320\276\320\263\320\276 \320\264\320\270\320\260\320\277\320\260\320\267\320\276\320\275\320\260 \320\273\320\270\320\261\320\276 \321\210\320\260\320\263\320\260."},
        {"AMD ADLX no est\303\241 disponible en el controlador instalado.", "AMD ADLX is unavailable in the installed driver.", "O AMD ADLX n\303\243o est\303\241 dispon\303\255vel no driver instalado.", "AMD ADLX \320\275\320\265\320\264\320\276\321\201\321\202\321\203\320\277\320\265\320\275 \320\262 \321\203\321\201\321\202\320\260\320\275\320\276\320\262\320\273\320\265\320\275\320\275\320\276\320\274 \320\264\321\200\320\260\320\271\320\262\320\265\321\200\320\265."},
        {"Error al inicializar AMD ADLX (", "Could not initialize AMD ADLX (", "Falha ao inicializar o AMD ADLX (", "\320\235\320\265 \321\203\320\264\320\260\320\273\320\276\321\201\321\214 \320\270\320\275\320\270\321\206\320\270\320\260\320\273\320\270\320\267\320\270\321\200\320\276\320\262\320\260\321\202\321\214 AMD ADLX ("},
        {"El controlador ya tiene estos valores.", "The driver already has these values.", "O driver j\303\241 usa esses valores.", "\320\255\321\202\320\270 \320\267\320\275\320\260\321\207\320\265\320\275\320\270\321\217 \321\203\320\266\320\265 \320\267\320\260\320\264\320\260\320\275\321\213 \320\262 \320\264\321\200\320\260\320\271\320\262\320\265\321\200\320\265."},
        {"El controlador rechaz\303\263 la curva (", "The driver rejected the curve (", "O driver rejeitou a curva (", "\320\224\321\200\320\260\320\271\320\262\320\265\321\200 \320\276\321\202\320\272\320\273\320\276\320\275\320\270\320\273 \320\272\321\200\320\270\320\262\321\203\321\216 ("},
        {"Ajustes comprobados mediante lectura del controlador.", "Settings verified by reading them back from the driver.", "Ajustes verificados por leitura do driver.", "\320\235\320\260\321\201\321\202\321\200\320\276\320\271\320\272\320\270 \320\277\321\200\320\276\320\262\320\265\321\200\320\265\320\275\321\213 \320\277\320\276\320\262\321\202\320\276\321\200\320\275\321\213\320\274 \321\207\321\202\320\265\320\275\320\270\320\265\320\274 \320\270\320\267 \320\264\321\200\320\260\320\271\320\262\320\265\321\200\320\260."},
        {"Valores sin confirmar:", "Unverified values:", "Valores n\303\243o confirmados:", "\320\235\320\265\320\277\320\276\320\264\321\202\320\262\320\265\321\200\320\266\320\264\321\221\320\275\320\275\321\213\320\265 \320\267\320\275\320\260\321\207\320\265\320\275\320\270\321\217:"},
        {" rechazado (ADLX ", " rejected (ADLX ", " rejeitado (ADLX ", " \320\276\321\202\320\272\320\273\320\276\320\275\320\265\320\275\320\276 (ADLX "},
        {" El controlador solicita restaurar el ajuste global antes de editar.", " The driver requires a global reset before editing.", " O driver exige restaurar os ajustes globais antes de editar.", " \320\237\320\265\321\200\320\265\320\264 \321\200\320\265\320\264\320\260\320\272\321\202\320\270\321\200\320\276\320\262\320\260\320\275\320\270\320\265\320\274 \320\264\321\200\320\260\320\271\320\262\320\265\321\200 \321\202\321\200\320\265\320\261\321\203\320\265\321\202 \320\276\320\261\321\211\320\265\320\263\320\276 \321\201\320\261\321\200\320\276\321\201\320\260 \320\275\320\260\321\201\321\202\321\200\320\276\320\265\320\272."},
        {" Se verificaron los valores anteriores.", " The previous values were verified.", " Os valores anteriores foram verificados.", " \320\237\321\200\320\265\320\266\320\275\320\270\320\265 \320\267\320\275\320\260\321\207\320\265\320\275\320\270\321\217 \320\277\321\200\320\276\320\262\320\265\321\200\320\265\320\275\321\213."},
        {" Sin recuperar:", " Could not recover:", " N\303\243o recuperado:", " \320\235\320\265 \321\203\320\264\320\260\320\273\320\276\321\201\321\214 \320\262\320\276\321\201\321\201\321\202\320\260\320\275\320\276\320\262\320\270\321\202\321\214:"},
        {"Usa de 1 a 40 letras, n\303\272meros, guiones o guiones bajos para el perfil.", "Use 1\342\200\22340 ASCII letters, digits, hyphens or underscores for the profile.", "Use de 1 a 40 letras ASCII, n\303\272meros, h\303\255fens ou sublinhados para o perfil.", "\320\230\320\274\321\217 \320\277\321\200\320\276\321\204\320\270\320\273\321\217: \320\276\321\202 1 \320\264\320\276 40 \320\273\320\260\321\202\320\270\320\275\321\201\320\272\320\270\321\205 \320\261\321\203\320\272\320\262, \321\206\320\270\321\204\321\200, \320\264\320\265\321\204\320\270\321\201\320\276\320\262 \320\270\320\273\320\270 \320\277\320\276\320\264\321\207\321\221\321\200\320\272\320\270\320\262\320\260\320\275\320\270\320\271."},
        {"No se pudo localizar LocalAppData.", "LocalAppData could not be found.", "N\303\243o foi poss\303\255vel localizar LocalAppData.", "\320\235\320\265 \321\203\320\264\320\260\320\273\320\276\321\201\321\214 \320\275\320\260\320\271\321\202\320\270 LocalAppData."},
        {"No se pudo leer el perfil de GPU.", "Could not read the GPU profile.", "N\303\243o foi poss\303\255vel ler o perfil da GPU.", "\320\235\320\265 \321\203\320\264\320\260\320\273\320\276\321\201\321\214 \320\277\321\200\320\276\321\207\320\270\321\202\320\260\321\202\321\214 \320\277\321\200\320\276\321\204\320\270\320\273\321\214 GPU."},
        {"El perfil no corresponde a esta GPU o versi\303\263n.", "The profile does not match this GPU or version.", "O perfil n\303\243o corresponde a esta GPU ou vers\303\243o.", "\320\237\321\200\320\276\321\204\320\270\320\273\321\214 \320\275\320\265 \321\201\320\276\320\276\321\202\320\262\320\265\321\202\321\201\321\202\320\262\321\203\320\265\321\202 \321\215\321\202\320\276\320\271 GPU \320\270\320\273\320\270 \320\262\320\265\321\200\321\201\320\270\320\270."},
        {"No se pudo renombrar el perfil. Comprueba que el nombre est\303\251 libre.", "Could not rename the profile. Check that the name is available.", "N\303\243o foi poss\303\255vel renomear o perfil. Verifique se o nome est\303\241 livre.", "\320\235\320\265 \321\203\320\264\320\260\320\273\320\276\321\201\321\214 \320\277\320\265\321\200\320\265\320\270\320\274\320\265\320\275\320\276\320\262\320\260\321\202\321\214 \320\277\321\200\320\276\321\204\320\270\320\273\321\214. \320\243\320\261\320\265\320\264\320\270\321\202\320\265\321\201\321\214, \321\207\321\202\320\276 \320\270\320\274\321\217 \321\201\320\262\320\276\320\261\320\276\320\264\320\275\320\276."},
        {"Perfil renombrado.", "Profile renamed.", "Perfil renomeado.", "\320\237\321\200\320\276\321\204\320\270\320\273\321\214 \320\277\320\265\321\200\320\265\320\270\320\274\320\265\320\275\320\276\320\262\320\260\320\275."},
        {"No se pudo eliminar el perfil.", "Could not delete the profile.", "N\303\243o foi poss\303\255vel excluir o perfil.", "\320\235\320\265 \321\203\320\264\320\260\320\273\320\276\321\201\321\214 \321\203\320\264\320\260\320\273\320\270\321\202\321\214 \320\277\321\200\320\276\321\204\320\270\320\273\321\214."},
        {"Perfil eliminado.", "Profile deleted.", "Perfil exclu\303\255do.", "\320\237\321\200\320\276\321\204\320\270\320\273\321\214 \321\203\320\264\320\260\320\273\321\221\320\275."},
        {"Ya existe un perfil con ese nombre.", "A profile already uses that name.", "J\303\241 existe um perfil com esse nome.", "\320\237\321\200\320\276\321\204\320\270\320\273\321\214 \321\201 \321\202\320\260\320\272\320\270\320\274 \320\270\320\274\320\265\320\275\320\265\320\274 \321\203\320\266\320\265 \321\201\321\203\321\211\320\265\321\201\321\202\320\262\321\203\320\265\321\202."},
        {"No se pudo guardar el perfil.", "Could not save the profile.", "N\303\243o foi poss\303\255vel salvar o perfil.", "\320\235\320\265 \321\203\320\264\320\260\320\273\320\276\321\201\321\214 \321\201\320\276\321\205\321\200\320\260\320\275\320\270\321\202\321\214 \320\277\321\200\320\276\321\204\320\270\320\273\321\214."},
        {"Perfil guardado.", "Profile saved.", "Perfil salvo.", "\320\237\321\200\320\276\321\204\320\270\320\273\321\214 \321\201\320\276\321\205\321\200\320\260\320\275\321\221\320\275."},
        {"Tipo de perfil no admitido.", "Unsupported profile type.", "Tipo de perfil incompat\303\255vel.", "\320\242\320\270\320\277 \320\277\321\200\320\276\321\204\320\270\320\273\321\217 \320\275\320\265 \320\277\320\276\320\264\320\264\320\265\321\200\320\266\320\270\320\262\320\260\320\265\321\202\321\201\321\217."},
        {"El perfil no coincide con la cantidad de puntos de esta GPU.", "The profile has a different number of points than this GPU.", "A quantidade de pontos do perfil difere da quantidade aceita por esta GPU.", "\320\247\320\270\321\201\320\273\320\276 \321\202\320\276\321\207\320\265\320\272 \320\262 \320\277\321\200\320\276\321\204\320\270\320\273\320\265 \320\275\320\265 \321\201\320\276\320\276\321\202\320\262\320\265\321\202\321\201\321\202\320\262\321\203\320\265\321\202 \321\215\321\202\320\276\320\271 GPU."},
        {"Perfil cargado como borrador. Revisa los valores y pulsa APLICAR en la pantalla correspondiente.", "Profile loaded as a draft. Review the values and select APPLY on the relevant page.", "Perfil carregado como rascunho. Revise os valores e selecione APLICAR na p\303\241gina correspondente.", "\320\237\321\200\320\276\321\204\320\270\320\273\321\214 \320\267\320\260\320\263\321\200\321\203\320\266\320\265\320\275 \320\272\320\260\320\272 \321\207\320\265\321\200\320\275\320\276\320\262\320\270\320\272. \320\237\321\200\320\276\320\262\320\265\321\200\321\214\321\202\320\265 \320\267\320\275\320\260\321\207\320\265\320\275\320\270\321\217 \320\270 \320\275\320\260\320\266\320\274\320\270\321\202\320\265 \302\253\320\237\320\240\320\230\320\234\320\225\320\235\320\230\320\242\320\254\302\273 \320\275\320\260 \320\275\321\203\320\266\320\275\320\276\320\271 \321\201\321\202\321\200\320\260\320\275\320\270\321\206\320\265."},
        {"ADLX no pudo enumerar las GPU AMD.", "ADLX could not enumerate AMD GPUs.", "O ADLX n\303\243o conseguiu listar as GPUs AMD.", "ADLX \320\275\320\265 \321\203\320\264\320\260\320\273\320\276\321\201\321\214 \320\277\320\265\321\200\320\265\321\207\320\270\321\201\320\273\320\270\321\202\321\214 \320\262\320\270\320\264\320\265\320\276\320\272\320\260\321\200\321\202\321\213 AMD."},
        {"ADLX devolvi\303\263 una identidad de GPU duplicada.", "ADLX returned a duplicate GPU identity.", "O ADLX retornou uma identidade de GPU duplicada.", "ADLX \320\262\320\265\321\200\320\275\321\203\320\273 \320\277\320\276\320\262\321\202\320\276\321\200\321\217\321\216\321\211\320\270\320\271\321\201\321\217 \320\270\320\264\320\265\320\275\321\202\320\270\321\204\320\270\320\272\320\260\321\202\320\276\321\200 GPU."},
        {"ADLX no detect\303\263 una GPU AMD identificable.", "ADLX found no identifiable AMD GPU.", "O ADLX n\303\243o detectou uma GPU AMD identific\303\241vel.", "ADLX \320\275\320\265 \320\276\320\261\320\275\320\260\321\200\321\203\320\266\320\270\320\273 AMD GPU \321\201 \320\276\320\277\321\200\320\265\320\264\320\265\320\273\321\217\320\265\320\274\321\213\320\274 \320\270\320\264\320\265\320\275\321\202\320\270\321\204\320\270\320\272\320\260\321\202\320\276\321\200\320\276\320\274."},
        {"Selecciona la GPU AMD que quieres ajustar.", "Select the AMD GPU you want to tune.", "Selecione a GPU AMD que deseja ajustar.", "\320\222\321\213\320\261\320\265\321\200\320\270\321\202\320\265 AMD GPU \320\264\320\273\321\217 \320\275\320\260\321\201\321\202\321\200\320\276\320\271\320\272\320\270."},
        {"La GPU seleccionada ya no est\303\241 disponible.", "The selected GPU is no longer available.", "A GPU selecionada n\303\243o est\303\241 mais dispon\303\255vel.", "\320\222\321\213\320\261\321\200\320\260\320\275\320\275\320\260\321\217 GPU \320\261\320\276\320\273\321\214\321\210\320\265 \320\275\320\265\320\264\320\276\321\201\321\202\321\203\320\277\320\275\320\260."},
        {"Otra instancia de ILION est\303\241 ajustando esta GPU o no se pudo obtener el bloqueo.", "Another ILION instance is tuning this GPU, or the lock could not be acquired.", "Outra inst\303\242ncia do ILION est\303\241 ajustando esta GPU ou n\303\243o foi poss\303\255vel obter o bloqueio.", "\320\224\321\200\321\203\320\263\320\260\321\217 \320\272\320\276\320\277\320\270\321\217 ILION \320\275\320\260\321\201\321\202\321\200\320\260\320\270\320\262\320\260\320\265\321\202 \321\215\321\202\321\203 GPU \320\270\320\273\320\270 \320\275\320\265 \321\203\320\264\320\260\320\273\320\276\321\201\321\214 \320\277\320\276\320\273\321\203\321\207\320\270\321\202\321\214 \320\261\320\273\320\276\320\272\320\270\321\200\320\276\320\262\320\272\321\203."},
        {"El controlador cambi\303\263 desde la \303\272ltima lectura. Revisa los valores antes de volver a aplicar.", "The driver settings changed since the last read. Review the values before applying again.", "As configura\303\247\303\265es do driver mudaram desde a \303\272ltima leitura. Revise os valores antes de aplicar novamente.", "\320\235\320\260\321\201\321\202\321\200\320\276\320\271\320\272\320\270 \320\264\321\200\320\260\320\271\320\262\320\265\321\200\320\260 \320\270\320\267\320\274\320\265\320\275\320\270\320\273\320\270\321\201\321\214 \320\277\320\276\321\201\320\273\320\265 \320\277\320\276\321\201\320\273\320\265\320\264\320\275\320\265\320\263\320\276 \321\207\321\202\320\265\320\275\320\270\321\217. \320\237\321\200\320\276\320\262\320\265\321\200\321\214\321\202\320\265 \320\267\320\275\320\260\321\207\320\265\320\275\320\270\321\217 \320\277\320\265\321\200\320\265\320\264 \320\277\320\276\320\262\321\202\320\276\321\200\320\275\321\213\320\274 \320\277\321\200\320\270\320\274\320\265\320\275\320\265\320\275\320\270\320\265\320\274."},
        {"No hay una curva anterior verificable para esta GPU.", "No previous curve can be verified for this GPU.", "N\303\243o h\303\241 uma curva anterior verific\303\241vel para esta GPU.", "\320\224\320\273\321\217 \321\215\321\202\320\276\320\271 GPU \320\275\320\265\321\202 \320\277\321\200\320\276\320\262\320\265\321\200\321\217\320\265\320\274\320\276\320\271 \320\277\321\200\320\265\320\264\321\213\320\264\321\203\321\211\320\265\320\271 \320\272\321\200\320\270\320\262\320\276\320\271."},
        {"Ajuste de f\303\241brica comprobado en el controlador.", "Factory defaults verified in the driver.", "Padr\303\265es de f\303\241brica verificados no driver.", "\320\227\320\260\320\262\320\276\320\264\321\201\320\272\320\270\320\265 \320\275\320\260\321\201\321\202\321\200\320\276\320\271\320\272\320\270 \320\277\321\200\320\276\320\262\320\265\321\200\320\265\320\275\321\213 \320\262 \320\264\321\200\320\260\320\271\320\262\320\265\321\200\320\265."},
        {"Restauraci\303\263n sin confirmar (ADLX ", "Reset unverified (ADLX ", "Restaura\303\247\303\243o n\303\243o confirmada (ADLX ", "\320\241\320\261\321\200\320\276\321\201 \320\275\320\265 \320\277\320\276\320\264\321\202\320\262\320\265\321\200\320\266\320\264\321\221\320\275 (ADLX "},
        {"). Par\303\241metros sin recuperar:", "). Settings not recovered:", "). Par\303\242metros n\303\243o recuperados:", "). \320\235\320\265 \320\262\320\276\321\201\321\201\321\202\320\260\320\275\320\276\320\262\320\273\320\265\320\275\321\213:"},
        {" ninguno de los campos comprobados.", " none of the verified fields.", " nenhum dos campos verificados.", " \320\262\321\201\320\265 \320\277\321\200\320\276\320\262\320\265\321\200\320\265\320\275\320\275\321\213\320\265 \320\277\320\276\320\273\321\217 \320\262\320\276\321\201\321\201\321\202\320\260\320\275\320\276\320\262\320\273\320\265\320\275\321\213."},
        {"El controlador no permite verificar la restauraci\303\263n.", "The driver does not allow the reset to be verified.", "O driver n\303\243o permite verificar a restaura\303\247\303\243o.", "\320\224\321\200\320\260\320\271\320\262\320\265\321\200 \320\275\320\265 \320\277\320\276\320\267\320\262\320\276\320\273\321\217\320\265\321\202 \320\277\321\200\320\276\320\262\320\265\321\200\320\270\321\202\321\214 \321\201\320\261\321\200\320\276\321\201."},
        {"ADLX rechaz\303\263 la curva (", "ADLX rejected the curve (", "O ADLX rejeitou a curva (", "ADLX \320\276\321\202\320\272\320\273\320\276\320\275\320\270\320\273 \320\272\321\200\320\270\320\262\321\203\321\216 ("},
        {"Error al procesar el ajuste. Consulta OptiScaler.log.", "The tuning request failed. See OptiScaler.log.", "Falha ao processar o ajuste. Consulte OptiScaler.log.", "\320\236\321\210\320\270\320\261\320\272\320\260 \320\276\320\261\321\200\320\260\320\261\320\276\321\202\320\272\320\270 \320\275\320\260\321\201\321\202\321\200\320\276\320\271\320\272\320\270. \320\241\320\274. OptiScaler.log."},
        {"CoreMin", "Min. GPU clock", "Freq. m\303\255n. da GPU", "\320\234\320\270\320\275. \321\207\320\260\321\201\321\202\320\276\321\202\320\260 GPU"},
        {"CoreMax", "Max. GPU clock", "Freq. m\303\241x. da GPU", "\320\234\320\260\320\272\321\201. \321\207\320\260\321\201\321\202\320\276\321\202\320\260 GPU"},
        {"Voltage", "Voltage", "Tens\303\243o", "\320\235\320\260\320\277\321\200\321\217\320\266\320\265\320\275\320\270\320\265"},
        {"PowerLimit", "Power limit", "Limite de pot\303\252ncia", "\320\233\320\270\320\274\320\270\321\202 \320\274\320\276\321\211\320\275\320\276\321\201\321\202\320\270"},
        {"VRAMMax", "Max. VRAM clock", "Freq. m\303\241x. da VRAM", "\320\234\320\260\320\272\321\201. \321\207\320\260\321\201\321\202\320\276\321\202\320\260 VRAM"},
        {"Curva", "Curve", "Curva", "\320\232\321\200\320\270\320\262\320\260\321\217"},
        {"Modo de ajuste", "Tuning mode", "Modo de ajuste", "\320\240\320\265\320\266\320\270\320\274 \320\275\320\260\321\201\321\202\321\200\320\276\320\271\320\272\320\270"},
    };
    if (source.starts_with("[json.exception."))
        return Lx("The profile file is invalid.", "El archivo de perfil no es v\303\241lido.", "O arquivo de perfil \303\251 inv\303\241lido.", "\320\244\320\260\320\271\320\273 \320\277\321\200\320\276\321\204\320\270\320\273\321\217 \320\275\320\265\320\264\320\265\320\271\321\201\321\202\320\262\320\270\321\202\320\265\320\273\320\265\320\275.");
    if (Ilion::CurrentLang() == Ilion::Lang::ES)
        return std::string(source);
    for (const auto& entry : messages)
        if (source == entry.es)
            return Lx(entry.en, entry.es, entry.pt, entry.ru);

    // ADLX error codes and names of changed fields are assembled by the worker.
    std::string translated;
    for (size_t offset = 0; offset < source.size();)
    {
        const auto remaining = source.substr(offset);
        const Entry* match = nullptr;
        for (const auto& entry : messages)
            if (remaining.starts_with(entry.es))
            {
                match = &entry;
                break;
            }
        if (match)
        {
            translated += Lx(match->en, match->es, match->pt, match->ru);
            offset += std::char_traits<char>::length(match->es);
        }
        else
            translated += source[offset++];
    }
    return translated;
}


struct AmdGpuPopupStyle
{
    AmdGpuPopupStyle()
    {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, U(11.f));
        ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, U(11.f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, U(8.f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, U(1.f));
        ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, U(1.f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, Sz(19.f, 15.f));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, Sz(9.f, 9.f));
        ImGui::PushStyleColor(ImGuiCol_PopupBg, Tone(pal::Panel));
        ImGui::PushStyleColor(ImGuiCol_Border, Tone(ImVec4(0.30f, 0.34f, 0.26f, 1.0f)));
        ImGui::PushStyleColor(ImGuiCol_Separator, Tone(pal::Line));
        ImGui::PushStyleColor(ImGuiCol_TitleBg, Tone(pal::PanelHi));
        ImGui::PushStyleColor(ImGuiCol_TitleBgActive, Tone(pal::PanelHi));
    }

    ~AmdGpuPopupStyle()
    {
        ImGui::PopStyleColor(5);
        ImGui::PopStyleVar(7);
    }
};

static bool AmdGpuPopupButton(const char* id, const char* label, ImVec2 size, bool primary = false,
                             const ImVec4& accent = pal::Green, float fontSize = 16.f)
{
    ImGui::PushFont(fUi, U(fontSize));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, U(1.f));
    ImGui::PushStyleColor(ImGuiCol_Border, Tone(primary ? Mix(pal::Line, accent, 0.55f) : pal::Line));
    ImGui::PushStyleColor(ImGuiCol_Button, Tone(Mix(pal::PanelHi, accent, primary ? 0.20f : 0.f)));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Tone(Mix(pal::PanelHi, accent, 0.30f)));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, Tone(Mix(pal::PanelHi, accent, 0.40f)));
    const bool pressed = ImGui::Button((std::string(label) + "###" + id).c_str(), Sz(size.x, size.y));
    ImGui::PopStyleColor(4);
    ImGui::PopStyleVar();
    ImGui::PopFont();
    return pressed;
}

static std::filesystem::path AmdGpuWarningMarker()
{
    const DWORD count = GetEnvironmentVariableW(L"LOCALAPPDATA", nullptr, 0);
    if (!count)
        return {};
    std::wstring local(count, L'\0');
    const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", local.data(), count);
    if (!length || length >= count)
        return {};
    local.resize(length);
    return std::filesystem::path(local) / L"ILION" / L"GpuTuning" / L"warning-v1.accepted";
}

static bool StoreAmdGpuWarning(const std::filesystem::path& marker)
{
    if (marker.empty())
        return false;
    std::error_code error;
    std::filesystem::create_directories(marker.parent_path(), error);
    if (error)
        return false;
    const auto file = CreateFileW(marker.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return GetLastError() == ERROR_FILE_EXISTS && std::filesystem::is_regular_file(marker, error);
    CloseHandle(file);
    return true;
}

enum class AmdGpuAccess { Waiting, Cancelled, Allowed };

static AmdGpuAccess DrawAmdGpuWarning()
{
    static const auto marker = AmdGpuWarningMarker();
    static bool accepted = []
    {
        std::error_code error;
        return std::filesystem::is_regular_file(marker, error);
    }();
    if (accepted)
        return AmdGpuAccess::Allowed;

    static bool read = false, saveFailed = false;
    const char* title = Lx(
            "WARNING \342\200\224 GPU TUNING###AmdGpuWarning",
            "ADVERTENCIA \342\200\224 AJUSTE GPU###AmdGpuWarning",
            "AVISO \342\200\224 AJUSTE DA GPU###AmdGpuWarning",
            "\320\237\320\240\320\225\320\224\320\243\320\237\320\240\320\225\320\226\320\224\320\225\320\235\320\230\320\225 \342\200\224 \320\235\320\220\320\241\320\242\320\240\320\236\320\231\320\232\320\220 GPU###AmdGpuWarning");
    if (!ImGui::IsPopupOpen(title))
    {
        read = saveFailed = false;
        ImGui::OpenPopup(title);
    }

    const AmdGpuPopupStyle style;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, U(12.f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, Sz(24.f, 20.f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, Sz(10.f, 10.f));
    ImGui::SetNextWindowSize(ImVec2(U(636.f), 0.f), ImGuiCond_Always);
    auto access = AmdGpuAccess::Waiting;
    if (ImGui::BeginPopupModal(title, nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                                               ImGuiWindowFlags_NoTitleBar))
    {
        const char* dash = std::strstr(title, " \342\200\224 ");
        const char* titleEnd = ImGui::FindRenderedTextEnd(title);
        ImGui::PushFont(fBrand, U(21.f));
        constexpr float titleGap = 7.f;
        const float titleWidth = ImGui::CalcTextSize(title, dash).x + U(titleGap) +
                                 ImGui::CalcTextSize(dash + 1, titleEnd).x;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (std::max)(0.f, (ImGui::GetContentRegionAvail().x - titleWidth) * 0.5f));
        ImGui::PushStyleColor(ImGuiCol_Text, Tone(pal::Red));
        ImGui::TextUnformatted(title, dash);
        ImGui::PopStyleColor();
        ImGui::SameLine(0.f, U(titleGap));
        ImGui::PushStyleColor(ImGuiCol_Text, Tone(pal::Text));
        ImGui::TextUnformatted(dash + 1, titleEnd);
        ImGui::PopStyleColor();
        ImGui::PopFont();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::PushTextWrapPos(0.f);
        ImGui::TextUnformatted(Lx(
            "This section lets you change your graphics card\342\200\231s operating parameters, including voltage, clock speeds, power consumption and fan speed.",
            "Esta secci\303\263n permite modificar par\303\241metros de funcionamiento de tu tarjeta gr\303\241fica, incluyendo voltaje, frecuencias, consumo energ\303\251tico y velocidad de los ventiladores.",
            "Esta se\303\247\303\243o permite alterar par\303\242metros de funcionamento da sua placa de v\303\255deo, incluindo tens\303\243o, frequ\303\252ncias, consumo de energia e velocidade das ventoinhas.",
            "\320\222 \321\215\321\202\320\276\320\274 \321\200\320\260\320\267\320\264\320\265\320\273\320\265 \320\274\320\276\320\266\320\275\320\276 \320\270\320\267\320\274\320\265\320\275\321\217\321\202\321\214 \320\277\320\260\321\200\320\260\320\274\320\265\321\202\321\200\321\213 \321\200\320\260\320\261\320\276\321\202\321\213 \320\262\320\270\320\264\320\265\320\276\320\272\320\260\321\200\321\202\321\213, \320\262\320\272\320\273\321\216\321\207\320\260\321\217 \320\275\320\260\320\277\321\200\321\217\320\266\320\265\320\275\320\270\320\265, \321\207\320\260\321\201\321\202\320\276\321\202\321\213, \321\215\320\275\320\265\321\200\320\263\320\276\320\277\320\276\321\202\321\200\320\265\320\261\320\273\320\265\320\275\320\270\320\265 \320\270 \321\201\320\272\320\276\321\200\320\276\321\201\321\202\321\214 \320\262\321\200\320\260\321\211\320\265\320\275\320\270\321\217 \320\262\320\265\320\275\321\202\320\270\320\273\321\217\321\202\320\276\321\200\320\276\320\262."));
        ImGui::Spacing();
        ImGui::TextUnformatted(Lx(
            "Changes may affect system stability, temperatures, cooling, power consumption and game performance. Unsuitable settings can cause crashes, reduced performance or unexpected shutdowns.",
            "Los cambios pueden afectar la estabilidad del sistema, las temperaturas, la refrigeraci\303\263n, el consumo el\303\251ctrico y el rendimiento de los juegos. Una configuraci\303\263n inadecuada puede provocar bloqueos, p\303\251rdida de rendimiento o apagados inesperados.",
            "As altera\303\247\303\265es podem afetar a estabilidade do sistema, as temperaturas, a refrigera\303\247\303\243o, o consumo de energia e o desempenho dos jogos. Uma configura\303\247\303\243o inadequada pode causar travamentos, perda de desempenho ou desligamentos inesperados.",
            "\320\230\320\267\320\274\320\265\320\275\320\265\320\275\320\270\321\217 \320\274\320\276\320\263\321\203\321\202 \320\277\320\276\320\262\320\273\320\270\321\217\321\202\321\214 \320\275\320\260 \321\201\321\202\320\260\320\261\320\270\320\273\321\214\320\275\320\276\321\201\321\202\321\214 \321\201\320\270\321\201\321\202\320\265\320\274\321\213, \321\202\320\265\320\274\320\277\320\265\321\200\320\260\321\202\321\203\321\200\321\203, \320\276\321\205\320\273\320\260\320\266\320\264\320\265\320\275\320\270\320\265, \321\215\320\275\320\265\321\200\320\263\320\276\320\277\320\276\321\202\321\200\320\265\320\261\320\273\320\265\320\275\320\270\320\265 \320\270 \320\277\321\200\320\276\320\270\320\267\320\262\320\276\320\264\320\270\321\202\320\265\320\273\321\214\320\275\320\276\321\201\321\202\321\214 \320\270\320\263\321\200. \320\235\320\265\320\277\320\276\320\264\321\205\320\276\320\264\321\217\321\211\320\270\320\265 \320\275\320\260\321\201\321\202\321\200\320\276\320\271\320\272\320\270 \320\274\320\276\320\263\321\203\321\202 \320\262\321\213\320\267\320\262\320\260\321\202\321\214 \320\267\320\260\320\262\320\270\321\201\320\260\320\275\320\270\321\217, \321\201\320\275\320\270\320\266\320\265\320\275\320\270\320\265 \320\277\321\200\320\276\320\270\320\267\320\262\320\276\320\264\320\270\321\202\320\265\320\273\321\214\320\275\320\276\321\201\321\202\320\270 \320\270\320\273\320\270 \320\262\320\275\320\265\320\267\320\260\320\277\320\275\320\276\320\265 \320\262\321\213\320\272\320\273\321\216\321\207\320\265\320\275\320\270\320\265."));
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, Tone(pal::Orange));
        ImGui::TextUnformatted(Lx(
            "Use these features only if you understand their effects. You are responsible for the settings you apply.",
            "Utiliza estas funciones \303\272nicamente si comprendes sus efectos. Los ajustes se realizan bajo tu responsabilidad.",
            "Use estas fun\303\247\303\265es somente se compreender seus efeitos. Voc\303\252 \303\251 respons\303\241vel pelos ajustes que aplicar.",
            "\320\230\321\201\320\277\320\276\320\273\321\214\320\267\321\203\320\271\321\202\320\265 \321\215\321\202\320\270 \321\204\321\203\320\275\320\272\321\206\320\270\320\270, \321\202\320\276\320\273\321\214\320\272\320\276 \320\265\321\201\320\273\320\270 \320\277\320\276\320\275\320\270\320\274\320\260\320\265\321\202\320\265 \320\270\321\205 \320\277\320\276\321\201\320\273\320\265\320\264\321\201\321\202\320\262\320\270\321\217. \320\222\321\213 \320\274\320\265\320\275\321\217\320\265\321\202\320\265 \320\275\320\260\321\201\321\202\321\200\320\276\320\271\320\272\320\270 \320\277\320\276\320\264 \321\201\320\262\320\276\321\216 \320\276\321\202\320\262\320\265\321\202\321\201\321\202\320\262\320\265\320\275\320\275\320\276\321\201\321\202\321\214."));
        ImGui::PopStyleColor();
        ImGui::PopTextWrapPos();
        ImGui::Separator();
        ImGui::Checkbox(Lx(
            "I have read and understood this warning.###AmdGpuWarningRead",
            "He le\303\255do y comprendido esta advertencia###AmdGpuWarningRead",
            "Li e compreendi este aviso.###AmdGpuWarningRead",
            "\320\257 \320\277\321\200\320\276\321\207\320\270\321\202\320\260\320\273(\320\260) \320\270 \320\277\320\276\320\275\320\270\320\274\320\260\321\216 \321\215\321\202\320\276 \320\277\321\200\320\265\320\264\321\203\320\277\321\200\320\265\320\266\320\264\320\265\320\275\320\270\320\265.###AmdGpuWarningRead"), &read);

        if (saveFailed)
            ImGui::TextColored(Tone(pal::Orange), "%s", Lx(
            "Could not save acceptance. Try again or cancel.",
            "No se pudo guardar la aceptaci\303\263n. Vuelve a intentarlo o cancela.",
            "N\303\243o foi poss\303\255vel salvar a aceita\303\247\303\243o. Tente novamente ou cancele.",
            "\320\235\320\265 \321\203\320\264\320\260\320\273\320\276\321\201\321\214 \321\201\320\276\321\205\321\200\320\260\320\275\320\270\321\202\321\214 \321\201\320\276\320\263\320\273\320\260\321\201\320\270\320\265. \320\237\320\276\320\262\321\202\320\276\321\200\320\270\321\202\320\265 \320\277\320\276\320\277\321\213\321\202\320\272\321\203 \320\270\320\273\320\270 \320\276\321\202\320\274\320\265\320\275\320\270\321\202\320\265 \320\264\320\265\320\271\321\201\321\202\320\262\320\270\320\265."));

        constexpr float cancelWidth = 156.f, acceptWidth = 254.f, buttonGap = 12.f, buttonHeight = 38.f;
        const float buttonsWidth = U(cancelWidth + buttonGap + acceptWidth);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (std::max)(0.f, (ImGui::GetContentRegionAvail().x - buttonsWidth) * 0.5f));
        if (AmdGpuPopupButton("AmdGpuWarningCancel", Lx(
            "CANCEL",
            "CANCELAR",
            "CANCELAR",
            "\320\236\320\242\320\234\320\225\320\235\320\220"), ImVec2(cancelWidth, buttonHeight)))
        {
            access = AmdGpuAccess::Cancelled;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine(0.f, U(buttonGap));
        ImGui::BeginDisabled(!read);
        if (AmdGpuPopupButton("AmdGpuWarningAccept", Lx(
            "ACCEPT AND CONTINUE",
            "ACEPTAR Y CONTINUAR",
            "ACEITAR E CONTINUAR",
            "\320\237\320\240\320\230\320\235\320\257\320\242\320\254 \320\230 \320\237\320\240\320\236\320\224\320\236\320\233\320\226\320\230\320\242\320\254"), ImVec2(acceptWidth, buttonHeight), true))
        {
            accepted = StoreAmdGpuWarning(marker);
            saveFailed = !accepted;
            if (accepted)
                ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar(3);
    return access;
}


static void AmdGpuButtonFeedback(const ImVec4& accent)
{
    const ImGuiID id = ImGui::GetItemID();
    const auto key = ImGui::GetIDWithSeed("##AmdGpuFeedback", nullptr, id);
    auto* storage = ImGui::GetStateStorage();
    const bool held = ImGui::IsItemActive();
    const float target = held ? 1.f : ImGui::IsItemHovered() ? 0.28f : 0.f;
    const float prior = storage->GetFloat(key);
    const float amount = held ? 1.f : ImLerp(prior, target, 1.f - std::exp(-19.f * ImGui::GetIO().DeltaTime));
    storage->SetFloat(key, amount);
    if (amount < 0.01f)
        return;
    auto* draw = ImGui::GetWindowDrawList();
    const auto a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
    draw->AddRectFilled(a, b, ImGui::GetColorU32(ImVec4(0.f, 0.f, 0.f, 0.17f * amount)), U(4.f));
    draw->AddRect(a, b, ImGui::GetColorU32(ImVec4(accent.x, accent.y, accent.z, 0.76f * amount)), U(4.f));
}

static bool AmdGpuActionButton(const char* id, const char* label, ImVec2 size, bool primary = false,
                               const ImVec4& accent = pal::Green, float fontSize = 16.f)
{
    const bool pressed = AmdGpuPopupButton(id, label, size, primary, accent, fontSize);
    AmdGpuButtonFeedback(accent);
    return pressed;
}

static void DrawGpuTuning()
{
    using namespace IlionGpuTuning;
    static std::shared_ptr<const Result> displayed;
    static std::string key, message;
    static Values draft;
    static bool dirty = false, external = false, pending = false, waiting = false;
    static int page = 0;
    static std::array<bool, FieldCount> enabled{};
    static bool customFan = false;
    static char profile[41] = "Predeterminado", newProfile[41]{};
    static int profileKind = 2;
    static bool profileConfirm = false, deleteConfirm = false, renameProfile = false;
    struct NumberEdit { char buffer[24]{}; bool active = false; };
    static std::vector<std::array<NumberEdit, 2>> pointEdits;
    static std::array<NumberEdit, FieldCount> fieldEdits;
    struct Reference
    {
        std::array<std::optional<double>, 8> values;
        std::array<double, 8> samples{};
        std::array<unsigned, 8> counts{};
        double started = 0;
        bool capturing = false, boardPower = false, capturedBoardPower = false;
    };
    static std::map<std::string, Reference> references;
    static int lastFrame = -1;
    static double lastVisit = 0.0;
    static double nextRead = 0.0;
    const double now = ImGui::GetTime();
    const int frame = ImGui::GetFrameCount();
    if (lastFrame >= 0 && (frame > lastFrame + 1 || now - lastVisit > 3.0) && !key.empty() && references[key].capturing)
    {
        references[key].capturing = false;
        message = "Captura interrumpida al salir de AJUSTE GPU. Se conserva la referencia anterior.";
    }
    lastFrame = frame;
    lastVisit = now;
    if (ImGui::IsWindowAppearing())
        nextRead = now;

    if (auto result = Latest(); result && result != displayed)
    {
        const auto& state = result->snapshot;
        pending = false;
        waiting = false;
        if (!state.key.empty() && (key.empty() || state.key == key))
        {
            const bool same = displayed && displayed->snapshot.key == state.key;
            if (same && result->action == Action::Read &&
                (displayed->snapshot.live != state.live || displayed->snapshot.factory != state.factory))
                external = true;
            if (result->loaded)
            {
                if (result->loadedKind != ProfileKind::Fans)
                {
                    draft.fields = result->loaded->fields;
                    for (int i = 0; i < FieldCount; ++i)
                        enabled[i] = draft.fields[i].has_value() && draft.fields[i] != state.live.fields[i];
                }
                if (result->loadedKind != ProfileKind::Undervolt)
                {
                    draft.curve = result->loaded->curve;
                    draft.zero = result->loaded->zero;
                    customFan = result->loadedCustomFan.value_or(draft.curve != state.live.curve);
                    pointEdits.clear();
                }
                dirty = draft != state.live;
            }
            else if (!same || !dirty || result->action == Action::Restore)
            {
                draft = state.live;
                dirty = false;
                if (!same || result->action == Action::Restore)
                {
                    enabled.fill(false);
                    customFan = false;
                    pointEdits.clear();
                    fieldEdits = {};
                }
            }
            else
            {
                const auto& previous = displayed->snapshot.live;
                for (int i = 0; i < FieldCount; ++i)
                    if (draft.fields[i] == previous.fields[i] || !state.live.fields[i])
                        draft.fields[i] = state.live.fields[i];
                const bool changedShape = draft.curve.size() != state.live.curve.size();
                const bool restoredCurve = result->action == Action::RestoreCurve && previous.curve != state.live.curve;
                if (draft.curve == previous.curve || changedShape || restoredCurve)
                    draft.curve = state.live.curve;
                if (changedShape || restoredCurve)
                    pointEdits.clear();
                if (draft.zero == previous.zero || !state.live.zero)
                    draft.zero = state.live.zero;
                dirty = draft != state.live;
            }
            key = state.key;
            auto& reference = references[key];
            if (reference.capturing && result->action == Action::Read && now - reference.started <= 30.0)
            {
                for (int metric : {0, 1, 2, 3, 4, 5})
                {
                    const auto value = state.metrics[metric];
                    if (!value || !std::isfinite(*value) || (metric == 2 && state.boardPower != reference.boardPower))
                        continue;
                    double& sample = reference.samples[metric];
                    if (metric == 4)
                        sample = reference.counts[metric] == 0 ? *value : (std::max)(sample, *value);
                    else
                        sample += *value;
                    ++reference.counts[metric];
                }
            }
        }
        if (result->action != Action::Read)
            message = result->message;
        displayed = std::move(result);
    }

    if (!waiting && (now >= nextRead || ImGui::IsWindowAppearing()))
    {
        Request request{Action::Read, key};
        if (page == 0 && customFan)
            request.draft.curve = draft.curve;
        waiting = Start(std::move(request));
        nextRead = now + 1.0;
    }

    if (!displayed)
    {
        ImGui::TextUnformatted(Lx("Reading the AMD driver\342\200\246", "Consultando el controlador AMD\342\200\246", "Consultando o driver AMD\342\200\246", "\320\247\321\202\320\265\320\275\320\270\320\265 \320\264\320\260\320\275\320\275\321\213\321\205 \320\264\321\200\320\260\320\271\320\262\320\265\321\200\320\260 AMD\342\200\246"));
        return;
    }
    const Snapshot& state = displayed->snapshot;
    const Device* selected = nullptr;
    for (const auto& device : state.devices)
        if (device.key == state.key)
            selected = &device;
    ImGui::BeginDisabled(pending);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - U(60));
    if (ImGui::BeginCombo("GPU", selected ? selected->label.c_str() : Lx("Select", "Seleccionar", "Selecionar", "\320\222\321\213\320\261\321\200\320\260\321\202\321\214")))
    {
        for (const auto& device : state.devices)
        {
            ImGui::PushID(device.key.c_str());
            if (ImGui::Selectable(device.label.c_str(), device.key == key) && device.key != key)
            {
                if (!key.empty())
                    references[key].capturing = false;
                key = device.key;
                dirty = external = false;
                message.clear();
                nextRead = now;
            }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    ImGui::EndDisabled();
    if (!state.error.empty())
        ImGui::TextWrapped("%s", AmdGpuMessage(state.error).c_str());
    if (state.key.empty() || key != state.key)
        return;

    auto& reference = references[key];
    if (reference.capturing && now - reference.started >= 30.0)
    {
        reference.capturing = false;
        if (std::ranges::any_of(reference.counts, [](auto count) { return count != 0; }))
        {
            for (int i = 0; i < 8; ++i)
                reference.values[i] = reference.counts[i] == 0 ? std::nullopt : std::optional<double>(
                    i == 4 ? reference.samples[i] : reference.samples[i] / reference.counts[i]);
            reference.capturedBoardPower = reference.boardPower;
            message = "Referencia capturada durante 30 segundos.";
        }
        else
            message = "La captura no obtuvo m\303\251tricas disponibles.";
    }
    const auto metricColor = [&](int metric, double value) -> const ImVec4&
    {
        if (metric == 4)
            return value < 75 ? pal::Green : value < 80 ? pal::Yellow : value < 90 ? IlionTheme::RadioactiveOrange : pal::Red;
        if (metric == 2)
            return value <= 300 ? pal::Green : value < 350 ? pal::Yellow : value < 400 ? IlionTheme::RadioactiveOrange : pal::Red;
        if (metric == 1)
            return value < 1000 ? pal::Green : value < 1200 ? pal::Yellow : IlionTheme::RadioactiveOrange;
        return pal::Green;
    };
    const char* const labels[] = {Lx("Min. GPU clock", "Frecuencia m\303\255nima GPU", "Frequ\303\252ncia m\303\255n. da GPU", "\320\234\320\270\320\275. \321\207\320\260\321\201\321\202\320\276\321\202\320\260 GPU"), Lx("Max. GPU clock", "Frecuencia m\303\241xima GPU", "Frequ\303\252ncia m\303\241x. da GPU", "\320\234\320\260\320\272\321\201. \321\207\320\260\321\201\321\202\320\276\321\202\320\260 GPU"), Lx("Configured voltage", "Voltaje configurado", "Tens\303\243o configurada", "\320\227\320\260\320\264\320\260\320\275\320\275\320\276\320\265 \320\275\320\260\320\277\321\200\321\217\320\266\320\265\320\275\320\270\320\265"), Lx("Power limit", "L\303\255mite de potencia", "Limite de pot\303\252ncia", "\320\233\320\270\320\274\320\270\321\202 \320\274\320\276\321\211\320\275\320\276\321\201\321\202\320\270"), Lx("Max. VRAM clock", "Frecuencia m\303\241xima VRAM", "Frequ\303\252ncia m\303\241x. da VRAM", "\320\234\320\260\320\272\321\201. \321\207\320\260\321\201\321\202\320\276\321\202\320\260 VRAM")};
    constexpr const char* units[] = {"MHz", "MHz", "mV", "%", "MHz"};
    const char* const metricNames[] = {Lx("GPU clock", "Frecuencia GPU", "Frequ\303\252ncia da GPU", "\320\247\320\260\321\201\321\202\320\276\321\202\320\260 GPU"), Lx("Actual voltage", "Voltaje real", "Tens\303\243o real", "\320\244\320\260\320\272\321\202. \320\275\320\260\320\277\321\200\321\217\320\266\320\265\320\275\320\270\320\265"), Lx("GPU power", "Consumo GPU", "Consumo da GPU", "\320\234\320\276\321\211\320\275\320\276\321\201\321\202\321\214 GPU"), Lx("GPU temperature", "Temperatura GPU", "Temperatura da GPU", "\320\242\320\265\320\274\320\277\320\265\321\200\320\260\321\202\321\203\321\200\320\260 GPU"), Lx("Hotspot", "Hotspot", "Ponto quente", "\320\223\320\276\321\200\321\217\321\207\320\260\321\217 \321\202\320\276\321\207\320\272\320\260"), Lx("Fans", "Ventiladores", "Ventoinhas", "\320\222\320\265\320\275\321\202\320\270\320\273\321\217\321\202\320\276\321\200\321\213"), Lx("GPU usage", "Utilizaci\303\263n GPU", "Uso da GPU", "\320\227\320\260\320\263\321\200\321\203\320\267\320\272\320\260 GPU"), Lx("VRAM clock", "Frecuencia VRAM", "Frequ\303\252ncia da VRAM", "\320\247\320\260\321\201\321\202\320\276\321\202\320\260 VRAM")};
    constexpr const char* metricUnits[] = {"MHz", "mV", "W", "\302\260C", "\302\260C", "RPM", "%", "MHz"};
    const char* const groups[] = {"GPU", Lx("TEMPERATURE", "TEMPERATURA", "TEMPERATURA", "\320\242\320\225\320\234\320\237\320\225\320\240\320\220\320\242\320\243\320\240\320\220"), Lx("POWER", "POTENCIA", "POT\303\212NCIA", "\320\234\320\236\320\251\320\235\320\236\320\241\320\242\320\254"), Lx("FANS / MEMORY", "VENTILADORES / MEMORIA", "VENTOINHAS / MEM\303\223RIA", "\320\236\320\245\320\233\320\220\320\226\320\224\320\225\320\235\320\230\320\225 / \320\237\320\220\320\234\320\257\320\242\320\254")};
    constexpr int groupMetrics[][2] = {{0, 1}, {3, 4}, {2, 6}, {5, 7}};
    const auto heading = [&](const char* name, const ImVec4& color)
    {
        ImGui::PushFont(fBrand, U(18));
        ImGui::TextColored(Tone(color), "%s", name);
        ImGui::PopFont();
    };
    const auto button = [&](const char* id, const char* label, bool selected, const ImVec4& color)
    {
        const bool pressed = Button(id, label, ImVec2(ImGui::GetContentRegionAvail().x / S, 40), selected, color, 16);
        AmdGpuButtonFeedback(color);
        return pressed;
    };
    constexpr auto Grid = ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings;
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, Sz(12, 8));
    if (ImGui::BeginTable("##AmdReadings", 4, Grid | ImGuiTableFlags_BordersInnerV))
    {
        for (int group = 0; group < 4; ++group)
        {
            ImGui::TableNextColumn();
            ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, Col(pal::Panel));
            heading(groups[group], pal::Green);
            for (int metric : groupMetrics[group])
            {
                ImGui::TextColored(Tone(pal::Text2), "%s", metric == 2 && state.boardPower ? Lx("Board power", "Potencia total", "Pot\303\252ncia da placa", "\320\234\320\276\321\211\320\275\320\276\321\201\321\202\321\214 \320\277\320\273\320\260\321\202\321\213") : metricNames[metric]);
                if (state.metrics[metric])
                {
                    ImGui::PushFont(fUi, U(23));
                    ImGui::TextColored(Tone(metricColor(metric, *state.metrics[metric])), metric == 2 || metric == 3 || metric == 4 || metric == 6 ? "%.1f %s" : "%.0f %s",
                                       *state.metrics[metric], metricUnits[metric]);
                    ImGui::PopFont();
                }
                else
                    ImGui::TextUnformatted(Lx("Unavailable", "No disponible", "Indispon\303\255vel", "\320\235\320\265\320\264\320\276\321\201\321\202\321\203\320\277\320\275\320\276"));
            }
        }
        ImGui::EndTable();
    }
    if (ImGui::BeginTable("##GpuPages", 2, Grid))
    {
        ImGui::TableNextColumn();
        if (button("##GpuFanPage", Lx("FANS", "VENTILADORES", "VENTOINHAS", "\320\222\320\225\320\235\320\242\320\230\320\233\320\257\320\242\320\236\320\240\320\253"), page == 0, pal::Orange))
            page = 0;
        ImGui::TableNextColumn();
        if (button("##GpuUndervoltPage", Lx("UNDERVOLT", "UNDERVOLT", "UNDERVOLT", "\320\220\320\235\320\224\320\225\320\240\320\222\320\236\320\233\320\254\320\242"), page == 1, pal::Orange))
            page = 1;
        ImGui::EndTable();
    }

    const auto action = [&](Action command, const Values& values, bool overwrite = false)
    {
        if (reference.capturing && (command == Action::Apply || command == Action::Restore || command == Action::RestoreCurve))
            return false;
        Request request{command, state.key, profile, state.live, values, state.factory};
        request.kind = static_cast<ProfileKind>(profileKind);
        request.overwrite = overwrite;
        request.customFan = customFan;
        request.newName = newProfile;
        if (Start(std::move(request)))
        {
            waiting = pending = true;
            message.clear();
            external = false;
            return true;
        }
        message = "Consulta en curso. Vuelve a intentarlo.";
        return false;
    };
    if (external)
        ImGui::TextWrapped(Lx("An external change was detected. Current driver values are shown.", "Se detect\303\263 un cambio externo. Se muestran los valores actuales del controlador.", "Foi detectada uma altera\303\247\303\243o externa. S\303\243o exibidos os valores atuais do driver.", "\320\236\320\261\320\275\320\260\321\200\321\203\320\266\320\265\320\275\320\276 \320\262\320\275\320\265\321\210\320\275\320\265\320\265 \320\270\320\267\320\274\320\265\320\275\320\265\320\275\320\270\320\265. \320\237\320\276\320\272\320\260\320\267\320\260\320\275\321\213 \321\202\320\265\320\272\321\203\321\211\320\270\320\265 \320\267\320\275\320\260\321\207\320\265\320\275\320\270\321\217 \320\264\321\200\320\260\320\271\320\262\320\265\321\200\320\260."));
    if (pending)
        ImGui::TextUnformatted(Lx("Reading the driver\342\200\246", "Consultando el controlador\342\200\246", "Consultando o driver\342\200\246", "\320\247\321\202\320\265\320\275\320\270\320\265 \320\264\320\260\320\275\320\275\321\213\321\205 \320\264\321\200\320\260\320\271\320\262\320\265\321\200\320\260\342\200\246"));
    if (!message.empty())
        ImGui::TextWrapped("%s", AmdGpuMessage(message).c_str());

    bool rejectedNumber = false, editingNumber = false;
    const auto input = [&](const char* id, NumberEdit& edit, int& value, const Range& range, const ImVec4& color,
                           float width = 0.f)
    {
        const int before = value;
        ImGui::PushID(id);
        const bool wasActive = edit.active;
        if (!wasActive)
            std::snprintf(edit.buffer, sizeof edit.buffer, "%d", value);
        const float buttonWidth = ImGui::GetFrameHeight();
        const float spacing = ImGui::GetStyle().ItemInnerSpacing.x;
        const float availableWidth = width > 0.f ? width : ImGui::GetContentRegionAvail().x;
        ImGui::SetNextItemWidth((std::max)(U(40), availableWidth - 2 * (buttonWidth + spacing)));
        // Some games register raw keyboards without legacy text messages.
        // Supply digits only for this active editor and only without text already queued.
        auto& io = ImGui::GetIO();
        if (wasActive && ImGui::GetActiveID() == ImGui::GetID("##number") &&
            io.InputQueueCharacters.empty() && !io.KeyCtrl && !io.KeyAlt && !io.KeyShift)
        {
            const bool raw = OptiInput::GetDebugState().RawKeyboardNoLegacy;
            for (int digit = 0; digit < 10; ++digit)
                if (ImGui::IsKeyPressed(static_cast<ImGuiKey>(ImGuiKey_Keypad0 + digit)) ||
                    (raw && (ImGui::IsKeyPressed(static_cast<ImGuiKey>(ImGuiKey_0 + digit)) ||
                             OptiInput::IsKeyPressed(VK_NUMPAD0 + digit))))
                    io.InputQueueCharacters.push_back(static_cast<ImWchar>('0' + digit));
            if (ImGui::IsKeyPressed(ImGuiKey_KeypadSubtract) ||
                (raw && (OptiInput::IsKeyPressed(VK_SUBTRACT) || OptiInput::IsKeyPressed(VK_OEM_MINUS))))
                io.InputQueueCharacters.push_back('-');
        }
        ImGui::PushStyleColor(ImGuiCol_Text, Tone(color));
        const bool enter = ImGui::InputText("##number", edit.buffer, sizeof edit.buffer,
            ImGuiInputTextFlags_CharsDecimal | ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
        ImGui::PopStyleColor();
        edit.active = ImGui::IsItemActive();
        if (enter && edit.active)
        {
            ImGui::ClearActiveID();
            edit.active = false;
        }
        if (ImGui::IsItemActivated())
            spdlog::debug("[AJUSTE GPU] Edición de ventilador iniciada id={:08x}", ImGui::GetItemID());
        editingNumber |= edit.active;
        if ((enter || (wasActive && !edit.active) || ImGui::IsItemDeactivatedAfterEdit()) &&
            !ImGui::IsKeyPressed(ImGuiKey_Escape))
        {
            char* end = nullptr;
            const long long proposed = std::strtoll(edit.buffer, &end, 10);
            if (end != edit.buffer && *end == '\0' && proposed >= range.min && proposed <= range.max &&
                range.contains(static_cast<int>(proposed)))
            {
                if (value != static_cast<int>(proposed))
                    spdlog::debug("[AJUSTE GPU] Borrador de ventilador confirmado id={:08x} Enter={}", ImGui::GetItemID(), enter);
                value = static_cast<int>(proposed);
            }
            else
            {
                spdlog::debug("[AJUSTE GPU] Borrador de ventilador no válido id={:08x} Enter={}", ImGui::GetItemID(), enter);
                rejectedNumber = true;
                // Dear ImGui retains a copy of the last edit across deactivation.
                // Discard that copy for this item after rejecting the draft.
                const ImGuiID item = ImGui::GetItemID();
                std::snprintf(edit.buffer, sizeof edit.buffer, "%d", value);
                if (auto* text = ImGui::GetInputTextState(item))
                    text->ReloadUserBufAndSelectAll();
                auto& deactivated = ImGui::GetCurrentContext()->InputTextDeactivatedState;
                if (deactivated.ID == item)
                    deactivated.ID = 0;
                message = Lx("Number outside the supported range or step. Previous value kept.",
                    "N\303\272mero fuera del rango o paso admitido. Se conserva el valor anterior.",
                    "N\303\272mero fora da faixa ou do passo permitido. O valor anterior foi mantido.",
                    "\320\247\320\270\321\201\320\273\320\276 \320\262\320\275\320\265 \320\264\320\276\320\277\321\203\321\201\321\202\320\270\320\274\320\276\320\263\320\276 \320\264\320\270\320\260\320\277\320\260\320\267\320\276\320\275\320\260 \320\270\320\273\320\270 \321\210\320\260\320\263\320\260. \320\237\321\200\320\265\320\266\320\275\320\265\320\265 \320\267\320\275\320\260\321\207\320\265\320\275\320\270\320\265 \321\201\320\276\321\205\321\200\320\260\320\275\320\265\320\275\320\276.");
            }
        }
        ImGui::SetItemTooltip(Lx("Range %d to %d \302\267 step %d", "Rango %d a %d \302\267 paso %d", "Faixa de %d a %d \302\267 passo %d", "\320\224\320\270\320\260\320\277\320\260\320\267\320\276\320\275 \320\276\321\202 %d \320\264\320\276 %d \302\267 \321\210\320\260\320\263 %d"), range.min, range.max, range.step);
        ImGui::SameLine(0, spacing);
        const bool decrease = ImGui::Button("-##step", ImVec2(buttonWidth, buttonWidth));
        AmdGpuButtonFeedback(color);
        if (decrease && static_cast<long long>(value) - range.step >= range.min)
            value -= range.step;
        ImGui::SameLine(0, spacing);
        const bool increase = ImGui::Button("+##step", ImVec2(buttonWidth, buttonWidth));
        AmdGpuButtonFeedback(color);
        if (increase && static_cast<long long>(value) + range.step <= range.max)
            value += range.step;
        ImGui::PopID();
        return before != value;
    };
    bool requestRestore = false, openSave = false, openLoad = false;
    ImGui::BeginDisabled(pending);
    if (page == 0)
    {
        auto& curve = draft.curve;
        const bool curveAvailable = !curve.empty() && curve.size() == state.live.curve.size();
        pointEdits.resize(curve.size());
        if (ImGui::BeginTable("##GpuFanOptions", 3, Grid))
        {
            ImGui::TableNextColumn();
            ImGui::BeginDisabled(reference.capturing || !state.canRestoreCurve || state.autoTuning);
            if (button("##GpuPreviousFan", Lx("PREVIOUS CURVE", "CURVA ANTERIOR", "CURVA ANTERIOR", "\320\237\320\240\320\225\320\226\320\235\320\257\320\257 \320\232\320\240\320\230\320\222\320\220\320\257"), false, pal::Green))
                action(Action::RestoreCurve, {});
            ImGui::EndDisabled();
            ImGui::SetItemTooltip(Lx("Restores only the last curve from this session. ADLX offers no separate switch to automatic fan control.", "Restaura solo la \303\272ltima curva de esta sesi\303\263n. ADLX no ofrece un cambio independiente a modo autom\303\241tico.", "Restaura apenas a \303\272ltima curva desta sess\303\243o. O ADLX n\303\243o oferece uma op\303\247\303\243o separada para o controle autom\303\241tico.", "\320\222\320\276\321\201\321\201\321\202\320\260\320\275\320\260\320\262\320\273\320\270\320\262\320\260\320\265\321\202 \321\202\320\276\320\273\321\214\320\272\320\276 \320\277\320\276\321\201\320\273\320\265\320\264\320\275\321\216\321\216 \320\272\321\200\320\270\320\262\321\203\321\216 \321\215\321\202\320\276\320\263\320\276 \321\201\320\265\320\260\320\275\321\201\320\260. \320\222 ADLX \320\275\320\265\321\202 \320\276\321\202\320\264\320\265\320\273\321\214\320\275\320\276\320\263\320\276 \320\277\320\265\321\200\320\265\320\272\320\273\321\216\321\207\320\260\321\202\320\265\320\273\321\217 \320\260\320\262\321\202\320\276\320\274\320\260\321\202\320\270\321\207\320\265\321\201\320\272\320\276\320\263\320\276 \321\203\320\277\321\200\320\260\320\262\320\273\320\265\320\275\320\270\321\217 \320\262\320\265\320\275\321\202\320\270\320\273\321\217\321\202\320\276\321\200\320\260\320\274\320\270."));
            ImGui::TableNextColumn();
            ImGui::BeginDisabled(!curveAvailable || !state.fanEditable || state.autoTuning);
            if (button("##GpuCustomFan", Lx("CUSTOM", "PERSONALIZADO", "PERSONALIZADO", "\320\222\320\240\320\243\320\247\320\235\320\243\320\256"), customFan, pal::Orange))
                customFan = true;
            ImGui::EndDisabled();
            ImGui::TableNextColumn();
            if (draft.zero && state.live.zero)
            {
                ImGui::BeginDisabled(state.autoTuning);
                ImGui::Checkbox("Zero RPM", &*draft.zero);
                ImGui::EndDisabled();
            }
            else
                ImGui::TextDisabled(Lx("Zero RPM unavailable", "Zero RPM no disponible", "Zero RPM indispon\303\255vel", "Zero RPM \320\275\320\265\320\264\320\276\321\201\321\202\321\203\320\277\320\265\320\275"));
            ImGui::EndTable();
        }
        if (curveAvailable)
        {
            ImGui::TextColored(Tone(pal::Green), Lx("Driver curve", "Curva del controlador", "Curva do driver", "\320\232\321\200\320\270\320\262\320\260\321\217 \320\264\321\200\320\260\320\271\320\262\320\265\321\200\320\260"));
            if (curve != state.live.curve)
            {
                ImGui::SameLine(0, U(24));
                ImGui::TextColored(Tone(pal::Orange), Lx("Pending changes", "Cambios pendientes", "Altera\303\247\303\265es pendentes", "\320\236\320\266\320\270\320\264\320\260\321\216\321\211\320\270\320\265 \320\270\320\267\320\274\320\265\320\275\320\265\320\275\320\270\321\217"));
            }
            const auto lo = ImGui::GetCursorScreenPos();
            const float width = ImGui::GetContentRegionAvail().x;
            const ImVec2 hi(lo.x + width, lo.y + U(300));
            ImGui::Dummy(ImVec2(width, hi.y - lo.y));
            auto* draw = ImGui::GetWindowDrawList();
            draw->AddRectFilled(lo, hi, Col(pal::Panel));
            draw->AddRect(lo, hi, Col(pal::Line));
            const ImVec2 a(lo.x + U(75), lo.y + U(40)), b(hi.x - U(36), hi.y - U(58));
            draw->AddText(ImVec2(lo.x + U(12), lo.y + U(10)), Col(pal::Text), Lx("Fan (%)", "Ventilador (%)", "Ventoinha (%)", "\320\222\320\265\320\275\321\202\320\270\320\273\321\217\321\202\320\276\321\200 (%)"));
            const char* const axis = Lx("Temperature (\302\260C)", "Temperatura (\302\260C)", "Temperatura (\302\260C)", "\320\242\320\265\320\274\320\277\320\265\321\200\320\260\321\202\321\203\321\200\320\260 (\302\260C)");
            draw->AddText(ImVec2((a.x + b.x - ImGui::CalcTextSize(axis).x) / 2, hi.y - U(24)), Col(pal::Text), axis);
            for (int i = 0; i <= 5; ++i)
            {
                const float f = i / 5.f;
                const float x = a.x + f * (b.x - a.x), y = b.y - f * (b.y - a.y);
                draw->AddLine(ImVec2(x, a.y), ImVec2(x, b.y), Col(pal::Line));
                draw->AddLine(ImVec2(a.x, y), ImVec2(b.x, y), Col(pal::Line));
                const auto temp = std::to_string(std::lround(std::lerp(double(state.temperature.min), double(state.temperature.max), double(f))));
                const auto speed = std::to_string(std::lround(std::lerp(double(state.speed.min), double(state.speed.max), double(f))));
                draw->AddText(ImVec2(x - ImGui::CalcTextSize(temp.c_str()).x / 2, b.y + U(8)), Col(pal::Text2), temp.c_str());
                draw->AddText(ImVec2(a.x - U(12) - ImGui::CalcTextSize(speed.c_str()).x, y - U(8)), Col(pal::Text2), speed.c_str());
            }
            const auto plot = [&](const std::vector<Point>& points, ImU32 color, bool label)
            {
                ImVec2 previous;
                for (size_t i = 0; i < points.size(); ++i)
                {
                    const auto& point = points[i];
                    const auto fraction = [](int value, const Range& limits)
                    {
                        return std::clamp(float((value - double(limits.min)) / (double(limits.max) - limits.min)), 0.f, 1.f);
                    };
                    const ImVec2 pos(std::lerp(a.x, b.x, fraction(point.temperature, state.temperature)),
                                     std::lerp(b.y, a.y, fraction(point.speed, state.speed)));
                    if (i)
                        draw->AddLine(previous, pos, color, U(2.5f));
                    draw->AddCircleFilled(pos, U(5), color);
                    if (label)
                    {
                        const auto name = "P" + std::to_string(i + 1);
                        draw->AddText(ImVec2(pos.x + U(8), pos.y - U(22)), color, name.c_str());
                    }
                    previous = pos;
                }
            };
            plot(state.live.curve, Col(pal::Green), true);
            if (curve != state.live.curve)
                plot(curve, Col(pal::Orange), false);

            ImGui::Indent(U(12));
            if (ImGui::BeginTable("##AmdFanPoints", static_cast<int>(curve.size()) + 1, Grid | ImGuiTableFlags_BordersInnerV,
                                 ImVec2(ImGui::GetContentRegionAvail().x - U(12), 0)))
            {
                ImGui::TableNextColumn();
                heading(Lx("POINT", "PUNTO", "PONTO", "\320\242\320\236\320\247\320\232\320\220"), pal::Text2);
                for (size_t i = 0; i < curve.size(); ++i)
                {
                    ImGui::TableNextColumn();
                    const auto title = "P" + std::to_string(i + 1);
                    heading(title.c_str(), pal::Green);
                }
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(Lx("Temperature (\302\260C)", "Temperatura (\302\260C)", "Temperatura (\302\260C)", "\320\242\320\265\320\274\320\277\320\265\321\200\320\260\321\202\321\203\321\200\320\260 (\302\260C)"));
                ImGui::BeginDisabled(!customFan || !state.fanEditable || state.autoTuning);
                for (size_t i = 0; i < curve.size(); ++i)
                {
                    ImGui::TableNextColumn();
                    ImGui::PushID(static_cast<int>(i));
                    input("##temp", pointEdits[i][0], curve[i].temperature, state.temperature, pal::Red);
                    ImGui::PopID();
                }
                ImGui::EndDisabled();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(Lx("Fan (%)", "Ventilador (%)", "Ventoinha (%)", "\320\222\320\265\320\275\321\202\320\270\320\273\321\217\321\202\320\276\321\200 (%)"));
                ImGui::BeginDisabled(!customFan || !state.fanEditable || state.autoTuning);
                for (size_t i = 0; i < curve.size(); ++i)
                {
                    ImGui::TableNextColumn();
                    ImGui::PushID(static_cast<int>(i));
                    input("##speed", pointEdits[i][1], curve[i].speed, state.speed, IlionTheme::InfoBlue);
                    ImGui::PopID();
                }
                ImGui::EndDisabled();
                ImGui::EndTable();
            }
            ImGui::Unindent(U(12));
        }
        else
            ImGui::TextDisabled(Lx("Curve unavailable", "Curva no disponible", "Curva indispon\303\255vel", "\320\232\321\200\320\270\320\262\320\260\321\217 \320\275\320\265\320\264\320\276\321\201\321\202\321\203\320\277\320\275\320\260"));
    }
    else
    {
        if (ImGui::BeginTable("##UndervoltColumns", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_BordersInnerV))
        {
        ImGui::TableSetupColumn(Lx("Setting", "Ajuste", "Ajuste", "\320\237\320\260\321\200\320\260\320\274\320\265\321\202\321\200"), ImGuiTableColumnFlags_WidthStretch, 55.f);
        ImGui::TableSetupColumn(Lx("Comparison", "Comparativa", "Compara\303\247\303\243o", "\320\241\321\200\320\260\320\262\320\275\320\265\320\275\320\270\320\265"), ImGuiTableColumnFlags_WidthStretch, 45.f);
        ImGui::TableNextColumn();
        float labelWidth = 0.0f;
        for (const char* label : labels)
            labelWidth = (std::max)(labelWidth, ImGui::CalcTextSize(label).x);
        const float controllerX = ImGui::GetCursorPosX() + ImGui::GetFrameHeight() +
                                  ImGui::GetStyle().ItemInnerSpacing.x + labelWidth + U(18);

        for (int i = 0; i < FieldCount; ++i)
        {
            ImGui::PushID(i);
            const auto& range = state.ranges[i];
            const bool readable = draft.fields[i] && state.live.fields[i];
            const bool available = readable && range.step > 0 && range.min < range.max;
            ImGui::Separator();
            ImGui::BeginDisabled(!available);
            ImGui::Checkbox((std::string(labels[i]) + "###GpuTuningField").c_str(), &enabled[i]);
            ImGui::EndDisabled();
            if (readable)
            {
                ImGui::SameLine(controllerX);
                ImGui::TextColored(Tone(pal::Green), Lx("Driver  %d %s", "Controlador  %d %s", "Driver  %d %s", "\320\224\321\200\320\260\320\271\320\262\320\265\321\200  %d %s"), *state.live.fields[i], units[i]);
                if (!available)
                {
                    if (range.step > 0)
                        ImGui::TextWrapped(Lx("ADLX reports %d to %d %s \302\267 step %d. Write support is unconfirmed.", "ADLX informa %d a %d %s \302\267 paso %d. Capacidad de escritura sin confirmar.", "O ADLX informa %d a %d %s \302\267 passo %d. Suporte \303\240 grava\303\247\303\243o n\303\243o confirmado.", "\320\224\320\270\320\260\320\277\320\260\320\267\320\276\320\275 ADLX: \320\276\321\202 %d \320\264\320\276 %d %s \302\267 \321\210\320\260\320\263 %d. \320\227\320\260\320\277\320\270\321\201\321\214 \320\275\320\265 \320\277\320\276\320\264\321\202\320\262\320\265\321\200\320\266\320\264\320\265\320\275\320\260."),
                                           range.min, range.max, units[i], range.step);
                    else
                        ImGui::TextUnformatted(Lx("Writable range unavailable.", "Intervalo de escritura no disponible.", "Faixa de grava\303\247\303\243o indispon\303\255vel.", "\320\224\320\270\320\260\320\277\320\260\320\267\320\276\320\275 \320\267\320\260\320\277\320\270\321\201\320\270 \320\275\320\265\320\264\320\276\321\201\321\202\321\203\320\277\320\265\320\275."));
                    ImGui::PopID();
                    continue;
                }
                int& proposed = *draft.fields[i];
                const auto steps = (static_cast<long long>(range.max) - range.min) / range.step;
                auto position = std::clamp((static_cast<long long>(proposed) - range.min) / range.step, 0LL, steps);
                ImGui::BeginDisabled(!enabled[i]);
                const float numericWidth = U(155);
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - numericWidth - U(14));
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, Sz(9, 9));
                const std::string formatted = std::to_string(proposed) + " " + (i == Power ? "%%" : units[i]);
                constexpr long long zero = 0;
                if (ImGui::SliderScalar("##range", ImGuiDataType_S64, &position, &zero, &steps, formatted.c_str(), ImGuiSliderFlags_NoInput))
                    proposed = static_cast<int>(range.min + position * range.step);
                ImGui::PopStyleVar();
                ImGui::EndDisabled();
                ImGui::SameLine(0, U(14));
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, Sz(9, 9));
                if (input("##value", fieldEdits[i], proposed, range, pal::Text, numericWidth))
                    enabled[i] = true;
                ImGui::PopStyleVar();
                ImGui::TextColored(Tone(!enabled[i] || range.contains(proposed) ? pal::Text : pal::Orange),
                                   Lx("%d to %d %s  \302\267  step %d", "%d a %d %s  \302\267  paso %d", "%d a %d %s  \302\267  passo %d", "\320\236\321\202 %d \320\264\320\276 %d %s  \302\267  \321\210\320\260\320\263 %d"), range.min, range.max, units[i], range.step);
            }
            else
                ImGui::TextDisabled(Lx("Unavailable", "No disponible", "Indispon\303\255vel", "\320\235\320\265\320\264\320\276\321\201\321\202\321\203\320\277\320\275\320\276"));
            ImGui::PopID();
        }
        ImGui::TableNextColumn();
        ImGui::PushFont(fBrand, U(18));
        const char* const comparisonTitle = Lx("GPU COMPARISON", "COMPARATIVA GPU", "COMPARA\303\207\303\203O DA GPU", "\320\241\320\240\320\220\320\222\320\235\320\225\320\235\320\230\320\225 GPU");
        const float titleOffset = (ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(comparisonTitle).x) * 0.5f;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (std::max)(0.0f, titleOffset));
        ImGui::TextColored(Tone(ImVec4(1, 1, 1, 1)), "%s", comparisonTitle);
        ImGui::PopFont();
        constexpr int compareMetrics[] = {1, 3, 4, 0, 2, 5};
        if (ImGui::BeginTable("##GpuComparison", 2, Grid | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_BordersInnerH))
        {
            ImGui::TableNextColumn();
            heading(Lx("BASELINE", "PERFIL BASE", "REFER\303\212NCIA", "\320\221\320\220\320\227\320\236\320\222\320\253\320\225 \320\224\320\220\320\235\320\235\320\253\320\225"), pal::Green);
            ImGui::TableNextColumn();
            heading(Lx("LIVE METRICS", "M\303\211TRICAS EN VIVO", "M\303\211TRICAS AO VIVO", "\320\242\320\225\320\232\320\243\320\251\320\230\320\225 \320\237\320\236\320\232\320\220\320\227\320\220\320\242\320\225\320\233\320\230"), pal::Green);
            for (int metric : compareMetrics)
            {
                const auto base = reference.values[metric];
                const auto live = state.metrics[metric];
                const char* name = metricNames[metric];
                for (const auto value : {base, live})
                {
                    ImGui::TableNextColumn();
                    ImGui::TextColored(Tone(pal::Text2), "%s", metric == 2 && value && (ImGui::TableGetColumnIndex() == 0 ? reference.capturedBoardPower : state.boardPower) ? Lx("Board power", "Potencia total", "Pot\303\252ncia da placa", "\320\234\320\276\321\211\320\275\320\276\321\201\321\202\321\214 \320\277\320\273\320\260\321\202\321\213") : name);
                    if (value)
                        ImGui::TextColored(Tone(metricColor(metric, *value)), "%.1f %s", *value, metricUnits[metric]);
                    else
                        ImGui::TextUnformatted(Lx("Unavailable", "No disponible", "Indispon\303\255vel", "\320\235\320\265\320\264\320\276\321\201\321\202\321\203\320\277\320\275\320\276"));
                }
                if (base && live && (metric != 2 || reference.capturedBoardPower == state.boardPower))
                    ImGui::TextColored(Tone(pal::Text2), "%+.1f %s", *live - *base, metricUnits[metric]);
                else
                    ImGui::TextDisabled(Lx("Difference unavailable", "Diferencia no disponible", "Diferen\303\247a indispon\303\255vel", "\320\240\320\260\320\267\320\275\320\270\321\206\320\260 \320\275\320\265\320\264\320\276\321\201\321\202\321\203\320\277\320\275\320\260"));
            }
            ImGui::EndTable();
        }
        if (reference.capturing)
        {
            const auto elapsed = std::clamp(now - reference.started, 0.0, 30.0);
            const std::string progress = std::to_string(static_cast<int>(elapsed)) + Lx(" / 30 s", " / 30 s", " / 30 s", " / 30 \321\201");
            ImGui::ProgressBar(static_cast<float>(elapsed / 30), ImVec2(-FLT_MIN, 0), progress.c_str());
        }
        ImGui::BeginDisabled(reference.capturing || pending);
        if (button("##CaptureGpuReference", Lx("CAPTURE BASELINE", "CAPTURAR PERFIL BASE", "CAPTURAR REFER\303\212NCIA", "\320\227\320\220\320\237\320\230\320\241\320\220\320\242\320\254 \320\221\320\220\320\227\320\236\320\222\320\253\320\225 \320\224\320\220\320\235\320\235\320\253\320\225"), false, pal::Orange))
        {
            reference.samples = {};
            reference.counts = {};
            reference.boardPower = state.boardPower;
            reference.started = now;
            reference.capturing = true;
            nextRead = now;
        }
        ImGui::EndDisabled();
        ImGui::TextWrapped(Lx("Differences depend on the workload.", "Las diferencias dependen de la carga de trabajo.", "As diferen\303\247as dependem da carga de trabalho.", "\320\240\320\260\320\267\320\275\320\270\321\206\320\260 \320\267\320\260\320\262\320\270\321\201\320\270\321\202 \320\276\321\202 \320\275\320\260\320\263\321\200\321\203\320\267\320\272\320\270."));

        ImGui::EndTable();
        }
    }

    dirty = draft != state.live;
    Values apply = state.live;
    if (page == 0)
    {
        if (customFan)
            apply.curve = draft.curve;
        if (draft.zero && state.live.zero)
            apply.zero = draft.zero;
    }
    else
        for (int i = 0; i < FieldCount; ++i)
            if (enabled[i] && draft.fields[i] && state.live.fields[i] && state.ranges[i].min < state.ranges[i].max)
                apply.fields[i] = draft.fields[i];

    if (page == 0)
        ImGui::Indent(U(20));
    if (ImGui::BeginTable("##GpuActions", page == 0 ? 2 : 3, Grid,
                         ImVec2(ImGui::GetContentRegionAvail().x - (page == 0 ? U(20) : 0), 0)))
    {
        if (page == 1)
        {
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(Lx("Profiles for this GPU", "Perfiles de esta GPU", "Perfis desta GPU", "\320\237\321\200\320\276\321\204\320\270\320\273\320\270 \321\215\321\202\320\276\320\271 GPU"));
            ImGui::TableNextColumn();
            openSave = button("##GpuSaveProfile", Lx("SAVE PROFILE", "GUARDAR PERFIL", "SALVAR PERFIL", "\320\241\320\236\320\245\320\240\320\220\320\235\320\230\320\242\320\254 \320\237\320\240\320\236\320\244\320\230\320\233\320\254"), false, pal::Green);
            ImGui::TableNextColumn();
            openLoad = button("##GpuLoadProfile", Lx("LOAD PROFILE", "CARGAR PERFIL", "CARREGAR PERFIL", "\320\227\320\220\320\223\320\240\320\243\320\227\320\230\320\242\320\254 \320\237\320\240\320\236\320\244\320\230\320\233\320\254"), false, pal::Green);
        }
        ImGui::TableNextColumn();
        const char* invalid = rejectedNumber ? Lx("Check the number you entered.", "Revisa el n\303\272mero introducido.", "Confira o n\303\272mero inserido.", "\320\237\321\200\320\276\320\262\320\265\321\200\321\214\321\202\320\265 \320\262\320\262\320\265\320\264\321\221\320\275\320\275\320\276\320\265 \321\207\320\270\321\201\320\273\320\276.") : ValidateDraft(state, apply);
        if (!invalid && apply.curve != state.live.curve)
            invalid = state.checkedCurve != apply.curve ? Lx("Waiting for ADLX to validate the curve.", "Esperando la validaci\303\263n de la curva por ADLX.", "Aguardando a valida\303\247\303\243o da curva pelo ADLX.", "\320\236\320\266\320\270\320\264\320\260\320\275\320\270\320\265 \320\277\321\200\320\276\320\262\320\265\321\200\320\272\320\270 \320\272\321\200\320\270\320\262\320\276\320\271 \320\262 ADLX.") :
                !state.curveError.empty() ? state.curveError.c_str() : nullptr;
        ImGui::BeginDisabled(reference.capturing || editingNumber || apply == state.live || invalid);
        if (button("##GpuApply", Lx("APPLY", "APLICAR", "APLICAR", "\320\237\320\240\320\230\320\234\320\225\320\235\320\230\320\242\320\254"), true, pal::Orange))
            action(Action::Apply, apply);
        ImGui::EndDisabled();
        if (invalid)
            ImGui::TextWrapped("%s", AmdGpuMessage(invalid).c_str());
        ImGui::TableNextColumn();
        if (page == 1)
        {
            ImGui::BeginDisabled(reference.capturing || editingNumber);
            requestRestore = button("##GpuRestore", Lx("RESTORE GPU SETTINGS", "RESTAURAR AJUSTES GPU", "RESTAURAR AJUSTES DA GPU", "\320\222\320\236\320\241\320\241\320\242\320\220\320\235\320\236\320\222\320\230\320\242\320\254 \320\235\320\220\320\241\320\242\320\240\320\236\320\231\320\232\320\230 GPU"), false, pal::Green);
            ImGui::EndDisabled();
        }
        if (page == 1)
            ImGui::TableNextColumn();
        ImGui::BeginDisabled(!dirty);
        if (button("##GpuDiscard", Lx("DISCARD EDITS", "DESCARTAR EDICI\303\223N", "DESCARTAR ALTERA\303\207\303\225ES", "\320\236\320\242\320\234\320\225\320\235\320\230\320\242\320\254 \320\237\320\240\320\220\320\222\320\232\320\230"), false, pal::Green))
        {
            draft = state.live;
            dirty = false;
        }
        ImGui::EndDisabled();
        ImGui::EndTable();
    }
    ImGui::EndDisabled();
    ImGui::PopStyleVar();


    if (page == 0)
    {
        ImGui::Unindent(U(20));
        ImGui::Dummy(Sz(0, 10));
    }

    if (!openSave && !openLoad && !requestRestore &&
        !ImGui::IsPopupOpen("###GpuSaveDialog") && !ImGui::IsPopupOpen("###GpuLoadDialog") &&
        !ImGui::IsPopupOpen("###IlionGpuRestore"))
        return;

    const AmdGpuPopupStyle popupStyle;
    if (openSave)
    {
        profileConfirm = false;
        message.clear();
        ImGui::OpenPopup(Lx("SAVE PROFILE###GpuSaveDialog", "GUARDAR PERFIL###GpuSaveDialog", "SALVAR PERFIL###GpuSaveDialog", "\320\241\320\236\320\245\320\240\320\220\320\235\320\230\320\242\320\254 \320\237\320\240\320\236\320\244\320\230\320\233\320\254###GpuSaveDialog"));
    }
    if (openLoad)
    {
        deleteConfirm = renameProfile = false;
        message.clear();
        ImGui::OpenPopup(Lx("GPU PROFILES###GpuLoadDialog", "PERFILES GPU###GpuLoadDialog", "PERFIS DA GPU###GpuLoadDialog", "\320\237\320\240\320\236\320\244\320\230\320\233\320\230 GPU###GpuLoadDialog"));
    }
    constexpr auto DialogFlags = ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings;
    const char* const kinds[] = {Lx("Fans only", "Solo ventiladores", "S\303\263 ventoinhas", "\320\242\320\276\320\273\321\214\320\272\320\276 \320\262\320\265\320\275\321\202\320\270\320\273\321\217\321\202\320\276\321\200\321\213"), Lx("Undervolt only", "Solo undervolt", "S\303\263 undervolt", "\320\242\320\276\320\273\321\214\320\272\320\276 \320\260\320\275\320\264\320\265\321\200\320\262\320\276\320\273\321\214\321\202"), Lx("Full profile", "Perfil completo", "Perfil completo", "\320\237\320\276\320\273\320\275\321\213\320\271 \320\277\321\200\320\276\321\204\320\270\320\273\321\214")};
    constexpr ImVec2 dialogButton{168, 38};
    const auto dialogStatus = [&]
    {
        ImGui::TextUnformatted(selected ? selected->name.c_str() : Lx("AMD GPU", "GPU AMD", "GPU AMD", "GPU AMD"));
        if (!message.empty())
        {
            const bool saveResult = displayed->action == Action::Save && displayed->message == message;
            if (saveResult)
                ImGui::PushStyleColor(ImGuiCol_Text, Tone(message == "Perfil guardado." ? pal::Green : pal::Orange));
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + U(580));
            ImGui::TextWrapped("%s", AmdGpuMessage(message).c_str());
            ImGui::PopTextWrapPos();
            if (saveResult)
                ImGui::PopStyleColor();
        }
    };
    if (ImGui::BeginPopupModal(Lx("SAVE PROFILE###GpuSaveDialog", "GUARDAR PERFIL###GpuSaveDialog", "SALVAR PERFIL###GpuSaveDialog", "\320\241\320\236\320\245\320\240\320\220\320\235\320\230\320\242\320\254 \320\237\320\240\320\236\320\244\320\230\320\233\320\254###GpuSaveDialog"), nullptr, DialogFlags))
    {
        dialogStatus();
        ImGui::BeginDisabled(pending || profileConfirm);
        ImGui::SetNextItemWidth(U(420));
        ImGui::InputText(Lx("Name###GpuSaveName", "Nombre###GpuSaveName", "Nome###GpuSaveName", "\320\230\320\274\321\217###GpuSaveName"), profile, sizeof profile);
        ImGui::SetNextItemWidth(U(420));
        ImGui::Combo(Lx("Type###GpuSaveType", "Tipo###GpuSaveType", "Tipo###GpuSaveType", "\320\242\320\270\320\277###GpuSaveType"), &profileKind, kinds, IM_ARRAYSIZE(kinds));
        ImGui::EndDisabled();
        if (profileConfirm)
            ImGui::Text(Lx("Replace profile \342\200\234%s\342\200\235?", "\302\277Sustituir el perfil \302\253%s\302\273?", "Substituir o perfil \302\253%s\302\273?", "\320\227\320\260\320\274\320\265\320\275\320\270\321\202\321\214 \320\277\321\200\320\276\321\204\320\270\320\273\321\214 \302\253%s\302\273?"), profile);
        ImGui::BeginDisabled(pending || *profile == '\0');
        if (AmdGpuActionButton("##SaveGpuProfile", profileConfirm ? Lx("REPLACE", "SUSTITUIR", "SUBSTITUIR", "\320\227\320\220\320\234\320\225\320\235\320\230\320\242\320\254") : Lx("SAVE", "GUARDAR", "SALVAR", "\320\241\320\236\320\245\320\240\320\220\320\235\320\230\320\242\320\254"), dialogButton, true, pal::Green))
        {
            const bool exists = std::ranges::any_of(state.profiles, [&](const auto& p) { return _stricmp(p.name.c_str(), profile) == 0; });
            if (exists && !profileConfirm)
                profileConfirm = true;
            else if (action(Action::Save, draft, profileConfirm))
                profileConfirm = false;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (AmdGpuActionButton("##CloseSaveGpuProfile", profileConfirm ? Lx("CANCEL", "CANCELAR", "CANCELAR", "\320\236\320\242\320\234\320\225\320\235\320\220") : Lx("CLOSE", "CERRAR", "FECHAR", "\320\227\320\220\320\232\320\240\320\253\320\242\320\254"), dialogButton))
        {
            if (profileConfirm)
                profileConfirm = false;
            else
                ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopupModal(Lx("GPU PROFILES###GpuLoadDialog", "PERFILES GPU###GpuLoadDialog", "PERFIS DA GPU###GpuLoadDialog", "\320\237\320\240\320\236\320\244\320\230\320\233\320\230 GPU###GpuLoadDialog"), nullptr, DialogFlags))
    {
        dialogStatus();
        ImGui::BeginDisabled(pending || deleteConfirm || renameProfile);
        if (ImGui::BeginTable("##GpuStoredProfiles", 3, Grid | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY, Sz(680, 44 + 29 * (std::clamp)(static_cast<int>(state.profiles.size()), 2, 8))))
        {
            ImGui::TableSetupColumn(Lx("Name", "Nombre", "Nome", "\320\230\320\274\321\217"));
            ImGui::TableSetupColumn(Lx("Type", "Tipo", "Tipo", "\320\242\320\270\320\277"));
            ImGui::TableSetupColumn("GPU");
            ImGui::TableHeadersRow();
            for (const auto& entry : state.profiles)
            {
                ImGui::TableNextColumn();
                if (ImGui::Selectable(entry.name.c_str(), entry.name == profile, ImGuiSelectableFlags_SpanAllColumns))
                    strcpy_s(profile, entry.name.c_str());
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(kinds[static_cast<int>(entry.kind)]);
                ImGui::TableNextColumn();
                ImGui::TextWrapped("%s", selected ? selected->name.c_str() : Lx("AMD GPU", "GPU AMD", "GPU AMD", "GPU AMD"));
            }
            ImGui::EndTable();
        }
        ImGui::EndDisabled();
        const bool exists = std::ranges::any_of(state.profiles, [&](const auto& p) { return p.name == profile; });
        if (deleteConfirm)
            ImGui::Text(Lx("Delete profile \342\200\234%s\342\200\235?", "\302\277Eliminar el perfil \302\253%s\302\273?", "Excluir o perfil \302\253%s\302\273?", "\320\243\320\264\320\260\320\273\320\270\321\202\321\214 \320\277\321\200\320\276\321\204\320\270\320\273\321\214 \302\253%s\302\273?"), profile);
        if (renameProfile)
        {
            ImGui::SetNextItemWidth(U(420));
            ImGui::InputText(Lx("New name###GpuNewName", "Nuevo nombre###GpuNewName", "Novo nome###GpuNewName", "\320\235\320\276\320\262\320\276\320\265 \320\270\320\274\321\217###GpuNewName"), newProfile, sizeof newProfile);
        }
        ImGui::BeginDisabled(pending || !exists);
        if (deleteConfirm)
        {
            if (AmdGpuActionButton("##ConfirmGpuDelete", Lx("DELETE", "ELIMINAR", "EXCLUIR", "\320\243\320\224\320\220\320\233\320\230\320\242\320\254"), dialogButton, true, pal::Green) && action(Action::Delete, {}))
                deleteConfirm = false;
        }
        else if (renameProfile)
        {
            if (AmdGpuActionButton("##ConfirmGpuRename", Lx("RENAME", "RENOMBRAR", "RENOMEAR", "\320\237\320\225\320\240\320\225\320\230\320\234\320\225\320\235\320\236\320\222\320\220\320\242\320\254"), dialogButton, true, pal::Green) && action(Action::Rename, {}))
                renameProfile = false;
        }
        else
        {
            if (AmdGpuActionButton("##LoadGpuProfile", Lx("LOAD DRAFT", "CARGAR BORRADOR", "CARREGAR RASCUNHO", "\320\227\320\220\320\223\320\240\320\243\320\227\320\230\320\242\320\254 \320\247\320\225\320\240\320\235\320\236\320\222\320\230\320\232"), dialogButton, true, pal::Green) && action(Action::Load, {}))
                ImGui::CloseCurrentPopup();
            ImGui::SameLine();
            if (AmdGpuActionButton("##RenameGpuProfile", Lx("RENAME", "RENOMBRAR", "RENOMEAR", "\320\237\320\225\320\240\320\225\320\230\320\234\320\225\320\235\320\236\320\222\320\220\320\242\320\254"), dialogButton))
            {
                strcpy_s(newProfile, profile);
                renameProfile = true;
            }
            ImGui::SameLine();
            if (AmdGpuActionButton("##DeleteGpuProfile", Lx("DELETE", "ELIMINAR", "EXCLUIR", "\320\243\320\224\320\220\320\233\320\230\320\242\320\254"), dialogButton))
                deleteConfirm = true;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (AmdGpuActionButton("##CloseLoadGpuProfile", deleteConfirm || renameProfile ? Lx("CANCEL", "CANCELAR", "CANCELAR", "\320\236\320\242\320\234\320\225\320\235\320\220") : Lx("CLOSE", "CERRAR", "FECHAR", "\320\227\320\220\320\232\320\240\320\253\320\242\320\254"), ImVec2(128, 38)))
        {
            if (deleteConfirm || renameProfile)
                deleteConfirm = renameProfile = false;
            else
                ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    static Snapshot confirmed;
    if (requestRestore)
    {
        confirmed = state;
        ImGui::OpenPopup(Lx("Restore GPU settings###IlionGpuRestore", "Restaurar ajustes GPU###IlionGpuRestore", "Restaurar ajustes da GPU###IlionGpuRestore", "\320\222\320\276\321\201\321\201\321\202\320\260\320\275\320\276\320\262\320\273\320\265\320\275\320\270\320\265 \320\275\320\260\321\201\321\202\321\200\320\276\320\265\320\272 GPU###IlionGpuRestore"));
    }
    if (ImGui::BeginPopupModal(Lx("Restore GPU settings###IlionGpuRestore", "Restaurar ajustes GPU###IlionGpuRestore", "Restaurar ajustes da GPU###IlionGpuRestore", "\320\222\320\276\321\201\321\201\321\202\320\260\320\275\320\276\320\262\320\273\320\265\320\275\320\270\320\265 \320\275\320\260\321\201\321\202\321\200\320\276\320\265\320\272 GPU###IlionGpuRestore"), nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings))
    {
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + U(465));
        ImGui::TextWrapped("%s", Lx("Only the defaults reported and validated by ADLX will be applied.", "Solo se aplicar\303\241n los valores predeterminados que ADLX devuelva y valide.", "S\303\263 ser\303\243o aplicados os valores padr\303\243o informados e validados pelo ADLX.", "\320\221\321\203\320\264\321\203\321\202 \320\277\321\200\320\270\320\274\320\265\320\275\320\265\320\275\321\213 \321\202\320\276\320\273\321\214\320\272\320\276 \320\270\321\201\321\205\320\276\320\264\320\275\321\213\320\265 \320\267\320\275\320\260\321\207\320\265\320\275\320\270\321\217, \320\277\320\276\320\273\321\203\321\207\320\265\320\275\320\275\321\213\320\265 \320\270 \320\277\321\200\320\276\320\262\320\265\321\200\320\265\320\275\320\275\321\213\320\265 \321\207\320\265\321\200\320\265\320\267 ADLX."));
        const auto restoreStatus = [&](bool available, bool changed)
        {
            return !available ? Lx("default unavailable", "valor predeterminado no disponible", "valor padr\303\243o indispon\303\255vel", "\320\270\321\201\321\205\320\276\320\264\320\275\320\276\320\265 \320\267\320\275\320\260\321\207\320\265\320\275\320\270\320\265 \320\275\320\265\320\264\320\276\321\201\321\202\321\203\320\277\320\275\320\276") : changed ? Lx("will be restored", "se restaurar\303\241", "ser\303\241 restaurado", "\320\261\321\203\320\264\320\265\321\202 \320\262\320\276\321\201\321\201\321\202\320\260\320\275\320\276\320\262\320\273\320\265\320\275\320\276") : Lx("already at default", "ya coincide con el valor predeterminado", "j\303\241 est\303\241 no padr\303\243o", "\321\203\320\266\320\265 \321\203\321\201\321\202\320\260\320\275\320\276\320\262\320\273\320\265\320\275\320\276 \320\270\321\201\321\205\320\276\320\264\320\275\320\276\320\265 \320\267\320\275\320\260\321\207\320\265\320\275\320\270\320\265");
        };
        bool fieldsToRestore = false;
        for (int i = 0; i < FieldCount; ++i)
        {
            const auto value = confirmed.defaults.fields[i];
            const bool changes = value != confirmed.live.fields[i];
            fieldsToRestore |= value.has_value() && changes;
            if (value)
                ImGui::TextWrapped("%s: %d %s (%s)", labels[i], *value, units[i], restoreStatus(true, changes));
            else
                ImGui::TextWrapped("%s: %s", labels[i], restoreStatus(false, false));
        }
        ImGui::TextWrapped(Lx("Fan curve: %s", "Curva de ventiladores: %s", "Curva das ventoinhas: %s", "\320\232\321\200\320\270\320\262\320\260\321\217 \320\262\320\265\320\275\321\202\320\270\320\273\321\217\321\202\320\276\321\200\320\276\320\262: %s"),
                           restoreStatus(!confirmed.defaults.curve.empty(), confirmed.defaults.curve != confirmed.live.curve));
        ImGui::TextWrapped(Lx("Zero RPM: %s", "Zero RPM: %s", "Zero RPM: %s", "Zero RPM: %s"),
                           restoreStatus(confirmed.defaults.zero.has_value(), confirmed.defaults.zero != confirmed.live.zero));
        const bool unchanged = confirmed.key == state.key && confirmed.live == state.live && confirmed.factory == state.factory &&
                               confirmed.defaults == state.defaults;
        if (!unchanged)
            ImGui::TextUnformatted(Lx("The driver state changed. Reopen the confirmation.", "Cambi\303\263 el estado del controlador. Abre de nuevo la confirmaci\303\263n.", "O estado do driver mudou. Abra a confirma\303\247\303\243o novamente.", "\320\241\320\276\321\201\321\202\320\276\321\217\320\275\320\270\320\265 \320\264\321\200\320\260\320\271\320\262\320\265\321\200\320\260 \320\270\320\267\320\274\320\265\320\275\320\270\320\273\320\276\321\201\321\214. \320\236\321\202\320\272\321\200\320\276\320\271\321\202\320\265 \320\277\320\276\320\264\321\202\320\262\320\265\321\200\320\266\320\264\320\265\320\275\320\270\320\265 \320\267\320\260\320\275\320\276\320\262\320\276."));
        const bool canRestore = fieldsToRestore || (!confirmed.defaults.curve.empty() && confirmed.defaults.curve != confirmed.live.curve) ||
            (confirmed.defaults.zero.has_value() && confirmed.defaults.zero != confirmed.live.zero);
        ImGui::BeginDisabled(!unchanged || pending || state.autoTuning || !canRestore);
        if (AmdGpuActionButton("##GpuConfirmRestore", Lx("CONFIRM", "CONFIRMAR", "CONFIRMAR", "\320\237\320\236\320\224\320\242\320\222\320\225\320\240\320\224\320\230\320\242\320\254"), ImVec2(145, 36), true, pal::Green, 15.f))
        {
            action(Action::Restore, confirmed.defaults);
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (AmdGpuActionButton("##GpuCancelRestore", Lx("CANCEL", "CANCELAR", "CANCELAR", "\320\236\320\242\320\234\320\225\320\235\320\220"), ImVec2(145, 36), false, pal::Green, 15.f))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

}

