/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "cp_trace.h"

#include <stdint.h>

void cp_trace_slice_begin(int which, int val) {
  ((volatile int64_t *)PFREADER_BASE)[which] = val;
}

void cp_trace_slice_end(int which) {
  ((volatile int64_t *)PFREADER_BASE)[which] = -1;
}

void cp_trace_instant(int which, int val) {
  ((volatile int64_t *)PFREADER_BASE)[which] = val;
}

void cp_trace_counter(int which, int val) {
  ((volatile int64_t *)PFREADER_BASE)[which] = val;
}
