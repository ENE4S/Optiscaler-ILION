# Manifiestos de ILION SETUP

Rama que lee **ILION SETUP** al arrancar (y con «Buscar versión nueva de ILION»):

- `base-congelada.json` — versión aprobada de ILION, base congelada de OptiScaler y variantes, con el SHA-256 de cada archivo.
- `catalogo.json` — mods instalables con su versión y su SHA-256 aprobados (formato 1, para ILION SETUP 0.3.x).
- `catalogo-2.json` — el mismo catálogo en formato 2 (ILION SETUP 0.4 o posterior), con recetas descritas por datos para mods nuevos sin publicar otro Setup.

Cada archivo va acompañado de su `.firma` (ECDSA P-256 con la clave de ILION). Setup ignora cualquier manifiesto sin una firma válida.
No editar a mano: se generan y firman con las herramientas de ILION SETUP.
