-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

target("ilha.test-bindings", function()
  set_kind("object")
  add_files("unit/cache_command_endpoint.cc")
  add_deps("libilha", "liblv")
end)

target("lv", function()
  add_deps("ilha.test-bindings")
  for _, name in ipairs({ "wg_init", "cache_controller", "sm_transactor" }) do
    add_tests("ilha/" .. name, {
      runargs = { path.join(os.scriptdir(), "unit", name .. ".lua") },
      rundir = os.scriptdir(),
      run_timeout = 30000,
    })
  end
  add_tests("ilha/config", {
    runargs = { path.join(os.scriptdir(), "config.lua"), path.directory(os.scriptdir()) },
    rundir = "/",
    run_timeout = 30000,
  })
end)

target("ilha-platform-data-test", function()
  add_rules("ilha.platform-data", { fixture = "ilha.tests.data.addr_map_fixture" })
  add_tests("platform_data")
  on_test(
    function(target, opt)
      return import("testing").run_with(os.programfile(), {
        "lua",
        path.join(target:scriptdir(), "platform_data.lua"),
        path.join(target:autogendir(), "formosa_addr_map.h"),
        path.join(target:autogendir(), "ilha_memory.ld"),
      }, opt)
    end
  )
end)

target(
  "lv",
  function()
    add_tests("ilha/integration/wg_lifecycle", {
      runargs = { path.join(os.scriptdir(), "integration/wg_lifecycle.lua") },
      rundir = path.directory(os.scriptdir()),
      run_timeout = 30000,
    })
  end
)

target("ilha-system-test", function()
  set_kind("binary")
  set_filename("ilha-idle-rom.elf")
  add_rules("fw.riscv", "lv.test")
  add_files("integration/idle_rom.S")
  add_ldflags(
    "-march=rv64im_zicsr",
    "-mabi=lp64",
    "-mcmodel=medany",
    "-nostdlib",
    "-Wl,--build-id=none",
    "-Wl,-Ttext=0",
    "-Wl,-e,start",
    { force = true }
  )
  after_load(function(target)
    for _, sm in ipairs({ "simtix.atomic_sm", "simtix.pipelined_sm" }) do
      target:add("tests", sm, {
        runargs = {
          path.join(target:scriptdir(), "integration/system_sm.lua"),
          path.absolute(target:targetfile()),
          sm,
        },
        run_timeout = 30000,
      })
    end
  end)
end)
