#!/usr/bin/env bash

# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

set -euo pipefail

# Get the server executable from the first argument
SERVER_EXEC=${1:?Server executable not provided}
# Get the test executable from the second argument
TEST_EXEC=${2:?Test executable not provided}

# Per-run socket so ctest -j does not collide on a shared path.
AGENT_SOCKET_PATH="$(mktemp "${TMPDIR:-/tmp}/formosa_lv_test.XXXXXX.sock")"
rm -f "${AGENT_SOCKET_PATH}"
export AGENT_SOCKET_PATH
cleanup() {
  kill "${SERVER_PID:-}" 2>/dev/null || true
  wait "${SERVER_PID:-}" 2>/dev/null || true
  rm -f "${AGENT_SOCKET_PATH}"
}
trap cleanup EXIT

# Set required environment variables for formosa-real configuration
export LV_FORMOSA_THREADS_PER_WARP="${LV_FORMOSA_THREADS_PER_WARP:-16}"
export LV_FORMOSA_WARPS_PER_CORE="${LV_FORMOSA_WARPS_PER_CORE:-4}"
export LV_FORMOSA_LOCAL_MEM_SIZE="${LV_FORMOSA_LOCAL_MEM_SIZE:-65536}" # Example: 64KB
export LV_FORMOSA_NUM_SM="${LV_FORMOSA_NUM_SM:-1}"
export LV_FORMOSA_SHARED_CACHE_SIZE="${LV_FORMOSA_SHARED_CACHE_SIZE:-1048576}"
export LV_FORMOSA_CACHE_BLOCK_SIZE="${LV_FORMOSA_CACHE_BLOCK_SIZE:-64}"
export LV_FORMOSA_GLOBAL_MEM_BASE="${LV_FORMOSA_GLOBAL_MEM_BASE:-2147483648}"
export LV_FORMOSA_GLOBAL_MEM_SIZE="${LV_FORMOSA_GLOBAL_MEM_SIZE:-2147483648}"
export LV_FORMOSA_FSA_MMIO_BASE="${LV_FORMOSA_FSA_MMIO_BASE:-8192}"
export LV_FORMOSA_STACK_SIZE_PER_THREAD="${LV_FORMOSA_STACK_SIZE_PER_THREAD:-1024}"

# Start the server in the background
"${SERVER_EXEC}" &
SERVER_PID=$!

# Give the server a moment to start
sleep 1

if ! kill -0 "${SERVER_PID}" 2>/dev/null; then
  echo "mmio_server failed to start" >&2
  exit 1
fi

# Run the specified test executable
"${TEST_EXEC}"
TEST_EXIT_CODE=$?

exit "${TEST_EXIT_CODE}"
