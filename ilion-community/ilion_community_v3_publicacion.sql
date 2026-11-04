-- ILION COMMUNITY — migración ilion_community_v3_publicacion (Community7)
-- Proyecto: ILION Community (kynhydceunbqhyyafgyi). APLICADA el 2026-11-04 (migración 20261104073434), autorizada por el
-- usuario tras un ensayo en transacción deshecha; pruebas de seguridad repetidas después.
--
-- Publicación inmediata sin aprobación previa, sin quitar ningún control de seguridad:
--   * Al publicar (insert de un usuario con sesión), el servidor pone status = 'aprobado': el perfil es visible al momento
--     en el catálogo (la política de lectura ya muestra los 'aprobado').
--   * Al actualizar su perfil, el autor lo deja publicado ('aprobado'); si la administración lo retiró ('rechazado'), sigue
--     retirado: editarlo no lo vuelve a publicar.
--   * Límites de frecuencia en el servidor: como máximo 10 publicaciones nuevas por autor y hora, y una actualización del
--     mismo perfil cada 20 segundos. Se mantiene el máximo de 50 perfiles por autor.
--   * La administración retira un perfil abusivo desde el panel con status = 'rechazado' (el cliente lo muestra como
--     «Retirado por moderación»); deja de ser visible al instante.
-- No cambia: RLS, políticas, permisos, listas cerradas de campos y tamaños (ilion_community_v1 / v2_fg), el contador de
-- descargas ni la autoría (el servidor sigue fijando author_id = auth.uid()). No modifica filas existentes.

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
    -- Administración desde el panel (rol postgres / service_role): sin restricciones
    if auth.uid() is null then
        return new;
    end if;
    if tg_op = 'INSERT' then
        if (select count(*) from public.ilion_profiles where author_id = auth.uid()) >= 50 then
            raise exception 'Límite de 50 perfiles por autor';
        end if;
        if (select count(*) from public.ilion_profiles where author_id = auth.uid() and created_at > now() - interval '1 hour') >= 10 then
            raise exception 'Demasiadas publicaciones seguidas: espera un poco antes de publicar otra';
        end if;
        new.author_id := auth.uid();
        new.status := 'aprobado'; -- publicación inmediata
        new.downloads := 0;
        new.version := 1;
        new.created_at := now();
        new.updated_at := now();
    else
        if old.updated_at > now() - interval '20 seconds' then
            raise exception 'Actualización demasiado seguida: espera unos segundos';
        end if;
        new.id := old.id;
        new.author_id := old.author_id;
        new.downloads := old.downloads;
        new.created_at := old.created_at;
        new.status := case when old.status = 'rechazado' then 'rechazado' else 'aprobado' end;
        new.version := old.version + 1;
        new.updated_at := now();
    end if;
    return new;
end $$;

comment on function public.ilion_profiles_guard() is
  'Publicación inmediata (status aprobado) con límites de frecuencia; lo retirado por la administración (rechazado) sigue retirado.';

-- ---- Comprobación (solo lectura) -------------------------------------------------------------------------------------
-- select pg_get_functiondef('public.ilion_profiles_guard'::regproc);
-- ---- Retirar un perfil abusivo (panel de Supabase, SQL Editor) -------------------------------------------------------
-- update public.ilion_profiles set status = 'rechazado' where id = '<id>';
