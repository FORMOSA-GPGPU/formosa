/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef CP_MOVING_AVERAGE_H
#define CP_MOVING_AVERAGE_H

#include <stdbool.h>
#include <stdint.h>

struct cp_ma;

enum cp_ma_kind {
  CMD_FETCH = 0,
  KERNEL_STATE_BUF_ALLOC,
  WGI_BUF_ALLOC,
  SM_ALLOC,
  SM_RETIRE,
  CACHE_START,
  CACHE_RETIRE,
  NUM_CP_MA_KIND,
};

void cp_ma_init();
struct cp_ma *cp_get_ma_by_kind(enum cp_ma_kind kind);
void cp_ma_start(struct cp_ma *ma);
void cp_ma_end(struct cp_ma *ma);
uint64_t cp_ma_get_tick(const struct cp_ma *ma);

#define CP_MA_WAIT_WHILE(cond, ma, busy_cond) \
  do {                                        \
    bool __ma_ever_busy = false;              \
    while (cond) {                            \
      if (!__ma_ever_busy && (busy_cond)) {   \
        cp_ma_start(ma);                      \
        __ma_ever_busy = true;                \
      }                                       \
      vTaskDelay(cp_ma_get_tick(ma));         \
    }                                         \
    if (__ma_ever_busy) {                     \
      cp_ma_end(ma);                          \
    }                                         \
  } while (0)

#define CP_MA_WAIT_UNTIL(cond, ma, busy_cond) \
  CP_MA_WAIT_WHILE(!(cond), ma, busy_cond)

#define CP_WAIT_WHILE(cond) \
  do {                      \
    while (cond) {          \
      taskYIELD();          \
    }                       \
  } while (0)

#define CP_WAIT_UNTIL(cond) CP_WAIT_WHILE(!(cond))

#endif
