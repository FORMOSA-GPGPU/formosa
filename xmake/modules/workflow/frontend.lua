-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

local function sorted_keys(value)
  local keys = {}
  for key in pairs(value or {}) do
    keys[#keys + 1] = key
  end
  table.sort(keys, function(a, b) return tostring(a) < tostring(b) end)
  return keys
end

function is_array(value)
  if type(value) ~= "table" then return false end
  local count = 0
  for key in pairs(value) do
    if type(key) ~= "number" or key < 1 or key % 1 ~= 0 then return false end
    count = count + 1
  end
  return count == #value
end

local function is_map(value)
  if type(value) ~= "table" then return false end
  for key in pairs(value) do
    if type(key) ~= "string" or key == "" then return false end
  end
  return true
end

local function is_empty(value)
  for _ in pairs(value) do
    return false
  end
  return true
end

local function add_error(errors, location, message)
  errors[#errors + 1] = location .. ": " .. message
end

local function check_fields(value, allowed, location, errors)
  if type(value) ~= "table" then return end
  for _, key in ipairs(sorted_keys(value)) do
    if not allowed[key] then
      add_error(errors, location .. "." .. tostring(key), "unknown field")
    end
  end
end

local function validate_id(value, location, errors)
  if type(value) ~= "string" or value == "" then
    add_error(errors, location, "must be a non-empty string")
  elseif not value:match("^[%w_.-]+$") then
    add_error(errors, location, "must contain only letters, numbers, '_', '.', or '-'")
  end
end

local function validate_string_array(value, location, errors, allow_empty)
  if not is_array(value) then
    add_error(errors, location, "must be an array of strings")
    return false
  end
  if not allow_empty and #value == 0 then
    add_error(errors, location, "must not be empty")
    return false
  end
  local seen = {}
  local valid = true
  for index, item in ipairs(value) do
    if type(item) ~= "string" or item == "" then
      add_error(errors, location .. "[" .. index .. "]", "must be a non-empty string")
      valid = false
    elseif seen[item] then
      add_error(errors, location .. "[" .. index .. "]", "duplicates " .. item)
      valid = false
    else
      seen[item] = true
    end
  end
  return valid
end

local function validate_glob(value, location, errors)
  if type(value) ~= "string" or value == "" then return end
  if value:sub(1, 1) == "/" then add_error(errors, location, "must be repository-relative") end
  if value:find("\\", 1, true) then add_error(errors, location, "must use forward slashes") end
  if value:find("//", 1, true) then
    add_error(errors, location, "must not contain empty path segments")
  end
  if value:match("^%.%./") or value:match("/%.%./") or value:match("/%.%.$") or value == ".." then
    add_error(errors, location, "must not traverse parent directories")
  end
  if value:find("[!|%[%]{}?]") then add_error(errors, location, "uses unsupported glob syntax") end
  local without_recursive_suffix = value:gsub("/%*%*$", "")
  if without_recursive_suffix:find("%*%*") then
    add_error(errors, location, "'**' is supported only as a recursive directory suffix")
  end
end

local function validate_config_value(value, location, errors)
  if type(value) == "string" then
    if value == "" then add_error(errors, location, "must not be empty") end
  elseif type(value) == "boolean" then
    return
  elseif type(value) == "table" then
    validate_string_array(value, location, errors, false)
  else
    add_error(errors, location, "must be a string, boolean, or array of strings")
  end
end

local function validate_options(value, location, errors)
  if not is_map(value) then
    add_error(errors, location, "must be a string-keyed table")
    return false
  end
  for _, option_name in ipairs(sorted_keys(value)) do
    validate_id(option_name, location .. "." .. tostring(option_name), errors)
    validate_config_value(value[option_name], location .. "." .. tostring(option_name), errors)
  end
  return true
end

local function validate_adapters(value, location, errors, suites)
  if not is_map(value) or is_empty(value) then
    add_error(errors, location, "must be a non-empty adapter table")
    return false
  end
  local valid = true
  for _, adapter_name in ipairs(sorted_keys(value)) do
    validate_id(adapter_name, location .. "." .. tostring(adapter_name), errors)
    local adapter = value[adapter_name]
    local adapter_location = location .. "." .. adapter_name
    if not is_map(adapter) then
      add_error(errors, adapter_location, "must be a table")
      valid = false
    elseif suites then
      check_fields(adapter, { build_options = true, selectors = true }, adapter_location, errors)
      if adapter.build_options ~= nil then
        valid = validate_options(
          adapter.build_options,
          adapter_location .. ".build_options",
          errors
        ) and valid
      end
      valid = validate_string_array(
        adapter.selectors,
        adapter_location .. ".selectors",
        errors,
        false
      ) and valid
    else
      valid = validate_options(adapter, adapter_location, errors) and valid
    end
  end
  return valid
end

local function validate_map(value, location, errors)
  if not is_map(value) or is_empty(value) then
    add_error(errors, location, "must be a non-empty string-keyed table")
    return false
  end
  return true
end

local function validate_components(config, errors)
  if not validate_map(config.components, "components", errors) then return end
  for _, name in ipairs(sorted_keys(config.components)) do
    validate_id(name, "components." .. tostring(name), errors)
    local component = config.components[name]
    local location = "components." .. name
    if not is_map(component) then
      add_error(errors, location, "must be a table")
    else
      check_fields(component, { paths = true, depends = true }, location, errors)
      if validate_string_array(component.paths, location .. ".paths", errors, false) then
        for index, pattern in ipairs(component.paths) do
          validate_glob(pattern, location .. ".paths[" .. index .. "]", errors)
        end
      end
      if component.depends ~= nil then
        validate_string_array(component.depends, location .. ".depends", errors, false)
      end
    end
  end
end

local function validate_profiles(config, errors)
  if not validate_map(config.build_profiles, "build_profiles", errors) then return end
  for _, name in ipairs(sorted_keys(config.build_profiles)) do
    validate_id(name, "build_profiles." .. tostring(name), errors)
    validate_adapters(config.build_profiles[name], "build_profiles." .. name, errors, false)
  end
end

local function validate_suites(config, errors)
  if not validate_map(config.test_suites, "test_suites", errors) then return end
  for _, name in ipairs(sorted_keys(config.test_suites)) do
    validate_id(name, "test_suites." .. tostring(name), errors)
    local suite = config.test_suites[name]
    local location = "test_suites." .. name
    if not is_map(suite) then
      add_error(errors, location, "must be a table")
    else
      check_fields(suite, { depends = true, adapters = true }, location, errors)
      validate_string_array(suite.depends, location .. ".depends", errors, false)
      validate_adapters(suite.adapters, location .. ".adapters", errors, true)
    end
  end
end

local function validate_workflows(config, errors)
  if not validate_map(config.workflows, "workflows", errors) then return end
  for _, name in ipairs(sorted_keys(config.workflows)) do
    validate_id(name, "workflows." .. tostring(name), errors)
    local workflow = config.workflows[name]
    local location = "workflows." .. name
    if not is_map(workflow) then
      add_error(errors, location, "must be a table")
    else
      check_fields(
        workflow,
        { description = true, test_suites = true, install = true },
        location,
        errors
      )
      if type(workflow.description) ~= "string" or workflow.description == "" then
        add_error(errors, location .. ".description", "must be a non-empty string")
      end
      validate_string_array(workflow.test_suites, location .. ".test_suites", errors, false)
      if workflow.install ~= nil and type(workflow.install) ~= "boolean" then
        add_error(errors, location .. ".install", "must be a boolean")
      end
    end
  end
end

local function validate_references(config, errors)
  if is_map(config.components) then
    for _, name in ipairs(sorted_keys(config.components)) do
      local component = config.components[name]
      if is_map(component) and is_array(component.depends or {}) then
        for index, dependency in ipairs(component.depends or {}) do
          if type(dependency) == "string" and not config.components[dependency] then
            add_error(
              errors,
              "components." .. name .. ".depends[" .. index .. "]",
              "references unknown component " .. dependency
            )
          end
        end
      end
    end
  end
  if is_map(config.test_suites) and is_map(config.components) then
    for _, name in ipairs(sorted_keys(config.test_suites)) do
      local suite = config.test_suites[name]
      if is_map(suite) and is_array(suite.depends or {}) then
        for index, dependency in ipairs(suite.depends or {}) do
          if type(dependency) == "string" and not config.components[dependency] then
            add_error(
              errors,
              "test_suites." .. name .. ".depends[" .. index .. "]",
              "references unknown component " .. dependency
            )
          end
        end
      end
    end
  end
  if is_map(config.workflows) and is_map(config.test_suites) then
    for _, name in ipairs(sorted_keys(config.workflows)) do
      local workflow = config.workflows[name]
      if is_map(workflow) and is_array(workflow.test_suites or {}) then
        for index, suite in ipairs(workflow.test_suites or {}) do
          if type(suite) == "string" and not config.test_suites[suite] then
            add_error(
              errors,
              "workflows." .. name .. ".test_suites[" .. index .. "]",
              "references unknown test suite " .. suite
            )
          end
        end
      end
    end
  end
end

local function validate_component_cycles(config, errors)
  if not is_map(config.components) then return end
  local state = {}
  local stack = {}
  local positions = {}

  local function visit(name)
    state[name] = "visiting"
    stack[#stack + 1] = name
    positions[name] = #stack
    local component = config.components[name]
    if is_map(component) and is_array(component.depends or {}) then
      for _, dependency in ipairs(component.depends or {}) do
        if config.components[dependency] then
          if state[dependency] == "visiting" then
            local cycle = {}
            for index = positions[dependency], #stack do
              cycle[#cycle + 1] = stack[index]
            end
            cycle[#cycle + 1] = dependency
            add_error(errors, "components", "dependency cycle: " .. table.concat(cycle, " -> "))
          elseif state[dependency] == nil then
            visit(dependency)
          end
        end
      end
    end
    positions[name] = nil
    stack[#stack] = nil
    state[name] = "done"
  end

  for _, name in ipairs(sorted_keys(config.components)) do
    if state[name] == nil then visit(name) end
  end
end

function common_adapters(config, profile_name, suite_names)
  local profile = config.build_profiles[profile_name]
  if not profile then raise("Unknown build profile: %s", profile_name) end
  local common = {}
  for adapter_name in pairs(profile) do
    common[adapter_name] = true
  end
  for _, suite_name in ipairs(suite_names) do
    local suite = config.test_suites[suite_name]
    if not suite then raise("Unknown test suite: %s", suite_name) end
    for adapter_name in pairs(common) do
      if not suite.adapters[adapter_name] then common[adapter_name] = nil end
    end
  end
  return sorted_keys(common)
end

local function validate_adapter_intersections(config, errors)
  if
    not is_map(config.build_profiles)
    or not is_map(config.workflows)
    or not is_map(config.test_suites)
  then
    return
  end
  for _, workflow_name in ipairs(sorted_keys(config.workflows)) do
    local workflow = config.workflows[workflow_name]
    if is_map(workflow) and is_array(workflow.test_suites) then
      for _, profile_name in ipairs(sorted_keys(config.build_profiles)) do
        local profile = config.build_profiles[profile_name]
        if is_map(profile) then
          local complete = true
          for _, suite_name in ipairs(workflow.test_suites) do
            local suite = config.test_suites[suite_name]
            if not is_map(suite) or not is_map(suite.adapters) then
              complete = false
              break
            end
          end
          if complete and #common_adapters(config, profile_name, workflow.test_suites) == 0 then
            add_error(
              errors,
              "workflows." .. workflow_name,
              "has no adapter in common with build profile " .. profile_name
            )
          end
        end
      end
    end
  end
end

local function normalize(config)
  for _, component in pairs(config.components) do
    component.depends = component.depends or {}
  end
  for _, suite in pairs(config.test_suites) do
    for _, adapter in pairs(suite.adapters) do
      adapter.build_options = adapter.build_options or {}
    end
  end
  for _, workflow in pairs(config.workflows) do
    workflow.install = workflow.install or false
  end
  return config
end

function load(rootdir)
  local config_loader = import("workflows", { rootdir = rootdir, anonymous = true })
  local config = config_loader()

  local errors = {}
  if not is_map(config) then
    errors[#errors + 1] = "configuration: must return a table"
  else
    check_fields(
      config,
      { components = true, build_profiles = true, test_suites = true, workflows = true },
      "configuration",
      errors
    )
    validate_components(config, errors)
    validate_profiles(config, errors)
    validate_suites(config, errors)
    validate_workflows(config, errors)
    validate_references(config, errors)
    validate_component_cycles(config, errors)
    validate_adapter_intersections(config, errors)
  end

  if #errors > 0 then
    raise("Invalid workflow configuration:\n  - %s", table.concat(errors, "\n  - "))
  end
  return normalize(config)
end

function keys(value) return sorted_keys(value) end
