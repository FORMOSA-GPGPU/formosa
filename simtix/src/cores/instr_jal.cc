// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include "cores/decode.h"
#include "cores/exec_context.h"
#include "cores/instr_def.h"

namespace simtix {

ExecFlag JAL::Execute(ExecContext *ctx, const Instr &instr) {
  bool instr_addr_misaligned = false;

  for (uint32_t i = 0; i < ctx->num_lanes; ++i) {
    if (ctx->tmask[i]) {
      uint64_t next_pc = ctx->pc + instr.imm();

      instr_addr_misaligned |= (next_pc % 4);

      ctx->rd_data[i] = ctx->pc + 4;
      ctx->next_pc[i] = next_pc;
    }
  }

  if (instr_addr_misaligned) {
    return ExecFlag::INSTR_ADDR_MISALIGNED;
  }

  return ExecFlag::RD_DATA | ExecFlag::NEXT_PC;
}

template <>
class Decoder<InstrJal>;

}  // namespace simtix
