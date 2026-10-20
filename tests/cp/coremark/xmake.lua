-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

for _, xlen in ipairs({ 32, 64 }) do
  local isa = "rv" .. xlen .. "im"
  local suffix = xlen == 64 and "" or "-" .. isa
  for _, profile in ipairs({ "performance", "validation" }) do
    local name = profile == "performance" and "coremark" or "coremark-validation"
    target("cp-" .. name .. suffix, function()
      set_arch("riscv" .. xlen)
      add_rules("cp.runtime-image")
      set_filename("cp-" .. name .. "-" .. isa .. ".elf")
      add_files(
        "$(projectdir)/tests/cp/benchmark/mailbox.c",
        "$(projectdir)/tests/cp/benchmark/linker.ld",
        "core_portme.c",
        "upstream/*.c"
      )
      add_includedirs("$(projectdir)/tests/cp/benchmark", ".", "upstream")
      add_defines("ITERATIONS=10", profile:upper() .. "_RUN=1")
      on_load(function(target)
        import("tests.cp.build", { rootdir = os.projectdir() }).tests(target, "coremark/run.lua", {
          accepts = function(core) return core.isa:match("^" .. isa) end,
          args = { "--profile", profile },
          timeout = 900000,
        })
      end)
      on_config(function(target)
        local runtime = target:dep(xlen == 64 and "cp.runtime" or "cp.runtime." .. isa)
        local flags = table.join(target:get("cflags") or {}, runtime:get("cxflags") or {})
        target:add("defines", 'COMPILER_FLAGS="' .. table.concat(flags, " ") .. '"')
      end)
      after_build(function(target)
        local suffix = profile == "validation" and "-validation" or ""
        import("lv.layout").stage_runfile(
          target:targetfile(),
          "tests/cp/coremark/" .. isa .. suffix .. ".elf"
        )
      end)
    end)
  end
end
