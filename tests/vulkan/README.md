<!--
SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
SPDX-License-Identifier: Apache-2.0
-->

# Vulkan compute tests

Run Vulkan compute shaders on the FORMOSA simulator through the FORMOSA Mesa
ICD. The driver exposes an experimental Vulkan 1.1 compute profile; it is not
Vulkan-conformant or certified.

Supported operations include buffer/image transfers, FP32/FP64 compute,
32/64-bit atomics, shared memory, subgroup BASIC/VOTE/BALLOT operations,
timestamps and binary/timeline synchronization. Subgroup width follows the
simulator configuration. Broadcast uses a software ballot fallback.

Current limits:

- Host-visible memory is noncoherent. Flush host writes and invalidate mappings
  before reading device results. Applications requiring HOST_COHERENT need adaptation.
- No graphics, presentation, FP16 shader arithmetic, float atomics or subgroup
  shuffle/arithmetic operations.
- Images have one mip level, no cube/MSAA support and identity component mappings.
- Four descriptor sets, 32 bindings per set and 128 push-constant bytes.
- The default configuration has 64 threads per workgroup. Use `large_sm` for
  workloads requiring larger groups; it uses 32 lanes per warp.

## Pinned driver and regression workflow

On Linux, the default Nix shell includes the FORMOSA Mesa ICD, Vulkan headers,
loader, shader tools and Khronos validation layers. `flake.lock` pins Mesa's
source alongside LLVM and PoCL; no separate Mesa checkout is needed to run tests.
CTS remains a separate package and is not included in the default environment.

Commands below assume direnv has loaded the environment for the current checkout.
From FORMOSA, with the repository's submodules initialized:

```sh
xmake workflow run vulkan --profile=release --jobs=8
```

This builds the host/shaders and runs smoke, validation smoke, unsupported-shader
and device-loss cases on both `simtix.atomic_sm` and `simtix.pipelined_sm`.
The suites are `vulkan.simtix.atomic_sm` and `vulkan.simtix.pipelined_sm`; they are
also selected by the full regression plan. Existing OpenCL and simtix workflows
keep their current suite selections. CTS is not part of this quick workflow.

Equivalent native commands are:

```sh
xmake f -c -m release --workflow=vulkan
xmake build -j8 vulkan-compute
xmake test -D 'vulkan-compute/**'
```

By default the target follows the selected suites; `--vulkan-tests=y` enables it
explicitly and `--vulkan-tests=n` disables it. Use `xmake f -c` to clear a previous
explicit selection. Validation errors fail the native test, and the validation
case also checks that the loader actually loaded the Khronos layer.

To build just the pinned driver: `nix build .#formosa-mesa`. The compatibility
shell `nix develop .#vulkan-compute` is an alias of the default shell.

## Build Mesa locally

Use a Mesa checkout containing the FORMOSA driver, starting from the
`formosa-mesa-src` revision in `flake.lock` when reproducing the pinned build.
The checkouts need not be adjacent. From Mesa:

```sh
nix develop /absolute/path/to/formosa#formosa-mesa
meson setup build-local --native-file "$MESA_NATIVE_FILE"
meson compile -C build-local -j8
meson install -C build-local
```

This shell inherits the package's build dependencies and Meson configuration.
It installs to `outputs/out` by default (`MESA_INSTALL_PREFIX` overrides it).
It uses the pinned Nix SDK, LLVM and PoCL unless local prefixes are supplied.

For automatic loading through direnv, set `FORMOSA_SOURCE_DIR` in Mesa's
`.envrc.local`. Update Mesa's `.envrc` to use the dedicated shell:

```sh
source_env_if_exists ./.envrc.local
: "${FORMOSA_SOURCE_DIR:?Set FORMOSA_SOURCE_DIR in .envrc.local}"
watch_file "$FORMOSA_SOURCE_DIR/flake.nix" "$FORMOSA_SOURCE_DIR/flake.lock"
watch_file "$FORMOSA_SOURCE_DIR"/formosa-mesa/*
watch_file "$FORMOSA_SOURCE_DIR"/formosa-{llvm,pocl}/local-override.sh
use flake "$FORMOSA_SOURCE_DIR#formosa-mesa"
```

