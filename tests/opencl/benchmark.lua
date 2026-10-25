-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

local json = import("core.base.json")
local config = import("core.project.config")

-- Narrow the eligible test domain, then let native Xmake apply group/patterns.
-- Configure the complete workflow first so its suite validation sees all tests.
function prepare(cases)
  for _, target in ipairs(import("core.project.project").ordertargets()) do
    local tests = {}
    if target:rule("opencl.benchmark") and not target:name():startswith("opencl-smoke-") then
      for _, name in ipairs(table.wrap(target:get("tests"))) do
        if
          target:extraconf("tests", name).sm == "simtix.pipelined_sm"
          and (not cases or table.contains(cases, target:name() .. "/" .. name))
        then
          table.insert(tests, name)
        end
      end
    end
    target:set("tests", tests)
  end
end

function run(target, opt)
  local context = os.getenv("FORMOSA_OPENCL_PERF")
  local perf = context and json.loadfile(context)
  local output = path.absolute(
    path.join(perf and perf.out or path.join(config.builddir(), "testing-out"), opt.name)
  )
  os.mkdir(output)
  local argv = {
    path.join(os.scriptdir(), "run_opencl.lua"),
    "-s",
    path.join(output, "stats.toml"),
    "-t",
    path.join(output, "trace"),
    "--sm",
    opt.sm,
  }
  if opt.config then table.join2(argv, { "--config", opt.config }) end
  table.join2(
    argv,
    perf and perf.runner_args or {},
    { "--", path.absolute(target:targetfile()) },
    opt.hostargs or {}
  )
  opt.runenvs = table.join(opt.runenvs or {}, { POCL_CACHE_DIR = path.join(output, "pocl-cache") })
  local binary = path.absolute(target:dep("lv"):targetfile())
  if opt.wrapper then
    argv = table.join(opt.wrapper, { "--", binary }, argv)
    binary = "python3"
  end
  local passed = import("testing").run_with(binary, argv, opt)
  if perf then
    io.writefile(path.join(output, "stdout.log"), opt.stdout or "")
    io.writefile(path.join(output, "stderr.log"), opt.stderr or "")
    if
      not os.isfile(path.join(output, "stats.toml"))
      or os.filesize(path.join(output, "stats.toml")) == 0
    then
      passed, opt.errors = false, "Missing or empty stats: " .. output
    end
    local args, wrapper = opt.hostargs or {}, opt.wrapper or {}
    json.mark_as_array(args)
    json.mark_as_array(wrapper)
    -- Validators already declare their output destination; it is not an input.
    local outputs = {}
    for index, arg in ipairs(wrapper) do
      if arg == "--output" then outputs[path.absolute(wrapper[index + 1], opt.rundir)] = true end
    end
    local inputs = {}
    for _, filename in ipairs(target:sourcefiles()) do
      table.insert(inputs, path.directory(path.absolute(filename)))
    end
    inputs = #inputs > 0 and table.unique(inputs) or { opt.rundir or os.projectdir() }
    json.mark_as_array(inputs)
    json.savefile(path.join(output, "case.json"), {
      passed = passed,
      name = opt.name,
      args = args,
      wrapper = wrapper,
      config = opt.config or "",
      input_dirs = inputs,
      outputs = outputs,
      rundir = opt.rundir or os.projectdir(),
      source_dir = os.projectdir(),
      build_dir = path.absolute(config.builddir()),
      sm = opt.sm,
    })
  end
  return passed
end
