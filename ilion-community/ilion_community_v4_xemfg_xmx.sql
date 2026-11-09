-- ILION COMMUNITY v4 (ILION v1.4.0 R3 fase 2): XeMFG XMX (realdody's dlssg-to-xefg-xmx) as a frame generation
-- implementation of community profiles. NOT APPLIED: it needs the user's authorization. Until it is applied, the client
-- keeps XeMFG XMX profiles local (IlionCommunity::kServerHasXeMfgXmx = false) and doesn't publish them.
-- It only adds 'nvngx_xemfg_xmx' to the three closed lists of v2 (ilion_community_v2_fg.sql); nothing else changes.

begin;

alter table public.ilion_profiles drop constraint if exists ilion_profiles_fg_check;
alter table public.ilion_profiles add constraint ilion_profiles_fg_check
    check (fg in ('ninguno', 'xefg', 'fsr_fg', 'dlssg_nativo', 'dlssg_juego', 'nvngx_nukem', 'nvngx_enabler', 'nvngx_ffx',
                  'nvngx_combo', 'nvngx_fglite', 'nvngx_xemfg_xmx', 'dlssg_salida'));

alter table public.ilion_profiles drop constraint if exists ilion_profiles_fg_ruta_combo_check;
alter table public.ilion_profiles add constraint ilion_profiles_fg_ruta_combo_check check (
    (fg = 'ninguno' and fg_ruta in ('ninguno', 'auto'))
    or (fg = 'dlssg_juego' and fg_ruta in ('juego', 'auto'))
    or (fg in ('xefg', 'fsr_fg', 'dlssg_nativo') and fg_ruta in ('optifg', 'streamline', 'fsr31', 'fsr30', 'auto'))
    or (fg in ('nvngx_nukem', 'nvngx_enabler', 'nvngx_ffx', 'nvngx_combo', 'nvngx_fglite', 'nvngx_xemfg_xmx')
        and fg_ruta in ('nvngx', 'streamline', 'optifg', 'fsr31', 'fsr30'))
    or (fg = 'dlssg_salida' and fg_ruta in ('auto', 'optifg', 'streamline', 'fsr31', 'fsr30')));

alter table public.ilion_profiles drop constraint if exists ilion_profiles_multiplier_check;
alter table public.ilion_profiles add constraint ilion_profiles_multiplier_check check (
    (fg = 'ninguno' and multiplier = 0)
    or (fg in ('fsr_fg', 'nvngx_nukem', 'nvngx_ffx', 'nvngx_fglite') and multiplier = 2)
    or (fg in ('xefg', 'dlssg_nativo', 'dlssg_juego', 'dlssg_salida') and multiplier between 2 and 4)
    or (fg in ('nvngx_enabler', 'nvngx_combo', 'nvngx_xemfg_xmx') and multiplier between 2 and 6));

commit;
