/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <array>
#include <cstdint>
#include <unordered_set>

#include "Regfile.h"
#include "csr.h"
#include "debug_if.h"
#include "instr.h"
#include "mem_if.h"
#include "riscv.h"
#include "trap.h"

/* int128_t type required for M extension */
#ifdef __SIZEOF_INT128__
typedef __int128_t int128_t;
typedef __uint128_t uint128_t;
#else
#error "int128_t not supported by this GCC version"
#endif

namespace rv64 {

class Core : public debug_if {
 public:
  explicit Core(uint64_t hart_id);
  Core(uint64_t hart_id, bool trace);
  void run_step();
  void run();

  /* Interrupt interface */
  void ext_interrupt(bool b);
  void sw_interrupt(bool b);
  void timer_interrupt(bool b);

  void set_pc(uint64_t pc);
  void set_instr_mem(instr_memory_if *);
  void set_data_mem(data_memory_if *);

  /* debug_if implementation */
  void enable_debug() override;
  void disable_debug() override;
  void set_single_step() override;
  void set_status(CoreExecStatus s) override;
  CoreExecStatus get_status() override;
  void insert_breakpoint(uint64_t pc) override;
  void remove_breakpoint(uint64_t pc) override;
  uint64_t get_program_counter() override;
  std::array<int64_t, 32> get_registers() override;
  uint64_t read_register(unsigned addr) override;
  void write_register(unsigned addr, uint64_t val) override;

  uint8_t load_byte(uint64_t addr) override;
  void store_byte(uint64_t addr, uint8_t value) override;

 private:
  instr_memory_if *instr_mem = nullptr;
  data_memory_if *data_mem = nullptr;
  RegFile regs;
  csr_table csr;

  bool enable_trace = false;
  bool debug_mode = false;
  bool shall_exit = false;
  uint64_t pc = 0;
  uint64_t last_pc = 0;
  PrivilegeLevel prv = MachineMode;

  // Last decoded and executed instruction and opcode
  Instruction instr;
  Opcode::Mapping op;

  CoreExecStatus status = Runnable;

  std::unordered_set<uint64_t> breakpoints;
  bool single_step = false;

  template <uint32_t alignment, bool isLoad>
  void trap_check_addr_alignment(uint64_t addr);
  void trap_check_pc_alignment();
  bool is_invalid_csr_access(uint64_t csr_addr, bool is_write);
  void validate_csr_counter_read_access_rights(uint64_t addr);
  uint64_t pc_alignment_mask();
  uint64_t get_csr_value(uint64_t addr);
  void set_csr_value(uint64_t addr, uint64_t val);
  void return_from_trap_handler(PrivilegeLevel return_mode);
  void switch_to_trap_handler(PrivilegeLevel target_mode);
  PendingInterrupts compute_pending_interrupts();
  void prepare_interrupt(const PendingInterrupts &e);
  PrivilegeLevel prepare_trap(const SimulationTrap &e);
  void counter_update();

  void exec_step();
};

}  // namespace rv64
