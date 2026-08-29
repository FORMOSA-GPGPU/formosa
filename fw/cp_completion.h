/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef CP_COMPLETION_H
#define CP_COMPLETION_H

#include <formosa_addr_map.h>
#include <stdint.h>

#include "command_packet.h"
#include "cp_status.h"

static inline int cp_completion_is_pending(FsaCompletionToken token) {
  if (!fsa_completion_token_has_valid_slot(token)) return 0;
  const uint16_t slot_index = fsa_completion_token_slot_index(token);
  volatile FsaCompletionSlot *slot =
      (volatile FsaCompletionSlot *)(FSA_COMPLETION_POOL_BASE +
                                     slot_index * FSA_COMPLETION_SLOT_BYTES);
  return fsa_completion_token_boot_generation(token) ==
             (uint16_t)cp_firmware_boot_generation() &&
         fsa_completion_slot_alloc_tag(*slot) ==
             fsa_completion_token_alloc_tag(token) &&
         fsa_completion_slot_result(*slot) == FSA_COMPLETION_RESULT_PENDING;
}

static inline void cp_publish_completion(FsaCompletionToken token,
                                         FsaCompletionResult result) {
  if (!cp_completion_is_pending(token)) return;
  const uint16_t slot_index = fsa_completion_token_slot_index(token);
  volatile FsaCompletionSlot *slot =
      (volatile FsaCompletionSlot *)(FSA_COMPLETION_POOL_BASE +
                                     slot_index * FSA_COMPLETION_SLOT_BYTES);
  asm volatile("fence w, w" ::: "memory");
  *slot =
      fsa_completion_slot_encode(fsa_completion_token_alloc_tag(token), result);
}

#endif /* CP_COMPLETION_H */
