# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

{ pkgs ? import <nixpkgs> { }, buildSystem ? "cmake" }:

let
  inherit (pkgs) lib;
  root = ./..;

  src = lib.fileset.toSource {
    inherit root;
    fileset = lib.fileset.unions [
      (root + "/CMakeLists.txt")
      (root + "/FormosaConfig.cmake.in")
      (root + "/cmake")
      (root + "/formosa-sdk-xmake.lua")
      (root + "/workflows.lua")
      (root + "/xmake")
      (root + "/ilha/platform-data/xmake.lua")
      (root + "/lv/device")
      (root + "/ilha/lua/addr_map.lua")
      (root + "/ilha/scripts/generate_header.lua")
      (root + "/ilha/scripts/generate_linker.lua")
      (root + "/fw")
      (root + "/hal")
      (root + "/libcomm")
      (root + "/third-party/FreeRTOS-Kernel.cmake")
    ];
  };

  # Fixed-output archive fetches keep evaluation offline.  The rev remains
  # explicit so updating the kernel is a normal source change with a new hash.
  freeRTOSKernel = pkgs.fetchFromGitHub {
    owner = "FreeRTOS";
    repo = "FreeRTOS-Kernel";
    rev = "0adc196d4bd52a2d91102b525b0aafc1e14a2386";
    hash = "sha256-MTKNyA/nUaoX44cWEsRaPUphUgdMXzIxAfRRN7CmKPQ=";
  };

  crossPkgs = pkgs.pkgsCross.riscv64-embedded;
in assert builtins.elem buildSystem [ "cmake" "xmake" ];
pkgs.stdenv.mkDerivation ({
  pname = "hal";
  version = "0.1";
  inherit src;

  nativeBuildInputs = with pkgs;
    [ xmake tinyxxd llvmPackages.lld ]
    ++ lib.optionals (buildSystem == "cmake") [ cmake ninja ];
  buildInputs = [ crossPkgs.stdenv.cc ]
    ++ lib.optionals (buildSystem == "cmake") [ crossPkgs.buildPackages.gdb ];
} // (if buildSystem == "cmake" then {
  cmakeFlags = [ "-DENABLE_PROJECTS=hal" "-DFREERTOS_PATH=${freeRTOSKernel}" ];
} else {
  postUnpack = ''
    mkdir -p "$sourceRoot/third-party"
    ln -s ${freeRTOSKernel} "$sourceRoot/third-party/FreeRTOS-Kernel"
  '';
  dontConfigure = true;
  buildPhase = ''
    runHook preBuild
    xmake f -F formosa-sdk-xmake.lua -m release -o build
    xmake -F formosa-sdk-xmake.lua -j "$NIX_BUILD_CORES" formosa-sdk
    runHook postBuild
  '';
  installPhase = ''
    runHook preInstall
    xmake install -F formosa-sdk-xmake.lua -o "$out" formosa-sdk
    runHook postInstall
  '';
}))
