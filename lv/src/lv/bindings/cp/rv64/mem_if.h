/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <cstdint>

struct instr_memory_if {
  virtual ~instr_memory_if() {}

  virtual uint32_t load_instr(uint64_t pc) = 0;
};

struct data_memory_if {
  virtual ~data_memory_if() {}

  virtual int64_t load_double(uint64_t addr) = 0;
  virtual int64_t load_word(uint64_t addr) = 0;
  virtual int64_t load_half(uint64_t addr) = 0;
  virtual int64_t load_byte(uint64_t addr) = 0;
  virtual uint64_t load_uword(uint64_t addr) = 0;
  virtual uint64_t load_uhalf(uint64_t addr) = 0;
  virtual uint64_t load_ubyte(uint64_t addr) = 0;

  virtual void store_double(uint64_t addr, uint64_t value) = 0;
  virtual void store_word(uint64_t addr, uint32_t value) = 0;
  virtual void store_half(uint64_t addr, uint16_t value) = 0;
  virtual void store_byte(uint64_t addr, uint8_t value) = 0;

  // virtual void flush_tlb() = 0;
};
