/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "cp_cache.h"

#include <stddef.h>

#include "cp_completion.h"
#include "cp_defs.h"
#include "cp_hwinfo.h"
#include "cp_log.h"
#include "cp_moving_average.h"

/* clang-format off */
#include "FreeRTOS.h" // IWYU pragma: keep
#include "task.h"
/* clang-format on */

struct cp_cache_state {
  // There are undergoing operation
  volatile uint32_t pending;
  // Every START store of the operation has completed
  volatile uint32_t started;
  // CacheTarget bits of the caches the operation was started on
  uint32_t targets;
  /* Host token, or 0 for firmware-internal cache ops. */
  FsaCompletionToken completion_token;
};

static struct cp_cache_state cache_state = {0};

/* Wait for an earlier operation, then claim cache_state for this one.  Until
 * started is set, its starts are in flight and cp_retire_cache will not retire
 * it. */
static void claim_cache_state(uint32_t targets,
                              FsaCompletionToken completion_token) {
  CP_WAIT_UNTIL(cache_state.pending == 0);
  cache_state.started = 0;
  cache_state.targets = targets;
  cache_state.completion_token = completion_token;
  cache_state.pending = 1;
  asm volatile("fence w, w" ::: "memory");
}

/* Whether a cache in targets on this SM is still running an operation. */
static bool sm_caches_busy(volatile struct sm_mmio *sm, uint32_t targets) {
  return ((targets & kCacheTargetICache) && sm->ICACHE_CONTROL_START) ||
         ((targets & kCacheTargetDCache) && sm->DCACHE_CONTROL_START);
}

static void start_on_every_sm(uint64_t addr, uint64_t size, CacheOpMode mode,
                              uint32_t targets) {
  for (size_t i = 0; i < g_num_sm; i++) {
    volatile struct sm_mmio *sm = sm_mmio_at((unsigned)i);
    if (targets & kCacheTargetICache) {
      sm->ICACHE_CONTROL_ADDR = addr;
      sm->ICACHE_CONTROL_SIZE = size;
      sm->ICACHE_CONTROL_MODE = mode;
      sm->ICACHE_CONTROL_START = 1;
    }

    if (targets & kCacheTargetDCache) {
      sm->DCACHE_CONTROL_ADDR = addr;
      sm->DCACHE_CONTROL_SIZE = size;
      sm->DCACHE_CONTROL_MODE = mode;
      sm->DCACHE_CONTROL_START = 1;
    }
  }
}

void cp_cache_operation_start(uint64_t addr, uint64_t size, CacheOpMode mode,
                              uint32_t targets,
                              FsaCompletionToken completion_token) {
  /* Claim the operation before the MMIO starts so other tasks see it pending,
   * but mark it started only after every START store.  The task can be
   * preempted between the two; until then, cp_retire_cache does not mistake
   * caches that have not started yet for finished ones. */
  claim_cache_state(targets, completion_token);
  start_on_every_sm(addr, size, mode, targets);
  cache_state.started = 1;
  asm volatile("fence w, w" ::: "memory");
}

void cp_cache_operation_start_and_wait(uint64_t addr, uint64_t size,
                                       CacheOpMode mode, uint32_t targets) {
  /* started stays zero, so cp_retire_cache leaves this operation to us. */
  claim_cache_state(targets, 0);
  start_on_every_sm(addr, size, mode, targets);
  /* A CP store completes only after the controller latched START, so START
   * reads 1 until the operation finishes.  Polling here avoids the task
   * switches that waiting for cp_retire_cache costs. */
  for (size_t i = 0; i < g_num_sm; i++) {
    while (sm_caches_busy(sm_mmio_at((unsigned)i), targets)) {
    }
  }
  asm volatile("fence w, w" ::: "memory");
  cache_state.pending = 0;
}

bool cp_cache_operation_pending(void) { return cache_state.pending != 0; }

void cp_cache_handle_packet(const union Packet *cmd, CacheOpMode mode) {
  const CacheOperationPacket *packet = &cmd->cache_operation_packet;
  /* A zero size selects the whole cache and ignores the address. */
  if (packet->size != 0 &&
      (packet->addr == 0 || packet->addr > UINT64_MAX - packet->size)) {
    cp_publish_completion(packet->completion_token,
                          FSA_COMPLETION_RESULT_COMMAND_FAILURE_MIN);
    return;
  }

  cp_cache_operation_start(packet->addr, packet->size, mode,
                           cp_cache_targets_for(mode),
                           packet->completion_token);
}

/* Whether a started operation has finished on every SM.  No next operation
 * can start before this one retires, so a finished cache's START stays 0. */
static bool cache_done(void) {
  if (cache_state.pending == 0 || cache_state.started == 0) {
    return false;
  }
  for (size_t i = 0; i < g_num_sm; i++) {
    if (sm_caches_busy(sm_mmio_at((unsigned)i), cache_state.targets)) {
      DPRINTF("[R]CacheBusy:%zu\r\n", i);
      return false;
    }
  }
  return true;
}

void cp_retire_cache(void *args) {
  while (1) {
    CP_WAIT_UNTIL(cache_done());
    DPRINTF("[R]CacheRetire\r\n");
    if (cache_state.completion_token != 0) {
      cp_publish_completion(cache_state.completion_token,
                            FSA_COMPLETION_RESULT_SUCCESS);
    }
    cache_state.completion_token = 0;
    asm volatile("fence w, w" ::: "memory");
    cache_state.pending = 0;
  }
}
