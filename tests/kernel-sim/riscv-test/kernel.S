.section .bss
.balign 8
kernel_ret_addr:
    .zero 8

.section .text
.global kernel

kernel:
    la t0, kernel_ret_addr
    sd ra, 0(t0)
    .global TEST_FUNC_NAME
    jal zero, TEST_FUNC_NAME
    .global TEST_FUNC_RET
TEST_FUNC_RET:
    la t0, kernel_ret_addr
    ld ra, 0(t0)
    ret