Then build and install through that environment:

```sh
direnv allow
meson setup build-local --native-file "$MESA_NATIVE_FILE"
meson compile -C build-local -j8
meson install -C build-local
```

Set `FORMOSA_MESA_PREFIX=/absolute/path/to/mesa/outputs/out` in FORMOSA's
`.envrc.local`, then run `direnv reload`. The shell now selects that installed
ICD. Remove the override to return to the pinned Nix package. Invalid ICDs,
missing libraries and LLVM prefix mismatches produce an environment-loading
error; correct that error before running tests.

Override handling lives with each package in
`formosa-{llvm,pocl,mesa}/local-override.sh`. FORMOSA's `.envrc` loads those
helpers and checks the compiler/driver combination. The Mesa build shell calls
`formosa_setup_mesa_build` from the same file to configure the local build, reusing
the LLVM and PoCL helpers for its build inputs.

For local HAL/REAL/firmware changes, first build and install the SDK in FORMOSA:

```sh
xmake build -j8 formosa-sdk
xmake install -o outputs/sdk formosa-sdk
```

Set `FORMOSA_INSTALL_PREFIX=/absolute/path/to/formosa/outputs/sdk` in Mesa's
`.envrc.local`, then rebuild Mesa. Independent `FORMOSA_LLVM_PREFIX`,
`FORMOSA_RUNTIME_PREFIX` and `FORMOSA_POCL_PREFIX` overrides select the compiler,
runtime and PoCL headers/bitcode respectively. Keep PoCL bitcode compatible with
the selected LLVM. For a direct `nix develop` invocation, export these variables
before entering the shell; for direnv, put them in Mesa's `.envrc.local`.

Use a fresh Mesa build directory after changing prefixes: Meson caches
dependencies and native files. Keep `outputs/toolchains/` and the selected
installations; shader compilation retains the LLVM/runtime chosen when Mesa was
built, independent of the caller's environment. To select local LLVM tools in
FORMOSA too, set the same `FORMOSA_LLVM_PREFIX` alongside `FORMOSA_MESA_PREFIX`.

## Run

From FORMOSA:

```sh
mkdir -p build/vulkan-results
build/bin/lv tests/opencl/run_opencl.lua --sm simtix.pipelined_sm --config large_sm \
  --stats build/vulkan-results/smoke.toml --trace build/vulkan-results/smoke -- \
  build/bin/vulkan-compute build/bin/vulkan-shaders
```

The smoke checks compute, subgroup operations, images, descriptors, transfers
and synchronization against CPU references. A mismatch fails the process.
Use `--sm simtix.atomic_sm` for the atomic model.

Pass `device-lost` after the shader directory to test device-loss propagation.
The separate `unsupported` mode checks rejection of float-atomic shaders.
Run it with validation layers disabled. Enable Khronos validation for supported
shaders with `VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation`; also run smoke
without layers because layers can normalize API arguments.

## Vulkan CTS

The lists under `cts/` select compute, image and subgroup cases. They cover a
subset of CTS; successful selected cases do not establish Vulkan conformance.

```sh
cts="$(nix build .#vulkan-cts --no-link --print-out-paths)/bin/deqp-vk"
build/bin/lv tests/opencl/run_opencl.lua --sm simtix.atomic_sm \
  --system-config tests/vulkan/cts/system.lua --trace build/vulkan-results/cts -- \
  "$cts" --deqp-compute-only=enable \
  --deqp-caselist-file=tests/vulkan/cts/compute.txt \
  --deqp-shadercache-filename=build/vulkan-results/cts-cache.bin \
  --deqp-log-filename=build/vulkan-results/cts.qpa
```

Use `cts/images.txt` or `cts/subgroups.txt` to select the other groups. Keep
`VK_INSTANCE_LAYERS` unset for CTS 1.4.3.3 on this Vulkan 1.1 profile. Subgroup
cases can take substantially longer than the smoke.

For shader diagnostics, set `FSA_COMPUTE_DUMP_NIR=1` or
`FSA_COMPUTE_DUMP_LLVM=/path/to/shader.ll`.
