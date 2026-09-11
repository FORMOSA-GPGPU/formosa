---
name: develop-runtime
description: Develop Formosa's OpenCL runtime (PoCL, HAL/SDK, firmware). Use when a local override should supply any of those layers.
---

# Develop Runtime

Wire a local OpenCL runtime so the host loads the intended PoCL, HAL/SDK, and firmware. Hierarchy changes use `compose-system` or `integrate-module` first. Architectural metric questions use `run-experiment`; invoke this first only if that experiment needs the local override.

Locate the PoCL checkout from FORMOSA `.envrc.local` / `FORMOSA_POCL_PREFIX`. Run each command through `direnv exec <that-repo-root>`.

## Override map

| Variable | Written in | Consumed by |
|---|---|---|
| `FORMOSA_SOURCE_DIR` | PoCL `.envrc.local` | PoCL flake `#formosa-pocl` |
| `FORMOSA_INSTALL_PREFIX` | PoCL `.envrc.local` | PoCL `Formosa_DIR` — install prefix containing `FormosaConfig.cmake` |
| `POCL_INSTALL_PREFIX` | PoCL `.envrc.local` | PoCL install; default that checkout `outputs/out` |
| `FORMOSA_POCL_PREFIX` | FORMOSA `.envrc.local` | FORMOSA ICD; equals `POCL_INSTALL_PREFIX` |
| `FORMOSA_LLVM_PREFIX` | FORMOSA `.envrc.local` | optional; requires `FORMOSA_POCL_PREFIX` |

No FORMOSA override → Nix PoCL. No `FORMOSA_INSTALL_PREFIX` → Nix SDK.

## Build order

Firmware ships inside the HAL; the SDK install is the firmware step. Start at the first layer whose install does not yet contain the change.

1. FORMOSA SDK — `opencl.debug` `--target install` (`outputs/out`)
2. PoCL — `$cmakeFlags` `--target install` (`POCL_INSTALL_PREFIX`); links `FORMOSA_INSTALL_PREFIX` when set, else Nix SDK
3. FORMOSA direnv — `FORMOSA_POCL_PREFIX` loads that PoCL

## Procedure

1. **Ensure both `.envrc.local` files**
   - Copy from each repo's `.envrc.local.example` if missing.
   - *Criterion*: Both files exist; `FORMOSA_POCL_PREFIX` equals `POCL_INSTALL_PREFIX`.

2. **Install from the first changed layer onward**
   - Execute each remaining install/override step in the build order.
   - After SDK install: PoCL direnv shows `FORMOSA LOCAL SDK OVERRIDE ACTIVE`.
   - After PoCL install: FORMOSA direnv shows `FORMOSA LOCAL POCL OVERRIDE ACTIVE`.
   - *Criterion*: Each remaining prefix exists; consuming direnv status matches that prefix.

3. **Run the host**
   - Default: `run_opencl.lua` from a FORMOSA direnv. It sets `AGENT_SOCKET_PATH` and `config:export()` so the host matches this simulator.
   - Standing `daemon.lua` only when a long-lived simulator is required. Host commands from a FORMOSA direnv. Publish/source/stale: `tests/formosa/daemon.lua` and FORMOSA `.envrc`.
   - *Criterion*: Host ran under the proven ICD. For daemon, FORMOSA direnv shows live `FORMOSA SIMULATOR CONFIG ACTIVE`.

4. **Report the runtime that ran**
   - ICD / PoCL library, SDK if overridden, runner.
   - *Criterion*: Report names those loaded installs.
