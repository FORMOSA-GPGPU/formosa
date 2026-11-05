<!--
SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University

SPDX-License-Identifier: Apache-2.0
-->

# Contributing

Contributions are welcome. Please open a GitHub pull request against the
`main` branch.

Please avoid modifying the following unless requested by a maintainer:

- Vendored code under `third-party/`.
- Generated files without also updating their source configuration.
- Dependency repository URLs.

This repository is synchronized from our internal development repository.
After your contribution is reviewed and accepted internally, it will be
synchronized back to GitHub and your pull request will appear as merged.

This process may take some time. Please do not close the pull request while
internal review is in progress. Once the pull request is approved, do not push
additional commits. If further changes are needed, please open a new pull
request or ask a maintainer for guidance.

## Development workflows

Use Xmake to configure, build and test FORMOSA. The CMake source build has been
removed; SDK consumer exports and external dependency builds remain supported.

`workflows.lua` owns workflows, profiles, suite membership and impact selection.
Modules register native Xmake cases; suites select their full names with globs.
Case paths use `/`, while model names retain their Lua module spelling, such as
`simtix.pipelined_sm`. Convenience groups such as `opencl/rodinia` are independent
of workflow selectors.

Inside the Nix/direnv development environment, configure the workflow and use
native commands for the edit loop:

```sh
xmake f -m debug --workflow=simtix.pipelined_sm
xmake
xmake test
```

Executables and Lua script launchers are in `<builddir>/bin`. Development script
launchers use live sources, so Lua edits take effect without rebuilding.
Workflow-controlled feature options accept `auto`, `y` and `n`; explicit values
win over workflow defaults:

```sh
xmake f -m debug --workflow=opencl --fw-enable-log=y
```

Repeat desired overrides when reconfiguring. Firmware options affect local
artifacts; selecting a local PoCL runtime remains an environment choice. See
[the HAL guide](hal/README.md) for SDK/PoCL development.

To configure, build, perform any required workflow installation, and test in
one command:

```sh
xmake workflow run opencl --profile=debug --jobs=8
xmake workflow run simtix.scalar_core --profile=release
```

`xmake workflow build` performs the same configure/build/install steps without
tests. For a workflow with `install = true`, such as `opencl`, plain `xmake build`
also builds and installs the declared installation targets. Installation happens
as soon as the SDK dependencies finish; another target can still fail later.
Building an individual host target does not install an unrelated SDK.

```sh
xmake f -m debug --workflow=opencl --installdir=outputs/sdk --fw-enable-log=y
xmake build
# SDK is now installed in outputs/sdk.
```

The configure option `--installdir` and the workflow task's `--installdir` select
the same prefix (default `outputs/out`). Explicit
`xmake install -o PREFIX formosa-sdk` overrides it for that invocation.
Installation does not select or rebuild an external PoCL runtime.

Pass native configuration overrides after `--`:

```sh
xmake workflow build opencl --profile=debug -- --fw-enable-log=y
```

Regression planning uses component impact; execution is explicit:

```sh
xmake workflow regression --profile=debug
xmake workflow regression --profile=debug --execute
xmake workflow regression --profile=coverage --all --execute --builddir=build-coverage
```

`--all` selects the complete regression. An ordinary `xmake test` runs only the
current configuration's selected cases. For a custom union, configure
`--test-suites=lv.unit,simtix.scalar_core` instead of `--workflow`. Unknown suites
and selected suites without matching cases fail configuration. Test execution
and dependencies remain native Xmake; overlapping suites run each case once.

Use independent `XMAKE_CONFIGDIR` values as well as separate output directories
for concurrent configurations. `xmake workflow graph` prints the dependency
graph as Mermaid. The isolated workflow configuration/union/failure checks can
be run with `xmake lua xmake/tests/workflow.lua`.

After changing `workflows.lua`, validate its definitions, references and build-option unions:

```sh
xmake workflow check
```

### Command reference

Run these from the repository root in the development environment:

| Operation | Xmake |
| --- | --- |
| Discover workflows and profiles | `xmake workflow list` |
| Configure a workflow | `xmake f -m debug --workflow=lv` |
| Build | `xmake` |
| Run selected tests, building their dependencies | `xmake test` |
| Select individual cases | `xmake test 'lv/unit/systemc/**'` |
| Choose another build directory | `xmake f -o build-xmake --workflow=lv` |
| Build and install the SDK | `xmake build formosa-sdk`, then `xmake install -o outputs/sdk formosa-sdk` |
| Complete regression | `xmake workflow regression --profile=coverage --all --execute` |

## Nested Git worktrees

Without `-P`, Xmake uses the outermost enclosing directory that contains an
`xmake.lua`. In a worktree nested inside another checkout, pass the worktree
directly after the task name of every command; `xmake workflow` forwards it to
the commands it starts:

```sh
xmake f -P . -m debug --workflow=simtix.pipelined_sm
xmake build -P .
xmake workflow -P . run simtix.pipelined_sm --profile=debug
```

## SDK consumers and external builds

FORMOSA source builds and both internal and public CI use Xmake. Add new targets,
generated inputs and tests to their owning Xmake definitions; keep suite selectors
in `workflows.lua`.

The shared Nix/Docker runtime uses the Xmake-built SDK from `nix build .#hal`.
`nix build .#formosa-sdk-xmake` is an alias of that same package. SDK consumer
exports and external PoCL, LLVM/compiler-rt and PyTorch-DLPrim CMake builds remain
supported. CMake and Ninja remain in the root development environment for those
consumers and external builds. Local SDK/PoCL overrides remain explicit; build
tooling does not change the runtime environment.

## Reporting a build or test failure

The contributor or agent should record the commit, platform, command, workflow,
build directory, relevant local overrides and failing logs. Reproduce in a
separate build directory and retain the failing test's stdout/stderr paths
reported by `xmake test -D`. Authors fix the underlying problem; reviewers check
the evidence and successful rerun. Native build/test failures must be resolved
before merging; there is no alternative source-build backend in CI.
