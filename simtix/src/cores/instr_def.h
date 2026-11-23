/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <string_view>
#include <tuple>

#include "cores/encoding.h"
#include "cores/instr_fields.h"

// Each row declares the formatter function, flags and operands. The lists are
// editorial groupings; the decoder derives opcode groups from mask/match.
// Flag names resolve through InstrFlags. Formatters take Instr and mnemonic.
// Keep one instruction per line so the declarative columns remain readable.
// clang-format off
#define FOREACH_INSTR_NTL(V)       \
  V(NTL_P1,   disasm::None, kNone) \
  V(NTL_PALL, disasm::None, kNone) \
  V(NTL_S1,   disasm::None, kNone) \
  V(NTL_ALL,  disasm::None, kNone)

#define FOREACH_INSTR_OP(V)                         \
  V(ADD,       disasm::RegReg, kNone, Rd, Rs1, Rs2) \
  V(SUB,       disasm::RegReg, kNone, Rd, Rs1, Rs2) \
  V(SLL,       disasm::RegReg, kNone, Rd, Rs1, Rs2) \
  V(SLT,       disasm::RegReg, kNone, Rd, Rs1, Rs2) \
  V(SLTU,      disasm::RegReg, kNone, Rd, Rs1, Rs2) \
  V(XOR,       disasm::RegReg, kNone, Rd, Rs1, Rs2) \
  V(SRL,       disasm::RegReg, kNone, Rd, Rs1, Rs2) \
  V(SRA,       disasm::RegReg, kNone, Rd, Rs1, Rs2) \
  V(OR,        disasm::RegReg, kNone, Rd, Rs1, Rs2) \
  V(AND,       disasm::RegReg, kNone, Rd, Rs1, Rs2) \
  V(MUL,       disasm::RegReg, kNone, Rd, Rs1, Rs2) \
  V(MULH,      disasm::RegReg, kNone, Rd, Rs1, Rs2) \
  V(MULHSU,    disasm::RegReg, kNone, Rd, Rs1, Rs2) \
  V(MULHU,     disasm::RegReg, kNone, Rd, Rs1, Rs2) \
  V(DIV,       disasm::RegReg, kNone, Rd, Rs1, Rs2) \
  V(DIVU,      disasm::RegReg, kNone, Rd, Rs1, Rs2) \
  V(REM,       disasm::RegReg, kNone, Rd, Rs1, Rs2) \
  V(REMU,      disasm::RegReg, kNone, Rd, Rs1, Rs2) \
  V(CZERO_EQZ, disasm::RegReg, kNone, Rd, Rs1, Rs2) \
  V(CZERO_NEZ, disasm::RegReg, kNone, Rd, Rs1, Rs2)

#define FOREACH_INSTR_OP_IMM(V)                  \
  V(ADDI,  disasm::RegImm, kNone, Rd, Rs1, ImmI) \
  V(SLTI,  disasm::RegImm, kNone, Rd, Rs1, ImmI) \
  V(SLTIU, disasm::RegImm, kNone, Rd, Rs1, ImmI) \
  V(XORI,  disasm::RegImm, kNone, Rd, Rs1, ImmI) \
  V(ORI,   disasm::RegImm, kNone, Rd, Rs1, ImmI) \
  V(ANDI,  disasm::RegImm, kNone, Rd, Rs1, ImmI) \
  V(SLLI,  disasm::RegImm, kNone, Rd, Rs1, ImmI) \
  V(SRLI,  disasm::RegImm, kNone, Rd, Rs1, ImmI) \
  V(SRAI,  disasm::RegImm, kNone, Rd, Rs1, ImmI)

#define FOREACH_INSTR_OP32(V)                   \
  V(ADDW,  disasm::RegReg, kNone, Rd, Rs1, Rs2) \
  V(SUBW,  disasm::RegReg, kNone, Rd, Rs1, Rs2) \
  V(SLLW,  disasm::RegReg, kNone, Rd, Rs1, Rs2) \
  V(SRLW,  disasm::RegReg, kNone, Rd, Rs1, Rs2) \
  V(SRAW,  disasm::RegReg, kNone, Rd, Rs1, Rs2) \
  V(MULW,  disasm::RegReg, kNone, Rd, Rs1, Rs2) \
  V(DIVW,  disasm::RegReg, kNone, Rd, Rs1, Rs2) \
  V(DIVUW, disasm::RegReg, kNone, Rd, Rs1, Rs2) \
  V(REMW,  disasm::RegReg, kNone, Rd, Rs1, Rs2) \
  V(REMUW, disasm::RegReg, kNone, Rd, Rs1, Rs2)

