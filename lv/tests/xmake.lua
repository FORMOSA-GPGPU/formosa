-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

-- Test-only Lua bindings, built into lv like ilha.test-bindings, such as
-- dbg.PassThroughTester.
target("lv.test-bindings", function()
  set_kind("object")
  add_files("unit/dbg/pass_through_tester.cc")
  add_includedirs("$(projectdir)")
  add_deps("liblv")
end)

target("lv", function()
  add_deps("lv.test-bindings")
  add_tests("docgen", {
    runargs = { "--docgen" },
    pass_outputs = ".*%-%-%-@meta.*",
    run_timeout = 30000,
  })
  for _, script in ipairs(os.files("unit/**.lua")) do
    local case = path.relative(script, "unit"):gsub("%.lua$", "")
    if path.filename(script) ~= "xmake.lua" and case ~= "systemc/ports" then
      add_tests("unit/" .. case, {
        runargs = { path.absolute(script) },
        run_timeout = 30000,
      })
    end
  end
  for _, scenario in ipairs({
    "missing-input",
    "missing-output",
    "duplicate-input",
    "duplicate-output",
    "multiple-writers",
    "late",
  }) do
    add_tests("unit/systemc/ports/" .. scenario, {
      runargs = { path.join(os.scriptdir(), "unit/systemc/ports.lua"), scenario },
      run_timeout = 30000,
    })
  end
  add_tests("unit/runtime", {
    runargs = { path.join(os.scriptdir(), "runtime.lua") },
    pass_outputs = ".*LV resources OK.*",
  })
end)

includes("unit/axi/xmake.lua")

target("lv-log-test", function()
  set_kind("binary")
  add_files("unit/log/log_test.cc")
  add_deps("liblv")
  add_tests("log", { pass_outputs = ".*ALL_OK.*", run_timeout = 30000 })
end)

target("lv-statistics-test", function()
  set_kind("binary")
  add_files("unit/statistics.cc")
  add_deps("liblv")
  add_tests("test", { run_timeout = 30000 })
end)

target("lv-schema-test", function()
  set_kind("binary")
  add_files("unit/schema_test.cc")
  add_deps("liblv")
  add_tests("test", { run_timeout = 30000 })
end)

target("lv-trace-test", function()
  set_kind("binary")
  add_files("unit/trace_test.cc")
  add_deps("liblv")
  add_tests("test", { run_timeout = 30000 })
end)

target("lv-address-router-test", function()
  set_kind("binary")
  add_files(
    "unit/nic/address_router_test.cc",
    "$(projectdir)/lv/src/lv/bindings/nic/address_router.cc"
  )
  add_includedirs("$(projectdir)")
  add_deps("liblv")
  for _, case in ipairs({ "protocol", "empty", "zero", "overflow", "overlap", "masters" }) do
    add_tests(case, { runargs = case ~= "protocol" and { case } or {}, run_timeout = 30000 })
  end
end)
