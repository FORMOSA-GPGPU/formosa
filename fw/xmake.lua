-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

for name, description in pairs({
  ["fw-enable-log"] = "Local firmware logging: auto follows workflow (otherwise n); y/n override.",
  ["fw-enable-trace"] = "Local firmware tracing: auto follows workflow (otherwise n); y/n override.",
}) do
  option(name, function()
    set_default("auto")
    set_values("auto", "y", "n")
    set_showmenu(true)
    set_description(description)
  end)
end

toolchain("formosa-riscv-gcc", function()
  set_kind("standalone")
  on_check(function()
    local find_tool = import("lib.detect.find_tool")
    return find_tool("gcc", { program = "riscv64-none-elf-gcc" }) ~= nil
      and find_tool("ar", { program = "riscv64-none-elf-ar" }) ~= nil
      and find_tool("ranlib", { program = "riscv64-none-elf-ranlib" }) ~= nil
  end)
  for kind, program in pairs({
    cc = "riscv64-none-elf-gcc",
    as = "riscv64-none-elf-gcc",
    ld = "riscv64-none-elf-gcc",
    ar = "riscv64-none-elf-ar",
    ranlib = "riscv64-none-elf-ranlib",
  }) do
    set_toolset(kind, program)
  end
end)

rule("fw.riscv", function()
  add_deps("freestanding")
  on_load(function(target)
    target:set("languages", "gnu17")
    target:set("plat", "cross")
    target:set("arch", "riscv64")
    target:set("toolchains", "formosa-riscv-gcc")
    target:add("cxflags", "-march=rv64imc_zicsr", "-mabi=lp64", "-mcmodel=medany", {
      force = true,
    })
    target:add("asflags", "-march=rv64imc_zicsr", "-mabi=lp64", "-mcmodel=medany", {
      force = true,
    })
  end)
end)

target("gpufw.freertos", function()
  set_kind("static")
  add_rules("fw.riscv")
  add_rules("freertos.kernel", {
    configdir = "freertos",
    portdir = "freertos/portable",
    heap = "heap_4",
  })
  add_deps("ilha-platform-data", { public = true })
  add_cflags("-ffreestanding", "-Os", { force = true })
end)

target("nnlc", function()
  set_kind("static")
  add_rules("fw.riscv")
  add_files(
    "nanolibc/c/assert.c",
    "nanolibc/c/ctype.c",
    "nanolibc/c/libc.c",
    "nanolibc/c/malloc.c",
    "nanolibc/c/stdio.c",
    "nanolibc/c/stdlib.c",
    "nanolibc/c/string.c",
    "nanolibc/c/strings.c",
    "nanolibc/c/time.c",
    "nanolibc/c/unistd.c",
    "nanolibc/c/unsup.c",
    "nanolibc/formosa/formosa.c",
    "tinyprintf/tinyprintf.c"
  )
  add_includedirs(".", "nanolibc", "nanolibc/c", "nanolibc/c/include", { public = true })
  add_deps("gpufw.freertos", { public = true })
  add_cflags("-ffreestanding", "-ffunction-sections", "-fdata-sections", "-ggdb3", "-Os", {
    force = true,
  })
end)

target("gpufw.elf", function()
  set_kind("binary")
  add_rules("fw.riscv", "linker.link_scripts")
  add_files(
    "freertos/common/start.S",
    "main.c",
    "cp_context.c",
    "cp_status.c",
    "cp_reboot.c",
    "cp_memory_copy.c",
    "cp_hwinfo.c",
    "cp_lmem_allocator.c",
    "cp_retire.c",
    "cp_dispatch.c",
    "cp_kernel_dispatch.c",
    "cp_wgi_buffer.c",
    "cp_stack_remap.c",
    "cp_trace.c",
    "cp_moving_average.c",
    "cp_panic.c",
    "freertos/common/linker.ld"
  )
  add_deps("nnlc", "hal.protocol", "ilha-platform-data")
  on_load(function(target)
    local backend = import("workflow.backends.xmake")
    for option, define in pairs({
      ["fw-enable-log"] = "FW_ENABLE_LOG",
      ["fw-enable-trace"] = "FW_ENABLE_TRACE",
    }) do
      if backend.enabled(option, false) then target:add("defines", define) end
    end
  end)
  add_defines("CP_LMEM_ALLOCATOR_USE_FREERTOS")
  after_load(
    function(target)
      target:add("ldflags", "-L" .. target:dep("ilha-platform-data"):autogendir(), { force = true })
    end
  )
  add_cflags("-ffunction-sections", "-fdata-sections", "-ggdb3", "-Os", { force = true })
  add_ldflags(
    "-march=rv64imc_zicsr",
    "-mabi=lp64",
    "-mcmodel=medany",
    "-ffreestanding",
    "-flto",
    "-nostdlib",
    "-nodefaultlibs",
    "-fuse-ld=lld",
    "-static",
    "-Wl,--gc-sections",
    { force = true }
  )
end)

target("fwrom.elf", function()
  set_kind("binary")
  add_rules("fw.riscv", "linker.link_scripts")
  add_files("rom/rom.S", "rom/linker.ld")
  add_deps("hal.protocol", "ilha-platform-data")
  add_asflags("-ffreestanding", "-ffunction-sections", "-fdata-sections", "-ggdb3", "-Os", {
    force = true,
  })
  after_load(
    function(target)
      target:add("ldflags", "-L" .. target:dep("ilha-platform-data"):autogendir(), { force = true })
    end
  )
  add_ldflags(
    "-march=rv64imc_zicsr",
    "-mabi=lp64",
    "-mcmodel=medany",
    "-nostdlib",
    "-nodefaultlibs",
    "-fuse-ld=lld",
    "-static",
    "-Wl,--gc-sections",
    { force = true }
  )
end)

target("fw.runfiles", function()
  set_kind("phony")
  add_deps("gpufw.elf", "fwrom.elf", { inherit = false })
  after_load(function(target)
    local dirs = import("lv.layout").directories()
    for _, name in ipairs({ "gpufw.elf", "fwrom.elf" }) do
      target:add("installfiles", path.absolute(target:dep(name):targetfile()), {
        prefixdir = path.join(dirs.runfiles, "bin"),
        public = true,
      })
    end
  end)
  on_build(function(target)
    import("lv.layout")
    for _, name in ipairs({ "gpufw.elf", "fwrom.elf" }) do
      layout.stage_runfile(target:dep(name):targetfile(), path.join("bin", name))
    end
  end)
end)

includes("tests/xmake.lua")
