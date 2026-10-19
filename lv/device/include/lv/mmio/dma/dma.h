/* SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 * SPDX-License-Identifier: Apache-2.0 */

#ifndef LV_MMIO_DMA_DMA_H_
#define LV_MMIO_DMA_DMA_H_

/* Dual-port DMA: offsets in bytes, relative to the platform-supplied base.
 * All registers require 64-bit accesses. START, ADDR0, ADDR1 and SIZE are
 * read/write while idle; STATUS is read-only. A nonzero START launches a copy.
 * Signed SIZE > 0 copies port0 -> port1; SIZE < 0 copies port1 -> port0.
 * A zero SIZE completes immediately. START clears when the copy finishes.
 */
#define LV_DMA_OFF_START 0x00
#define LV_DMA_OFF_ADDR0 0x08
#define LV_DMA_OFF_ADDR1 0x10
#define LV_DMA_OFF_SIZE 0x18
#define LV_DMA_OFF_STATUS 0x20
#define LV_DMA_REGISTER_BYTES 8

#define LV_DMA_STATUS_IDLE 0
#define LV_DMA_STATUS_BUSY 1
#define LV_DMA_STATUS_DONE 2
#define LV_DMA_STATUS_BUS_ERROR 3
#define LV_DMA_STATUS_INVALID_DESCRIPTOR 4

#endif /* LV_MMIO_DMA_DMA_H_ */
