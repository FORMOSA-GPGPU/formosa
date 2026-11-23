-- SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
-- SPDX-License-Identifier: Apache-2.0

-- Matches common/linker.ld, common/putchar.c, and include/riscv-virt.h.
return {
  clock_ns = 10,
  ram_base = 0x80000000,
  ram_size = 0x200000,
  serial_base = 0x10000000,
  clint_base = 0x02000000,
}
