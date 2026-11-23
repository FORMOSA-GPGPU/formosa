/* SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 * SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <fmt/compile.h>

#include <string>
#include <string_view>

#include "cores/instr.h"

namespace simtix::detail {

inline std::string_view RegName(uint8_t r) {
  static constexpr std::string_view names[] = {
      "zero", "ra", "sp", "gp", "tp",  "t0",  "t1", "t2", "s0", "s1", "a0",
      "a1",   "a2", "a3", "a4", "a5",  "a6",  "a7", "s2", "s3", "s4", "s5",
      "s6",   "s7", "s8", "s9", "s10", "s11", "t3", "t4", "t5", "t6"};
  return r < 32 ? names[r] : "unknown";
}

// The optional operand owns its separator. The omitted mode is selected at
// compile time; the instruction's encoded mode determines the actual suffix.
template <unsigned Omitted>
inline std::string_view RmSuffix(uint8_t rm) {
  static_assert(Omitted < 8);
  static constexpr std::string_view suffixes[] = {
      ", rne", ", rtz", ", rdn", ", rup", ", rmm", ", 5", ", 6", ", dyn"};
  if (rm == Omitted) return {};
  return rm < 8 ? suffixes[rm] : ", unknown";
}

}  // namespace simtix::detail

namespace simtix::disasm {

// Formatters describe assembly text only; Fields independently define decoding.
// Reg counts source operands, excluding rd. All fmt strings compile in C++17.
inline std::string None(const Instr&, std::string_view mnemonic) {
  return std::string(mnemonic);
}

inline std::string Reg(const Instr& instr, std::string_view mnemonic) {
  return fmt::format(FMT_COMPILE("{} {}, {}"), mnemonic,
                     detail::RegName(instr.rd()), detail::RegName(instr.rs1()));
}

inline std::string RegReg(const Instr& instr, std::string_view mnemonic) {
  return fmt::format(FMT_COMPILE("{} {}, {}, {}"), mnemonic,
                     detail::RegName(instr.rd()), detail::RegName(instr.rs1()),
                     detail::RegName(instr.rs2()));
}

inline std::string RegImm(const Instr& instr, std::string_view mnemonic) {
  return fmt::format(FMT_COMPILE("{} {}, {}, {}"), mnemonic,
                     detail::RegName(instr.rd()), detail::RegName(instr.rs1()),
                     instr.imm());
}

inline std::string Imm(const Instr& instr, std::string_view mnemonic) {
  return fmt::format(FMT_COMPILE("{} {}, {}"), mnemonic,
                     detail::RegName(instr.rd()), instr.imm());
}

inline std::string Load(const Instr& instr, std::string_view mnemonic) {
  return fmt::format(FMT_COMPILE("{} {}, {}({})"), mnemonic,
                     detail::RegName(instr.rd()), instr.imm(),
                     detail::RegName(instr.rs1()));
}

inline std::string Store(const Instr& instr, std::string_view mnemonic) {
  return fmt::format(FMT_COMPILE("{} {}, {}({})"), mnemonic,
                     detail::RegName(instr.rs2()), instr.imm(),
                     detail::RegName(instr.rs1()));
}

inline std::string Atomic(const Instr& instr, std::string_view mnemonic) {
  return fmt::format(FMT_COMPILE("{} {}, {}, ({})"), mnemonic,
                     detail::RegName(instr.rd()), detail::RegName(instr.rs2()),
                     detail::RegName(instr.rs1()));
}

inline std::string Branch(const Instr& instr, std::string_view mnemonic) {
  return fmt::format(FMT_COMPILE("{} {}, {}, {}"), mnemonic,
                     detail::RegName(instr.rs1()), detail::RegName(instr.rs2()),
                     instr.imm());
}

inline std::string CsrReg(const Instr& instr, std::string_view mnemonic) {
  return fmt::format(FMT_COMPILE("{} {}, {:#05x}, {}"), mnemonic,
                     detail::RegName(instr.rd()), instr.csr(),
                     detail::RegName(instr.rs1()));
}

inline std::string CsrImm(const Instr& instr, std::string_view mnemonic) {
  return fmt::format(FMT_COMPILE("{} {}, {:#05x}, {}"), mnemonic,
                     detail::RegName(instr.rd()), instr.csr(), instr.uimm());
}

inline std::string Pri(const Instr& instr, std::string_view mnemonic) {
  return fmt::format(FMT_COMPILE("{} {}"), mnemonic, instr.pri());
}

// Dynamic rounding (7) is implicit for ordinary FP operations. Exact widening
// conversions use RegRm<0> to omit their default RNE instead.
template <unsigned Omitted = 7>
inline std::string RegRm(const Instr& instr, std::string_view mnemonic) {
  return fmt::format(FMT_COMPILE("{} {}, {}{}"), mnemonic,
                     detail::RegName(instr.rd()), detail::RegName(instr.rs1()),
                     detail::RmSuffix<Omitted>(instr.rm()));
}

inline std::string RegRegRm(const Instr& instr, std::string_view mnemonic) {
  return fmt::format(FMT_COMPILE("{} {}, {}, {}{}"), mnemonic,
                     detail::RegName(instr.rd()), detail::RegName(instr.rs1()),
                     detail::RegName(instr.rs2()),
                     detail::RmSuffix<7>(instr.rm()));
}

inline std::string RegRegRegRm(const Instr& instr, std::string_view mnemonic) {
  return fmt::format(FMT_COMPILE("{} {}, {}, {}, {}{}"), mnemonic,
                     detail::RegName(instr.rd()), detail::RegName(instr.rs1()),
                     detail::RegName(instr.rs2()), detail::RegName(instr.rs3()),
                     detail::RmSuffix<7>(instr.rm()));
}

}  // namespace simtix::disasm
