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

After changing `workflows.lua`, regenerate and check the retained CMake presets:

```sh
xmake workflow generate
xmake workflow check
```

For the CMake fallback, use the same workflow definitions and independent output:

```sh
cmake --workflow --preset simtix.pipelined_sm.debug --fresh
cmake --build --preset simtix.pipelined_sm.debug
ctest --preset simtix.pipelined_sm.debug
```

The CMake workflow configures and builds; its tests run through the explicit
CTest command. To generate presets for an affected or complete regression:

```sh
xmake workflow regression --profile=debug --adapter=cmake --generate
xmake workflow regression --profile=coverage --all --adapter=cmake --generate
```

These commands write `CMakeUserPresets.json`; use its `regression.<profile>`
configure/build/test presets. Generators continue to require Xmake.

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

## Internal CMake fallback during the transition

Public PRs use Xmake CI. During the approximately one-to-two-week observation
period after integration into `main`, keep new targets, generated inputs and
tests working in both build systems. Remove CMake only in a separate reviewed
MR after the workflows are proven; expiry does not remove it automatically.

CMake replaces native build/test failures and still uses Xmake generators.
Keep output and installation directories separate when comparing backends:

```sh
cmake --preset opencl.debug -B build-cmake -DCMAKE_INSTALL_PREFIX=outputs/cmake-sdk
cmake --build build-cmake
ctest --test-dir build-cmake --output-on-failure
cmake --install build-cmake
```

The shared Nix/Docker runtime continues to use a CMake-built SDK. The independent
`nix build .#formosa-sdk-xmake` package exercises the native SDK producer.
Component development shells retain CMake and Ninja alongside Xmake.

If an MR's Xmake build/test fails, keep its logs and let the reviewer decide
whether CMake fallback is appropriate. In the MR Pipelines tab, select **Run
pipeline with modified values** and set the `regression-backend` input to
`cmake`. Verify the pipeline uses the same MR head revision. It runs complete
product regression for the profile and must pass the other required checks.
Retrying a job does not change its backend. The next ordinary push uses Xmake
again; GitHub PRs do not have this fallback path. Native Xmake infrastructure
checks are explicitly skipped on fallback and are not claimed as validated.

Local SDK/PoCL overrides remain explicit for either producer.
