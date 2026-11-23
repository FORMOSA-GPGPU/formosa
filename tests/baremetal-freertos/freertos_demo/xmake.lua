-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
-- SPDX-License-Identifier: Apache-2.0

target("freertos-demo.kernel", function()
  set_kind("static")
  add_rules("baremetal.riscv")
  add_rules("freertos.kernel", { configdir = ".", portdir = "../portable" })
  add_includedirs("../include", { public = true })
  add_deps("lv.device", { public = true })
end)

target("freertos-demo", function()
  set_kind("binary")
  add_rules("baremetal.riscv", "linker.link_scripts", "lv.test")
  add_files("*.c", "../common/linker.ld")
  add_deps("baremetal.common", "freertos-demo.kernel")
  add_syslinks("c")
  set_filename("freertos_demo.elf")
  on_load(function(target)
    import("tests.cp.build", { rootdir = os.projectdir() }).tests(
      target,
      path.join(target:scriptdir(), "run.lua"),
      {
        accepts = function(core) return core.isa:match("^rv64im") and core.has_timer_int end,
        timeout = 120000,
      }
    )
  end)
  after_build(
    function(target)
      import("lv.layout").stage_runfile(
        target:targetfile(),
        "tests/baremetal-freertos/freertos_demo/freertos_demo.elf"
      )
    end
  )
end)
