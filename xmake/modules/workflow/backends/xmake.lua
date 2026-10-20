-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

local config = import("core.project.config")
local frontend = import("workflow.frontend")
local resolve = import("workflow.resolve")

local active
-- Display only: remember values already resolved for this process, without
-- changing saved options or duplicating the owning targets' fallback defaults.
local effective_options = {}
local summary_scheduled

function selected()
  if active then return active end
  local name = config.get("workflow") or ""
  local suites = config.get("test-suites") or ""
  assert(name == "" or suites == "", "--workflow and --test-suites are mutually exclusive")
  if name == "" and suites == "" then return { build_options = {}, test_suites = {} } end
  local definition = frontend.load(os.projectdir())
  local workflow = name ~= "" and assert(definition.workflows[name], "Unknown workflow: " .. name)
  local names = workflow and workflow.test_suites or suites:split(",", { plain = true })
  assert(#names > 0, "--test-suites requires at least one test suite")
  active = resolve.run(definition, config.mode(), names, "xmake", {
    workflow = workflow and name,
    install = workflow and workflow.install,
  })
  if active.install then
    assert(
      #(active.build_options["install-targets"] or {}) > 0,
      "Installing workflow requires Xmake install-targets"
    )
  end
  return active
end

function enabled(name, default)
  local value = config.get(name)
  local enabled
  if value ~= nil and value ~= "auto" then
    enabled = value == "y" or value == true
  else
    enabled = selected().build_options[name]
    if enabled == nil then enabled = default end
  end
  effective_options[name] = enabled
  return enabled
end

function schedule_config_summary()
  if summary_scheduled then return end
  summary_scheduled = true
  -- after_config runs per target, including during build/test/run. Wait until
  -- the explicit config task succeeds before printing one project summary.
  os.atexit(function(ok)
    local option = import("core.base.option")
    if not ok or option.taskname() ~= "config" or option.get("quiet") then return end
    local selected = selected()
    local selection = selected.workflow
    if not selection then
      selection = #selected.test_suites > 0
          and "suites " .. table.concat(selected.test_suites, ", ")
        or "project defaults"
    end
    print("Configuration: %s", selection)
    print("  Mode: %s", config.mode())
    print("  Build directory: %s", path.absolute(config.builddir()))
    for _, name in ipairs(frontend.keys(effective_options)) do
      local value = config.get(name)
      local policy = (value == nil or value == "auto") and "auto" or "explicit"
      print("  %s: %s (%s)", name, effective_options[name] and "y" or "n", policy)
    end
    if selected.install then
      local project = import("core.project.project")
      for _, name in ipairs(selected.build_options["install-targets"]) do
        print("  Install on build: %s -> %s", name, project.target(name):installdir())
      end
    end
  end)
end

-- Validate before filtering any target. A stale selector must fail configuration,
-- rather than turning an advertised suite into a successful zero-test run.
function validate_tests()
  if not active or active.validated then return end
  local project = import("core.project.project")
  local definition = frontend.load(os.projectdir())
  for _, suite in ipairs(active.test_suites) do
    local matched = false
    for _, selector in ipairs(definition.test_suites[suite].adapters.xmake.selectors) do
      local pattern = "^" .. path.pattern(selector) .. "$"
      for _, target in pairs(project.targets()) do
        for _, name in ipairs(table.wrap(target:get("tests"))) do
          if (target:name() .. "/" .. name):match(pattern) then matched = true end
        end
      end
    end
    assert(matched, "Selected test suite '" .. suite .. "' matches no Xmake test cases")
  end
  active.validated = true
end
