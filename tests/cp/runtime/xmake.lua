-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

-- The runtime object dependency supplies the startup and libc implementation;
-- each executable owns its test data and ELF, independent of simulator mode.
rule("cp.runtime-image", function()
  add_deps("riscv.elf", "lv.test", "linker.link_scripts")
  on_load(function(target)
    local runtime = target:is_arch("riscv32") and "cp.runtime.rv32im" or "cp.runtime"
    target:add("deps", runtime)
    target:add("cflags", "-O2", "-fno-lto", { force = true })
    local isa = target:is_arch("riscv32") and "rv32im" or "rv64im"
    target:add("ldflags", "-march=" .. isa, "-fno-lto", { force = true })
  end)
  after_load(function(target)
    -- linker.link_scripts selects one script; benchmarks also have a mailbox
    -- overlay, so their common runtime script is a separate link input.
    local runtime = target:is_arch("riscv32") and "cp.runtime.rv32im" or "cp.runtime"
    local script = path.join(target:dep(runtime):autogendir(), "linker.ld")
    target:add("ldflags", "-T" .. script, { force = true })
    target:data_add("linkdepfiles", script)
  end)
end)

for _, xlen in ipairs({ 32, 64 }) do
  local arch, isa = "riscv" .. xlen, "rv" .. xlen .. "im"
  local suffix = xlen == 64 and "" or "-" .. isa
  local runtime = xlen == 64 and "cp.runtime" or "cp.runtime." .. isa
  target(runtime, function()
    set_kind("object")
    set_plat("cross")
    set_arch(arch)
    set_toolchains("formosa-clang")
    add_rules("freestanding")
    add_files(
      "start.S",
      "stdio.c",
      "arithmetic.c",
      "$(projectdir)/fw/nanolibc/c/string.c",
      "$(projectdir)/fw/tinyprintf/tinyprintf.c"
    )
    add_includedirs("$(projectdir)/fw/nanolibc/c/include", { public = true })
    add_includedirs("$(projectdir)/fw/tinyprintf")
    add_cxflags(
      "--target=" .. arch,
      "-march=" .. isa,
      "-ffreestanding",
      "-fno-builtin",
      "-fno-stack-protector",
      "-ffunction-sections",
      "-fdata-sections",
      { public = true, force = true }
    )
    add_asflags("--target=" .. arch, "-march=" .. isa, { force = true })
    add_cflags("-O2", "-fno-lto", { force = true })
    add_configfiles("linker.ld.in", { pattern = "@([%w_]+)@" })
    on_load(function(target)
      target:set("configdir", target:autogendir())
      local platform = import("tests.cp.platform", { rootdir = os.projectdir() })()
      for field, value in pairs(platform) do
        target:set("configvar", "IMAGE_" .. field:upper(), string.format("%d", value))
      end
    end)
  end)

  target("cp-runtime-test" .. suffix, function()
    set_arch(arch)
    add_rules("cp.runtime-image")
    add_files("test.c")
    set_filename("cp-runtime-" .. isa .. ".elf")
    on_load(function(target)
      import("tests.cp.build", { rootdir = os.projectdir() }).tests(target, "runtime/test.lua", {
        accepts = function(core) return core.isa:match("^" .. isa) end,
      })
    end)
    after_build(
      function(target)
        import("lv.layout").stage_runfile(target:targetfile(), "tests/cp/runtime/" .. isa .. ".elf")
      end
    )
  end)
end

target(
  "lv",
  function()
    add_tests("cp/runtime_test", {
      runargs = { path.join(os.projectdir(), "tests/cp/runtime_test.lua") },
      run_timeout = 30000,
    })
  end
)
