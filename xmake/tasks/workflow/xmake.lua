-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

task("workflow", function()
  set_category("plugin")
  set_menu({
    usage = "xmake workflow <run|build|regression|list|graph|generate|check> [options]",
    description = [[Select, build and test workflows, or generate CMake presets.

Commands:
  run NAME     Configure, build, install when required, and test a workflow.
  build NAME   Configure, build, and install when required; skip tests.
  regression   Plan affected suites; add --execute to run or --all for all suites.
  list         List workflows, test suites and build profiles from workflows.lua.
  graph        Print the component, suite, and workflow impact graph as Mermaid.
  generate     Regenerate CMakePresets.json from workflows.lua.
  check        Check that CMakePresets.json matches workflows.lua.

Run xmake workflow COMMAND --help for command options, defaults and examples.
Run xmake workflow list to discover available names.]],
    options = {
      { nil, "arguments", "vs", nil, "Command followed by command options" },
    },
  })
  on_run(function() import("workflow.main").run() end)
end)
