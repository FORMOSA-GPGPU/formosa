/* Address map: addr_map/formosa_addr_map.h (CP_ROM + CP_TCM) */
MEMORY
{
  ROM (RX): ORIGIN = 0x0, LENGTH = 4K
  TCM (RWX): ORIGIN = 0x10000, LENGTH = 256K
}

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
