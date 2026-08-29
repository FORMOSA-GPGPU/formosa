// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include "cores/decode.h"
#include "cores/exec_context.h"
#include "cores/instr_def.h"

namespace simtix {

namespace {

ExecFlag ExecuteAtomicInstr(ExecContext *ctx, const Instr &instr,
                            ExecFlag op_flag, ExecFlag width_flag) {
  for (uint32_t i = 0; i < ctx->num_lanes; ++i) {
    if (ctx->tmask[i]) {
      ctx->mem.addr[i] = ctx->rs1_data[i];
      ctx->mem.data[i] = ctx->rs2_data[i];
    }
  }
  ExecFlag flags = ExecFlag::ATOMIC | op_flag | width_flag;
  // On RV64, all AMO.W variants write the old 32-bit value back to rd with
  // sign-extension, even for unsigned comparison ops such as AMOMINU.W.
  // Signed-vs-unsigned compare semantics are carried by op_flag
  // (AMO_MIN/MAX vs AMO_MINU/MAXU), not by ExecFlag::SIGNED.
  if (width_flag == ExecFlag::WIDTH_4) {
    flags = flags | ExecFlag::SIGNED;
  }
  return flags;
}

}  // namespace

#define DEF_AMO_EXEC(NAME, OP, WIDTH)                                     \
  ExecFlag NAME::Execute(ExecContext *ctx, const Instr &instr) {          \
    return ExecuteAtomicInstr(ctx, instr, ExecFlag::OP, ExecFlag::WIDTH); \
  }

DEF_AMO_EXEC(AMOSWAP_W, AMO_SWAP, WIDTH_4)
DEF_AMO_EXEC(AMOADD_W, AMO_ADD, WIDTH_4)
DEF_AMO_EXEC(AMOXOR_W, AMO_XOR, WIDTH_4)
DEF_AMO_EXEC(AMOAND_W, AMO_AND, WIDTH_4)
DEF_AMO_EXEC(AMOOR_W, AMO_OR, WIDTH_4)
DEF_AMO_EXEC(AMOMIN_W, AMO_MIN, WIDTH_4)
DEF_AMO_EXEC(AMOMAX_W, AMO_MAX, WIDTH_4)
DEF_AMO_EXEC(AMOMINU_W, AMO_MINU, WIDTH_4)
DEF_AMO_EXEC(AMOMAXU_W, AMO_MAXU, WIDTH_4)
DEF_AMO_EXEC(AMOSWAP_D, AMO_SWAP, WIDTH_8)
DEF_AMO_EXEC(AMOADD_D, AMO_ADD, WIDTH_8)
DEF_AMO_EXEC(AMOXOR_D, AMO_XOR, WIDTH_8)
DEF_AMO_EXEC(AMOAND_D, AMO_AND, WIDTH_8)
DEF_AMO_EXEC(AMOOR_D, AMO_OR, WIDTH_8)
DEF_AMO_EXEC(AMOMIN_D, AMO_MIN, WIDTH_8)
DEF_AMO_EXEC(AMOMAX_D, AMO_MAX, WIDTH_8)
DEF_AMO_EXEC(AMOMINU_D, AMO_MINU, WIDTH_8)
DEF_AMO_EXEC(AMOMAXU_D, AMO_MAXU, WIDTH_8)

#undef DEF_AMO_EXEC

template <>
class Decoder<InstrAmo>;

}  // namespace simtix
