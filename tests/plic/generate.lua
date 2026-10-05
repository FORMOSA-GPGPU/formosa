-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
-- SPDX-License-Identifier: Apache-2.0

local platform = require("plic.platform")
local directory = assert(arg[1], "expected output directory")

local header = assert(io.open(directory .. "/platform.h", "w"))
header:write("/* Generated from plic.platform. */\n#pragma once\n\n")

local keys = {}
for key in pairs(platform) do
  keys[#keys + 1] = key
end
-- Keep generated headers stable despite Lua's unspecified table iteration order.
table.sort(keys)
for _, key in ipairs(keys) do
  header:write(string.format("#define PLIC_TEST_%s 0x%x\n", key:upper(), platform[key]))
end
header:close()

local linker = assert(io.open(directory .. "/linker.ld", "w"))
linker:write(string.format(
  [[
/* Generated from plic.platform. */
OUTPUT_ARCH(riscv)
ENTRY(start)

SECTIONS {
  . = 0x%x;
  .text : { KEEP(*(.text.start)) *(.text .text.*) }
  .rodata : { *(.rodata .rodata.* .srodata .srodata.*) }
  .data : { *(.data .data.* .sdata .sdata.*) }
  PROVIDE(__global_pointer$ = . + 0x800);

  /* start.S clears this range before calling C code. */
  .bss ALIGN(8) : {
    __bss_start = .;
    *(.bss .bss.* .sbss .sbss.*) *(COMMON)
    . = ALIGN(8);
    __bss_end = .;
  }

  ASSERT(. <= 0x%x, "PLIC test exceeds RAM")
}
]],
  platform.text_base,
  platform.ram_base + platform.ram_size
))
linker:close()
