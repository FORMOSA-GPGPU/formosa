<!--
SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University

SPDX-License-Identifier: Apache-2.0
-->

# FORMOSA Monorepo

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

### Use locally installed FORMOSA PoCL and LLVM (Linux)

The default shell uses the PoCL and LLVM revisions pinned by the Nix flake. To
test an already-installed local PoCL, copy the private configuration example:

```bash
cp .envrc.local.example .envrc.local
```

Edit `.envrc.local`, uncomment `FORMOSA_POCL_PREFIX`, and set it to the PoCL
install prefix. To validate a local LLVM through OpenCL, also set
`FORMOSA_LLVM_PREFIX` and use a local PoCL built against that LLVM. A local LLVM
cannot be validated with the pinned PoCL.

Reload the environment after changing either prefix:

```bash
direnv reload
```

direnv validates the ICD, `libpocl`, and LLVM shared-library resolution before
activating the override. It fails instead of silently falling back to the Nix
versions, and prints a `FORMOSA LOCAL OVERRIDE ACTIVE` banner with the resolved
paths. Reinstalling into the same prefixes does not require another reload.

Confirm that the Formosa OpenCL device is visible:

```bash
clinfo -l
```

Then use the existing OpenCL workflow as usual:

```bash
cmake --preset tests.opencl
cmake --build --preset tests.opencl
ctest --preset tests.opencl
```

### No Nix? Use the Docker image

A maintainer builds and exports the image:

```bash
nix build .#formosa-docker-env
docker load < result
docker save formosa:latest -o formosa-image.tar
```

You load and run it (Docker only, no Nix):

```bash
docker load -i formosa-image.tar
docker run --rm -it formosa:latest
```

Fill in the actual image tag and any GPU/device flags for your deployment.

## Publications

- L.-C. Chen, C.-E. Wu, Y.-Y. Hsiao, C.-M. Lin, and C.-H. Chen, "Improving Stack Access Locality in RISC-V-Based GPGPU via LSU-Side Address Remapping," 2026 IEEE Asia Pacific Conference on Circuits and Systems (APCCAS), 2026. (Accepted, to appear)
- L.-C. Chen, Y.-Y. Hsiao, and C.-H. Chen, "Lunaverse: A Scriptable SoC Virtual Platform," 2025 22nd International SoC Design Conference (ISOCC), 2025. [[IEEE Xplore]](https://ieeexplore.ieee.org/document/11329791)
- Y.-Y. Hsiao, L.-C. Chen, and C.-H. Chen, "Improve GPGPU Front-end Efficiency Via Inter-Warp Instruction Sharing," 2025 IEEE International Symposium on Circuits and Systems (ISCAS), 2025. [[IEEE Xplore]](https://ieeexplore.ieee.org/document/11043684)
- Y.-Y. Hsiao, L.-C. Chen, and C.-H. Chen, "GPGPU Pipeline Visualization for RISC-V SIMT Architecture," CARRV, 2024. [[PDF]](https://carrv.github.io/2024/papers/CARRV_2024_paper_4.pdf)

## License

Distributed under the Apache License 2.0. See [LICENSE](LICENSE) for details.
Third-party code and benchmark notices are listed in
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
