# SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
#
# SPDX-License-Identifier: Apache-2.0

{
  description = "FORMOSA GPGPU";

  inputs = {
    nixpkgs.url = "github:nixos/nixpkgs?ref=nixos-25.11";
    flake-utils.url = "github:numtide/flake-utils";

  };

  outputs = { self, nixpkgs, flake-utils, ... }:
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

        formosaToolchain = import ./formosa-llvm/default.nix { inherit pkgs; };

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
        #   - openssh: the CI image's default before_script runs
        #     ssh-agent/ssh-add/ssh-keyscan, so the image must ship an ssh
        #     client. On macOS this shadows /usr/bin/ssh in the dev shell; if
        #     ~/.ssh/config uses Apple-only options (e.g. UseKeychain), either
        #     add `IgnoreUnknown UseKeychain` to it or point git at the system
        #     ssh with `git config --global core.sshCommand /usr/bin/ssh`.
        ciPackages = with pkgs; [ pre-commit clang-tools stylua gcovr openssh ];

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

        # buildNixShellImage hardcodes the image config's `created` field to
        # 1970-01-01 (it exposes no `created` arg). The container-registry
        # cleanup orders tags solely by that timestamp with no tie-break, so
        # identical timestamps make it delete tags at random. We re-stamp
        # `created` to the flake's commit time (`self.lastModified`) by rewriting
        # only that one field in the image config; the layers — and therefore
        # Cmd/Env/Entrypoint — are left byte-for-byte intact. On a clean tree
        # `self.lastModified` is HEAD's committer time, so `nix build` is
        # reproducible (same commit -> same bytes). On a dirty tree it falls back
        # to the working-tree file mtimes (no commit), so scripts/update-docker-env.sh
        # refuses to publish a dirty tree — published images always map to a commit.
        buildDockerImage = name: drv:
          let
            baseImage = pkgs.dockerTools.buildNixShellImage {
              inherit name drv;
              uid = 0;
            };
            # `@N` is GNU date's epoch syntax; self.lastModified is HEAD's
            # committer time on a clean tree (file mtimes on a dirty one).
            createdEpoch = "@${toString self.lastModified}";
          in pkgs.runCommand "${name}-env.tar.gz" {
            nativeBuildInputs = [ pkgs.jq ];
          } ''
            mkdir unpacked
            tar -xf ${baseImage} -C unpacked
            cd unpacked

            created=$(date -u -d "${createdEpoch}" +%Y-%m-%dT%H:%M:%SZ)
            config=$(jq -r '.[0].Config' manifest.json)
            jq --arg c "$created" '.created = $c' "$config" > config.new

            # The config file is named after the sha256 of its content; rewriting
            # `created` changes that hash, so rename the file and repoint the
            # manifest (docker load resolves the config by the manifest's Config
            # field). Layers are untouched, so the image is otherwise identical.
            newname="$(sha256sum config.new | cut -d' ' -f1).json"
            mv config.new "$newname"
            [ "$newname" != "$config" ] && rm -f "$config"
            jq --arg new "$newname" '.[0].Config = $new' manifest.json > manifest.new
            mv manifest.new manifest.json

            tar --sort=name --owner=0 --group=0 --numeric-owner --mtime=@0 \
              -cf - . | gzip -n > $out
          '';
      in {
        devShells = {
          default = defaultDevShell;
          formosa-pocl = formosaPoclDevShell;
        } // projectDevShells;

        packages = {
          dev-shell = defaultDevShell;
          formosa-docker-env = buildDockerImage "formosa" defaultDevShell;
        } // projectPackages // softwareStacks // tools
          // (pkgs.lib.attrsets.mapAttrs' (name: value: {
            name = "${name}-docker-env";
            value = buildDockerImage name value;
          }) projectDevShells); # Per-project dev-shell docker image
      });
}
