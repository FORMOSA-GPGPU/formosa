# Third-Party Notices

<!--
SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University

SPDX-License-Identifier: Apache-2.0
-->

<!-- REUSE-IgnoreStart -->

This repository contains third-party code and benchmark material. The FORMOSA
license does not replace those upstream licenses. Keep upstream copyright,
license, and attribution notices when redistributing source or binaries.

## Vendored third-party directories and submodules

The following vendored directories are distributed with their own license
notices. Keep the referenced license files when redistributing this repository
or binaries built from it.

| Path | License notice |
| --- | --- |
| `third-party/Catch2` | See `third-party/Catch2/LICENSE.txt`. |
| `third-party/DRAMSys` | See `third-party/DRAMSys/LICENSE.txt`. |
| `third-party/FreeRTOS-Kernel` | See `third-party/FreeRTOS-Kernel/LICENSE.md`; nested portable ports may carry additional notices. |
| `third-party/argparse` | See `third-party/argparse/LICENSE`. |
| `third-party/elfio` | See `third-party/elfio/LICENSE.txt`. |
| `third-party/fmt` | See `third-party/fmt/LICENSE`. |
| `third-party/glm` | See `third-party/glm/copying.txt`. |
| `third-party/perfetto` | See `third-party/perfetto/LICENSE` and its bundled third-party notices. |
| `third-party/quill` | See `third-party/quill/LICENSE`. |
| `third-party/riscv-opcodes` | See `third-party/riscv-opcodes/LICENSE`. |
| `third-party/riscv-tests` | See `third-party/riscv-tests/LICENSE`; nested `env` content may carry additional notices. |
| `third-party/softfloat` | See `third-party/softfloat/COPYING.txt`. |
| `third-party/sol2` | See `third-party/sol2/LICENSE.txt`. |
| `third-party/stb` | `stb_image.h` and `stb_image_write.h` carry their own notice text and are available under either MIT License or public domain/Unlicense terms, at the recipient's option. |
| `third-party/systemc` | See `third-party/systemc/LICENSE` and `third-party/systemc/NOTICE`. |
| `third-party/tomlplusplus` | See `third-party/tomlplusplus/LICENSE`. |

## In-Tree Third-Party and Adapted Source Files

Some third-party or adapted source files are kept directly in FORMOSA source
directories instead of under `third-party/`. Keep their file-level notices when
redistributing source or binaries.

| FORMOSA path | Source / attribution | License / notice |
| --- | --- | --- |
| `fw/freertos` | FreeRTOS RISC-V port/configuration material. Several files retain Amazon.com, Inc. copyright notices. | MIT, as indicated by the file-level `SPDX-License-Identifier: MIT` notices. |
| `tests/baremetal-freertos/portable` | FreeRTOS RISC-V portable layer material. | MIT, as indicated by the file-level `SPDX-License-Identifier: MIT` notices. |
| `tests/baremetal-freertos/freertos_demo/FreeRTOSConfig.h` | FreeRTOS configuration header derived from Amazon FreeRTOS material. | The file retains the upstream Amazon.com, Inc. copyright and license notice. |
| `fw/nanolibc/c` and `fw/nanolibc/cxx` | nanolibc files carrying Google LLC copyright notices. | Apache-2.0, as stated in the file headers. |
| `fw/tinyprintf` | tinyprintf by Kustaa Nyholm, originally published by Spare Time Labs at `http://www.sparetimelabs.com/tinyprintf/tinyprintf.php`. The `cjlano/tinyprintf` distribution preserves the project with LGPL-2.1-or-later and BSD-new license options. | FORMOSA uses tinyprintf under the BSD-new option; see `fw/tinyprintf/LICENSE.BSD-new`. The source files also retain the original LGPL-2.1-or-later notice from the upstream dual-license source. |
| `simtix/src/cores/encoding.h` | Auto-generated RISC-V CSR encoding header from `riscv-opcodes`. | BSD-3-Clause with RISC-V International copyright, as stated in the file header. |

## OpenCL benchmark tests

Some tests under `tests/opencl` are derived from Rodinia Benchmark Suite 3.1.
Some OpenCL test harnesses and file layout were adapted from the Vortex OpenCL
tests, then modified for FORMOSA.

| FORMOSA path | Source / attribution | License / notice |
| --- | --- | --- |
| `tests/opencl/bfs` | Rodinia BFS benchmark, adapted through Vortex `tests/opencl/bfs`. The kernel header identifies Jianbin Fang and the BFS benchmark date. | Rodinia 3-clause BSD-like license from the University of Virginia applies; existing source attribution is retained in the files. |
| `tests/opencl/kmeans` | Rodinia kmeans benchmark, adapted through Vortex `tests/opencl/kmeans`. Several files retain Northwestern University and University of Virginia attribution. | Rodinia 3-clause BSD-like license from the University of Virginia applies. BSD-like Northwestern University license headers are also retained in source files. |
| `tests/opencl/gaussian` | Rodinia Gaussian Elimination benchmark, adapted through Vortex `tests/opencl/guassian`. | Rodinia 3-clause BSD-like license from the University of Virginia applies. AMD APP SDK helper files retain Advanced Micro Devices, Inc. BSD-like license and export-control notice. |
| `tests/opencl/nn` | Rodinia Nearest Neighbor benchmark, adapted through Vortex `tests/opencl/nearn`. `inputgen/hurricanegen.c` identifies the dataset generator as being for Rodinia's Nearest Neighbor benchmark. | Rodinia 3-clause BSD-like license from the University of Virginia applies. AMD APP SDK helper files retain Advanced Micro Devices, Inc. BSD-like license and export-control notice. |

Vortex is licensed under Apache-2.0 and its paper states that Vortex evaluated
a subset of the Rodinia benchmark suite. FORMOSA retains upstream benchmark
notices in these files.

Rodinia 3.1 is distributed under a 3-clause BSD-like license by the University
of Virginia. The license requires retaining the copyright notice, conditions,
and disclaimer in source redistributions, reproducing them in documentation or
other materials for binary redistributions, and not using the University of
Virginia, the Department of Computer Science, or contributor names to endorse
derived products without prior written permission.
<!-- REUSE-IgnoreEnd -->
