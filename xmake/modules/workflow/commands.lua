-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

local option = import("core.base.option")

local execution_options = {
  { nil, "builddir", "kv", "build", "Build output directory." },
  { nil, "jobs", "kv", nil, "Parallel build/test jobs (default: Xmake's native default)." },
  { nil, "installdir", "kv", nil, "Installation prefix (default: outputs/out)." },
}
local build_options = table.join({
  { nil, "profile", "kv", "debug", "Build profile; see xmake workflow list." },
  { nil, "adapter", "kv", "xmake", "Build adapter; execution supports Xmake only." },
}, execution_options)

local commands = {
  run = {
    usage = "run NAME [options] [-- configure-options]",
    description = "Configure, build, install when required, and test a workflow.",
    options = build_options,
    configure = true,
    notes = "Execution uses Xmake. Installing workflows install during the build step.",
    examples = [[
  xmake workflow run lv
  xmake workflow run opencl --profile=release --builddir=build-opencl -- --fw-enable-log=y]],
  },
  build = {
    usage = "build NAME [options] [-- configure-options]",
    description = "Configure, build, and install when required; skip tests.",
    options = build_options,
    configure = true,
    notes = "Execution uses Xmake. Installing workflows install during the build step.",
    examples = [[
  xmake workflow build lv
  xmake workflow build opencl --profile=release --installdir=outputs/sdk]],
  },
  regression = {
    usage = "regression --profile=NAME [options] [-- configure-options]",
    description = "Plan affected test suites. Nothing is built or tested without --execute.",
    options = table.join({
      { nil, "profile", "kv", nil, "Build profile (required; see xmake workflow list)." },
      {
        nil,
        "adapter",
        "kv",
        nil,
        "Build adapter (default: xmake; --generate defaults to cmake).",
      },
      {
        nil,
        "base",
        "kv",
        nil,
        "Diff base (default: inferred from CI or the remote default branch).",
      },
      { nil, "head", "kv", nil, "Diff head (default: HEAD)." },
      {
        nil,
        "working-tree",
        "k",
        nil,
        "Include staged, unstaged and untracked changes; head must be HEAD.",
      },
      { nil, "all", "k", nil, "Select every suite; cannot combine with diff selection options." },
      { nil, "execute", "k", nil, "Configure, build, and test the selected suites using Xmake." },
      { nil, "generate", "k", nil, "Write CMakeUserPresets.json for the retained CMake fallback." },
    }, execution_options),
    configure = true,
    notes = "--generate and --execute are mutually exclusive. CMake execution remains explicit.",
    examples = [[
  xmake workflow regression --profile=debug --base=main --working-tree
  xmake workflow regression --profile=coverage --all --execute
  xmake workflow regression --profile=debug --all --generate]],
  },
  list = {
    usage = "list",
    description = "List workflows, test suites and build profiles from workflows.lua, sorted by name.",
    examples = "  xmake workflow list",
  },
  graph = {
    usage = "graph",
    description = "Print the component, suite and workflow impact graph as Mermaid.",
    examples = "  xmake workflow graph > workflow.mmd",
  },
  generate = {
    usage = "generate",
    description = "Regenerate CMakePresets.json from workflows.lua for the retained CMake fallback.",
    examples = "  xmake workflow generate",
  },
  check = {
    usage = "check",
    description = "Check CMakePresets.json against workflows.lua; exit unsuccessfully if stale.",
    examples = "  xmake workflow check",
  },
}

function has_command(name) return commands[name] ~= nil end

local function options_for(command)
  return table.join({
    { "h", "help", "k", nil, "Show this help without configuring or building." },
  }, commands[command].options or {})
end

function show(command)
  local entry = commands[command]
  io.write("Usage: xmake workflow " .. entry.usage .. "\n\n" .. entry.description .. "\n")
  option.show_options(options_for(command))
  if entry.configure then
    io.write("\nAfter --, remaining options are forwarded to xmake f; repeat desired overrides.\n")
    if command == "regression" then io.write("Forwarding applies when using --execute.\n") end
  end
  if entry.notes then io.write("\n" .. entry.notes .. "\n") end
  io.write("\nExamples:\n" .. entry.examples .. "\n\nDiscover names: xmake workflow list\n")
  io.write("Output: xmake workflow [-q | -v | -D] " .. command .. " ...\n")
  io.write("  -q suppresses output, including errors; -v shows details; -D enables diagnostics.\n")
  io.write("Xmake common options: xmake workflow --help\n")
end

function parse(command, arguments)
  local definitions = options_for(command)
  local values = {}
  for _, definition in ipairs(definitions) do
    if definition[3] == "kv" then values[definition[2]] = true end
  end
  local argv, configure = {}, nil
  local name
  local index = 2
  -- Native parsing stops at the first positional argument. The workflow name
  -- precedes its options in our CLI, so extract it before parsing the options.
  if command == "run" or command == "build" then
    name = arguments[index]
    assert(
      name and name:sub(1, 1) ~= "-",
      command .. " requires a workflow name; discover names with xmake workflow list"
    )
    index = index + 1
  end
  local seen = {}
  while index <= #arguments do
    local argument = arguments[index]
    if argument == "--" then
      assert(commands[command].configure, "configure options are not valid for '" .. command .. "'")
      configure = table.slice(arguments, index + 1)
      break
    end
    local key = argument:match("^%-%-([%w-]+)")
    if key then
      assert(not seen[key], "--" .. key .. " may be specified only once")
      seen[key] = true
    end
    -- Preserve --key value as well as Xmake's native --key=value spelling.
    if values[key] and argument == "--" .. key then
      index = index + 1
      local value = arguments[index]
      assert(value and value ~= "" and value:sub(1, 2) ~= "--", "--" .. key .. " requires a value")
      argument = argument .. "=" .. value
    end
    table.insert(argv, argument)
    index = index + 1
  end
  local result = option.raw_parse(argv, definitions)
  for key in pairs(values) do
    assert(
      result[key] == nil or (type(result[key]) == "string" and result[key] ~= ""),
      "--" .. key .. " requires a value"
    )
  end
  result.name, result.configure = name, configure
  result.working_tree = result["working-tree"]
  return result
end
