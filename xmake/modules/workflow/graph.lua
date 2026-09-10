-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

local frontend = import("workflow.frontend")

local function label(value) return value:gsub("\\", "\\\\"):gsub('"', '\\"') end

function render(config)
  local lines = { "flowchart LR" }
  local component_nodes = {}
  local suite_nodes = {}
  local workflow_nodes = {}

  for index, name in ipairs(frontend.keys(config.components)) do
    component_nodes[name] = "c" .. index
    lines[#lines + 1] = string.format('  %s["component: %s"]', component_nodes[name], label(name))
  end
  for index, name in ipairs(frontend.keys(config.test_suites)) do
    suite_nodes[name] = "s" .. index
    lines[#lines + 1] = string.format('  %s["test suite: %s"]', suite_nodes[name], label(name))
  end
  for index, name in ipairs(frontend.keys(config.workflows)) do
    workflow_nodes[name] = "w" .. index
    lines[#lines + 1] = string.format('  %s["workflow: %s"]', workflow_nodes[name], label(name))
  end

  for _, name in ipairs(frontend.keys(config.components)) do
    for _, dependency in ipairs(config.components[name].depends) do
      lines[#lines + 1] =
        string.format("  %s --> %s", component_nodes[dependency], component_nodes[name])
    end
  end
  for _, name in ipairs(frontend.keys(config.test_suites)) do
    for _, dependency in ipairs(config.test_suites[name].depends) do
      lines[#lines + 1] =
        string.format("  %s --> %s", component_nodes[dependency], suite_nodes[name])
    end
  end
  for _, name in ipairs(frontend.keys(config.workflows)) do
    for _, suite_name in ipairs(config.workflows[name].test_suites) do
      lines[#lines + 1] =
        string.format("  %s --> %s", suite_nodes[suite_name], workflow_nodes[name])
    end
  end
  return table.concat(lines, "\n") .. "\n"
end
