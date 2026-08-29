/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef CP_MEMORY_COPY_H
#define CP_MEMORY_COPY_H

#include <stdbool.h>

#include "command_packet.h"

bool cp_memory_copy_is_active(void);
bool cp_memory_copy_dma_is_idle(void);
void cp_memory_copy_submit(const MemoryCopyPacket *packet);
void cp_memory_copy_task(void *args);

#endif