#define FOREACH_INSTR_OP_IMM32(V)                \
  V(ADDIW, disasm::RegImm, kNone, Rd, Rs1, ImmI) \
  V(SLLIW, disasm::RegImm, kNone, Rd, Rs1, ImmI) \
  V(SRLIW, disasm::RegImm, kNone, Rd, Rs1, ImmI) \
  V(SRAIW, disasm::RegImm, kNone, Rd, Rs1, ImmI)

#define FOREACH_INSTR_BRANCH(V)                    \
  V(BEQ,  disasm::Branch, kBranch, Rs1, Rs2, ImmB) \
  V(BNE,  disasm::Branch, kBranch, Rs1, Rs2, ImmB) \
  V(BLT,  disasm::Branch, kBranch, Rs1, Rs2, ImmB) \
  V(BGE,  disasm::Branch, kBranch, Rs1, Rs2, ImmB) \
  V(BLTU, disasm::Branch, kBranch, Rs1, Rs2, ImmB) \
  V(BGEU, disasm::Branch, kBranch, Rs1, Rs2, ImmB)

#define FOREACH_INSTR_LUI(V) \
  V(LUI, disasm::Imm, kNone, Rd, ImmU)

#define FOREACH_INSTR_AUIPC(V) \
  V(AUIPC, disasm::Imm, kNone, Rd, ImmU)

#define FOREACH_INSTR_JAL(V) \
  V(JAL, disasm::Imm, kJump, Rd, ImmJ)

#define FOREACH_INSTR_JALR(V) \
  V(JALR, disasm::Load, kJump, Rd, Rs1, ImmI)

#define FOREACH_INSTR_LOAD(V)                \
  V(LB,  disasm::Load, kLoad, Rd, Rs1, ImmI) \
  V(LH,  disasm::Load, kLoad, Rd, Rs1, ImmI) \
  V(LW,  disasm::Load, kLoad, Rd, Rs1, ImmI) \
  V(LBU, disasm::Load, kLoad, Rd, Rs1, ImmI) \
  V(LHU, disasm::Load, kLoad, Rd, Rs1, ImmI) \
  V(LWU, disasm::Load, kLoad, Rd, Rs1, ImmI) \
  V(LD,  disasm::Load, kLoad, Rd, Rs1, ImmI)

#define FOREACH_INSTR_AMO(V)                       \
  V(AMOSWAP_W, disasm::RegReg, kAmo, Rd, Rs1, Rs2) \
  V(AMOADD_W,  disasm::RegReg, kAmo, Rd, Rs1, Rs2) \
  V(AMOXOR_W,  disasm::RegReg, kAmo, Rd, Rs1, Rs2) \
  V(AMOAND_W,  disasm::RegReg, kAmo, Rd, Rs1, Rs2) \
  V(AMOOR_W,   disasm::RegReg, kAmo, Rd, Rs1, Rs2) \
  V(AMOMIN_W,  disasm::RegReg, kAmo, Rd, Rs1, Rs2) \
  V(AMOMAX_W,  disasm::RegReg, kAmo, Rd, Rs1, Rs2) \
  V(AMOMINU_W, disasm::RegReg, kAmo, Rd, Rs1, Rs2) \
  V(AMOMAXU_W, disasm::RegReg, kAmo, Rd, Rs1, Rs2) \
  V(AMOSWAP_D, disasm::RegReg, kAmo, Rd, Rs1, Rs2) \
  V(AMOADD_D,  disasm::RegReg, kAmo, Rd, Rs1, Rs2) \
  V(AMOXOR_D,  disasm::RegReg, kAmo, Rd, Rs1, Rs2) \
  V(AMOAND_D,  disasm::RegReg, kAmo, Rd, Rs1, Rs2) \
  V(AMOOR_D,   disasm::RegReg, kAmo, Rd, Rs1, Rs2) \
  V(AMOMIN_D,  disasm::RegReg, kAmo, Rd, Rs1, Rs2) \
  V(AMOMAX_D,  disasm::RegReg, kAmo, Rd, Rs1, Rs2) \
  V(AMOMINU_D, disasm::RegReg, kAmo, Rd, Rs1, Rs2) \
  V(AMOMAXU_D, disasm::RegReg, kAmo, Rd, Rs1, Rs2)

