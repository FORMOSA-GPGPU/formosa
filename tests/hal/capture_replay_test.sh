#!/usr/bin/env bash

# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

set -euo pipefail

if (($# != 6)); then
  echo "Usage: $0 LV RUN_OPENCL RUN_OPENCL_REPLAY HOST_PROGRAM OUT_DIR SM" >&2
  exit 2
fi

LV="$1"
RUN_OPENCL="$2"
RUN_OPENCL_REPLAY="$3"
HOST_PROGRAM="$4"
OUT_DIR="$5"
SM="$6"

CAPTURE_DIR="$OUT_DIR/capture"

# Prefer the local Formosa PoCL install (runtime-test.md) over the nix pin.
REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
if [[ -f "${REPO_ROOT}/outputs/out/etc/OpenCL/vendors/pocl.icd" ]]; then
  export OCL_ICD_VENDORS="${REPO_ROOT}/outputs/out/etc/OpenCL/vendors/pocl.icd"
  export LD_LIBRARY_PATH="${REPO_ROOT}/outputs/out/lib64:${REPO_ROOT}/outputs/out/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
  export POCL_DEVICES="${POCL_DEVICES:-formosa}"
fi

rm -rf "$OUT_DIR"
mkdir -p "$CAPTURE_DIR"

"$LV" "$RUN_OPENCL" \
  --replay-capture "$CAPTURE_DIR" \
  -s "$OUT_DIR/capture.toml" \
  -t "$OUT_DIR/capture" \
  --sm "$SM" \
  -- "$HOST_PROGRAM"

test -s "$CAPTURE_DIR/manifest.txt"
test -s "$CAPTURE_DIR/events.tsv"

for event in scratchpad_write completion_pool_write completion_slot memory_copy_h2d memory_copy_d2h; do
  if ! awk -F '\t' -v event="$event" 'NR > 1 && $2 == event { found = 1 } END { exit found ? 0 : 1 }' \
      "$CAPTURE_DIR/events.tsv"; then
    echo "capture is missing required event: $event" >&2
    exit 1
  fi
done
# Reject dual legacy completion event names in new captures.
if awk -F '\t' 'NR > 1 && ($2 == "memory_copy_completion" || $2 == "wait_completion") { exit 0 } END { exit 1 }' \
    "$CAPTURE_DIR/events.tsv"; then
  echo "capture still emits legacy completion event vocabulary" >&2
  exit 1
fi

"$LV" "$RUN_OPENCL_REPLAY" \
  -s "$OUT_DIR/replay1.toml" \
  -t "$OUT_DIR/replay1" \
  --sm "$SM" \
  "$CAPTURE_DIR"

"$LV" "$RUN_OPENCL_REPLAY" \
  -s "$OUT_DIR/replay2.toml" \
  -t "$OUT_DIR/replay2" \
  --sm "$SM" \
  "$CAPTURE_DIR"

if ! cmp -s "$OUT_DIR/replay1.toml" "$OUT_DIR/replay2.toml"; then
  diff -u "$OUT_DIR/replay1.toml" "$OUT_DIR/replay2.toml" >&2 || true
  exit 1
fi
