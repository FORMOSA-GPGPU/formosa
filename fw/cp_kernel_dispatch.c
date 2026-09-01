/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "cp_kernel_dispatch.h"

#include <stdbool.h>
#include <stdio.h>
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

static int wait_for_stack_remap_slot(volatile struct sm_mmio *sm,
                                     uint64_t stack_base, int *slot) {
  int status;
  do {
    status = cp_stack_remap_configure(sm, stack_base, slot);
    if (status == kCpStackRemapFull) {
      taskYIELD();
    }
  } while (status == kCpStackRemapFull);
  return status;
}

static void signal_kernel_failure(KernelDispatchPacket *packet,
                                  FsaCompletionResult result) {
  KernelStatus status = {
      .mcause = 0,
      .mepc = 0,
      .mtval = 0,
  };
  if (packet->kernel_status != 0) {
    memcpy((void *)packet->kernel_status, &status, sizeof(status));
  }
  cp_publish_completion(packet->completion_token, result);
}

static bool valid_kernel_dispatch_packet(const KernelDispatchPacket *packet) {
  const uint16_t flags = packet->dim_reserved0;
  const uint16_t allowed_flags = FSA_KERNEL_DISPATCH_DIM_MASK |
                                 FSA_KERNEL_DISPATCH_HAS_PRINTF_META |
                                 FSA_KERNEL_DISPATCH_STACK_REMAP;
  const uint16_t dim = flags & FSA_KERNEL_DISPATCH_DIM_MASK;
  if (dim < 1 || dim > 3 || (flags & ~allowed_flags) != 0) return false;
  return packet->local_size_x != 0 && packet->local_size_y != 0 &&
         packet->local_size_z != 0 && packet->num_groups_x != 0 &&
         packet->num_groups_y != 0 && packet->num_groups_z != 0;
}

static void signal_stack_remap_failure(KernelDispatchPacket *packet) {
  signal_kernel_failure(packet, kKernelCompletionUnknownError);
}

static inline bool update_id(int num_x, int num_y, int num_z, int *x, int *y,
                             int *z) {
  (*z)++;
  if (*z < num_z) {
    return true;
  }
  *z = 0;
  (*y)++;
  if (*y < num_y) {
    return true;
  }
  *y = 0;
  (*x)++;
  if (*x < num_x) {
    return true;
  }
  return false;
}

/**
 * @brief Try to find a free SM and allocate local memory on it if needed.
 *
 * @param local_mem_size The size of local memory to allocate for this SM, or 0
 * if no local memory is needed.
 * @param smp Output pointer to store the allocated SM's MMIO pointer.
 * @param sm_id_out Output pointer to store the allocated SM's ID.
 * @param local_mem_base_out Output pointer to store the base address of the
 * allocated local memory (valid only if local_mem_size > 0).
 * @return true if an SM is successfully allocated, false if no SM is available
 * or local memory allocation fails.
 */
static bool sm_alloc(uint64_t local_mem_size, volatile struct sm_mmio **smp,
                     size_t *sm_id_out, uint64_t *local_mem_base_out) {
  static size_t prev_sm_id = 0;
  for (size_t i = 0; i < g_num_sm; i++) {
    size_t sm_id = (prev_sm_id + i) % g_num_sm;
    volatile struct sm_mmio *sm = sm_mmio_at((unsigned)sm_id);
    DPRINTF("[D]SMCheck:%zu %lu(%p)\r\n", sm_id, sm->ENQ_VALID, &sm->ENQ_VALID);
    if (!sm->ENQ_VALID) {
      // Found an available SM. Try to allocate local memory if needed.
      uint64_t local_mem_base = 0;
      if (local_mem_size != 0 &&
          cp_lmem_allocator_alloc(sm_id, local_mem_size, &local_mem_base) !=
              0) {
        // Failed to allocate local memory on this SM. Try the next one.
        DPRINTF("[D]SMLmemAllocFail:%zu\r\n", sm_id);
        continue;
      }
      DPRINTF("[D]SMAlloc: SM%zu, local_mem_size=%lu, local_mem_base=0x%lx\r\n",
              sm_id, local_mem_size, local_mem_base);
      *smp = sm;
      *sm_id_out = sm_id;
      *local_mem_base_out = local_mem_base;
      prev_sm_id = (sm_id + 1) % g_num_sm;
      return true;
    }
  }
  return false;
}

