/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef CP_STACK_REMAP_H
#define CP_STACK_REMAP_H

#include <stdbool.h>
#include <stdint.h>

#include "cp_defs.h"

enum CpStackRemapStatus {
  kCpStackRemapOkay = 0,
  kCpStackRemapFull = 1,
  kCpStackRemapInvalid = -1,
};

bool cp_stack_remap_validate_config(const volatile struct sm_mmio *sm);
void cp_stack_remap_reset(volatile struct sm_mmio *sm);
int cp_stack_remap_configure(volatile struct sm_mmio *sm, uint64_t stack_base,
                             int *slot);
void cp_stack_remap_release(volatile struct sm_mmio *sm, int slot);

#endif
