// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include "cores/decode.h"
#include "cores/exec_context.h"
#include "cores/instr_def.h"

namespace simtix {

ExecFlag ADDI::Execute(ExecContext *ctx, const Instr &instr) {
  for (uint32_t i = 0; i < ctx->num_lanes; ++i) {
    if (ctx->tmask[i]) {
      ctx->rd_data[i] = ctx->rs1_data[i] + instr.imm();
    }
  }
  return ExecFlag::RD_DATA;
}

ExecFlag SLTI::Execute(ExecContext *ctx, const Instr &instr) {
  for (uint32_t i = 0; i < ctx->num_lanes; ++i) {
    if (ctx->tmask[i]) {
      ctx->rd_data[i] = ctx->rs1_data[i] < instr.imm() ? 1 : 0;
    }
  }
  return ExecFlag::RD_DATA;
}

ExecFlag SLTIU::Execute(ExecContext *ctx, const Instr &instr) {
  for (uint32_t i = 0; i < ctx->num_lanes; ++i) {
    if (ctx->tmask[i]) {
      ctx->rd_data[i] = (static_cast<uint64_t>(ctx->rs1_data[i]) <
                         static_cast<uint64_t>(instr.imm()))
                            ? 1
                            : 0;
    }
  }
  return ExecFlag::RD_DATA;
}

ExecFlag XORI::Execute(ExecContext *ctx, const Instr &instr) {
  for (uint32_t i = 0; i < ctx->num_lanes; ++i) {
    if (ctx->tmask[i]) {
      ctx->rd_data[i] = ctx->rs1_data[i] ^ instr.imm();
    }
  }
  return ExecFlag::RD_DATA;
}

ExecFlag ORI::Execute(ExecContext *ctx, const Instr &instr) {
  for (uint32_t i = 0; i < ctx->num_lanes; ++i) {
    if (ctx->tmask[i]) {
      ctx->rd_data[i] = ctx->rs1_data[i] | instr.imm();
    }
  }
  return ExecFlag::RD_DATA;
}

ExecFlag ANDI::Execute(ExecContext *ctx, const Instr &instr) {
  for (uint32_t i = 0; i < ctx->num_lanes; ++i) {
    if (ctx->tmask[i]) {
      ctx->rd_data[i] = ctx->rs1_data[i] & instr.imm();
    }
  }
  return ExecFlag::RD_DATA;
}

ExecFlag SLLI::Execute(ExecContext *ctx, const Instr &instr) {
  for (uint32_t i = 0; i < ctx->num_lanes; ++i) {
    if (ctx->tmask[i]) {
      ctx->rd_data[i] = ctx->rs1_data[i] << (instr.imm() & 0x3f);
    }
  }
  return ExecFlag::RD_DATA;
}

ExecFlag SRLI::Execute(ExecContext *ctx, const Instr &instr) {
  for (uint32_t i = 0; i < ctx->num_lanes; ++i) {
    if (ctx->tmask[i]) {
      ctx->rd_data[i] =
          static_cast<uint64_t>(ctx->rs1_data[i]) >> (instr.imm() & 0x3f);
    }
  }
  return ExecFlag::RD_DATA;
}

ExecFlag SRAI::Execute(ExecContext *ctx, const Instr &instr) {
  for (uint32_t i = 0; i < ctx->num_lanes; ++i) {
    if (ctx->tmask[i]) {
      ctx->rd_data[i] = ctx->rs1_data[i] >> (instr.imm() & 0x3f);
    }
  }
  return ExecFlag::RD_DATA;
}

template <>
class Decoder<InstrOpImm>;

}  // namespace simtix
