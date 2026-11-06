/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "cp_dispatch.h"

#include <stdbool.h>
#include <string.h>

#include "cp_cache.h"
#include "cp_completion.h"
#include "cp_context.h"
#include "cp_defs.h"
#include "cp_kernel_dispatch.h"
#include "cp_log.h"
#include "cp_memory_copy.h"
#include "cp_reboot.h"
#include "cp_status.h"

/* Final eight bytes of every Host packet carry the completion field. */
static FsaCompletionToken host_token_from_packet(const union Packet *cmd) {
  FsaCompletionToken token = 0;
  memcpy(&token, cmd->raw + FSA_COMPLETION_TOKEN_OFFSET, sizeof(token));
  return token;
}

static void fail_host_token_if_present(const union Packet *cmd) {
  const FsaCompletionToken token = host_token_from_packet(cmd);
  if (token != 0) {
    cp_publish_completion(token, FSA_COMPLETION_RESULT_COMMAND_FAILURE_MIN);
  }
}

/* clang-format off */
#include "FreeRTOS.h" // IWYU pragma: keep
#include "task.h"
/* clang-format on */

bool cp_has_active_kernel_or_cache() {
  if (cp_cache_operation_pending()) {
    return true;
  }

  for (int i = 0; i < kernel_state_buf_size; i++) {
    if (kernel_state_buf[i].in_use) {
      return true;
    }
  }
  return false;
}

static bool host_launch_packet_is_blocked(union Packet *cmd) {
  uint16_t header = cmd->kernel_dispatch_packet.header;
  return header == kKernelDispatchPacketHeader &&
         cp_has_active_kernel_or_cache();
}

void cp_dispatch(void *args) {
  union Packet cmd;
  volatile struct cp_mmio *cp = (volatile struct cp_mmio *)CP_BASE;
  union Packet *cmd_ring = (union Packet *)(cp->CP_CMD_RING_BASE);
  cp_firmware_status_ready();
  while (1) {
    if (cp_reboot_is_requested()) {
      cp_reboot_perform();
    }
    if (cp_firmware_is_faulted()) {
      if (cp_reboot_is_requested()) {
        cp_reboot_perform();
      }
      taskYIELD();
      continue;
    }
    /* Spin until cmd buffer updated */
    DPRINTF("[D]PollCMD\r\n");

    while (cp->CP_RD_PTR == cp->CP_WR_PTR || cp_memory_copy_is_active() ||
           host_launch_packet_is_blocked(&cmd_ring[cp->CP_RD_PTR])) {
      if (cp_reboot_is_requested()) {
        cp_reboot_perform();
      }
      taskYIELD();
    }
    DPRINTF("[D]GetCMD\r\n");

    taskENTER_CRITICAL();
    if (cp_reboot_is_requested()) {
      taskEXIT_CRITICAL();
      cp_reboot_perform();
    }
    union Packet *pkt = &cmd_ring[cp->CP_RD_PTR];
    memcpy(&cmd, pkt, sizeof(union Packet));
    cp->CP_RD_PTR = (cp->CP_RD_PTR + 1) % cp->CP_CMD_RING_SIZE;
    taskEXIT_CRITICAL();

    if (!cp_completion_is_pending(host_token_from_packet(&cmd))) {
      DPRINTF("InvalidCompletionToken\r\n");
      continue;
    }

    switch (cmd.kernel_dispatch_packet.header) {
      case kKernelDispatchPacketHeader:
        /* Obtain kernel_state_buf */
        DPRINTF("[D]KDPReceived\r\n");
        handle_kernel_dispatch_packet(&cmd.kernel_dispatch_packet);
        break;

      case kCacheFlushPacketHeader:
        DPRINTF("[D]CFPReceived\r\n");
        cp_cache_handle_packet(&cmd, kCacheOpFlush);
        break;
      case kCacheInvalidatePacketHeader:
        DPRINTF("[D]CIPReceived\r\n");
        cp_cache_handle_packet(&cmd, kCacheOpInvalidate);
        break;
      case kMemoryCopyPacketHeader:
        DPRINTF("[D]MemoryCopyReceived\r\n");
        cp_memory_copy_submit(&cmd.memory_copy_packet);
        break;
      case kBarrierPacketHeader: {
        /* Wait until every preceding Host command is terminal, then publish.
         * Dispatch is single-threaded: blocking here also stalls followers. */
        const FsaCompletionToken token = host_token_from_packet(&cmd);
        DPRINTF("[D]BarrierReceived token=0x%lx\r\n", (unsigned long)token);
        while (cp_has_active_kernel_or_cache() || cp_memory_copy_is_active()) {
          if (cp_reboot_is_requested()) {
            cp_reboot_perform();
          }
          taskYIELD();
        }
        if (token != 0) {
          cp_publish_completion(token, FSA_COMPLETION_RESULT_SUCCESS);
        }
        break;
      }
      default:
        DPRINTF("UnknownPacketHeader:0x%04x\r\n",
                cmd.kernel_dispatch_packet.header);
        fail_host_token_if_present(&cmd);
        break;
    }

    DPRINTF("[D]DispatchDone(%d)\r\n", cmd.kernel_dispatch_packet.header);
    asm volatile("ebreak");
  }
}
