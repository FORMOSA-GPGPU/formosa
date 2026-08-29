/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <tuple>

#include "cores/encoding.h"
#include "cores/instr.h"

#define FOREACH_INSTR_OP(V) \
  V(ADD)                    \
  V(SUB)                    \
  V(SLL)                    \
  V(SLT)                    \
  V(SLTU)                   \
  V(XOR)                    \
  V(SRL)                    \
  V(SRA)                    \
  V(OR)                     \
  V(AND)                    \
  V(MUL)                    \
  V(MULH)                   \
  V(MULHSU)                 \
  V(MULHU)                  \
  V(DIV)                    \
  V(DIVU)                   \
  V(REM)                    \
  V(REMU)                   \
  V(CZERO_EQZ)              \
  V(CZERO_NEZ)

#define FOREACH_INSTR_OP_IMM(V) \
  V(ADDI)                       \
  V(SLTI)                       \
  V(SLTIU)                      \
  V(XORI)                       \
  V(ORI)                        \
  V(ANDI)                       \
  V(SLLI)                       \
  V(SRLI)                       \
  V(SRAI)

#define FOREACH_INSTR_OP32(V) \
  V(ADDW)                     \
  V(SUBW)                     \
  V(SLLW)                     \
  V(SRLW)                     \
  V(SRAW)                     \
  V(MULW)                     \
  V(DIVW)                     \
  V(DIVUW)                    \
  V(REMW)                     \
  V(REMUW)

#define FOREACH_INSTR_OP_IMM32(V) \
  V(ADDIW)                        \
  V(SLLIW)                        \
  V(SRLIW)                        \
  V(SRAIW)

#define FOREACH_INSTR_BRANCH(V) \
  V(BEQ)                        \
  V(BNE)                        \
  V(BLT)                        \
  V(BGE)                        \
  V(BLTU)                       \
  V(BGEU)

#define FOREACH_INSTR_LUI(V) V(LUI)

#define FOREACH_INSTR_AUIPC(V) V(AUIPC)

#define FOREACH_INSTR_JAL(V) V(JAL)

#define FOREACH_INSTR_JALR(V) V(JALR)

#define FOREACH_INSTR_LOAD(V) \
  V(LB)                       \
  V(LH)                       \
  V(LW)                       \
  V(LBU)                      \
  V(LHU)                      \
  V(LWU)                      \
  V(LD)

#define FOREACH_INSTR_AMO(V) \
  V(AMOSWAP_W)               \
  V(AMOADD_W)                \
  V(AMOXOR_W)                \
  V(AMOAND_W)                \
  V(AMOOR_W)                 \
  V(AMOMIN_W)                \
  V(AMOMAX_W)                \
  V(AMOMINU_W)               \
  V(AMOMAXU_W)               \
  V(AMOSWAP_D)               \
  V(AMOADD_D)                \
  V(AMOXOR_D)                \
  V(AMOAND_D)                \
  V(AMOOR_D)                 \
  V(AMOMIN_D)                \
  V(AMOMAX_D)                \
  V(AMOMINU_D)               \
  V(AMOMAXU_D)

#define FOREACH_INSTR_STORE(V) \
  V(SB)                        \
  V(SH)                        \
  V(SW)                        \
  V(SD)

#define FOREACH_INSTR_SYSTEM(V) \
  V(CSRRW)                      \
  V(CSRRS)                      \
  V(CSRRC)                      \
  V(CSRRWI)                     \
  V(CSRRSI)                     \
  V(CSRRCI)                     \
  V(ECALL)

#define FOREACH_INSTR_FORMOSA(V) \
  V(FSA_PRI_RESET)               \
  V(FSA_PRI_RAISE)               \
  V(FSA_PRI_LOWER)               \
  V(FSA_BAR)

