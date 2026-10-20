-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

for _, xlen in ipairs({ 32, 64 }) do
  local arch = "riscv" .. xlen
  local isa = "rv" .. xlen .. "i"
  local suffix = xlen == 64 and "" or "-rv" .. xlen
  target("cp-interrupt" .. suffix, function()
    set_arch(arch)
    add_rules("riscv.elf", "lv.test")
    add_files("interrupt.S")
    set_filename("cp-interrupt-rv" .. xlen .. ".elf")
    add_asflags("-march=" .. isa .. "_zicsr", { force = true })
    add_ldflags("-march=" .. isa .. "_zicsr", "-Wl,-e,start", { force = true })
    on_load(function(target)
      local build = import("tests.cp.build", { rootdir = os.projectdir() })
      target:add(
        "ldflags",
        string.format(
          "-Wl,-Ttext=0x%x",
          import("tests.cp.platform", { rootdir = os.projectdir() })().text_base
        ),
        { force = true }
      )
      build.tests(target, "interrupt/run.lua", {
        accepts = function(core) return core.isa:match("^" .. isa) and core.has_ext_int end,
      })
    end)
    after_build(
      function(target)
        import("lv.layout").stage_runfile(
          target:targetfile(),
          "tests/cp/interrupt/rv" .. xlen .. ".elf"
        )
      end
    )
  end)
end
