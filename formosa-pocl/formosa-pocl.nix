# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

{ stdenv, opencl-headers, opencl-clhpp, openssh, git, cmake, ninja, python3
, zlib, hwloc, ocl-icd, clinfo, pkg-config, formosa-llvm, hal, xxd
, formosa-clang-cross, spirvLlvm, src, pkgs ? import <nixpkgs> { }, }:

let crossPkgs = pkgs.pkgsCross.riscv64-embedded;
in stdenv.mkDerivation {
  name = "formosa-pocl";
  inherit src;

  # The Formosa device backend uses <elf.h>. On Darwin it is supplied by the
  # elf-header package listed in buildInputs below.
  # When the Formosa device is built into libpocl, the imported HAL target must
  # be GLOBAL so the parent scope can resolve FORMOSA_LIBS.
  postPatch = ''
    substituteInPlace lib/CL/devices/formosa/CMakeLists.txt \
      --replace-fail 'find_package(Formosa REQUIRED)' 'find_package(Formosa REQUIRED GLOBAL)'
  '';

  cmakeFlags = [
    "-D WITH_LLVM_CONFIG=${formosa-llvm}/bin/llvm-config"
    "-D ENABLE_SPIRV=ON"
    "-D LLVM_SPIRV=${spirvLlvm}/bin/llvm-spirv"
    "-D FORMOSA_CLANG_PATH=${formosa-clang-cross}/bin/formosa-clang"
    "-D LLC_TRIPLE=x86_64-unknown-linux-gnu"
    "-D ENABLE_ICD=ON"
    "-D ENABLE_FORMOSA=ON"
    "-D ENABLE_CUDA=OFF"
    "-D ENABLE_TCE=OFF"
    "-D ENABLE_HSA=OFF"
    "-D ENABLE_VULKAN=OFF"
    "-D ENABLE_LEVEL0=OFF"
    "-D CMAKE_INSTALL_PREFIX=${placeholder "out"}"
    "-D ENABLE_LATEST_CXX_STD=ON"
    "-D ENABLE_LIBLLVMOPENCL=ON"
    "-D ENABLE_HOST_CPU_DEVICES=OFF"
    "-D INSTALL_OPENCL_HEADERS=ON"
  ] ++ pkgs.lib.optionals stdenv.isDarwin [
    # The formosa device calls LLVM APIs directly. As a loadable driver it is a
    # standalone shared lib with the LLVM symbols left undefined and resolved at
    # runtime from libpocl, which works on ELF but not on macOS' two-level
    # namespace. Build devices into libpocl instead so the symbols are resolved
    # at static link time.
    "-D ENABLE_LOADABLE_DRIVERS=OFF"
  ];

  # On macOS pocl installs its OpenCL headers to include/OpenCL (its CMake sets
  # POCL_INSTALL_OPENCL_HEADER_DIR to "OpenCL" on Apple to mimic the framework
  # layout) instead of include/CL as on Linux. But pocl's own OpenCL/opencl.h
  # aggregate includes its headers via <CL/...>, and the Khronos <CL/opencl.hpp>
  # bindings pull <OpenCL/opencl.h> on Apple — so consumers need both spellings.
  # Add a CL -> OpenCL alias so <CL/...> and <OpenCL/...> both resolve to pocl's
  # headers, matching the Linux layout.
  postInstall = pkgs.lib.optionalString stdenv.isDarwin ''
    ln -s OpenCL $out/include/CL
  '';

  nativeBuildInputs = [ openssh git cmake ninja python3 spirvLlvm ];

  buildInputs = [
    zlib
    opencl-headers
    opencl-clhpp
    ocl-icd
    pkg-config
    hwloc
    formosa-llvm
    formosa-clang-cross
    clinfo
    hal
    xxd
    spirvLlvm
    crossPkgs.stdenv.cc
    crossPkgs.buildPackages.gdb
  ] ++ pkgs.lib.optionals stdenv.isDarwin [ pkgs.elf-header ];
}
