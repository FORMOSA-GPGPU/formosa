/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

OUTPUT_ARCH(riscv)

/* Above SM lmem_window (0x10000) and sm_printbuf (4 KiB @ 0x10000). */
MEMORY {
  MEM (RWX): ORIGIN = 0x11000, LENGTH = 0x12000
}

ENTRY(_start);

SECTIONS {
  . = ORIGIN(MEM);

  /* Text section: code and read-only data */
  .text : {
    *(.text)
    *(.text.*)
    *(.rodata)
    *(.rodata.*)
  }

  /* Data section: initialized data */
  .data : {
    . = ALIGN(8);
    *(.data)
    *(.data.*)
  }

  /* BSS section: zero-initialized data */
  .bss : {
    . = ALIGN(8);
    _bss_start = .;
    *(.bss)
    *(.bss.*)
    *(COMMON)
    _bss_end = .;
  }
  _end = .;

  . = ORIGIN(MEM) + LENGTH(MEM);  /* End of the memory */
  _stack = .;
}
