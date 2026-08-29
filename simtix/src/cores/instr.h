/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "cores/encoding.h"
#include "cores/exec_flag.h"

namespace simtix {

class ExecContext;
class Instr;

using ExecFunc = ExecFlag (*)(ExecContext *, const Instr &);
using MnemonicFunc = std::string (*)(const Instr &);

enum Opcode : uint8_t {
  // clang-format off
  kLoad    = 0b0000011,
  kFLoad   = 0b0000111,
  kAmo     = 0b0101111,
  kStore   = 0b0100011,
  kFStore  = 0b0100111,
  kBranch  = 0b1100011,
  kCustom1 = 0b0101011,
  kOpImm   = 0b0010011,
  kOp      = 0b0110011,
  kSystem  = 0b1110011,
  kAuipc   = 0b0010111,
  kLui     = 0b0110111,
  kOpImm32 = 0b0011011,
  kOp32    = 0b0111011,
  kFmadd   = 0b1000011,
  kFmsub   = 0b1000111,
  kFnmsub  = 0b1001011,
  kFnmadd  = 0b1001111,
  kOpFp    = 0b1010011,
  kJal     = 0b1101111,
  kJalr    = 0b1100111
  // clang-format on
};

class Instr {
 private:
  enum class MemoryClass {
    kNone,
    kLoad,
    kStore,
    kAtomic,
  };

  enum class ControlClass {
    kNone,
    kBranch,
    kSerializing,
  };

 public:
  inline uint8_t rd() const { return rd_; }
  inline uint8_t rs1() const { return rs1_; }
  inline uint8_t rs2() const { return rs2_; }
  inline uint8_t rs3() const { return rs3_; }
  inline int64_t imm() const { return imm_; }
  inline uint32_t csr() const { return csr_; }
  inline uint8_t uimm() const { return uimm_; }
  inline uint32_t pri() const { return pri_; }
  inline uint8_t rm() const { return rm_; }
  inline bool is_cti() const { return is_cti_; }
  inline bool is_mem() const { return memory_class_ != MemoryClass::kNone; }
  inline bool is_load() const { return memory_class_ == MemoryClass::kLoad; }
  inline bool is_store() const { return memory_class_ == MemoryClass::kStore; }
  inline bool is_atomic() const {
    return memory_class_ == MemoryClass::kAtomic;
  }
  inline bool is_serializing() const {
    return control_class_ == ControlClass::kSerializing;
  }
  inline bool is_control() const {
    return control_class_ != ControlClass::kNone;
  }

  void Reset() {
    rd_ = kNullReg;
    rs1_ = kNullReg;
    rs2_ = kNullReg;
    rs3_ = kNullReg;
    imm_ = 0;
    is_cti_ = false;
    memory_class_ = MemoryClass::kNone;
    control_class_ = ControlClass::kNone;
  }

  static constexpr uint8_t kNullReg = 0xff;

 private:
  uint8_t rd_ = kNullReg;
  uint8_t rs1_ = kNullReg;
  uint8_t rs2_ = kNullReg;
  uint8_t rs3_ = kNullReg;
  union {
    int64_t imm_ = 0;
    struct {
      uint32_t csr_;
      uint8_t uimm_;
    };
    uint32_t pri_;
    uint8_t rm_;
  };

  bool is_cti_ = false;

  MemoryClass memory_class_ = MemoryClass::kNone;
  ControlClass control_class_ = ControlClass::kNone;

  ExecFunc exec_ = nullptr;
  MnemonicFunc mnemonic_ = nullptr;

  friend struct RType;
  friend struct IType;
  friend struct SType;
  friend struct BType;
  friend struct UType;
  friend struct JType;
  friend struct SystemType;
  friend struct FormosaType;
  friend struct FPType;
  friend struct R4Type;

  template <typename T>
  friend class Decoder;

  friend ExecFlag Execute(ExecContext *ctx, const Instr &instr);
  friend std::string Mnemonic(const Instr &instr);
};

// Execute an instruction on a given context.
inline ExecFlag Execute(ExecContext *ctx, const Instr &instr) {
  return instr.exec_(ctx, instr);
}

// Get the mnemonic of an instruction.
inline std::string Mnemonic(const Instr &instr) {
  return instr.mnemonic_(instr);
}

// Ostream support for the instruction
inline std::ostream &operator<<(std::ostream &os, const Instr &instr) {
  return os << Mnemonic(instr);
}

/////////////////////////
// Instruction formats //
/////////////////////////
struct NoType {
  static void Fill(Instr *, uint32_t) {}
};

struct RType {
  static void Fill(Instr *instr, uint32_t iword);
  static std::string Disasm(std::string_view name, const Instr &instr);
};

struct IType {
  static void Fill(Instr *instr, uint32_t iword);
  static std::string Disasm(std::string_view name, const Instr &instr);
};

struct SType {
  static void Fill(Instr *instr, uint32_t iword);
  static std::string Disasm(std::string_view name, const Instr &instr);
};

struct BType {
  static void Fill(Instr *instr, uint32_t iword);
  static std::string Disasm(std::string_view name, const Instr &instr);
};

struct UType {
  static void Fill(Instr *instr, uint32_t iword);
  static std::string Disasm(std::string_view name, const Instr &instr);
};

struct JType {
  static void Fill(Instr *instr, uint32_t iword);
  static std::string Disasm(std::string_view name, const Instr &instr);
};

struct SystemType {
  static void Fill(Instr *instr, uint32_t iword);
  static std::string Disasm(std::string_view name, const Instr &instr);
};

struct FormosaType {
  static void Fill(Instr *instr, uint32_t iword);
  static std::string Disasm(std::string_view name, const Instr &instr);
};

struct FPType {
  static void Fill(Instr *instr, uint32_t iword);
  static std::string Disasm(std::string_view name, const Instr &instr);
};

struct R4Type {
  static void Fill(Instr *instr, uint32_t iword);
  static std::string Disasm(std::string_view name, const Instr &instr);
};

}  // namespace simtix
