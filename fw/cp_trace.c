/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "cp_trace.h"

#include <lv/drivers/simple/pfreader.h>

void cp_trace_slice_begin(int which, int val) {
  pfreader_slice_begin(PFREADER_BASE, which, val);
}

void cp_trace_slice_end(int which) { pfreader_slice_end(PFREADER_BASE, which); }

void cp_trace_instant(int which, int val) {
  pfreader_instant(PFREADER_BASE, which, val);
}

void cp_trace_counter(int which, int val) {
  pfreader_counter(PFREADER_BASE, which, val);
}
