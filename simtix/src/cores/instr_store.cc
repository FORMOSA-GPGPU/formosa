// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include "cores/decode.h"
#include "cores/exec_context.h"
#include "cores/instr_def.h"

namespace simtix {

namespace {

ExecFlag ExecuteStore(ExecContext *ctx, const Instr &instr,
                      ExecFlag width_flag) {
  bool store_addr_misaligned = false;
  uint32_t alignment = GetMemSize(width_flag);

  for (uint32_t i = 0; i < ctx->num_lanes; ++i) {
    if (ctx->tmask[i]) {
      uint64_t addr = ctx->rs1_data[i] + instr.imm();
      ctx->mem.addr[i] = addr;
      ctx->mem.data[i] = ctx->rs2_data[i];
      store_addr_misaligned |= (addr % alignment) != 0;
    }
  }

  if (store_addr_misaligned) {
    // Preserve width_flag so trap handling can identify the faulting address.
    return ExecFlag::STORE_ADDR_MISALIGNED | width_flag;
  }

  return ExecFlag::STORE | width_flag;
}

}  // namespace

ExecFlag SB::Execute(ExecContext *ctx, const Instr &instr) {
  return ExecuteStore(ctx, instr, ExecFlag::WIDTH_1);
}

ExecFlag SH::Execute(ExecContext *ctx, const Instr &instr) {
  return ExecuteStore(ctx, instr, ExecFlag::WIDTH_2);
}

ExecFlag SW::Execute(ExecContext *ctx, const Instr &instr) {
  return ExecuteStore(ctx, instr, ExecFlag::WIDTH_4);
}

ExecFlag SD::Execute(ExecContext *ctx, const Instr &instr) {
  return ExecuteStore(ctx, instr, ExecFlag::WIDTH_8);
}

template <>
class Decoder<InstrStore>;

}  // namespace simtix
