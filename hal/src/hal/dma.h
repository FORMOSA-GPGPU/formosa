/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <lv/mmio/dma/dma.h>

#define FSA_DMA_STATUS_IDLE LV_DMA_STATUS_IDLE
#define FSA_DMA_STATUS_BUSY LV_DMA_STATUS_BUSY
#define FSA_DMA_STATUS_DONE LV_DMA_STATUS_DONE
#define FSA_DMA_STATUS_BUS_ERROR LV_DMA_STATUS_BUS_ERROR
#define FSA_DMA_STATUS_INVALID_DESCRIPTOR LV_DMA_STATUS_INVALID_DESCRIPTOR

#ifndef __ASSEMBLER__

typedef enum {
  kDmaStatusIdle = FSA_DMA_STATUS_IDLE,
  kDmaStatusBusy = FSA_DMA_STATUS_BUSY,
  kDmaStatusDone = FSA_DMA_STATUS_DONE,
  kDmaStatusBusError = FSA_DMA_STATUS_BUS_ERROR,
  kDmaStatusInvalidDescriptor = FSA_DMA_STATUS_INVALID_DESCRIPTOR,
} DmaStatus;

#endif /* __ASSEMBLER__ */
