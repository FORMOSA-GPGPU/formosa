/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef CP_CACHE_H
#define CP_CACHE_H

#include <stdbool.h>
#include <stdint.h>

#include "cp_defs.h"

/* Per-SM caches that one cache operation targets, as a bit mask. */
typedef enum {
  kCacheTargetICache = 1u << 0,
  kCacheTargetDCache = 1u << 1,
} CacheTarget;

/* Caches that a host-visible operation of this mode covers.  The I-cache
 * only needs to handle invalidate. */
static inline uint32_t cp_cache_targets_for(CacheOpMode mode) {
  return kCacheTargetDCache |
         (mode == kCacheOpInvalidate ? kCacheTargetICache : 0u);
}

/* One operation runs at a time on every SM.  Only one task may start one at
 * a time, as claiming the state is not atomic; the dispatch task takes no
 * packet while a memory copy is active, which keeps it and the memory-copy
 * task apart.
 *
 * Completion is read from each cache's START bit, which relies on non-posted
 * MMIO stores: a CP store to START completes only once the cache controller
 * latched it, so START cannot read 0 before the operation began.  Recheck
 * this if CP stores become posted, for example behind a store buffer or a
 * fabric that responds before the controller accepts the write. */

/* Start the operation of a host cache packet, or fail its completion token
 * when the range is invalid. */
void cp_cache_handle_packet(const union Packet *cmd, CacheOpMode mode);
/* Start one operation on the CacheTarget caches of every SM and return, after
 * waiting, yielding, for an earlier operation to retire.  A zero size selects
 * the whole cache.  cp_retire_cache retires the operation and publishes
 * completion_token unless zero. */
void cp_cache_operation_start(uint64_t addr, uint64_t size, CacheOpMode mode,
                              uint32_t targets,
                              FsaCompletionToken completion_token);
/* Start one operation like cp_cache_operation_start, including its wait for
 * an earlier operation, then busy-poll without yielding until every SM
 * finishes it.  For short firmware-internal operations. */
void cp_cache_operation_start_and_wait(uint64_t addr, uint64_t size,
                                       CacheOpMode mode, uint32_t targets);
/* Whether an operation has started and not yet retired. */
bool cp_cache_operation_pending(void);

/* Task that retires the operations cp_cache_operation_start begins. */
void cp_retire_cache(void *args);

#endif
