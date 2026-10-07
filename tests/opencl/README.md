<!--
SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University

SPDX-License-Identifier: Apache-2.0
-->

# OpenCL suites

| Directory | Contents | CTest label |
| --- | --- | --- |
| `smoke/` | OpenCL API and execution checks. | `smoke` |
| `formosa/` | Computational workloads and microbenchmarks maintained by FORMOSA. | `formosa` |
| `rodinia/` | Rodinia benchmarks. | `rodinia` |
| `parboil/` | Parboil benchmarks. | `parboil` |
| `shoc/` | SHOC benchmarks. | `shoc` |
| [`gemma/`](gemma/README.md) | Model preparation and manual verification. | Manual |

## Build and run

From the repository root:

```sh
direnv exec . cmake --workflow --preset simtix.pipelined_sm.release --fresh
direnv exec . ctest --test-dir build -L '^rodinia$' --output-on-failure
```

Replace `rodinia` with another suite label to select it. `benchmark` selects
Rodinia, Parboil, and SHOC together. Tests run on atomic and pipelined SMs.

## Imported benchmarks

| Suite | Benchmarks |
| --- | --- |
| Rodinia | BFS, Gaussian, Kmeans, NN, Hotspot, Hotspot3D, LUD, NW, Pathfinder, B+tree, SRAD, Particlefilter, DWT2D, Streamcluster, LavaMD |
| Parboil | CUTCP, MRI-Q, SGEMM, SpMV, Stencil |
| SHOC | Stencil2D, SpMV |

Sources and licenses are included in each suite directory. See
[third-party notices](../../THIRD_PARTY_NOTICES.md).

## Source changes

Compute kernels and work-group sizes match upstream. The sources include these
small host fixes:

- Rodinia: add successful returns in Backprop and Particlefilter, allocate space
  for filename terminators in DWT2D, and reject NaN/Inf in LUD validation.
- Parboil SGEMM: declare `int main` and return success from both matrix readers.
- SHOC: reject NaN/Inf, use absolute reference magnitudes for relative error,
  allow absolute error up to `1e-6` for zero references, and preserve double
  precision in Stencil2D comparisons.
