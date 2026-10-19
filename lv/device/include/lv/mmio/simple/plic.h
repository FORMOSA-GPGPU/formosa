/* SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 * SPDX-License-Identifier: Apache-2.0 */

#ifndef LV_MMIO_SIMPLE_PLIC_H_
#define LV_MMIO_SIMPLE_PLIC_H_

/* PLIC 1.0.0: byte offsets relative to the platform-supplied base.
 * Registers use little-endian 32-bit accesses. Source IDs start at 1;
 * context IDs start at 0. Bitmap word n contains source IDs 32*n..32*n+31.
 * Priority, enable and threshold are read/write. Pending is read-only.
 * Reading claim/complete claims an interrupt; writing it completes service.
 * These strides are fixed regardless of the implemented source/context count.
 */
#define LV_PLIC_REGISTER_BYTES 4
#define LV_PLIC_PENDING_BASE 0x1000
#define LV_PLIC_BITMAP_BYTES 0x80
#define LV_PLIC_ENABLE_BASE 0x2000
#define LV_PLIC_ENABLE_STRIDE 0x80
#define LV_PLIC_CONTEXT_BASE 0x200000
#define LV_PLIC_CONTEXT_STRIDE 0x1000
#define LV_PLIC_THRESHOLD_OFFSET 0
#define LV_PLIC_CLAIM_COMPLETE_OFFSET 4
#define LV_PLIC_WINDOW_BYTES 0x4000000

#endif /* LV_MMIO_SIMPLE_PLIC_H_ */
