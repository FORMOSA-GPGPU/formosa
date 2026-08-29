// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include "cores/decode.h"
#include "cores/exec_context.h"
#include "cores/instr_def.h"

namespace simtix {

namespace {

ExecFlag ExecuteLoad(ExecContext *ctx, const Instr &instr, ExecFlag width_flag,
                     ExecFlag signed_flag) {
  bool load_addr_misaligned = false;
  uint32_t alignment = GetMemSize(width_flag);

  for (uint32_t i = 0; i < ctx->num_lanes; ++i) {
    if (ctx->tmask[i]) {
      uint64_t addr = ctx->rs1_data[i] + instr.imm();
      ctx->mem.addr[i] = addr;
      load_addr_misaligned |= (addr % alignment) != 0;
    }
  }

  if (load_addr_misaligned) {
    // Preserve width_flag so trap handling can identify the faulting address.
    return ExecFlag::LOAD_ADDR_MISALIGNED | width_flag;
  }

  return ExecFlag::LOAD | width_flag | signed_flag;
}

}  // namespace

ExecFlag LB::Execute(ExecContext *ctx, const Instr &instr) {
  return ExecuteLoad(ctx, instr, ExecFlag::WIDTH_1, ExecFlag::SIGNED);
}

ExecFlag LH::Execute(ExecContext *ctx, const Instr &instr) {
  return ExecuteLoad(ctx, instr, ExecFlag::WIDTH_2, ExecFlag::SIGNED);
}

ExecFlag LW::Execute(ExecContext *ctx, const Instr &instr) {
  return ExecuteLoad(ctx, instr, ExecFlag::WIDTH_4, ExecFlag::SIGNED);
}

ExecFlag LBU::Execute(ExecContext *ctx, const Instr &instr) {
  return ExecuteLoad(ctx, instr, ExecFlag::WIDTH_1, ExecFlag::NONE);
}

ExecFlag LHU::Execute(ExecContext *ctx, const Instr &instr) {
  return ExecuteLoad(ctx, instr, ExecFlag::WIDTH_2, ExecFlag::NONE);
}

ExecFlag LWU::Execute(ExecContext *ctx, const Instr &instr) {
  return ExecuteLoad(ctx, instr, ExecFlag::WIDTH_4, ExecFlag::NONE);
}

ExecFlag LD::Execute(ExecContext *ctx, const Instr &instr) {
  return ExecuteLoad(ctx, instr, ExecFlag::WIDTH_8, ExecFlag::SIGNED);
}

template <>
class Decoder<InstrLoad>;

}  // namespace simtix
