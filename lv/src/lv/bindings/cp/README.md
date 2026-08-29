<!--
SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University

SPDX-License-Identifier: Apache-2.0
-->

# RISC-V CPU
RV64GC instruction accurate model.

## File structure
```
.
├── rv64  # riscv implementation
└── tests # riscv tests related
```

## Test
This test is systemc agnostic which verifies the correctness of riscv implementation.

### Address mapping
| Name | Address | Size | Description |
| ---- | ------- | ---- | ----------- |
| Serial | 0x1000| 0x4 | Serial output |
| RAM | 0x2000 | 0x200000 | Memory |
Will need at least 3MB memory.

### Build
On ubuntu 22.04 or 24.04, `make` downloads gnu toolchain and builds `test.elf`.
```sh
# Build test system
$ cmake -B build -G Ninja
$ cmake --build build
# Build test binary
$ make -C src/bindings/cpu/tests
```

### Run test
```sh
# Run with riscv-test binary
$ ./build/bin/cputest src/bindings/cpu/tests/test.elf
```
