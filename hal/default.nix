# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

{ pkgs ? import <nixpkgs> { } }:

let
  inherit (pkgs) lib;
  root = ./..;

  src = lib.fileset.toSource {
    inherit root;
    fileset = lib.fileset.unions [
      (root + "/CMakeLists.txt")
      (root + "/FormosaConfig.cmake.in")
      (root + "/cmake")
      (root + "/addr_map")
      (root + "/fw")
      (root + "/hal")
      (root + "/libcomm")
      (root + "/third-party/FreeRTOS-Kernel.cmake")
    ];
  };

  freeRTOSKernel = builtins.fetchGit {
    url = "https://github.com/FreeRTOS/FreeRTOS-Kernel.git";
    ref = "refs/tags/V11.2.0";
    rev = "0adc196d4bd52a2d91102b525b0aafc1e14a2386";
  };

  crossPkgs = pkgs.pkgsCross.riscv64-embedded;
in pkgs.stdenv.mkDerivation {
  pname = "hal";
  version = "0.1";
  inherit src;

  nativeBuildInputs = with pkgs; [ cmake ninja tinyxxd llvmPackages.lld ];

  buildInputs = [ crossPkgs.stdenv.cc crossPkgs.buildPackages.gdb ];

  cmakeFlags = [ "-DENABLE_PROJECTS=hal" "-DFREERTOS_PATH=${freeRTOSKernel}" ];
}
