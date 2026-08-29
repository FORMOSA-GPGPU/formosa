# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

{ stdenv, lib, cmake, ninja, python3, pkg-config, zlib, libedit, src, swig
, fixDarwinDylibNames, }:

stdenv.mkDerivation {
  pname = "formosa-llvm-unwrapped";
  version = "20.1.8";
  src = src;

  configurePhase = ''
    cmake -B build -G Ninja -S llvm \
          -D CMAKE_INSTALL_PREFIX=$out \
          -D LLVM_ENABLE_PROJECTS="clang;lld;lldb" \
          -D CMAKE_BUILD_TYPE=Release \
          -D LLVM_DEFAULT_TARGET_TRIPLE=riscv64-unknown-elf \
          -D LLDB_INCLUDE_TESTS=OFF \
          -D LLVM_TARGETS_TO_BUILD="X86;RISCV;NVPTX" \
          -D BUILD_SHARED_LIBS=ON \
          -D LLDB_ENABLE_LIBEDIT=ON \
          -D LLDB_ENABLE_PYTHON=ON \
          -D LLDB_USE_SYSTEM_DEBUGSERVER=ON
  '';

  buildPhase = ''
    cmake --build build
  '';

  installPhase = ''
    cmake --build build --target install
  '';

  # BUILD_SHARED_LIBS=ON emits ~110 separate libLLVM*.dylib (plus libclang-cpp),
  # and LLVM gives each an @rpath/lib*.dylib install name. Downstream consumers
  # (spirv-llvm-translator, pocl, ...) then record those @rpath names and
  # fail to load the libraries at runtime unless every one of them adds an rpath
  # back to this package. fixDarwinDylibNames rewrites each dylib's install id to
  # its absolute store path (and fixes the inter-library references), so linkers
  # record absolute paths and no consumer needs an rpath workaround. No-op on
  # Linux, where the libraries use sonames instead.
  nativeBuildInputs = [ cmake ninja python3 pkg-config ]
    ++ lib.optional stdenv.isDarwin fixDarwinDylibNames;

  buildInputs = [ zlib libedit swig ];
}
