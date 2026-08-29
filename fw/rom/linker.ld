/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* CP_ROM window 4 KiB @ 0x0 — addr_map/formosa_addr_map.h */
MEMORY
{
  ROM (RX): ORIGIN = 0x0, LENGTH = 4K
}

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
