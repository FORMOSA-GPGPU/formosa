<!-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# OpenCL performance

`xmake perf` wraps native `xmake test`: Xmake selects tests, builds their targets
and dependencies on demand, schedules jobs, runs correctness validators and owns
output paths. Python only collects stats/provenance and compares results.

Configure the full OpenCL workflow once. Xmake remembers the build directory:

```sh
xmake f -m release --workflow=opencl -o build-opencl
xmake perf -o outputs/perf/base
```

The default includes every non-smoke pipelined OpenCL benchmark. Atomic SM tests
and standalone correctness validators are outside the current perf statistics
schema. Gemma tools have no native test cases. There is no separate perf workflow,
CMake perf adapter, execution manifest or Python scheduler.

Selection uses native test-name patterns (multiple patterns are ORed) and
`-g/--group` (intersected with patterns). Options precede patterns, as in `xmake test`:

```sh
xmake perf --ci -o outputs/perf/ci
xmake perf -j1 -o outputs/perf/kmeans 'opencl-kmeans/*'
xmake perf -g opencl/parboil -o outputs/perf/parboil
xmake perf --ci -g opencl/shoc -o outputs/perf/ci-shoc
xmake perf -o outputs/perf/gto 'opencl-kmeans/*' -- --sm-param=/absolute/path/gto.lua
```

`--ci` restricts the candidates to `xmake/modules/perf/ci_cases.lua`,
the single CI membership list, then intersects with native groups and patterns.
Cases retain their original `opencl/rodinia`, `opencl/parboil` and `opencl/shoc` groups.
Native `xmake test` uses the same execution adapter and family groups, with output
under the active build directory; `--ci` and JSON collection belong to `xmake perf`.

Host arguments, working directories, configurations and numerical validators come
from the existing native test declarations. Options after `--` pass to
`run_opencl.lua`; `--stats`, `--trace` and the host command belong to Xmake and
cannot be overridden. Use absolute paths for runner input files because individual
tests retain their own working directories. Simulation/build concurrency defaults
to Xmake's `os.default_njob()`; use `-j1` to limit local runs.

Timeouts also come from each native test's `run_timeout` (milliseconds), shared
by `xmake test` and `xmake perf`. The adapter adds no timeout when it is unset.
SHOC cases set this through `timeout` in `tests/opencl/shoc/xmake.lua`; FFT allows
20 minutes. Adjust the case declaration for slower machines or larger inputs.

A perf run writes `OUT/<full-test-name>/{stats.toml,stdout.log,stderr.log,case.json}`,
optional `trace.*` and a PoCL cache. After native tests pass, Xmake writes a run
record (`OUT/cases.json`) and automatically collects `OUT/result.json`. A failed
run produces no result JSON; rerunning clears previous stats and case records.

```sh
# Recollect saved raw stats without rerunning simulations.
xmake perf collect -i outputs/perf/base -o outputs/perf/base/result.json
xmake perf compare outputs/perf/base/result.json outputs/perf/gto/result.json \
  -o outputs/perf/comparison
xmake perf compare outputs/perf/base/result.json outputs/perf/gto/result.json \
  -o outputs/perf/cache --metric=System.PipelinedSM0.DCache.bank0.primary_misses
```

The comparator owns `DEFAULT_PLOT_METRICS`: `active_cycles`, `ipc`, `total_instret`.
The default Markdown table includes the seven aliases defined by the collector's
`METRICS` mapping. Compile/transfer-only cases may have zero GPU work; undefined
IPC appears as N/A in the table and is omitted from the IPC plot. `--metric` selects one alias or any collected finite numeric TOML
path (omit a stat node's `.val`) for both the table and PNG. Results retain all
numeric fields; undefined numeric values become JSON null, appear as N/A and
are omitted from plots. Missing/nonnumeric metrics and incompatible case sets,
benchmark inputs or configurations are rejected. Recollection reads only the saved
run's recorded cases, so nested output directories remain separate runs. Zero-to-zero is 0%; zero-to-nonzero has no
percentage delta. The new full native test names require regenerating older CI
baselines. Historical flat stats plus their schema-2 records can still be collected.
Local collection records HEAD; set `CI_COMMIT_SHA` to the original revision when
recollecting historical stats.

CI explicitly uses `--ci`; the default local command is broader. The same entry
point, CI case list, collector and comparator are available in the public repository.
The development shell supplies Python 3.11+ and Matplotlib for collection and plots.
Run `python scripts/perf/test_opencl_perf.py` for the native execution/comparison fixture.
