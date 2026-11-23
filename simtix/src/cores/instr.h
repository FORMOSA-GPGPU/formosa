/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <cstdint>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>

#include "cores/encoding.h"
#include "cores/exec_flag.h"

namespace simtix {

class ExecContext;
class Instr;
struct Illegal;

using ExecFunc = ExecFlag (*)(ExecContext *, const Instr &);
using DisassembleFunc = std::string (*)(const Instr &);

struct InstrFlags {
  using Bits = uint16_t;

  static constexpr Bits kNone = 0;

  // Instruction declarations contain only primitive semantic properties.
  // Combining bits asserts every property; query unions belong in Instr.
  // None means none of these properties, not an absence of side effects.
  // Load/store denote ordinary accesses; AMOs have their own classification.
  static constexpr Bits kLoad = 1u << 0;
  static constexpr Bits kStore = 1u << 1;
  static constexpr Bits kAmo = 1u << 2;
  static constexpr Bits kBranch = 1u << 3;  // Conditional branch.
  static constexpr Bits kJump = 1u << 4;    // Unconditional jump.
  // Explicit trap request/return, not an incidental execution fault.
  static constexpr Bits kTrapRequest = 1u << 5;
  static constexpr Bits kTrapReturn = 1u << 6;
  static constexpr Bits kCsr = 1u << 7;
  static constexpr Bits kMemoryFence = 1u << 8;  // Data and I/O ordering.
  static constexpr Bits kFetchFence = 1u << 9;   // Instruction-fetch sync.
  static constexpr Bits kWait = 1u << 10;        // Wait-for-event instruction.
  // Execution-group priority or participation, including barrier arrival.
  // This is a SIMT property; resolving it may redirect the group's PC.
  static constexpr Bits kScheduling = 1u << 11;
};

enum Opcode : uint8_t {
  // clang-format off
  kLoad    = 0b0000011,
  kFLoad   = 0b0000111,
  kAmo     = 0b0101111,
  kStore   = 0b0100011,
  kMiscMem = 0b0001111,
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
  // Explicit control transfers, including trap requests and returns. A branch
  // need not be taken; an incidental fault does not turn a load into a CTI.
  inline bool is_cti() const {
    constexpr auto kMask = InstrFlags::kBranch | InstrFlags::kJump |
                           InstrFlags::kTrapRequest | InstrFlags::kTrapReturn;
    return (instr_flags_ & kMask) != 0;
  }
  inline bool is_branch() const {
    return (instr_flags_ & InstrFlags::kBranch) != 0;
  }
  inline bool is_jump() const {
    return (instr_flags_ & InstrFlags::kJump) != 0;
  }
  // Data accesses only; fences order accesses but do not themselves access
  // data.
  inline bool is_memory_access() const {
    constexpr auto kMask =
        InstrFlags::kLoad | InstrFlags::kStore | InstrFlags::kAmo;
    return (instr_flags_ & kMask) != 0;
  }
  inline bool is_load() const {
    return (instr_flags_ & InstrFlags::kLoad) != 0;
  }
  inline bool is_store() const {
    return (instr_flags_ & InstrFlags::kStore) != 0;
  }
  inline bool is_amo() const { return (instr_flags_ & InstrFlags::kAmo) != 0; }
  inline bool is_trap_request() const {
    return (instr_flags_ & InstrFlags::kTrapRequest) != 0;
  }
  inline bool is_trap_return() const {
    return (instr_flags_ & InstrFlags::kTrapReturn) != 0;
  }
  inline bool is_csr() const { return (instr_flags_ & InstrFlags::kCsr) != 0; }
  inline bool is_memory_fence() const {
    return (instr_flags_ & InstrFlags::kMemoryFence) != 0;
  }
  inline bool is_fetch_fence() const {
    return (instr_flags_ & InstrFlags::kFetchFence) != 0;
  }
  inline bool is_wait() const {
    return (instr_flags_ & InstrFlags::kWait) != 0;
  }
  inline bool affects_scheduling() const {
    return (instr_flags_ & InstrFlags::kScheduling) != 0;
  }
  // Call/return classification depends on operands, so it is not cached.
  bool is_call() const;
  bool is_ret() const;
  template <typename Definition>
  bool is() const {
    return exec_ == &Definition::Execute;
  }
  template <typename InstrClass>
  bool in() const {
    // O(n) in the number of definitions in InstrClass.
    return IsAny<InstrClass>(
        std::make_index_sequence<std::tuple_size_v<InstrClass>>{});
  }

  void Reset() {
    rd_ = kNullReg;
    rs1_ = kNullReg;
    rs2_ = kNullReg;
    rs3_ = kNullReg;
    imm_ = 0;
    instr_flags_ = InstrFlags::kNone;
    exec_ = nullptr;
    disassemble_ = nullptr;
  }

  static constexpr uint8_t kNullReg = 0xff;

 private:
  template <typename InstrClass, std::size_t... I>
  bool IsAny(std::index_sequence<I...>) const {
    // Illegal is the fallback in decoder tuples, not a member of each class.
    return ((!std::is_same_v<std::tuple_element_t<I, InstrClass>, Illegal> &&
             is<std::tuple_element_t<I, InstrClass>>()) ||
            ...);
  }

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

  InstrFlags::Bits instr_flags_ = InstrFlags::kNone;

  ExecFunc exec_ = nullptr;
  DisassembleFunc disassemble_ = nullptr;

  friend struct InstrFields;

  template <typename T>
  friend class Decoder;

  friend ExecFlag Execute(ExecContext *ctx, const Instr &instr);
  friend std::string Disassemble(const Instr &instr);
};

// Execute an instruction on a given context.
inline ExecFlag Execute(ExecContext *ctx, const Instr &instr) {
  return instr.exec_(ctx, instr);
}

// Disassemble an instruction, including its operands.
inline std::string Disassemble(const Instr &instr) {
  return instr.disassemble_(instr);
}

// Ostream support for the instruction
inline std::ostream &operator<<(std::ostream &os, const Instr &instr) {
  return os << Disassemble(instr);
}

}  // namespace simtix
