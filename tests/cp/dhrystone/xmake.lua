-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

for _, xlen in ipairs({ 32, 64 }) do
  local isa = "rv" .. xlen .. "im"
  local suffix = xlen == 64 and "" or "-" .. isa
  target("cp-dhrystone" .. suffix, function()
    set_arch("riscv" .. xlen)
    add_rules("cp.runtime-image")
    set_filename("cp-dhrystone-" .. isa .. ".elf")
    add_files(
      "$(projectdir)/tests/cp/benchmark/mailbox.c",
      "$(projectdir)/tests/cp/benchmark/linker.ld",
      "main.c",
      "$(projectdir)/third-party/riscv-tests/benchmarks/dhrystone/dhrystone.c"
    )
    add_includedirs(
      "$(projectdir)/tests/cp/benchmark",
      "$(projectdir)/third-party/riscv-tests/benchmarks/dhrystone"
    )
    add_cflags(
      "-std=gnu89",
      "-fno-inline",
      "-Wno-implicit-int",
      "-Wno-implicit-function-declaration",
      "-Wno-deprecated-non-prototype",
      { force = true }
    )
    add_defines("DHRYSTONE_ITERATIONS=1000")
    on_load(function(target)
      import("tests.cp.build", { rootdir = os.projectdir() }).tests(target, "dhrystone/run.lua", {
        accepts = function(core) return core.isa:match("^" .. isa) end,
        timeout = 900000,
      })
    end)
    after_build(
      function(target)
        import("lv.layout").stage_runfile(
          target:targetfile(),
          "tests/cp/dhrystone/" .. isa .. ".elf"
        )
      end
    )
  end)
end
