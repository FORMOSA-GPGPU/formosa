<!--
SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University

SPDX-License-Identifier: Apache-2.0
-->

# FORMOSA Monorepo

The FORMOSA monorepo is an integrated RISC-V GPGPU research platform spanning
the software stack, ESL virtual platform, performance models, and Nix-based
toolchains and development environments.

## Project Structure

```
formosa/
├── simtix/        SIMT instruction-set simulator (C++)
├── fw/            Command-processor firmware
├── hal/           Hardware Abstraction Layer (HAL) + REAL
├── libcomm/       Virtual-platform IPC library
├── lv/            Lunaverse — shared runtime utilities (logging, Lua bindings)
├── formosa-llvm/  Nix packaging: LLVM toolchain (compiler-rt, newlib)
├── formosa-pocl/  Nix packaging: Portable OpenCL (PoCL)
├── tools/         Nix packaging: dev tools (konata, trace_processor, ...)
├── tests/         Cross-component integration tests
│                    (opencl, kernel-sim, baremetal-freertos, cp, hal, ipc, ...)
├── scripts/       Helper and code-generation scripts
├── cmake/         Shared CMake modules
├── third-party/   Vendored dependencies
├── flake.nix      Nix dev environment and packages
└── CMakeLists.txt Top-level build entry
```

## Contributing

Contributions are welcome. See [CONTRIBUTING.md](CONTRIBUTING.md) before
opening a pull request.

## Development Environment

Run `nix develop` at the repo root to enter the dev environment. Recommended: set up `direnv` so it loads automatically.

### Install Nix

```bash
curl --proto '=https' --tlsv1.2 -sSf -L https://install.determinate.systems/nix | sh -s -- install
```

### Enter the environment

```bash
git clone <repo-url> formosa
cd formosa
nix develop
```

First entry is slow; later entries are fast.

### Recommended: direnv (auto-load, faster entry)

Install `direnv` + `nix-direnv` once.

**NixOS / home-manager:**

```nix
programs.direnv = {
  enable = true;
  nix-direnv.enable = true;
};
```

Run `home-manager switch`, then open a new terminal.

**Ubuntu / other Linux:**

```bash
nix profile install nixpkgs#direnv nixpkgs#nix-direnv
# For bash:
echo 'eval "$(direnv hook bash)"' >> ~/.bashrc
# For zsh:
# echo 'eval "$(direnv hook zsh)"' >> ~/.zshrc
mkdir -p ~/.config/direnv
echo 'source $HOME/.nix-profile/share/nix-direnv/direnvrc' >> ~/.config/direnv/direnvrc
exec $SHELL
```

Then authorize the repo once:

```bash
cd formosa
direnv allow
```

After this, `cd` into the repo auto-loads the environment and leaving unloads it.

### No Nix? Use the Docker image

A public Docker image containing the default FORMOSA development environment is
available from the
[GitHub Container Registry](https://github.com/FORMOSA-GPGPU/formosa/pkgs/container/formosa).
It supports Linux on AMD64 and does not require authentication to pull.

```bash
docker pull ghcr.io/formosa-gpgpu/formosa:latest
```

From the repository root, start an interactive shell with the checkout mounted
at `/workspace`:

```bash
docker run --rm -it \
  --volume "$PWD:/workspace" \
  --workdir /workspace \
  ghcr.io/formosa-gpgpu/formosa:latest
```

The `latest` tag tracks the most recently published environment from `main`.
For reproducible use, pin an immutable `sha-<full Git commit SHA>` image tag.
Do not use the registry-generated `sha256-...` attestation tag.

## Publications

- L.-C. Chen, C.-E. Wu, Y.-Y. Hsiao, C.-M. Lin, and C.-H. Chen, "Improving Stack Access Locality in RISC-V-Based GPGPU via LSU-Side Address Remapping," 2026 IEEE Asia Pacific Conference on Circuits and Systems (APCCAS), 2026. (Accepted, to appear)
- L.-C. Chen, Y.-Y. Hsiao, and C.-H. Chen, "Lunaverse: A Scriptable SoC Virtual Platform," 2025 22nd International SoC Design Conference (ISOCC), 2025. [[IEEE Xplore]](https://ieeexplore.ieee.org/document/11329791)
- Y.-Y. Hsiao, L.-C. Chen, and C.-H. Chen, "Improve GPGPU Front-end Efficiency Via Inter-Warp Instruction Sharing," 2025 IEEE International Symposium on Circuits and Systems (ISCAS), 2025. [[IEEE Xplore]](https://ieeexplore.ieee.org/document/11043684)
- Y.-Y. Hsiao, L.-C. Chen, and C.-H. Chen, "GPGPU Pipeline Visualization for RISC-V SIMT Architecture," CARRV, 2024. [[PDF]](https://carrv.github.io/2024/papers/CARRV_2024_paper_4.pdf)

## License

Distributed under the Apache License 2.0. See [LICENSE](LICENSE) for details.
Third-party code and benchmark notices are listed in
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
