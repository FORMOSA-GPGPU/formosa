/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "cp_moving_average.h"

#include "cp_trace.h"

/* clang-format off */
#include "FreeRTOS.h" // IWYU pragma: keep
#include "task.h"
/* clang-format on */

struct cp_ma {
  uint64_t tickstamp;
  uint64_t avg;
  uint64_t count;
  /* Used only for tracing */
  uint64_t id;
};

static struct cp_ma ma_buf[NUM_CP_MA_KIND];

static uint64_t get_tick() { return xTaskGetTickCount(); }

void cp_ma_init() {
  for (int i = 0; i < NUM_CP_MA_KIND; ++i) {
    ma_buf[i].id = i;
  }
}

struct cp_ma *cp_get_ma_by_kind(enum cp_ma_kind kind) {
  if (kind < NUM_CP_MA_KIND) {
    return &ma_buf[kind];
  } else {
    return NULL;
  }
}

void cp_ma_start(struct cp_ma *ma) { ma->tickstamp = get_tick(); }

void cp_ma_end(struct cp_ma *ma) {
  uint64_t elapsed = get_tick() - ma->tickstamp;
  ma->count++;
  ma->avg = (ma->avg * (ma->count - 1) + elapsed) / ma->count;
  DTRACE_COUNTER(PF_MA_BASE + ma->id, ma->avg);
}

uint64_t cp_ma_get_tick(const struct cp_ma *ma) { return ma->avg; }
