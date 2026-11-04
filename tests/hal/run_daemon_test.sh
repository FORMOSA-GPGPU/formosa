#!/usr/bin/env bash

# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

set -euo pipefail

# Run a HAL integration test using the real daemon flow.
# It starts the lv daemon in the background, waits for its agent socket, and
# then runs the test. HAL reads hardware capabilities from SystemInfo.

# Get the executables and scripts from arguments
DAEMON_EXEC=${1:?Daemon executable (lv) not provided}
DAEMON_LUA=${2:?Daemon lua script (daemon.lua) not provided}
TEST_EXEC=${3:?Test executable not provided}

# Change to repo root
cd "$(dirname "$0")/../.."

DAEMON_SOCKET_DIR=$(mktemp -d "${TMPDIR:-/tmp}/formosa.XXXXXX")
export AGENT_SOCKET_PATH="${DAEMON_SOCKET_DIR}/agent.sock"

# Start daemon in background
DAEMON_COMMAND=("${DAEMON_EXEC}" "${DAEMON_LUA}")
if [ -n "${FORMOSA_DAEMON_CONFIG:-}" ]; then
  DAEMON_COMMAND+=(--config "${FORMOSA_DAEMON_CONFIG}")
fi
echo "Starting daemon: ${DAEMON_COMMAND[*]}"
"${DAEMON_COMMAND[@]}" &
DAEMON_PID=$!

# Cleanup on exit
cleanup() {
  if [ -n "${DAEMON_PID}" ]; then
    echo "Cleaning up daemon (PID: ${DAEMON_PID})..."
    kill "${DAEMON_PID}" 2>/dev/null || true
    wait "${DAEMON_PID}" 2>/dev/null || true
  fi
  rm -f "$AGENT_SOCKET_PATH"
  rmdir "$DAEMON_SOCKET_DIR"
}
trap cleanup EXIT

# Give the daemon time to create its agent socket.
MAX_RETRIES=30
COUNT=0
echo "Waiting for ${AGENT_SOCKET_PATH}..."
while [ ! -S "${AGENT_SOCKET_PATH}" ] && [ "${COUNT}" -lt "${MAX_RETRIES}" ]; do
  if ! kill -0 "${DAEMON_PID}" 2>/dev/null; then
    echo "Error: Daemon died unexpectedly"
    exit 1
  fi
  sleep 1
  COUNT=$((COUNT + 1))
done

if [ ! -S "${AGENT_SOCKET_PATH}" ]; then
  echo "Error: agent socket was not created after ${MAX_RETRIES} seconds"
  exit 1
fi

# Run the test
"${TEST_EXEC}" "${@:4}"
# Successful clients request Terminate through HAL cleanup. Do not interrupt
# the daemon's finalizers with SIGTERM or hide a failed daemon exit.
DAEMON_EXIT_CODE=0
wait "${DAEMON_PID}" || DAEMON_EXIT_CODE=$?
DAEMON_PID=""
exit "${DAEMON_EXIT_CODE}"
