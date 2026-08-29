// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include "cores/instr.h"

#include <fmt/format.h>
#include <liblv/binding.h>

#include <string>
#include <string_view>

namespace simtix {

namespace {

std::string_view RegName(uint8_t r) {
  // Static array of names stored in the data segment
  static constexpr std::string_view kRegNames[] = {
      "zero", "ra", "sp", "gp", "tp",  "t0",  "t1", "t2", "s0", "s1", "a0",
      "a1",   "a2", "a3", "a4", "a5",  "a6",  "a7", "s2", "s3", "s4", "s5",
      "s6",   "s7", "s8", "s9", "s10", "s11", "t3", "t4", "t5", "t6"};

  if (r < 32) return kRegNames[r];
  return "unknown";
}

///////////////////////////////////
// Helper functions for decoding //
///////////////////////////////////
inline uint8_t DecodeOpcode(uint32_t iword) {
  return iword & INSN_FIELD_OPCODE;
}

inline uint8_t DecodeRd(uint32_t iword) { return (iword & INSN_FIELD_RD) >> 7; }

inline uint8_t DecodeRs1(uint32_t iword) {
  return (iword & INSN_FIELD_RS1) >> 15;
}

inline uint8_t DecodeRs2(uint32_t iword) {
  return (iword & INSN_FIELD_RS2) >> 20;
}

inline uint8_t DecodeRs3(uint32_t iword) {
  return (iword & INSN_FIELD_RS3) >> 27;
}

inline uint8_t DecodeFunct3(uint32_t iword) {
  return (iword & INSN_FIELD_FUNCT3) >> 12;
}

inline uint8_t DecodeFunct7(uint32_t iword) {
  return (iword & INSN_FIELD_FUNCT7) >> 25;
}

inline int64_t DecodeImmIType(uint32_t iword) {
  return static_cast<int32_t>(iword) >> 20;
}

inline int64_t DecodeImmSType(uint32_t iword) {
  return (static_cast<int32_t>(iword & INSN_FIELD_IMM12HI) >> 20) +
         (static_cast<int32_t>(iword & INSN_FIELD_IMM12LO) >> 7);
}

inline int64_t DecodeImmBType(uint32_t iword) {
  return ((static_cast<int32_t>(iword & 0x80000000) >> 19) +
          (static_cast<int32_t>(iword & 0x80) << 4) +
          (static_cast<int32_t>(iword & 0x7e000000) >> 20) +
          (static_cast<int32_t>(iword & 0xf00) >> 7));
}

inline int64_t DecodeImmUType(uint32_t iword) {
  return static_cast<int32_t>(iword & INSN_FIELD_IMM20);
}

inline int64_t DecodeImmJType(uint32_t iword) {
  return ((static_cast<int32_t>(iword & 0x80000000) >> 11) +
          (static_cast<int32_t>(iword & 0xff000)) +
          (static_cast<int32_t>(iword & 0x100000) >> 9) +
          (static_cast<int32_t>(iword & 0x7fe00000) >> 20));
}

static inline uint16_t DecodeCSR(uint32_t iword) {
  return (iword & INSN_FIELD_CSR) >> 20;
}

static inline uint8_t DecodeUImm(uint32_t iword) {
  return (iword & INSN_FIELD_ZIMM5) >> 15;
}

}  // namespace

////////////
// R type //
////////////
void RType::Fill(Instr *instr, uint32_t iword) {
  instr->rd_ = DecodeRd(iword);
  instr->rs1_ = DecodeRs1(iword);
  instr->rs2_ = DecodeRs2(iword);
  if ((iword & INSN_FIELD_OPCODE) == kAmo) {
    instr->memory_class_ = Instr::MemoryClass::kAtomic;
  }
}

std::string RType::Disasm(std::string_view name, const Instr &instr) {
  return fmt::format("{} {}, {}, {}", name, RegName(instr.rd()),
                     RegName(instr.rs1()), RegName(instr.rs2()));
}

////////////
// I type //
////////////
void IType::Fill(Instr *instr, uint32_t iword) {
  instr->rd_ = DecodeRd(iword);
  instr->rs1_ = DecodeRs1(iword);
  instr->imm_ = DecodeImmIType(iword);
  if ((iword & MASK_JALR) == MATCH_JALR) {
    instr->is_cti_ = true;
    instr->control_class_ = Instr::ControlClass::kBranch;
  }
  if ((iword & INSN_FIELD_OPCODE) == kLoad) {
    instr->memory_class_ = Instr::MemoryClass::kLoad;
  }
}

std::string IType::Disasm(std::string_view name, const Instr &instr) {
  // Check if it's a load instruction (starts with 'l') or jalr
  if (!name.empty() && (name.front() == 'l' || name == "jalr")) {
    // Format as: op rd, imm(rs1)
    return fmt::format("{} {}, {}({})", name, RegName(instr.rd()), instr.imm(),
                       RegName(instr.rs1()));
  }
  return fmt::format("{} {}, {}, {}", name, RegName(instr.rd()),
                     RegName(instr.rs1()), instr.imm());
}

////////////
// S type //
////////////
void SType::Fill(Instr *instr, uint32_t iword) {
  instr->rs1_ = DecodeRs1(iword);
  instr->rs2_ = DecodeRs2(iword);
  instr->imm_ = DecodeImmSType(iword);
  instr->memory_class_ = Instr::MemoryClass::kStore;
}

std::string SType::Disasm(std::string_view name, const Instr &instr) {
  return fmt::format("{} {}, {}({})", name, RegName(instr.rs2()), instr.imm(),
                     RegName(instr.rs1()));
}

////////////
// B type //
////////////
void BType::Fill(Instr *instr, uint32_t iword) {
  instr->rs1_ = DecodeRs1(iword);
  instr->rs2_ = DecodeRs2(iword);
  instr->imm_ = DecodeImmBType(iword);
  instr->is_cti_ = true;
  instr->control_class_ = Instr::ControlClass::kBranch;
}

std::string BType::Disasm(std::string_view name, const Instr &instr) {
  return fmt::format("{} {}, {}, {}", name, RegName(instr.rs1()),
                     RegName(instr.rs2()), instr.imm());
}

////////////
// U type //
////////////
void UType::Fill(Instr *instr, uint32_t iword) {
  instr->rd_ = DecodeRd(iword);
  instr->imm_ = DecodeImmUType(iword);
}

std::string UType::Disasm(std::string_view name, const Instr &instr) {
  return fmt::format("{} {}, {}", name, RegName(instr.rd()), instr.imm());
}

////////////
// J type //
////////////
void JType::Fill(Instr *instr, uint32_t iword) {
  instr->rd_ = DecodeRd(iword);
  instr->imm_ = DecodeImmJType(iword);
  instr->is_cti_ = true;
  instr->control_class_ = Instr::ControlClass::kBranch;
}

std::string JType::Disasm(std::string_view name, const Instr &instr) {
  return fmt::format("{} {}, {}", name, RegName(instr.rd()), instr.imm());
}

/////////////////
// System type //
/////////////////
void SystemType::Fill(Instr *instr, uint32_t iword) {
  uint8_t funct3 = DecodeFunct3(iword);
  if (funct3 != 0) {
    instr->rd_ = DecodeRd(iword);
    instr->csr_ = DecodeCSR(iword);
    instr->control_class_ = Instr::ControlClass::kSerializing;
    switch (DecodeFunct3(iword)) {
      case 0b001:
      case 0b010:
      case 0b011:
        instr->rs1_ = DecodeRs1(iword);
        break;
      case 0b101:
      case 0b110:
      case 0b111:
        instr->uimm_ = DecodeUImm(iword);
        break;
    }
  }
  if ((iword & MASK_ECALL) == MATCH_ECALL) {
    instr->is_cti_ = true;
    instr->control_class_ = Instr::ControlClass::kSerializing;
  }
}

std::string SystemType::Disasm(std::string_view name, const Instr &instr) {
  if (name == "ecall") {
    return "ecall";
  }
  // CSR instructions: op rd, csr, rs1/uimm
  if (name.back() == 'i') {
    return fmt::format("{} {}, {:#05x}, {}", name, RegName(instr.rd()),
                       instr.csr(), instr.uimm());
  }
  return fmt::format("{} {}, {:#05x}, {}", name, RegName(instr.rd()),
                     instr.csr(), RegName(instr.rs1()));
}

//////////////////
// Formosa type //
//////////////////
void FormosaType::Fill(Instr *instr, uint32_t iword) {
  // raise or lower.
  uint8_t funct3 = DecodeFunct3(iword);
  if (funct3 == 0b101 || funct3 == 0b011) {
    instr->pri_ = DecodeRs2(iword);  // rs2 encodes the pri field.
  }
  instr->is_cti_ = true;
  instr->control_class_ = Instr::ControlClass::kSerializing;
}

std::string FormosaType::Disasm(std::string_view name, const Instr &instr) {
  if (name == "fsa.pri.raise" || name == "fsa.pri.lower") {
    return fmt::format("{} {}", name, instr.pri());
  }
  return std::string(name);
}

/////////////
// FP type //
/////////////
void FPType::Fill(Instr *instr, uint32_t iword) {
  instr->rd_ = DecodeRd(iword);
  instr->rs1_ = DecodeRs1(iword);
  instr->rs2_ = DecodeRs2(iword);
  instr->rm_ = DecodeFunct3(iword);
  if ((iword & INSN_FIELD_OPCODE) == kFLoad) {
    instr->memory_class_ = Instr::MemoryClass::kLoad;
  } else if ((iword & INSN_FIELD_OPCODE) == kFStore) {
    instr->memory_class_ = Instr::MemoryClass::kStore;
  }
}

std::string FPType::Disasm(std::string_view name, const Instr &instr) {
  return fmt::format("{} {}, {}, {}", name, RegName(instr.rd()),
                     RegName(instr.rs1()), RegName(instr.rs2()));
}

/////////////
// R4 type //
/////////////
void R4Type::Fill(Instr *instr, uint32_t iword) {
  instr->rd_ = DecodeRd(iword);
  instr->rs1_ = DecodeRs1(iword);
  instr->rs2_ = DecodeRs2(iword);
  instr->rs3_ = DecodeRs3(iword);
  instr->rm_ = DecodeFunct3(iword);
}

std::string R4Type::Disasm(std::string_view name, const Instr &instr) {
  return fmt::format("{} {}, {}, {}, {}", name, RegName(instr.rd()),
                     RegName(instr.rs1()), RegName(instr.rs2()),
                     RegName(instr.rs3()));
}

LV_BINDING(simtix, Instr)
    .property("rd", &Instr::rd, lv::doc("Destination register index"))
    .property("rs1", &Instr::rs1, lv::doc("First source register index"))
    .property("rs2", &Instr::rs2, lv::doc("Second source register index"))
    .property("rs3", &Instr::rs3, lv::doc("Third source register index"))
    .property("is_cti", &Instr::is_cti,
              lv::doc("Whether this is a control-transfer instruction"))
    .property("is_mem", &Instr::is_mem,
              lv::doc("Whether this is a memory instruction"));

}  // namespace simtix
