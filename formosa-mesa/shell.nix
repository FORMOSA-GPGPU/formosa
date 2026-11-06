# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
# SPDX-License-Identifier: Apache-2.0

{ pkgs, formosa-mesa, formosa-llvm, formosa-pocl, formosa-clang-cross, hal }:

pkgs.mkShell.override { stdenv = pkgs.gccStdenv; } {
  name = "formosa-mesa";
  inputsFrom = [ formosa-mesa ];
  packages = [ formosa-llvm formosa-pocl formosa-clang-cross ];
  FORMOSA_DEFAULT_SDK = hal;
  FORMOSA_MESA_NATIVE_BASE = formosa-mesa.nativeFile;
  FORMOSA_MESA_CLANG = "${formosa-clang-cross}/bin/formosa-clang";
  shellHook = ''
    # Reuse the runtime override validation in interactive Nix shells too.
    if ! declare -F log_status >/dev/null; then
      eval "$(${pkgs.direnv}/bin/direnv stdlib)"
    fi
    source ${../formosa-llvm/local-override.sh}
    source ${../formosa-pocl/local-override.sh}
    source ${./local-override.sh}
    formosa_setup_mesa_build || exit 1
  '';
}
