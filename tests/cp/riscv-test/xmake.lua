-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

for _, xlen in ipairs({ 32, 64 }) do
  local isa = "rv" .. xlen .. "im"
  local suffix = xlen == 64 and "" or "-" .. isa
  target("scalar-riscv-test" .. suffix, function()
    set_arch("riscv" .. xlen)
    add_rules("riscv.elf", "lv.test", "linker.link_scripts")
    set_filename("riscv-test-" .. isa .. ".elf")
    add_includedirs(".", "$(projectdir)/third-party/riscv-tests/isa/macros/scalar")
    on_load(function(target)
      local build = import("tests.cp.build", { rootdir = os.projectdir() })
      local directory = path.absolute(target:autogendir(), os.projectdir())
      os.mkdir(directory)
      local plan = import("tests.cp.riscv-test.prepare", { rootdir = os.projectdir() })(
        isa,
        import("tests.cp.platform", { rootdir = os.projectdir() })(),
        path.join(os.projectdir(), "third-party/riscv-tests"),
        target:scriptdir()
      )
      for name, contents in pairs(plan.files) do
        local output = path.join(directory, name)
        if not os.isfile(output) or io.readfile(output) ~= contents then
          io.writefile(output, contents)
        end
      end
      local prefix = path.join(directory, plan.workload)
      target:add("files", prefix .. ".start.S", prefix .. ".ld")
      target:add("asflags", "-march=" .. plan.isa, { force = true })
      target:add("ldflags", "-march=" .. plan.isa, { force = true })
      for _, source in ipairs(plan.sources) do
        local name = path.basename(source)
        target:add("files", source, {
          defines = {
            "TEST_FUNC_NAME=" .. name,
            "TEST_FUNC_RET=" .. name .. "_ret",
            'TEST_FUNC_TXT="' .. name .. '"',
          },
        })
      end
      target:data_set("manifest", prefix .. ".tests")
      build.tests(target, "riscv-test/run.lua", {
        accepts = function(core) return core.isa == plan.isa end,
        timeout = 120000,
      })
    end)
    after_build(function(target)
      local manifest = target:targetfile():gsub("%.elf$", ".tests")
      os.cp(target:data("manifest"), manifest)
      local logical = "tests/cp/riscv-test/" .. path.basename(target:data("manifest"))
      local layout = import("lv.layout")
      layout.stage_runfile(target:targetfile(), logical .. ".elf")
      layout.stage_runfile(manifest, logical .. ".tests")
    end)
  end)
end

target(
  "lv",
  function()
    add_tests(
      "cp/riscv-test/check_test",
      { runargs = { path.join(os.scriptdir(), "check_test.lua") }, run_timeout = 30000 }
    )
  end
)
