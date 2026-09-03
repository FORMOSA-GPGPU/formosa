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
  # `${llvm}/bin/clang` is a symlink into the unwrapped LLVM tree (the assemble
  # derivation above mirrors LLVM as a symlink forest). Without
  # -no-canonical-prefixes, clang resolves that symlink to find its install dir,
  # which lands in the unwrapped tree where no `riscv64-unknown-elf/` sysroot is
  # adjacent. clang picks its RISC-V bare-metal driver by probing for an adjacent
  # `<triple>/lib/crt0.o` (--sysroot does not affect this choice), so it would
  # fall back to the BareMetal toolchain that links only -lc and drops -lgloss,
  # leaving newlib's reent syscall stubs (_write/_sbrk/...) undefined. Keeping the
  # prefix uncanonicalized pins the install dir to ${llvm}/bin, where the stowed
  # sysroot is adjacent, so clang selects the gcc-compatible RISCVToolChain and
  # auto-links --start-group -lc -lgloss --end-group as before the symlink merge.
  formosa-clang-cross = pkgs.symlinkJoin {
    name = "formosa-clang-cross";
    paths = [
      (pkgs.writeShellScriptBin "formosa-clang" ''
        exec ${llvm}/bin/clang \
          -no-canonical-prefixes \
          --target=riscv64-unknown-elf \
          --sysroot=${formosa-sysroot} \
          -resource-dir=${llvm}/lib/clang/current \
          --rtlib=compiler-rt \
          -fuse-ld=lld \
          --ld-path=${llvm}/bin/ld.lld \
          -B${llvm}/bin \
          "$@"
      '')
      (pkgs.writeShellScriptBin "formosa-clang++" ''
        exec ${llvm}/bin/clang++ \
          -no-canonical-prefixes \
          --target=riscv64-unknown-elf \
          --sysroot=${formosa-sysroot} \
          -resource-dir=${llvm}/lib/clang/current \
          --rtlib=compiler-rt \
          -fuse-ld=lld \
          --ld-path=${llvm}/bin/ld.lld \
          -B${llvm}/bin \
          "$@"
      '')
    ];
  };
in { inherit llvm compiler-rt newlib formosa-sysroot formosa-clang-cross; }
