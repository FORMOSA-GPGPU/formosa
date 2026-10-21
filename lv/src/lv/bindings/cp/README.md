<!--
SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University

SPDX-License-Identifier: Apache-2.0
-->

# RISC-V CPU bindings

Build and test the CPU models from the repository root in the development
environment:

```sh
xmake f -m release
xmake test 'cp-*/**' 'scalar-riscv-test*/**' 'lv/cp/**'
```

The [CPU test harness](../../../../../tests/cp/README.md) owns the current
programs, adapter capabilities and address mapping. Its scripts run directly
with `./build/bin/lv tests/cp/<program>/run.lua --core <module>` after building
their ELF targets. See that guide for adding programs or using the temporary
CMake fallback.
