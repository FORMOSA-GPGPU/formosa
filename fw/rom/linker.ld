/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

INCLUDE ilha_memory.ld

ENTRY(start);

SECTIONS {
  . = ORIGIN(ROM);

  .text : {
    *(.text)
    *(.text.*)
    *(.rodata)
    *(.rodata.*)
  } > ROM

  .data : {
    *(.data)
    *(.data.*)
  } > ROM

  .bss : {
    *(.bss)
    *(.bss.*)
    *(COMMON)
    . = ALIGN(8);
    _ebss = .;
    _end = .;
  } > ROM
}
