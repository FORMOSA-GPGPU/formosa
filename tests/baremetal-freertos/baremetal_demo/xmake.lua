-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
-- SPDX-License-Identifier: Apache-2.0

target("baremetal-demo", function()
  set_kind("binary")
  add_rules("baremetal.riscv", "linker.link_scripts", "lv.test")
  add_files("*.c", "../common/linker.ld")
  add_deps("baremetal.common")
  add_syslinks("c")
  set_filename("baremetal_demo.elf")
  on_load(function(target)
    import("tests.cp.build", { rootdir = os.projectdir() }).tests(
      target,
      path.join(target:scriptdir(), "run.lua"),
      {
        accepts = function(core) return core.isa:match("^rv64im") end,
        timeout = 120000,
      }
    )
  end)
  after_build(
    function(target)
      import("lv.layout").stage_runfile(
        target:targetfile(),
        "tests/baremetal-freertos/baremetal_demo/baremetal_demo.elf"
      )
    end
  )
end)
