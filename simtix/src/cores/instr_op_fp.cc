// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include <cstring>
#include <type_traits>

#include "cores/decode.h"
#include "cores/exec_context.h"
#include "cores/instr_def.h"
#include "cores/softfloat_util.h"

namespace simtix {

namespace {

/* Generic execution engine for binary floating point operations */
template <typename T, typename Op>
ExecFlag ExecuteFpBinaryOp(ExecContext *ctx, const Instr &instr, Op op) {
  for (uint32_t i = 0; i < ctx->num_lanes; ++i) {
    if (ctx->tmask[i]) {
      uint8_t rm = (instr.rm() == 0b111) ? ctx->dynamic_rm[i] : instr.rm();
      softfloat_init(rm);
      T res = op(unbox<T>(ctx->rs1_data[i]), unbox<T>(ctx->rs2_data[i]));
      ctx->rd_data[i] = box(res);
      ctx->fflags[i] = softfloat_exceptionFlags;
    }
  }
  return ExecFlag::RD_DATA | ExecFlag::FP_EXC | ExecFlag::FPU;
}

/* Generic execution engine for unary floating point operations */
template <typename DestT, typename SrcT = DestT, typename Op>
ExecFlag ExecuteFpUnaryOp(ExecContext *ctx, const Instr &instr, Op op) {
  for (uint32_t i = 0; i < ctx->num_lanes; ++i) {
    if (ctx->tmask[i]) {
      uint8_t rm = (instr.rm() == 0b111) ? ctx->dynamic_rm[i] : instr.rm();
      softfloat_init(rm);
      DestT res = op(unbox<SrcT>(ctx->rs1_data[i]));
      ctx->rd_data[i] = box(res);
      ctx->fflags[i] = softfloat_exceptionFlags;
    }
  }
  return ExecFlag::RD_DATA | ExecFlag::FP_EXC | ExecFlag::FPU;
}

}  // namespace
/* Arithmetic operations */

ExecFlag FADD_S::Execute(ExecContext *ctx, const Instr &instr) {
  return ExecuteFpBinaryOp<float32_t>(ctx, instr, f32_add);
}

ExecFlag FADD_D::Execute(ExecContext *ctx, const Instr &instr) {
  return ExecuteFpBinaryOp<float64_t>(ctx, instr, f64_add);
}

ExecFlag FSUB_S::Execute(ExecContext *ctx, const Instr &instr) {
  return ExecuteFpBinaryOp<float32_t>(ctx, instr, f32_sub);
}

ExecFlag FSUB_D::Execute(ExecContext *ctx, const Instr &instr) {
  return ExecuteFpBinaryOp<float64_t>(ctx, instr, f64_sub);
}

ExecFlag FMUL_S::Execute(ExecContext *ctx, const Instr &instr) {
  return ExecuteFpBinaryOp<float32_t>(ctx, instr, f32_mul);
}

ExecFlag FMUL_D::Execute(ExecContext *ctx, const Instr &instr) {
  return ExecuteFpBinaryOp<float64_t>(ctx, instr, f64_mul);
}

ExecFlag FDIV_S::Execute(ExecContext *ctx, const Instr &instr) {
  return ExecuteFpBinaryOp<float32_t>(ctx, instr, f32_div);
}

ExecFlag FDIV_D::Execute(ExecContext *ctx, const Instr &instr) {
  return ExecuteFpBinaryOp<float64_t>(ctx, instr, f64_div);
}

ExecFlag FSQRT_S::Execute(ExecContext *ctx, const Instr &instr) {
  return ExecuteFpUnaryOp<float32_t>(ctx, instr, f32_sqrt);
}

ExecFlag FSQRT_D::Execute(ExecContext *ctx, const Instr &instr) {
  return ExecuteFpUnaryOp<float64_t>(ctx, instr, f64_sqrt);
}

namespace {

/* Sign-injection operations */
enum class SgnjOp { J, JN, JX };

template <typename T, SgnjOp Op>
ExecFlag ExecuteFsgnj(ExecContext *ctx) {
  using traits = fp_traits<T>;
  for (uint32_t i = 0; i < ctx->num_lanes; ++i) {
    if (ctx->tmask[i]) {
      auto rs1 = static_cast<typename traits::bits_t>(ctx->rs1_data[i]);
      auto rs2 = static_cast<typename traits::bits_t>(ctx->rs2_data[i]);
      typename traits::bits_t res;
      if constexpr (Op == SgnjOp::J) {
        res = (rs2 & traits::sign_mask) | (rs1 & ~traits::sign_mask);
      } else if constexpr (Op == SgnjOp::JN) {
        res = (~rs2 & traits::sign_mask) | (rs1 & ~traits::sign_mask);
      } else {
        res = ((rs1 ^ rs2) & traits::sign_mask) | (rs1 & ~traits::sign_mask);
      }
      ctx->rd_data[i] = static_cast<int64_t>(
          static_cast<typename std::make_signed<typename traits::bits_t>::type>(
              res));
    }
  }
  return ExecFlag::RD_DATA | ExecFlag::FPU;
}

}  // namespace

ExecFlag FSGNJ_S::Execute(ExecContext *ctx, const Instr &) {
  return ExecuteFsgnj<float32_t, SgnjOp::J>(ctx);
}

ExecFlag FSGNJN_S::Execute(ExecContext *ctx, const Instr &) {
  return ExecuteFsgnj<float32_t, SgnjOp::JN>(ctx);
}

ExecFlag FSGNJX_S::Execute(ExecContext *ctx, const Instr &) {
  return ExecuteFsgnj<float32_t, SgnjOp::JX>(ctx);
}

ExecFlag FSGNJ_D::Execute(ExecContext *ctx, const Instr &) {
  return ExecuteFsgnj<float64_t, SgnjOp::J>(ctx);
}

ExecFlag FSGNJN_D::Execute(ExecContext *ctx, const Instr &) {
  return ExecuteFsgnj<float64_t, SgnjOp::JN>(ctx);
}

ExecFlag FSGNJX_D::Execute(ExecContext *ctx, const Instr &) {
  return ExecuteFsgnj<float64_t, SgnjOp::JX>(ctx);
}

namespace {

/* Min/Max operations */
template <typename T, bool IsMax>
ExecFlag ExecuteFminmax(ExecContext *ctx, const Instr &instr) {
  using traits = fp_traits<T>;
  for (uint32_t i = 0; i < ctx->num_lanes; ++i) {
    if (ctx->tmask[i]) {
      auto a_bits = static_cast<typename traits::bits_t>(ctx->rs1_data[i]);
      auto b_bits = static_cast<typename traits::bits_t>(ctx->rs2_data[i]);
      auto fa = unbox<T>(ctx->rs1_data[i]);
      auto fb = unbox<T>(ctx->rs2_data[i]);
      uint8_t rm = (instr.rm() == 0b111) ? ctx->dynamic_rm[i] : instr.rm();
      softfloat_init(rm);

      bool less;
      if constexpr (std::is_same_v<T, float32_t>) {
        less = f32_lt_quiet(fa, fb) ||
               (f32_eq(fa, fb) && (a_bits & traits::sign_mask));
      } else {
        less = f64_lt_quiet(fa, fb) ||
               (f64_eq(fa, fb) && (a_bits & traits::sign_mask));
      }

      /* Signaling NaN sets the invalid operation exception flag */
      if (traits::is_snan(a_bits) || traits::is_snan(b_bits)) {
        softfloat_raiseFlags(softfloat_flag_invalid);
      }

      typename traits::bits_t res_bits;
      if (traits::is_nan(a_bits) && traits::is_nan(b_bits)) {
        res_bits = traits::default_nan;
      } else if (traits::is_nan(a_bits)) {
        res_bits = b_bits;
      } else if (traits::is_nan(b_bits)) {
        res_bits = a_bits;
      } else {
        bool use_a = (IsMax ? !less : less);
        res_bits = use_a ? a_bits : b_bits;
      }
      ctx->rd_data[i] = box(T{res_bits});
      ctx->fflags[i] = softfloat_exceptionFlags;
    }
  }
  return ExecFlag::RD_DATA | ExecFlag::FP_EXC | ExecFlag::FPU;
}

}  // namespace

ExecFlag FMIN_S::Execute(ExecContext *ctx, const Instr &instr) {
  return ExecuteFminmax<float32_t, false>(ctx, instr);
}

ExecFlag FMAX_S::Execute(ExecContext *ctx, const Instr &instr) {
  return ExecuteFminmax<float32_t, true>(ctx, instr);
}

ExecFlag FMIN_D::Execute(ExecContext *ctx, const Instr &instr) {
  return ExecuteFminmax<float64_t, false>(ctx, instr);
}

ExecFlag FMAX_D::Execute(ExecContext *ctx, const Instr &instr) {
  return ExecuteFminmax<float64_t, true>(ctx, instr);
}

/* Comparison operations */
template <typename T, typename Op>
ExecFlag ExecuteFcmp(ExecContext *ctx, Op op) {
  for (uint32_t i = 0; i < ctx->num_lanes; ++i) {
    if (ctx->tmask[i]) {
      softfloat_init(0);
      ctx->rd_data[i] =
          op(unbox<T>(ctx->rs1_data[i]), unbox<T>(ctx->rs2_data[i])) ? 1 : 0;
      ctx->fflags[i] = softfloat_exceptionFlags;
    }
  }
  return ExecFlag::RD_DATA | ExecFlag::FP_EXC | ExecFlag::FPU;
}

ExecFlag FEQ_S::Execute(ExecContext *ctx, const Instr &) {
  return ExecuteFcmp<float32_t>(ctx, f32_eq);
}

ExecFlag FLT_S::Execute(ExecContext *ctx, const Instr &) {
  return ExecuteFcmp<float32_t>(ctx, f32_lt);
}

ExecFlag FLE_S::Execute(ExecContext *ctx, const Instr &) {
  return ExecuteFcmp<float32_t>(ctx, f32_le);
}

ExecFlag FEQ_D::Execute(ExecContext *ctx, const Instr &) {
  return ExecuteFcmp<float64_t>(ctx, f64_eq);
}

ExecFlag FLT_D::Execute(ExecContext *ctx, const Instr &) {
  return ExecuteFcmp<float64_t>(ctx, f64_lt);
}

ExecFlag FLE_D::Execute(ExecContext *ctx, const Instr &) {
  return ExecuteFcmp<float64_t>(ctx, f64_le);
}

/* Conversion operations: Integer to FP */
template <typename T, typename IT, typename Op>
ExecFlag ExecuteFcvtFX(ExecContext *ctx, const Instr &instr, Op op) {
  for (uint32_t i = 0; i < ctx->num_lanes; ++i) {
    if (ctx->tmask[i]) {
      uint8_t rm = (instr.rm() == 0b111) ? ctx->dynamic_rm[i] : instr.rm();
      softfloat_init(rm);
      T res = op(static_cast<IT>(ctx->rs1_data[i]));
      ctx->rd_data[i] = box(res);
      ctx->fflags[i] = softfloat_exceptionFlags;
    }
  }
  return ExecFlag::RD_DATA | ExecFlag::FP_EXC | ExecFlag::FPU;
}

ExecFlag FCVT_S_W::Execute(ExecContext *ctx, const Instr &instr) {
  return ExecuteFcvtFX<float32_t, int32_t>(ctx, instr, i32_to_f32);
}

ExecFlag FCVT_S_WU::Execute(ExecContext *ctx, const Instr &instr) {
  return ExecuteFcvtFX<float32_t, uint32_t>(ctx, instr, ui32_to_f32);
}

ExecFlag FCVT_S_L::Execute(ExecContext *ctx, const Instr &instr) {
  return ExecuteFcvtFX<float32_t, int64_t>(ctx, instr, i64_to_f32);
}

ExecFlag FCVT_S_LU::Execute(ExecContext *ctx, const Instr &instr) {
  return ExecuteFcvtFX<float32_t, uint64_t>(ctx, instr, ui64_to_f32);
}

ExecFlag FCVT_D_W::Execute(ExecContext *ctx, const Instr &instr) {
  return ExecuteFcvtFX<float64_t, int32_t>(ctx, instr, i32_to_f64);
}

ExecFlag FCVT_D_WU::Execute(ExecContext *ctx, const Instr &instr) {
  return ExecuteFcvtFX<float64_t, uint32_t>(ctx, instr, ui32_to_f64);
}

ExecFlag FCVT_D_L::Execute(ExecContext *ctx, const Instr &instr) {
  return ExecuteFcvtFX<float64_t, int64_t>(ctx, instr, i64_to_f64);
}

ExecFlag FCVT_D_LU::Execute(ExecContext *ctx, const Instr &instr) {
  return ExecuteFcvtFX<float64_t, uint64_t>(ctx, instr, ui64_to_f64);
}

/* Conversion operations: FP to Integer */
inline int64_t SignExtendWord(uint32_t bits) {
  if ((bits & (UINT32_C(1) << 31)) != 0) {
    return static_cast<int64_t>(bits) - (INT64_C(1) << 32);
  }
  return static_cast<int64_t>(bits);
}

template <typename T>
int64_t extend_int_result(T val) {
  if constexpr (sizeof(T) == sizeof(uint32_t)) {
    return SignExtendWord(static_cast<uint32_t>(val));
  } else {
    return static_cast<int64_t>(val);
  }
}

template <typename SrcT, typename DestT, typename Op>
ExecFlag ExecuteFcvtXF(ExecContext *ctx, const Instr &instr, Op op) {
  for (uint32_t i = 0; i < ctx->num_lanes; ++i) {
    if (ctx->tmask[i]) {
      uint8_t rm = (instr.rm() == 0b111) ? ctx->dynamic_rm[i] : instr.rm();
      softfloat_init(rm);
      DestT res =
          static_cast<DestT>(op(unbox<SrcT>(ctx->rs1_data[i]), rm, true));
      ctx->rd_data[i] = extend_int_result(res);
      ctx->fflags[i] = softfloat_exceptionFlags;
    }
  }
  return ExecFlag::RD_DATA | ExecFlag::FP_EXC | ExecFlag::FPU;
}

ExecFlag FCVT_W_S::Execute(ExecContext *ctx, const Instr &instr) {
  return ExecuteFcvtXF<float32_t, int32_t>(ctx, instr, f32_to_i32);
}

ExecFlag FCVT_WU_S::Execute(ExecContext *ctx, const Instr &instr) {
  return ExecuteFcvtXF<float32_t, uint32_t>(ctx, instr, f32_to_ui32);
}

ExecFlag FCVT_L_S::Execute(ExecContext *ctx, const Instr &instr) {
  return ExecuteFcvtXF<float32_t, int64_t>(ctx, instr, f32_to_i64);
}

ExecFlag FCVT_LU_S::Execute(ExecContext *ctx, const Instr &instr) {
  return ExecuteFcvtXF<float32_t, uint64_t>(ctx, instr, f32_to_ui64);
}

ExecFlag FCVT_W_D::Execute(ExecContext *ctx, const Instr &instr) {
  return ExecuteFcvtXF<float64_t, int32_t>(ctx, instr, f64_to_i32);
}

ExecFlag FCVT_WU_D::Execute(ExecContext *ctx, const Instr &instr) {
  return ExecuteFcvtXF<float64_t, uint32_t>(ctx, instr, f64_to_ui32);
}

ExecFlag FCVT_L_D::Execute(ExecContext *ctx, const Instr &instr) {
  return ExecuteFcvtXF<float64_t, int64_t>(ctx, instr, f64_to_i64);
}

ExecFlag FCVT_LU_D::Execute(ExecContext *ctx, const Instr &instr) {
  return ExecuteFcvtXF<float64_t, uint64_t>(ctx, instr, f64_to_ui64);
}

/* Conversion operations: FP to FP */

ExecFlag FCVT_S_D::Execute(ExecContext *ctx, const Instr &instr) {
  return ExecuteFpUnaryOp<float32_t, float64_t>(ctx, instr, f64_to_f32);
}

ExecFlag FCVT_D_S::Execute(ExecContext *ctx, const Instr &instr) {
  return ExecuteFpUnaryOp<float64_t, float32_t>(ctx, instr, f32_to_f64);
}

/* FCLASS operations */
template <typename T>
ExecFlag ExecuteFclass(ExecContext *ctx) {
  using traits = fp_traits<T>;
  for (uint32_t i = 0; i < ctx->num_lanes; ++i) {
    if (ctx->tmask[i]) {
      auto a = static_cast<typename traits::bits_t>(ctx->rs1_data[i]);
      bool inf_or_nan =
          (traits::exp(a) == (std::is_same_v<T, float32_t> ? 0xFF : 0x7FF));
      bool subnorm_or_zero = (traits::exp(a) == 0);
      bool sign = traits::sign(a);
      bool frac_zero = (traits::frac(a) == 0);
      bool is_nan = traits::is_nan(a);
      bool is_snan = traits::is_snan(a);

      uint32_t res = (sign && inf_or_nan && frac_zero) << 0 |
                     (sign && !inf_or_nan && !subnorm_or_zero) << 1 |
                     (sign && subnorm_or_zero && !frac_zero) << 2 |
                     (sign && subnorm_or_zero && frac_zero) << 3 |
                     (!sign && inf_or_nan && frac_zero) << 7 |
                     (!sign && !inf_or_nan && !subnorm_or_zero) << 6 |
                     (!sign && subnorm_or_zero && !frac_zero) << 5 |
                     (!sign && subnorm_or_zero && frac_zero) << 4 |
                     (is_nan && is_snan) << 8 | (is_nan && !is_snan) << 9;
      ctx->rd_data[i] = res;
    }
  }
  return ExecFlag::RD_DATA | ExecFlag::FPU;
}

ExecFlag FCLASS_S::Execute(ExecContext *ctx, const Instr &) {
  return ExecuteFclass<float32_t>(ctx);
}

ExecFlag FCLASS_D::Execute(ExecContext *ctx, const Instr &) {
  return ExecuteFclass<float64_t>(ctx);
}

template <>
class Decoder<InstrOpFp>;

}  // namespace simtix
