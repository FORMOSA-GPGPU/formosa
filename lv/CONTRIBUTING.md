<!--
SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University

SPDX-License-Identifier: Apache-2.0
-->

# Contributing to lv

## Developer guidelines

- If you have [Nix](https://nixos.org) installed, you can enter the dev shell directly with:

  ```bash
  nix develop -c $SHELL
  ```

  This way, you don't have to manually install the packages listed below.

- Install `ninja` for faster builds.

  ```bash
  sudo apt install ninja-build
  ```

- To enable tests and coverage analysis, append the following options when configuring:

  - `-D LV_ENABLE_TESTING=1`

  - `-D LV_ENABLE_GCOV=1`

  ```bash
  cmake -S . -B build -G Ninja -D ENABLE_TESTING=1 -D ENABLE_GCOV=1
  ```

- To run tests, you can use `ctest` (installed along with `cmake`)

  ```bash
  cmake --build build  # you need to build the tests first
  ctest --test-dir build  # cd build && ctest
  ```

  or use the `test` target in `make` or `ninja`.

  ```bash
  cd build && ninja test
  ```

- For the code coverage report, you will need `gcovr`.

  Note that `pipx` is recommended to install command line tools.

  ```bash
  sudo apt install pipx
  pipx install gcovr
  ```

  After running the test, execute `gcovr` in the project root directly to see the summary of coverage report.

  Append `-e /path` if you want to exclude some paths for coverage report.

  ```bash
  gcovr -e build -e ext  # typically we ignore external sources
  ```

  To generate a detailed HTML report, do

  ```bash
  gcovr --html-details -o build/coverage.html
  ```

  After that, you can open the report, `build/coverage.html`, using web browsers.

- Lua language server is supported. LuaLS will find the definition of Lunaverse-defined Lua opjects
under `lua/library`, as defined in `.luarc.json`. For VS Code users, please download the [Lua
extension](https://marketplace.visualstudio.com/items?itemName=sumneko.lua), and various language
features will automatically be enabled.

## Git commits

- Install `pre-commit` and static analysis hooks to ensure code quality.

  ```bash
  sudo apt install pipx
  pipx install pre-commit clang-format
  ```

  Also, make sure you install [StyLua](https://github.com/JohnnyMorganz/StyLua) from the [pre-built binaries](https://github.com/JohnnyMorganz/StyLua/releases).

  Run the hook over all files to make sure they are installed correctly.

  ```bash
  pre-commit run --all-files
  ```

  Finally, install the hook to the git repository.

  ```bash
  pre-commit install
  ```
