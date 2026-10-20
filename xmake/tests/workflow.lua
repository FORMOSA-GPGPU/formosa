-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

-- Exercise real native configuration and execution without building the product.
function main()
  local root = path.absolute(path.join(os.scriptdir(), "../.."))
  local fixture = os.tmpdir() .. "/formosa-workflow-" .. tostring(os.getpid())
  os.mkdir(fixture)
  local errors
  try({
    function()
      io.writefile(
        path.join(fixture, "workflows.lua"),
        [[
local config = {
  components = { source = { paths = { "source/**" } } },
  build_profiles = { debug = { xmake = { mode = "debug" } } },
  test_suites = {
    first = { depends = { "source" }, adapters = { xmake = { build_options = { probe = true }, selectors = { "sample/first", "sample/shared" } } } },
    second = { depends = { "source" }, adapters = { xmake = { selectors = { "sample/second", "sample/shared" } } } },
    missing = { depends = { "source" }, adapters = { xmake = { selectors = { "sample/absent" } } } },
    failing = { depends = { "source" }, adapters = { xmake = { selectors = { "failure/test" } } } },
    build_failure = { depends = { "source" }, adapters = { xmake = { selectors = { "build-failure/test" } } } },
    install_failure = { depends = { "source" }, adapters = { xmake = { build_options = { ["install-targets"] = { "package" } }, selectors = { "sample/first" } } } },
  },
  workflows = {
    good = { description = "Overlapping suites", test_suites = { "first", "second" } },
    missing = { description = "Stale selector", test_suites = { "missing" } },
    failing = { description = "Test failure", test_suites = { "failing" } },
    build_failure = { description = "Build failure", test_suites = { "build_failure" } },
    installing = { description = "Build installs package", install = true, test_suites = { "install_failure" } },
    install_failure = { description = "Install failure", install = true, test_suites = { "install_failure" } },
  },
}
function main() return config end
]]
      )
      io.writefile(
        path.join(fixture, "xmake.lua"),
        string.format(
          [[
set_project("workflow-fixture")
set_defaultmode("debug")
set_default(false)
add_moduledirs(%q)
option("workflow", function() set_default("") end)
option("test-suites", function() set_default("") end)
option("probe", function() set_default("auto"); set_values("auto", "y", "n") end)
includes(%q, %q)
add_rules("workflow.test_suites")
local function output_flags(option)
  local flags = {}
  for _, name in ipairs({ "quiet", "verbose", "diagnosis" }) do
    table.insert(flags, tostring(not not option.get(name)))
  end
  return table.concat(flags, ",")
end
target("sample", function()
  on_load(function(target)
    target:data_set("probe", import("workflow.backends.xmake").enabled("probe", false))
  end)
  on_config(function()
    io.writefile(path.join(os.projectdir(), "configure-flags.txt"), output_flags(import("core.base.option")))
  end)
  set_kind("phony")
  add_tests("first")
  add_tests("second")
  add_tests("shared")
  on_build(function(target)
    io.writefile(path.join(os.projectdir(), "build-flags.txt"), output_flags(import("core.base.option")))
    io.writefile("probe.txt", tostring(target:data("probe")))
  end)
  on_test(function(target, opt)
    if opt.name == "sample/first" then
      io.writefile(path.join(os.projectdir(), "test-flags.txt"), output_flags(import("core.base.option")))
    end
    local file = io.open(path.join(os.projectdir(), "executed.txt"), "a")
    file:write(opt.name .. "\n")
    file:close()
    return true
  end)
end)
target("failure", function()
  set_kind("phony")
  add_tests("test")
  on_test(function(target, opt)
    opt.errors = "deliberate test failure"
    return false
  end)
end)
target("build-failure", function()
  set_kind("phony")
  add_tests("test")
  on_build(function() raise("deliberate build failure") end)
end)
target("package", function()
  set_kind("phony")
  add_rules("workflow.install")
  add_deps("sample")
  on_install(function(target)
    assert(os.isfile("probe.txt"), "installation ran before dependencies")
    if get_config("workflow") == "install_failure" then raise("deliberate install failure") end
    os.mkdir(target:installdir())
    io.writefile(path.join(target:installdir(), "package.txt"), io.readfile("probe.txt"))
    local file = io.open("installed.txt", "a")
    file:write("installed\n")
    file:close()
  end)
end)
]],
          path.join(root, "xmake/modules"),
          path.join(root, "xmake/tasks/workflow/xmake.lua"),
          path.join(root, "xmake/rules/workflow/xmake.lua")
        )
      )
      local function invoke(args, expected, diagnostic)
        local out, err = os.tmpfile(), os.tmpfile()
        local status = os.execv(os.programfile(), args, {
          try = true,
          stdout = out,
          stderr = err,
          curdir = fixture,
          envs = { XMAKE_CONFIGDIR = path.join(fixture, ".xmake") },
        })
        local output = (io.readfile(out) or "") .. (io.readfile(err) or "")
        os.tryrm(out)
        os.tryrm(err)
        local context = table.concat(args, " ") .. " (exit " .. tostring(status) .. ")\n" .. output
        assert((status == 0) == expected, context)
        if diagnostic then assert(output:find(diagnostic, 1, true), context) end
        return output
      end
      -- Help must work before loading workflows.lua or running any product hook.
      local definition = path.join(fixture, "workflows.lua")
      local contents = io.readfile(definition)
      io.writefile(definition, 'error("help must not load the workflow definition")')
      invoke({ "workflow", "--help" }, true, "COMMAND --help")
      for _, command in ipairs({
        "run",
        "build",
        "regression",
        "list",
        "graph",
        "generate",
        "check",
      }) do
        for _, flag in ipairs({ "--help", "-h" }) do
          local output =
            invoke({ "workflow", command, flag }, true, "Usage: xmake workflow " .. command)
          assert(not output:find("Configuration:", 1, true), output)
        end
      end
      invoke({ "workflow", "run", "good", "--help" }, true, "-- configure-options")
      assert(not os.isfile(path.join(fixture, "probe.txt")), "help built a target")
      assert(not os.isfile(path.join(fixture, "executed.txt")), "help ran tests")
      assert(not os.isfile(path.join(fixture, "CMakePresets.json")), "help generated presets")
      assert(
        not os.isfile(path.join(fixture, "CMakeUserPresets.json")),
        "help generated user presets"
      )
      assert(
        not os.isfile(path.join(fixture, ".xmake/.xmake", os.host(), os.arch(), "xmake.conf")),
        "help configured the project"
      )
      io.writefile(definition, contents)
      local listing = invoke({ "workflow", "list" }, true, "Workflows:")
      assert(listing:find("good%s+%- Overlapping suites"), listing)
      assert(listing:find("Test suites:", 1, true), listing)
      assert(listing:find("Build profiles:\n  - debug", 1, true), listing)
      assert(listing:find("  - first\n", 1, true), listing)
      invoke({ "workflow" }, false, "Run: xmake workflow --help")
      invoke({ "workflow", "unknown", "--help" }, false, "Unknown workflow command 'unknown'")
      invoke({ "workflow", "run" }, false, "Run: xmake workflow run --help")
      invoke({ "workflow", "build", "--jobs=2" }, false, "requires a workflow name")
      invoke({ "workflow", "run", "unknown" }, false, "Discover names: xmake workflow list")
      invoke({ "workflow", "run", "unknown", "--", "--help" }, false, "Unknown workflow: unknown")
      invoke({ "workflow", "run", "good", "--invalid" }, false, "Run: xmake workflow run --help")
      invoke(
        { "workflow", "run", "good", "--profile=unknown" },
        false,
        "Unknown build profile: unknown"
      )
      invoke({ "workflow", "regression" }, false, "Run: xmake workflow regression --help")
      invoke({ "workflow", "list", "--profile=debug" }, false, "Run: xmake workflow list --help")
      invoke({ "workflow", "regression", "--profile=" }, false, "requires a value")
      invoke({ "workflow", "run", "good", "--jobs" }, false, "requires a value")
      invoke({ "workflow", "run", "good", "--jobs=1", "--jobs=2" }, false, "only once")
      invoke({ "workflow", "list", "--", "--help" }, false, "configure options are not valid")
      local plan = invoke({ "workflow", "regression", "--profile=debug", "--all" }, true)
      assert(plan:find("Regression: debug / xmake", 1, true), plan)
      assert(plan:find("Selection: all suites requested", 1, true), plan)
      assert(plan:find("Selected test suites:", 1, true), plan)
      assert(plan:find("Plan only", 1, true), plan)
      assert(not plan:find("Merged selectors", 1, true), plan)
      assert(not plan:find("Changed files", 1, true), plan)
      invoke(
        { "workflow", "-v", "regression", "--profile=debug", "--all" },
        true,
        "Merged selectors"
      )
      local quiet = invoke({ "workflow", "-q", "regression", "--profile=debug", "--all" }, true)
      assert(quiet == "", quiet)
      invoke({ "workflow", "-q", "run", "unknown" }, false)
      local configured =
        invoke({ "workflow", "-vD", "run", "good", "--profile", "debug", "--jobs=2" }, true)
      local _, summaries = configured:gsub("Configuration: good", "")
      assert(summaries == 1, configured)
      assert(configured:find("probe: y (auto)", 1, true), configured)
      for _, phase in ipairs({ "configure", "build", "test" }) do
        assert(io.readfile(path.join(fixture, phase .. "-flags.txt")) == "false,true,true", phase)
      end
      local names = io.readfile(path.join(fixture, "executed.txt"))
        :split("\n", { trimempty = true })
      table.sort(names)
      assert(
        table.concat(names, ",") == "sample/first,sample/second,sample/shared",
        "suite union duplicated or dropped tests"
      )
      assert(io.readfile(path.join(fixture, "probe.txt")) == "true")
      local executed = io.readfile(path.join(fixture, "executed.txt"))
      local quiet_build =
        invoke({ "workflow", "-q", "build", "good", "--jobs=2", "--", "--probe=n" }, true)
      assert(quiet_build == "", quiet_build)
      for _, phase in ipairs({ "configure", "build" }) do
        assert(io.readfile(path.join(fixture, phase .. "-flags.txt")) == "true,false,false", phase)
      end
      assert(io.readfile(path.join(fixture, "probe.txt")) == "false", "CLI override lost")
      assert(io.readfile(path.join(fixture, "executed.txt")) == executed, "build ran tests")
      invoke({ "f", "-c", "-y", "--workflow=unknown" }, false, "Unknown workflow: unknown")
      invoke({ "f", "-c", "-y", "--test-suites=unknown" }, false, "Unknown test suite: unknown")
      invoke({ "f", "-c", "-y", "--test-suites=,,," }, false, "requires at least one test suite")
      local failed_config =
        invoke({ "f", "-c", "-y", "--workflow=missing" }, false, "matches no Xmake test cases")
      assert(not failed_config:find("Configuration:", 1, true), failed_config)
      invoke({ "workflow", "-q", "run", "failing", "--jobs=2" }, false)
      local logs = os.files(path.join(fixture, "build/.gens/**/failure/test.errors.log"))
      assert(#logs == 1, "quiet failure lost its diagnostic log")
      assert(io.readfile(logs[1]):find("deliberate test failure", 1, true))
      invoke({ "workflow", "run", "build_failure" }, false, "deliberate build failure")
      invoke({ "workflow", "run", "install_failure" }, false, "deliberate install failure")
      -- The native build path must install, including on a no-op rebuild after
      -- deleting the prefix. A targeted host build must not trigger installation.
      os.tryrm(path.join(fixture, "probe.txt"))
      local configured = invoke({ "f", "-c", "-y", "--workflow=installing" }, true)
      assert(configured:find("Install on build: package -> ", 1, true), configured)
      local built = invoke({ "build", "sample" }, true)
      assert(not built:find("Configuration:", 1, true), built)
      assert(not os.isfile(path.join(fixture, "installed.txt")), "host build installed package")
      invoke({ "build" }, true)
      assert(os.isfile(path.join(fixture, "outputs/out/package.txt")))
      os.rm(path.join(fixture, "outputs/out"))
      invoke({ "build" }, true)
      assert(
        os.isfile(path.join(fixture, "outputs/out/package.txt")),
        "build did not restore prefix"
      )
      os.rm(path.join(fixture, "installed.txt"))
      invoke({ "workflow", "build", "installing", "--installdir=custom-prefix" }, true)
      assert(os.isfile(path.join(fixture, "custom-prefix/package.txt")))
      assert(
        io.readfile(path.join(fixture, "installed.txt")) == "installed\n",
        "duplicate installation"
      )
      invoke({ "install", "-o", "manual-prefix", "package" }, true)
      assert(os.isfile(path.join(fixture, "manual-prefix/package.txt")), "manual prefix ignored")
      os.rm(path.join(fixture, "installed.txt"))
      local configured = invoke({ "f", "-c", "-y", "--workflow=good", "--probe=n" }, true)
      assert(configured:find("probe: n (explicit)", 1, true), configured)
      assert(not configured:find("Install on build:", 1, true), configured)
      invoke({ "build", "package" }, true)
      assert(
        not os.isfile(path.join(fixture, "installed.txt")),
        "non-installing workflow installed"
      )
      invoke({ "f", "-c", "-y", "--workflow=install_failure" }, true)
      invoke({ "build" }, false, "deliberate install failure")
    end,
    catch({ function(err) errors = err end }),
    finally({ function() os.tryrm(fixture) end }),
  })
  assert(not errors, errors)
  print("Workflow configuration, suite union and native failure propagation passed")
end
