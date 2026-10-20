-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

rule("baremetal.riscv", function()
  add_deps("freestanding")
  on_load(function(target)
    import("lib.detect.find_tool")
    -- Sysroot lookup needs the full path, even without `which` or with a
    -- cached bare compiler name from another toolchain check.
    local clang = assert(
      find_tool("clang", { paths = { "$(env PATH)" }, force = true }),
      "FORMOSA LLVM clang is required"
    )
    local sysroot = path.join(path.directory(path.directory(clang.program)), "riscv64-unknown-elf")
    assert(os.isdir(sysroot), "FORMOSA LLVM RISC-V sysroot is missing: " .. sysroot)
    local flags = {
      "--target=riscv64-unknown-elf",
      "-march=rv64im",
      "-mcmodel=medany",
      "--sysroot=" .. sysroot,
    }
    target:set("plat", "cross")
    target:set("arch", "riscv64")
    target:set("toolchains", "formosa-clang")
    target:set("cxflags", table.join(flags, { "-ffreestanding" }), { force = true })
    target:set("asflags", flags, { force = true })
    target:set(
      "ldflags",
      table.join(flags, {
        "-nostdlib",
        "-nodefaultlibs",
        "-fuse-ld=lld",
        "-static",
        "-Wl,--gc-sections",
      }),
      { force = true }
    )
  end)
end)

target("baremetal.common", function()
  set_kind("object")
  add_rules("baremetal.riscv")
  add_files("common/start.S", "common/putchar.c", "$(projectdir)/fw/tinyprintf/tinyprintf.c")
  add_includedirs("common", "$(projectdir)/fw/tinyprintf", { public = true })
end)

target("baremetal.freertos", function()
  set_kind("static")
  add_rules("baremetal.riscv")
  add_rules("freertos.kernel", { configdir = "freertos_demo", portdir = "portable" })
  add_includedirs("include", { public = true })
  add_deps("lv.device", { public = true })
end)

for _, name in ipairs({ "baremetal", "freertos" }) do
  target(name .. "-demo", function()
    set_kind("binary")
    set_default(false)
    set_filename(name .. "_demo.elf")
    add_rules("baremetal.riscv", "linker.link_scripts")
    add_files(name .. "_demo/*.c", "common/linker.ld")
    add_deps("baremetal.common")
    add_syslinks("c")
    if name == "freertos" then add_deps("baremetal.freertos") end
  end)
end
