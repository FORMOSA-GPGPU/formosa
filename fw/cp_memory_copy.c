/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "cp_memory_copy.h"

#include <limits.h>
#include <stdint.h>

#include "cp_completion.h"
#include "cp_context.h"
#include "cp_defs.h"
#include "cp_dispatch.h"
#include "cp_log.h"
#include "cp_moving_average.h"
#include "cp_status.h"

/* clang-format off */
#include "FreeRTOS.h" // IWYU pragma: keep
#include "task.h"
/* clang-format on */

static volatile struct dma_mmio *device_dma_mmio(void) {
  return (volatile struct dma_mmio *)DEVICE_DMA_CSR_BASE;
}

static volatile struct dma_mmio *host_dma_mmio(void) {
  return (volatile struct dma_mmio *)HOST_DMA_CSR_BASE;
}

#define CP_MEMORY_COPY_TIMEOUT_TICKS pdMS_TO_TICKS(5000)

static bool range_in_window(uint64_t address, uint64_t size, uint64_t base,
                            uint64_t window_size) {
  if (size == 0 || address < base) {
    return false;
  }
  return address - base <= window_size &&
         size <= window_size - (address - base);
}

static bool valid_memory_range(uint64_t address, uint64_t size) {
  return range_in_window(address, size, FSA_ONCHIP_GMEM_BASE,
                         FSA_ONCHIP_GMEM_SIZE) ||
         /* WGI and the firmware stack remain reserved; the non-cache heap is
          * a device-visible payload aperture used for synchronization data. */
         range_in_window(address, size, FSA_NONCACHE_ALLOC_BASE,
                         FSA_NONCACHE_ALLOC_SIZE) ||
         range_in_window(address, size, FSA_GLOBAL_ALLOC_BASE,
                         FSA_GLOBAL_ALLOC_SIZE);
  /* Firmware staging is reserved; never a MemoryCopy destination/source. */
}

static bool ranges_overlap(uint64_t src, uint64_t dst, uint64_t size) {
  if (size == 0) {
    return false;
  }
  if (src > UINT64_MAX - size || dst > UINT64_MAX - size) return true;
  const uint64_t src_end = src + size;
  const uint64_t dst_end = dst + size;
  return !(src_end <= dst || dst_end <= src);
}

static void complete_memory_copy(uint64_t status) {
  cp_publish_completion(memory_copy_state.completion_token,
                        (FsaCompletionResult)status);
  memory_copy_state.phase = kMemoryCopyPhaseIdle;
  memory_copy_state.active = 0;
}

static void fail_memory_copy(uint64_t status) {
  cp_publish_completion(memory_copy_state.completion_token,
                        (FsaCompletionResult)status);

  /* Keep a timed-out DMA active until it reaches a terminal status. */
  if (memory_copy_state.phase == kMemoryCopyPhaseWaitDma) {
    memory_copy_state.phase = kMemoryCopyPhaseDrainDma;
    return;
  }

  memory_copy_state.phase = kMemoryCopyPhaseIdle;
  memory_copy_state.active = 0;
}

static bool phase_timed_out(void) {
  /* A long predecessor is valid; timeout starts once copy progress begins. */
  if (memory_copy_state.phase == kMemoryCopyPhaseWaitPredecessor ||
      memory_copy_state.phase == kMemoryCopyPhaseDrainDma) {
    return false;
  }
  return (xTaskGetTickCount() - memory_copy_state.phase_started_tick) >=
         CP_MEMORY_COPY_TIMEOUT_TICKS;
}

static void enter_phase(enum cp_memory_copy_phase phase) {
  memory_copy_state.phase = phase;
  memory_copy_state.phase_started_tick = xTaskGetTickCount();
}

static bool start_cache_operation(uint64_t address, uint64_t size,
                                  CacheOpMode mode) {
  /* The regular cache-command path may wait for an earlier operation.  A
   * memory-copy task must never enter that blocking helper: doing so would
   * bypass the copy phase timeout and could starve fault retirement.  Check
   * the shared cache state here and let the cooperative task yield instead. */
  if (cache_state.pending != 0) {
    return false;
  }
  union Packet cmd = {0};
  cmd.cache_operation_packet.addr = address;
  cmd.cache_operation_packet.size = size;
  cmd.cache_operation_packet.completion_token = 0;
  handle_cache_control_packet(&cmd, mode);
  return true;
}

static bool uses_device_source(void) {
  return memory_copy_state.src_domain == kMemoryDomainDevice;
}

