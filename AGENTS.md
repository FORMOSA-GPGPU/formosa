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
- Add a test only when existing tests cannot catch the regression.
- Every test MUST fail when its protected behavior regresses. Tautological tests
  MUST be discarded.
- Edit `presets.lua`, not generated `CMakePresets.json`.

## Architecture before convenience

Architecture MUST take precedence over local convenience. Shared abstractions
MUST remain architecture- and feature-agnostic; they MUST NOT become
feature-aware merely to simplify the current task.

- Before adding a mechanism, identify its owning architectural layer and first
  use existing schemas, composition, extension points, internal helpers, private
  types, or local configuration. MUST NOT duplicate an existing path. If none
  fits cleanly, identify the design gap before adding plumbing.
- MUST NOT add or expand public parameters, getters/setters, enums, structs,
  types, or configuration fields for one feature, experiment, SM, or
  architecture. Public surface MUST represent the abstraction and be useful
  beyond the current requirement; otherwise keep it private and local.
- Shared code MUST expose generic capabilities and configuration, not concrete
  feature semantics. Feature-specific names, flags, branches, or types MUST NOT
  enter generic layers unless the concept belongs to their abstraction.
- `run_opencl.lua` MUST remain a generic runner. It MUST NOT gain
  architecture-specific options; use generic paths such as `--sm-param`. The
  runner MUST NOT know which features or parameters an SM supports.
- Keep specialization in its owning implementation, make it obvious through
  naming, placement, or comments, and minimize shared-code changes.
- “Minimal change” means the smallest coherent architectural change, not the
  fewest lines. A flag, special-case branch, boolean parameter, or one-off public
  type is not justified by a smaller diff.

### Mandatory design gate

Before adding a public API or type, shared configuration path, or CLI option, ask:

- Does generic code learn a concrete architecture, feature, experiment, or
  implementation's name or semantics?
- Is the public parameter meaningful to most implementations?
- Does the public type only carry one feature's configuration?
- Does a generic runner or shared component gain a feature-specific option?
- Does this duplicate an existing configuration or composition path?
- Am I choosing it mainly for a smaller diff?

If any answer indicates feature-specific plumbing, you MUST reconsider before
proceeding. If generic code learns the name or semantics of a concrete
architecture, feature, experiment, or implementation, assume the design is wrong
unless a clear architectural reason proves otherwise.

## Pointers

- `integrate-module` — Integrate an implementation into Lunaverse
- `spec-to-rtl` — Convert a SystemC/C++ spec into SystemVerilog RTL
- `compose-system` — Compose a Lua system hierarchy
- `run-experiment` — Experiment on Formosa with a controlled setup
- `develop-runtime` — Develop Formosa's OpenCL runtime (PoCL, HAL/SDK, firmware)
