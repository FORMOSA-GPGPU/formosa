# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

{ pkgs, formosa-pocl }:

pkgs.mkShell {
  name = "formosa-pocl";
  inputsFrom = [ formosa-pocl ];
  inherit (formosa-pocl) cmakeFlags;
  # language:system hook needs clang-format on PATH (see ../pocl .pre-commit-config.yaml)
  packages = with pkgs; [ pre-commit clang-tools ];
}
