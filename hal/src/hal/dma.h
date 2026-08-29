/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#define FSA_DMA_STATUS_IDLE 0
#define FSA_DMA_STATUS_BUSY 1
#define FSA_DMA_STATUS_DONE 2
#define FSA_DMA_STATUS_BUS_ERROR 3
#define FSA_DMA_STATUS_INVALID_DESCRIPTOR 4

#ifndef __ASSEMBLER__

typedef enum {
  kDmaStatusIdle = FSA_DMA_STATUS_IDLE,
  kDmaStatusBusy = FSA_DMA_STATUS_BUSY,
  kDmaStatusDone = FSA_DMA_STATUS_DONE,
  kDmaStatusBusError = FSA_DMA_STATUS_BUS_ERROR,
  kDmaStatusInvalidDescriptor = FSA_DMA_STATUS_INVALID_DESCRIPTOR,
} DmaStatus;

#endif /* __ASSEMBLER__ */
