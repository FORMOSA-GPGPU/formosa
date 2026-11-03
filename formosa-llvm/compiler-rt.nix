# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

{ stdenv, lib, cmake, ninja, python3, pkg-config, zlib, formosa-llvm, src }:

stdenv.mkDerivation (finalAttrs: {
  name = "compiler-rt";
  inherit src;

  cmakeFlags = [
    "-DCMAKE_SYSTEM_NAME=Linux"
    "-DCMAKE_C_COMPILER_TARGET=riscv64-unknown-elf"
    "-DCMAKE_ASM_COMPILER_TARGET=riscv64-unknown-elf"
    "-DCOMPILER_RT_DEFAULT_TARGET_ONLY=ON"
    "-DCOMPILER_RT_BAREMETAL_BUILD=ON"
    "-DCOMPILER_RT_BUILD_BUILTINS=ON"
    "-DCOMPILER_RT_BUILD_LIBFUZZER=OFF"
    "-DCOMPILER_RT_BUILD_MEMPROF=OFF"
    "-DCOMPILER_RT_BUILD_PROFILE=OFF"
    "-DCOMPILER_RT_BUILD_CTX_PROFILE=OFF"
    "-DCOMPILER_RT_BUILD_ORC=OFF"
    "-DCOMPILER_RT_BUILD_SANITIZERS=OFF"
    "-DCOMPILER_RT_BUILD_XRAY=OFF"
    "-DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY"
    "-DCMAKE_C_COMPILER=${formosa-llvm}/bin/clang"
    "-DCMAKE_ASM_COMPILER=${formosa-llvm}/bin/clang"
    "-DCMAKE_C_FLAGS=-march=rv64im_zicsr_zicond -mabi=lp64 -mno-relax -mcmodel=medany"
    "-DCMAKE_ASM_FLAGS=-march=rv64im_zicsr_zicond -mabi=lp64 -mno-relax -mcmodel=medany"
    "-DCMAKE_AR=${formosa-llvm}/bin/llvm-ar"
    "-DCMAKE_NM=${formosa-llvm}/bin/llvm-nm"
    "-DCMAKE_RANLIB=${formosa-llvm}/bin/llvm-ranlib"
    "-DLLVM_CONFIG_PATH=${formosa-llvm}/bin/llvm-config"
    "-DLLVM_ENABLE_PER_TARGET_RUNTIME_DIR=OFF"
  ];

  configurePhase = ''
    cmake -B build -G Ninja -S compiler-rt -DCMAKE_INSTALL_PREFIX="$out" \
      ${lib.escapeShellArgs finalAttrs.cmakeFlags}
  '';

  buildPhase = ''
    cmake --build build
  '';

  installPhase = ''
    cmake --build build --target install
  '';

  # The build produces a clean, well-formed GNU archive for the RISC-V builtins
  # (llvm-ar qc + llvm-ranlib, with '/' and '//' as the legitimate symbol/string
  # tables). On Darwin nixpkgs' default fixupPhase then runs the host `strip -S`
  # over everything under lib/, and Apple's strip corrupts that archive: it
  # prepends a BSD __.SYMDEF symbol table while leaving the GNU '/' and '//'
  # tables behind as orphaned members. The RISC-V lld later loads the whole
  # archive to link a kernel, treats those orphans as objects, and warns "archive
  # member '/' is neither ET_REL nor LLVM bitcode". Stripping a cross-compiled
  # RISC-V archive with the host toolchain is wrong anyway, so skip it on Darwin
  # to keep the archive intact. Linux keeps its normal strip (GNU strip handles
  # GNU archives correctly).
  dontStrip = stdenv.isDarwin;

  nativeBuildInputs = [ cmake ninja python3 pkg-config ];

  buildInputs = [ zlib ];
})