static bool uses_device_destination(void) {
  return memory_copy_state.dst_domain == kMemoryDomainDevice;
}

static bool valid_memory_copy_domain_pair(MemoryDomain src_domain,
                                          MemoryDomain dst_domain) {
  /* Runtime memory copies support H2D, D2H, and D2D.  H2H is intentionally
   * invalid because Host DMA is only the host/device transport interface. */
  return (src_domain == kMemoryDomainHost &&
          dst_domain == kMemoryDomainDevice) ||
         (src_domain == kMemoryDomainDevice &&
          (dst_domain == kMemoryDomainHost ||
           dst_domain == kMemoryDomainDevice));
}

static void start_device_dma(void) {
  volatile struct dma_mmio *device_dma = device_dma_mmio();
  device_dma->ADDR0 = memory_copy_state.src_addr;
  device_dma->ADDR1 = memory_copy_state.dst_addr;
  device_dma->SIZE = (int64_t)memory_copy_state.size;
  asm volatile("fence w, w" ::: "memory");
  device_dma->START = 1;
}

static void start_host_dma(void) {
  volatile struct dma_mmio *host_dma = host_dma_mmio();
  const bool host_to_device = memory_copy_state.src_domain == kMemoryDomainHost;
  const uint64_t host_addr =
      host_to_device ? memory_copy_state.src_addr : memory_copy_state.dst_addr;
  const uint64_t device_addr =
      host_to_device ? memory_copy_state.dst_addr : memory_copy_state.src_addr;
  host_dma->ADDR0 = host_addr;
  host_dma->ADDR1 = device_addr;
  host_dma->SIZE = host_to_device ? (int64_t)memory_copy_state.size
                                  : -(int64_t)memory_copy_state.size;
  asm volatile("fence w, w" ::: "memory");
  host_dma->START = 1;
}

bool cp_memory_copy_is_active(void) { return memory_copy_state.active != 0; }

bool cp_memory_copy_dma_is_idle(void) {
  const uint64_t host_status = host_dma_mmio()->STATUS;
  const uint64_t device_status = device_dma_mmio()->STATUS;
  const bool host_terminal = host_status == kDmaStatusIdle ||
                             host_status == kDmaStatusDone ||
                             host_status == kDmaStatusBusError ||
                             host_status == kDmaStatusInvalidDescriptor;
  const bool device_terminal = device_status == kDmaStatusIdle ||
                               device_status == kDmaStatusDone ||
                               device_status == kDmaStatusBusError ||
                               device_status == kDmaStatusInvalidDescriptor;
  return host_terminal && device_terminal;
}

void cp_memory_copy_submit(const MemoryCopyPacket *packet) {
  if (packet == NULL) {
    return;
  }

  if (cp_memory_copy_is_active()) {
    cp_publish_completion(packet->completion_token,
                          kMemoryCopyStatusInternalError);
    return;
  }

  if (!cp_firmware_is_ready() ||
      fsa_completion_token_boot_generation(packet->completion_token) !=
          (uint16_t)cp_firmware_boot_generation()) {
    cp_publish_completion(packet->completion_token, kMemoryCopyStatusNotReady);
    return;
  }

  const MemoryDomain src_domain = (MemoryDomain)packet->src_domain;
  const MemoryDomain dst_domain = (MemoryDomain)packet->dst_domain;
  if (!valid_memory_copy_domain_pair(src_domain, dst_domain)) {
    cp_publish_completion(packet->completion_token,
                          kMemoryCopyStatusInvalidDomainPair);
    return;
  }
  const bool src_is_device = src_domain == kMemoryDomainDevice;
  const bool dst_is_device = dst_domain == kMemoryDomainDevice;

  const bool size_is_zero = packet->size == 0;
  const bool host_address_valid =
      size_is_zero ||
      ((src_domain == kMemoryDomainHost ? packet->src_addr
                                        : packet->dst_addr) != 0);
  const bool device_address_valid =
      size_is_zero ||
      (!src_is_device || valid_memory_range(packet->src_addr, packet->size)) &&
          (!dst_is_device ||
           valid_memory_range(packet->dst_addr, packet->size));
  const bool size_valid = packet->size <= INT64_MAX;
  const bool address_valid =
      host_address_valid && device_address_valid && size_valid;
  const bool overlap =
      src_is_device && dst_is_device &&
      ranges_overlap(packet->src_addr, packet->dst_addr, packet->size);
  if (!address_valid || overlap) {
    const uint64_t result =
        !address_valid ? (!host_address_valid ? kMemoryCopyStatusInvalidAddress
                                              : kMemoryCopyStatusInvalidRange)
                       : kMemoryCopyStatusOverlap;
    cp_publish_completion(packet->completion_token, result);
    return;
  }

  memory_copy_state.src_addr = packet->src_addr;
  memory_copy_state.dst_addr = packet->dst_addr;
  memory_copy_state.size = packet->size;
  memory_copy_state.src_domain = src_domain;
  memory_copy_state.dst_domain = dst_domain;
  memory_copy_state.completion_token = packet->completion_token;
  enter_phase(kMemoryCopyPhaseWaitPredecessor);
  memory_copy_state.active = 1;
}

