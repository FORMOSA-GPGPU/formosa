// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include "cores/decode.h"
#include "cores/exec_context.h"
#include "cores/instr_def.h"

namespace simtix {

ExecFlag CSRRW::Execute(ExecContext *ctx, const Instr &instr) {
  for (uint32_t i = 0; i < ctx->num_lanes; ++i) {
    ctx->csr.data[i] = ctx->rs1_data[i];
  }
  *ctx->csr.addr = instr.csr();
  return ExecFlag::CSR | ExecFlag::CSRRW;
}

ExecFlag CSRRS::Execute(ExecContext *ctx, const Instr &instr) {
  for (uint32_t i = 0; i < ctx->num_lanes; ++i) {
    ctx->csr.data[i] = ctx->rs1_data[i];
  }
  *ctx->csr.addr = instr.csr();
  return ExecFlag::CSR | ExecFlag::CSRRS;
}

ExecFlag CSRRC::Execute(ExecContext *ctx, const Instr &instr) {
  for (uint32_t i = 0; i < ctx->num_lanes; ++i) {
    ctx->csr.data[i] = ctx->rs1_data[i];
  }
  *ctx->csr.addr = instr.csr();
  return ExecFlag::CSR | ExecFlag::CSRRC;
}

ExecFlag CSRRWI::Execute(ExecContext *ctx, const Instr &instr) {
  for (uint32_t i = 0; i < ctx->num_lanes; ++i) {
    ctx->csr.data[i] = instr.uimm();
  }
  *ctx->csr.addr = instr.csr();
  return ExecFlag::CSR | ExecFlag::CSRRW;
}

ExecFlag CSRRSI::Execute(ExecContext *ctx, const Instr &instr) {
  for (uint32_t i = 0; i < ctx->num_lanes; ++i) {
    ctx->csr.data[i] = instr.uimm();
  }
  *ctx->csr.addr = instr.csr();
  return ExecFlag::CSR | ExecFlag::CSRRS;
}

ExecFlag CSRRCI::Execute(ExecContext *ctx, const Instr &instr) {
  for (uint32_t i = 0; i < ctx->num_lanes; ++i) {
    ctx->csr.data[i] = instr.uimm();
  }
  *ctx->csr.addr = instr.csr();
  return ExecFlag::CSR | ExecFlag::CSRRC;
}

ExecFlag ECALL::Execute(ExecContext *, const Instr &) {
  return ExecFlag::ECALL;
}

template <>
class Decoder<InstrSystem>;

}  // namespace simtix
