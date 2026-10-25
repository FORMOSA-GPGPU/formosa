-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

local option = import("core.base.option")
local json = import("core.base.json")
local config = import("core.project.config")

function run()
  local arguments = option.get("arguments") or {}
  local command = table.contains({ "collect", "compare" }, arguments[1]) and arguments[1] or "run"
  local request = { command = command }
  if command == "collect" then
    local options = option.parse(table.slice(arguments, 2), {
      { "i", "input", "kv", nil, "Stats directory (required)." },
      { "o", "out", "kv", nil, "Result JSON file (required)." },
    }, "Usage: xmake perf collect -i STATS -o RESULT.json")
    assert(options.input and options.out, "collect requires -i STATS and -o RESULT.json")
    request.input, request.out = path.absolute(options.input), path.absolute(options.out)
  elseif command == "compare" then
    local first_option = arguments[2] and arguments[2]:sub(1, 1) ~= "-" and 4 or 2
    local options = option.parse(table.slice(arguments, first_option), {
      { "o", "out", "kv", "outputs/perf/comparison", "Report and PNG output directory." },
      {
        nil,
        "metric",
        "kv",
        nil,
        "Numeric TOML path or short alias; default: three primary metrics.",
      },
    }, "Usage: xmake perf compare BASELINE.json CURRENT.json [-o DIR] [--metric=NAME]")
    assert(
      arguments[2]
        and arguments[3]
        and arguments[2]:sub(1, 1) ~= "-"
        and arguments[3]:sub(1, 1) ~= "-",
      "compare requires BASELINE.json CURRENT.json"
    )
    request.baseline, request.current = path.absolute(arguments[2]), path.absolute(arguments[3])
    request.out, request.metric = path.absolute(options.out), options.metric
  else
    local runner_args = {}
    if arguments[1] and arguments[1]:sub(1, 1) == "-" and arguments[1] ~= "--" then
      runner_args, arguments = arguments, {}
    end
    for index, value in ipairs(arguments) do
      if value == "--" then
        runner_args, arguments =
          table.slice(arguments, index + 1), table.slice(arguments, 1, index - 1)
        break
      end
    end
    local jobs = tonumber(option.get("jobs"))
    assert(jobs and jobs > 0 and jobs % 1 == 0, "--jobs must be a positive integer")
    config.load()
    assert(
      config.mode() == "release",
      "Configure first: xmake f -m release --workflow=opencl -o build-opencl"
    )
    if table.contains(runner_args, "--help") or table.contains(runner_args, "-h") then
      os.execv(path.absolute(path.join(config.builddir(), "bin/run_opencl.lua")), runner_args)
      return
    end
    for _, arg in ipairs(runner_args) do
      local key = arg:match("^[^=]+")
      assert(
        arg ~= "--" and key ~= "--stats" and key ~= "--trace" and not arg:match("^%-[st]"),
        "Perf manages --stats, --trace and host arguments per case"
      )
    end
    local out = path.absolute(option.get("out"))
    os.mkdir(out)
    for _, pattern in ipairs({ "**/stats.toml", "**/case.json", "cases.json", "result.json" }) do
      for _, filename in ipairs(os.files(path.join(out, pattern))) do
        os.rm(filename)
      end
    end
    local context = os.tmpfile() .. ".json"
    json.mark_as_array(runner_args)
    json.savefile(context, { out = out, runner_args = runner_args })
    local previous = os.getenv("FORMOSA_OPENCL_PERF")
    local started = os.mclock()
    try({
      function()
        os.setenv("FORMOSA_OPENCL_PERF", context)
        local task = import("core.base.task")
        task.run("config", {}, { disable_dump = true })
        local cases = option.get("ci") and import("perf.ci_cases").main()
        import("benchmark", { rootdir = path.join(os.scriptdir(), "../../../tests/opencl") }).prepare(
          cases
        )
        task.run("test", {
          group = option.get("group"),
          jobs = jobs,
          tests = #arguments > 0 and arguments or nil,
        })
        assert(#os.files(path.join(out, "**/case.json")) > 0, "No OpenCL perf cases selected")
        json.savefile(
          path.join(out, "cases.json"),
          { wall_seconds = (os.mclock() - started) / 1000, runner_args = runner_args }
        )
      end,
      finally({
        function(ok, err)
          os.setenv("FORMOSA_OPENCL_PERF", previous)
          os.tryrm(context)
          if not ok then raise(err) end
        end,
      }),
    })
    request.command, request.input, request.out = "collect", out, path.join(out, "result.json")
  end
  local filename = os.tmpfile() .. ".json"
  json.savefile(filename, request)
  try({
    function()
      local commit = os.getenv("CI_COMMIT_SHA") or os.iorunv("git", { "rev-parse", "HEAD" }):trim()
      os.execv(
        "python3",
        { path.join(os.scriptdir(), "../../../scripts/perf/collect_opencl_stats.py"), filename },
        {
          envs = { CI_COMMIT_SHA = commit },
        }
      )
    end,
    finally({
      function(ok, err)
        os.tryrm(filename)
        if not ok then raise(err) end
      end,
    }),
  })
end
