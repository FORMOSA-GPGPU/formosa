#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

# Phase 0 feasibility probe. This is intentionally outside every production
# target: it checks the RISC-V/Formosa compiler contract without changing the
# build graph.

set -u

usage() {
  cat <<'EOF'
Usage: scripts/toolchain_probe.sh --output PATH [--strict]

Probe RISC-V C/C++/ASM compilers, archiver, an explicit linker script, linker
and objcopy. Tool names can be overridden with RISCV_CC, RISCV_CXX, RISCV_AR,
RISCV_LD, RISCV_OBJCOPY and RISCV_READELF (or the corresponding FORMOSA_*
variables).
FORMOSA_TARGET selects the clang target triple; it defaults to
riscv64-none-elf.  --strict requires one complete explicit RISCV_* or
FORMOSA_* tool set and never falls back to PATH or to the other family.
EOF
}

output=""
strict=0
while [ "$#" -gt 0 ]; do
  case "$1" in
    --output)
      [ "$#" -ge 2 ] || { echo "--output requires a path" >&2; exit 2; }
      output=$2
      shift 2
      ;;
    --strict)
      strict=1
      shift
      ;;
    --help|-h)
      usage
      exit 0
      ;;
    *)
      echo "unknown argument: $1" >&2
      usage >&2
      exit 2
      ;;
  esac
done

if [ -z "$output" ]; then
  echo "--output is required" >&2
  usage >&2
  exit 2
fi

output_dir=$(dirname "$output")
mkdir -p "$output_dir"

target=${FORMOSA_TARGET:-riscv64-none-elf}
march=${FORMOSA_MARCH:-rv64imc_zicsr}
mabi=${FORMOSA_MABI:-lp64}
mcmodel=${FORMOSA_MCMODEL:-medany}

write_failure() {
  local reason=$1
  cat >"$output" <<EOF
status=fail
reason=$reason
compiler=${cc:-missing}
compiler_cxx=${cxx:-missing}
archiver=${ar:-missing}
linker=${ld:-missing}
objcopy=${objcopy:-missing}
readelf=${readelf:-missing}
target=$target
march=$march
mabi=$mabi
mcmodel=$mcmodel
EOF
}

strict_family=""
if [ "$strict" -eq 1 ]; then
  for family in RISCV FORMOSA; do
    any_set=0
    missing=""
    for suffix in CC CXX AR LD OBJCOPY READELF; do
      variable="${family}_${suffix}"
      if [ -n "${!variable:-}" ]; then
        any_set=1
      else
        missing="$missing ${variable}"
      fi
    done
    if [ "$any_set" -eq 1 ]; then
      if [ -n "$missing" ]; then
        write_failure "strict-missing:${missing# }"
        echo "toolchain probe: strict mode requires all ${family}_* tools; missing:${missing}" >&2
        exit 1
      fi
      if [ -n "$strict_family" ]; then
        write_failure "strict-multiple-families"
        echo "toolchain probe: strict mode received both RISCV_* and FORMOSA_* tools" >&2
        exit 1
      fi
      strict_family=$family
    fi
  done
  if [ -z "$strict_family" ]; then
    write_failure "strict-explicit-toolchain-required"
    echo "toolchain probe: strict mode requires RISCV_* or FORMOSA_* tools" >&2
    exit 1
  fi
fi

