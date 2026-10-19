/* SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 * SPDX-License-Identifier: Apache-2.0 */

#ifndef LV_DRIVERS_SIMPLE_PFREADER_H_
#define LV_DRIVERS_SIMPLE_PFREADER_H_

#include <lv/mmio/simple/pfreader.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

static inline void lv_pfreader_detail_write_track(uintptr_t base,
                                                  unsigned track,
                                                  int32_t value) {
  *(volatile int64_t *)(base + (uintptr_t)track * LV_PFREADER_TRACK_STRIDE) =
      value;
}

/* Write PerfettoReader events using volatile 64-bit MMIO stores. The platform
 * supplies the mapped, 8-byte-aligned base and valid zero-based track IDs.
 * Slice/instant values index the track's configured content names. Counter
 * values are signed 32-bit. The caller supplies any required I/O ordering;
 * these operations add no fences or cache maintenance.
 */
static inline void pfreader_slice_begin(uintptr_t base, unsigned track,
                                        int32_t value) {
  lv_pfreader_detail_write_track(base, track, value);
}
static inline void pfreader_slice_end(uintptr_t base, unsigned track) {
  lv_pfreader_detail_write_track(base, track, LV_PFREADER_SLICE_END);
}
static inline void pfreader_instant(uintptr_t base, unsigned track,
                                    int32_t value) {
  lv_pfreader_detail_write_track(base, track, value);
}
static inline void pfreader_counter(uintptr_t base, unsigned track,
                                    int32_t value) {
  lv_pfreader_detail_write_track(base, track, value);
}

#ifdef __cplusplus
}
#endif

#endif /* LV_DRIVERS_SIMPLE_PFREADER_H_ */
