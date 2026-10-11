# Disabled workflows

These are the GitHub Actions workflows of OptiScaler, unchanged. They were moved here from `.github/workflows` so
that GitHub does not run them in this repository: GitHub only runs workflows found in `.github/workflows`.

| File | Trigger in OptiScaler |
|---|---|
| `build.yml` | Every day at 04:00 UTC, and manual |
| `just_build.yml`, `just_build_no_signature.yml`, `release_debug.yml`, `test.yml` | Manual |
| `clang-format.yml` | Every push and pull request |

All but `clang-format.yml` build the project; several of them also request a code signature and create or update
releases. They belong to the release process of the OptiScaler project and do not apply to ILION. To use one, move
it back to `.github/workflows` and adapt it.
