-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

target("pfreader-test", function()
  add_rules("riscv.elf", "lv.test", "linker.link_scripts")
  set_filename("pfreader_tester.elf")
  add_files("main.c", "start.S", "linker.ld")
  add_deps("lv.device")
  on_load(function(target)
    target:add("cxflags", "-march=rv64im", { force = true })
    target:add("asflags", "-march=rv64im", { force = true })
    target:add("tests", "trace", {
      runargs = {
        path.join(target:scriptdir(), "pfreader.lua"),
        path.absolute(target:targetfile()),
      },
      run_timeout = 30000,
    })
  end)
  on_test(function(target, opt)
    local passed = target:rule("lv.test"):script("test")(target, opt)
    if passed and not (opt.stdout or ""):find("[CP] End", 1, true) then
      opt.errors = "guest did not reach [CP] End"
      return false
    end
    return passed
  end)
end)
