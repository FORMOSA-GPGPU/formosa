#!/usr/bin/env bash

# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

set -euo pipefail

# Tracing must not change simulated timing. Run one HAL workload against the
# daemon twice, once with every system.trace switch on and command labels sent
# and once with every switch off and no labels, then require the same end time
# and identical statistics. In a Perfetto build, the fourth argument is the
# hal-trace-slice-check binary, and the traced run's memory copies must be
# slices named by the label.

DAEMON_EXEC=${1:?Daemon executable (lv) not provided}
DAEMON_LUA=${2:?Daemon lua script (daemon.lua) not provided}
TEST_EXEC=${3:?Test executable not provided}
TRACE_CHECK=${4:-}
LABEL=trace-invariance-copy

# Socket paths must stay short, so keep the work directory under TMPDIR.
WORK=$(mktemp -d "${TMPDIR:-/tmp}/formosa-trace.XXXXXX")
DAEMON_PID=
cleanup() {
  if [ -n "${DAEMON_PID}" ]; then
    kill "${DAEMON_PID}" 2>/dev/null || true
    wait "${DAEMON_PID}" 2>/dev/null || true
  fi
  rm -rf "${WORK}"
}
trap cleanup EXIT
cat >"${WORK}/trace_on.lua" <<'EOF'
return { trace = { commands = true, dma = true, cache_operations = true, wg_slots = true } }
EOF
cat >"${WORK}/trace_off.lua" <<'EOF'
return { trace = { commands = false, dma = false, cache_operations = false, wg_slots = false } }
EOF

run() {
  local dir=$1 system_config=$2 label=$3
  mkdir -p "${dir}"
  export AGENT_SOCKET_PATH="${dir}/agent.sock"
  (cd "${dir}" && exec "${DAEMON_EXEC}" "${DAEMON_LUA}" --stats "${dir}/stats.toml" \
    --system-config "${system_config}") >"${dir}/daemon.log" 2>&1 &
  DAEMON_PID=$!
  for _ in $(seq 30); do
    [ -S "${AGENT_SOCKET_PATH}" ] && break
    if ! kill -0 "${DAEMON_PID}" 2>/dev/null; then
      cat "${dir}/daemon.log"
      echo "Error: daemon died unexpectedly"
      exit 1
    fi
    sleep 1
  done
  if [ -n "${label}" ]; then
    FORMOSA_HAL_TEST_LABEL="${label}" "${TEST_EXEC}"
  else
    env -u FORMOSA_HAL_TEST_LABEL "${TEST_EXEC}"
  fi
  # HAL cleanup sends Terminate; the daemon then ends the simulation and writes
  # its statistics and trace.
  wait "${DAEMON_PID}"
  DAEMON_PID=
  if [ ! -s "${dir}/stats.toml" ]; then
    cat "${dir}/daemon.log"
    echo "Error: the daemon wrote no statistics"
    exit 1
  fi
}

run "${WORK}/traced" "${WORK}/trace_on.lua" "${LABEL}"
run "${WORK}/untraced" "${WORK}/trace_off.lua" ""

# Event counters alone miss a uniform delay, so compare when each run ended.
stop_time() {
  sed -n 's/.*\[ *\([0-9]*\) ns\].*Simulation stops.*/\1/p' "$1/daemon.log" | tail -n 1
}
TRACED_END=$(stop_time "${WORK}/traced")
UNTRACED_END=$(stop_time "${WORK}/untraced")
if [ -z "${TRACED_END}" ] || [ "${TRACED_END}" != "${UNTRACED_END}" ]; then
  echo "Error: simulation ended at ${TRACED_END:-?} ns traced, ${UNTRACED_END:-?} ns untraced"
  exit 1
fi

if ! cmp -s "${WORK}/traced/stats.toml" "${WORK}/untraced/stats.toml"; then
  diff -u "${WORK}/untraced/stats.toml" "${WORK}/traced/stats.toml" || true
  echo "Error: tracing changed the simulation statistics"
  exit 1
fi

if [ -n "${TRACE_CHECK}" ]; then
  if [ ! -s "${WORK}/traced/daemon.pftrace" ]; then
    echo "Error: the traced run of a Perfetto build wrote no trace"
    exit 1
  fi
  if ! "${TRACE_CHECK}" "${WORK}/traced/daemon.pftrace" "Memory copies" "${LABEL}"; then
    echo "Error: the traced run did not name its memory copies with the HAL label"
    exit 1
  fi
fi
echo "Tracing kept the statistics and the end time (${TRACED_END} ns) identical"
