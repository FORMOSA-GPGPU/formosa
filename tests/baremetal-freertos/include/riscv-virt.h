#ifndef RISCV_VIRT_H_
#define RISCV_VIRT_H_

#ifdef __ASSEMBLER__
#define CONS(NUM, TYPE) NUM
#else
#define CONS(NUM, TYPE) NUM##TYPE
#endif /* __ASSEMBLER__ */

#define PRIM_HART 0

#define CLINT_ADDR CONS(0x02000000, UL)
#define CLINT_MSIP CONS(0x0000, UL)
#define CLINT_MTIMECMP CONS(0x4000, UL)
#define CLINT_MTIME CONS(0xbff8, UL)

#endif /* RISCV_VIRT_H_ */
