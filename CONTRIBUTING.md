<!--
SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University

SPDX-License-Identifier: Apache-2.0
-->

# Contributing

Contributions are welcome. Please open a GitHub pull request against the
`main` branch.

Please avoid modifying the following unless requested by a maintainer:

- Vendored code under `third-party/`.
- Generated files without also updating their source configuration.
- Dependency repository URLs.

This repository is synchronized from our internal development repository.
After your contribution is reviewed and accepted internally, it will be
synchronized back to GitHub and your pull request will appear as merged.

This process may take some time. Please do not close the pull request while
internal review is in progress. Once the pull request is approved, do not push
additional commits. If further changes are needed, please open a new pull
request or ask a maintainer for guidance.

## Development workflows

`workflows.lua` defines the available development workflows, build profiles,
and test suites. Regenerate the committed CMake presets after changing it:

```bash
xmake workflow generate
```

Use a workflow preset when initializing or switching development contexts,
then use the matching build and test presets for the normal edit loop:

```bash
cmake --workflow --preset simtix.pipelined_sm.debug --fresh
cmake --build --preset simtix.pipelined_sm.debug
ctest --preset simtix.pipelined_sm.debug
```

The workflow step configures and builds; tests run only through the explicit
CTest command. `xmake workflow graph` prints the dependency graph as Mermaid.
Use `xmake workflow regression --profile=debug` to report tests affected by
the current branch, and add `--generate` to write a directly usable
`CMakeUserPresets.json` containing the `regression.debug` presets.
