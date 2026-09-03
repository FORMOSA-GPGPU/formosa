# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

{
  description = "FORMOSA GPGPU";

  inputs = {
    nixpkgs.url = "github:nixos/nixpkgs?ref=nixos-25.11";
    flake-utils.url = "github:numtide/flake-utils";

    # Keep private source fetching in the flake evaluator.  Developers use
    # their existing SSH agent; CI rewrites these URLs to its read-only HTTPS
    # job-token URL before evaluation.  The lock file records the exact tree
    # hash, so the source identity does not depend on a mutable timestamp.
    formosa-llvm-src = {
      url =
        "git+https://github.com/FORMOSA-GPGPU/formosa-llvm.git?rev=cfd7f122277767595096114ec366c38abeffbf48&shallow=1";
      flake = false;
    };
    formosa-pocl-src = {
      url =
        "git+https://github.com/FORMOSA-GPGPU/formosa-pocl.git?rev=ebc9805ba5ec6a1061dca2a9c3b344770c43e29a&shallow=1&submodules=1";
      flake = false;
    };

  };

  outputs =
    { self, nixpkgs, flake-utils, formosa-llvm-src, formosa-pocl-src, ... }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = import nixpkgs {
          inherit system;
          config.allowUnfree = true;
        };

        projectPackages = { hal = import ./hal/default.nix { inherit pkgs; }; };

        gemmaPython = pkgs.python313.withPackages (ps:
          with ps;
          [ matplotlib numpy safetensors sentencepiece torch tqdm transformers ]
          ++ [ ps."huggingface-hub" ]);

        formosaToolchain = import ./formosa-llvm/default.nix {
          inherit pkgs;
          src = formosa-llvm-src;
        };

        softwareStacks = rec {
          formosa-llvm = formosaToolchain.llvm;
          formosa-sysroot = formosaToolchain.formosa-sysroot;
          formosa-clang-cross = formosaToolchain.formosa-clang-cross;
          spirvLlvm = let
            spirvLlvmBase = import
              "${pkgs.path}/pkgs/by-name/sp/spirv-llvm-translator/package.nix";
            llvmForTranslator = formosa-llvm.overrideAttrs (old: {
              passthru = (old.passthru or { }) // { dev = formosa-llvm; };
            });
          in (pkgs.callPackage spirvLlvmBase {
            llvm = llvmForTranslator;
          }).overrideAttrs (old: {
            # formosa-llvm is a monolithic output that bundles clang/clang++
            # (default target riscv64) in bin, and spirv-llvm-translator puts
            # llvm.dev on PATH via nativeBuildInputs. On Darwin CMake's compiler
            # detection then picks up that riscv-default clang as the host
            # compiler and chokes on the Apple SDK's -arch flag. Pin the host
            # compiler to the stdenv cc so it isn't shadowed.
            cmakeFlags = (old.cmakeFlags or [ ]) ++ [
              "-DCMAKE_C_COMPILER=${pkgs.stdenv.cc}/bin/cc"
              "-DCMAKE_CXX_COMPILER=${pkgs.stdenv.cc}/bin/c++"
            ];
          });
          "spirv-llvm" = spirvLlvm;
          formosa-pocl = import ./formosa-pocl/default.nix {
            inherit pkgs formosa-llvm formosa-clang-cross spirvLlvm;
            src = formosa-pocl-src;
            hal = projectPackages.hal;
          };
        };

        tools = {
          konata = pkgs.callPackage ./tools/konata.nix { };
          trace_processor = pkgs.callPackage ./tools/trace_processor.nix { };
        };

        projectDevShells = rec {
          libcomm = import ./libcomm/shell.nix { inherit pkgs; };
          simtix = import ./simtix/shell.nix { inherit pkgs; };
          lv = import ./lv/shell.nix { inherit pkgs; };
          sw = import ./hal/default.nix { inherit pkgs; };
        };

        formosaPoclDevShell = import ./formosa-pocl/shell.nix {
          inherit pkgs;
          formosa-pocl = softwareStacks.formosa-pocl;
        };

        basePackages = with pkgs; [
          git
          curl
          reviewdog
          reuse
          jq
          nixfmt-classic
          tree
          black
          lua-language-server
          opencl-headers
          opencl-clhpp
          ocl-icd
          clinfo
        ];

        # Tools the CI pipeline expects inside the docker env image (built from
        # defaultDevShell below), collected in one place instead of scattered
        # through the per-project dev shells:
        #   - pre-commit + reviewdog (basePackages) run in the lint job; the
        #     language:system hooks additionally need clang-format
        #     (clang-tools), nixfmt (basePackages), stylua, luajit (lv shell)
        #     and python (gemmaPython) on PATH. The cpplint/black hooks are
        #     language:python, installed by pre-commit into its own venv.
        #   - gcovr produces the coverage report in test jobs.
        #   - openssh: local developers use the flake's private Git sources via
        #     their existing SSH setup.  CI uses the job-token HTTPS rewrite in
        #     the bootstrap image, so it does not need an SSH agent there. On
        #     macOS this package can shadow /usr/bin/ssh in the dev shell; if
        #     ~/.ssh/config uses Apple-only options (e.g. UseKeychain), either
        #     add `IgnoreUnknown UseKeychain` to it or point git at the system
        #     ssh with `git config --global core.sshCommand /usr/bin/ssh`.
        ciPackages = with pkgs; [ pre-commit clang-tools stylua gcovr openssh ];

        # Container executors need a stable shell entrypoint.  Keeping it
        # in the image means jobs can select an image by digest without having
        # to interpolate store paths into every CI YAML file.
        containerEntrypoint =
          pkgs.writeShellScriptBin "formosa-container-entrypoint" ''
            set -euo pipefail

            shell=$1
            rcfile=$2
            shift 2

            if (( $# == 0 )); then
              exec "$shell" --rcfile "$rcfile" -i
            fi
            if (( $# == 1 )); then
              exec "$shell" --rcfile "$rcfile" -ci "$1"
            fi

            printf -v command ' %q' "$@"
            exec "$shell" --rcfile "$rcfile" -ci "exec''${command}"
          '';

        toolPackages = pkgs.lib.mapAttrsToList (name: value: value) tools;
        shellToolBins = [
          "${softwareStacks.formosa-llvm}/bin"
          "${softwareStacks.formosa-clang-cross}/bin"
          "${softwareStacks.spirvLlvm}/bin"
          "${softwareStacks.formosa-pocl}/bin"
        ];

        # All-in-one dev shell for FORMOSA GPGPU projects.
        defaultDevShell = pkgs.mkShell.override { stdenv = pkgs.gccStdenv; } {
          name = "formosa";
          inputsFrom =
            pkgs.lib.mapAttrsToList (name: value: value) projectDevShells;
          packages = [ gemmaPython softwareStacks.formosa-pocl ] ++ basePackages
            ++ ciPackages ++ toolPackages;
          AGENT_SOCKET_PATH = "/tmp/formosa.sock";
          OCL_ICD_VENDORS = "${softwareStacks.formosa-pocl}/etc/OpenCL/vendors";
          shellHook = ''
            export PATH="${pkgs.lib.concatStringsSep ":" shellToolBins}:$PATH"
            # Prefer Formosa OpenCL (ocl-icd + formosa-pocl) over any host ICD
            export LD_LIBRARY_PATH="${pkgs.ocl-icd}/lib''${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
          '';
        };

        # A lightweight identity for preflight lookup.  Evaluating the Docker
        # archive itself asks Nix to materialize the full shell closure; this
        # text output only records the shell and entrypoint derivation paths,
        # so registry hits do not download the image contents just to decide
        # whether a rebuild is needed.  Bump the format version when the image
        # transformation below changes independently of these paths.
        formosaDockerEnvFingerprint =
          pkgs.writeText "formosa-docker-env-fingerprint" ''
            format-version = 2
            dev-shell = ${defaultDevShell}
            entrypoint = ${containerEntrypoint}
          '';

        rewriteImageConfig = outputName: image: transform:
          pkgs.runCommand outputName { nativeBuildInputs = [ pkgs.jq ]; } ''
            mkdir unpacked
            tar -xf ${image} -C unpacked
            cd unpacked

            config=$(jq -r '.[0].Config' manifest.json)
            ${transform}

            newname="$(sha256sum config.new | cut -d' ' -f1).json"
            mv config.new "$newname"
            [ "$newname" != "$config" ] && rm -f "$config"
            jq --arg new "$newname" '.[0].Config = $new' manifest.json > manifest.new
            mv manifest.new manifest.json

            tar --sort=name --owner=0 --group=0 --numeric-owner --mtime=@0 \
              -cf - . | gzip -n > $out
          '';

        rawDockerImage = name: drv:
          pkgs.dockerTools.buildNixShellImage {
            inherit name drv;
            uid = 0;
          };

        formosaDockerShell = defaultDevShell.overrideAttrs (old: {
          nativeBuildInputs = (old.nativeBuildInputs or [ ])
            ++ [ containerEntrypoint ];
        });

        # This output contains only semantically relevant environment content.
        # buildNixShellImage's constant `created` timestamp is intentionally
        # retained here, so its store hash is stable across commits that do not
        # change the environment.
        formosaDockerImageContent =
          rewriteImageConfig "formosa-env-content.tar.gz"
          (rawDockerImage "formosa" formosaDockerShell) ''
            jq --arg entrypoint "${containerEntrypoint}/bin/formosa-container-entrypoint" '
              if (.config.Cmd | length) != 3 or .config.Cmd[1] != "--rcfile" then
                error("unexpected buildNixShellImage Cmd")
              else
                .config.Entrypoint = [$entrypoint, .config.Cmd[0], .config.Cmd[2]]
                | .config.Cmd = []
              end
            ' "$config" > config.new
          '';

        # Registry cleanup orders tags by image `created`.  Add the commit time
        # only to the published presentation output; the lightweight
        # fingerprint output above remains the canonical environment identity.
        stampDockerImage = name: baseImage:
          let
            createdEpoch = "@${toString self.lastModified}";
            fingerprint = builtins.unsafeDiscardStringContext
              (builtins.substring 0 32
                (builtins.baseNameOf formosaDockerEnvFingerprint.drvPath));
          in rewriteImageConfig "${name}-env.tar.gz" baseImage ''
            created=$(date -u -d "${createdEpoch}" +%Y-%m-%dT%H:%M:%SZ)
            jq --arg created "$created" --arg fingerprint "${fingerprint}" '
              .created = $created
              | .config.Labels = ((.config.Labels // {}) + {
                  "org.formosa.nix-fingerprint": $fingerprint
                })
            ' "$config" > config.new
          '';

        buildDockerImage = name: drv:
          stampDockerImage name (rawDockerImage name drv);

        # Maintainer-built once and pinned in the CI configuration.  This image
        # is deliberately separate from the product environment: it is the
        # control plane used to evaluate, inspect and publish environments.
        ciBootstrapPackages = with pkgs; [
          attic-client
          bashInteractive
          cacert
          coreutils
          curl
          git
          gnugrep
          gnutar
          jq
          nix
          openssh
          ripgrep
          skopeo
        ];
        formosaCiBootstrap = pkgs.dockerTools.buildLayeredImage {
          name = "formosa-ci-bootstrap";
          tag = "bootstrap";
          contents = ciBootstrapPackages;
          config = {
            Env = [
              "PATH=${pkgs.lib.makeBinPath ciBootstrapPackages}"
              "SSL_CERT_FILE=${pkgs.cacert}/etc/ssl/certs/ca-bundle.crt"
              "NIX_CONFIG=experimental-features = nix-command flakes"
            ];
            WorkingDir = "/builds";
          };
        };
      in {
        devShells = {
          default = defaultDevShell;
          formosa-pocl = formosaPoclDevShell;
        } // projectDevShells;

        packages = {
          dev-shell = defaultDevShell;
          formosa-docker-env-fingerprint = formosaDockerEnvFingerprint;
          formosa-docker-env-content = formosaDockerImageContent;
          formosa-docker-env =
            stampDockerImage "formosa" formosaDockerImageContent;
          formosa-ci-bootstrap = formosaCiBootstrap;
        } // projectPackages // softwareStacks // tools
          // (pkgs.lib.attrsets.mapAttrs' (name: value: {
            name = "${name}-docker-env";
            value = buildDockerImage name value;
          }) projectDevShells); # Per-project dev-shell docker image
      });
}
