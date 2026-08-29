MEMORY
{
  ram (rw) : ORIGIN = 0x80000000, LENGTH = 0x200000
}

ENTRY(_start);

SECTIONS
{
    . = ORIGIN(ram);

    .text : {
        *(.text)
        *(.text.*)
        *(.rodata)
        *(.rodata.*)
    } > ram

    .data : {
        *(.data)
        *(.data.*)
    } > ram

    .bss : {
        *(.bss)
        *(.bss.*)
        *(COMMON)
    } > ram

    _end = .; /* Mark the end of the program */

    _stack_start = ORIGIN(ram) + LENGTH(ram);
    _stack_end = _end;
}
