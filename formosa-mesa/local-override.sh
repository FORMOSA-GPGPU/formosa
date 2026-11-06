# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
# SPDX-License-Identifier: Apache-2.0

# Requires formosa-llvm/local-override.sh for toolchain validation and logging.

formosa_load_mesa_overrides() {
  local formosa_mesa_icd formosa_mesa_library
  local -a formosa_mesa_icds
  if [[ -n ${FORMOSA_MESA_PREFIX:-} ]]; then
    FORMOSA_MESA_PREFIX="$(realpath "$FORMOSA_MESA_PREFIX")" || return 1
    export FORMOSA_MESA_PREFIX
    formosa_mesa_icds=("$FORMOSA_MESA_PREFIX"/share/vulkan/icd.d/formosa_icd*.json)
    if [[ ${#formosa_mesa_icds[@]} != 1 || ! -f ${formosa_mesa_icds[0]} ]]; then
      log_error "expected one installed Formosa ICD under $FORMOSA_MESA_PREFIX/share/vulkan/icd.d"
      return 1
    fi
    formosa_mesa_icd="${formosa_mesa_icds[0]}"
    formosa_mesa_library="$(python3 - "$formosa_mesa_icd" <<'PYTHON'
import json
import sys
from pathlib import Path

icd = Path(sys.argv[1])
try:
    library = Path(json.loads(icd.read_text())["ICD"]["library_path"])
    print(library if library.is_absolute() else icd.parent / library)
except (OSError, ValueError, KeyError, TypeError) as error:
    sys.exit(f"Invalid Formosa ICD {icd}: {error}")
PYTHON
    )" || return 1
    formosa_check_driver Mesa "$FORMOSA_MESA_PREFIX" "$formosa_mesa_library" || return 1
    export VK_DRIVER_FILES="$formosa_mesa_icd"
    log_status "  Vulkan ICD: $VK_DRIVER_FILES"
  fi
}

# Called only by the Mesa build shell; sourcing this file does not configure a build.
# Also requires formosa-pocl/local-override.sh for headers and bitcode selection.
formosa_setup_mesa_build() {
  local formosa_clang_wrapper formosa_clang_script formosa_clang_hash formosa_clang_tmp
  formosa_load_toolchain_overrides || return 1

  if [[ -n ${FORMOSA_INSTALL_PREFIX:-} ]]; then
    formosa_override_warning SDK "$FORMOSA_INSTALL_PREFIX"
  fi
  export FORMOSA_INSTALL_PREFIX="${FORMOSA_INSTALL_PREFIX:-$FORMOSA_DEFAULT_SDK}"
  if [[ ! -f $FORMOSA_INSTALL_PREFIX/lib/libformosa-hal.lv.a ]]; then
    log_error "install the Formosa SDK first: $FORMOSA_INSTALL_PREFIX"
    return 1
  fi
  export FORMOSA_INSTALL_PREFIX="$(realpath "$FORMOSA_INSTALL_PREFIX")"
  export MESA_INSTALL_PREFIX="$(realpath -m "${MESA_INSTALL_PREFIX:-$PWD/outputs/out}")"
  # Use PoCL's persistent-launcher scheme: installed shaders keep their build-time
  # LLVM/runtime selection even when the caller has different overrides.
  formosa_clang_wrapper="$FORMOSA_MESA_CLANG"
  formosa_clang_script="$(printf '#!%s\nexport FORMOSA_LLVM_PREFIX=%q\nexport FORMOSA_RUNTIME_PREFIX=%q\nexec %q "$@"\n' \
    "$(command -v bash)" "${FORMOSA_LLVM_PREFIX:-}" "${FORMOSA_RUNTIME_PREFIX:-}" \
    "$formosa_clang_wrapper")" || return 1
  formosa_clang_hash="$(printf '%s\n' "$formosa_clang_script" | sha256sum)" || return 1
  export FORMOSA_CLANG_PATH="$PWD/outputs/toolchains/formosa-clang-${formosa_clang_hash%% *}"
  if [[ ! -x $FORMOSA_CLANG_PATH ]]; then
    mkdir -p "$PWD/outputs/toolchains" || return 1
    formosa_clang_tmp="$(mktemp "$PWD/outputs/toolchains/.formosa-clang.XXXXXX")" || return 1
    if ! { printf '%s\n' "$formosa_clang_script" > "$formosa_clang_tmp" &&
           chmod +x "$formosa_clang_tmp" &&
           mv -f "$formosa_clang_tmp" "$FORMOSA_CLANG_PATH"; }; then
      rm -f "$formosa_clang_tmp"
      return 1
    fi
  fi

  formosa_load_pocl_build_overrides || return 1

  export MESA_NATIVE_FILE="$PWD/.direnv/formosa.ini"
  mkdir -p .direnv
  python3 - <<'PYTHON' || return 1
import configparser
import os
from pathlib import Path


def quote(value):
    return "'" + value.replace("\\", "\\\\").replace("'", "\\'") + "'"


config = configparser.ConfigParser(interpolation=None)
with open(os.environ["FORMOSA_MESA_NATIVE_BASE"]) as base:
    config.read_file(base)
config["built-in options"]["prefix"] = quote(os.environ["MESA_INSTALL_PREFIX"])
config["project options"]["formosa-sdk"] = quote(os.environ["FORMOSA_INSTALL_PREFIX"])
config["binaries"]["formosa-clang"] = quote(os.environ["FORMOSA_CLANG_PATH"])
if os.environ.get("FORMOSA_LLVM_PREFIX"):
    config["binaries"]["llvm-config"] = quote(os.environ["FORMOSA_LLVM_PREFIX"] + "/bin/llvm-config")
if os.environ.get("FORMOSA_POCL_PREFIX"):
    config["binaries"]["poclcc"] = quote(os.environ["FORMOSA_POCL_PREFIX"] + "/bin/poclcc")
with Path(os.environ["MESA_NATIVE_FILE"]).open("w") as output:
    config.write(output)
PYTHON
  log_status "Formosa SDK: $FORMOSA_INSTALL_PREFIX"
  log_status "Mesa install: $MESA_INSTALL_PREFIX"
  log_status 'Configure: meson setup build --native-file "$MESA_NATIVE_FILE"'
}
