# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

{ pkgs ? import <nixpkgs> { } }:

let
  nativeBuildInputs = with pkgs; [ git bear cmake ninja ];

  buildInputs = with pkgs; [ zlib ];

  # CI tooling (pre-commit, gcovr, openssh, ...) lives in the top-level
  # flake's ciPackages; keep this shell to simtix-specific dev tools.
  packages = with pkgs; [ gdb cppcheck ];
in pkgs.mkShell.override { stdenv = pkgs.gccStdenv; } {
  name = "simtix";
  inherit packages buildInputs nativeBuildInputs;
}
