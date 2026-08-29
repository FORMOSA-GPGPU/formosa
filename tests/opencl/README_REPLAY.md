<!--
SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University

SPDX-License-Identifier: Apache-2.0
-->

# OpenCL Replay

This flow records the HAL-visible behavior of an OpenCL run and replays it
inside the simulator. The goal is reproducible simulation results from the same
captured trace.

## Flow

```text
capture:
  OpenCL program -> PoCL -> Formosa HAL -> simulator
                           |
                           +-> dump replay trace

replay:
  replay trace -> run_opencl_replay.lua -> simple.Initiator -> simulator
```

Capture uses the normal OpenCL runtime and Formosa HAL. Replay does not run the
OpenCL program or HAL again; it reads the captured trace and drives the
simulator directly.

## Capture

Use `run_opencl.lua` with `--replay-capture`.

```sh
CAP=/tmp/formosa-vecadd-capture

build/bin/run_opencl.lua \
  --replay-capture "$CAP" \
  -s "$CAP.toml" \
  -t "$CAP" \
  --sm simtix.pipelined_sm \
  -- build/bin/vecadd
```

The capture directory contains:

- `manifest.txt`: simulator configuration values required by replay.
- `events.tsv`: ordered HAL event stream.
- `blobs/`: binary payloads for scratchpad writes and host-memory transfer
  payloads.

`-s` writes simulator stats as TOML. `-t` is the Perfetto trace output prefix.

## Replay

Replay the captured trace with `run_opencl_replay.lua`.

```sh
build/bin/run_opencl_replay.lua \
  -s /tmp/formosa-vecadd-replay.toml \
  -t /tmp/formosa-vecadd-replay \
  --sm simtix.pipelined_sm \
  "$CAP"
```

Replay validates captured device-to-host payloads, including
firmware-managed D2H transfers, with `--check strict` by default. A successful
run prints:

```text
Replay passed: N events
```

Replay prints progress to stderr by default. TTY output uses a single colored
line; non-TTY output uses plain newline-delimited text. Use `--no-progress` to
disable it:

```sh
build/bin/run_opencl_replay.lua \
  --progress-interval 0.25 \
  -s /tmp/formosa-vecadd-replay.toml \
  -t /tmp/formosa-vecadd-replay \
  --sm simtix.pipelined_sm \
  "$CAP"
```

Progress is based on the captured HAL event stream, so it reports the current
event number, sequence number, event type, transfer size for DMA-like events,
and poll counts while waiting for DMA or kernel completion. It does not require
extra capture metadata and does not identify OpenCL kernel names.

### Output Check Modes

`--check strict` is the default mode. It byte-compares every replayed
device-to-host DMA payload against the captured blob. Use this for deterministic
workloads and correctness guards. A mismatch fails replay with a
`strict_mismatch` hint because the simulator produced bytes that differ from the
capture.

`--check unstrict` still replays the same captured HAL event stream, but
device-to-host payload mismatches are reported and ignored. Use this for
perf-only replay when a workload can legally produce non-byte-identical output
under a different scheduler or configuration, for example contended atomics
where the returned old values can be a different valid permutation.

`unstrict` does not prove semantic correctness. It only keeps the fixed replay
trace running so the resulting stats can be used for architecture exploration.

## Determinism Check

Run the same capture multiple times and compare the generated stats.

```sh
BASE=/tmp/formosa-vecadd-replay

build/bin/run_opencl_replay.lua -s "$BASE-1.toml" -t "$BASE-1" --sm simtix.pipelined_sm "$CAP"
build/bin/run_opencl_replay.lua -s "$BASE-2.toml" -t "$BASE-2" --sm simtix.pipelined_sm "$CAP"
build/bin/run_opencl_replay.lua -s "$BASE-3.toml" -t "$BASE-3" --sm simtix.pipelined_sm "$CAP"

cmp -s "$BASE-1.toml" "$BASE-2.toml"; echo replay1_vs_2=$?
cmp -s "$BASE-1.toml" "$BASE-3.toml"; echo replay1_vs_3=$?
```

`cmp` exit code `0` means the replay stats are byte-identical.

## Notes

- Capture and replay stats do not need to match exactly. Capture includes the
  OpenCL runtime and host/socket timing; replay drives the captured HAL trace
  directly inside the simulator.
- `completion_slot` events wait for the replayed simulator to complete. Replay
  does not force the poll count observed during capture, so faster or slower
  simulator changes can show up in replay stats.
- The reproducibility guarantee is for replaying the same capture multiple
  times with the same simulator configuration.
- The replay path is selected internally with `System(..., { replay = true })`.
  Users should call `run_opencl_replay.lua` rather than setting this manually.
- Use the same SM implementation for capture and replay when comparing stats,
  for example `--sm simtix.pipelined_sm`.
