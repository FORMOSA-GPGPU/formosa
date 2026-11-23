// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include "cores/scalar/scalar_core.h"

#include <liblv/binding.h>
#include <liblv/output.h>

#include "cores/decode.h"
#include "cores/exec_context.h"
#include "cores/exec_flag.h"
#include "cores/instr.h"

#define WITH_TRACER(code)   \
  do {                      \
    if (konata_tracer_) {   \
      konata_tracer_->code; \
    }                       \
  } while (0)

namespace simtix::scalar {

namespace {

// Select this hart's integer, CSR, and machine-control instructions.
#define AS_TUPLE(NAME, ...) std::tuple<NAME>{},
using ScalarInstructionSet = InstructionSet<
    decltype(std::tuple_cat(
        FOREACH_INSTR_NTL(AS_TUPLE) FOREACH_INSTR_OP(AS_TUPLE)
            FOREACH_INSTR_OP_IMM(AS_TUPLE) FOREACH_INSTR_OP32(AS_TUPLE)
                FOREACH_INSTR_OP_IMM32(AS_TUPLE) FOREACH_INSTR_BRANCH(AS_TUPLE)
                    FOREACH_INSTR_LUI(AS_TUPLE) FOREACH_INSTR_AUIPC(AS_TUPLE)
                        FOREACH_INSTR_JAL(AS_TUPLE) FOREACH_INSTR_JALR(AS_TUPLE)
                            FOREACH_INSTR_LOAD(AS_TUPLE) FOREACH_INSTR_STORE(
                                AS_TUPLE) FOREACH_INSTR_FENCE(AS_TUPLE)
                                FOREACH_INSTR_SYSTEM(AS_TUPLE) std::tuple<>{})),
    PrivilegedInstructions, std::tuple<EBREAK, FENCE_I>>;
#undef AS_TUPLE

constexpr uint64_t kMachineInterrupts = MIP_MEIP | MIP_MSIP | MIP_MTIP;

}  // namespace

void ScalarCore::before_end_of_elaboration() {
  // Unconnected interrupt inputs remain inactive.
  for (auto *port : {&ext_int, &sw_int, &timer_int}) {
    if (port->bind_count() == 0) port->bind(inactive_interrupt_);
  }
}

uint64_t ScalarCore::PendingInterrupts() const {
  return (ext_int.read() ? MIP_MEIP : 0) | (sw_int.read() ? MIP_MSIP : 0) |
         (timer_int.read() ? MIP_MTIP : 0);
}

bool ScalarCore::ReadCsr(uint32_t addr, uint64_t &value) const {
  switch (addr) {
    case CSR_MSTATUS:
      value = mstatus_;
      break;
    case CSR_MISA:
      value = (2ULL << 62) | (1 << ('I' - 'A')) | (1 << ('M' - 'A'));
      break;
    case CSR_MIE:
      value = mie_;
      break;
    case CSR_MTVEC:
      value = mtvec_;
      break;
    case CSR_MSCRATCH:
      value = mscratch_;
      break;
    case CSR_MEPC:
      value = mepc_;
      break;
    case CSR_MCAUSE:
      value = mcause_;
      break;
    case CSR_MTVAL:
      value = mtval_;
      break;
    case CSR_MIP:
      value = PendingInterrupts();
      break;
    case CSR_MVENDORID:
    case CSR_MARCHID:
    case CSR_MIMPID:
    case CSR_MHARTID:
      value = 0;
      break;
    default:
      return false;
  }
  return true;
}

void ScalarCore::WriteCsr(uint32_t addr, uint64_t value) {
  switch (addr) {
    case CSR_MSTATUS:
      mstatus_ = (value & (MSTATUS_MIE | MSTATUS_MPIE)) | MSTATUS_MPP;
      break;
    case CSR_MIE:
      mie_ = value & kMachineInterrupts;
      break;
    case CSR_MTVEC:
      // Support direct and vectored modes; reserved modes select direct.
      mtvec_ = (value & ~3ULL) | ((value & 3) == 1 ? 1 : 0);
      break;
    case CSR_MSCRATCH:
      mscratch_ = value;
      break;
    case CSR_MEPC:
      mepc_ = value & ~3ULL;
      break;
    case CSR_MCAUSE:
      mcause_ = value;
      break;
    case CSR_MTVAL:
      mtval_ = value;
      break;
    // MISA is fixed; pending interrupt bits are owned by their input lines.
    default:
      break;
  }
}

bool ScalarCore::ExecuteCsr(Packet *packet) {
  const auto &instr = packet->instr;
  const auto op = GetCsrOp(packet->flag);
  const bool write =
      op == ExecFlag::CSRRW ||
      (instr.rs1() == Instr::kNullReg ? instr.uimm() != 0 : instr.rs1() != 0);
  uint64_t old;
  if (!ReadCsr(packet->csr_buf, old) ||
      (write && (packet->csr_buf >> 10) == 3)) {
    return false;
  }
  if (write) {
    uint64_t value = packet->data_buf;
    if (op == ExecFlag::CSRRS) value = old | value;
    if (op == ExecFlag::CSRRC) value = old & ~value;
    WriteCsr(packet->csr_buf, value);
  }
  packet->data_buf = old;
  packet->flag = ExecFlag::RD_DATA;
  return true;
}

void ScalarCore::Discard(Packet *&packet) {
  if (!packet) return;
  WITH_TRACER(Flush(packet));
  packet_pool_.Release(packet);
  packet = nullptr;
}

void ScalarCore::Redirect(uint64_t pc) {
  pc_ = pc;
  flush_ = true;
  ++imem_epoch_;
  InvalidateOutstandingFetches();
}

void ScalarCore::EnterTrap(uint64_t cause, uint64_t epc, uint64_t tval,
                           bool interrupt) {
  mepc_ = epc & ~3ULL;
  mcause_ = cause | (interrupt ? 1ULL << 63 : 0);
  mtval_ = tval;
  mstatus_ = MSTATUS_MPP | ((mstatus_ & MSTATUS_MIE) ? MSTATUS_MPIE : 0);
  const uint64_t vector =
      (mtvec_ & ~3ULL) + (interrupt && (mtvec_ & 3) == 1 ? 4 * cause : 0);
  Redirect(vector);
}

void ScalarCore::enable_konata_trace(const std::string &path) {
  // Caveat: FileTracer's Quill sink is cached by path for the process lifetime.
  // Disabling then re-enabling with the SAME path appends to the existing
  // file (the second open is short-circuited by the cache). Use distinct
  // paths per simulation run if a fresh file is required.
  konata_tracer_.reset();
  try {
    konata_tracer_ =
        std::make_unique<konata::KonataTracer<Packet>>("konata_tracer", path);
    konata_tracer_->clock.bind(clock);
  } catch (const std::exception &e) {
    LV_WARNING("Failed to open Konata trace file {}: {}", path, e.what());
  }
}

void ScalarCore::disable_konata_trace() { konata_tracer_.reset(); }

void ScalarCore::Tick() {
  // Recompute control signals every cycle.
  stall_ = false;
  flush_ = false;

  Writeback();
  Memory();
  Execute();
  Decode();
  Fetch();
  UpdatePipelineRegisters();
}

void ScalarCore::UpdatePipelineRegisters() {
  if (packet_w_) {
    WITH_TRACER(Retire(packet_w_));
    packet_pool_.Release(packet_w_);
    packet_w_ = nullptr;
  }

  if (packet_m_ && !dmem_busy_ && !stall_) {
    packet_w_ = packet_m_;
    packet_m_ = nullptr;
    WITH_TRACER(StartStage(packet_w_, 0, "W"));
  }

  if (packet_e_) {
    if (stall_ || packet_m_) {
      stall_ = true;
      return;
    }

    packet_m_ = packet_e_;
    packet_e_ = nullptr;
    WITH_TRACER(StartStage(packet_m_, 0, "M"));
  }

  if (packet_d_) {
    if (flush_) {
      Discard(packet_d_);
    } else if (!stall_) {
      packet_e_ = packet_d_;
      packet_d_ = nullptr;
      WITH_TRACER(StartStage(packet_e_, 0, "E"));
    }
  }

  if (packet_f_) {
    if (flush_) {
      Discard(packet_f_);
    } else if (!stall_) {
      packet_d_ = packet_f_;
      packet_f_ = nullptr;
      WITH_TRACER(StartStage(packet_d_, 0, "D"));
    }
  }
}

void ScalarCore::Writeback() {
  if (!packet_w_) {
    return;
  }

  if (packet_w_->instr.rd() != Instr::kNullReg && packet_w_->instr.rd() != 0) {
    regfile_[packet_w_->instr.rd()] = packet_w_->data_buf;
  }
}

void ScalarCore::Memory() {
  if (!packet_m_) {
    return;
  }

  ExecFlag flag = packet_m_->flag;

  // Waiting for dmem response
  if (dmem_busy_) {
    tlm::tlm_generic_payload *resp;
    if (dmem_port_.resp_port->nb_read(resp)) {
      dmem_busy_ = false;
      if (resp->is_response_error()) {
        EnterTrap(HasFlag(flag, ExecFlag::LOAD) ? CAUSE_LOAD_ACCESS
                                                : CAUSE_STORE_ACCESS,
                  packet_m_->pc, packet_m_->addr_buf);
        Discard(packet_m_);
        Discard(packet_e_);
        return;
      }
      if (HasFlag(flag, ExecFlag::LOAD)) {
        SignExtension(packet_m_, GetMemSize(flag), IsSigned(flag));
      }
    }
    return;
  }

  // Forward new dmem transaction
  if (HasFlag(flag, ExecFlag::LOAD) || HasFlag(flag, ExecFlag::STORE)) {
    SetupDmemTrans(packet_m_,
                   HasFlag(flag, ExecFlag::LOAD) ? tlm::TLM_READ_COMMAND
                                                 : tlm::TLM_WRITE_COMMAND,
                   GetMemSize(flag));
    dmem_busy_ = dmem_port_.req_port->nb_write(&dmem_trans_);
    stall_ = !dmem_busy_;
  }
}

void ScalarCore::Execute() {
  if (!packet_e_) {
    return;
  }

  // Execute each instruction once, after the older memory operation has
  // completed. This also gives CSR writes and traps a precise boundary.
  if (stall_ || dmem_busy_) {
    stall_ = true;
    return;
  }

  const uint64_t pending = PendingInterrupts() & mie_;
  if ((mstatus_ & MSTATUS_MIE) && pending) {
    const uint64_t cause = pending & MIP_MEIP   ? IRQ_M_EXT
                           : pending & MIP_MSIP ? IRQ_M_SOFT
                                                : IRQ_M_TIMER;
    EnterTrap(cause, packet_e_->pc, 0, true);
    Discard(packet_e_);
    return;
  }

  if ((packet_e_->pc & 3) || packet_e_->fetch_fault) {
    EnterTrap((packet_e_->pc & 3) ? CAUSE_MISALIGNED_FETCH : CAUSE_FETCH_ACCESS,
              packet_e_->pc, packet_e_->pc);
    Discard(packet_e_);
    return;
  }

  const auto read_operand = [&](uint8_t reg_id) -> int64_t {
    if (reg_id == Instr::kNullReg || reg_id == 0) return 0;
    // Writeback has committed already; the completed memory stage is the
    // only older instruction whose destination still needs forwarding.
    if (packet_m_ && packet_m_->instr.rd() == reg_id &&
        HasFlag(packet_m_->flag, ExecFlag::RD_DATA | ExecFlag::LOAD)) {
      return packet_m_->data_buf;
    }
    return regfile_[reg_id];
  };
  packet_e_->rs1_data = read_operand(packet_e_->instr.rs1());
  packet_e_->rs2_data = read_operand(packet_e_->instr.rs2());
  packet_e_->rs3_data = read_operand(packet_e_->instr.rs3());

  const auto &instr = packet_e_->instr;
  ExecContext ctx = {
      .pc = packet_e_->pc,
      .tmask = dummy_tmask_,
      .rs1_data = &packet_e_->rs1_data,
      .rs2_data = &packet_e_->rs2_data,
      .rs3_data = &packet_e_->rs3_data,
      .num_lanes = 1,
      .rd_data = &packet_e_->data_buf,
      .next_pc = &packet_e_->addr_buf,
      .mem = {.addr = &packet_e_->addr_buf, .data = &packet_e_->data_buf},
      .csr = {.addr = &packet_e_->csr_buf, .data = &packet_e_->data_buf},
  };
  packet_e_->flag = simtix::Execute(&ctx, instr);

  if (HasFlag(packet_e_->flag, ExecFlag::MRET)) {
    mstatus_ = MSTATUS_MPP | MSTATUS_MPIE |
               ((mstatus_ & MSTATUS_MPIE) ? MSTATUS_MIE : 0);
    Redirect(mepc_);
    return;
  }
  // WFI retires without suspending this hart. The older data access has
  // drained, so FENCE needs no further action; FENCE.I also discards prefetched
  // code.
  if (HasFlag(packet_e_->flag,
              ExecFlag::WFI | ExecFlag::FENCE | ExecFlag::FENCE_I)) {
    if (HasFlag(packet_e_->flag, ExecFlag::FENCE_I))
      Redirect(packet_e_->pc + 4);
    return;
  }

  if (HasFlag(packet_e_->flag, ExecFlag::CSR)) {
    const bool executed = ExecuteCsr(packet_e_);
    if (!executed) packet_e_->flag = ExecFlag::ILLEGAL;
  }

  if (HasException(packet_e_->flag)) {
    const auto exception = GetException(packet_e_->flag);
    const uint64_t tval = exception == ExecFlag::ILLEGAL  ? packet_e_->iword
                          : exception == ExecFlag::ECALL  ? 0
                          : exception == ExecFlag::EBREAK ? packet_e_->pc
                                                          : packet_e_->addr_buf;
    EnterTrap(DecodeExceptionCause(packet_e_->flag), packet_e_->pc, tval);
    Discard(packet_e_);
    return;
  }

  if (HasFlag(packet_e_->flag, ExecFlag::NEXT_PC)) {
    uint64_t target_pc = packet_e_->addr_buf;
    if (target_pc != packet_e_->pc + 4) {
      Redirect(target_pc);
    }
  }
}

void ScalarCore::InvalidateOutstandingFetches() {
  while (!issued_fetch_entries_.empty()) {
    FetchEntry *entry = issued_fetch_entries_.front();
    issued_fetch_entries_.pop_front();
    entry->MarkDiscarded();

    if (Packet *packet = entry->ReleasePacket()) {
      WITH_TRACER(Flush(packet));
      packet_pool_.Release(packet);
    }

    if (!entry->inflight()) {
      entry->Reset();
      free_fetch_entries_.push(entry);
    }
  }
}

void ScalarCore::Decode() {
  if (!packet_d_) {
    return;
  }

  if (!packet_d_->decoded) {
    packet_d_->instr = simtix::Decode<ScalarInstructionSet>(packet_d_->iword);
    packet_d_->decoded = true;
    WITH_TRACER(AddLabel(
        packet_d_,
        fmt::format("{:#x} {}", packet_d_->pc, Disassemble(packet_d_->instr))));
  }
}

void ScalarCore::CollectFetchResponses() {
  while (imem_port_.resp_port->num_available() > 0) {
    auto *resp = imem_port_.resp_port->read();

    FetchEntryExtension *ext = nullptr;
    resp->get_extension(ext);
    assert(ext && ext->entry);

    FetchEntry *entry = ext->entry;
    entry->NotifyFill();

    if (entry->discard() || entry->epoch() != imem_epoch_) {
      if (Packet *packet = entry->ReleasePacket()) {
        WITH_TRACER(Flush(packet));
        packet_pool_.Release(packet);
      }
      entry->Reset();
      free_fetch_entries_.push(entry);
    }
  }
}

void ScalarCore::DeliverFetchedInstruction() {
  if (packet_f_ || issued_fetch_entries_.empty()) {
    return;
  }

  FetchEntry *entry = issued_fetch_entries_.front();
  if (!entry->ready()) {
    return;
  }

  issued_fetch_entries_.pop_front();
  packet_f_ = entry->ReleasePacket();
  entry->Reset();
  free_fetch_entries_.push(entry);
}

void ScalarCore::IssueFetchRequest() {
  if (flush_ || free_fetch_entries_.empty() ||
      imem_port_.req_port->num_free() == 0) {
    return;
  }

  FetchEntry *entry = free_fetch_entries_.front();
  free_fetch_entries_.pop();

  Packet *packet = packet_pool_.Acquire();
  packet->pc = pc_;
  packet->wid = 0;
  entry->Issue(packet, pc_, imem_epoch_);

  if (!imem_port_.req_port->nb_write(entry->payload())) {
    packet_pool_.Release(packet);
    entry->Reset();
    free_fetch_entries_.push(entry);
    return;
  }

  issued_fetch_entries_.push_back(entry);
  WITH_TRACER(Declare(packet));
  WITH_TRACER(StartStage(packet, 0, "F"));
  pc_ += 4;
}

void ScalarCore::Fetch() {
  CollectFetchResponses();
  DeliverFetchedInstruction();
  IssueFetchRequest();
}

#undef WITH_TRACER

LV_BINDING(simtix, ScalarCore)
    .constructor(
        [](const char *name) {
          return std::make_shared<ScalarCore>(name);
        },
        lv::params("name"), lv::doc("Create a scalar core"))
    .property("clock", &ScalarCore::set_clock, lv::doc("SystemC clock"))
    .property("imem", &ScalarCore::set_imem,
              lv::doc("Instruction memory target"))
    .property("dmem", &ScalarCore::set_dmem, lv::doc("Data memory target"))
    .property("pc", &ScalarCore::set_pc, lv::doc("Program counter"))
    .property("ext_int", &ScalarCore::ext_int,
              lv::doc("Machine external interrupt input"))
    .property("sw_int", &ScalarCore::sw_int,
              lv::doc("Machine software interrupt input"))
    .property("timer_int", &ScalarCore::timer_int,
              lv::doc("Machine timer interrupt input"))
    .method("enable_konata_trace", &ScalarCore::enable_konata_trace,
            lv::params("path"), lv::doc("Enable Konata trace output"))
    .method("disable_konata_trace", &ScalarCore::disable_konata_trace,
            lv::doc("Disable Konata trace output"));

}  // namespace simtix::scalar
