-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
--
-- SPDX-License-Identifier: Apache-2.0

toolchain("formosa-clang", function()
  set_kind("standalone")
  set_toolset("cc", "clang")
  set_toolset("cxx", "clang++")
  set_toolset("as", "clang")
  set_toolset("ld", "clang")
  set_toolset("ar", "llvm-ar")
end)

rule("riscv.elf", function()
  add_deps("freestanding")
  on_load(function(target)
    target:set("kind", "binary")
    target:set("plat", "cross")
    local arch = target:arch()
    if arch ~= "riscv32" and arch ~= "riscv64" then arch = "riscv64" end
    target:set("arch", arch)
    target:set("toolchains", "formosa-clang")
    target:add("cxflags", "--target=" .. arch, { force = true })
    target:add("asflags", "--target=" .. arch, { force = true })
    target:add(
      "ldflags",
      "--target=" .. arch,
      "-fuse-ld=lld",
      "-static",
      "-Wl,--gc-sections",
      "-nostdlib",
      { force = true }
    )
  end)
end)
