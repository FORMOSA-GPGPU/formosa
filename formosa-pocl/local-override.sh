# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
# SPDX-License-Identifier: Apache-2.0

# Requires formosa-llvm/local-override.sh for toolchain validation and logging.

formosa_load_pocl_overrides() {
  local formosa_pocl_icd formosa_pocl_library formosa_pocl_libdir
  if [[ -n ${FORMOSA_POCL_PREFIX:-} ]]; then
    FORMOSA_POCL_PREFIX="$(realpath "$FORMOSA_POCL_PREFIX")" || return 1
    export FORMOSA_POCL_PREFIX
    formosa_pocl_icd="$FORMOSA_POCL_PREFIX/etc/OpenCL/vendors/pocl.icd"
    if [[ ! -f $formosa_pocl_icd ]]; then
      log_error "local PoCL ICD does not exist: $formosa_pocl_icd"
      return 1
    fi
    formosa_pocl_library="$(head -n 1 "$formosa_pocl_icd")" || return 1
    if [[ $formosa_pocl_library != /* ]]; then
      for formosa_pocl_libdir in "$FORMOSA_POCL_PREFIX/lib64" "$FORMOSA_POCL_PREFIX/lib"; do
        if [[ -f $formosa_pocl_libdir/$formosa_pocl_library ]]; then
          formosa_pocl_library="$formosa_pocl_libdir/$formosa_pocl_library"
          break
        fi
      done
    fi
    formosa_check_driver PoCL "$FORMOSA_POCL_PREFIX" "$formosa_pocl_library" || return 1
    PATH_add "$FORMOSA_POCL_PREFIX/bin"
    export OCL_ICD_VENDORS="$formosa_pocl_icd"
    export POCL_DEVICES=formosa
  fi
}

# Mesa consumes PoCL headers and bitcode without loading the OpenCL driver.
formosa_load_pocl_build_overrides() {
  local formosa_pocl_file
  if [[ -n ${FORMOSA_POCL_PREFIX:-} ]]; then
    FORMOSA_POCL_PREFIX="$(realpath "$FORMOSA_POCL_PREFIX")" || return 1
    export FORMOSA_POCL_PREFIX
    for formosa_pocl_file in bin/poclcc share/pocl/include/_kernel.h share/pocl/kernel-riscv64-formosa.bc; do
      if [[ ! -f $FORMOSA_POCL_PREFIX/$formosa_pocl_file ]]; then
        log_error "local PoCL is missing $formosa_pocl_file: $FORMOSA_POCL_PREFIX"
        return 1
      fi
    done
    PATH_add "$FORMOSA_POCL_PREFIX/bin"
    formosa_override_warning POCL "$FORMOSA_POCL_PREFIX"
  fi
}
