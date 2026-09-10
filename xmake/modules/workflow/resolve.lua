-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

local frontend = import("workflow.frontend")
local common_adapters = frontend.common_adapters

local function value_text(value)
  if type(value) == "table" then return "[" .. table.concat(value, ", ") .. "]" end
  return tostring(value)
end

local function select_adapter(config, profile_name, suite_names, requested)
  local adapters = common_adapters(config, profile_name, suite_names)
  if requested then
    for _, adapter_name in ipairs(adapters) do
      if adapter_name == requested then return requested end
    end
    raise(
      "Adapter '%s' is not supported by build profile '%s' and every selected test suite",
      requested,
      profile_name
    )
  end
  if #adapters == 0 then
    raise("Build profile '%s' and the selected test suites have no adapter in common", profile_name)
  end
  if #adapters > 1 then
    raise(
      "Multiple adapters are available (%s); select one explicitly",
      table.concat(adapters, ", ")
    )
  end
  return adapters[1]
end

local function copy_array(value)
  local result = {}
  for _, item in ipairs(value) do
    result[#result + 1] = item
  end
  return result
end

local function merge_option(options, sources, name, value, source)
  local existing = options[name]
  if existing == nil then
    options[name] = type(value) == "table" and copy_array(value) or value
    sources[name] = { { source = source, value = value_text(value) } }
    return
  end

  sources[name][#sources[name] + 1] = { source = source, value = value_text(value) }
  if type(existing) ~= type(value) then
    local details = {}
    for _, item in ipairs(sources[name]) do
      details[#details + 1] = item.source .. "=" .. item.value
    end
    raise("Conflicting build option '%s': %s", name, table.concat(details, ", "))
  elseif type(value) == "boolean" then
    options[name] = existing or value
  elseif type(value) == "string" then
    if existing ~= value then
      local details = {}
      for _, item in ipairs(sources[name]) do
        details[#details + 1] = item.source .. "=" .. item.value
      end
      raise("Conflicting build option '%s': %s", name, table.concat(details, ", "))
    end
  else
    local seen = {}
    for _, item in ipairs(existing) do
      seen[item] = true
    end
    for _, item in ipairs(value) do
      if not seen[item] then
        existing[#existing + 1] = item
        seen[item] = true
      end
    end
  end
end

function adapters(config, profile_name, suite_names)
  return common_adapters(config, profile_name, suite_names)
end

function run(config, profile_name, suite_names, requested_adapter, options)
  options = options or {}
  local adapter_name = select_adapter(config, profile_name, suite_names, requested_adapter)
  local build_options = {}
  local option_sources = {}
  for _, option_name in ipairs(frontend.keys(config.build_profiles[profile_name][adapter_name])) do
    merge_option(
      build_options,
      option_sources,
      option_name,
      config.build_profiles[profile_name][adapter_name][option_name],
      "profile " .. profile_name
    )
  end

  local selectors = {}
  local seen_selectors = {}
  for _, suite_name in ipairs(suite_names) do
    local adapter = config.test_suites[suite_name].adapters[adapter_name]
    for _, option_name in ipairs(frontend.keys(adapter.build_options)) do
      merge_option(
        build_options,
        option_sources,
        option_name,
        adapter.build_options[option_name],
        "suite " .. suite_name
      )
    end
    for _, selector in ipairs(adapter.selectors) do
      if not seen_selectors[selector] then
        selectors[#selectors + 1] = selector
        seen_selectors[selector] = true
      end
    end
  end

  return {
    adapter = adapter_name,
    build_options = build_options,
    build_targets = options.install and { "all", "install" } or {},
    install = options.install or false,
    option_sources = option_sources,
    profile = profile_name,
    selectors = selectors,
    test_suites = copy_array(suite_names),
    workflow = options.workflow,
  }
end

function workflow_runs(config, adapter_name)
  local runs = {}
  for _, workflow_name in ipairs(frontend.keys(config.workflows)) do
    local workflow = config.workflows[workflow_name]
    for _, profile_name in ipairs(frontend.keys(config.build_profiles)) do
      local common = common_adapters(config, profile_name, workflow.test_suites)
      local supported = not adapter_name
      for _, available in ipairs(common) do
        if available == adapter_name then supported = true end
      end
      if supported then
        local resolved = run(config, profile_name, workflow.test_suites, adapter_name, {
          install = workflow.install,
          workflow = workflow_name,
        })
        resolved.name = workflow_name .. "." .. profile_name
        resolved.description = workflow.description
        runs[#runs + 1] = resolved
      end
    end
  end
  return runs
end
