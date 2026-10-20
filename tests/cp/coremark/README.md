<!-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# CoreMark simulation port

From the repository root in the development shell, build the desired profiles
and run them directly with LV:

```sh
xmake build cp-coremark
xmake build cp-coremark-validation
./build/bin/lv tests/cp/coremark/run.lua --core cp.command_processor
./build/bin/lv tests/cp/coremark/run.lua --core simtix.scalar_core --profile validation
```

The build also supplies LV and stages each ELF under its logical runfile path.
The manual commands need neither `--elf` nor an Xmake launcher. With a custom
output directory, use `<builddir>/bin/lv`. To run all four profile/core pairs:

```sh
xmake test 'cp-coremark/**' 'cp-coremark-validation/**'
```

See the [CPU tests](../README.md) for the temporary CMake fallback.

`--profile` defaults to `performance`. Both profiles run 10 iterations.
The reported iterations/s/MHz is `iterations * 1e6 / measured_cycles`.

`upstream/` contains the six unmodified benchmark source files from
[EEMBC CoreMark commit 1f483d5b8316753a742cbf5590caf5bd0a4e4777](https://github.com/eembc/coremark/tree/1f483d5b8316753a742cbf5590caf5bd0a4e4777).
The upstream [license and trademark terms](upstream/LICENSE.md) are retained.
CoreMark is a registered trademark of EEMBC.

`core_portme.h` and `core_portme.c` supply the bare-metal port: one context,
2000 bytes of static data, volatile input seeds, and fixed `ITERATIONS=10`.
The performance build uses seeds `0,0,0x66`; the validation build defines
`VALIDATION_RUN=1` and uses `0x3415,0x3415,0x66`.
Both use the original upstream `main` and benchmark algorithms.

The current cores do not provide a cycle-counter CSR. The port emits the
benchmark start/stop markers; the Lua harness timestamps
them in simulated core cycles. Initialization, validation, and result reporting
are outside the measured interval. The tiny marker/call overhead is included.
`ee_printf` uses the CP runtime library's UART printf backend, which displays the original
report before its measured score. `get_time` and `time_in_secs` still return zero
because there is no target timer. The upstream report therefore prints the
ten-second run warning; the runner measures the real interval separately.
`portable_fini` independently checks the three upstream reference CRCs, final
accumulated CRC, seeds, data size, iteration count, algorithm mask, upstream
error field, and data types before reporting completion.
The ten-iteration final CRC references are `0xfcaf` (performance) and `0xc64e`
(validation), independently verified on a host with GCC at `-O0` and `-O2`.
An incorrect result must fail the test even though upstream `main` returns zero.

The reported iteration rate and rate per MHz are **simulation estimates**.
They are not official, reportable CoreMark scores: the short fixed-iteration
runs do not establish the required ten seconds of target execution. See
[upstream run and reporting rules](https://github.com/eembc/coremark/blob/1f483d5b8316753a742cbf5590caf5bd0a4e4777/README.md#run-rules).
