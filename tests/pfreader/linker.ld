/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

MEMORY {
    INST_MEM (RX): ORIGIN = 0x10000, LENGTH = 1M
    DATA_MEM (RWX): ORIGIN = ORIGIN(INST_MEM) + LENGTH(INST_MEM), LENGTH = 1M
}

ENTRY(start);

_pfreader_base = 0x200000;
_serial_base = _pfreader_base + 0x8 * 3;

SECTIONS {
  . = 0x00004000;
  . = ORIGIN(INST_MEM);

  /* Text section: code and read-only data */
  .text : {
    *(.text)
    *(.text.*)
    *(.rodata)
    *(.rodata.*)
  }

  . = ORIGIN(DATA_MEM);

  /* Data section: initialized data */
  .data : {
    *(.data)
    *(.data.*)
  }

  /* BSS section: zero-initialized data */
  .bss : {
    *(.bss)
    *(.bss.*)
    *(COMMON)
  }

  . = 0x180000;
  _stack_start = .;

  _end = .;
}
