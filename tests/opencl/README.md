<!--
SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University

SPDX-License-Identifier: Apache-2.0
-->

# OpenCL suites

| Directory | Contents | Xmake group |
| --- | --- | --- |
| `smoke/` | OpenCL API and execution checks. | `opencl/smoke` |
| `formosa/` | Computational workloads and microbenchmarks maintained by FORMOSA. | `opencl/formosa` |
| `rodinia/` | Rodinia benchmarks. | `opencl/rodinia` |
| `parboil/` | Parboil benchmarks. | `opencl/parboil` |
| `shoc/` | SHOC benchmarks. | `opencl/shoc` |
| [`gemma/`](gemma/README.md) | Model preparation and manual verification. | Manual |

## Build and run

From the repository root in the development environment:

```sh
xmake f -m release --workflow=opencl
xmake test -g 'opencl/**'
xmake test -g 'opencl/rodinia'
```

Native groups classify targets by their source family: `opencl/formosa`,
`opencl/smoke`, `opencl/rodinia`, `opencl/parboil`, and `opencl/shoc`.
Groups are a convenience for interactive selection with `-g`; workflow suites
continue to select full case names through `workflows.lua`, independently of groups.
When a workflow is configured, `-g` further filters its enabled cases.

Full native names combine the host target, optional case and full SM module
name. A quoted case glob such as `xmake test 'opencl-vecadd/**'` selects cases
without changing suite membership. The original BFS, Gaussian, Kmeans and NN
targets use `opencl-bfs`, `opencl-gaussian`, `opencl-kmeans` and `opencl-nn`.

Run an already built host directly through the Lua launcher:

```sh
./build/bin/run_opencl.lua --sm simtix.pipelined_sm ./build/bin/opencl-vecadd
```

For coverage, configure with `xmake f -m coverage --workflow=opencl`, then run
the same `xmake test` commands. Input sizes and numerical checks are the same
in both profiles. Tests run on atomic and pipelined SMs. The runner needs
permission to bind a local Unix socket for host/simulator communication.

For performance runs and comparisons, use
[`xmake perf`](../../scripts/perf/README.md).

## Imported benchmarks

| Suite | Benchmarks |
| --- | --- |
| Rodinia | Backprop, BFS, B+tree, CFD, DWT2D, Gaussian, Hotspot, Hotspot3D, Kmeans, LavaMD, LUD, Myocyte, NN, NW, Particlefilter, Pathfinder, SRAD, Streamcluster |
| Parboil | CUTCP, Histo, LBM, MRI-Gridding, MRI-Q, SAD, SGEMM, SpMV, Stencil, TPACF |
| SHOC | BFS, BusSpeedDownload, BusSpeedReadback, DeviceMemory, FFT, GEMM, KernelCompile, MaxFlops, MD, MD5Hash, QueueDelay, Reduction, S3D, Scan, Sort, SpMV, Stencil2D, Triad |

Sources and licenses are included in each suite directory. See
[third-party notices](../../THIRD_PARTY_NOTICES.md).

This covers the serial OpenCL benchmarks from the pinned upstream versions,
with the following source-license exclusions. CUDA and MPI implementations
are not part of this OpenCL integration suite.

