// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include "Regfile.h"

#include <fmt/format.h>
#include <liblv/log.h>

#include <cassert>

#include "util.h"

namespace rv64 {

const char *regnames[] = {
    "zero (x0)", "ra   (x1)", "sp   (x2)", "gp   (x3)", "tp   (x4)",
    "t0   (x5)", "t1   (x6)", "t2   (x7)", "s0/fp(x8)", "s1   (x9)",
    "a0  (x10)", "a1  (x11)", "a2  (x12)", "a3  (x13)", "a4  (x14)",
    "a5  (x15)", "a6  (x16)", "a7  (x17)", "s2  (x18)", "s3  (x19)",
    "s4  (x20)", "s5  (x21)", "s6  (x22)", "s7  (x23)", "s8  (x24)",
    "s9  (x25)", "s10 (x26)", "s11 (x27)", "t3  (x28)", "t4  (x29)",
    "t5  (x30)", "t6  (x31)",
};

RegFile::RegFile() { regs.fill(0); }

RegFile::RegFile(const RegFile &other) { regs = other.regs; }

void RegFile::write(uint64_t index, int64_t value) {
  assert(index >= x0 && index <= x31);
  regs[index] = value;
}

int64_t RegFile::read(uint64_t index) {
  assert(index <= x31);
  return regs[index];
}

uint64_t RegFile::shamt_w(uint64_t index) {
  assert(index <= x31);
  return BIT_RANGE(regs[index], 5, 0);
}

uint64_t RegFile::shamt(uint64_t index) {
  assert(index <= x31);
  return BIT_RANGE(regs[index], 5, 0);
}

int64_t &RegFile::operator[](const uint64_t index) {
  assert(index <= x31);
  return regs[index];
}

void RegFile::show() {
  for (int i = 0; i < 32; i++) {
    lv::Println("%s = %16lx", regnames[i], regs[i]);
  }
}

std::array<int64_t, 32> RegFile::get_regs() { return this->regs; }

}  // namespace rv64
