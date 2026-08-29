.section .text
.globl _start
.align 4

_start:
    li x1,  0   /* ra */
    li x2,  0   /* sp */
    li x3,  0   /* gp */
    li x4,  0   /* tp */
    li x5,  0   /* t0 */
    li x6,  0   /* t1 */
    li x7,  0   /* t2 */
    li x8,  0   /* s0/fp */
    li x9,  0   /* s1 */
    li x10, 0   /* a0 */
    li x11, 0   /* a1 */
    li x12, 0   /* a2 */
    li x13, 0   /* a3 */
    li x14, 0   /* a4 */
    li x15, 0   /* a5 */
    li x16, 0   /* a6 */
    li x17, 0   /* a7 */
    li x18, 0   /* s2 */
    li x19, 0   /* s3 */
    li x20, 0   /* s4 */
    li x21, 0   /* s5 */
    li x22, 0   /* s6 */
    li x23, 0   /* s7 */
    li x24, 0   /* s8 */
    li x25, 0   /* s9 */
    li x26, 0   /* s10 */
    li x27, 0   /* s11 */
    li x28, 0   /* t3 */
    li x29, 0   /* t4 */
    li x30, 0   /* t5 */
    li x31, 0   /* t6 */

    # Set stack pointer
    la sp, _stack_start

    # Set trap handler
    la t0, trap_handler
    li t1, 0xfffffffc
    and t0, t0, t1
    csrw mtvec, t0

    li t0, 0x8          # MIE bit in mstatus (bit 3)
    csrs mstatus, t0    # Set MIE bit to globally enable interrupts

    # Call main function
    call main

hang:
    wfi
    j hang

trap_handler:
    j hang
