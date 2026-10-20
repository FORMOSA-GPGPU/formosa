-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

target("hal-mmio-server", function()
  set_kind("binary")
  add_files("lv/mmio_server.cc")
  add_includedirs("..", "../include", "../src", "../src/hal")
  add_deps("libcomm", "formosa-real.lv", "ilha-platform-data")
end)

target("hal-selftest", function()
  set_kind("binary")
  add_files("lv/formosa_hal_test.cc")
  add_includedirs("../include", "../src")
  add_deps("formosa-hal.lv", "ilha-platform-data")
  after_load(
    function(target) target:add("includedirs", target:dep("gpufw-embedded"):autogendir()) end
  )
end)

target("hal-race-test", function()
  set_kind("binary")
  add_files("lv/race_condition_test.cc")
  add_deps("formosa-hal.lv")
end)

target("hal-lv-tests", function()
  set_kind("phony")
  add_deps("hal-mmio-server", "hal-selftest", "hal-race-test")
  add_tests("normal")
  for _, failure in ipairs({ "version", "length", "read" }) do
    add_tests("invalid_" .. failure)
  end
  add_tests("race")
  on_test(function(target, opt)
    local case = path.filename(opt.name)
    local server = path.absolute(target:dep("hal-mmio-server"):targetfile())
    local binary = case == "race" and target:dep("hal-race-test") or target:dep("hal-selftest")
    opt.runenvs = opt.runenvs or {}
    if case == "normal" then
      opt.runenvs.LV_FORMOSA_FAIL_INIT_ONCE = "1"
    elseif case ~= "race" then
      opt.runenvs.FORMOSA_HAL_INVALID_CONFIG = "1"
      opt.runenvs.LV_FORMOSA_CAPABILITY_FAILURE = case:sub(#"invalid_" + 1)
    end
    return import("testing").run_with("bash", {
      path.join(target:scriptdir(), "lv/run_lv_test.sh"),
      server,
      path.absolute(binary:targetfile()),
    }, opt)
  end)
end)
