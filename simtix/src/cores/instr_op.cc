// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include "cores/decode.h"
#include "cores/exec_context.h"
#include "cores/instr_def.h"

namespace simtix {

ExecFlag ADD::Execute(ExecContext *ctx, const Instr &) {
  for (uint32_t i = 0; i < ctx->num_lanes; ++i) {
    if (ctx->tmask[i]) {
      ctx->rd_data[i] = ctx->rs1_data[i] + ctx->rs2_data[i];
    }
  }
  return ExecFlag::RD_DATA;
}

ExecFlag SUB::Execute(ExecContext *ctx, const Instr &) {
  for (uint32_t i = 0; i < ctx->num_lanes; ++i) {
    if (ctx->tmask[i]) {
      ctx->rd_data[i] = ctx->rs1_data[i] - ctx->rs2_data[i];
    }
  }
  return ExecFlag::RD_DATA;
}

ExecFlag SLL::Execute(ExecContext *ctx, const Instr &) {
  for (uint32_t i = 0; i < ctx->num_lanes; ++i) {
    if (ctx->tmask[i]) {
      ctx->rd_data[i] = ctx->rs1_data[i] << (ctx->rs2_data[i] & 0x3f);
    }
  }
  return ExecFlag::RD_DATA;
}

ExecFlag SLT::Execute(ExecContext *ctx, const Instr &) {
  for (uint32_t i = 0; i < ctx->num_lanes; ++i) {
    if (ctx->tmask[i]) {
      ctx->rd_data[i] = (ctx->rs1_data[i] < ctx->rs2_data[i]) ? 1 : 0;
    }
  }
  return ExecFlag::RD_DATA;
}

ExecFlag SLTU::Execute(ExecContext *ctx, const Instr &) {
  for (uint32_t i = 0; i < ctx->num_lanes; ++i) {
    if (ctx->tmask[i]) {
      ctx->rd_data[i] = (static_cast<uint64_t>(ctx->rs1_data[i]) <
                         static_cast<uint64_t>(ctx->rs2_data[i]))
                            ? 1
                            : 0;
    }
  }
  return ExecFlag::RD_DATA;
}

ExecFlag XOR::Execute(ExecContext *ctx, const Instr &) {
  for (uint32_t i = 0; i < ctx->num_lanes; ++i) {
    if (ctx->tmask[i]) {
      ctx->rd_data[i] = ctx->rs1_data[i] ^ ctx->rs2_data[i];
    }
  }
  return ExecFlag::RD_DATA;
}

ExecFlag SRL::Execute(ExecContext *ctx, const Instr &) {
  for (uint32_t i = 0; i < ctx->num_lanes; ++i) {
    if (ctx->tmask[i]) {
      ctx->rd_data[i] =
          static_cast<uint64_t>(ctx->rs1_data[i]) >> (ctx->rs2_data[i] & 0x3f);
    }
  }
  return ExecFlag::RD_DATA;
}

ExecFlag SRA::Execute(ExecContext *ctx, const Instr &) {
  for (uint32_t i = 0; i < ctx->num_lanes; ++i) {
    if (ctx->tmask[i]) {
      ctx->rd_data[i] = ctx->rs1_data[i] >> (ctx->rs2_data[i] & 0x3f);
    }
  }
  return ExecFlag::RD_DATA;
}

ExecFlag OR::Execute(ExecContext *ctx, const Instr &) {
  for (uint32_t i = 0; i < ctx->num_lanes; ++i) {
    if (ctx->tmask[i]) {
      ctx->rd_data[i] = ctx->rs1_data[i] | ctx->rs2_data[i];
    }
  }
  return ExecFlag::RD_DATA;
}

ExecFlag AND::Execute(ExecContext *ctx, const Instr &) {
  for (uint32_t i = 0; i < ctx->num_lanes; ++i) {
    if (ctx->tmask[i]) {
      ctx->rd_data[i] = ctx->rs1_data[i] & ctx->rs2_data[i];
    }
  }
  return ExecFlag::RD_DATA;
}

ExecFlag MUL::Execute(ExecContext *ctx, const Instr &) {
  for (uint32_t i = 0; i < ctx->num_lanes; ++i) {
    if (ctx->tmask[i]) {
      ctx->rd_data[i] = ctx->rs1_data[i] * ctx->rs2_data[i];
    }
  }
  return ExecFlag::RD_DATA | ExecFlag::MDU;
}

ExecFlag MULH::Execute(ExecContext *ctx, const Instr &) {
  for (uint32_t i = 0; i < ctx->num_lanes; ++i) {
    if (ctx->tmask[i]) {
      __int128_t mul_res = static_cast<__int128_t>(ctx->rs1_data[i]) *
                           static_cast<__int128_t>(ctx->rs2_data[i]);
      ctx->rd_data[i] = static_cast<int64_t>(mul_res >> 64);
    }
  }
  return ExecFlag::RD_DATA | ExecFlag::MDU;
}

ExecFlag MULHSU::Execute(ExecContext *ctx, const Instr &) {
  for (uint32_t i = 0; i < ctx->num_lanes; ++i) {
    if (ctx->tmask[i]) {
      uint64_t rs2_unsigned = static_cast<uint64_t>(ctx->rs2_data[i]);
      __int128_t mul_res = static_cast<__int128_t>(ctx->rs1_data[i]) *
                           static_cast<__int128_t>(rs2_unsigned);
      ctx->rd_data[i] = static_cast<int64_t>(mul_res >> 64);
    }
  }
  return ExecFlag::RD_DATA | ExecFlag::MDU;
}

ExecFlag MULHU::Execute(ExecContext *ctx, const Instr &) {
  for (uint32_t i = 0; i < ctx->num_lanes; ++i) {
    if (ctx->tmask[i]) {
      uint64_t rs1_unsigned = static_cast<uint64_t>(ctx->rs1_data[i]);
      uint64_t rs2_unsigned = static_cast<uint64_t>(ctx->rs2_data[i]);
      __uint128_t mul_res = static_cast<__uint128_t>(rs1_unsigned) *
                            static_cast<__uint128_t>(rs2_unsigned);
      ctx->rd_data[i] = static_cast<int64_t>(mul_res >> 64);
    }
  }
  return ExecFlag::RD_DATA | ExecFlag::MDU;
}

ExecFlag DIV::Execute(ExecContext *ctx, const Instr &) {
  for (uint32_t i = 0; i < ctx->num_lanes; ++i) {
    if (ctx->tmask[i]) {
      if (ctx->rs2_data[i] == 0) {
        ctx->rd_data[i] = -1;
      } else if ((ctx->rs1_data[i] == INT64_MIN) && (ctx->rs2_data[i] == -1)) {
        ctx->rd_data[i] = ctx->rs1_data[i];
      } else {
        ctx->rd_data[i] = ctx->rs1_data[i] / ctx->rs2_data[i];
      }
    }
  }
  return ExecFlag::RD_DATA | ExecFlag::MDU;
}

ExecFlag DIVU::Execute(ExecContext *ctx, const Instr &) {
  for (uint32_t i = 0; i < ctx->num_lanes; ++i) {
    if (ctx->tmask[i]) {
      if (ctx->rs2_data[i] == 0) {
        ctx->rd_data[i] = -1;
      } else {
        ctx->rd_data[i] =
            static_cast<int64_t>(static_cast<uint64_t>(ctx->rs1_data[i]) /
                                 static_cast<uint64_t>(ctx->rs2_data[i]));
      }
    }
  }
  return ExecFlag::RD_DATA | ExecFlag::MDU;
}

ExecFlag REM::Execute(ExecContext *ctx, const Instr &) {
  for (uint32_t i = 0; i < ctx->num_lanes; ++i) {
    if (ctx->tmask[i]) {
      if (ctx->rs2_data[i] == 0) {
        ctx->rd_data[i] = ctx->rs1_data[i];
      } else if ((ctx->rs1_data[i] == INT64_MIN) && (ctx->rs2_data[i] == -1)) {
        ctx->rd_data[i] = 0;
      } else {
        ctx->rd_data[i] = ctx->rs1_data[i] % ctx->rs2_data[i];
      }
    }
  }
  return ExecFlag::RD_DATA | ExecFlag::MDU;
}

ExecFlag REMU::Execute(ExecContext *ctx, const Instr &) {
  for (uint32_t i = 0; i < ctx->num_lanes; ++i) {
    if (ctx->tmask[i]) {
      if (ctx->rs2_data[i] == 0) {
        ctx->rd_data[i] = ctx->rs1_data[i];
      } else {
        ctx->rd_data[i] =
            static_cast<int64_t>(static_cast<uint64_t>(ctx->rs1_data[i]) %
                                 static_cast<uint64_t>(ctx->rs2_data[i]));
      }
    }
  }
  return ExecFlag::RD_DATA | ExecFlag::MDU;
}

ExecFlag CZERO_EQZ::Execute(ExecContext *ctx, const Instr &) {
  for (uint32_t i = 0; i < ctx->num_lanes; ++i) {
    if (ctx->tmask[i]) {
      ctx->rd_data[i] = (ctx->rs2_data[i] == 0) ? 0 : ctx->rs1_data[i];
    }
  }
  return ExecFlag::RD_DATA;
}

ExecFlag CZERO_NEZ::Execute(ExecContext *ctx, const Instr &) {
  for (uint32_t i = 0; i < ctx->num_lanes; ++i) {
    if (ctx->tmask[i]) {
      ctx->rd_data[i] = (ctx->rs2_data[i] != 0) ? 0 : ctx->rs1_data[i];
    }
  }
  return ExecFlag::RD_DATA;
}

template <>
class Decoder<InstrOp>;

}  // namespace simtix
