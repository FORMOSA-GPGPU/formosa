# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

{ stdenv, cmake, ninja, python3, pkg-config, zlib, formosa-llvm, src }:

stdenv.mkDerivation {
  name = "compiler-rt";
  inherit src;

  configurePhase = ''
    cmake -B build -G Ninja -S compiler-rt \
    	    -D CMAKE_INSTALL_PREFIX=$out \
    	    -D CMAKE_SYSTEM_NAME=Linux \
    	    -D CMAKE_C_COMPILER_TARGET="riscv64-unknown-elf" \
    	    -D CMAKE_ASM_COMPILER_TARGET="riscv64-unknown-elf" \
    	    -D COMPILER_RT_DEFAULT_TARGET_ONLY=ON \
    	    -D COMPILER_RT_BAREMETAL_BUILD=ON \
    	    -D COMPILER_RT_BUILD_BUILTINS=ON \
    	    -D COMPILER_RT_BUILD_LIBFUZZER=OFF \
    	    -D COMPILER_RT_BUILD_MEMPROF=OFF \
    	    -D COMPILER_RT_BUILD_PROFILE=OFF \
    	    -D COMPILER_RT_BUILD_SANITIZERS=OFF \
    	    -D COMPILER_RT_BUILD_XRAY=OFF \
    	    -D CMAKE_C_COMPILER_WORKS=1 \
    	    -D CMAKE_CXX_COMPILER_WORKS=1 \
    	    -D CMAKE_SIZEOF_VOID_P=4 \
    	    -D CMAKE_C_COMPILER="${formosa-llvm}/bin/clang" \
    	    -D CMAKE_C_FLAGS="-march="rv64im_zicsr_zicond" -mabi=lp64 -mno-relax -mcmodel=medany" \
    	    -D CMAKE_ASM_FLAGS="-march="rv64im_zicsr_zicond" -mabi=lp64 -mno-relax -mcmodel=medany" \
    	    -D CMAKE_AR=${formosa-llvm}/bin/llvm-ar \
    	    -D CMAKE_NM=${formosa-llvm}/bin/llvm-nm \
    	    -D CMAKE_RANLIB=${formosa-llvm}/bin/llvm-ranlib \
    	    -D LLVM_CONFIG_PATH=${formosa-llvm}/bin/llvm-config
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
}
