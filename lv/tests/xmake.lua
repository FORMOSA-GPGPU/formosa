-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

-- Test-only Lua bindings, built into lv like ilha.test-bindings, such as
-- dbg.PassThroughTester.
target("lv.test-bindings", function()
  set_kind("object")
  add_files(
    "unit/dbg/pass_through_tester.cc",
    "unit/dbg/offset_clock.cc",
    "unit/dbg/test_memory.cc",
    "unit/dbg/transport_probe.cc"
  )
  add_includedirs("$(projectdir)")
  add_deps("liblv")
end)

-- Each FastISS case runs unit/cp/fast_iss.lua in its own process. Timing
-- cases sweep Q and each cost; interrupt cases put the timer edge just
-- before, at and after the first quantum boundary.
local fast_iss_cases = {}
for _, q in ipairs({ 1, 4, 16, 64 }) do
  fast_iss_cases["timing-q" .. q] = { "--q", tostring(q) }
  for _, boundary in ipairs({ -1, 0, 1 }) do
    fast_iss_cases["interrupt-q" .. q .. "-boundary" .. boundary] =
      { "--irq", "--q", tostring(q), "--boundary", tostring(boundary) }
  end
end
for _, slope in ipairs({ "cost", "fetch", "read", "write" }) do
  fast_iss_cases["timing-" .. slope] = { "--" .. slope, "15000" }
end
fast_iss_cases["interrupt-fractional"] =
  { "--irq", "--cost", "3333", "--fetch", "2222", "--read", "4444", "--write", "5555" }
fast_iss_cases["private-policy"] = { "--excluded" }
fast_iss_cases["read-only-dmi"] = { "--read-only" }
fast_iss_cases["no-dmi"] = { "--no-dmi" }
fast_iss_cases["compressed"] = { "--compressed" }
fast_iss_cases["ebreak"] = { "--ebreak" }

target("lv", function()
  add_deps("lv.test-bindings")
  add_tests("docgen", {
    runargs = { "--docgen" },
    pass_outputs = ".*%-%-%-@meta.*",
    run_timeout = 30000,
  })
  local multi_case = { ["systemc/ports"] = true, ["cp/fast_iss"] = true }
  for _, script in ipairs(os.files("unit/**.lua")) do
    local case = path.relative(script, "unit"):gsub("%.lua$", "")
    if path.filename(script) ~= "xmake.lua" and not multi_case[case] then
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
  local fast_iss = path.join(os.scriptdir(), "unit/cp/fast_iss.lua")
  for name, args in pairs(fast_iss_cases) do
    add_tests("unit/cp/fast_iss/" .. name, {
      runargs = table.join({ fast_iss }, args),
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
