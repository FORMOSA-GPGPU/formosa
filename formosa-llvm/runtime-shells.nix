# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

{ pkgs, toolchain }:

let
  llvm = toolchain.formosa-llvm-unwrapped;
  commonHook = ''
    formosa_default_llvm="${llvm}"
    formosa_build_llvm="''${FORMOSA_LLVM_PREFIX:-$formosa_default_llvm}"
    for tool in clang llvm-config llvm-ar llvm-nm llvm-ranlib ld.lld; do
      if [[ ! -x $formosa_build_llvm/bin/$tool ]]; then
        echo "LLVM build tool is missing: $formosa_build_llvm/bin/$tool" >&2
        exit 1
      fi
    done
    formosa_build_llvm="$(realpath "$formosa_build_llvm")"
    export FORMOSA_RUNTIME_PREFIX="$(realpath -m "''${FORMOSA_RUNTIME_PREFIX:-$PWD/outputs/out}")"
  '';
in {
  formosa-newlib = pkgs.mkShell {
    name = "formosa-newlib";
    inputsFrom = [ toolchain.newlib ];
    shellHook = commonHook + ''
      # Keep assignments containing compiler flags as single configure arguments.
      configureFlagsArray=(${
        pkgs.lib.escapeShellArgs toolchain.newlib.configureFlags
      })
      for i in "''${!configureFlagsArray[@]}"; do
        configureFlagsArray[i]="''${configureFlagsArray[i]//"$formosa_default_llvm"/"$formosa_build_llvm"}"
      done
      configureFlagsArray+=("--prefix=$FORMOSA_RUNTIME_PREFIX")
    '';
  };

  formosa-compiler-rt = pkgs.mkShell {
    name = "formosa-compiler-rt";
    inputsFrom = [ toolchain.compiler-rt ];
    shellHook = commonHook + ''
      cmakeFlagsArray=(${
        pkgs.lib.escapeShellArgs toolchain.compiler-rt.cmakeFlags
      })
      for i in "''${!cmakeFlagsArray[@]}"; do
        cmakeFlagsArray[i]="''${cmakeFlagsArray[i]//"$formosa_default_llvm"/"$formosa_build_llvm"}"
      done
      cmakeFlagsArray+=(
        "-DCMAKE_INSTALL_PREFIX=$FORMOSA_RUNTIME_PREFIX/lib/clang/current"
        "-DLLVM_ENABLE_PER_TARGET_RUNTIME_DIR=ON"
        "-DCMAKE_PROJECT_INCLUDE=${./runtime-install.cmake}"
      )
    '';
  };
}
