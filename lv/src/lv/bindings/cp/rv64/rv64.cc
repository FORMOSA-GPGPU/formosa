// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include "rv64.h"

#include <fmt/format.h>
#include <liblv/log.h>

#include <cassert>

#include "riscv.h"
#include "trap.h"

#define RAISE_ILLEGAL_INSTRUCTION() raise_trap(EXC_ILLEGAL_INSTR, instr.data());

namespace rv64 {

void Core::exec_step() {
  // assert(((pc & ~pc_alignment_mask()) == 0) && "misaligned instruction");

  uint32_t mem_word;
  try {
    mem_word = instr_mem->load_instr(pc);
    instr = Instruction(mem_word);
  } catch (SimulationTrap &e) {
    op = Opcode::UNDEF;
    instr = Instruction(0);
    throw;
  }

  if (instr.is_compressed()) {
    pc += 2;
  } else {
    pc += 4;
  }
  op = instr.decode();

  /* Trace instruction here */

  if (enable_trace) {
    LV_PRINTLN("PC: {:08x} Op: {}", last_pc, Opcode::mappingStr[op]);
  }

  switch (op) {
    case Opcode::UNDEF:
      raise_trap(EXC_ILLEGAL_INSTR, instr.data());
      break;

    case Opcode::ADDI:
      regs[instr.rd()] = regs[instr.rs1()] + instr.I_imm();
      break;

    case Opcode::SLTI:
      regs[instr.rd()] = regs[instr.rs1()] < instr.I_imm();
      break;

    case Opcode::SLTIU:
      regs[instr.rd()] =
          ((uint64_t)regs[instr.rs1()]) < ((uint64_t)instr.I_imm());
      break;

    case Opcode::XORI:
      regs[instr.rd()] = regs[instr.rs1()] ^ instr.I_imm();
      break;

    case Opcode::ORI:
      regs[instr.rd()] = regs[instr.rs1()] | instr.I_imm();
      break;

    case Opcode::ANDI:
      regs[instr.rd()] = regs[instr.rs1()] & instr.I_imm();
      break;

    case Opcode::ADD:
      regs[instr.rd()] = regs[instr.rs1()] + regs[instr.rs2()];
      break;

    case Opcode::SUB:
      regs[instr.rd()] = regs[instr.rs1()] - regs[instr.rs2()];
      break;

    case Opcode::SLL:
      regs[instr.rd()] = regs[instr.rs1()] << regs.shamt(instr.rs2());
      break;

    case Opcode::SLT:
      regs[instr.rd()] = regs[instr.rs1()] < regs[instr.rs2()];
      break;

    case Opcode::SLTU:
      regs[instr.rd()] =
          ((uint64_t)regs[instr.rs1()]) < ((uint64_t)regs[instr.rs2()]);
      break;

    case Opcode::SRL:
      regs[instr.rd()] =
          ((uint64_t)regs[instr.rs1()]) >> regs.shamt(instr.rs2());
      break;

    case Opcode::SRA:
      regs[instr.rd()] = regs[instr.rs1()] >> regs.shamt(instr.rs2());
      break;

    case Opcode::XOR:
      regs[instr.rd()] = regs[instr.rs1()] ^ regs[instr.rs2()];
      break;

    case Opcode::OR:
      regs[instr.rd()] = regs[instr.rs1()] | regs[instr.rs2()];
      break;

    case Opcode::AND:
      regs[instr.rd()] = regs[instr.rs1()] & regs[instr.rs2()];
      break;

    case Opcode::SLLI:
      regs[instr.rd()] = regs[instr.rs1()] << instr.shamt();
      break;

    case Opcode::SRLI:
      regs[instr.rd()] = ((uint64_t)regs[instr.rs1()]) >> instr.shamt();
      break;

    case Opcode::SRAI:
      regs[instr.rd()] = regs[instr.rs1()] >> instr.shamt();
      break;

    case Opcode::LUI:
      regs[instr.rd()] = instr.U_imm();
      break;

    case Opcode::AUIPC:
      regs[instr.rd()] = last_pc + instr.U_imm();
      break;

    case Opcode::JAL: {
      auto link = pc;
      pc = last_pc + instr.J_imm();
      trap_check_pc_alignment();
      regs[instr.rd()] = link;
    } break;

    case Opcode::JALR: {
      auto link = pc;
      pc = (regs[instr.rs1()] + instr.I_imm()) & ~1;
      trap_check_pc_alignment();
      regs[instr.rd()] = link;
    } break;

    case Opcode::SB: {
      uint64_t addr = regs[instr.rs1()] + instr.S_imm();
      data_mem->store_byte(addr, regs[instr.rs2()]);
    } break;

    case Opcode::SH: {
      uint64_t addr = regs[instr.rs1()] + instr.S_imm();
      trap_check_addr_alignment<2, false>(addr);
      data_mem->store_half(addr, regs[instr.rs2()]);
    } break;

    case Opcode::SW: {
      uint64_t addr = regs[instr.rs1()] + instr.S_imm();
      trap_check_addr_alignment<4, false>(addr);
      data_mem->store_word(addr, regs[instr.rs2()]);
    } break;

    case Opcode::SD: {
      uint64_t addr = regs[instr.rs1()] + instr.S_imm();
      trap_check_addr_alignment<8, false>(addr);
      data_mem->store_double(addr, regs[instr.rs2()]);
    } break;

    case Opcode::LB: {
      uint64_t addr = regs[instr.rs1()] + instr.I_imm();
      regs[instr.rd()] = data_mem->load_byte(addr);
    } break;

    case Opcode::LH: {
      uint64_t addr = regs[instr.rs1()] + instr.I_imm();
      trap_check_addr_alignment<2, true>(addr);
      regs[instr.rd()] = data_mem->load_half(addr);
    } break;

    case Opcode::LW: {
      uint64_t addr = regs[instr.rs1()] + instr.I_imm();
      trap_check_addr_alignment<4, true>(addr);
      regs[instr.rd()] = data_mem->load_word(addr);
    } break;

    case Opcode::LD: {
      uint64_t addr = regs[instr.rs1()] + instr.I_imm();
      trap_check_addr_alignment<8, true>(addr);
      regs[instr.rd()] = data_mem->load_double(addr);
    } break;

    case Opcode::LBU: {
      uint64_t addr = regs[instr.rs1()] + instr.I_imm();
      regs[instr.rd()] = data_mem->load_ubyte(addr);
    } break;

    case Opcode::LHU: {
      uint64_t addr = regs[instr.rs1()] + instr.I_imm();
      trap_check_addr_alignment<2, true>(addr);
      regs[instr.rd()] = data_mem->load_uhalf(addr);
    } break;

    case Opcode::LWU: {
      uint64_t addr = regs[instr.rs1()] + instr.I_imm();
      trap_check_addr_alignment<4, true>(addr);
      regs[instr.rd()] = data_mem->load_uword(addr);
    } break;

    case Opcode::BEQ:
      if (regs[instr.rs1()] == regs[instr.rs2()]) {
        pc = last_pc + instr.B_imm();
        trap_check_pc_alignment();
      }
      break;

    case Opcode::BNE:
      if (regs[instr.rs1()] != regs[instr.rs2()]) {
        pc = last_pc + instr.B_imm();
        trap_check_pc_alignment();
      }
      break;

    case Opcode::BLT:
      if (regs[instr.rs1()] < regs[instr.rs2()]) {
        pc = last_pc + instr.B_imm();
        trap_check_pc_alignment();
      }
      break;

    case Opcode::BGE:
      if (regs[instr.rs1()] >= regs[instr.rs2()]) {
        pc = last_pc + instr.B_imm();
        trap_check_pc_alignment();
      }
      break;

    case Opcode::BLTU:
      if ((uint64_t)regs[instr.rs1()] < (uint64_t)regs[instr.rs2()]) {
        pc = last_pc + instr.B_imm();
        trap_check_pc_alignment();
      }
      break;

    case Opcode::BGEU:
      if ((uint64_t)regs[instr.rs1()] >= (uint64_t)regs[instr.rs2()]) {
        pc = last_pc + instr.B_imm();
        trap_check_pc_alignment();
      }
      break;

    case Opcode::ADDIW:
      regs[instr.rd()] = (int32_t)regs[instr.rs1()] + (int32_t)instr.I_imm();
      break;

    case Opcode::SLLIW:
      regs[instr.rd()] =
          (int32_t)((uint32_t)regs[instr.rs1()] << instr.shamt_w());
      break;

    case Opcode::SRLIW:
      regs[instr.rd()] =
          (int32_t)(((uint32_t)regs[instr.rs1()]) >> instr.shamt_w());
      break;

    case Opcode::SRAIW:
      regs[instr.rd()] =
          (int32_t)((int32_t)regs[instr.rs1()] >> instr.shamt_w());
      break;

    case Opcode::ADDW:
      regs[instr.rd()] =
          (int32_t)regs[instr.rs1()] + (int32_t)regs[instr.rs2()];
      break;

    case Opcode::SUBW:
      regs[instr.rd()] =
          (int32_t)regs[instr.rs1()] - (int32_t)regs[instr.rs2()];
      break;

    case Opcode::SLLW:
      regs[instr.rd()] =
          (int32_t)((uint32_t)regs[instr.rs1()] << regs.shamt_w(instr.rs2()));
      break;

    case Opcode::SRLW:
      regs[instr.rd()] =
          (int32_t)(((uint32_t)regs[instr.rs1()]) >> regs.shamt_w(instr.rs2()));
      break;

    case Opcode::SRAW:
      regs[instr.rd()] =
          (int32_t)((int32_t)regs[instr.rs1()] >> regs.shamt_w(instr.rs2()));
      break;

    case Opcode::FENCE:
    case Opcode::FENCE_I: {
      // not using out of order execution/caches so can be ignored
    } break;

    case Opcode::ECALL: {
      switch (prv) {
        case MachineMode:
          raise_trap(EXC_ECALL_M_MODE, last_pc);
          break;
        case SupervisorMode:
          raise_trap(EXC_ECALL_S_MODE, last_pc);
          break;
        case UserMode:
          raise_trap(EXC_ECALL_U_MODE, last_pc);
          break;
        default:
          throw std::runtime_error("unknown privilege level " +
                                   std::to_string(prv));
      }
    } break;

    case Opcode::EBREAK: {
      // TODO: also raise trap and let the SW deal with it?
      status = CoreExecStatus::HitBreakpoint;
    } break;

    case Opcode::CSRRW: {
      auto addr = instr.csr();
      if (is_invalid_csr_access(addr, true)) {
        raise_trap(EXC_ILLEGAL_INSTR, instr.data());
      } else {
        auto rd = instr.rd();
        auto rs1_val = regs[instr.rs1()];
        if (rd != RegFile::zero) {
          regs[instr.rd()] = get_csr_value(addr);
        }
        set_csr_value(addr, rs1_val);
      }
    } break;

    case Opcode::CSRRS: {
      auto addr = instr.csr();
      auto rs1 = instr.rs1();
      auto write = rs1 != RegFile::zero;
      if (is_invalid_csr_access(addr, write)) {
        raise_trap(EXC_ILLEGAL_INSTR, instr.data());
      } else {
        auto rd = instr.rd();
        auto rs1_val = regs[rs1];
        auto csr_val = get_csr_value(addr);
        if (rd != RegFile::zero) regs[rd] = csr_val;
        if (write) set_csr_value(addr, csr_val | rs1_val);
      }
    } break;

    case Opcode::CSRRC: {
      auto addr = instr.csr();
      auto rs1 = instr.rs1();
      auto write = rs1 != RegFile::zero;
      if (is_invalid_csr_access(addr, write)) {
        raise_trap(EXC_ILLEGAL_INSTR, instr.data());
      } else {
        auto rd = instr.rd();
        auto rs1_val = regs[rs1];
        auto csr_val = get_csr_value(addr);
        if (rd != RegFile::zero) regs[rd] = csr_val;
        if (write) set_csr_value(addr, csr_val & ~rs1_val);
      }
    } break;

    case Opcode::CSRRWI: {
      auto addr = instr.csr();
      if (is_invalid_csr_access(addr, true)) {
        raise_trap(EXC_ILLEGAL_INSTR, instr.data());
      } else {
        auto rd = instr.rd();
        if (rd != RegFile::zero) {
          regs[rd] = get_csr_value(addr);
        }
        set_csr_value(addr, instr.zimm());
      }
    } break;

    case Opcode::CSRRSI: {
      auto addr = instr.csr();
      auto zimm = instr.zimm();
      auto write = zimm != 0;
      if (is_invalid_csr_access(addr, write)) {
        raise_trap(EXC_ILLEGAL_INSTR, instr.data());
      } else {
        auto csr_val = get_csr_value(addr);
        auto rd = instr.rd();
        if (rd != RegFile::zero) regs[rd] = csr_val;
        if (write) set_csr_value(addr, csr_val | zimm);
      }
    } break;

    case Opcode::CSRRCI: {
      auto addr = instr.csr();
      auto zimm = instr.zimm();
      auto write = zimm != 0;
      if (is_invalid_csr_access(addr, write)) {
        raise_trap(EXC_ILLEGAL_INSTR, instr.data());
      } else {
        auto csr_val = get_csr_value(addr);
        auto rd = instr.rd();
        if (rd != RegFile::zero) regs[rd] = csr_val;
        if (write) set_csr_value(addr, csr_val & ~zimm);
      }
    } break;

    case Opcode::MUL: {
      int128_t ans = (int128_t)regs[instr.rs1()] * (int128_t)regs[instr.rs2()];
      regs[instr.rd()] = (int64_t)ans;
    } break;

    case Opcode::MULH: {
      int128_t ans = (int128_t)regs[instr.rs1()] * (int128_t)regs[instr.rs2()];
      regs[instr.rd()] = ans >> 64;
    } break;

    case Opcode::MULHU: {
      int128_t ans = ((uint128_t)(uint64_t)regs[instr.rs1()]) *
                     (uint128_t)((uint64_t)regs[instr.rs2()]);
      regs[instr.rd()] = ans >> 64;
    } break;

    case Opcode::MULHSU: {
      int128_t ans = (int128_t)regs[instr.rs1()] *
                     (uint128_t)((uint64_t)regs[instr.rs2()]);
      regs[instr.rd()] = ans >> 64;
    } break;

    case Opcode::DIV: {
      auto a = regs[instr.rs1()];
      auto b = regs[instr.rs2()];
      if (b == 0) {
        regs[instr.rd()] = -1;
      } else if (a == INT64_MIN && b == -1) {
        regs[instr.rd()] = a;
      } else {
        regs[instr.rd()] = a / b;
      }
    } break;

    case Opcode::DIVU: {
      auto a = regs[instr.rs1()];
      auto b = regs[instr.rs2()];
      if (b == 0) {
        regs[instr.rd()] = -1;
      } else {
        regs[instr.rd()] = (uint64_t)a / (uint64_t)b;
      }
    } break;

    case Opcode::REM: {
      auto a = regs[instr.rs1()];
      auto b = regs[instr.rs2()];
      if (b == 0) {
        regs[instr.rd()] = a;
      } else if (a == INT64_MIN && b == -1) {
        regs[instr.rd()] = 0;
      } else {
        regs[instr.rd()] = a % b;
      }
    } break;

    case Opcode::REMU: {
      auto a = regs[instr.rs1()];
      auto b = regs[instr.rs2()];
      if (b == 0) {
        regs[instr.rd()] = a;
      } else {
        regs[instr.rd()] = (uint64_t)a % (uint64_t)b;
      }
    } break;

    case Opcode::MULW: {
      regs[instr.rd()] = (int32_t)(regs[instr.rs1()] * regs[instr.rs2()]);
    } break;

    case Opcode::DIVW: {
      int32_t a = regs[instr.rs1()];
      int32_t b = regs[instr.rs2()];
      if (b == 0) {
        regs[instr.rd()] = -1;
      } else if (a == INT32_MIN && b == -1) {
        regs[instr.rd()] = a;
      } else {
        regs[instr.rd()] = a / b;
      }
    } break;

    case Opcode::DIVUW: {
      int32_t a = regs[instr.rs1()];
      int32_t b = regs[instr.rs2()];
      if (b == 0) {
        regs[instr.rd()] = -1;
      } else {
        regs[instr.rd()] = (int32_t)((uint32_t)a / (uint32_t)b);
      }
    } break;

    case Opcode::REMW: {
      int32_t a = regs[instr.rs1()];
      int32_t b = regs[instr.rs2()];
      if (b == 0) {
        regs[instr.rd()] = a;
      } else if (a == INT32_MIN && b == -1) {
        regs[instr.rd()] = 0;
      } else {
        regs[instr.rd()] = a % b;
      }
    } break;

    case Opcode::REMUW: {
      int32_t a = regs[instr.rs1()];
      int32_t b = regs[instr.rs2()];
      if (b == 0) {
        regs[instr.rd()] = a;
      } else {
        regs[instr.rd()] = (int32_t)((uint32_t)a % (uint32_t)b);
      }
    } break;
      // privileged instructions

    case Opcode::WFI:
      // NOTE: only a hint, can be implemented as NOP
      // std::cout << "[sim:wfi] CSR mstatus.fields.mie " << csrs.mstatus->mie
      // << std::endl;
      // release_lr_sc_reservation();

      if (prv == SupervisorMode && csr.mstatus.fields.tw)
        raise_trap(EXC_ILLEGAL_INSTR, instr.data());

      if (prv == UserMode && csr.misa.has_supervisor_mode_extension())
        raise_trap(EXC_ILLEGAL_INSTR, instr.data());

      /*
      if (!ignore_wfi && !has_local_pending_enabled_interrupts())
        sc_core::wait(wfi_event);
      */
      break;

    case Opcode::SFENCE_VMA:
      if (prv == SupervisorMode && csr.mstatus.fields.tvm)
        raise_trap(EXC_ILLEGAL_INSTR, instr.data());
      // data_mem->flush_tlb();
      break;

    case Opcode::URET:
      if (!csr.misa.has_user_mode_extension())
        raise_trap(EXC_ILLEGAL_INSTR, instr.data());
      return_from_trap_handler(UserMode);
      break;

    case Opcode::SRET:
      if (!csr.misa.has_supervisor_mode_extension() ||
          (prv == SupervisorMode && csr.mstatus.fields.tsr))
        raise_trap(EXC_ILLEGAL_INSTR, instr.data());
      return_from_trap_handler(SupervisorMode);
      break;

    case Opcode::MRET:
      return_from_trap_handler(MachineMode);
      break;

    default:
      throw std::runtime_error("unknown opcode");
  }

  if (enable_trace) {
    LV_PRINTLN(" REG[{}]: 0x{:016x}", Opcode::regnamePrettyStr[instr.rd()],
               regs[instr.rd()]);
  }
}

void Core::trap_check_pc_alignment() {
  assert(!(pc & 0x1) &&
         "not possible due to immediate formats and jump execution");

  if (unlikely((pc & 0x3) && (!csr.misa.has_C_extension()))) {
    // NOTE: misaligned instruction address not possible on machines supporting
    // compressed instructions
    raise_trap(EXC_INSTR_ADDR_MISALIGNED, pc);
  }
}

template <uint32_t alignment, bool isLoad>
void Core::trap_check_addr_alignment(uint64_t addr) {
  if (unlikely(addr % alignment)) {
    raise_trap(
        isLoad ? EXC_LOAD_ADDR_MISALIGNED : EXC_STORE_AMO_ADDR_MISALIGNED,
        addr);
  }
}

bool Core::is_invalid_csr_access(uint64_t csr_addr, bool is_write) {
  PrivilegeLevel csr_prv = (0x300 & csr_addr) >> 8;
  bool csr_readonly = ((0xC00 & csr_addr) >> 10) == 3;
  return (is_write && csr_readonly) || (prv < csr_prv);
}

void Core::validate_csr_counter_read_access_rights(uint64_t addr) {
  // match against counter CSR addresses, see RISC-V privileged spec for the
  // address definitions
  if ((addr >= 0xC00 && addr <= 0xC1F)) {
    auto cnt = addr & 0x1F;  // 32 counter in total, naturally aligned with the
                             // mcounteren and scounteren CSRs

    if (prv == SupervisorMode && !csr::is_bitset(csr.mcounteren, cnt))
      RAISE_ILLEGAL_INSTRUCTION();

    if (prv == UserMode && (!csr::is_bitset(csr.mcounteren, cnt) ||
                            !csr::is_bitset(csr.scounteren, cnt)))
      RAISE_ILLEGAL_INSTRUCTION();
  }
}

uint64_t Core::get_csr_value(uint64_t addr) {
  validate_csr_counter_read_access_rights(addr);

  auto read = [=](auto &x, uint64_t mask) {
    return x.reg & mask;
  };

  using namespace csr;

  switch (addr) {
    case TIME_ADDR:
    case MTIME_ADDR: {
      /* No clint, must provide a way to update time reg */
      // uint64_t mtime = clint->update_and_get_mtime();
      // csr.time.reg = mtime;
      return csr.time.reg;
    }

    case MCYCLE_ADDR:
      /* The original implementation uses sc::time as counter, I prefer
       * decoupling systemc and core logic */
      // csr.cycle.reg = _compute_and_get_current_cycles();
      return csr.cycle.reg;

    case MINSTRET_ADDR:
      return csr.instret.reg;

    SWITCH_CASE_MATCH_ANY_HPMCOUNTER_RV64:  // not implemented
      return 0;

      // TODO: SD should be updated as SD=XS|FS and SD should be read-only ->
      // update mask
    case MSTATUS_ADDR:
      return read(csr.mstatus, MSTATUS_READ_MASK);
    case SSTATUS_ADDR:
      return read(csr.mstatus, SSTATUS_READ_MASK);
    case USTATUS_ADDR:
      return read(csr.mstatus, USTATUS_MASK);

    case MIP_ADDR:
      return read(csr.mip, MIP_READ_MASK);
    case SIP_ADDR:
      return read(csr.mip, SIP_MASK);
    case UIP_ADDR:
      return read(csr.mip, UIP_MASK);

    case MIE_ADDR:
      return read(csr.mie, MIE_MASK);
    case SIE_ADDR:
      return read(csr.mie, SIE_MASK);
    case UIE_ADDR:
      return read(csr.mie, UIE_MASK);

    case SATP_ADDR:
      if (csr.mstatus.fields.tvm) RAISE_ILLEGAL_INSTRUCTION();
      break;

    case FCSR_ADDR:
      return read(csr.fcsr, FCSR_MASK);

    case FFLAGS_ADDR:
      return csr.fcsr.fields.fflags;

    case FRM_ADDR:
      return csr.fcsr.fields.frm;

    case MHARTID_ADDR:
      return 0;
  }

  if (!csr.is_valid_csr64_addr(addr)) RAISE_ILLEGAL_INSTRUCTION();

  return csr.default_read64(addr);
}

uint64_t Core::pc_alignment_mask() {
  if (csr.misa.has_C_extension())
    return ~uint64_t(0x1);
  else
    return ~uint64_t(0x3);
}

void Core::set_csr_value(uint64_t addr, uint64_t val) {
  auto write = [=](auto &x, uint64_t mask) {
    x.reg = (x.reg & ~mask) | (val & mask);
  };

  using namespace csr;

  switch (addr) {
    case MISA_ADDR:  // currently, read-only, thus cannot be changed at runtime
    SWITCH_CASE_MATCH_ANY_HPMCOUNTER_RV64:  // not implemented
      break;

    case SATP_ADDR: {
      if (csr.mstatus.fields.tvm) RAISE_ILLEGAL_INSTRUCTION();
      auto mode = csr.satp.fields.mode;
      write(csr.satp, SATP_MASK);
      if (csr.satp.fields.mode != SATP_MODE_BARE &&
          csr.satp.fields.mode != SATP_MODE_SV39 &&
          csr.satp.fields.mode != SATP_MODE_SV48)
        csr.satp.fields.mode = mode;
      // std::cout << "[iss] satp=" << boost::format("%x") %
      // csrs.satp.fields.reg << std::endl;
    } break;

    case MTVEC_ADDR:
      write(csr.mtvec, MTVEC_MASK);
      break;
    case STVEC_ADDR:
      write(csr.stvec, MTVEC_MASK);
      break;
    case UTVEC_ADDR:
      write(csr.utvec, MTVEC_MASK);
      break;

    case MEPC_ADDR:
      write(csr.mepc, pc_alignment_mask());
      break;
    case SEPC_ADDR:
      write(csr.sepc, pc_alignment_mask());
      break;
    case UEPC_ADDR:
      write(csr.uepc, pc_alignment_mask());
      break;

    case MSTATUS_ADDR:
      write(csr.mstatus, MSTATUS_WRITE_MASK);
      break;
    case SSTATUS_ADDR:
      write(csr.mstatus, SSTATUS_WRITE_MASK);
      break;
    case USTATUS_ADDR:
      write(csr.mstatus, USTATUS_MASK);
      break;

    case MIP_ADDR:
      write(csr.mip, MIP_WRITE_MASK);
      break;
    case SIP_ADDR:
      write(csr.mip, SIP_MASK);
      break;
    case UIP_ADDR:
      write(csr.mip, UIP_MASK);
      break;

    case MIE_ADDR:
      write(csr.mie, MIE_MASK);
      break;
    case SIE_ADDR:
      write(csr.mie, SIE_MASK);
      break;
    case UIE_ADDR:
      write(csr.mie, UIE_MASK);
      break;

    case MIDELEG_ADDR:
      write(csr.mideleg, MIDELEG_MASK);
      break;

    case MEDELEG_ADDR:
      write(csr.medeleg, MEDELEG_MASK);
      break;

    case SIDELEG_ADDR:
      write(csr.sideleg, SIDELEG_MASK);
      break;

    case SEDELEG_ADDR:
      write(csr.sedeleg, SEDELEG_MASK);
      break;

    case MCOUNTEREN_ADDR:
      write(csr.mcounteren, MCOUNTEREN_MASK);
      break;

    case SCOUNTEREN_ADDR:
      write(csr.scounteren, MCOUNTEREN_MASK);
      break;

    case MCOUNTINHIBIT_ADDR:
      write(csr.mcountinhibit, MCOUNTINHIBIT_MASK);
      break;

    case FCSR_ADDR:
      write(csr.fcsr, FCSR_MASK);
      break;

    case FFLAGS_ADDR:
      csr.fcsr.fields.fflags = val;
      break;

    case FRM_ADDR:
      csr.fcsr.fields.frm = val;
      break;

    default:
      if (!csr.is_valid_csr64_addr(addr)) RAISE_ILLEGAL_INSTRUCTION();

      csr.default_write64(addr, val);
  }
}

void Core::return_from_trap_handler(PrivilegeLevel return_mode) {
  switch (return_mode) {
    case MachineMode:
      prv = csr.mstatus.fields.mpp;
      csr.mstatus.fields.mie = csr.mstatus.fields.mpie;
      csr.mstatus.fields.mpie = 1;
      pc = csr.mepc.reg;
      if (csr.misa.has_user_mode_extension())
        csr.mstatus.fields.mpp = UserMode;
      else
        csr.mstatus.fields.mpp = MachineMode;
      break;

    case SupervisorMode:
      prv = csr.mstatus.fields.spp;
      csr.mstatus.fields.sie = csr.mstatus.fields.spie;
      csr.mstatus.fields.spie = 1;
      pc = csr.sepc.reg;
      if (csr.misa.has_user_mode_extension())
        csr.mstatus.fields.spp = UserMode;
      else
        csr.mstatus.fields.spp = SupervisorMode;
      break;

    case UserMode:
      prv = UserMode;
      csr.mstatus.fields.uie = csr.mstatus.fields.upie;
      csr.mstatus.fields.upie = 1;
      pc = csr.uepc.reg;
      break;

    default:
      throw std::runtime_error("unknown privilege level " +
                               std::to_string(return_mode));
  }
}

void Core::switch_to_trap_handler(PrivilegeLevel target_mode) {
  auto pp = prv;
  prv = target_mode;

  switch (target_mode) {
    case MachineMode:
      csr.mepc.reg = pc;

      csr.mstatus.fields.mpie = csr.mstatus.fields.mie;
      csr.mstatus.fields.mie = 0;
      csr.mstatus.fields.mpp = pp;

      pc = csr.mtvec.get_base_address();

      if (csr.mcause.fields.interrupt &&
          csr.mtvec.fields.mode == csr_mtvec::Mode::Vectored)
        pc += 4 * csr.mcause.fields.exception_code;
      break;

    case SupervisorMode:
      assert(prv == SupervisorMode || prv == UserMode);

      csr.sepc.reg = pc;

      csr.mstatus.fields.spie = csr.mstatus.fields.sie;
      csr.mstatus.fields.sie = 0;
      csr.mstatus.fields.spp = pp;

      pc = csr.stvec.get_base_address();

      if (csr.scause.fields.interrupt &&
          csr.stvec.fields.mode == csr_mtvec::Mode::Vectored)
        pc += 4 * csr.scause.fields.exception_code;
      break;

    case UserMode:
      assert(prv == UserMode);

      csr.uepc.reg = pc;

      csr.mstatus.fields.upie = csr.mstatus.fields.uie;
      csr.mstatus.fields.uie = 0;

      pc = csr.utvec.get_base_address();

      if (csr.ucause.fields.interrupt &&
          csr.utvec.fields.mode == csr_mtvec::Mode::Vectored)
        pc += 4 * csr.ucause.fields.exception_code;
      break;

    default:
      throw std::runtime_error("unknown privilege level " +
                               std::to_string(target_mode));
  }
}

void Core::run_step() {
  assert(regs.read(0) == 0);

  if (debug_mode && (breakpoints.find(pc) != breakpoints.end())) {
    status = CoreExecStatus::HitBreakpoint;
    return;
  }

  last_pc = pc;
  try {
    exec_step();

    auto x = compute_pending_interrupts();
    if (x.target_mode != NoneMode) {
      prepare_interrupt(x);
      switch_to_trap_handler(x.target_mode);
    }
  } catch (SimulationTrap &e) {
    auto target_mode = prepare_trap(e);
    switch_to_trap_handler(target_mode);
  }

  // NOTE: writes to zero register are supposedly allowed but must be ignored
  // (reset it after every instruction, instead of checking *rd != zero*
  // before every register write)
  regs.write(0, 0);

  // Do not use a check *pc == last_pc* here. The reason is that due to
  // interrupts *pc* can be set to *last_pc* accidentally (when jumping back
  // to *mepc*).
  if (shall_exit) status = CoreExecStatus::Terminated;

  counter_update();
  if (single_step) {
    status = CoreExecStatus::HitBreakpoint;
    single_step = false;
  }
}

void Core::counter_update() {
  if (!csr.mcountinhibit.fields.IR) csr.instret.reg++;
  /* Update cycle counters here */
}

void Core::ext_interrupt(bool b) { csr.mip.fields.meip = b; }

void Core::sw_interrupt(bool b) { csr.mip.fields.msip = b; }

void Core::timer_interrupt(bool b) { csr.mip.fields.mtip = b; }

PendingInterrupts Core::compute_pending_interrupts() {
  uint64_t pending = csr.mie.reg & csr.mip.reg;

  if (!pending) return {NoneMode, 0};

  auto m_pending = pending & ~csr.mideleg.reg;
  if (m_pending &&
      (prv < MachineMode || (prv == MachineMode && csr.mstatus.fields.mie))) {
    return {MachineMode, m_pending};
  }

  pending = pending & csr.mideleg.reg;
  auto s_pending = pending & ~csr.sideleg.reg;
  if (s_pending && (prv < SupervisorMode ||
                    (prv == SupervisorMode && csr.mstatus.fields.sie))) {
    return {SupervisorMode, s_pending};
  }

  auto u_pending = pending & csr.sideleg.reg;
  if (u_pending && (prv == UserMode && csr.mstatus.fields.uie)) {
    return {UserMode, u_pending};
  }

  return {NoneMode, 0};
}

void Core::prepare_interrupt(const PendingInterrupts &e) {
  csr_mip x{e.pending};

  ExceptionCode exc;
  if (x.fields.meip)
    exc = EXC_M_EXTERNAL_INTERRUPT;
  else if (x.fields.msip)
    exc = EXC_M_SOFTWARE_INTERRUPT;
  else if (x.fields.mtip)
    exc = EXC_M_TIMER_INTERRUPT;
  else if (x.fields.seip)
    exc = EXC_S_EXTERNAL_INTERRUPT;
  else if (x.fields.ssip)
    exc = EXC_S_SOFTWARE_INTERRUPT;
  else if (x.fields.stip)
    exc = EXC_S_TIMER_INTERRUPT;
  else if (x.fields.ueip)
    exc = EXC_U_EXTERNAL_INTERRUPT;
  else if (x.fields.usip)
    exc = EXC_U_SOFTWARE_INTERRUPT;
  else if (x.fields.utip)
    exc = EXC_U_TIMER_INTERRUPT;
  else
    throw std::runtime_error("some pending interrupt must be available here");

  switch (e.target_mode) {
    case MachineMode:
      csr.mcause.fields.exception_code = exc;
      csr.mcause.fields.interrupt = 1;
      break;

    case SupervisorMode:
      csr.scause.fields.exception_code = exc;
      csr.scause.fields.interrupt = 1;
      break;

    case UserMode:
      csr.ucause.fields.exception_code = exc;
      csr.ucause.fields.interrupt = 1;
      break;

    default:
      throw std::runtime_error("unknown privilege level " +
                               std::to_string(e.target_mode));
  }
}

PrivilegeLevel Core::prepare_trap(const SimulationTrap &e) {
  // undo any potential pc update (for traps the pc should point to the
  // originating instruction and not it's successor)
  pc = last_pc;
  unsigned exc_bit = (1 << e.reason);

  // 1) machine mode execution takes any traps, independent of delegation
  // setting 2) non-delegated traps are processed in machine mode, independent
  // of current execution mode
  if (prv == MachineMode || !(exc_bit & csr.medeleg.reg)) {
    csr.mcause.fields.interrupt = 0;
    csr.mcause.fields.exception_code = e.reason;
    csr.mtval.reg = e.mtval;
    return MachineMode;
  }

  // see above machine mode comment
  if (prv == SupervisorMode || !(exc_bit & csr.sedeleg.reg)) {
    csr.scause.fields.interrupt = 0;
    csr.scause.fields.exception_code = e.reason;
    csr.stval.reg = e.mtval;
    return SupervisorMode;
  }

  assert(prv == UserMode && (exc_bit & csr.medeleg.reg) &&
         (exc_bit & csr.sedeleg.reg));
  csr.ucause.fields.interrupt = 0;
  csr.ucause.fields.exception_code = e.reason;
  csr.utval.reg = e.mtval;
  return UserMode;
}

void Core::run() {
  do {
    run_step();
  } while (status == CoreExecStatus::Runnable);
}

Core::Core(uint64_t hart_id, bool trace) {
  csr.mhartid.reg = hart_id;
  op = Opcode::Mapping::UNDEF;
  enable_trace = trace;

  /* Reset counter here */
}

Core::Core(uint64_t hart_id) {
  csr.mhartid.reg = hart_id;
  op = Opcode::Mapping::UNDEF;
  enable_trace = false;

  /* Reset counter here */
}

void Core::set_instr_mem(instr_memory_if *imem) { instr_mem = imem; }

void Core::set_data_mem(data_memory_if *dmem) { data_mem = dmem; }

void Core::set_pc(uint64_t pc) {
  this->pc = pc;
  this->last_pc = pc;
};

void Core::enable_debug() { debug_mode = true; }
void Core::disable_debug() { debug_mode = false; }
void Core::set_single_step() { single_step = true; }

CoreExecStatus Core::get_status() { return status; }
void Core::set_status(CoreExecStatus s) { status = s; }

void Core::insert_breakpoint(uint64_t pc) { breakpoints.insert(pc); }
void Core::remove_breakpoint(uint64_t pc) { breakpoints.erase(pc); }

uint64_t Core::get_program_counter() { return pc; }
std::array<int64_t, 32> Core::get_registers() { return regs.get_regs(); }
uint64_t Core::read_register(unsigned addr) { return regs[addr]; }
void Core::write_register(unsigned addr, uint64_t val) { regs[addr] = val; }

uint8_t Core::load_byte(uint64_t addr) {
  return static_cast<uint8_t>(data_mem->load_ubyte(addr));
}
void Core::store_byte(uint64_t addr, uint8_t value) {
  data_mem->store_byte(addr, value);
}

}  // namespace rv64
