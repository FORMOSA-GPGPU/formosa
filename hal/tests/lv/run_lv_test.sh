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
