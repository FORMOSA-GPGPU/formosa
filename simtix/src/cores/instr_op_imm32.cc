// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include "cores/decode.h"
#include "cores/exec_context.h"
#include "cores/instr_def.h"

namespace simtix {

ExecFlag ADDIW::Execute(ExecContext *ctx, const Instr &instr) {
  for (uint32_t i = 0; i < ctx->num_lanes; ++i) {
    if (ctx->tmask[i]) {
      ctx->rd_data[i] = static_cast<int32_t>(ctx->rs1_data[i]) +
                        static_cast<int32_t>(instr.imm());
    }
  }
  return ExecFlag::RD_DATA;
}

ExecFlag SLLIW::Execute(ExecContext *ctx, const Instr &instr) {
  for (uint32_t i = 0; i < ctx->num_lanes; ++i) {
    if (ctx->tmask[i]) {
      ctx->rd_data[i] = static_cast<int32_t>(ctx->rs1_data[i])
                        << (instr.imm() & 0x1f);
    }
  }
  return ExecFlag::RD_DATA;
}

ExecFlag SRLIW::Execute(ExecContext *ctx, const Instr &instr) {
  for (uint32_t i = 0; i < ctx->num_lanes; ++i) {
    if (ctx->tmask[i]) {
      ctx->rd_data[i] = static_cast<int32_t>(
          static_cast<uint32_t>(ctx->rs1_data[i]) >> (instr.imm() & 0x1f));
    }
  }
  return ExecFlag::RD_DATA;
}

ExecFlag SRAIW::Execute(ExecContext *ctx, const Instr &instr) {
  for (uint32_t i = 0; i < ctx->num_lanes; ++i) {
    if (ctx->tmask[i]) {
      ctx->rd_data[i] =
          static_cast<int32_t>(ctx->rs1_data[i]) >> (instr.imm() & 0x1f);
    }
  }
  return ExecFlag::RD_DATA;
}

template <>
class Decoder<InstrOpImm32>;

}  // namespace simtix
