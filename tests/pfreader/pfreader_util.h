/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef PR_UTIL
#define PR_UTIL

#include <stdint.h>

void pr_init(int64_t *addr);
void pr_slice_begin(int which, int val);
void pr_slice_end(int which);
void pr_instant(int which, int val);
void pr_counter(int which, int val);

#endif
