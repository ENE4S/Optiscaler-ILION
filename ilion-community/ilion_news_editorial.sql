-- ILION AL DÍA — control editorial (migración ilion_news_editorial)
-- Proyecto: ILION Community (kynhydceunbqhyyafgyi).
-- ESTADO: PREPARADA, NO EJECUTADA. Requiere autorización expresa antes de aplicarla.
--
-- Qué añade (sin tocar ninguna tabla, política, función ni dato de PERFILES / COMUNIDAD):
--   * ilion_news_editorial: entradas manuales del administrador. ILION las lee con la clave pública y solo ve las
--     activas (no ocultas, ya empezadas y sin caducar). Mientras la tabla no exista, ILION funciona todo en automático.
--   * ilion_news_admins: quién puede escribir. Sin políticas: solo se rellena desde el panel de Supabase (rol postgres).
-- Qué permite (desde el panel de Supabase o con una cuenta que esté en ilion_news_admins):
--   * Añadir una noticia propia (block 0 videojuegos, 1 tecnología, 2 mods; title, url https, source).
--   * Modificarla o retirarla (hidden = true, o borrar la fila).
--   * Fijarla arriba de su bloque (pinned = true) y ordenarla (priority, mayor primero).
--   * Ocultar una noticia automática: una fila con hide_url = enlace de esa noticia (title y url pueden quedar vacíos).
--   * Fecha de inicio y caducidad (starts_at, ends_at).
--   * Volver a todo automático: ocultar o borrar las filas, o dejar que caduquen.
--   * sponsored: la columna existe para el futuro (ILION la mostraría como «PATROCINADO»); no se usa en esta prueba.
-- El cliente no lleva credenciales editoriales: escribir exige una sesión de una cuenta administradora (RLS).

create table if not exists public.ilion_news_admins (
    user_id uuid primary key references auth.users (id) on delete cascade,
    created_at timestamptz not null default now()
);
alter table public.ilion_news_admins enable row level security; -- sin políticas: nadie lo lee ni lo escribe con la API

create table if not exists public.ilion_news_editorial (
    id uuid primary key default gen_random_uuid(),
    block smallint not null default 0 check (block between 0 and 2),
    title text check (title is null or char_length(title) between 4 and 200),
    url text check (url is null or url ~ '^https://'),
    source text not null default 'ILION' check (char_length(source) between 1 and 40),
    summary text check (summary is null or char_length(summary) <= 300),      -- resumen propio (español)
    image_url text check (image_url is null or image_url ~ '^https://'),       -- fotografía de la noticia
    pinned boolean not null default false,
    sponsored boolean not null default false,
    hide_url text check (hide_url is null or hide_url ~ '^https?://'),
    priority integer not null default 0 check (priority between -100 and 100),
    starts_at timestamptz not null default now(),
    ends_at timestamptz,
    hidden boolean not null default false,
    created_by uuid default auth.uid(),
    created_at timestamptz not null default now(),
    updated_at timestamptz not null default now(),
    -- una noticia propia (título y enlace) o una ocultación de una automática (hide_url)
    constraint ilion_news_editorial_kind check ((title is not null and url is not null) or hide_url is not null),
    constraint ilion_news_editorial_dates check (ends_at is null or ends_at > starts_at)
);
alter table public.ilion_news_editorial enable row level security;

-- Lectura pública: solo lo activo
create policy ilion_news_editorial_read on public.ilion_news_editorial
    for select to anon, authenticated
    using (not hidden and starts_at <= now() and (ends_at is null or ends_at > now()));

-- Escritura: solo cuentas administradoras (y el panel de Supabase, que no pasa por RLS)
create policy ilion_news_editorial_admin on public.ilion_news_editorial
    for all to authenticated
    using (exists (select 1 from public.ilion_news_admins a where a.user_id = auth.uid()))
    with check (exists (select 1 from public.ilion_news_admins a where a.user_id = auth.uid()));

revoke truncate on public.ilion_news_editorial from anon, authenticated;
revoke all on public.ilion_news_admins from anon, authenticated;

-- ---- Ejemplos (panel de Supabase, SQL Editor) -------------------------------------------------------------------------
-- insert into public.ilion_news_editorial (block, title, url, source, pinned, ends_at)
--   values (1, 'Título propio', 'https://ejemplo.com/noticia', 'ILION', true, now() + interval '3 days');
-- insert into public.ilion_news_editorial (hide_url) values ('https://www.ejemplo.com/noticia-a-ocultar');
-- update public.ilion_news_editorial set hidden = true where id = '<id>';   -- retirar
-- delete from public.ilion_news_editorial;                                   -- volver a todo automático
