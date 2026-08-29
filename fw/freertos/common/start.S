    .section .text
    .globl   _start
    .globl   _deadloop
    .align   8

_start:
    li       x1, 0
    li       x2, 0
    li       x3, 0
    li       x4, 0
    li       x5, 0
    li       x6, 0
    li       x7, 0
    li       x8, 0
    li       x9, 0
    li       x10, 0
    li       x11, 0
    li       x12, 0
    li       x13, 0
    li       x14, 0
    li       x15, 0
    li       x16, 0
    li       x17, 0
    li       x18, 0
    li       x19, 0
    li       x20, 0
    li       x21, 0
    li       x22, 0
    li       x23, 0
    li       x24, 0
    li       x25, 0
    li       x26, 0
    li       x27, 0
    li       x28, 0
    li       x29, 0
    li       x30, 0
    li       x31, 0

# Set stack pointer
    la       sp, _stack_start

# Clear BSS so a Firmware Reboot does not reuse prior FreeRTOS state.
    la       t0, __bss_start
    la       t1, _ebss
1:
    bgeu     t0, t1, 2f
    sd       zero, 0(t0)
    addi     t0, t0, 8
    j        1b
2:

# Init nnlc
    call     formosa_libc_init

# Set trap handler
    la       t0, freertos_risc_v_trap_handler
    li       t1, 0xfffffffc
    and      t0, t0, t1
    csrw     mtvec, t0

# Call main function
    call     main

_deadloop:
    j        _deadloop
