# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

{ lib, stdenv, fetchurl }:

let
  # Pin to a specific Perfetto release so the hash never changes unless you
  # intentionally bump the version.  Previously this used the launcher script
  # at get.perfetto.dev/trace_processor, which always resolved to the latest
  # release and therefore broke the fixed-output hash on every upstream release.
  # The version is shared with the SDK that writes the traces.
  version = import ../third-party/perfetto-version.nix;

  artifacts = {
    "x86_64-linux" = {
      arch = "linux-amd64";
      sha256 = "sha256-JDQSmjr03J4gNKR297dkR7SQl1IT4wF2kxDTvQ1dZFg=";
    };
    "aarch64-linux" = {
      arch = "linux-arm64";
      sha256 = "sha256-oI5INhcch747pYNO7zSJqogDR2018pLtf+rMriUnR9A=";
    };
    "x86_64-darwin" = {
      arch = "mac-amd64";
      sha256 = "sha256-3zgDmEUkTDU8OZmUdIWZNFzwj3QEkbWDLn/rmk28xHY=";
    };
    "aarch64-darwin" = {
      arch = "mac-arm64";
      sha256 = "sha256-hIjtvvrfxL4VLNodQtm/zt6LR2OCleuF6zutjNFI/fQ=";
    };
  };

  plat = artifacts.${stdenv.hostPlatform.system} or (throw
    "trace_processor: unsupported platform ${stdenv.hostPlatform.system}");

  url =
    "https://commondatastorage.googleapis.com/perfetto-luci-artifacts/v${version}/${plat.arch}/trace_processor_shell";

in stdenv.mkDerivation {
  pname = "trace_processor";
  inherit version;

  src = fetchurl {
    inherit url;
    sha256 = plat.sha256;
  };

  # Pre-compiled binary – no unpack / build needed.
  dontUnpack = true;
  dontBuild = true;

  installPhase = ''
    runHook preInstall
    install -D -m 755 $src $out/bin/trace_processor
    runHook postInstall
  '';

  meta = with lib; {
    description = "A tool to query and analyze Perfetto traces using SQL";
    homepage = "https://perfetto.dev/";
    license = licenses.asl20;
    platforms = platforms.linux ++ platforms.darwin;
    mainProgram = "trace_processor";
  };
}
