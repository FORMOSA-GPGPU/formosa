# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

{ pkgs ? import <nixpkgs> { } }:

let
  nativeBuildInputs = with pkgs; [ git cmake ninja ];

  # CI/lint tooling (pre-commit, clang-format, gcovr, ...) lives in the
  # top-level flake's ciPackages.
  packages = with pkgs; [ gdb ];
in pkgs.mkShell.override { stdenv = pkgs.gccStdenv; } {
  name = "libcomm";
  inherit packages nativeBuildInputs;
}
