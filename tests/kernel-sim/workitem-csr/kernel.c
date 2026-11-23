/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* Every lane writes its own value to the per-thread work-item CSRs, and lane 0
 * of each warp writes its warp's value to the per-warp ones. After all warps
 * have written, every lane reads all of them back. CSRs are named by number so
 * the test does not depend on the assembler knowing their names. */

#include <stdint.h>

#define CSR_READ(csr)                                  \
  ({                                                   \
    uint64_t v_;                                       \
    asm volatile("csrr %0, %1" : "=r"(v_) : "i"(csr)); \
    v_;                                                \
  })
#define CSR_WRITE(csr, v) asm volatile("csrw %0, %1" ::"i"(csr), "r"(v))

/* mhartid, xlanes, then the 13 work-item CSRs (see workitem-csr.lua). */
#define ROW_SIZE 15

void kernel(void *args) {
  uint64_t *out = args;
  uint64_t tid = CSR_READ(0xf14);   /* mhartid */
  uint64_t lane = CSR_READ(0xfd0);  /* xlaneid */
  uint64_t lanes = CSR_READ(0xfe0); /* xlanes */
  uint64_t warp = tid / lanes;

  CSR_WRITE(0xbd0, 0x1000 + tid); /* xlocalid_x */
  CSR_WRITE(0xbd1, 0x2000 + tid); /* xlocalid_y */
  CSR_WRITE(0xbd2, 0x3000 + tid); /* xlocalid_z */
  if (lane == 0) {
    CSR_WRITE(0xbc0, 0x10000 + warp); /* xsgsize */
    CSR_WRITE(0xbf0, 0x20000 + warp); /* xgroupid_x */
    CSR_WRITE(0xbf1, 0x30000 + warp); /* xgroupid_y */
    CSR_WRITE(0xbf2, 0x40000 + warp); /* xgroupid_z */
    CSR_WRITE(0xbf4, 0x50000 + warp); /* xlocalsize_x */
    CSR_WRITE(0xbf5, 0x60000 + warp); /* xlocalsize_y */
    CSR_WRITE(0xbf6, 0x70000 + warp); /* xlocalsize_z */
    CSR_WRITE(0xbf8, 0x80000 + warp); /* xglobaloffset_x */
    CSR_WRITE(0xbf9, 0x90000 + warp); /* xglobaloffset_y */
    CSR_WRITE(0xbfa, 0xa0000 + warp); /* xglobaloffset_z */
  }
  asm volatile("fsa.bar 0, 0" ::: "memory");

  uint64_t *row = &out[tid * ROW_SIZE];
  row[0] = tid;
  row[1] = lanes;
  row[2] = CSR_READ(0xbd0);
  row[3] = CSR_READ(0xbd1);
  row[4] = CSR_READ(0xbd2);
  row[5] = CSR_READ(0xbc0);
  row[6] = CSR_READ(0xbf0);
  row[7] = CSR_READ(0xbf1);
  row[8] = CSR_READ(0xbf2);
  row[9] = CSR_READ(0xbf4);
  row[10] = CSR_READ(0xbf5);
  row[11] = CSR_READ(0xbf6);
  row[12] = CSR_READ(0xbf8);
  row[13] = CSR_READ(0xbf9);
  row[14] = CSR_READ(0xbfa);
}
