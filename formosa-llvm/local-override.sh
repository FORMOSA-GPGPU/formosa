# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
# SPDX-License-Identifier: Apache-2.0

formosa_override_warning() {
  local warning="[WARNING] FORMOSA LOCAL $1 OVERRIDE ACTIVE"
  [[ -t 2 ]] && warning=$'\033[1;33m'"$warning"$'\033[0m'
  log_status "$warning"
  log_status "  $1: $2"
}

formosa_load_toolchain_overrides() {
  local formosa_llvm_tool
  if [[ -n ${FORMOSA_LLVM_PREFIX:-} ]]; then
    FORMOSA_LLVM_PREFIX="$(realpath "$FORMOSA_LLVM_PREFIX")" || return 1
    export FORMOSA_LLVM_PREFIX
    for formosa_llvm_tool in llvm-config clang ld.lld; do
      if [[ ! -x $FORMOSA_LLVM_PREFIX/bin/$formosa_llvm_tool ]]; then
        log_error "local LLVM is missing bin/$formosa_llvm_tool: $FORMOSA_LLVM_PREFIX"
        return 1
      fi
    done
    PATH_add "$FORMOSA_LLVM_PREFIX/bin"
    formosa_override_warning LLVM "$FORMOSA_LLVM_PREFIX"
  fi

  if [[ -n ${FORMOSA_RUNTIME_PREFIX:-} ]]; then
    if [[ ! -d $FORMOSA_RUNTIME_PREFIX ]]; then
      log_error "FORMOSA_RUNTIME_PREFIX is not a directory: $FORMOSA_RUNTIME_PREFIX"
      return 1
    fi
    export FORMOSA_RUNTIME_PREFIX="$(realpath "$FORMOSA_RUNTIME_PREFIX")"
    # The wrapper also validates direct use outside direnv.
    formosa-clang --print-resource-dir >/dev/null || return 1
    formosa_override_warning RUNTIME "$FORMOSA_RUNTIME_PREFIX"
  fi
}

# Shared LLVM compatibility check for the library selected by a local driver.
# Each driver owns ICD discovery and passes the resolved library here.
formosa_check_driver() {
  local name=$1 prefix=$2 library=$3 libdir dependencies llvm_libraries llvm_library
  if [[ ! -f $library ]]; then
    log_error "local $name library does not exist: $library"
    return 1
  fi
  library="$(realpath "$library")"
  if [[ $library != "$prefix"/* ]]; then
    log_error "local $name library is outside its override prefix: $library"
    return 1
  fi
  for libdir in "$(dirname "$library")" "$prefix/lib" "$prefix/lib64"; do
    if [[ -d $libdir ]]; then
      path_add LD_LIBRARY_PATH "$libdir"
      path_add DYLD_LIBRARY_PATH "$libdir"
    fi
  done
  if command -v ldd >/dev/null 2>&1 && dependencies="$(ldd "$library" 2>&1)"; then
    :
  elif command -v otool >/dev/null 2>&1 && dependencies="$(otool -L "$library" 2>&1)"; then
    :
  else
    log_error "cannot inspect local $name dependencies: $library"
    log_error "$dependencies"
    return 1
  fi
  if [[ $dependencies == *"not found"* ]]; then
    log_error "local $name has unresolved dependencies: $dependencies"
    return 1
  fi
  llvm_libraries="$(awk '
    $1 ~ /^libLLVM/ && $2 == "=>" { print $3; next }
    $1 ~ /^\// && $1 ~ /\/libLLVM/ { print $1 }
  ' <<< "$dependencies")"
  if [[ -n ${FORMOSA_LLVM_PREFIX:-} && -z $llvm_libraries ]]; then
    log_error "local $name does not resolve LLVM libraries from $FORMOSA_LLVM_PREFIX"
    return 1
  fi
  if [[ -n $llvm_libraries ]]; then
    while IFS= read -r llvm_library; do
      llvm_library="$(realpath "$llvm_library")" || return 1
      if [[ -n ${FORMOSA_LLVM_PREFIX:-} && $llvm_library != "$FORMOSA_LLVM_PREFIX"/* ]]; then
        log_error "local $name does not resolve LLVM libraries from $FORMOSA_LLVM_PREFIX: $llvm_library"
        return 1
      fi
    done <<< "$llvm_libraries"
    llvm_library="${llvm_libraries%%$'\n'*}"
    log_status "  $name LLVM libs: $(dirname "$llvm_library")"
  fi
  formosa_override_warning "${name^^}" "$library"
}
