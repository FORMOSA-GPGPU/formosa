// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include "cores/decode.h"
#include "cores/exec_context.h"
#include "cores/instr_def.h"

namespace simtix {

ExecFlag AUIPC::Execute(ExecContext *ctx, const Instr &instr) {
  for (uint32_t i = 0; i < ctx->num_lanes; ++i) {
    if (ctx->tmask[i]) {
      ctx->rd_data[i] = ctx->pc + instr.imm();
    }
  }
  return ExecFlag::RD_DATA;
}

template <>
class Decoder<InstrAuipc>;

}  // namespace simtix
