<!--
SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University

SPDX-License-Identifier: Apache-2.0
-->

# FORMOSA HAL

The Hardware Abstraction Layer (HAL) and Real Embedded Abstraction Layer (REAL) for the FORMOSA platform.

# Project Layout

```
.
├── include/                     Public HAL headers
│   └── formosa-hal/             HAL header files
├── src/                         Source files for the HAL and REAL
│   ├── common/                  Common source files shared by different platforms
│   ├── hal/                     HAL source files
│   └── real/                    REAL source files and private `real.h`
└── test/                        Test suite for the driver
    └── lv/                      Tests for the lv
```

## Native build and tests

From the repository root in the development environment:

```sh
xmake f -m debug --workflow=hal
xmake test
```

## Install a local SDK

The standalone entry builds firmware and the host SDK without building LV:

```sh
xmake f -F formosa-sdk-xmake.lua -m release -o build-sdk --fw-enable-log=y
xmake -F formosa-sdk-xmake.lua formosa-sdk
xmake install -F formosa-sdk-xmake.lua -o "$PWD/outputs/sdk" formosa-sdk
```

Use an independent `XMAKE_CONFIGDIR` when keeping the full project configured
at the same time. `nix build .#formosa-sdk-xmake` builds the same native recipe
as a Nix package. The default shared runtime still uses the CMake SDK during
the transition.

The installed prefix supplies `lib/cmake/Formosa/FormosaConfig.cmake` for CMake
consumers, including PoCL. This is a consumer export; native Xmake targets build
their own source dependencies.

## Use the SDK with local PoCL

In the PoCL checkout's `.envrc.local`, set absolute paths:

```sh
export FORMOSA_SOURCE_DIR=/path/to/formosa
export FORMOSA_INSTALL_PREFIX=/path/to/formosa/outputs/sdk
export POCL_INSTALL_PREFIX=/path/to/pocl/outputs/out
```

Reload direnv there, check the local SDK override message, then rebuild/install:

```sh
cmake -S . -B build -G Ninja $cmakeFlags
cmake --build build --target install
```

In FORMOSA's `.envrc.local`, select that installed PoCL:

```sh
export FORMOSA_POCL_PREFIX=/path/to/pocl/outputs/out
```

Reload direnv and check the local PoCL override message. Existing OpenCL host
programs then use the new runtime without being recompiled:

```sh
./build/bin/run_opencl.lua ./build/bin/opencl-vecadd
```

Changing firmware requires rebuilding/installing the SDK and relinking/installing
PoCL before running again. Xmake does not edit either `.envrc.local` file.
For CMake SDK production and the full transition policy, see
[the internal fallback guide](../CONTRIBUTING.md#internal-cmake-fallback-during-the-transition).
