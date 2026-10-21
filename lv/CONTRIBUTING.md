<!--
SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University

SPDX-License-Identifier: Apache-2.0
-->

# Contributing to lv

Use the repository-root development environment and Xmake workflow:

```sh
nix develop
xmake f -m debug --workflow=lv
xmake
xmake test
```

`xmake test` builds the dependencies required by its selected cases. To select
SystemC cases, use `xmake test 'lv/unit/systemc/**'`.

For lv coverage in a separate output directory:

```sh
xmake f -m coverage -o build-lv-coverage --workflow=lv
xmake test
gcovr --root . --filter lv/ --html-details -o build-lv-coverage/coverage.html build-lv-coverage
```

This measures the lv workflow; the complete product regression is documented
in the [contributor guide](../CONTRIBUTING.md#development-workflows).
FORMOSA's CMake source build is deprecated. Use the
[temporary fallback policy](../CONTRIBUTING.md#internal-cmake-fallback-during-the-transition)
if the native build/test path fails.

Xmake's docgen target updates `.cache/lv_meta/lv_api.lua` for LuaLS. The root
`.luarc.json` includes these generated definitions.
VS Code users can install the
[Lua extension](https://marketplace.visualstudio.com/items?itemName=sumneko.lua).

Before committing, run `pre-commit run --all-files` in the development
environment. Use `pre-commit install` to enable the hooks for future commits.
