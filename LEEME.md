# Manifiestos de ILION SETUP

Rama que lee **ILION SETUP** al arrancar (y con «Buscar versión nueva de ILION»):

- `base-congelada.json` — versión aprobada de ILION, base congelada de OptiScaler y variantes, con el SHA-256 de cada archivo.
- `catalogo.json` — mods instalables con su versión y su SHA-256 aprobados.

Cada archivo va acompañado de su `.firma` (ECDSA P-256 con la clave de ILION). Setup ignora cualquier manifiesto sin una firma válida.
No editar a mano: se generan y firman con las herramientas de ILION SETUP.
