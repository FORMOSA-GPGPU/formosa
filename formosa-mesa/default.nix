# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
# SPDX-License-Identifier: Apache-2.0

{ pkgs, src, formosa-llvm, formosa-pocl, formosa-clang-cross, hal }:

let
  # Installed drivers retain the compiler/runtime selected at build time.
  shaderCompiler = pkgs.writeShellScriptBin "formosa-mesa-clang" ''
    unset FORMOSA_LLVM_PREFIX FORMOSA_RUNTIME_PREFIX
    exec ${formosa-clang-cross}/bin/formosa-clang "$@"
  '';
  nativeFile = pkgs.writeText "formosa-mesa.ini" ''
    [binaries]
    llvm-config = '${formosa-llvm}/bin/llvm-config'
    poclcc = '${formosa-pocl}/bin/poclcc'
    formosa-clang = '${shaderCompiler}/bin/formosa-mesa-clang'

    [built-in options]
    libdir = 'lib'
    buildtype = 'debugoptimized'
    cpp_rtti = false
    wrap_mode = 'nofallback'

    [project options]
    platforms = []
    gallium-drivers = []
    vulkan-drivers = ['formosa']
    glx = 'disabled'
    egl = 'disabled'
    gles1 = 'disabled'
    gles2 = 'disabled'
    opengl = false
    gbm = 'disabled'
    llvm = 'enabled'
    shared-llvm = 'enabled'
    tools = ['fsa-compute']
    build-tests = false
    video-codecs = []
    formosa-sdk = '${hal}'
  '';
in pkgs.gccStdenv.mkDerivation {
  pname = "formosa-mesa";
  version = "25.1.0-dev";
  inherit src;
  nativeBuildInputs = with pkgs; [
    meson
    ninja
    pkg-config
    bison
    flex
    glslang
    (python3.withPackages (ps: with ps; [ mako pyyaml packaging ]))
  ];
  buildInputs = with pkgs; [
    zlib
    zstd
    expat
    libdrm
    spirv-tools
    glslang
    fftw
    formosa-llvm
  ];
  mesonFlags = [ "--native-file=${nativeFile}" ];
  mesonBuildType = "debugoptimized";
  mesonAutoFeatures = "auto";
  mesonWrapMode = "nofallback";
  passthru = { inherit nativeFile; };
  meta.platforms = pkgs.lib.platforms.linux;
}
