/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "pfreader_util.h"

#include <stdint.h>

static volatile int64_t *pr_base = 0x0;

void pr_init(int64_t *addr) { pr_base = addr; }

void pr_slice_begin(int which, int val) { pr_base[which] = val; }

void pr_slice_end(int which) { pr_base[which] = -1; }

void pr_instant(int which, int val) { pr_base[which] = val; }

void pr_counter(int which, int val) { pr_base[which] = val; }
