/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef CP_KERNEL_DISPATCH_H
#define CP_KERNEL_DISPATCH_H

#include <stdbool.h>
#include <stdint.h>

#include "cp_defs.h"

void handle_kernel_dispatch_packet(KernelDispatchPacket *packet);

#endif
