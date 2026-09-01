/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "cp_retire.h"

#include <string.h>

#include "cp_completion.h"
#include "cp_context.h"
#include "cp_defs.h"
#include "cp_hwinfo.h"
#include "cp_lmem_allocator.h"
#include "cp_log.h"
#include "cp_moving_average.h"
#include "cp_stack_remap.h"
#include "cp_wgi_buffer.h"

/* clang-format off */
#include "FreeRTOS.h" // IWYU pragma: keep
#include "task.h"
/* clang-format on */

// WGInitializer deq status encoding:
//   0: kWGOkay, 1: kWGException, 2: kWGInvalid, 3: kWGDispatchFailed
static FsaCompletionResult to_kernel_completion_result(uint64_t deq_status) {
  switch (deq_status) {
    case 0:
      return FSA_COMPLETION_RESULT_SUCCESS;
    case 1:
      return kKernelCompletionException;
    case 2:
      return kKernelCompletionBadDimension;
    default:
      return kKernelCompletionUnknownError;
  }
}

// Iterate through all the SMs and see which one has DEQ_VALID awaiting
static bool sm_free(volatile struct sm_mmio **smp) {
  for (size_t i = 0; i < g_num_sm; i++) {
    volatile struct sm_mmio *sm = sm_mmio_at((unsigned)i);
    DPRINTF("[R]SMCheck:%zu %lu(%p)\r\n", i, sm->DEQ_VALID, &sm->DEQ_VALID);
    if (sm->DEQ_VALID) {
      *smp = sm;
      return true;
    }
  }
  return false;
}

static bool cache_done() {
  // Early return if there is no undergoing cache operations
  if (cache_state.pending == 0) {
    return false;
  }
  /* A pending operation with no mask is still publishing its MMIO starts.
   * Do not sample START bits until the issuer has made the mask visible. */
  if (cache_state.pending_mask == 0) {
    return false;
  }
  DPRINTF("[R]CacheCheckAll:%lu\r\n", cache_state.pending_mask);

  // Iterate through every SM and update the pending_mask
  for (size_t i = 0; i < g_num_sm; i++) {
    // Skip if the SM is not pending
    if ((cache_state.pending_mask & (1ULL << i)) == 0) {
      continue;
    }

    volatile struct sm_mmio *sm = sm_mmio_at((unsigned)i);
    bool icache_done =
        cache_state.mode != kCacheOpInvalidate || !sm->ICACHE_CONTROL_START;
    bool dcache_done = !sm->DCACHE_CONTROL_START;

    DPRINTF("[R]CacheCheck:%zu %d\r\n", i, icache_done && dcache_done);

    // If both ICache and DCache are done, clear the pending bit
    if (icache_done && dcache_done) {
      cache_state.pending_mask &= ~(1ULL << i);
    }
  }

  // Only when all SMs are not pending will the function return true.
  DPRINTF("[R]CacheCheckAll:%lu\r\n", cache_state.pending_mask);
  if (cache_state.pending_mask == 0) {
    return true;
  }

  // Not finished yet.
  return false;
}

static void free_wg_local_memory(const struct cp_wgi_buf *wginfo) {
  if (wginfo->smid < 0 || (uint64_t)wginfo->smid >= g_num_sm) {
    return;
  }
  if (wginfo->wginfo.local_memory_size == 0) {
    return;
  }

  int rc =
      cp_lmem_allocator_free(wginfo->smid, wginfo->wginfo.local_memory_base);
  if (rc != 0) {
    DPRINTF("[R]LMemFreeFail sm=%d base=%lx size=%u\r\n", wginfo->smid,
            wginfo->wginfo.local_memory_base, wginfo->wginfo.local_memory_size);
  }
}

static void free_wg_stack_remap(const struct cp_wgi_buf *wginfo) {
  if (wginfo->smid < 0 || (uint64_t)wginfo->smid >= g_num_sm) {
    return;
  }
  cp_stack_remap_release(sm_mmio_at((unsigned)wginfo->smid),
                         wginfo->stack_remap_slot);
}

void cp_retire_sm(void *args) {
  volatile struct sm_mmio *sm = sm_mmio_at(0);
  while (1) {
    CP_WAIT_UNTIL(sm_free(&sm));

    struct cp_wgi_buf *wginfo = (struct cp_wgi_buf *)sm->DEQ_INFO_PTR;
    if (wginfo == NULL) {
      DPRINTF("[R]Ignore dequeue with null info ptr\r\n");
      sm->DEQ_VALID = 0;
      continue;
    }

    int kid = wginfo->kid;
    int wbid = wginfo->wbid;
    uint64_t kstatus_ptr = wginfo->kernel_status_ptr;
    if (wbid < 0 || wbid >= wgi_buf_len) {
      DPRINTF("[R]Ignore dequeue with invalid wbid=%d\r\n", wbid);
      sm->DEQ_VALID = 0;
      continue;
    }

    if (kid < 0 || kid >= kernel_state_buf_size) {
      DPRINTF("[R]Ignore dequeue with invalid kid=%d\r\n", kid);
      sm->DEQ_VALID = 0;
      free_wg_local_memory(wginfo);
      free_wg_stack_remap(wginfo);
      cp_wgi_buf_free(wbid);
      continue;
    }

    if (!wgi_buf[wbid].in_use) {
      DPRINTF("[R]Ignore duplicate dequeue kid=%d wbid=%d\r\n", kid, wbid);
      sm->DEQ_VALID = 0;
      continue;
    }

    struct cp_kernel_state *kstate = &kernel_state_buf[kid];
    if (!kstate->in_use) {
      DPRINTF("[R]Ignore dequeue for inactive kernel kid=%d wbid=%d\r\n", kid,
              wbid);
      sm->DEQ_VALID = 0;
      free_wg_local_memory(wginfo);
      free_wg_stack_remap(wginfo);
      cp_wgi_buf_free(wbid);
      continue;
    }

    const FsaCompletionResult result =
        to_kernel_completion_result(sm->DEQ_STATUS);
    KernelStatus kstatus = {
        .mcause = sm->DEQ_MCAUSE,
        .mepc = sm->DEQ_MEPC,
        .mtval = sm->DEQ_MTVAL,
    };
    sm->DEQ_VALID = 0;

    /* Free up bufs */
    free_wg_local_memory(wginfo);
    free_wg_stack_remap(wginfo);
    cp_wgi_buf_free(wbid);

    DPRINTF("[R]Retire kid=%d left_before=%u\r\n", kid, kstate->wg_left);
    if (kstate->wg_left == 0) {
      DPRINTF("[R]Ignore dequeue when wg_left is already zero kid=%d\r\n", kid);
      continue;
    }

    kstate->wg_left--;
    DPRINTF("[R]left_after:%d\r\n", kstate->wg_left);

    if (kstate->wg_left == 0) {
      DPRINTF("[R]KDone:%d\r\n", kid);

      const FsaCompletionToken completion_token = kstate->completion_token;

      if (kstatus_ptr != 0) {
        memcpy((void *)kstatus_ptr, &kstatus, sizeof(kstatus));
      }
      cp_publish_completion(completion_token, result);
      DPRINTF("[R]WriteCompletion token=0x%lx result=%u\r\n",
              (unsigned long)completion_token, (unsigned)result);
      cp_kernel_state_buf_free(kid);
    }
  }
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
