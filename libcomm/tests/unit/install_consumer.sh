#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

# Compile the public-header consumer using only an installed libcomm prefix.
# This is intentionally build-system neutral and is shared by the CMake and
# install gates.

set -euo pipefail

if [ "$#" -ne 1 ]; then
  printf 'usage: %s INSTALL_PREFIX\n' "$0" >&2
  exit 2
fi

prefix=$1
compiler=${CXX:-c++}
consumer_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
scratch=$(mktemp -d)
trap 'rm -rf -- "$scratch"' EXIT

test -f "$prefix/include/libcomm/libcomm.h" || {
  printf 'missing installed libcomm header under %s\n' "$prefix" >&2
  exit 1
}

"$compiler" -std=c++17 -Wall -Wextra \
  -I"$prefix/include" "$consumer_dir/consumer.cc" \
  -o "$scratch/libcomm-consumer"
"$scratch/libcomm-consumer"

printf '%s\n' "installed libcomm consumer passed: $prefix"
