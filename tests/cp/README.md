<!--
SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
SPDX-License-Identifier: Apache-2.0
-->

# CPU test harness

Build and run every available adapter from the repository root:

```sh
direnv exec . cmake -S . -B build-cp -G Ninja -DCMAKE_BUILD_TYPE=Release \
  '-DENABLE_PROJECTS=simtix;tests' -DTESTS_ENABLE_TESTS=cp -DCP_TEST_CORES=all
direnv exec . cmake --build build-cp -j 8
direnv exec . ctest --test-dir build-cp -L cp --output-on-failure
```

Run individual programs using the LV executable from the same build:

```sh
direnv exec . build-cp/bin/lv tests/cp/riscv-test/run.lua -h
direnv exec . build-cp/bin/lv tests/cp/riscv-test/run.lua --core cp.command_processor
direnv exec . build-cp/bin/lv tests/cp/riscv-test/run.lua --core simtix.scalar_core
direnv exec . build-cp/bin/lv tests/cp/interrupt/run.lua --core cp.command_processor
direnv exec . build-cp/bin/lv tests/cp/prog0/run.lua --core simtix.scalar_core --golden tests/cp/prog0/rv64.golden
direnv exec . build-cp/bin/lv tests/cp/coremark/run.lua --core cp.command_processor
direnv exec . build-cp/bin/lv tests/cp/coremark/run.lua --core simtix.scalar_core --profile validation
direnv exec . build-cp/bin/lv tests/cp/dhrystone/run.lua --core simtix.scalar_core
```

`--core` is a Lua module name, passed directly to `require`. The runner finds
its ELF automatically. Use `--elf` for another ELF; the ISA test also needs a
matching `.tests` file beside it. CTest runs these same scripts.

`prog0` compares 45 memory words against the golden file passed with `--golden`.
Choose `rv32.golden` or `rv64.golden` to match the core. Its fixed
layout (code at zero, completion at `0x7ffc`, results at `0x8000`) is part of the
program: the AUIPC golden depends on instruction addresses. Supply a matching
golden file when using a custom ELF.

## How it works

- `cores/`: modules with the shared [cp.core interface](system.lua), wrapped
  with `lv.sc_module`. They expose capabilities, ports, and `boot(entry)`.
- `platform.lua`: the test platform's memory layout and reset duration in cycles.
- `system.lua`: builds memory and UART, wires the core, and runs an ELF.
- Each program directory owns its sources, runner, result checks, and checker tests.
  Programs select compatible cores by capability; `interrupt/` requires `has_ext_int`.
- `runtime/`: the bare-metal C runtime library, with startup, platform linking,
  UART `printf`/`vprintf`, and nanolibc string functions. Each ISA is compiled once
  and shared by its C test programs.
- `coremark/` and `dhrystone/` are independent benchmark programs;
  `benchmark/` supplies only their measurement mailbox, linker fragment, and checker.
- `runtime_test.lua` checks the shared execution lifecycle using synthetic programs.

## Add a core

1. Copy [a core adapter](cores/simtix/scalar_core.lua) into `cores/<namespace>/<name>.lua` and
   wrap its native LV model. Set `isa`, `num_mem_ports`, `has_ext_int`, and `has_reset`.
2. Implement `clock`, `boot(entry)`, and either `mem_target` or
   `imem_target`/`dmem_target`. These port properties accept memory targets.
   Expose `ext_int` as an `sc.BoolIn` port when supported; the system owns
   its signal and connects it with `core.ext_int(signal)`.
   Accept `reset_n` as an active-low reset signal, or ignore it if the model has
   no reset input (`has_reset = false`). The system owns the signal. For cores
   with `has_reset = true`, it holds reset low for `platform.reset_cycles` clock
   periods during boot; other cores boot without advancing simulation time.
   `boot(entry)` prepares the entry point or trampoline without advancing
   simulation or driving reset. It runs before the reset interval, so preparation
   must survive that reset. The run's cycle budget starts after boot completes.
3. Add `<namespace>.<name>` to the available cores in `CMakeLists.txt` when its
   model is enabled. Rebuild and run with `--core <namespace>.<name>`.

## Add a test program

1. Add `<program>/` with sources and a `run.lua` supporting `-h` and `--core`.
   Follow [the ISA runner](riscv-test/run.lua) to find its ELF.
2. Construct `require("cp.system")("test", args.core)`, then call
   `system:run(elf_path, check, options)`. The checker receives the system and
   elapsed cycles: return `nil` to continue, a truthy result to pass, or raise
   an error to fail. It can read memory or UART output.
3. Add the ELF build and test registration in `<program>/CMakeLists.txt`, then
   `add_subdirectory(<program>)` here. Follow
   [the ISA registration](riscv-test/CMakeLists.txt) to use `CP_SELECTED_CORES`.

C programs can call `add_cp_runtime(rv64im)` and link `cp-runtime-rv64im` to
reuse startup, platform layout, standard headers, and console/string support.
See [the standalone runtime test](runtime/CMakeLists.txt) for a complete example.
The runtime initializes `gp`, `sp`, and BSS, calls `main`, then halts if it returns.
It imposes no program completion or benchmark protocol; the program and its Lua
checker own that contract.

Update `workflows.lua` for CI coverage. Each program owns its result checks;
core adapters are shared across programs.
