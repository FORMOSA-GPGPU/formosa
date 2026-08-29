# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

{ pkgs ? import <nixpkgs> { }, formosa-llvm, formosa-clang-cross, hal, spirvLlvm
}:
pkgs.callPackage ./formosa-pocl.nix {
  inherit formosa-llvm formosa-clang-cross hal spirvLlvm;
}
