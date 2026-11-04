-- ILION COMMUNITY — migración incremental ilion_community_v2_fg (Community5)
-- Proyecto: ILION Community (kynhydceunbqhyyafgyi). NO EJECUTADA: pendiente de autorización.
--
-- Qué hace:
--   * Perfiles v2: la implementación de Frame Generation exacta (catálogo técnico de ILION) y su ruta de entrada
--     (fg_ruta). Los perfiles v1 siguen siendo válidos tal como están.
--   * Amplía las listas cerradas: fg (Nukem, Enabler, FFX, Combo, FG Lite, DLSSG nativo), tech (d4r, FSR 2.1), model
--     (presets DLSS), y el multiplicador fijo máximo de cada implementación (hasta 6X solo Enabler y FFX + Enabler).
--   * Restricciones de combinación en el servidor: qué ruta admite cada implementación y su multiplicador máximo. Nada
--     arbitrario: todo son listas cerradas.
--   * La restricción de duplicados incluye la ruta.
-- Qué NO hace: no borra ni modifica filas (la columna nueva tiene un valor por defecto válido para v1), no toca RLS,
-- políticas, el disparador de guardia, el contador ni los permisos.

begin;

-- ---- Versión de formato --------------------------------------------------------------------------------------------
alter table public.ilion_profiles drop constraint if exists ilion_profiles_schema_version_check;
alter table public.ilion_profiles add constraint ilion_profiles_schema_version_check check (schema_version in (1, 2));

-- ---- Ruta de entrada del FG (v1: 'auto' / 'juego' / 'ninguno') --------------------------------------------------------
alter table public.ilion_profiles add column if not exists fg_ruta text not null default 'auto';
alter table public.ilion_profiles drop constraint if exists ilion_profiles_fg_ruta_check;
alter table public.ilion_profiles add constraint ilion_profiles_fg_ruta_check
    check (fg_ruta in ('ninguno', 'optifg', 'streamline', 'nvngx', 'fsr31', 'fsr30', 'juego', 'auto'));

-- ---- Implementaciones de FG ------------------------------------------------------------------------------------------
alter table public.ilion_profiles drop constraint if exists ilion_profiles_fg_check;
alter table public.ilion_profiles add constraint ilion_profiles_fg_check
    check (fg in ('ninguno', 'xefg', 'fsr_fg', 'dlssg_nativo', 'dlssg_juego', 'nvngx_nukem', 'nvngx_enabler', 'nvngx_ffx',
                  'nvngx_combo', 'nvngx_fglite', 'dlssg_salida'));

-- v1 solo conoce sus cinco valores; v2 no usa el valor heredado 'dlssg_salida' ni la ruta 'auto'
alter table public.ilion_profiles drop constraint if exists ilion_profiles_fg_version_check;
alter table public.ilion_profiles add constraint ilion_profiles_fg_version_check check (
    (schema_version = 1 and fg in ('ninguno', 'xefg', 'fsr_fg', 'dlssg_juego', 'dlssg_salida'))
    or (schema_version = 2 and fg <> 'dlssg_salida' and fg_ruta <> 'auto'));

-- Qué ruta admite cada implementación (el mismo catálogo que IlionCommunity::kFgImpls)
alter table public.ilion_profiles drop constraint if exists ilion_profiles_fg_ruta_combo_check;
alter table public.ilion_profiles add constraint ilion_profiles_fg_ruta_combo_check check (
    (fg = 'ninguno' and fg_ruta in ('ninguno', 'auto'))
    or (fg = 'dlssg_juego' and fg_ruta in ('juego', 'auto'))
    or (fg in ('xefg', 'fsr_fg', 'dlssg_nativo') and fg_ruta in ('optifg', 'streamline', 'fsr31', 'fsr30', 'auto'))
    or (fg in ('nvngx_nukem', 'nvngx_enabler', 'nvngx_ffx', 'nvngx_combo', 'nvngx_fglite')
        and fg_ruta in ('nvngx', 'streamline', 'optifg', 'fsr31', 'fsr30'))
    or (fg = 'dlssg_salida' and fg_ruta in ('auto', 'optifg', 'streamline', 'fsr31', 'fsr30')));

-- Multiplicador fijo: 0 sin FG; el máximo conocido de cada implementación
alter table public.ilion_profiles drop constraint if exists ilion_profiles_check;
alter table public.ilion_profiles drop constraint if exists ilion_profiles_multiplier_check;
alter table public.ilion_profiles add constraint ilion_profiles_multiplier_check check (
    (fg = 'ninguno' and multiplier = 0)
    or (fg in ('fsr_fg', 'nvngx_nukem', 'nvngx_ffx', 'nvngx_fglite') and multiplier = 2)
    or (fg in ('xefg', 'dlssg_nativo', 'dlssg_juego', 'dlssg_salida') and multiplier between 2 and 4)
    or (fg in ('nvngx_enabler', 'nvngx_combo') and multiplier between 2 and 6));

-- ---- Escaladores y modelos -----------------------------------------------------------------------------------------
alter table public.ilion_profiles drop constraint if exists ilion_profiles_tech_check;
alter table public.ilion_profiles add constraint ilion_profiles_tech_check
    check (tech in ('fsr4_driver', 'fsr4_dll', 'fsr4_lite', 'hlx_fsr4', 'helixsr', 'dlss', 'dlss_d4r', 'xess', 'fsr31', 'fsr22', 'fsr21'));

alter table public.ilion_profiles drop constraint if exists ilion_profiles_model_check;
alter table public.ilion_profiles add constraint ilion_profiles_model_check
    check (model in ('completo', 'int8lite', 'no_aplica', 'dlss_auto', 'dlss_j', 'dlss_k', 'dlss_l', 'dlss_m', 'dlss_latest'));

-- Los presets DLSS solo con DLSS; FSR 4 con su modelo; el resto sin modelo
alter table public.ilion_profiles drop constraint if exists ilion_profiles_model_tech_check;
alter table public.ilion_profiles add constraint ilion_profiles_model_tech_check check (
    (tech in ('dlss', 'dlss_d4r') and model like 'dlss\_%')
    or (tech in ('fsr4_driver', 'fsr4_dll') and model in ('completo', 'int8lite'))
    or (tech not in ('dlss', 'dlss_d4r', 'fsr4_driver', 'fsr4_dll') and model = 'no_aplica')
    or (schema_version = 1 and tech = 'dlss' and model = 'no_aplica'));

-- ---- Duplicados: la misma configuración con otra ruta es otro perfil ------------------------------------------------
alter table public.ilion_profiles drop constraint if exists ilion_profiles_dup;
alter table public.ilion_profiles add constraint ilion_profiles_dup
    unique (author_id, game_exe, api, arch, gpu, out_w, out_h, mode, tech, model, preset, fg, fg_ruta, multiplier);

commit;

-- ---- Comprobación (solo lectura) -------------------------------------------------------------------------------------
-- select conname, pg_get_constraintdef(oid) from pg_constraint where conrelid = 'public.ilion_profiles'::regclass order by 1;
-- select column_name, data_type, column_default from information_schema.columns where table_name = 'ilion_profiles' and column_name = 'fg_ruta';
