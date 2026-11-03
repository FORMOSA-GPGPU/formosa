# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

{ pkgs ? import <nixpkgs> { }, src }:

let
  # LLVM (clang/lld/lldb) is built exactly once here.
  formosa-llvm-unwrapped = pkgs.callPackage ./formosa-llvm.nix { inherit src; };
  compiler-rt = pkgs.callPackage ./compiler-rt.nix {
    inherit src;
    formosa-llvm = formosa-llvm-unwrapped;
  };
  newlib =
    pkgs.callPackage ./newlib.nix { formosa-llvm = formosa-llvm-unwrapped; };

  # Assemble the final single package without rebuilding LLVM. Each of the
  # three inputs is layered in with the tool that fits its shape:
  #   - LLVM:        cloned wholesale as a symlink forest (real dirs, symlinked
  #                  files). cp -as is the only one of the three that mirrors a
  #                  large tree in place; --no-preserve=mode keeps the cloned
  #                  dirs writable (the store originals are 0555) so we can
  #                  drop links into them below, without a post-hoc chmod.
  #   - compiler-rt: a single dir grafted at a fixed point in the resource dir,
  #                  so a plain ln -s is enough.
  #   - newlib:      a self-contained prefix tree merged at $out root, which is
  #                  exactly what stow is for.
  llvm = pkgs.stdenv.mkDerivation {
    pname = "formosa-llvm";
    version = "20.1.8";
    dontUnpack = true;
    nativeBuildInputs = [ pkgs.stow ];
    installPhase = ''
      runHook preInstall
      mkdir -p $out
      cp -as --no-preserve=mode ${formosa-llvm-unwrapped}/. $out/

      CLANG_VERSION=$($out/bin/clang --version | grep -oP 'clang version \K[0-9]+')

      # compiler-rt builtins into the clang resource dir. compiler-rt installs
      # them under its default OS dir/name; clang 20 looks up the normalized
      # baremetal target path/name when linking with --rtlib=compiler-rt.
      mkdir -p $out/lib/clang/$CLANG_VERSION/lib
      ln -s ${compiler-rt}/lib/linux $out/lib/clang/$CLANG_VERSION/lib/linux
      mkdir -p $out/lib/clang/$CLANG_VERSION/lib/riscv64-unknown-unknown-elf
      ln -s ${compiler-rt}/lib/linux/libclang_rt.builtins-riscv64.a \
        $out/lib/clang/$CLANG_VERSION/lib/riscv64-unknown-unknown-elf/libclang_rt.builtins.a

      # Stable, version-independent alias for the resource dir so the clang
      # wrappers can reference it without re-deriving CLANG_VERSION at runtime.
      ln -s $CLANG_VERSION $out/lib/clang/current

      # newlib into the sysroot ($out/riscv64-unknown-elf)
      cd ${newlib}/.. && stow ${builtins.baseNameOf newlib} -t $out
      runHook postInstall
    '';
  };

  formosa-sysroot = "${llvm}/riscv64-unknown-elf";
  # A local compiler can reuse the packaged newlib/compiler-rt. Explicitly
  # select the GNU-compatible RISC-V driver: a local clang has no adjacent
  # sysroot to discover, and the BareMetal driver omits newlib's -lgloss.
  formosa-clang-cross = pkgs.symlinkJoin {
    name = "formosa-clang-cross";
    paths = map (compiler:
      pkgs.writeShellScriptBin "formosa-${compiler}" ''
        llvm_prefix="''${FORMOSA_LLVM_PREFIX:-${llvm}}"
        runtime_prefix="''${FORMOSA_RUNTIME_PREFIX:-${llvm}}"
        if [[ -n ''${FORMOSA_RUNTIME_PREFIX:-} ]]; then
          for runtime_file in \
            riscv64-unknown-elf/include/stdio.h \
            riscv64-unknown-elf/lib/libc.a \
            riscv64-unknown-elf/lib/libm.a \
            riscv64-unknown-elf/lib/libgloss.a \
            lib/clang/current/include/stddef.h \
            lib/clang/current/include/opencl-c.h \
            lib/clang/current/include/opencl-c-base.h \
            lib/clang/current/lib/riscv64-unknown-unknown-elf/libclang_rt.builtins.a; do
            if [[ ! -f $runtime_prefix/$runtime_file || ! -r $runtime_prefix/$runtime_file ]]; then
              echo "FORMOSA runtime is missing a readable $runtime_file: $runtime_prefix" >&2
              exit 1
            fi
          done
        fi
        exec "$llvm_prefix/bin/${compiler}" \
          -no-canonical-prefixes \
          --target=riscv64-unknown-elf \
          --gcc-toolchain="$runtime_prefix" \
          --sysroot="$runtime_prefix/riscv64-unknown-elf" \
          -resource-dir="$runtime_prefix/lib/clang/current" \
          --rtlib=compiler-rt \
          -fuse-ld=lld \
          --ld-path="$llvm_prefix/bin/ld.lld" \
          -B"$llvm_prefix/bin" \
          "$@"
      '') [ "clang" "clang++" ];
  };
in {
  inherit llvm formosa-llvm-unwrapped compiler-rt newlib formosa-sysroot
    formosa-clang-cross;
}
