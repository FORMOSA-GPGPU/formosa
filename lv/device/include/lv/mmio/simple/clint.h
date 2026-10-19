/* SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 * SPDX-License-Identifier: Apache-2.0 */

#ifndef LV_MMIO_SIMPLE_CLINT_H_
#define LV_MMIO_SIMPLE_CLINT_H_

/* SiFive CLINT layout shared by simple.Clint and simple.VClint.
 * Byte offsets are relative to the platform-supplied base. MSIP is a 32-bit
 * read/write register per hart (bit 0 is implemented). MTIMECMP is a 64-bit
 * read/write register per hart; MTIME is a shared 64-bit read/write register.
 * Timer registers also support 32-bit accesses to their low/high words.
 * The platform supplies the base, hart count and timer frequency.
 */
#define LV_CLINT_MSIP_OFFSET 0x0000
#define LV_CLINT_MSIP_STRIDE 4
#define LV_CLINT_MTIMECMP_OFFSET 0x4000
#define LV_CLINT_MTIMECMP_STRIDE 8
#define LV_CLINT_MTIME_OFFSET 0xBFF8
#define LV_CLINT_TIMER_BYTES 8
#define LV_CLINT_WORD_BYTES 4
#define LV_CLINT_WINDOW_BYTES 0x10000

#endif /* LV_MMIO_SIMPLE_CLINT_H_ */
