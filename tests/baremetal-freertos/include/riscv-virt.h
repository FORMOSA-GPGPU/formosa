#ifndef RISCV_VIRT_H_
#define RISCV_VIRT_H_

#include <lv/mmio/simple/clint.h>

#ifdef __ASSEMBLER__
#define CONS(NUM, TYPE) NUM
#else
#define CONS(NUM, TYPE) NUM##TYPE
#endif /* __ASSEMBLER__ */

#define PRIM_HART 0

#define CLINT_ADDR CONS(0x02000000, UL)
#define CLINT_MSIP LV_CLINT_MSIP_OFFSET
#define CLINT_MTIMECMP LV_CLINT_MTIMECMP_OFFSET
#define CLINT_MTIME LV_CLINT_MTIME_OFFSET

#endif /* RISCV_VIRT_H_ */
