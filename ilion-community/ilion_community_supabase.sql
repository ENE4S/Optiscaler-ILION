-- ILION COMMUNITY — esquema del catálogo comunitario para Supabase (Postgres)
-- Ejecutar una vez en el SQL Editor del proyecto. Contrato con el cliente: OptiScaler/misc/IlionCommunity.h
--
-- Seguridad:
--   * El cliente solo lleva la clave pública (anon). La moderación se hace desde el panel de Supabase, nunca desde ILION.
--   * Lectura pública: solo perfiles aprobados (y los propios, para su autor).
--   * Publicar exige sesión (Supabase Auth). El autor lo fija el servidor (auth.uid()), no el cliente.
--   * El alias es único sin distinguir mayúsculas y queda ligado a la cuenta: nadie publica con el alias de otro.
--   * Una actualización del autor sube la versión y vuelve a revisión; no puede tocar descargas, autor ni fechas.
--   * Las descargas solo cambian por ilion_register_download (perfiles aprobados): una por cuenta y perfil, nunca la del
--     propio autor. Sin sesión se puede descargar, pero no cuenta. El cliente no envía ninguna cifra.
--   * Moderar (aprobar / rechazar) solo desde el panel de Supabase (rol postgres): ningún cliente puede cambiar el estado.
--   * Sin telemetría: no hay tablas de FPS, sesiones ni equipos.

-- gen_random_uuid() es nativo desde PostgreSQL 13: no hace falta pgcrypto (instalarla en public sería un aviso de seguridad)

-- ---- Autores ---------------------------------------------------------------------------------------------------------
create table if not exists public.ilion_authors (
    user_id    uuid primary key references auth.users (id) on delete cascade,
    alias      text not null check (char_length(alias) between 3 and 24 and alias = btrim(alias) and alias !~ '[<>"\\/`{}\[\]$;:|&@]'),
    created_at timestamptz not null default now()
);
create unique index if not exists ilion_authors_alias_ci on public.ilion_authors (lower(alias));

alter table public.ilion_authors enable row level security;
drop policy if exists authors_read on public.ilion_authors;
create policy authors_read on public.ilion_authors for select using (true);
drop policy if exists authors_claim on public.ilion_authors;
create policy authors_claim on public.ilion_authors for insert to authenticated with check (user_id = auth.uid());
-- Sin políticas de update/delete: el alias no se cambia desde el cliente

-- ---- Perfiles --------------------------------------------------------------------------------------------------------
create table if not exists public.ilion_profiles (
    id             uuid primary key default gen_random_uuid(),
    author_id      uuid not null references public.ilion_authors (user_id) on delete cascade,
    schema_version int not null check (schema_version = 1),
    version        int not null default 1,
    name           text not null check (char_length(name) between 3 and 40 and name !~ '[<>"\\/`{}\[\]$;|&]'),
    game_exe       text not null check (game_exe ~ '^[a-z0-9_. -]{1,60}\.exe$'),
    game_name      text not null check (char_length(game_name) between 1 and 80 and game_name !~ '[<>"\\/`{}\[\]$;|&]'),
    api            text not null check (api in ('DX12', 'DX11', 'Vulkan')),
    gpu            text not null check (char_length(gpu) between 3 and 64 and gpu !~ '[<>"\\/`{}\[\]$;|&]'),
    arch           text not null check (arch in ('rdna4', 'rdna3', 'rdna2', 'amd_apu', 'amd_old', 'nv_gtx', 'rtx20', 'rtx30', 'rtx40',
                                                 'rtx50', 'arc_a', 'arc_b', 'intel_xe2', 'intel_other')),
    out_w          int not null check (out_w between 640 and 7680),
    out_h          int not null check (out_h between 480 and 4320),
    mode           text not null check (mode in ('optimo', 'calidad', 'equilibrado', 'rendimiento')),
    tech           text not null check (tech in ('fsr4_driver', 'fsr4_dll', 'fsr4_lite', 'hlx_fsr4', 'helixsr', 'dlss', 'xess', 'fsr31', 'fsr22')),
    model          text not null check (model in ('completo', 'int8lite', 'no_aplica')),
    preset         text not null check (preset in ('nativa', 'ultra_calidad', 'calidad', 'balanceado', 'rendimiento', 'ultra_rendimiento')),
    fg             text not null check (fg in ('ninguno', 'xefg', 'fsr_fg', 'dlssg_juego', 'dlssg_salida')),
    multiplier     int not null check ((fg = 'ninguno' and multiplier = 0) or (fg <> 'ninguno' and multiplier between 2 and 4)),
    content_hash   text not null check (content_hash ~ '^[0-9a-f]{16}$'),
    status         text not null default 'pendiente' check (status in ('pendiente', 'aprobado', 'rechazado')),
    downloads      bigint not null default 0,
    created_at     timestamptz not null default now(),
    updated_at     timestamptz not null default now(),
    -- Control de duplicados: el mismo autor no publica dos veces la misma configuración
    constraint ilion_profiles_dup unique (author_id, game_exe, api, arch, gpu, out_w, out_h, mode, tech, model, preset, fg, multiplier)
);
create index if not exists ilion_profiles_catalog on public.ilion_profiles (status, downloads desc);

-- Lo que el cliente no puede decidir
create or replace function public.ilion_profiles_guard() returns trigger
language plpgsql set search_path = public as $$
declare
    r public.ilion_profiles;
