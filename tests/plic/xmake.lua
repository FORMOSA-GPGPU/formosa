-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

-- Register the platform module as a configuration input, not just a hook import.
includes("platform.lua")

target("plic.lua", function()
  set_kind("phony")
  add_rules("lv.lua", { namespaces = { plic = "." } })
end)

target("cp-plic-test", function()
  add_rules("riscv.elf", "lv.test")
  add_files("start.S", "program.c")
  add_deps("lv.device", "plic.lua", "cp.lua")
  set_filename("cp-plic-rv64.elf")
  add_cxflags(
    "-march=rv64im_zicsr",
    "-mabi=lp64",
    "-mcmodel=medany",
    "-O2",
    "-ffreestanding",
    "-fno-builtin",
    "-fno-stack-protector",
    "-ffunction-sections",
    "-fdata-sections",
    "-Wall",
    "-Wextra",
    "-Werror",
    { force = true }
  )
  add_asflags("-march=rv64im_zicsr", "-mabi=lp64", "-mcmodel=medany", { force = true })
  add_ldflags("-march=rv64im_zicsr", "-mabi=lp64", { force = true })
  add_configfiles("platform.h.in", "linker.ld.in", { pattern = "@([%w_]+)@" })
  on_load(function(target)
    local directory = target:autogendir()
    target:set("configdir", directory)
    target:add("includedirs", directory)
    local platform = import("tests.plic.platform", { rootdir = os.projectdir() })()
    for key, value in pairs(platform) do
      target:set("configvar", "PLIC_TEST_" .. key:upper(), string.format("0x%x", value))
    end
    target:set(
      "configvar",
      "PLIC_TEST_RAM_END",
      string.format("0x%x", platform.ram_base + platform.ram_size)
    )
    local script = path.join(directory, "linker.ld")
    target:add("ldflags", "-T" .. script, { force = true })
    target:data_add("linkdepfiles", script)
  end)
  add_tests(
    "cp.command_processor",
    { runargs = { path.join(os.scriptdir(), "run.lua") }, run_timeout = 30000 }
  )
  after_build(
    function(target) import("lv.layout").stage_runfile(target:targetfile(), "tests/plic/rv64.elf") end
  )
end)
