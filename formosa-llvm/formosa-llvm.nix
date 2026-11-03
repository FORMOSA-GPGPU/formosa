# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

{ stdenv, lib, cmake, ninja, python3, pkg-config, zlib, libedit, src, swig
, fixDarwinDylibNames, }:

stdenv.mkDerivation (finalAttrs: {
  pname = "formosa-llvm-unwrapped";
  version = "20.1.8";
  src = src;

  cmakeFlags = [
    "-DLLVM_ENABLE_PROJECTS=clang;lld;lldb"
    "-DCMAKE_BUILD_TYPE=Release"
    "-DLLDB_INCLUDE_TESTS=OFF"
    "-DLLVM_TARGETS_TO_BUILD=X86;Native;RISCV;NVPTX"
    "-DBUILD_SHARED_LIBS=ON"
    "-DLLDB_ENABLE_LIBEDIT=ON"
    "-DLLDB_ENABLE_PYTHON=ON"
    "-DLLDB_USE_SYSTEM_DEBUGSERVER=ON"
  ];

  configurePhase = ''
    cmake -B build -G Ninja -S llvm -DCMAKE_INSTALL_PREFIX=$out \
      ${lib.escapeShellArgs finalAttrs.cmakeFlags}
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
})