void cp_memory_copy_task(void *args) {
  (void)args;

  while (1) {
    if (!cp_memory_copy_is_active()) {
      taskYIELD();
      continue;
    }

    if (phase_timed_out()) {
      cp_firmware_status_fault(kFirmwareFaultMemoryCopyTimeout);
      fail_memory_copy(kMemoryCopyStatusTimeout);
      continue;
    }

    switch (memory_copy_state.phase) {
      case kMemoryCopyPhaseWaitPredecessor:
        if (cp_has_active_kernel_or_cache()) {
          taskYIELD();
          break;
        }
        if (memory_copy_state.size == 0) {
          complete_memory_copy(FSA_COMPLETION_RESULT_SUCCESS);
          break;
        }
        if (uses_device_source()) {
          if (start_cache_operation(memory_copy_state.src_addr,
                                    memory_copy_state.size, kCacheOpFlush)) {
            enter_phase(kMemoryCopyPhaseFlushSource);
          }
        } else if (uses_device_destination()) {
          if (start_cache_operation(memory_copy_state.dst_addr,
                                    memory_copy_state.size, kCacheOpFlush)) {
            enter_phase(kMemoryCopyPhaseFlushDestination);
          }
        } else {
          start_host_dma();
          enter_phase(kMemoryCopyPhaseWaitDma);
        }
        break;

      case kMemoryCopyPhaseFlushSource:
        if (cache_state.pending != 0) {
          taskYIELD();
          break;
        }
        if (uses_device_destination()) {
          if (start_cache_operation(memory_copy_state.dst_addr,
                                    memory_copy_state.size, kCacheOpFlush)) {
            enter_phase(kMemoryCopyPhaseFlushDestination);
          }
        } else {
          start_host_dma();
          enter_phase(kMemoryCopyPhaseWaitDma);
        }
        break;

      case kMemoryCopyPhaseFlushDestination: {
        if (cache_state.pending != 0) {
          taskYIELD();
          break;
        }
        if (uses_device_source() && uses_device_destination()) {
          start_device_dma();
        } else {
          start_host_dma();
        }
        enter_phase(kMemoryCopyPhaseWaitDma);
        break;
      }

      case kMemoryCopyPhaseWaitDma: {
        const bool uses_device_dma =
            uses_device_source() && uses_device_destination();
        const uint64_t transfer_status = uses_device_dma
                                             ? device_dma_mmio()->STATUS
                                             : host_dma_mmio()->STATUS;
        if (transfer_status == kDmaStatusDone) {
          if (!uses_device_destination()) {
            complete_memory_copy(FSA_COMPLETION_RESULT_SUCCESS);
            break;
          }
          if (start_cache_operation(memory_copy_state.dst_addr,
                                    memory_copy_state.size,
                                    kCacheOpInvalidate)) {
            enter_phase(kMemoryCopyPhaseInvalidateDestination);
          } else {
            taskYIELD();
          }
        } else if (transfer_status == kDmaStatusBusError) {
          complete_memory_copy(kMemoryCopyStatusDmaBusError);
        } else if (transfer_status == kDmaStatusInvalidDescriptor) {
          complete_memory_copy(kMemoryCopyStatusInvalidRange);
        } else {
          taskYIELD();
        }
        break;
      }

      case kMemoryCopyPhaseInvalidateDestination:
        if (cache_state.pending != 0) {
          taskYIELD();
          break;
        }
        complete_memory_copy(FSA_COMPLETION_RESULT_SUCCESS);
        break;

      case kMemoryCopyPhaseDrainDma:
        if (cp_memory_copy_dma_is_idle()) {
          memory_copy_state.phase = kMemoryCopyPhaseIdle;
          memory_copy_state.active = 0;
        } else {
          taskYIELD();
        }
        break;

      case kMemoryCopyPhaseIdle:
      default:
        memory_copy_state.active = 0;
        break;
    }
  }
}
