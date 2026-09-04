#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

set -euo pipefail

COPYRIGHT_YEAR="2026"
COPYRIGHT_HOLDER="CASLab, National Cheng Kung University"
LICENSE_ID="Apache-2.0"
apply=0
lint=0

usage() {
  cat <<'EOF'
Usage:
  bash scripts/reuse_annotate_formosa.sh                 # list files only
  bash scripts/reuse_annotate_formosa.sh --apply [PATH]   # add REUSE headers
  bash scripts/reuse_annotate_formosa.sh --lint [PATH]    # lint eligible files

This script intentionally annotates only a conservative whitelist of
FORMOSA-owned files. It does not annotate vendored/generated/private areas such
as third-party/, Rodinia-derived OpenCL tests, FreeRTOS ports, nanolibc, or
tinyprintf.
EOF
}

paths=()
while (($# > 0)); do
  case "$1" in
    --apply)
      apply=1
      ;;
    --lint)
      lint=1
      ;;
    -h | --help)
      usage
      exit 0
      ;;
    --)
      shift
      paths+=("$@")
      break
      ;;
    -*)
      usage >&2
      exit 2
      ;;
    *)
      paths+=("$1")
      ;;
  esac
  shift
done

if ((apply == 1 && lint == 1)); then
  echo "--apply and --lint are mutually exclusive" >&2
  exit 2
fi

repo_root="$(git rev-parse --show-toplevel)"
cd "$repo_root"

owned_roots=(
  CMakeLists.txt
  FormosaConfig.cmake.in
  README.md
  THIRD_PARTY_NOTICES.md
  flake.nix
  presets.lua
  cmake
  formosa-llvm
  formosa-pocl
  fw
  hal
  libcomm
  lv
  scripts
  simtix
  tests/CMakeLists.txt
  tests/cp
  tests/formosa
  tests/hal
  tests/ipc
  tests/kernel-sim
  tests/opencl/CMakeLists.txt
  tests/opencl/README_REPLAY.md
  tests/opencl/run_opencl.lua
  tests/opencl/run_opencl_replay.lua
  tests/opencl/activation
  tests/opencl/attention
  tests/opencl/batched_gemm
  tests/opencl/bias_relu
  tests/opencl/conv2d
  tests/opencl/embedding
  tests/opencl/gemm
  tests/opencl/gemma
  tests/opencl/gru
  tests/opencl/gustavson_gemm
  tests/opencl/layernorm
  tests/opencl/pooling
  tests/opencl/residual_mlp
  tests/opencl/rmsnorm
  tests/opencl/smoke
  tests/opencl/softmax
  tests/opencl/swiglu
  tests/opencl/transformer_block
  tests/opencl/vecmul
  tests/pfreader
  third-party/DRAMSys.cmake
  third-party/FreeRTOS-Kernel.cmake
  third-party/fmt.cmake
  third-party/perfetto.cmake
  third-party/quill.cmake
  third-party/riscv-tests.cmake
  third-party/softfloat.cmake
  third-party/stb/CMakeLists.txt
  third-party/stb/libstb.c
  third-party/systemc.cmake
  tools
)

excluded_prefixes=(
  .direnv/
  build/
  fw/freertos/
  fw/nanolibc/c/
  fw/nanolibc/cxx/
  fw/tinyprintf/
  tests/baremetal-freertos/
  tests/cp/riscv-test/
  tests/kernel-sim/riscv-test/
  tests/opencl/bfs/
  tests/opencl/gaussian/
  tests/opencl/kmeans/
  tests/opencl/mandelbrot/
  tests/opencl/montecarlo/
  tests/opencl/nn/
  tests/opencl/spmv/
  tests/opencl/transpose/
  tests/opencl/vecadd/
)

is_excluded() {
  local path="$1"
  local prefix
  path="${path#./}"
  for prefix in "${excluded_prefixes[@]}"; do
    [[ "$path" == "$prefix"* ]] && return 0
  done
  return 1
}

is_owned() {
  local path="$1"
  local root
  path="${path#./}"
  for root in "${owned_roots[@]}"; do
    [[ "$path" == "$root" || "$path" == "$root/"* ]] && return 0
  done
  return 1
}

