-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

-- Each source in these pinned ISA directories is a Makefrag member.
-- Each ELF owns two native cases; the shared wrapper calls one instruction test.
for _, isa in ipairs({ "rv64ui", "rv64um", "rv64uzicond" }) do
  local directory = path.join(os.projectdir(), "third-party/riscv-tests/isa", isa)
  for _, source in ipairs(os.files(path.join(directory, "*.S"))) do
    local name = path.basename(source)
    if name ~= "fence_i" and name ~= "ma_data" then
      local id = isa .. "." .. name
      target("kernel-sim-riscv-test-" .. isa .. "-" .. name, function()
        add_rules("kernel-sim.elf", "lv.test", "linker.link_scripts")
        set_filename("riscv-test." .. id .. ".kernel-sim.elf")
        add_files("../common/start.S", "link.ld")
        add_files(
          "kernel.S",
          path.join(directory, name .. ".S"),
          { defines = { "TEST_FUNC_NAME=" .. name, "TEST_FUNC_RET=" .. name .. "_ret" } }
        )
        add_includedirs(".", "../../../third-party/riscv-tests/isa/macros/scalar")
        on_config(function(target)
          for _, sm in ipairs({ "simtix.atomic_sm", "simtix.pipelined_sm" }) do
            target:add("tests", sm, {
              runargs = {
                path.join(target:scriptdir(), "riscv-test.lua"),
                path.absolute(target:targetfile()),
                "--sm",
                sm,
              },
              run_timeout = 30000,
            })
          end
        end)
      end)
    end
  end
end
