/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once
#include <cstdint>

#include "riscv.h"
#include "util.h"

namespace rv64 {

struct Instruction {
  Instruction() : instr(0) {}

  Instruction(uint32_t instr) : instr(instr) {}

  inline uint32_t quadrant() { return instr & 0x3; }

  inline bool is_compressed() { return quadrant() < 3; }

  inline uint32_t c_format() { return instr & 0xffff; }

  inline uint32_t c_opcode() { return BIT_SLICE(instr, 15, 13); }

  inline uint32_t c_b12() { return BIT_SINGLE_P1(instr, 12); }

  inline uint32_t c_rd() { return rd(); }

  inline uint32_t c_rd_small() { return BIT_SLICE(instr, 9, 7) | 8; }

  inline uint32_t c_rs2_small() { return BIT_SLICE(instr, 4, 2) | 8; }

  inline uint32_t c_rs2() { return BIT_SLICE(instr, 6, 2); }

  inline uint32_t c_imm() {
    return BIT_SLICE(instr, 6, 2) | EXTRACT_SIGN_BIT(instr, 12, 5);
  }

  inline uint32_t c_uimm() {
    return BIT_SLICE(instr, 6, 2) | (BIT_SINGLE_P1(instr, 12) << 5);
  }

  inline uint32_t c_f2_high() { return BIT_SLICE(instr, 11, 10); }

  inline uint32_t c_f2_low() { return BIT_SLICE(instr, 6, 5); }

  Opcode::Mapping decode();
  Opcode::Mapping decode_32();
  Compressed::Opcode decode_16();

  /* Decode compressed in the same function */
  // Opcode::Mapping decode_and_expand_compressed(Architecture arch);

  inline uint32_t csr() {
    // cast to unsigned to avoid sign extension when shifting
    return BIT_RANGE((uint32_t)instr, 31, 20) >> 20;
  }

  inline uint32_t zimm() { return BIT_RANGE(instr, 19, 15) >> 15; }

  inline unsigned shamt() { return (BIT_RANGE(instr, 25, 20) >> 20); }

  inline unsigned shamt_w() { return (BIT_RANGE(instr, 24, 20) >> 20); }

  inline int32_t funct2() { return (BIT_RANGE(instr, 26, 25) >> 25); }

  inline int32_t funct3() { return (BIT_RANGE(instr, 14, 12) >> 12); }

  inline int32_t funct12() {
    // cast to unsigned to avoid sign extension when shifting
    return (BIT_RANGE((uint32_t)instr, 31, 20) >> 20);
  }

  inline int32_t funct7() {
    // cast to unsigned to avoid sign extension when shifting
    return (BIT_RANGE((uint32_t)instr, 31, 25) >> 25);
  }

  inline int32_t funct6() {
    // cast to unsigned to avoid sign extension when shifting
    return (BIT_RANGE((uint32_t)instr, 31, 26) >> 26);
  }

  inline int32_t funct5() {
    // cast to unsigned to avoid sign extension when shifting
    return (BIT_RANGE((uint32_t)instr, 31, 27) >> 27);
  }

  inline uint32_t frm() { return BIT_SLICE(instr, 14, 12); }

  inline uint32_t fence_succ() { return BIT_SLICE(instr, 23, 20); }

  inline uint32_t fence_pred() { return BIT_SLICE(instr, 27, 24); }

  inline uint32_t fence_fm() { return BIT_SLICE(instr, 31, 28); }

  inline bool aq() { return BIT_SINGLE(instr, 26); }

  inline bool rl() { return BIT_SINGLE(instr, 25); }

  inline int32_t opcode() { return BIT_RANGE(instr, 6, 0); }

  inline int32_t J_imm() {
    return (BIT_SINGLE(instr, 31) >> 11) | BIT_RANGE(instr, 19, 12) |
           (BIT_SINGLE(instr, 20) >> 9) | (BIT_RANGE(instr, 30, 21) >> 20);
  }

  inline int32_t I_imm() { return BIT_RANGE(instr, 31, 20) >> 20; }

  inline int32_t S_imm() {
    return (BIT_RANGE(instr, 31, 25) >> 20) | (BIT_RANGE(instr, 11, 7) >> 7);
  }

  inline int32_t B_imm() {
    return (BIT_SINGLE(instr, 31) >> 19) | (BIT_SINGLE(instr, 7) << 4) |
           (BIT_RANGE(instr, 30, 25) >> 20) | (BIT_RANGE(instr, 11, 8) >> 7);
  }

  inline int32_t U_imm() { return BIT_RANGE(instr, 31, 12); }

  inline uint32_t rs1() { return BIT_RANGE(instr, 19, 15) >> 15; }

  inline uint32_t rs2() { return BIT_RANGE(instr, 24, 20) >> 20; }

  inline uint32_t rs3() { return BIT_RANGE((uint32_t)instr, 31, 27) >> 27; }

  inline uint32_t rd() { return BIT_RANGE(instr, 11, 7) >> 7; }

  inline uint32_t data() { return instr; }

 private:
  // use signed variable to have correct sign extension in immediates
  int32_t instr;
};

}  // namespace rv64
