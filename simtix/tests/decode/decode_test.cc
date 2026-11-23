// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
// SPDX-License-Identifier: Apache-2.0

#include "cores/decode.h"

#include <string>
#include <type_traits>

#include "catch2/catch_session.hpp"
#include "catch2/catch_test_macros.hpp"
#include "cores/instr_disasm.h"

namespace {

using namespace simtix;

struct CustomInstruction : Def<Matches<0xffffffff, 0x2b>> {
  static constexpr InstrFlags::Bits kFlags =
      InstrFlags::kCsr | InstrFlags::kScheduling;
  static ExecFlag Execute(ExecContext *, const Instr &) {
    return ExecFlag::NONE;
  }
  static std::string Disassemble(const Instr &) { return "custom"; }
};

// Keep LD's semantics while giving the definition a distinct identity. It is
// intentionally absent from the default set: flags belong to the definition.
struct LoadAlias : LD {
  static ExecFlag Execute(ExecContext *ctx, const Instr &instr) {
    return LD::Execute(ctx, instr);
  }
};

using TestInstructionSet =
    InstructionSet<std::tuple<ADD, LD, CSRRS, ECALL, CustomInstruction>,
                   PrivilegedInstructions, std::tuple<EBREAK, FENCE, FENCE_I>>;

// Neither opcode was present in the handwritten dispatch. A wildcard in bit 6
// must route this definition to both buckets without accepting other encodings.
struct WildOpcode : Def<Matches<0xffffffbf, 0x0b>> {
  static ExecFlag Execute(ExecContext *, const Instr &) {
    return ExecFlag::NONE;
  }
  static std::string Disassemble(const Instr &) { return "wild"; }
};

// A precise ADD alias has no operands despite overlapping ADD.
struct Hint : Def<Matches<MASK_NTL_P1, MATCH_NTL_P1>, Fields<>> {
  static ExecFlag Execute(ExecContext *, const Instr &) {
    return ExecFlag::NONE;
  }
  static std::string Disassemble(const Instr &) { return "hint"; }
};

template <unsigned N>
struct Many : Def<Matches<0xffffffff, 0x2b | (N << 7)>> {
  static ExecFlag Execute(ExecContext *, const Instr &) {
    return ExecFlag::NONE;
  }
  static std::string Disassemble(const Instr &) {
    return "many" + std::to_string(N);
  }
};
struct Broad : Def<Matches<0x7f, 0x2b>, Fields<Rd>> {
  static ExecFlag Execute(ExecContext *, const Instr &) {
    return ExecFlag::NONE;
  }
  static std::string Disassemble(const Instr &) { return "broad"; }
};
template <size_t... I>
auto ManyInstructions(std::index_sequence<I...>)
    -> std::tuple<Many<I>..., Broad>;

// Distinct identities with inseparable encodings must retain first-match order
// even when their count exceeds the preferred bitmap leaf size.
template <unsigned N>
struct SameEncoding : CustomInstruction {
  static ExecFlag Execute(ExecContext *, const Instr &) {
    return ExecFlag::NONE;
  }
};

TEST_CASE("Disassembly formatters print instruction operands") {
  struct Case {
    uint32_t word;
    const char *text;
  };
  for (const auto [word, text] :
       {Case{MATCH_ADD | (10 << 7) | (11 << 15) | (12 << 20), "add a0, a1, a2"},
        Case{MATCH_ADDI | (10 << 7) | (11 << 15) | 0xff800000,
             "addi a0, a1, -8"},
        Case{MATCH_LD | (10 << 7) | (11 << 15) | (8 << 20), "ld a0, 8(a1)"},
        Case{MATCH_SD | (10 << 20) | (11 << 15) | (8 << 7), "sd a0, 8(a1)"},
        Case{MATCH_BEQ | (10 << 15) | (11 << 20) | (4 << 8), "beq a0, a1, 8"},
        Case{MATCH_LUI | (10 << 7) | 0x1000, "lui a0, 4096"},
        Case{MATCH_JAL | (1 << 7) | (8 << 20), "jal ra, 8"},
        Case{MATCH_JALR | (1 << 7) | (10 << 15), "jalr ra, 0(a0)"},
        Case{MATCH_CSRRS | (10 << 7) | (11 << 15) | (0xabcu << 20),
             "csrrs a0, 0xabc, a1"},
        Case{MATCH_CSRRSI | (10 << 7) | (17 << 15) | (1 << 20),
             "csrrsi a0, 0x001, 17"},
        Case{MATCH_FCLASS_S | (10 << 7) | (11 << 15), "fclass.s a0, a1"},
        Case{MATCH_ECALL, "ecall"}, Case{MATCH_FSA_BAR, "fsa.bar"},
        Case{MATCH_FSA_PRI_RAISE | (7 << 20), "fsa.pri.raise 7"},
        Case{MATCH_FMADD_S | (10 << 7) | (11 << 15) | (12 << 20) | (13 << 27),
             "fmadd.s a0, a1, a2, a3, rne"}}) {
    CAPTURE(word);
    REQUIRE(Disassemble(Decode(word)) == text);
  }
}

TEST_CASE("Floating-point disassembly preserves encoded rounding modes") {
  // Named expected strings also cover reserved encodings rather than hiding
  // them.
  const char *suffixes[] = {", rne", ", rtz", ", rdn", ", rup",
                            ", rmm", ", 5",   ", 6",   ", dyn"};
  struct Case {
    uint32_t word;
    const char *text;
    unsigned default_rm;
  };
  for (const auto &c : {
           Case{MATCH_FADD_S, "fadd.s a0, a1, a2", 7},
           Case{MATCH_FDIV_D, "fdiv.d a0, a1, a2", 7},
           Case{MATCH_FSQRT_S, "fsqrt.s a0, a1", 7},
           Case{MATCH_FCVT_W_S, "fcvt.w.s a0, a1", 7},
           Case{MATCH_FCVT_S_D, "fcvt.s.d a0, a1", 7},
           Case{MATCH_FCVT_D_L, "fcvt.d.l a0, a1", 7},
           Case{MATCH_FCVT_D_S, "fcvt.d.s a0, a1", 0},
           Case{MATCH_FCVT_D_W, "fcvt.d.w a0, a1", 0},
           Case{MATCH_FCVT_D_WU, "fcvt.d.wu a0, a1", 0},
           Case{MATCH_FMADD_S, "fmadd.s a0, a1, a2, a3", 7},
           Case{MATCH_FNMADD_D, "fnmadd.d a0, a1, a2, a3", 7},
       }) {
    for (unsigned rm = 0; rm < 8; ++rm) {
      CAPTURE(c.text, rm);
      uint32_t word = c.word | (10 << 7) | (11 << 15) | (rm << 12);
      if (c.word == MATCH_FADD_S || c.word == MATCH_FDIV_D ||
          c.word == MATCH_FMADD_S || c.word == MATCH_FNMADD_D)
        word |= 12 << 20;
      if (c.word == MATCH_FMADD_S || c.word == MATCH_FNMADD_D) word |= 13 << 27;
      const auto instr = Decode(word);
      REQUIRE(instr.rm() == rm);
      REQUIRE(Disassemble(instr) ==
              std::string(c.text) + (rm == c.default_rm ? "" : suffixes[rm]));
    }
  }
  // funct3 is an opcode discriminator here, not a rounding-mode operand.
  REQUIRE(Disassemble(Decode(MATCH_FMIN_S)) == "fmin.s zero, zero, zero");
  REQUIRE(Disassemble(Decode(MATCH_FEQ_S)) == "feq.s zero, zero, zero");
}

TEST_CASE("NTL hints precede ADD and have no register dependencies or writes") {
  REQUIRE(Decode(MATCH_NTL_P1).is<NTL_P1>());
  REQUIRE(Decode(MATCH_NTL_PALL).is<NTL_PALL>());
  REQUIRE(Decode(MATCH_NTL_S1).is<NTL_S1>());
  REQUIRE(Decode(MATCH_NTL_ALL).is<NTL_ALL>());
  struct Case {
    uint32_t word;
    const char *text;
  };
  for (const auto [word, text] :
       {Case{MATCH_NTL_P1, "ntl.p1"}, Case{MATCH_NTL_PALL, "ntl.pall"},
        Case{MATCH_NTL_S1, "ntl.s1"}, Case{MATCH_NTL_ALL, "ntl.all"}}) {
    const auto instr = Decode(word);
    REQUIRE(Disassemble(instr) == text);
    REQUIRE(instr.rd() == Instr::kNullReg);
    REQUIRE(instr.rs1() == Instr::kNullReg);
    REQUIRE(instr.rs2() == Instr::kNullReg);
    REQUIRE(instr.rs3() == Instr::kNullReg);
    REQUIRE_FALSE(instr.is_cti());
    REQUIRE_FALSE(instr.affects_scheduling());
    REQUIRE_FALSE(instr.is_memory_access());
    REQUIRE(Execute(nullptr, instr) == ExecFlag::NONE);
    REQUIRE(Decode(word | (1 << 7)).is<ADD>());
    REQUIRE(Decode(word | (1 << 15)).is<ADD>());
  }
}

TEST_CASE(
    "Operand declarations share canonical layouts and preserve empty aliases") {
  STATIC_REQUIRE(std::is_same_v<Fields<Rd, Rs1, Rs2>, Fields<Rs2, Rd, Rs1>>);
  STATIC_REQUIRE(std::is_same_v<ADD::OperandFields, SUB::OperandFields>);
  STATIC_REQUIRE(detail::DisjointFields<Csr, Uimm>::value);
  STATIC_REQUIRE_FALSE(detail::DisjointFields<ImmI, Rm>::value);
  using Aliases = std::tuple<Hint, ADD>;
  const auto hint = Decode<Aliases>(MATCH_NTL_P1);
  REQUIRE(hint.is<Hint>());
  REQUIRE(hint.rd() == Instr::kNullReg);
  REQUIRE(hint.rs1() == Instr::kNullReg);
  REQUIRE(hint.rs2() == Instr::kNullReg);
  const auto add = Decode<Aliases>(MATCH_NTL_P1 | (1 << 7));
  REQUIRE(add.is<ADD>());
  REQUIRE(add.rd() == 1);
  REQUIRE(add.rs1() == 0);
  REQUIRE(add.rs2() == 2);
}

TEST_CASE("Large instruction groups partition into bounded bitmap leaves") {
  using Group = decltype(ManyInstructions(std::make_index_sequence<130>{}));
  REQUIRE(Decode<Group>(0x2b).is<Many<0>>());
  REQUIRE(Decode<Group>(0x2b | (63 << 7)).is<Many<63>>());
  REQUIRE(Decode<Group>(0x2b | (64 << 7)).is<Many<64>>());
  REQUIRE(Decode<Group>(0x2b | (129 << 7)).is<Many<129>>());
  for (uint32_t n = 0; n < 130; ++n) {
    CAPTURE(n);
    const auto word = 0x2b | (n << 7);
    REQUIRE(Disassemble(Decode<Group>(word)) == "many" + std::to_string(n));
    // Only tested bits may be omitted: untested fixed bits still distinguish
    // the precise instruction from the wildcard after several partitions.
    REQUIRE(Decode<Group>(word | 0x80000000).is<Broad>());
  }
  const auto broad = Decode<Group>(0x2b | (130 << 7));
  REQUIRE(broad.is<Broad>());
  REQUIRE(broad.rd() == 2);
  REQUIRE(Decode<Group>(0xffffffff).is<Illegal>());
}

TEST_CASE("Inseparable encodings preserve priority beyond the leaf target") {
  using Set = std::tuple<SameEncoding<0>, SameEncoding<1>, SameEncoding<2>,
                         SameEncoding<3>, SameEncoding<4>, SameEncoding<5>>;
  REQUIRE(Decode<Set>(0x2b).is<SameEncoding<0>>());
  REQUIRE(Decode<Set>(0x102b).is<Illegal>());
}

TEST_CASE("Flat sets deduplicate types and retain alias precedence") {
  using Set = InstructionSet<std::tuple<Hint, ADD>, std::tuple<ADD, LD, Hint>>;
  STATIC_REQUIRE(std::is_same_v<Set, std::tuple<Hint, ADD, LD>>);
  REQUIRE(Decode<Set>(MATCH_NTL_P1).is<Hint>());
  REQUIRE(Decode<Set>(MATCH_NTL_P1 | (1 << 7)).is<ADD>());
  REQUIRE(Decode<Set>(MATCH_LD).is<LD>());

  using Extended = InstructionSet<DefaultInstructionSet, PrivilegedInstructions,
                                  std::tuple<EBREAK, FENCE_I>>;
  REQUIRE(Decode<Extended>(MATCH_MRET).is<MRET>());
  REQUIRE(Decode<Extended>(MATCH_FENCE_I).is<FENCE_I>());
  REQUIRE(Decode<Extended>(MATCH_FADD_S).is<FADD_S>());
  REQUIRE(Decode<Extended>(MATCH_NTL_P1).is<NTL_P1>());
}

TEST_CASE("Opcode filtering includes wildcards and supplies empty fallbacks") {
  using Set = std::tuple<WildOpcode>;
  for (uint32_t opcode = 0; opcode < 128; ++opcode) {
    CAPTURE(opcode);
    const auto instr = Decode<Set>(opcode);
    REQUIRE(instr.is<WildOpcode>() == (opcode == 0x0b || opcode == 0x4b));
    REQUIRE(instr.is<Illegal>() == (opcode != 0x0b && opcode != 0x4b));
    // Routing by opcode cannot discard the other fixed encoding bits.
    REQUIRE(Decode<Set>(opcode | (1 << 20)).is<Illegal>());
    REQUIRE(Decode<std::tuple<>>(opcode).is<Illegal>());
  }
}

TEST_CASE("Fields describe actual FP and CSR operands") {
  const auto convert =
      Decode(MATCH_FCVT_L_S | (3 << 7) | (5 << 15) | (7 << 12));
  REQUIRE(convert.is<FCVT_L_S>());
  REQUIRE(convert.rd() == 3);
  REQUIRE(convert.rs1() == 5);
  REQUIRE(convert.rs2() == Instr::kNullReg);
  REQUIRE(convert.rm() == 7);
  REQUIRE(Disassemble(convert) == "fcvt.l.s gp, t0");
  const auto compare = Decode(MATCH_FLT_S | (3 << 7) | (5 << 15) | (6 << 20));
  REQUIRE(compare.is<FLT_S>());
  REQUIRE(compare.rs2() == 6);
  REQUIRE(compare.rm() == 0);
  const auto csr =
      Decode(MATCH_CSRRSI | (0xabcu << 20) | (17 << 15) | (3 << 7));
  REQUIRE(csr.csr() == 0xabc);
  REQUIRE(csr.uimm() == 17);
  REQUIRE(csr.rd() == 3);
  REQUIRE(csr.rs1() == Instr::kNullReg);
}

TEST_CASE(
    "Compare-and-swap reads the original destination as its third source") {
  for (uint32_t word : {MATCH_AMOCAS_W, MATCH_AMOCAS_D}) {
    const auto instr = Decode(word | (10 << 7) | (11 << 15) | (12 << 20));
    REQUIRE(instr.in<std::tuple<AMOCAS_W, AMOCAS_D>>());
    REQUIRE(instr.rd() == 10);
    REQUIRE(instr.rs1() == 11);
    REQUIRE(instr.rs2() == 12);
    REQUIRE(instr.rs3() == 10);
    REQUIRE(instr.is_amo());
    REQUIRE(instr.is_memory_access());
    REQUIRE(Disassemble(instr) == (word == MATCH_AMOCAS_W
                                       ? "amocas.w a0, a2, (a1)"
                                       : "amocas.d a0, a2, (a1)"));
  }
  REQUIRE(Decode(MATCH_AMOADD_W).rs3() == Instr::kNullReg);
}

TEST_CASE("Immediate recipes preserve signed endpoints and split bits") {
  REQUIRE(Decode(MATCH_ADDI | 0x80000000).imm() == -2048);
  REQUIRE(Decode(MATCH_SW | 0x80000000).imm() == -2048);
  REQUIRE(Decode(MATCH_SW | 0x7e000f80).imm() == 2047);
  REQUIRE(Decode(MATCH_BEQ | 0x80000000).imm() == -4096);
  REQUIRE(Decode(MATCH_BEQ | 0x7e000f80).imm() == 4094);
  REQUIRE(Decode(MATCH_JAL | 0x80000000).imm() == -1048576);
  REQUIRE(Decode(MATCH_JAL | 0x7ffff000).imm() == 1048574);
  REQUIRE(Decode(MATCH_LUI | 0x80000000).imm() == -2147483648LL);
}

TEST_CASE("Default decoding preserves the shared instruction catalog") {
  REQUIRE(Decode(MATCH_ADD).is<ADD>());
  REQUIRE(Decode(MATCH_AMOADD_W).is<AMOADD_W>());
  const auto fence = Decode(MATCH_FENCE);
  REQUIRE(fence.is<FENCE>());
  REQUIRE_FALSE(fence.is_memory_access());
  REQUIRE(fence.is_memory_fence());
  REQUIRE_FALSE(fence.is_cti());
  REQUIRE(Disassemble(fence) == "fence");
  REQUIRE(Execute(nullptr, fence) == ExecFlag::FENCE);
  REQUIRE(Decode(MATCH_FADD_S).is<FADD_S>());
  REQUIRE(Decode(MATCH_FSA_PRI_RESET).is<FSA_PRI_RESET>());
  for (uint32_t word : {MATCH_MRET, MATCH_WFI, MATCH_EBREAK, MATCH_FENCE_I}) {
    REQUIRE(Decode(word).is<Illegal>());
  }
}

TEST_CASE(
    "Flat instruction sets compose privileged and unprivileged instructions") {
  const Instr mret = Decode<TestInstructionSet>(MATCH_MRET);
  REQUIRE(mret.is<MRET>());
  REQUIRE(Disassemble(mret) == "mret");
  REQUIRE(mret.is_trap_return());
  REQUIRE(mret.is_cti());
  REQUIRE_FALSE(mret.is_jump());
  REQUIRE(Execute(nullptr, mret) == ExecFlag::ILLEGAL);

  const Instr csr =
      Decode<TestInstructionSet>(MATCH_CSRRS | (CSR_MSTATUS << 20) | (5 << 7));
  REQUIRE(csr.is<CSRRS>());
  REQUIRE(csr.csr() == CSR_MSTATUS);
  REQUIRE(csr.rd() == 5);
  REQUIRE(csr.is_csr());
  REQUIRE(Decode<TestInstructionSet>(MATCH_ECALL).is<ECALL>());
  const Instr ebreak = Decode<TestInstructionSet>(MATCH_EBREAK);
  REQUIRE(ebreak.is<EBREAK>());
  REQUIRE(Disassemble(ebreak) == "ebreak");
  REQUIRE(Execute(nullptr, ebreak) == ExecFlag::ILLEGAL);
  const Instr wfi = Decode<TestInstructionSet>(MATCH_WFI);
  REQUIRE(wfi.is<WFI>());
  REQUIRE(Disassemble(wfi) == "wfi");
  REQUIRE(Execute(nullptr, wfi) == ExecFlag::ILLEGAL);

  const Instr fence = Decode<TestInstructionSet>(MATCH_FENCE_I);
  REQUIRE(fence.is<FENCE_I>());
  REQUIRE(fence.is_fetch_fence());
  REQUIRE_FALSE(fence.is_cti());
  REQUIRE(Disassemble(fence) == "fence.i");
  REQUIRE(Execute(nullptr, fence) == ExecFlag::ILLEGAL);
  REQUIRE(Decode<TestInstructionSet>(MATCH_FENCE).is<FENCE>());
}

TEST_CASE("Instruction set membership determines supported instructions") {
  // Declaring all built-in classes must not enable them in every decoder.
  for (uint32_t word : {MATCH_MRET, MATCH_WFI, MATCH_EBREAK, MATCH_FENCE_I}) {
    CAPTURE(word);
    REQUIRE(Decode(word).is<Illegal>());
  }
  REQUIRE(Decode<PrivilegedInstructions>(MATCH_MRET).is<MRET>());
  REQUIRE(Decode<PrivilegedInstructions>(MATCH_WFI).is<WFI>());
  for (uint32_t word :
       {MATCH_ECALL, MATCH_EBREAK, MATCH_FENCE, MATCH_FENCE_I}) {
    CAPTURE(word);
    REQUIRE(Decode<PrivilegedInstructions>(word).is<Illegal>());
  }
  REQUIRE(Execute(nullptr, Decode(MATCH_FENCE)) == ExecFlag::FENCE);
  REQUIRE(Decode<TestInstructionSet>(MATCH_AMOADD_W).is<Illegal>());
  REQUIRE(Decode<TestInstructionSet>(MATCH_FADD_S).is<Illegal>());
  REQUIRE(Decode<TestInstructionSet>(MATCH_FMADD_S).is<Illegal>());
  REQUIRE(Decode<TestInstructionSet>(MATCH_FSA_PRI_RESET).is<Illegal>());
  const Instr load =
      Decode<TestInstructionSet>(MATCH_LD | (3 << 7) | (5 << 15));
  REQUIRE(load.is<LD>());
  REQUIRE(load.is_load());
  REQUIRE(load.rd() == 3);
  REQUIRE(load.rs1() == 5);
  const auto alias = Decode<std::tuple<LoadAlias>>(MATCH_LD);
  REQUIRE(alias.is<LoadAlias>());
  REQUIRE(alias.is_load());
  REQUIRE(alias.is_memory_access());
  REQUIRE_FALSE(alias.is_cti());
  REQUIRE_FALSE(alias.affects_scheduling());
}

TEST_CASE("Primitive properties determine instruction classifications") {
  // Expected classifications come from instruction semantics, not kFlags.
  using Query = bool (Instr::*)() const;
  struct Case {
    uint32_t word;
    Query kind;
    bool cti = false;
    bool memory = false;
  };
  using Set = InstructionSet<DefaultInstructionSet, PrivilegedInstructions,
                             std::tuple<EBREAK, FENCE_I>>;
  for (const auto &c : {
           Case{MATCH_ADD, nullptr},
           Case{MATCH_LB, &Instr::is_load, false, true},
           Case{MATCH_LH, &Instr::is_load, false, true},
           Case{MATCH_LW, &Instr::is_load, false, true},
           Case{MATCH_LBU, &Instr::is_load, false, true},
           Case{MATCH_LHU, &Instr::is_load, false, true},
           Case{MATCH_LWU, &Instr::is_load, false, true},
           Case{MATCH_LD, &Instr::is_load, false, true},
           Case{MATCH_SB, &Instr::is_store, false, true},
           Case{MATCH_SH, &Instr::is_store, false, true},
           Case{MATCH_SW, &Instr::is_store, false, true},
           Case{MATCH_SD, &Instr::is_store, false, true},
           Case{MATCH_AMOADD_W, &Instr::is_amo, false, true},
           Case{MATCH_AMOCAS_W, &Instr::is_amo, false, true},
           Case{MATCH_AMOCAS_D, &Instr::is_amo, false, true},
           Case{MATCH_BEQ, &Instr::is_branch, true},
           Case{MATCH_BNE, &Instr::is_branch, true},
           Case{MATCH_BLT, &Instr::is_branch, true},
           Case{MATCH_BGE, &Instr::is_branch, true},
           Case{MATCH_BLTU, &Instr::is_branch, true},
           Case{MATCH_BGEU, &Instr::is_branch, true},
           Case{MATCH_JAL, &Instr::is_jump, true},
           Case{MATCH_JALR, &Instr::is_jump, true},
           Case{MATCH_CSRRW, &Instr::is_csr},
           Case{MATCH_CSRRS, &Instr::is_csr},
           Case{MATCH_CSRRC, &Instr::is_csr},
           Case{MATCH_CSRRWI, &Instr::is_csr},
           Case{MATCH_CSRRSI, &Instr::is_csr},
           Case{MATCH_CSRRCI, &Instr::is_csr},
           Case{MATCH_ECALL, &Instr::is_trap_request, true},
           Case{MATCH_EBREAK, &Instr::is_trap_request, true},
           Case{MATCH_MRET, &Instr::is_trap_return, true},
           Case{MATCH_WFI, &Instr::is_wait},
           Case{MATCH_FENCE, &Instr::is_memory_fence},
           Case{MATCH_FENCE_I, &Instr::is_fetch_fence},
           Case{MATCH_FSA_PRI_RESET, &Instr::affects_scheduling},
           Case{MATCH_FSA_PRI_RAISE, &Instr::affects_scheduling},
           Case{MATCH_FSA_PRI_LOWER, &Instr::affects_scheduling},
           Case{MATCH_FSA_BAR, &Instr::affects_scheduling},
           Case{MATCH_FADD_S, nullptr},
           Case{MATCH_NTL_P1, nullptr},
           Case{0xffffffff, nullptr},
       }) {
    CAPTURE(c.word);
    const auto instr = Decode<Set>(c.word);
    for (Query query :
         {&Instr::is_load, &Instr::is_store, &Instr::is_amo, &Instr::is_branch,
          &Instr::is_jump, &Instr::is_trap_request, &Instr::is_trap_return,
          &Instr::is_csr, &Instr::is_memory_fence, &Instr::is_fetch_fence,
          &Instr::is_wait, &Instr::affects_scheduling}) {
      REQUIRE((instr.*query)() == (query == c.kind));
    }
    REQUIRE(instr.is_cti() == c.cti);
    REQUIRE(instr.is_memory_access() == c.memory);
  }
}

TEST_CASE("Caller-defined instructions use the same dispatch and metadata") {
  REQUIRE(Decode(0x2b).is<Illegal>());
  const Instr custom = Decode<TestInstructionSet>(0x2b);
  REQUIRE(custom.is<CustomInstruction>());
  REQUIRE(custom.is_csr());
  REQUIRE(custom.affects_scheduling());
  REQUIRE_FALSE(custom.is_cti());
  REQUIRE_FALSE(custom.is_memory_access());
  REQUIRE(Disassemble(custom) == "custom");
  REQUIRE(Decode<TestInstructionSet>(0x102b).is<Illegal>());
  REQUIRE(Decode<TestInstructionSet>(0xffffffff).is<Illegal>());
  REQUIRE(Decode(0xffffffff).is<Illegal>());
}

}  // namespace

int sc_main(int argc, char *argv[]) { return Catch::Session().run(argc, argv); }
