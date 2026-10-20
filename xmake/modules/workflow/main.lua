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
local execute = import("workflow.execute")
local commands = import("workflow.commands")

local OUTPUT = "CMakePresets.json"
local USER_OUTPUT = "CMakeUserPresets.json"
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

local function usage_error(command, message, ...)
  raise("%s\nRun: xmake workflow %s --help", string.format(message, ...), command)
end

local function print_list(title, values)
  if #values == 0 then return end
  print("%s:", title)
  for _, value in ipairs(values) do
    print("  - %s", value)
  end
end

local function option_text(value)
  if type(value) == "table" then return "[" .. table.concat(value, ", ") .. "]" end
  return tostring(value)
end

local function list_names(config)
  local names = frontend.keys(config.workflows)
  local width = 0
  for _, name in ipairs(names) do
    width = math.max(width, #name)
  end
  print("Workflows:")
  for _, name in ipairs(names) do
    print("  %-" .. width .. "s - %s", name, config.workflows[name].description)
  end
  print_list("Test suites", frontend.keys(config.test_suites))
  print_list("Build profiles", frontend.keys(config.build_profiles))
end

local function regression_result(config, rootdir, options)
  if options.all then
    if options.base or options.head or options.working_tree then
      usage_error("regression", "--all cannot be combined with --base, --head, or --working-tree")
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
  if not profile then usage_error("regression", "regression requires --profile") end
  if options.generate and options.adapter and options.adapter ~= "cmake" then
    usage_error("regression", "--generate supports only the CMake adapter")
  end
  if options.generate and options.execute then
    usage_error("regression", "--generate and --execute are mutually exclusive")
  end
  local adapter = options.generate and "cmake" or options.adapter or "xmake"
  local result = regression_result(config, rootdir, options)
  local run
  if #result.test_suites > 0 then
    run = resolve.run(config, profile, result.test_suites, adapter)
  end

  print("Regression: %s / %s", profile, adapter)
  if options.all then
    print("Selection: all suites requested by --all")
  else
    print(
      "Selection: changes in %s..%s%s (%d changed files)",
      result.base,
      result.head,
      options.working_tree and " + working tree" or "",
      #result.changed_files
    )
  end
  if #result.test_suites > 0 then
    print_list("Selected test suites", result.test_suites)
  else
    print("Selected test suites: none")
  end
  if option.get("verbose") or option.get("diagnosis") then
    print_list("Changed files", result.changed_files)
    print_list("Directly matched components", result.direct_components)
    print_list("Transitively affected components", result.affected_components)
    print_list("Informationally affected workflows", result.affected_workflows)
    if run then
      print("Merged build options:")
      for _, name in ipairs(frontend.keys(run.build_options)) do
        print("  %s = %s", name, option_text(run.build_options[name]))
      end
      print_list("Merged selectors", run.selectors)
    end
    print_list("Unmatched files", result.unmatched_files)
  elseif #result.unmatched_files > 0 then
    print(
      "Unmatched files: %d; use xmake workflow -v regression ... for details.",
      #result.unmatched_files
    )
  end
  if not options.execute and not options.generate then
    print("Plan only; add --execute to build and test.")
  end
  if options.generate then
    local contents = serializer.json(cmake.run_presets(run, "regression." .. profile, "build"))
      .. "\n"
    write_atomic(path.join(rootdir, USER_OUTPUT), contents)
    print("Generated %s", USER_OUTPUT)
  elseif options.execute then
    if run then
      execute.run(run, options)
    else
      print("No affected test suites; nothing to run.")
    end
  end
end

local function generated_contents(config)
  return serializer.json(cmake.generate(resolve.workflow_runs(config, "cmake"))) .. "\n"
end

function run()
  local arguments = option.get("arguments") or {}
  local command = arguments[1]
  if not command then raise("A workflow command is required\nRun: xmake workflow --help") end
  if not commands.has_command(command) then
    raise("Unknown workflow command '%s'\nRun: xmake workflow --help", command)
  end
  -- The tail belongs to xmake f, including any --help intended for configure.
  for index = 2, #arguments do
    if arguments[index] == "--" then break end
    if arguments[index] == "--help" or arguments[index] == "-h" then
      commands.show(command)
      return
    end
  end
  local options
  try({
    function() options = commands.parse(command, arguments) end,
    catch({ function(err) usage_error(command, "%s", err) end }),
  })

  local rootdir = os.projectdir()
  local config = frontend.load(rootdir)
  if options.profile and not config.build_profiles[options.profile] then
    usage_error(
      command,
      "Unknown build profile: %s\nDiscover names: xmake workflow list",
      options.profile
    )
  end
  if command == "run" or command == "build" then
    local name = options.name
    local workflow = config.workflows[name]
    if not workflow then
      usage_error(command, "Unknown workflow: %s\nDiscover names: xmake workflow list", name)
    end
    local resolved = resolve.run(
      config,
      options.profile or "debug",
      workflow.test_suites,
      options.adapter or "xmake",
      { workflow = name, install = workflow.install }
    )
    options.build_only = command == "build"
    execute.run(resolved, options)
  elseif command == "list" then
    list_names(config)
  elseif command == "graph" then
    io.write(graph.render(config))
  elseif command == "regression" then
    run_regression(config, rootdir, options)
  elseif command == "generate" then
    write_atomic(path.join(rootdir, OUTPUT), generated_contents(config))
    print("Generated %s", OUTPUT)
  elseif command == "check" then
    local expected = generated_contents(config)
    if read_file(path.join(rootdir, OUTPUT)) ~= expected then
      io.stderr:write(OUTPUT .. " is stale.\nRun: xmake workflow generate\n")
      os.exit(1)
    end
    print("%s is up to date.", OUTPUT)
  end
end
