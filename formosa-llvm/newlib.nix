# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

{ stdenv, lib, formosa-llvm, texinfo, fetchFromGitHub }:

stdenv.mkDerivation (finalAttrs: {
  name = "newlib";
  src = fetchFromGitHub {
    owner = "cygwin";
    repo = "cygwin";
    rev = "8ba4275b83ec27529f67e0d477611fa6d8d6e6bd";
    hash = "sha256-mqWJFZIgz6HjuommjtpqCrEl0MKEsJhLqPlvMOZNH6o=";
  };

  enableParallelBuilding = true;

  configureFlags = [
    "--target=riscv64-unknown-elf"
    "CC_FOR_TARGET=${formosa-llvm}/bin/clang --target=riscv64-unknown-elf -mcpu=formosa-gpgpu -mabi=lp64 -mno-relax -mcmodel=medany -Wno-error-implicit-function-declaration -Wno-unused-command-line-argument -Wno-error=int-conversion"
    "AS_FOR_TARGET=${formosa-llvm}/bin/clang --target=riscv64-unknown-elf -mcpu=formosa-gpgpu -mabi=lp64 -mno-relax -mcmodel=medany -c"
    "AR_FOR_TARGET=${formosa-llvm}/bin/llvm-ar"
    "LD_FOR_TARGET=${formosa-llvm}/bin/ld.lld"
    "RANLIB_FOR_TARGET=${formosa-llvm}/bin/llvm-ranlib"
  ];

  configurePhase = ''
    ./configure --prefix="$out" ${lib.escapeShellArgs finalAttrs.configureFlags}
  '';

  buildInputs = [ formosa-llvm texinfo ];
})
