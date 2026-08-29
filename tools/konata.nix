# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

{ lib, stdenvNoCC, fetchzip, makeWrapper, bash, coreutils, python3 }:

let
  src = fetchzip {
    url =
      "https://github.com/shioyadan/Konata/releases/download/v1.1.0/konata-v1.1.0.zip";
    hash = "sha256-J5B1o3NBTlvPdY5CgU7IhotiGwHRvCkRsGEQyUvMsrI=";
    stripRoot = true;
  };
in stdenvNoCC.mkDerivation {
  pname = "konata";
  version = "1.1.0";

  inherit src;

  dontBuild = true;

  nativeBuildInputs = [ makeWrapper ];

  postPatch = ''
    substituteInPlace konata.sh \
      --replace-fail 'echo "Usage: $0 TRACE1 [TRACE2]"' \
        'echo "Usage: konata TRACE1 [TRACE2]"'
  '';

  installPhase = ''
    runHook preInstall
    mkdir -p $out/libexec/konata
    cp -r . $out/libexec/konata
    makeWrapper $out/libexec/konata/konata.sh $out/bin/konata \
      --prefix PATH : ${lib.makeBinPath [ bash coreutils python3 ]}
    runHook postInstall
  '';

  meta = {
    description =
      "Browser-based instruction pipeline visualizer for Onikiri2-Kanata and gem5 O3PipeView traces.";
    homepage = "https://github.com/shioyadan/Konata";
    license = lib.licenses.bsd3;
    mainProgram = "konata";
    platforms = lib.platforms.unix;
  };
}