#define FOREACH_INSTR_CAS(V)                                 \
  V(AMOCAS_W, disasm::Atomic, kAmo, Rd, Rs1, Rs2, Rs3FromRd) \
  V(AMOCAS_D, disasm::Atomic, kAmo, Rd, Rs1, Rs2, Rs3FromRd)

#define FOREACH_INSTR_FENCE(V) \
  V(FENCE, disasm::None, kMemoryFence)

#define FOREACH_INSTR_STORE(V)                 \
  V(SB, disasm::Store, kStore, Rs1, Rs2, ImmS) \
  V(SH, disasm::Store, kStore, Rs1, Rs2, ImmS) \
  V(SW, disasm::Store, kStore, Rs1, Rs2, ImmS) \
  V(SD, disasm::Store, kStore, Rs1, Rs2, ImmS)

#define FOREACH_INSTR_SYSTEM(V)                          \
  V(CSRRW,  disasm::CsrReg, kCsr,         Rd, Rs1, Csr)  \
  V(CSRRS,  disasm::CsrReg, kCsr,         Rd, Rs1, Csr)  \
  V(CSRRC,  disasm::CsrReg, kCsr,         Rd, Rs1, Csr)  \
  V(CSRRWI, disasm::CsrImm, kCsr,         Rd, Csr, Uimm) \
  V(CSRRSI, disasm::CsrImm, kCsr,         Rd, Csr, Uimm) \
  V(CSRRCI, disasm::CsrImm, kCsr,         Rd, Csr, Uimm) \
  V(ECALL,  disasm::None,   kTrapRequest)

// Explicitly selected additions to the default SIMT instruction set. EBREAK
// and Zifencei are unprivileged; only MRET and WFI belong to the privileged set.
#define FOREACH_INSTR_PRIVILEGED(V)  \
  V(MRET, disasm::None, kTrapReturn) \
  V(WFI,  disasm::None, kWait)

#define FOREACH_INSTR_BREAKPOINT(V) \
  V(EBREAK, disasm::None, kTrapRequest)

#define FOREACH_INSTR_ZIFENCEI(V) \
  V(FENCE_I, disasm::None, kFetchFence)

#define FOREACH_INSTR_FORMOSA(V)                         \
  V(FSA_PRI_RESET, disasm::None,    kScheduling)         \
  V(FSA_PRI_RAISE, disasm::Pri,     kScheduling, Pri)    \
  V(FSA_PRI_LOWER, disasm::Pri,     kScheduling, Pri)    \
  V(FSA_BAR,       disasm::Barrier, kScheduling, Uimm12)

