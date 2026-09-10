-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

task("workflow")
set_category("plugin")
set_menu({
  usage = "xmake workflow <command> [options]",
  description = "Analyze workflows and generate build-tool presets.",
  options = {
    { nil, "arguments", "vs", nil, "Command followed by command options" },
  },
})
on_run(function() import("workflow.main").run() end)
task_end()
