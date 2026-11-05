-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

local frontend = import("workflow.frontend")

local function value_text(value)
  if type(value) == "table" then return "[" .. table.concat(value, ", ") .. "]" end
  return tostring(value)
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

function run(config, profile_name, suite_names, options)
  options = options or {}
  local profile = config.build_profiles[profile_name]
  assert(profile, "Unknown build profile: " .. profile_name)
  local build_options = {}
  local option_sources = {}
  for _, option_name in ipairs(frontend.keys(profile)) do
    merge_option(
      build_options,
      option_sources,
      option_name,
      profile[option_name],
      "profile " .. profile_name
    )
  end

  local selectors = {}
  local seen_selectors = {}
  for _, suite_name in ipairs(suite_names) do
    local suite = config.test_suites[suite_name]
    assert(suite, "Unknown test suite: " .. suite_name)
    for _, option_name in ipairs(frontend.keys(suite.build_options)) do
      merge_option(
        build_options,
        option_sources,
        option_name,
        suite.build_options[option_name],
        "suite " .. suite_name
      )
    end
    for _, selector in ipairs(suite.selectors) do
      if not seen_selectors[selector] then
        selectors[#selectors + 1] = selector
        seen_selectors[selector] = true
      end
    end
  end

  return {
    build_options = build_options,
    install = options.install or false,
    profile = profile_name,
    selectors = selectors,
    test_suites = copy_array(suite_names),
    workflow = options.workflow,
  }
end
