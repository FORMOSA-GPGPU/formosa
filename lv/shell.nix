# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

{ pkgs ? import <nixpkgs> { } }:

let
  # CI/lint tooling (pre-commit, clang-format, stylua, gcovr, ...) lives in
  # the top-level flake's ciPackages.
  packages = with pkgs; [ gdb ];

  nativeBuildInputs = with pkgs; [ git xmake cmake ninja ];

  buildInputs = with pkgs; [
    luajit
    luajitPackages.argparse
    luajitPackages.luaposix
    zlib
    sqlite
  ];
in pkgs.mkShell.override { stdenv = pkgs.gccStdenv; } {
  name = "lv";
  inherit packages buildInputs nativeBuildInputs;
}