#define FOREACH_INSTR_OPFP(V)                             \
  V(FADD_S,    disasm::RegRegRm, kNone, Rd, Rs1, Rs2, Rm) \
  V(FADD_D,    disasm::RegRegRm, kNone, Rd, Rs1, Rs2, Rm) \
  V(FSUB_S,    disasm::RegRegRm, kNone, Rd, Rs1, Rs2, Rm) \
  V(FSUB_D,    disasm::RegRegRm, kNone, Rd, Rs1, Rs2, Rm) \
  V(FMUL_S,    disasm::RegRegRm, kNone, Rd, Rs1, Rs2, Rm) \
  V(FMUL_D,    disasm::RegRegRm, kNone, Rd, Rs1, Rs2, Rm) \
  V(FDIV_S,    disasm::RegRegRm, kNone, Rd, Rs1, Rs2, Rm) \
  V(FDIV_D,    disasm::RegRegRm, kNone, Rd, Rs1, Rs2, Rm) \
  V(FSQRT_S,   disasm::RegRm,    kNone, Rd, Rs1, Rm)      \
  V(FSQRT_D,   disasm::RegRm,    kNone, Rd, Rs1, Rm)      \
  V(FSGNJ_S,   disasm::RegReg,   kNone, Rd, Rs1, Rs2)     \
  V(FSGNJN_S,  disasm::RegReg,   kNone, Rd, Rs1, Rs2)     \
  V(FSGNJX_S,  disasm::RegReg,   kNone, Rd, Rs1, Rs2)     \
  V(FSGNJ_D,   disasm::RegReg,   kNone, Rd, Rs1, Rs2)     \
  V(FSGNJN_D,  disasm::RegReg,   kNone, Rd, Rs1, Rs2)     \
  V(FSGNJX_D,  disasm::RegReg,   kNone, Rd, Rs1, Rs2)     \
  V(FMIN_S,    disasm::RegReg,   kNone, Rd, Rs1, Rs2)     \
  V(FMAX_S,    disasm::RegReg,   kNone, Rd, Rs1, Rs2)     \
  V(FMIN_D,    disasm::RegReg,   kNone, Rd, Rs1, Rs2)     \
  V(FMAX_D,    disasm::RegReg,   kNone, Rd, Rs1, Rs2)     \
  V(FCVT_W_S,  disasm::RegRm,    kNone, Rd, Rs1, Rm)      \
  V(FCVT_WU_S, disasm::RegRm,    kNone, Rd, Rs1, Rm)      \
  V(FCVT_L_S,  disasm::RegRm,    kNone, Rd, Rs1, Rm)      \
  V(FCVT_LU_S, disasm::RegRm,    kNone, Rd, Rs1, Rm)      \
  V(FCVT_S_W,  disasm::RegRm,    kNone, Rd, Rs1, Rm)      \
  V(FCVT_S_WU, disasm::RegRm,    kNone, Rd, Rs1, Rm)      \
  V(FCVT_S_L,  disasm::RegRm,    kNone, Rd, Rs1, Rm)      \
  V(FCVT_S_LU, disasm::RegRm,    kNone, Rd, Rs1, Rm)      \
  V(FEQ_S,     disasm::RegReg,   kNone, Rd, Rs1, Rs2)     \
  V(FLT_S,     disasm::RegReg,   kNone, Rd, Rs1, Rs2)     \
  V(FLE_S,     disasm::RegReg,   kNone, Rd, Rs1, Rs2)     \
  V(FEQ_D,     disasm::RegReg,   kNone, Rd, Rs1, Rs2)     \
  V(FLT_D,     disasm::RegReg,   kNone, Rd, Rs1, Rs2)     \
  V(FLE_D,     disasm::RegReg,   kNone, Rd, Rs1, Rs2)     \
  V(FCLASS_S,  disasm::Reg,      kNone, Rd, Rs1)          \
  V(FCLASS_D,  disasm::Reg,      kNone, Rd, Rs1)          \
  V(FCVT_W_D,  disasm::RegRm,    kNone, Rd, Rs1, Rm)      \
  V(FCVT_WU_D, disasm::RegRm,    kNone, Rd, Rs1, Rm)      \
  V(FCVT_L_D,  disasm::RegRm,    kNone, Rd, Rs1, Rm)      \
  V(FCVT_LU_D, disasm::RegRm,    kNone, Rd, Rs1, Rm)      \
  V(FCVT_D_W,  disasm::RegRm<0>, kNone, Rd, Rs1, Rm)      \
  V(FCVT_D_WU, disasm::RegRm<0>, kNone, Rd, Rs1, Rm)      \
  V(FCVT_D_L,  disasm::RegRm,    kNone, Rd, Rs1, Rm)      \
  V(FCVT_D_LU, disasm::RegRm,    kNone, Rd, Rs1, Rm)      \
  V(FCVT_S_D,  disasm::RegRm,    kNone, Rd, Rs1, Rm)      \
  V(FCVT_D_S,  disasm::RegRm<0>, kNone, Rd, Rs1, Rm)

