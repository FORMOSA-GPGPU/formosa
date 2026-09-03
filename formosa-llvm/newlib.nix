# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

{ stdenv, formosa-llvm, texinfo, fetchFromGitHub }:

stdenv.mkDerivation {
  name = "newlib";
  src = fetchFromGitHub {
    owner = "cygwin";
    repo = "cygwin";
    rev = "8ba4275b83ec27529f67e0d477611fa6d8d6e6bd";
    hash = "sha256-mqWJFZIgz6HjuommjtpqCrEl0MKEsJhLqPlvMOZNH6o=";
  };

  enableParallelBuilding = true;

  configurePhase = ''
    ./configure --prefix=$out \
    --target=riscv64-unknown-elf \
    CC_FOR_TARGET="${formosa-llvm}/bin/clang -march="rv64im_zicsr_zicond" -mabi=lp64 \
    -mno-relax -mcmodel=medany \
    -Wno-error-implicit-function-declaration \
    -Wno-unused-command-line-argument \
    -Wno-error=int-conversion" \
    AS_FOR_TARGET="${formosa-llvm}/bin/llvm-as -march="rv64im_zicsr_zicond" -mabi=lp64" \
    AR_FOR_TARGET=${formosa-llvm}/bin/llvm-ar \
    LD_FOR_TARGET=${formosa-llvm}/bin/llvm-ld \
    RANLIB_FOR_TARGET=${formosa-llvm}/bin/llvm-ranlib
  '';

  buildInputs = [ formosa-llvm texinfo ];
}