pick_tool() {
  # $1 is the preferred environment variable, $2 is the fallback variable,
  # and the remaining arguments are command names searched in PATH.
  local preferred_var=$1
  local fallback_var=$2
  shift 2
  local value
  if [ "$strict" -eq 1 ]; then
    local suffix=${preferred_var#*_}
    local strict_var="${strict_family}_${suffix}"
    value=${!strict_var:-}
    if [ -n "$value" ]; then
      printf '%s\n' "$value"
      return 0
    fi
    return 1
  fi
  eval "value=\${$preferred_var:-}"
  if [ -z "$value" ]; then
    eval "value=\${$fallback_var:-}"
  fi
  if [ -n "$value" ]; then
    printf '%s\n' "$value"
    return 0
  fi
  local candidate
  for candidate in "$@"; do
    if command -v "$candidate" >/dev/null 2>&1; then
      command -v "$candidate"
      return 0
    fi
  done
  return 1
}

cc=$(pick_tool RISCV_CC FORMOSA_CC \
  riscv64-none-elf-gcc riscv64-unknown-elf-gcc clang 2>/dev/null || true)
cxx=$(pick_tool RISCV_CXX FORMOSA_CXX \
  riscv64-none-elf-g++ riscv64-unknown-elf-g++ clang++ 2>/dev/null || true)
ld=$(pick_tool RISCV_LD FORMOSA_LD \
  riscv64-none-elf-ld riscv64-unknown-elf-ld ld.lld 2>/dev/null || true)
ar=$(pick_tool RISCV_AR FORMOSA_AR \
  riscv64-none-elf-ar riscv64-unknown-elf-ar llvm-ar ar 2>/dev/null || true)
objcopy=$(pick_tool RISCV_OBJCOPY FORMOSA_OBJCOPY \
  riscv64-none-elf-objcopy riscv64-unknown-elf-objcopy llvm-objcopy objcopy 2>/dev/null || true)
readelf=$(pick_tool RISCV_READELF FORMOSA_READELF \
  riscv64-none-elf-readelf riscv64-unknown-elf-readelf llvm-readelf readelf 2>/dev/null || true)

is_clang_driver() {
  local program=$1
  "$program" --version 2>/dev/null | grep -qi 'clang'
}

for required in cc cxx ar ld objcopy readelf; do
  if [ -z "${!required}" ]; then
    write_failure "missing-$required"
    echo "toolchain probe: missing $required" >&2
    exit 1
  fi
  if [ ! -x "${!required}" ]; then
    write_failure "not-executable-$required"
    echo "toolchain probe: $required is not executable: ${!required}" >&2
    exit 1
  fi
done

work=$(mktemp -d "${TMPDIR:-/tmp}/formosa-toolchain-probe.XXXXXX")
cleanup() { rm -rf "$work"; }
trap cleanup EXIT

cat >"$work/probe.c" <<'EOF'
int formosa_toolchain_probe_value(void) { return 7; }
EOF

cat >"$work/probe.cc" <<'EOF'
extern "C" int formosa_toolchain_probe_cpp_value() { return 9; }
EOF

cat >"$work/start.S" <<'EOF'
.section .text.start,"ax",@progbits
.globl _start
.type _start,@function
_start:
  call formosa_toolchain_probe_value
1:
  j 1b
.size _start, .-_start
EOF

cat >"$work/linker.ld" <<'EOF'
ENTRY(_start)
MEMORY { ROM (rx) : ORIGIN = 0x0, LENGTH = 0x1000 }
SECTIONS {
  .text : { KEEP(*(.text.start)) *(.text*) } > ROM
  .rodata : { *(.rodata*) } > ROM
  .data : { *(.data*) } > ROM
  .bss : { *(.bss*) *(COMMON) } > ROM
}
EOF

compiler_flags="-march=$march -mabi=$mabi -mcmodel=$mcmodel -ffreestanding -fno-builtin -fno-pic -fno-stack-protector -O0 -g"
if is_clang_driver "$cc"; then
  compiler_flags="$compiler_flags --target=$target"
fi
if [ -n "${FORMOSA_EXTRA_FLAGS:-}" ]; then
  compiler_flags="$compiler_flags $FORMOSA_EXTRA_FLAGS"
fi

if ! "$cc" $compiler_flags -c "$work/probe.c" -o "$work/probe.o" >"$work/compile-c.log" 2>&1; then
  write_failure "c-compile"
  cat "$work/compile-c.log" >&2
  exit 1
fi

cxx_flags="-march=$march -mabi=$mabi -mcmodel=$mcmodel -ffreestanding -fno-builtin -fno-pic -fno-stack-protector -fno-exceptions -fno-rtti -O0 -g"
if is_clang_driver "$cxx"; then
  cxx_flags="$cxx_flags --target=$target"
fi
if [ -n "${FORMOSA_EXTRA_FLAGS:-}" ]; then
  cxx_flags="$cxx_flags $FORMOSA_EXTRA_FLAGS"
fi

if ! "$cxx" $cxx_flags -c "$work/probe.cc" -o "$work/probe-cxx.o" >"$work/compile-cxx.log" 2>&1; then
  write_failure "cxx-compile"
  cat "$work/compile-cxx.log" >&2
  exit 1
fi

if ! "$ar" rcs "$work/probe.a" "$work/probe-cxx.o" >"$work/archive.log" 2>&1; then
  write_failure "archive"
  cat "$work/archive.log" >&2
  exit 1
fi

if ! "$cc" $compiler_flags -x assembler-with-cpp -c "$work/start.S" -o "$work/start.o" >"$work/compile-asm.log" 2>&1; then
  write_failure "asm-compile"
  cat "$work/compile-asm.log" >&2
  exit 1
fi

if ! "$ld" -m elf64lriscv -T "$work/linker.ld" -o "$work/probe.elf" \
  "$work/start.o" "$work/probe.o" "$work/probe.a" >"$work/link.log" 2>&1; then
  write_failure "link"
  cat "$work/link.log" >&2
  exit 1
fi

if ! "$objcopy" -O binary "$work/probe.elf" "$work/probe.bin" >"$work/objcopy.log" 2>&1; then
  write_failure "objcopy"
  cat "$work/objcopy.log" >&2
  exit 1
fi

if ! "$readelf" -h "$work/probe.elf" >"$work/header.txt" 2>"$work/readelf.log"; then
  write_failure "readelf"
  cat "$work/readelf.log" >&2
  exit 1
fi

if ! grep -Eq 'RISC-V|riscv' "$work/header.txt"; then
  write_failure "non-riscv-elf"
  cat "$work/header.txt" >&2
  exit 1
fi

binary_size=$(wc -c <"$work/probe.bin" | tr -d ' ')
cat >"$output" <<EOF
status=pass
compiler=$cc
compiler_cxx=$cxx
archiver=$ar
linker=$ld
objcopy=$objcopy
readelf=$readelf
target=$target
march=$march
mabi=$mabi
mcmodel=$mcmodel
linker_script=explicit
binary_size=$binary_size
EOF

echo "toolchain probe: PASS ($output)"