#define FOREACH_INSTR_OPFUSED(V)                                 \
  V(FMADD_S,  disasm::RegRegRegRm, kNone, Rd, Rs1, Rs2, Rs3, Rm) \
  V(FMADD_D,  disasm::RegRegRegRm, kNone, Rd, Rs1, Rs2, Rs3, Rm) \
  V(FMSUB_S,  disasm::RegRegRegRm, kNone, Rd, Rs1, Rs2, Rs3, Rm) \
  V(FMSUB_D,  disasm::RegRegRegRm, kNone, Rd, Rs1, Rs2, Rs3, Rm) \
  V(FNMSUB_S, disasm::RegRegRegRm, kNone, Rd, Rs1, Rs2, Rs3, Rm) \
  V(FNMSUB_D, disasm::RegRegRegRm, kNone, Rd, Rs1, Rs2, Rs3, Rm) \
  V(FNMADD_S, disasm::RegRegRegRm, kNone, Rd, Rs1, Rs2, Rs3, Rm) \
  V(FNMADD_D, disasm::RegRegRegRm, kNone, Rd, Rs1, Rs2, Rs3, Rm)
// clang-format on

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

template <typename M, typename Operands = Fields<>,
          InstrFlags::Bits Flags = InstrFlags::kNone>
struct Def : M {
  using OperandFields = Operands;
  static constexpr InstrFlags::Bits kFlags = Flags;
};

// Set membership is independent of which instruction classes are declared.
#define FOREACH_INSTR_DEFAULT(V) \
  FOREACH_INSTR_NTL(V)           \
  FOREACH_INSTR_OP(V)            \
  FOREACH_INSTR_OP_IMM(V)        \
  FOREACH_INSTR_OP32(V)          \
  FOREACH_INSTR_OP_IMM32(V)      \
  FOREACH_INSTR_BRANCH(V)        \
  FOREACH_INSTR_LUI(V)           \
  FOREACH_INSTR_AUIPC(V)         \
  FOREACH_INSTR_JAL(V)           \
  FOREACH_INSTR_JALR(V)          \
  FOREACH_INSTR_LOAD(V)          \
  FOREACH_INSTR_AMO(V)           \
  FOREACH_INSTR_CAS(V)           \
  FOREACH_INSTR_FENCE(V)         \
  FOREACH_INSTR_STORE(V)         \
  FOREACH_INSTR_SYSTEM(V)        \
  FOREACH_INSTR_FORMOSA(V)       \
  FOREACH_INSTR_OPFP(V)          \
  FOREACH_INSTR_OPFUSED(V)

// All built-in definitions share the same declaration and disassembly path.
// Out-of-line disassembly definitions are generated in instr_disasm.cc.
#define FOREACH_INSTR(V)      \
  FOREACH_INSTR_DEFAULT(V)    \
  FOREACH_INSTR_PRIVILEGED(V) \
  FOREACH_INSTR_BREAKPOINT(V) \
  FOREACH_INSTR_ZIFENCEI(V)

#define DECL_INSTR(NAME, FORMATTER, FLAGS, ...)                              \
  struct NAME : Def<Matches<MASK_##NAME, MATCH_##NAME>, Fields<__VA_ARGS__>, \
                    InstrFlags::FLAGS> {                                     \
    static constexpr detail::MnemonicName kMnemonic{#NAME};                  \
    static ExecFlag Execute(ExecContext *, const Instr &);                   \
    static std::string Disassemble(const Instr &instr);                      \
  };

FOREACH_INSTR(DECL_INSTR)
#undef DECL_INSTR

// Illegal instruction
struct Illegal : Def<Matches<0, 0>> {
  static ExecFlag Execute(ExecContext *, const Instr &) {
    return ExecFlag::ILLEGAL;
  }
  static std::string Disassemble(const Instr &) { return "illegal"; }
};

// Flat instruction sets, generated from the same rows as the classes.
// Preserve declaration order: specific aliases (NTL) precede broad patterns.
// Illegal is supplied by the decoder, not by instruction-set authors.
#define AS_TUPLE(NAME, ...) std::tuple<NAME>{},
using DefaultInstructionSet =
    decltype(std::tuple_cat(FOREACH_INSTR_DEFAULT(AS_TUPLE) std::tuple<>{}));
// This selects decoding, not privilege checks or hart-state execution. Those
// remain the consumer's responsibility; an unhandled instruction is illegal.
using PrivilegedInstructions =
    decltype(std::tuple_cat(FOREACH_INSTR_PRIVILEGED(AS_TUPLE) std::tuple<>{}));
#undef AS_TUPLE

}  // namespace simtix
