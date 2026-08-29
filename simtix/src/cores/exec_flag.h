/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <cstdint>

#include "cores/encoding.h"
#include "tlm_extensions/atomic_extension.h"

namespace simtix {

enum class ExecFlag : uint32_t {
  NONE = 0,

  // --- Primary Actions ---
  RD_DATA = 1 << 0,
  NEXT_PC = 1 << 1,

  // --- Subsystem Actions ---
  LOAD = 1 << 2,
  STORE = 1 << 3,
  CSR = 1 << 4,

  // --- Priority & Barrier ---
  PRIORITY = 1 << 5,
  BARRIER = 1 << 6,

  // --- Floating Point Status ---
  FP_EXC = 1 << 7,

  // --- Atomic Operation ---
  ATOMIC = 1 << 8,

  // --- Control / Exception ---
  ECALL = 1 << 9,
  ILLEGAL = 1 << 10,
  INSTR_ADDR_MISALIGNED = 1 << 11,
  LOAD_ADDR_MISALIGNED = 1 << 12,
  STORE_ADDR_MISALIGNED = 1 << 13,
  _EXCEPTION_MASK = 0b11111 << 9,

  // =========================================================
  // --- [High Bits] (Modifiers) ---
  // =========================================================

  // --- Memory Modifiers (Bit 16-18) ---
  WIDTH_1 = 0 << 16,
  WIDTH_2 = 1 << 16,
  WIDTH_4 = 2 << 16,
  WIDTH_8 = 3 << 16,
  _WIDTH_MASK = 3 << 16,
  // Controls sign-extension of a memory result when written back into an
  // XLEN-sized register. For AMOs, comparison signedness is encoded by the
  // AMO op itself (e.g. AMO_MIN vs AMO_MINU), not by this bit.
  SIGNED = 1 << 18,

  // --- CSR Modifiers (Bit 19-20) ---
  CSRRW = 1 << 19,
  CSRRS = 2 << 19,
  CSRRC = 3 << 19,
  _CSR_MASK = 3 << 19,

  // --- [Extension] Priority Encoding (Bit 21-22) ---
  PRI_RESET = 0 << 21,  // Reset Priority
  PRI_RAISE = 1 << 21,  // Raise Priority
  PRI_LOWER = 2 << 21,  // Lower Priority
  _PRI_MASK = 3 << 21,  // Helper Mask

  // --- [Extension] Atomic Encoding (Bit 23-26) ---
  AMO_SWAP = 1 << 23,
  AMO_ADD = 2 << 23,
  AMO_XOR = 3 << 23,
  AMO_AND = 4 << 23,
  AMO_OR = 5 << 23,
  AMO_MIN = 6 << 23,
  AMO_MAX = 7 << 23,
  AMO_MINU = 8 << 23,
  AMO_MAXU = 9 << 23,
  _AMO_MASK = 0xf << 23,

  // --- [Extension] M extension (Bit 27) ---
  MDU = 1 << 27,

  // --- [Extension] F / D extension (Bit 28) ---
  FPU = 1 << 28
};

// --- Operators ---
inline ExecFlag operator|(ExecFlag a, ExecFlag b) {
  return static_cast<ExecFlag>(static_cast<uint32_t>(a) |
                               static_cast<uint32_t>(b));
}

inline ExecFlag operator&(ExecFlag a, ExecFlag b) {
  return static_cast<ExecFlag>(static_cast<uint32_t>(a) &
                               static_cast<uint32_t>(b));
}

inline bool HasFlag(ExecFlag target, ExecFlag test) {
  return static_cast<uint32_t>(target & test) != 0;
}

inline ExecFlag GetException(ExecFlag f) {
  return f & ExecFlag::_EXCEPTION_MASK;
}

inline bool HasException(ExecFlag f) {
  return GetException(f) != ExecFlag::NONE;
}

inline uint64_t DecodeExceptionCause(ExecFlag f) {
  switch (GetException(f)) {
    case ExecFlag::ECALL:
      return CAUSE_MACHINE_ECALL;
    case ExecFlag::ILLEGAL:
      return CAUSE_ILLEGAL_INSTRUCTION;
    case ExecFlag::INSTR_ADDR_MISALIGNED:
      return CAUSE_MISALIGNED_FETCH;
    case ExecFlag::LOAD_ADDR_MISALIGNED:
      return CAUSE_MISALIGNED_LOAD;
    case ExecFlag::STORE_ADDR_MISALIGNED:
      return CAUSE_MISALIGNED_STORE;
    default:
      return 0;
  }
}

inline uint32_t GetMemSize(ExecFlag f) {
  return 1 << (static_cast<uint32_t>(f & ExecFlag::_WIDTH_MASK) >> 16);
}

inline bool IsSigned(ExecFlag f) { return HasFlag(f, ExecFlag::SIGNED); }

inline ExecFlag GetCsrOp(ExecFlag f) { return f & ExecFlag::_CSR_MASK; }

inline ExecFlag GetPriOp(ExecFlag f) { return f & ExecFlag::_PRI_MASK; }

inline ExecFlag GetAmoOp(ExecFlag f) { return f & ExecFlag::_AMO_MASK; }

inline AtomicExtension::Op DecodeAtomicOp(ExecFlag flag) {
  switch (GetAmoOp(flag)) {
    case ExecFlag::AMO_SWAP:
      return AtomicExtension::Op::kSwap;
    case ExecFlag::AMO_ADD:
      return AtomicExtension::Op::kAdd;
    case ExecFlag::AMO_XOR:
      return AtomicExtension::Op::kXor;
    case ExecFlag::AMO_AND:
      return AtomicExtension::Op::kAnd;
    case ExecFlag::AMO_OR:
      return AtomicExtension::Op::kOr;
    case ExecFlag::AMO_MIN:
      return AtomicExtension::Op::kMin;
    case ExecFlag::AMO_MAX:
      return AtomicExtension::Op::kMax;
    case ExecFlag::AMO_MINU:
      return AtomicExtension::Op::kMinU;
    case ExecFlag::AMO_MAXU:
      return AtomicExtension::Op::kMaxU;
    default:
      return AtomicExtension::Op::kAdd;
  }
}

}  // namespace simtix
