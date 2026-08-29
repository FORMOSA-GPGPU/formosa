// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include <climits>

#include "cores/decode.h"
#include "cores/exec_context.h"
#include "cores/instr_def.h"

namespace simtix {

ExecFlag ADDW::Execute(ExecContext *ctx, const Instr &) {
  for (uint32_t i = 0; i < ctx->num_lanes; ++i) {
    if (ctx->tmask[i]) {
      ctx->rd_data[i] = static_cast<int32_t>(ctx->rs1_data[i]) +
                        static_cast<int32_t>(ctx->rs2_data[i]);
    }
  }
  return ExecFlag::RD_DATA;
}

ExecFlag SUBW::Execute(ExecContext *ctx, const Instr &) {
  for (uint32_t i = 0; i < ctx->num_lanes; ++i) {
    if (ctx->tmask[i]) {
      ctx->rd_data[i] = static_cast<int32_t>(ctx->rs1_data[i]) -
                        static_cast<int32_t>(ctx->rs2_data[i]);
    }
  }
  return ExecFlag::RD_DATA;
}

ExecFlag SLLW::Execute(ExecContext *ctx, const Instr &) {
  for (uint32_t i = 0; i < ctx->num_lanes; ++i) {
    if (ctx->tmask[i]) {
      ctx->rd_data[i] = static_cast<int32_t>(ctx->rs1_data[i])
                        << (ctx->rs2_data[i] & 0x1f);
    }
  }
  return ExecFlag::RD_DATA;
}

ExecFlag SRLW::Execute(ExecContext *ctx, const Instr &) {
  for (uint32_t i = 0; i < ctx->num_lanes; ++i) {
    if (ctx->tmask[i]) {
      ctx->rd_data[i] = static_cast<int32_t>(
          static_cast<uint32_t>(ctx->rs1_data[i]) >> (ctx->rs2_data[i] & 0x1f));
    }
  }
  return ExecFlag::RD_DATA;
}

ExecFlag SRAW::Execute(ExecContext *ctx, const Instr &) {
  for (uint32_t i = 0; i < ctx->num_lanes; ++i) {
    if (ctx->tmask[i]) {
      ctx->rd_data[i] =
          static_cast<int32_t>(ctx->rs1_data[i]) >> (ctx->rs2_data[i] & 0x1f);
    }
  }
  return ExecFlag::RD_DATA;
}

ExecFlag MULW::Execute(ExecContext *ctx, const Instr &) {
  for (uint32_t i = 0; i < ctx->num_lanes; ++i) {
    if (ctx->tmask[i]) {
      ctx->rd_data[i] = static_cast<int32_t>(ctx->rs1_data[i]) *
                        static_cast<int32_t>(ctx->rs2_data[i]);
    }
  }
  return ExecFlag::RD_DATA | ExecFlag::MDU;
}

ExecFlag DIVW::Execute(ExecContext *ctx, const Instr &) {
  for (uint32_t i = 0; i < ctx->num_lanes; ++i) {
    if (ctx->tmask[i]) {
      int32_t rs1_data_w = static_cast<int32_t>(ctx->rs1_data[i]);
      int32_t rs2_data_w = static_cast<int32_t>(ctx->rs2_data[i]);
      if (rs2_data_w == 0) {
        ctx->rd_data[i] = -1;
      } else if ((rs1_data_w == INT_MIN) && (rs2_data_w == -1)) {
        ctx->rd_data[i] = rs1_data_w;
      } else {
        ctx->rd_data[i] = rs1_data_w / rs2_data_w;
      }
    }
  }
  return ExecFlag::RD_DATA | ExecFlag::MDU;
}

ExecFlag DIVUW::Execute(ExecContext *ctx, const Instr &) {
  for (uint32_t i = 0; i < ctx->num_lanes; ++i) {
    if (ctx->tmask[i]) {
      uint32_t rs1_data_w = static_cast<uint32_t>(ctx->rs1_data[i]);
      uint32_t rs2_data_w = static_cast<uint32_t>(ctx->rs2_data[i]);
      if (rs2_data_w == 0) {
        ctx->rd_data[i] = -1;
      } else {
        ctx->rd_data[i] = static_cast<int32_t>(rs1_data_w / rs2_data_w);
      }
    }
  }
  return ExecFlag::RD_DATA | ExecFlag::MDU;
}

ExecFlag REMW::Execute(ExecContext *ctx, const Instr &) {
  for (uint32_t i = 0; i < ctx->num_lanes; ++i) {
    if (ctx->tmask[i]) {
      int32_t rs1_data_w = static_cast<int32_t>(ctx->rs1_data[i]);
      int32_t rs2_data_w = static_cast<int32_t>(ctx->rs2_data[i]);
      if (rs2_data_w == 0) {
        ctx->rd_data[i] = rs1_data_w;
      } else if ((rs1_data_w == INT_MIN) && (rs2_data_w == -1)) {
        ctx->rd_data[i] = 0;
      } else {
        ctx->rd_data[i] = rs1_data_w % rs2_data_w;
      }
    }
  }
  return ExecFlag::RD_DATA | ExecFlag::MDU;
}

ExecFlag REMUW::Execute(ExecContext *ctx, const Instr &) {
  for (uint32_t i = 0; i < ctx->num_lanes; ++i) {
    if (ctx->tmask[i]) {
      uint32_t rs1_data_w = static_cast<uint32_t>(ctx->rs1_data[i]);
      uint32_t rs2_data_w = static_cast<uint32_t>(ctx->rs2_data[i]);
      if (rs2_data_w == 0) {
        ctx->rd_data[i] = static_cast<int32_t>(rs1_data_w);
      } else {
        ctx->rd_data[i] = static_cast<int32_t>(rs1_data_w % rs2_data_w);
      }
    }
  }
  return ExecFlag::RD_DATA | ExecFlag::MDU;
}

template <>
class Decoder<InstrOp32>;

}  // namespace simtix
