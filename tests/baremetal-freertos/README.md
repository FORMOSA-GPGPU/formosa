<!--
SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
SPDX-License-Identifier: Apache-2.0
-->

# Bare-metal and FreeRTOS CPU examples

After the repository's `direnv allow` setup, build and run the examples from the
repository root:

```sh
xmake f -m release -o build-os --test-suites=simtix.scalar_core,lv.bindings
xmake test -j4 'baremetal-demo/*' 'freertos-demo/*'
```

Select a core when running an individual example with `lv` from the same build:

```sh
build-os/bin/lv tests/baremetal-freertos/baremetal_demo/run.lua --core simtix.scalar_core
build-os/bin/lv tests/baremetal-freertos/freertos_demo/run.lua --core simtix.scalar_core
build-os/bin/lv tests/baremetal-freertos/freertos_demo/run.lua --core cp.command_processor
```

Each example has its own directory with sources, `run.lua`, `xmake.lua`, and result
checks. Both require RV64IM; FreeRTOS also requires machine timer interrupts.
The [CPU build helper](../cp/build.lua) registers each example for compatible
cores. Xmake stages the generated ELFs in lv runfiles, where the scripts find
them automatically. Use `--elf` for another ELF and `--max-cycles` to set the
execution cycle limit.

The examples use the [CPU test harness](../cp/README.md). This suite's
[platform](platform.lua) maps RAM at `0x80000000`, UART at `0x10000000`, and the
standard CLINT at `0x02000000`. The layout matches the linker script, UART writer,
and RISC-V port headers. Clock frequency is 100 MHz; FreeRTOS ticks at 1 kHz.

`baremetal_demo` checks the greeting. `freertos_demo` runs two tasks that do not
yield and checks their UART output for at least three task switches and a kernel
tick count that advances by at least three. This checks timer preemption, saved
task contexts, and trap return.

## Add a program

1. Add a directory with sources, `run.lua`, and `xmake.lua`, and include its build
   file from the suite's [xmake.lua](xmake.lua). Follow an
   [existing target](baremetal_demo/xmake.lua): use the `baremetal.riscv`,
   `linker.link_scripts`, and `lv.test` rules, link `baremetal.common`, register
   compatible cores through `tests.cp.build.tests`, and stage the ELF with
   `lv.layout.stage_runfile`.
2. For a FreeRTOS program, supply `FreeRTOSConfig.h`, define a kernel target with
   the `freertos.kernel` rule, and require `has_timer_int`. See
   [freertos_demo/xmake.lua](freertos_demo/xmake.lua). Each program defines its
   kernel target so it can choose its own configuration.
3. Add the program's selectors to [workflows.lua](../../workflows.lua) for CI.

A runner calls `require("baremetal-freertos.runner")(argv, name, check, requirements)`.
The optional requirements list uses the same capability names as the build.
The program's checker returns `nil` while pending or `true` on success, and raises
an error on failure. See the [bare-metal runner](baremetal_demo/run.lua) and
[FreeRTOS runner](freertos_demo/run.lua) for examples.

To add a core, follow the [CPU adapter instructions](../cp/README.md#add-a-core).
The example scripts select adapters through `--core`.