is_header_file() {
  local path="$1"
  case "$path" in
    CMakeLists.txt | */CMakeLists.txt) return 0 ;;
    *.c | *.cc | *.cl | *.cmake | *.cpp | *.h | *.hpp | *.ld | *.lua) return 0 ;;
    *.md | *.nix | *.py | *.S | *.sh | *.sky | *.tcl | *.yml | *.yaml) return 0 ;;
  esac
  return 1
}

has_existing_notice() {
  local path="$1"
  grep -Eq 'SPDX-License-Identifier|Copyright \(C\)|Copyright \(c\)|Copyright [0-9]|SPDX-FileCopyrightText' "$path"
}

restore_shebangs() {
  local path first line shebang tmp
  for path in "$@"; do
    [[ -f "$path" ]] || continue
    first="$(sed -n '1p' "$path")"
    [[ "$first" == '#!'* ]] && continue

    line="$(awk 'NR <= 20 && /^#!/ { print NR; exit }' "$path")"
    [[ -n "$line" ]] || continue

    shebang="$(sed -n "${line}p" "$path")"
    tmp="$(mktemp)"
    {
      printf '%s\n' "$shebang"
      awk -v n="$line" 'NR != n { print }' "$path"
    } > "$tmp"
    cat "$tmp" > "$path"
    rm -f "$tmp"
  done
}

files=()
cl_files=()
other_files=()
lint_files=()
roots=("${owned_roots[@]}")
if ((${#paths[@]} > 0)); then
  roots=("${paths[@]}")
fi

collect_file() {
  local path="$1"
  [[ -f "$path" ]] || return 0
  is_excluded "$path" && return 0
  is_owned "$path" || return 0
  lint_files+=("$path")
  is_header_file "$path" || return 0
  has_existing_notice "$path" && return 0
  files+=("$path")
  case "$path" in
    *.cl) cl_files+=("$path") ;;
    *) other_files+=("$path") ;;
  esac
}

if ((${#paths[@]} > 0)); then
  for path in "${paths[@]}"; do
    if [[ -d "$path" ]]; then
      while IFS= read -r -d '' found; do
        collect_file "$found"
      done < <(find "$path" -type f -print0)
    else
      collect_file "$path"
    fi
  done
else
  while IFS= read -r -d '' path; do
    collect_file "$path"
  done < <(git ls-files -z -- "${roots[@]}")
fi

if ((lint == 1)); then
  if ((${#lint_files[@]} == 0)); then
    exit 0
  fi
  printf '%s\0' "${lint_files[@]}" | xargs -0 reuse lint-file
  exit 0
fi

printf 'REUSE header candidates: %d files\n' "${#files[@]}"
if ((${#files[@]} == 0)); then
  exit 0
fi
printf '%s\n' "${files[@]}"

if ((apply == 0)); then
  cat <<EOF

Dry run only. To apply:
  direnv exec . bash scripts/reuse_annotate_formosa.sh --apply

To lint the same eligible file set:
  direnv exec . bash scripts/reuse_annotate_formosa.sh --lint

The commands that will be used:
  reuse annotate --style cppsingle --year "$COPYRIGHT_YEAR" --copyright "$COPYRIGHT_HOLDER" --license "$LICENSE_ID" --skip-existing <listed .cl files>
  reuse annotate --year "$COPYRIGHT_YEAR" --copyright "$COPYRIGHT_HOLDER" --license "$LICENSE_ID" --skip-existing <listed non-.cl files>
EOF
  exit 0
fi

if ((${#cl_files[@]} > 0)); then
  printf '%s\0' "${cl_files[@]}" |
    xargs -0 reuse annotate \
      --style cppsingle \
      --year "$COPYRIGHT_YEAR" \
      --copyright "$COPYRIGHT_HOLDER" \
      --license "$LICENSE_ID" \
      --skip-existing
fi

if ((${#other_files[@]} > 0)); then
  printf '%s\0' "${other_files[@]}" |
    xargs -0 reuse annotate \
      --year "$COPYRIGHT_YEAR" \
      --copyright "$COPYRIGHT_HOLDER" \
      --license "$LICENSE_ID" \
      --skip-existing
fi

restore_shebangs "${files[@]}"
