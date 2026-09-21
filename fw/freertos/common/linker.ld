/* SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University */
/* SPDX-License-Identifier: Apache-2.0 */

INCLUDE ilha_memory.ld

ENTRY(_start);

SECTIONS {
  . = ORIGIN(TCM);

  .text : {
    *(.text)
    *(.text.*)
    *(.rodata)
    *(.rodata.*)
  } > TCM

  .data : {
    *(.data)
    *(.data.*)
  } > TCM

  .bss : {
    __bss_start = .;
    *(.bss)
    *(.bss.*)
    *(COMMON)
    . = ALIGN(8);
    _ebss = .;
    _end = .;
  } > TCM

  . = ALIGN(8);

  /* 4K stack at top of TCM */
  _stack_start = ORIGIN(TCM) + LENGTH(TCM);
  _stack_bottom = _stack_start - 4K;

  ASSERT(_stack_bottom >= _ebss, "Error: BSS is too large, overlaps with Stack in TCM")
}
