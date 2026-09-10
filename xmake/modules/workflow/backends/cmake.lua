-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

local function cmake_options(options)
  local result = {}
  for name, value in pairs(options) do
    result[name] = type(value) == "table" and table.concat(value, ";") or value
  end
  return result
end

local function validate_install(run)
  if not run.install then return end
  local prefix = run.build_options.CMAKE_INSTALL_PREFIX
  if type(prefix) ~= "string" or prefix == "" then
    raise(
      "Workflow '%s' installs but its resolved CMake options do not define CMAKE_INSTALL_PREFIX",
      run.workflow
    )
  end
end

function configure_preset(run, name, binary_dir)
  validate_install(run)
  local preset = {
    cacheVariables = cmake_options(run.build_options),
    inherits = "workflow.base",
    name = name or run.name,
  }
  if binary_dir then preset.binaryDir = binary_dir end
  return preset
end

function build_preset(run, name)
  name = name or run.name
  local preset = {
    configurePreset = name,
    name = name,
  }
  if #run.build_targets > 0 then preset.targets = run.build_targets end
  return preset
end

function test_preset(run, name)
  name = name or run.name
  return {
    configurePreset = name,
    execution = { noTestsAction = "error" },
    filter = { include = { name = table.concat(run.selectors, "|") } },
    name = name,
    output = { outputOnFailure = true },
  }
end

function run_presets(run, name, binary_dir)
  if not run then return { version = 8 } end
  return {
    buildPresets = { build_preset(run, name) },
    configurePresets = { configure_preset(run, name, binary_dir) },
    testPresets = { test_preset(run, name) },
    version = 8,
    workflowPresets = {
      {
        name = name,
        steps = {
          { name = name, type = "configure" },
          { name = name, type = "build" },
        },
      },
    },
  }
end

function generate(runs)
  local configure_presets = {
    {
      binaryDir = "build",
      generator = "Ninja",
      hidden = true,
      name = "workflow.base",
    },
  }
  local build_presets = {}
  local test_presets = {}
  local workflow_presets = {}

  for _, run in ipairs(runs) do
    configure_presets[#configure_presets + 1] = configure_preset(run)
    build_presets[#build_presets + 1] = build_preset(run)
    test_presets[#test_presets + 1] = test_preset(run)
    workflow_presets[#workflow_presets + 1] = {
      displayName = run.description,
      name = run.name,
      steps = {
        { name = run.name, type = "configure" },
        { name = run.name, type = "build" },
      },
    }
  end

  return {
    buildPresets = build_presets,
    configurePresets = configure_presets,
    testPresets = test_presets,
    version = 8,
    workflowPresets = workflow_presets,
  }
end