#define FOREACH_INSTR_OPFP(V) \
  V(FADD_S)                   \
  V(FADD_D)                   \
  V(FSUB_S)                   \
  V(FSUB_D)                   \
  V(FMUL_S)                   \
  V(FMUL_D)                   \
  V(FDIV_S)                   \
  V(FDIV_D)                   \
  V(FSQRT_S)                  \
  V(FSQRT_D)                  \
  V(FSGNJ_S)                  \
  V(FSGNJN_S)                 \
  V(FSGNJX_S)                 \
  V(FSGNJ_D)                  \
  V(FSGNJN_D)                 \
  V(FSGNJX_D)                 \
  V(FMIN_S)                   \
  V(FMAX_S)                   \
  V(FMIN_D)                   \
  V(FMAX_D)                   \
  V(FCVT_W_S)                 \
  V(FCVT_WU_S)                \
  V(FCVT_L_S)                 \
  V(FCVT_LU_S)                \
  V(FCVT_S_W)                 \
  V(FCVT_S_WU)                \
  V(FCVT_S_L)                 \
  V(FCVT_S_LU)                \
  V(FEQ_S)                    \
  V(FLT_S)                    \
  V(FLE_S)                    \
  V(FEQ_D)                    \
  V(FLT_D)                    \
  V(FLE_D)                    \
  V(FCLASS_S)                 \
  V(FCLASS_D)                 \
  V(FCVT_W_D)                 \
  V(FCVT_WU_D)                \
  V(FCVT_L_D)                 \
  V(FCVT_LU_D)                \
  V(FCVT_D_W)                 \
  V(FCVT_D_WU)                \
  V(FCVT_D_L)                 \
  V(FCVT_D_LU)                \
  V(FCVT_S_D)                 \
  V(FCVT_D_S)

#define FOREACH_INSTR_OPFUSED(V) \
  V(FMADD_S)                     \
  V(FMADD_D)                     \
  V(FMSUB_S)                     \
  V(FMSUB_D)                     \
  V(FNMSUB_S)                    \
  V(FNMSUB_D)                    \
  V(FNMADD_S)                    \
  V(FNMADD_D)

