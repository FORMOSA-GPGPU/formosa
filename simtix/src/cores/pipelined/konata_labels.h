/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <fmt/format.h>

#include <string>

#include "cores/encoding.h"
#include "cores/pipelined/packet.h"
#include "cores/pipelined/stats.h"

namespace simtix::pipelined {

inline bool IsBranchDivergence(const Packet &packet, uint32_t num_lanes) {
  const uint8_t opcode = packet.iword & INSN_FIELD_OPCODE;
  if (opcode != Opcode::kBranch && opcode != Opcode::kJalr) {
    return false;
  }

  uint64_t first_target = 0;
  bool seen_active_lane = false;
  for (uint32_t i = 0; i < num_lanes; ++i) {
    if (!packet.tmask[i]) {
      continue;
    }
    if (!seen_active_lane) {
      first_target = packet.addr_buf[i];
      seen_active_lane = true;
      continue;
    }
    if (packet.addr_buf[i] != first_target) {
      return true;
    }
  }
  return false;
}

inline FlushReason ClassifyControlFlowRedirect(const Packet &packet) {
  const uint8_t opcode = packet.iword & INSN_FIELD_OPCODE;
  if (opcode == Opcode::kJal) {
    return packet.instr.imm() < 0 ? FlushReason::kJalBackward
                                  : FlushReason::kJalForward;
  }
  // RISC-V `ret` == jalr x0, 0(x1/ra)
  if (opcode == Opcode::kJalr && packet.instr.rd() == 0 &&
      packet.instr.rs1() == 1 && packet.instr.imm() == 0) {
    return FlushReason::kJalrReturn;
  }
  return FlushReason::kJbOthers;
}

inline FlushReason ClassifyRedirectReason(const Packet &packet,
                                          uint32_t num_lanes) {
  if (IsBranchDivergence(packet, num_lanes)) {
    return FlushReason::kBranchDivergence;
  }
  return ClassifyControlFlowRedirect(packet);
}

inline std::string KonataSpecDiscardLabel(FlushReason reason,
                                          uint64_t wpc = 0) {
  if (reason == FlushReason::kDuplicateInst) {
    return fmt::format("duplicate_inst @ {:#x}", wpc);
  }
  return std::string(FlushCause(reason));
}

inline std::string KonataFlushLabel(FlushReason reason,
                                    uint64_t redirect_wpc = 0) {
  if (redirect_wpc != 0) {
    return fmt::format("{} -> {:#x}", reason, redirect_wpc);
  }
  return fmt::format("{}", reason);
}

}  // namespace simtix::pipelined
