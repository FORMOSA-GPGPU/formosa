OUTPUT_ARCH(riscv)

/* Above SM lmem_window (0x10000) and sm_printbuf (4 KiB @ 0x10000).
 * Ends at 0x3e000 so WG info at 0x3ffe0 stays outside the image. */
MEMORY {
  MEM (RWX): ORIGIN = 0x11000, LENGTH = 0x2d000
}

ENTRY(_start);

SECTIONS {
  . = ORIGIN(MEM);

  .text : {
    *(.text)
    *(.text.*)
    *(.rodata)
    *(.rodata.*)
  }

  .data : {
    . = ALIGN(8);
    *(.data)
    *(.data.*)
  }

  .bss : {
    . = ALIGN(8);
    _bss_start = .;
    *(.bss)
    *(.bss.*)
    *(COMMON)
    _bss_end = .;
  }
  _end = .;

  . = ORIGIN(MEM) + LENGTH(MEM);
  _stack = .;
}