void handle_kernel_dispatch_packet(KernelDispatchPacket *packet) {
  if (!valid_kernel_dispatch_packet(packet)) {
    signal_kernel_failure(packet, kKernelCompletionBadDimension);
    return;
  }

  int kid = -1;

  DPRINTF("[D]KallocStart\r\n");
  CP_WAIT_UNTIL((kid = cp_kernel_state_buf_alloc()) >= 0);
  DPRINTF("[D]KallocEnd:%d\r\n", kid);

  struct WGInfo wginfo = {0};

  int num_x = packet->num_groups_x;
  int num_y = packet->num_groups_y;
  int num_z = packet->num_groups_z;

  kernel_state_buf[kid].in_use = 1;
  kernel_state_buf[kid].completion_token = packet->completion_token;
  kernel_state_buf[kid].wg_left = num_x * num_y * num_z;

  if (kernel_state_buf[kid].wg_left == 0) {
    signal_kernel_failure(packet, kKernelCompletionBadDimension);
    cp_kernel_state_buf_free(kid);
    return;
  }

  DPRINTF("[D]Kstate kid=%d wg_left=%u compl=0x%lx status=0x%lx\r\n", kid,
          kernel_state_buf[kid].wg_left,
          (unsigned long)kernel_state_buf[kid].completion_token,
          packet->kernel_status);
  wginfo.dim = packet->dim_reserved0 & FSA_KERNEL_DISPATCH_DIM_MASK;
  wginfo.local_size[0] = packet->local_size_x;
  wginfo.local_size[1] = packet->local_size_y;
  wginfo.local_size[2] = packet->local_size_z;
  wginfo.num_groups[0] = num_x;
  wginfo.num_groups[1] = num_y;
  wginfo.num_groups[2] = num_z;
  wginfo.global_offset[0] = packet->global_offset_x;
  wginfo.global_offset[1] = packet->global_offset_y;
  wginfo.global_offset[2] = packet->global_offset_z;
  wginfo.num_threads = (uint32_t)packet->local_size_x *
                       (uint32_t)packet->local_size_y *
                       (uint32_t)packet->local_size_z;
  wginfo.trampoline = packet->kernel_trampoline;
  if ((packet->dim_reserved0 & FSA_KERNEL_DISPATCH_HAS_PRINTF_META) != 0) {
    PoclPrintfLaunchMeta printf_meta;
    memcpy(&printf_meta, (const void *)packet->kernarg_address,
           sizeof(printf_meta));
    wginfo.printf_buffer = printf_meta.printf_buffer;
    wginfo.printf_buffer_position = printf_meta.printf_buffer_position;
    wginfo.printf_buffer_capacity = printf_meta.printf_buffer_capacity;
    wginfo.reserved0 = 0;
    wginfo.kargs = packet->kernarg_address + sizeof(printf_meta);
  } else {
    wginfo.printf_buffer = 0;
    wginfo.printf_buffer_position = 0;
    wginfo.printf_buffer_capacity = 0;
    wginfo.reserved0 = 0;
    wginfo.kargs = packet->kernarg_address;
  }
  wginfo.stack_size = PER_THREAD_STACK_SIZE;
  wginfo.local_memory_size = packet->local_mem_size;
  int x = 0, y = 0, z = 0;
  /* Iterate through all wg id */
  do {
    wginfo.wg_id[0] = x;
    wginfo.wg_id[1] = y;
    wginfo.wg_id[2] = z;

    int wbid = -1;
    CP_WAIT_UNTIL((wbid = cp_wgi_buf_alloc()) >= 0);
    wgi_buf[wbid].kid = kid;
    wgi_buf[wbid].wbid = wbid;
    wgi_buf[wbid].kernel_status_ptr = packet->kernel_status;

    memcpy(&wgi_buf[wbid].wginfo, &wginfo, sizeof(wginfo));

    DPRINTF("[D]WGI slot=%d kid=%d wbid=%d entry=0x%lx kargs=0x%lx\r\n", wbid,
            kid, wbid, packet->kernel_object, wginfo.kargs);

    /* Launch */
    volatile struct sm_mmio *sm = sm_mmio_at(0);
    size_t sm_id = 0;
    uint64_t local_mem_base = 0;
    DPRINTF("[D]ENQStart\r\n");
    CP_WAIT_UNTIL(
        sm_alloc(wginfo.local_memory_size, &sm, &sm_id, &local_mem_base));
    DPRINTF("[D]ENQEnd\r\n");

    wgi_buf[wbid].smid = sm_id;
    wgi_buf[wbid].wginfo.local_memory_base = local_mem_base;
    wgi_buf[wbid].wginfo.stack_base =
        STACK_BASE +
        wbid * cp_hwinfo_threads_per_core(sm_id) * PER_THREAD_STACK_SIZE;

    if ((packet->dim_reserved0 & FSA_KERNEL_DISPATCH_STACK_REMAP) != 0) {
      int stack_remap_slot = -1;
      int stack_remap_status = wait_for_stack_remap_slot(
          sm, wgi_buf[wbid].wginfo.stack_base, &stack_remap_slot);
      if (stack_remap_status != kCpStackRemapOkay) {
        fprintf(stderr,
                "\033[31mInvalid stack remap configuration for SM %lu, "
                "stack base 0x%lx\033[0m\r\n",
                (unsigned long)sm_id,
                (unsigned long)wgi_buf[wbid].wginfo.stack_base);
        if (wginfo.local_memory_size != 0) {
          cp_lmem_allocator_free(sm_id, local_mem_base);
        }
        cp_wgi_buf_free(wbid);
        cp_kernel_state_buf_free(kid);
        signal_stack_remap_failure(packet);
        return;
      }
      wgi_buf[wbid].stack_remap_slot = stack_remap_slot;
    }

    sm->ENQ_KERNEL_PC = packet->kernel_object;
    sm->ENQ_INFO_PTR = (uint64_t)&wgi_buf[wbid];
    sm->ENQ_NUM_THREADS = wginfo.num_threads;
    asm volatile("fence w, w" ::: "memory");
    sm->ENQ_VALID = 1;
  } while (update_id(num_x, num_y, num_z, &x, &y, &z));
}