| Suite | Excluded benchmarks | Source notice |
| --- | --- | --- |
| Rodinia | Heartwall, Leukocyte | Bundled `avilib` carries GPL-2.0-or-later terms: [Heartwall](https://github.com/yuhc/gpu-rodinia/blob/9c10d3ea16ddba2ba057cc3951a9efc4c2cc18a4/opencl/heartwall/util/avi/avilib.h), [Leukocyte](https://github.com/yuhc/gpu-rodinia/blob/9c10d3ea16ddba2ba057cc3951a9efc4c2cc18a4/opencl/leukocyte/OpenCL/avilib.h). |
| Rodinia | Hybridsort | [`histogram1024.cl`](https://github.com/yuhc/gpu-rodinia/blob/9c10d3ea16ddba2ba057cc3951a9efc4c2cc18a4/opencl/hybridsort/histogram1024.cl) requires NVIDIA proprietary/EULA permission. |
| Parboil | BFS | The [OpenCL source notice](https://github.com/yuhc/gpu-parboil/blob/ccf3d3126f1754ca85528722f4ced5894ccb852b/benchmarks/bfs/src/opencl_base/kernel.cl) grants use only for educational purposes. |

The tests use small inputs and bounded iteration counts suitable for simulation,
not the suites' full performance datasets. Hosts retain the upstream size and
iteration defaults when adding runtime options; CTest supplies the smaller
values explicitly. The LBM and CFD integration executables instead use local
compile-time bounds. No benchmark options are added to the generic OpenCL runner.

### Upstream versus FORMOSA behavior

The following differences are intentional. These regression workloads and
modified output formats should not be treated as upstream-equivalent performance
runs, including when comparing host I/O time.

| Benchmark | Pinned upstream | FORMOSA integration | Reason |
| --- | --- | --- | --- |
| SHOC Sort input | Keys are `i % 16`. | Same default; CTest enables `--validation-input` for deterministic mixed 32-bit keys. Both modes compare the full result with `std::sort`. | Exercise higher radix digits without changing the default input distribution. |
| SHOC Sort / Scan / Reduction launch geometry | 64 work groups for the local-memory kernels. | Cap groups at `min(64, elements / 512)`: CTest uses 2 / 8 / 8 groups respectively. All original size classes still use 64 groups; Reduction's no-local-memory path remains one work item. | Bound small-input simulation cost. |
| SHOC QueueDelay | Hard-coded 3 passes; ignores the shared `--passes` option. | The executable defaults to 3 and honors `--passes` / `-n`. CTest checks both explicit 1 pass and the default 3, with reduced groups and iterations. | Preserve the standalone repetition default and allow bounded regression runs. |
| Parboil Histo output | BMP visualization. | Width and height followed by all saturated 8-bit histogram bins. | Compare numerical bins directly without image conversion. |
| Parboil MRI-Gridding output | A pass/fail flag. | Element count, full complex grid, and sample-density arrays. | Validate both complex values and densities against the CPU reference. |
| Parboil SAD output | Six sampled macroblock rows. | All macroblock rows and their sub-block/search-position values. | Check complete output and avoid negative sampled-row indices for tiny images. |
| Rodinia CFD workload | 2000 iterations; input mesh determines the cell count. | Integration executables use 2 iterations; CTest supplies 5 real cells padded to 192. | Bound simulation while retaining the 192-lane kernels and testing padding. |
| Parboil LBM workload | A 120x120x150 grid. | Integration executables use 8x8x8; CTest requests 3 steps. | Bound simulation while retaining a nontrivial cavity. |
| SHOC MaxFlops workload | Runtime calibration chooses the repetition factor. | Default execution still calibrates; CTest uses `--repeat-factor 0.15 --elements 2048 --unroll 1 --passes 1 --verify`. | Bound arithmetic work and validate finite results against CPU calculations. |

Numerical checks cover complete output arrays or independent CPU references;
successful process exit alone is insufficient. Rodinia Particlefilter covers
single, double, and naive executables; SHOC BFS covers both algorithms, and bus
transfers cover pinned and unpinned host memory. DWT2D exercises the integer
5/3 transform with explicit and default output names.

## Source changes

The imported sources retain upstream algorithms, with these corrections:

- Rodinia: add successful returns in Backprop and Particlefilter, allocate space
  for filename terminators in DWT2D, and reject NaN/Inf in LUD validation.
  Backprop reduction starts at a two-element span instead of adding each
  element to itself. Myocyte writes CAM communication values to the communication
  buffer and passes time as floating point. CFD accepts positional paths safely
  and uses a valid C++ stream-open check; padded cells copy the last real cell's
  normals instead of reading uninitialized memory. Gaussian NUL-terminates
  matrix paths up to its 200-byte buffer and reports open failures. NN uses its
  filelist argument directly instead of copying it into a 100-byte buffer, and
  joins database paths with `snprintf` into a `PATH_MAX` buffer instead of
  `sprintf` into 128 bytes, so long checkout paths no longer overflow them.
- Parboil SGEMM: declare `int main` and return success from both matrix readers.
- Parboil TPACF: synchronize local histogram initialization before atomics.
  MRI-Gridding fixes ambiguous OpenCL integer overloads, initializes padded
  radix-sort values, and avoids zero-byte spill readbacks. Histo bounds its
  small-input scheduling, sizes its input by image height, and includes overflow
  counts when saturating bins. It uses standard OpenCL `atomic_min`/`atomic_max`
  names instead of unsupported legacy aliases.
  Histo, MRI-Gridding, and SAD expose complete numerical output for validation.
- SHOC: reject NaN/Inf, use absolute reference magnitudes for relative error,
  allow absolute error up to `1e-6` for zero references, and preserve double
  precision in Stencil2D comparisons.
  Sort initializes its shared scan seed once before synchronization and checks
  the complete sorted input. FFT, GEMM, Triad, and MaxFlops check computed values;
  S3D checks all species at every grid point against an independent CPU OpenCL
  reference. MaxFlops passes correctly typed scalar arguments to double kernels.
  Its validation mode uses finite custom-kernel inputs and rejects non-finite
  results, so overflow cannot mask a broken arithmetic operation.
  Bus transfer tests check contents and unmap pinned buffers before releasing them.
  DeviceMemory checks global, constant, local, and image memory contents against
  host calculations.
  BFS UIUC uses atomic accesses consistently for shared cost and visited state:
  Formosa's global atomics bypass L1 and linearize at L2, so mixing cached ordinary
  accesses with atomics can read stale costs or overwrite visited updates. Its
  local queue copy also excludes entries that have not been initialized.
  The graph fixture exercises queue spill, multiple work-groups, converging
  paths, and unreachable vertices.
  SpMV passes its matrix path to the reader directly instead of copying it into
  a 128-byte buffer, so long checkout paths no longer overflow it.
