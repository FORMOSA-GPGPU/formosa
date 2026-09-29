<!--
SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
SPDX-License-Identifier: Apache-2.0
-->

# CPU test harness

After building, run from the repository root using your LV executable:

```sh
build/bin/lv tests/cp/riscv-test/run.lua -h
build/bin/lv tests/cp/riscv-test/run.lua --core cp.command_processor
build/bin/lv tests/cp/riscv-test/run.lua --core simtix.scalar_core
build/bin/lv tests/cp/interrupt/run.lua --core cp.command_processor
```

`--core` is a Lua module name, passed directly to `require`. The runner finds
its ELF automatically. Use `--elf` for another ELF; the ISA test also needs a
matching `.tests` file beside it. CTest runs these same scripts.

## How it works

- `cores/`: modules with the shared [cp.core interface](system.lua), wrapped
  with `lv.sc_module`. They expose capabilities, ports, and `boot(entry)`.
- `platform.lua`: the test platform's memory layout.
- `system.lua`: builds memory and UART, wires the core, and runs an ELF.
- Each program directory owns its sources, runner, and result checks.
  Programs select compatible cores by capability; `interrupt/` requires `has_ext_int`.

## Add a core

1. Copy [a core adapter](cores/simtix/scalar_core.lua) into `cores/<namespace>/<name>.lua` and
   wrap its native LV model. Set `isa`, `num_mem_ports`, and `has_ext_int`.
2. Implement `clock`, `boot(entry)`, and either `mem_target` or
   `imem_target`/`dmem_target`. These port properties accept memory targets.
   Provide `ext_int` when supported; the system creates and connects its signal.
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

Update `workflows.lua` for CI coverage. Each program owns its result checks;
core adapters are shared across programs.
