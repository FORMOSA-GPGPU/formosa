// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include "cores/decode.h"
#include "cores/exec_context.h"
#include "cores/instr_def.h"
#include "cores/softfloat_util.h"

namespace simtix {

/* Generic execution engine for fused multiply-add floating point operations */
template <typename T, bool NegRS1, bool NegRS3, typename Op>
ExecFlag ExecuteFpFused(ExecContext *ctx, const Instr &instr, Op op) {
  using traits = fp_traits<T>;
  for (uint32_t i = 0; i < ctx->num_lanes; ++i) {
    if (ctx->tmask[i]) {
      uint8_t rm = (instr.rm() == 0b111) ? ctx->dynamic_rm[i] : instr.rm();
      softfloat_init(rm);

      auto s1_bits = static_cast<typename traits::bits_t>(ctx->rs1_data[i]);
      auto s2_bits = static_cast<typename traits::bits_t>(ctx->rs2_data[i]);
      auto s3_bits = static_cast<typename traits::bits_t>(ctx->rs3_data[i]);

      if constexpr (NegRS1) s1_bits ^= traits::sign_mask;
      if constexpr (NegRS3) s3_bits ^= traits::sign_mask;

      T res = op({s1_bits}, {s2_bits}, {s3_bits});
      ctx->rd_data[i] = box(res);
      ctx->fflags[i] = softfloat_exceptionFlags;
    }
  }
  return ExecFlag::RD_DATA | ExecFlag::FP_EXC | ExecFlag::FPU;
}

ExecFlag FMADD_S::Execute(ExecContext *ctx, const Instr &instr) {
  return ExecuteFpFused<float32_t, false, false>(ctx, instr, f32_mulAdd);
}

ExecFlag FMADD_D::Execute(ExecContext *ctx, const Instr &instr) {
  return ExecuteFpFused<float64_t, false, false>(ctx, instr, f64_mulAdd);
}

ExecFlag FMSUB_S::Execute(ExecContext *ctx, const Instr &instr) {
  return ExecuteFpFused<float32_t, false, true>(ctx, instr, f32_mulAdd);
}

ExecFlag FMSUB_D::Execute(ExecContext *ctx, const Instr &instr) {
  return ExecuteFpFused<float64_t, false, true>(ctx, instr, f64_mulAdd);
}

ExecFlag FNMSUB_S::Execute(ExecContext *ctx, const Instr &instr) {
  return ExecuteFpFused<float32_t, true, false>(ctx, instr, f32_mulAdd);
}

ExecFlag FNMSUB_D::Execute(ExecContext *ctx, const Instr &instr) {
  return ExecuteFpFused<float64_t, true, false>(ctx, instr, f64_mulAdd);
}

ExecFlag FNMADD_S::Execute(ExecContext *ctx, const Instr &instr) {
  return ExecuteFpFused<float32_t, true, true>(ctx, instr, f32_mulAdd);
}

ExecFlag FNMADD_D::Execute(ExecContext *ctx, const Instr &instr) {
  return ExecuteFpFused<float64_t, true, true>(ctx, instr, f64_mulAdd);
}

template <>
class Decoder<InstrOpFused>;

}  // namespace simtix