begin
    -- El contador de descargas: solo cambia downloads
    if tg_op = 'UPDATE' and coalesce(current_setting('ilion.counter', true), 'off') = 'on' then
        r := old;
        r.downloads := old.downloads + 1;
        return r;
    end if;
    -- Moderación desde el panel (rol postgres / service_role): sin restricciones
    if auth.uid() is null then
        return new;
    end if;
    if tg_op = 'INSERT' then
        if (select count(*) from public.ilion_profiles where author_id = auth.uid()) >= 50 then
            raise exception 'Límite de 50 perfiles por autor';
        end if;
        new.author_id := auth.uid();
        new.status := 'pendiente';
        new.downloads := 0;
        new.version := 1;
        new.created_at := now();
        new.updated_at := now();
    else
        new.id := old.id;
        new.author_id := old.author_id;
        new.downloads := old.downloads;
        new.created_at := old.created_at;
        new.status := 'pendiente';
        new.version := old.version + 1;
        new.updated_at := now();
    end if;
    return new;
end $$;

drop trigger if exists ilion_profiles_guard on public.ilion_profiles;
create trigger ilion_profiles_guard before insert or update on public.ilion_profiles
    for each row execute function public.ilion_profiles_guard();

alter table public.ilion_profiles enable row level security;
drop policy if exists profiles_read on public.ilion_profiles;
create policy profiles_read on public.ilion_profiles for select using (status = 'aprobado' or author_id = auth.uid());
drop policy if exists profiles_insert_own on public.ilion_profiles;
create policy profiles_insert_own on public.ilion_profiles for insert to authenticated with check (author_id = auth.uid());
drop policy if exists profiles_update_own on public.ilion_profiles;
create policy profiles_update_own on public.ilion_profiles for update to authenticated
    using (author_id = auth.uid()) with check (author_id = auth.uid());
drop policy if exists profiles_delete_own on public.ilion_profiles;
create policy profiles_delete_own on public.ilion_profiles for delete to authenticated using (author_id = auth.uid());

-- ---- Descargas -------------------------------------------------------------------------------------------------------
-- Quién ya contó una descarga de qué perfil (solo para no contarla dos veces). Nadie la lee desde el cliente
create table if not exists public.ilion_downloads (
    profile_id uuid not null references public.ilion_profiles (id) on delete cascade,
    user_id    uuid not null references auth.users (id) on delete cascade,
    created_at timestamptz not null default now(),
    primary key (profile_id, user_id)
);
create index if not exists ilion_downloads_user on public.ilion_downloads (user_id);
alter table public.ilion_downloads enable row level security;
revoke all on public.ilion_downloads from anon, authenticated;

-- Cuenta una descarga de un perfil aprobado: solo con sesión, una vez por cuenta y perfil, nunca la del autor.
-- Devuelve true si la ha contado ahora. Mide popularidad, no rendimiento. Límite conocido: quien cree muchas cuentas de
-- correo puede sumar una descarga por cuenta
drop function if exists public.ilion_register_download(uuid);
create or replace function public.ilion_register_download(pid uuid) returns boolean
language plpgsql security definer set search_path = public as $$
declare
    uid uuid := auth.uid();
    inserted int;
begin
    if uid is null then
        return false;
    end if;
    if not exists (select 1 from public.ilion_profiles where id = pid and status = 'aprobado' and author_id <> uid) then
        return false;
    end if;
    insert into public.ilion_downloads (profile_id, user_id) values (pid, uid) on conflict do nothing;
    get diagnostics inserted = row_count;
    if inserted = 0 then
        return false;
    end if;
    perform set_config('ilion.counter', 'on', true);
    update public.ilion_profiles set downloads = downloads + 1 where id = pid;
    perform set_config('ilion.counter', 'off', true);
    return true;
end $$;
revoke all on function public.ilion_register_download(uuid) from public, anon;
grant execute on function public.ilion_register_download(uuid) to authenticated;

-- ---- Permisos de tabla (las filas las limitan las políticas RLS de arriba) ------------------------------------------
grant select on public.ilion_profiles, public.ilion_authors to anon, authenticated;
grant insert on public.ilion_authors to authenticated;
grant insert, update, delete on public.ilion_profiles to authenticated;
revoke insert, update, delete on public.ilion_profiles, public.ilion_authors from anon;
revoke update, delete on public.ilion_authors from authenticated;

-- ---- Estadísticas básicas (sin telemetría) ----------------------------------------------------------------------------
-- Preparado para futuras estadísticas agregadas: solo recuentos públicos de perfiles aprobados
create or replace view public.ilion_stats with (security_invoker = on) as
select count(*) as perfiles_publicados, coalesce(sum(downloads), 0) as descargas_totales
from public.ilion_profiles where status = 'aprobado';

-- ---- Comprobación (opcional, solo lectura): debe listar 3 tablas con RLS activado y 6 políticas -------------------------
-- select tablename, rowsecurity from pg_tables where schemaname = 'public' and tablename like 'ilion_%';
-- select tablename, policyname, cmd from pg_policies where tablename like 'ilion_%' order by 1, 2;

-- ---- Moderación (desde el SQL Editor o el Table Editor de Supabase; nunca desde ILION) -------------------------------
-- Pendientes:  select id, name, game_name, gpu, created_at from public.ilion_profiles where status = 'pendiente';
-- Aprobar:     update public.ilion_profiles set status = 'aprobado'  where id = '<id>';
-- Rechazar:    update public.ilion_profiles set status = 'rechazado' where id = '<id>';
