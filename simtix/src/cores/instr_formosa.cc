// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include "cores/decode.h"
#include "cores/exec_context.h"
#include "cores/instr_def.h"

namespace simtix {

ExecFlag FSA_PRI_RESET::Execute(ExecContext *, const Instr &) {
  return ExecFlag::PRIORITY | ExecFlag::PRI_RESET;
}

ExecFlag FSA_PRI_RAISE::Execute(ExecContext *ctx, const Instr &instr) {
  *ctx->pri = instr.pri();
  return ExecFlag::PRIORITY | ExecFlag::PRI_RAISE;
}

ExecFlag FSA_PRI_LOWER::Execute(ExecContext *ctx, const Instr &instr) {
  *ctx->pri = instr.pri();
  return ExecFlag::PRIORITY | ExecFlag::PRI_LOWER;
}

ExecFlag FSA_BAR::Execute(ExecContext *, const Instr &) {
  return ExecFlag::BARRIER;
}

template <>
class Decoder<InstrFormosa>;

}  // namespace simtix
