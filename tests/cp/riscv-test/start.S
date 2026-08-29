.section .text
.globl start
.globl serial_putchar
.globl print_string
.globl green_code
.globl red_code
.globl reset_code
.globl ok_string
.globl err_string

#define TEST(n) \
  .global n; \
  jal zero,n; \
  .global n##_ret; \
  n##_ret:

start:
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

    li t0, 0x888        # Enable MTIE (bit 7), MSIE (bit 3), MEIE (bit 11)
    csrs mie, t0        # Enable timer, software, and external machine-level interrupts

    /* if simple test failed, little chance the rest will pass */
	TEST(simple)

    TEST(lui)
	TEST(auipc)
	TEST(jal)
	TEST(jalr)

	TEST(beq)
	TEST(bne)
	TEST(blt)
	TEST(bge)
	TEST(bltu)
	TEST(bgeu)

	TEST(lb)
	TEST(lh)
	TEST(lw)
    TEST(ld)
	TEST(lbu)
	TEST(lhu)
    TEST(lwu)

	TEST(sb)
	TEST(sh)
	TEST(sw)
    TEST(sd)

	TEST(addi)
    TEST(addiw)
	TEST(slti)
    TEST(sltiu)
	TEST(xori)
	TEST(ori)
	TEST(andi)
	TEST(slli)
    TEST(slliw)
	TEST(srli)
	TEST(srliw)
	TEST(srai)
	TEST(sraiw)

	TEST(add)
    TEST(addw)
	TEST(sub)
    TEST(subw)
	TEST(sll)
    TEST(sllw)
	TEST(slt)
    TEST(sltu)
	TEST(xor)
	TEST(srl)
    TEST(srlw)
	TEST(sra)
    TEST(sraw)
	TEST(or)
	TEST(and)

	TEST(mulh)
	TEST(mulhsu)
	TEST(mulhu)
    TEST(mulw)
	TEST(mul)

	TEST(div)
	TEST(divu)
    TEST(divw)
    TEST(divuw)
	TEST(rem)
	TEST(remu)
    TEST(remw)
    TEST(remuw)

    TEST(ld_st)
    TEST(st_ld)


    la a1, done_string
    call print_string
1:
    j 1b

# serial_putchar(char c)
serial_putchar:
    la t2, _serial_base
    sb a0, 0(t2)
    ret

# Print a null-terminated string at address in a1
print_string:
    addi sp, sp, -4
    sw ra, 0(sp)
1:
    lb a0, 0(a1)
    beqz a0, print_done
    call serial_putchar
    addi a1, a1, 1
    j 1b
print_done:
    lw ra, 0(sp)
    addi sp, sp, 4
    ret

.align 4
trap_handler:
    addi sp, sp, -4*32
    sw x1, 0(sp)
    sw x2, 4(sp)
    sw x3, 8(sp)
    sw x4, 12(sp)
    sw x5, 16(sp)
    sw x6, 20(sp)
    sw x7, 24(sp)
    sw x8, 28(sp)
    sw x9, 32(sp)
    sw x10, 36(sp)
    sw x11, 40(sp)
    sw x12, 44(sp)
    sw x13, 48(sp)
    sw x14, 52(sp)
    sw x15, 56(sp)
    sw x16, 60(sp)
    sw x17, 64(sp)
    sw x18, 68(sp)
    sw x19, 72(sp)
    sw x20, 76(sp)
    sw x21, 80(sp)
    sw x22, 84(sp)
    sw x23, 88(sp)
    sw x24, 92(sp)
    sw x25, 96(sp)
    sw x26, 100(sp)
    sw x27, 104(sp)
    sw x28, 108(sp)
    sw x29, 112(sp)
    sw x30, 116(sp)
    sw x31, 120(sp)

    la a1, red_code
    call print_string
    la a1, int_string
    call print_string
    la a1, reset_code
    call print_string

    lw x1, 0(sp)
    lw x2, 4(sp)
    lw x3, 8(sp)
    lw x4, 12(sp)
    lw x5, 16(sp)
    lw x6, 20(sp)
    lw x7, 24(sp)
    lw x8, 28(sp)
    lw x9, 32(sp)
    lw x10, 36(sp)
    lw x11, 40(sp)
    lw x12, 44(sp)
    lw x13, 48(sp)
    lw x14, 52(sp)
    lw x15, 56(sp)
    lw x16, 60(sp)
    lw x17, 64(sp)
    lw x18, 68(sp)
    lw x19, 72(sp)
    lw x20, 76(sp)
    lw x21, 80(sp)
    lw x22, 84(sp)
    lw x23, 88(sp)
    lw x24, 92(sp)
    lw x25, 96(sp)
    lw x26, 100(sp)
    lw x27, 104(sp)
    lw x28, 108(sp)
    lw x29, 112(sp)
    lw x30, 116(sp)
    lw x31, 120(sp)

    addi sp, sp, 4*32
    mret

.section .data
.align 4
done_string: .string "RISC-V test done\n"
green_code:  .string "\x1B[32m"
red_code:    .string "\x1B[31m"
reset_code:  .string "\x1B[0m"
ok_string:   .string "OK\n"
err_string:  .string "ERR\n"
int_string:  .string "Interrupted\n"
