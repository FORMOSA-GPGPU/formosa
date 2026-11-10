# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

{ pkgs ? import <nixpkgs> { } }:

let
  inherit (pkgs) lib;
  root = builtins.dirOf ./.;
  sourceFiles = directory:
    lib.fileset.fileFilter (file: lib.any file.hasExt [ "c" "cc" "h" "S" "ld" ])
    directory;

  # Keep the SDK's source identity limited to its native build inputs and
  # consumer exports; CI/tasks, documentation and source CMake recipes stay out.
  src = lib.fileset.toSource {
    inherit root;
    fileset = lib.fileset.unions [
      (root + "/hal/include")
      (sourceFiles (root + "/hal/src"))
      (lib.fileset.difference (sourceFiles (root + "/fw")) (root + "/fw/tests"))
      (root + "/fw/tinyprintf/LICENSE.BSD-new")
      (root + "/libcomm/include")
      (root + "/lv/device/include")
      (root + "/ilha/lua/addr_map.lua")
      (root + "/ilha/scripts/generate_header.lua")
      (root + "/ilha/scripts/generate_linker.lua")
      (root + "/formosa-sdk-xmake.lua")
      # The SDK uses native targets without selecting a workflow/test suite.
      # Workflow metadata must not invalidate the SDK or its PoCL consumers.
      (root + "/hal/xmake.lua")
      # Consumer exports remain supported after the CMake producer is removed.
      (root + "/hal/cmake/FormosaConfig.cmake")
      (root + "/hal/cmake/FormosaConfigVersion.cmake")
      (root + "/hal/cmake/FormosaTargets.install.cmake.in")
      (root + "/fw/xmake.lua")
      (root + "/libcomm/xmake.lua")
      (root + "/lv/device/xmake.lua")
      (root + "/ilha/platform-data/xmake.lua")
      (root + "/xmake/rules/freertos/xmake.lua")
      (root + "/xmake/rules/freestanding/xmake.lua")
      (root + "/xmake/rules/workflow/xmake.lua")
      (root + "/xmake/modules/workflow/backends/xmake.lua")
      (root + "/xmake/modules/workflow/frontend.lua")
      (root + "/xmake/modules/workflow/resolve.lua")
      (root + "/xmake/modules/lv/layout.lua")
      # The shared module recipes also declare these native test/example targets.
      (root + "/hal/tests/xmake.lua")
      (sourceFiles (root + "/hal/tests/lv"))
      (root + "/hal/tests/lv/run_lv_test.sh")
      (root + "/fw/tests/xmake.lua")
      (sourceFiles (root + "/fw/tests"))
      (sourceFiles (root + "/libcomm/example"))
      (sourceFiles (root + "/libcomm/tests"))
      (root + "/libcomm/tests/unit/install_consumer.sh")
      (root + "/xmake/modules/testing.lua")
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
in pkgs.stdenv.mkDerivation {
  pname = "hal";
  version = "0.1";
  inherit src;

  nativeBuildInputs = with pkgs; [ xmake tinyxxd llvmPackages.lld ];
  buildInputs = [ crossPkgs.stdenv.cc ];

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
}
