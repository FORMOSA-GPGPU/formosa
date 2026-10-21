<!--
SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
SPDX-License-Identifier: Apache-2.0
-->

# Dhrystone CPU benchmark

Build the [CPU tests](../README.md), then run Dhrystone independently:

```sh
xmake build cp-dhrystone
./build/bin/lv tests/cp/dhrystone/run.lua --core cp.command_processor
./build/bin/lv tests/cp/dhrystone/run.lua --core simtix.scalar_core
xmake test 'cp-dhrystone/**'
```

See the [CPU test guide](../README.md) for the temporary CMake fallback and
custom build directories.

This ports the Dhrystone C 2.2 implementation in the existing
[`riscv-tests` submodule](../../../third-party/riscv-tests/benchmarks/dhrystone),
pinned at `1eb47d946c55f55cab8653c224c2993acc0276bd`. Its header identifies
Reinhold P. Weicker's Dhrystone 2.1 and Steven Pemberton's 2.2 packaging changes.
The upstream license is retained in [LICENSE](LICENSE).

`main.c` derives from that revision's `dhrystone_main.c`. The measured loop and
`Proc_1` through `Proc_5` retain the upstream statements. `dhrystone.c` and
`dhrystone.h` are compiled directly from the submodule. Only setup, timing,
reporting, and validation are ported: automatic records replace `alloca`, the
benchmark mailbox brackets the measured loop, and actual final values are
checked before reporting success. Initialization and validation are outside the
measured interval. The version banner and final-value report use the CP runtime
library's UART printf backend and appear on stdout. Timing and scores are
reported by the runner. A failure reports a nonzero number of mismatched values.

The default is 1,000 iterations (`DHRYSTONE_ITERATIONS` at compile time). Both
translation units are compiled separately with inlining, builtins, and LTO
disabled. This preserves Dhrystone's procedure boundaries and its timed
`strcpy`/`strcmp` calls; the existing `fw/nanolibc/c/string.c` supplies bytewise
string operations. No `register` declarations are requested. Report the compiler,
optimization flags, memory configuration, and iteration count alongside scores.

The runner derives DMIPS/MHz from simulated clock cycles. DMIPS is
Dhrystones/second divided by 1,757; DMIPS/MHz is
`iterations * 1e6 / (cycles * 1757)`. These short deterministic simulation runs
are useful for comparing the supported models under the same build and memory
configuration, rather than for claiming results from a long physical run.

Successful final state includes `Int_Glob = 5`, `Bool_Glob = 1`, global
characters `A`/`B`, `Arr_1_Glob[8] = 7`, and
`Arr_2_Glob[8][7] = iterations + 10`. The records must retain their expected
pointers, discriminants, strings, and integer values 17/18; local integers must
be 5/13/7 with `Enum_Loc = Ident_2` and the original first/second strings.
