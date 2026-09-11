# Agent Notes

- Do not assume `direnv` is active in the shell. Check with `direnv status`
  or environment variables such as `DIRENV_DIR` / `IN_NIX_SHELL` before relying
  on the flake environment.
- Prefer running commands through `direnv exec . <command>` so they use the
  repo's `.envrc` environment.
- If `direnv exec . <command>` reports that `.envrc` is blocked, run
  `direnv allow` in the repo first.
- If `direnv` is unavailable or cannot load the environment, fall back to
  `nix develop -c <command>`.

## Repository map

- `lv/` Lunaverse runtime, bindings, stats, tracing
- `simtix/` GPGPU architectural models
- `*/lua/` Lua system and composite modules
- `tests/` Formosa integration tests

## Always-on rules

- Compose existing Lunaverse modules in Lua before adding native code.
- Existing SystemC/C++ gets a thin binding. A new native SystemC is last.
- `LV_SCHEMA` for Lua configuration; `LV_BINDING` for the useful public surface.
- Commit a new test only when existing tests cannot catch a regression of this change.
- Edit `presets.lua`, not generated `CMakePresets.json`.

## Pointers

- `integrate-module` — Integrate an implementation into Lunaverse
- `spec-to-rtl` — Convert a SystemC/C++ spec into SystemVerilog RTL
- `compose-system` — Compose a Lua system hierarchy
- `run-experiment` — Experiment on Formosa with a controlled setup
- `develop-runtime` — Develop Formosa's OpenCL runtime (PoCL, HAL/SDK, firmware)
