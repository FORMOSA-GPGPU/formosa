-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

local option = import("core.base.option")
local frontend = import("workflow.frontend")
local graph = import("workflow.graph")
local impact = import("workflow.impact")
local resolve = import("workflow.resolve")
local serializer = import("workflow.serializer")
local cmake = import("workflow.backends.cmake")

local OUTPUT = "CMakePresets.json"
local USER_OUTPUT = "CMakeUserPresets.json"
local VALUE_OPTIONS = { adapter = true, base = true, head = true, profile = true }
local REGRESSION_OPTIONS =
  { "profile", "adapter", "base", "head", "working_tree", "all", "generate" }

local function read_file(filename)
  local file = io.open(filename, "rb")
  if not file then return nil end
  local contents = file:read("*a")
  file:close()
  return contents
end

local function write_atomic(filename, contents)
  local temporary = filename .. ".tmp"
  local file, message = io.open(temporary, "wb")
  if not file then raise("Cannot write %s: %s", temporary, message) end
  file:write(contents)
  file:close()
  os.mv(temporary, filename)
end

local function reject_options(command, options, names)
  for _, name in ipairs(names) do
    if options[name] ~= nil then raise("--%s is not valid for '%s'", name, command) end
  end
end

local function parse_arguments()
  local arguments = option.get("arguments") or {}
  local command = arguments[1]
  local options = {}
  local index = 2
  while index <= #arguments do
    local argument = arguments[index]
    if argument == "--all" or argument == "--generate" or argument == "--working-tree" then
      local name = argument:sub(3):gsub("-", "_")
      if options[name] then raise("%s may be specified only once", argument) end
      options[name] = true
    else
      local name, value = argument:match("^%-%-([%w-]+)=(.+)$")
      if not name then
        name = argument:match("^%-%-([%w-]+)$")
        if name and VALUE_OPTIONS[name] then
          index = index + 1
          value = arguments[index]
        end
      end
      if not name or not VALUE_OPTIONS[name] or not value or value == "" then
        raise("Invalid workflow option: %s", argument)
      end
      if options[name] ~= nil then raise("--%s may be specified only once", name) end
      options[name] = value
    end
    index = index + 1
  end
  return command, options
end

local function print_list(title, values)
  io.write(title .. ":\n")
  if #values == 0 then
    io.write("  (none)\n")
  else
    for _, value in ipairs(values) do
      io.write("  - " .. value .. "\n")
    end
  end
end

local function option_text(value)
  if type(value) == "table" then return "[" .. table.concat(value, ", ") .. "]" end
  return tostring(value)
end

local function regression_result(config, rootdir, options)
  if options.all then
    if options.base or options.head or options.working_tree then
      raise("--all cannot be combined with --base, --head, or --working-tree")
    end
    return {
      affected_components = {},
      affected_workflows = {},
      base = nil,
      changed_files = {},
      direct_components = {},
      head = "HEAD",
      test_suites = frontend.keys(config.test_suites),
      unmatched_files = {},
    }
  end
  return impact.analyze(config, rootdir, {
    base = options.base,
    head = options.head,
    working_tree = options.working_tree or false,
  })
end

local function run_regression(config, rootdir, options)
  local profile = options.profile
  if not profile then raise("regression requires --profile") end
  if not config.build_profiles[profile] then raise("Unknown build profile: %s", profile) end
  if options.generate and options.adapter and options.adapter ~= "cmake" then
    raise("--generate supports only the CMake adapter")
  end
  local result = regression_result(config, rootdir, options)
  local run
  if #result.test_suites > 0 then
    run = resolve.run(
      config,
      profile,
      result.test_suites,
      options.generate and "cmake" or options.adapter
    )
  end

  io.write("Diff: " .. (result.base or "(not used)") .. ".." .. result.head .. "\n")
  print_list("Changed files", result.changed_files)
  print_list("Directly matched components", result.direct_components)
  print_list("Transitively affected components", result.affected_components)
  print_list("Selected test suites", result.test_suites)
  print_list("Informationally affected workflows", result.affected_workflows)
  if run then
    io.write("Resolved profile/adapter: " .. run.profile .. " / " .. run.adapter .. "\n")
    io.write("Merged build options:\n")
    for _, name in ipairs(frontend.keys(run.build_options)) do
      io.write("  " .. name .. " = " .. option_text(run.build_options[name]) .. "\n")
    end
    print_list("Merged selectors", run.selectors)
  end
  print_list("Unmatched files", result.unmatched_files)
  if options.generate then
    local contents = serializer.json(cmake.run_presets(run, "regression." .. profile, "build"))
      .. "\n"
    write_atomic(path.join(rootdir, USER_OUTPUT), contents)
    print("Generated %s", USER_OUTPUT)
  end
end

local function generated_contents(config)
  return serializer.json(cmake.generate(resolve.workflow_runs(config, "cmake"))) .. "\n"
end

function run()
  local command, options = parse_arguments()
  if not command then raise("Usage: xmake workflow <graph|regression|generate|check> [options]") end

  local rootdir = os.projectdir()
  local config = frontend.load(rootdir)
  if command == "graph" then
    reject_options(command, options, REGRESSION_OPTIONS)
    io.write(graph.render(config))
  elseif command == "regression" then
    run_regression(config, rootdir, options)
  elseif command == "generate" then
    reject_options(command, options, REGRESSION_OPTIONS)
    write_atomic(path.join(rootdir, OUTPUT), generated_contents(config))
    print("Generated %s", OUTPUT)
  elseif command == "check" then
    reject_options(command, options, REGRESSION_OPTIONS)
    local expected = generated_contents(config)
    if read_file(path.join(rootdir, OUTPUT)) ~= expected then
      io.stderr:write(OUTPUT .. " is stale.\nRun: xmake workflow generate\n")
      os.exit(1)
    end
    print("%s is up to date.", OUTPUT)
  else
    raise("Unknown workflow command '%s'; expected graph, regression, generate, or check", command)
  end
end