namespace simtix {

namespace detail {
template <std::size_t N>
struct MnemonicName {
  char data[N];
  constexpr MnemonicName(const char (&str)[N]) : data{} {
    for (std::size_t i = 0; i < N - 1; ++i) {
      char c = str[i];
      if (c >= 'A' && c <= 'Z')
        c += ('a' - 'A');
      else if (c == '_')
        c = '.';
      data[i] = c;
    }
    data[N - 1] = '\0';
  }
  constexpr operator std::string_view() const { return {data, N - 1}; }
};
}  // namespace detail

template <uint32_t mask, uint32_t match>
struct Matches {
  static constexpr uint32_t kMaskVal = mask;
  static constexpr uint32_t kMatchVal = match;
  static constexpr bool Match(uint32_t iword) {
    return (iword & mask) == match;
  }
};

template <typename F, typename M>
struct Def : M {
  using Fmt = F;
};

#define DECL_INSTR(NAME, TYPE)                                  \
  struct NAME : Def<TYPE, Matches<MASK_##NAME, MATCH_##NAME>> { \
    static ExecFlag Execute(ExecContext *, const Instr &);      \
    static std::string Mnemonic(const Instr &instr) {           \
      static constexpr detail::MnemonicName kName(#NAME);       \
      return TYPE::Disasm(kName, instr);                        \
    }                                                           \
  };

#define DECL_INSTR_RTYPE(NAME) DECL_INSTR(NAME, RType)
#define DECL_INSTR_ITYPE(NAME) DECL_INSTR(NAME, IType)
#define DECL_INSTR_STYPE(NAME) DECL_INSTR(NAME, SType)
#define DECL_INSTR_BTYPE(NAME) DECL_INSTR(NAME, BType)
#define DECL_INSTR_UTYPE(NAME) DECL_INSTR(NAME, UType)
#define DECL_INSTR_JTYPE(NAME) DECL_INSTR(NAME, JType)
#define DECL_INSTR_SYSTEM(NAME) DECL_INSTR(NAME, SystemType)
#define DECL_INSTR_FORMOSA(NAME) DECL_INSTR(NAME, FormosaType)
#define DECL_INSTR_FPTYPE(NAME) DECL_INSTR(NAME, FPType)
#define DECL_INSTR_R4TYPE(NAME) DECL_INSTR(NAME, R4Type)

FOREACH_INSTR_OP(DECL_INSTR_RTYPE)
FOREACH_INSTR_OP_IMM(DECL_INSTR_ITYPE)
FOREACH_INSTR_OP32(DECL_INSTR_RTYPE)
FOREACH_INSTR_OP_IMM32(DECL_INSTR_ITYPE)
FOREACH_INSTR_BRANCH(DECL_INSTR_BTYPE)
FOREACH_INSTR_LUI(DECL_INSTR_UTYPE)
FOREACH_INSTR_AUIPC(DECL_INSTR_UTYPE)
FOREACH_INSTR_JAL(DECL_INSTR_JTYPE)
FOREACH_INSTR_JALR(DECL_INSTR_ITYPE)
FOREACH_INSTR_LOAD(DECL_INSTR_ITYPE)
FOREACH_INSTR_AMO(DECL_INSTR_RTYPE)
FOREACH_INSTR_STORE(DECL_INSTR_STYPE)
FOREACH_INSTR_SYSTEM(DECL_INSTR_SYSTEM)
FOREACH_INSTR_FORMOSA(DECL_INSTR_FORMOSA)
FOREACH_INSTR_OPFP(DECL_INSTR_FPTYPE)
FOREACH_INSTR_OPFUSED(DECL_INSTR_R4TYPE)

#undef DECL_INSTR
#undef DECL_INSTR_RTYPE
#undef DECL_INSTR_ITYPE
#undef DECL_INSTR_STYPE
#undef DECL_INSTR_BTYPE
#undef DECL_INSTR_UTYPE
#undef DECL_INSTR_JTYPE
#undef DECL_INSTR_SYSTEM
#undef DECL_INSTR_FORMOSA
#undef DECL_INSTR_FPTYPE
#undef DECL_INSTR_R4TYPE

// Illegal instruction
struct Illegal : Def<NoType, Matches<0, 0>> {
  static ExecFlag Execute(ExecContext *, const Instr &) {
    return ExecFlag::ILLEGAL;
  }
  static std::string Mnemonic(const Instr &) { return "illegal"; }
};

#define ADD_TO_TUPLE(NAME) NAME,

using InstrOp = std::tuple<FOREACH_INSTR_OP(ADD_TO_TUPLE) Illegal>;
using InstrOpImm = std::tuple<FOREACH_INSTR_OP_IMM(ADD_TO_TUPLE) Illegal>;
using InstrOp32 = std::tuple<FOREACH_INSTR_OP32(ADD_TO_TUPLE) Illegal>;
using InstrOpImm32 = std::tuple<FOREACH_INSTR_OP_IMM32(ADD_TO_TUPLE) Illegal>;
using InstrBranch = std::tuple<FOREACH_INSTR_BRANCH(ADD_TO_TUPLE) Illegal>;
using InstrLui = std::tuple<FOREACH_INSTR_LUI(ADD_TO_TUPLE) Illegal>;
using InstrAuipc = std::tuple<FOREACH_INSTR_AUIPC(ADD_TO_TUPLE) Illegal>;
using InstrJal = std::tuple<FOREACH_INSTR_JAL(ADD_TO_TUPLE) Illegal>;
using InstrJalr = std::tuple<FOREACH_INSTR_JALR(ADD_TO_TUPLE) Illegal>;
using InstrLoad = std::tuple<FOREACH_INSTR_LOAD(ADD_TO_TUPLE) Illegal>;
using InstrAmo = std::tuple<FOREACH_INSTR_AMO(ADD_TO_TUPLE) Illegal>;
using InstrStore = std::tuple<FOREACH_INSTR_STORE(ADD_TO_TUPLE) Illegal>;
using InstrSystem = std::tuple<FOREACH_INSTR_SYSTEM(ADD_TO_TUPLE) Illegal>;
using InstrFormosa = std::tuple<FOREACH_INSTR_FORMOSA(ADD_TO_TUPLE) Illegal>;
using InstrOpFp = std::tuple<FOREACH_INSTR_OPFP(ADD_TO_TUPLE) Illegal>;
using InstrOpFused = std::tuple<FOREACH_INSTR_OPFUSED(ADD_TO_TUPLE) Illegal>;
using InstrIllegal = std::tuple<Illegal>;

#undef ADD_TO_TUPLE

}  // namespace simtix

#undef FOREACH_INSTR_OP
#undef FOREACH_INSTR_OP_IMM
#undef FOREACH_INSTR_OP32
#undef FOREACH_INSTR_OP_IMM32
#undef FOREACH_INSTR_BRANCH
#undef FOREACH_INSTR_LUI
#undef FOREACH_INSTR_AUIPC
#undef FOREACH_INSTR_JAL
#undef FOREACH_INSTR_JALR
#undef FOREACH_INSTR_LOAD
#undef FOREACH_INSTR_AMO
#undef FOREACH_INSTR_STORE
