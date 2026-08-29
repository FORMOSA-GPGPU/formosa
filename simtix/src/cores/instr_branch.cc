// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include "cores/decode.h"
#include "cores/exec_context.h"
#include "cores/instr_def.h"

namespace simtix {

namespace {

template <typename Predicate>
ExecFlag ExecuteBranch(ExecContext *ctx, const Instr &instr,
                       Predicate is_taken) {
  bool instr_addr_misaligned = false;

  for (uint32_t i = 0; i < ctx->num_lanes; ++i) {
    if (ctx->tmask[i]) {
      bool taken = is_taken(ctx->rs1_data[i], ctx->rs2_data[i]);
      uint64_t next_pc = taken ? ctx->pc + instr.imm() : ctx->pc + 4;

      if (taken) {
        instr_addr_misaligned |= (next_pc % 4) != 0;
      }

      ctx->next_pc[i] = next_pc;
    }
  }

  if (instr_addr_misaligned) {
    return ExecFlag::INSTR_ADDR_MISALIGNED;
  }

  return ExecFlag::NEXT_PC;
}

}  // namespace

ExecFlag BEQ::Execute(ExecContext *ctx, const Instr &instr) {
  return ExecuteBranch(ctx, instr, [](int64_t a, int64_t b) {
    return a == b;
  });
}

ExecFlag BNE::Execute(ExecContext *ctx, const Instr &instr) {
  return ExecuteBranch(ctx, instr, [](int64_t a, int64_t b) {
    return a != b;
  });
}

ExecFlag BLT::Execute(ExecContext *ctx, const Instr &instr) {
  return ExecuteBranch(ctx, instr, [](int64_t a, int64_t b) {
    return a < b;
  });
}

ExecFlag BGE::Execute(ExecContext *ctx, const Instr &instr) {
  return ExecuteBranch(ctx, instr, [](int64_t a, int64_t b) {
    return a >= b;
  });
}

ExecFlag BLTU::Execute(ExecContext *ctx, const Instr &instr) {
  return ExecuteBranch(ctx, instr, [](int64_t a, int64_t b) {
    return static_cast<uint64_t>(a) < static_cast<uint64_t>(b);
  });
}

ExecFlag BGEU::Execute(ExecContext *ctx, const Instr &instr) {
  return ExecuteBranch(ctx, instr, [](int64_t a, int64_t b) {
    return static_cast<uint64_t>(a) >= static_cast<uint64_t>(b);
  });
}

template <>
class Decoder<InstrBranch>;

}  // namespace simtix
