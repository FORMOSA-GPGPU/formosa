MEMORY {
    INST_MEM (RX): ORIGIN = 0x10000, LENGTH = 1M
    DATA_MEM (RWX): ORIGIN = ORIGIN(INST_MEM) + LENGTH(INST_MEM), LENGTH = 1M
}

ENTRY(start);

_serial_base = 0x200000;

SECTIONS {
  . = 0x00004000;
  _stack_start = .;
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

  _end = .;
}
