-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

for _, xlen in ipairs({ 32, 64 }) do
  local arch = "riscv" .. xlen
  local isa = "rv" .. xlen .. "i"
  local suffix = xlen == 64 and "" or "-rv" .. xlen
  target("cp-prog0" .. suffix, function()
    set_arch(arch)
    add_rules("riscv.elf", "lv.test", "linker.link_scripts")
    add_files("setup.S", "test.S", "linker.ld")
    set_filename("cp-prog0-rv" .. xlen .. ".elf")
    add_asflags("-march=" .. isa, { force = true })
    add_ldflags("-march=" .. isa, { force = true })
    on_load(function(target)
      import("tests.cp.build", { rootdir = os.projectdir() }).tests(target, "prog0/run.lua", {
        accepts = function(core) return core.isa:match("^" .. isa) end,
        args = {
          "--golden",
          path.join(target:scriptdir(), "rv" .. xlen .. ".golden"),
        },
        timeout = 120000,
      })
    end)
    after_build(
      function(target)
        import("lv.layout").stage_runfile(
          target:targetfile(),
          "tests/cp/prog0/rv" .. xlen .. ".elf"
        )
      end
    )
  end)
end
