// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

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

ExecFlag FSA_BAR::Execute(ExecContext *, const Instr &instr) {
  // Only id 0 is supported: count 0 waits for the work-group, 1 for this warp.
  return instr.imm() <= 1 ? ExecFlag::BARRIER : ExecFlag::ILLEGAL;
}

}  // namespace simtix
