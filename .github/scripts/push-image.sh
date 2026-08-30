#!/usr/bin/env bash

set -euo pipefail

readonly image=$1
readonly log=$2
readonly transient_pattern='HTTP status: (429|5[0-9]{2})|unexpected status.* (429|5[0-9]{2})|toomanyrequests|timeout|timed out|connection reset|unexpected EOF|temporar|service unavailable|internal server error|bad gateway'

for delay in 0 10 20; do
  sleep "$delay"
  if docker push "$image" 2>&1 | tee "$log"; then
    exit 0
  fi
  if ! grep -Eqi "$transient_pattern" "$log"; then
    echo "::error::Non-transient GHCR push failure; not retrying"
    exit 1
  fi
done

echo "::error::GHCR push failed after three transient attempts"
exit 1
