# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

{ lib, stdenv, fetchzip, xmake, writeText }:

let
  version = import ./perfetto-version.nix;
  buildScript = writeText "perfetto-xmake.lua" ''
    set_project("perfetto-sdk")
    set_languages("c++17")
    target("perfetto", function()
      set_kind("static")
      add_files("perfetto.cc")
      add_headerfiles("perfetto.h")
      add_cxflags("-fPIC")
    end)
  '';
in stdenv.mkDerivation {
  pname = "perfetto-sdk";
  inherit version;
  src = fetchzip {
    pname = "perfetto-sdk";
    inherit version;
    url =
      "https://github.com/google/perfetto/releases/download/v${version}/perfetto-cpp-sdk-src.zip";
    stripRoot = false;
    hash = "sha256-U13USWk/avEc6igg1o00K1Veps+v03WMMTEunK6DevM=";
  };

  nativeBuildInputs = [ xmake ];
  strictDeps = true;
  enableParallelBuilding = true;

  # Apple signposts use Clang intrinsics; the FORMOSA shell also uses GCC on Darwin.
  postPatch = lib.optionalString stdenv.hostPlatform.isDarwin ''
    substituteInPlace perfetto.cc \
      --replace-fail \
        $'#if PERFETTO_BUILDFLAG(PERFETTO_OS_APPLE)\n#include <os/signpost.h>' \
        $'#if PERFETTO_BUILDFLAG(PERFETTO_OS_APPLE) && defined(__clang__)\n#include <os/signpost.h>' \
      --replace-fail \
        $'#if PERFETTO_BUILDFLAG(PERFETTO_OS_APPLE)\n    // Emit a MacOS point-of-interest signpost' \
        $'#if PERFETTO_BUILDFLAG(PERFETTO_OS_APPLE) && defined(__clang__)\n    // Emit a MacOS point-of-interest signpost'
  '';

  configurePhase = ''
    runHook preConfigure
    export XMAKE_ROOT=y
    export XMAKE_CONFIGDIR="$TMPDIR/xmake-config"
    cp ${buildScript} xmake.lua
    xmake f -y -m release --cc="$CC" --cxx="$CXX" --ar="$AR"
    runHook postConfigure
  '';
  buildPhase = ''
    runHook preBuild
    xmake build -j "$NIX_BUILD_CORES" perfetto
    runHook postBuild
  '';
  installPhase = ''
    runHook preInstall
    xmake install -o "$out" perfetto
    install -Dm644 LICENSE "$out/share/licenses/perfetto/LICENSE"
    mkdir -p "$out/lib/pkgconfig"
    cat > "$out/lib/pkgconfig/perfetto.pc" <<EOF
    Name: perfetto
    Description: Perfetto C++ tracing SDK
    Version: ${version}
    Libs: -L$out/lib -lperfetto -lpthread${
      lib.optionalString stdenv.hostPlatform.isLinux " -ldl"
    }
    Cflags: -I$out/include
    EOF
    runHook postInstall
  '';

  meta = with lib; {
    description = "Perfetto amalgamated C++ tracing SDK";
    homepage = "https://perfetto.dev/docs/instrumentation/tracing-sdk";
    license = licenses.asl20;
    platforms = platforms.linux ++ platforms.darwin;
  };
}
