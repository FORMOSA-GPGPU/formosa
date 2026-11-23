// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

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

ExecFlag FENCE::Execute(ExecContext *, const Instr &) {
  return ExecFlag::FENCE;
}

// Shared execution has no hart trap/privilege state or instruction-fetch
// synchronization. Consumers must handle these effects before calling it.
// Reject unhandled instructions instead of silently treating them as no-ops.
// TODO: Define the ExecContext/ExecFlag execution-request contract and
// core-side trap return, wait, breakpoint and fetch synchronization handling
// before enabling these instructions for OS execution.
ExecFlag MRET::Execute(ExecContext *, const Instr &) {
  return ExecFlag::ILLEGAL;
}

ExecFlag WFI::Execute(ExecContext *, const Instr &) {
  return ExecFlag::ILLEGAL;
}

ExecFlag EBREAK::Execute(ExecContext *, const Instr &) {
  return ExecFlag::ILLEGAL;
}

ExecFlag FENCE_I::Execute(ExecContext *, const Instr &) {
  return ExecFlag::ILLEGAL;
}

}  // namespace simtix
