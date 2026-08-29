/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <array>
#include <cstdint>

#include "riscv.h"

struct debug_if {
  virtual ~debug_if() {}

  virtual void enable_debug(void) = 0;
  virtual void disable_debug(void) = 0;
  virtual void set_single_step(void) = 0;
  virtual rv64::CoreExecStatus get_status(void) = 0;
  virtual void set_status(rv64::CoreExecStatus) = 0;

  virtual void insert_breakpoint(uint64_t) = 0;
  virtual void remove_breakpoint(uint64_t) = 0;

  virtual uint64_t get_program_counter(void) = 0;
  virtual std::array<int64_t, 32> get_registers(void) = 0;
  virtual uint64_t read_register(unsigned) = 0;
  virtual void write_register(unsigned, uint64_t) = 0;

  virtual uint8_t load_byte(uint64_t addr) = 0;
  virtual void store_byte(uint64_t addr, uint8_t val) = 0;
};
