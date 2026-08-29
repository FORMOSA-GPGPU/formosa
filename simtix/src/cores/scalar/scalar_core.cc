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

  if (packet_m_ && !dmem_busy_) {
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
      WITH_TRACER(Flush(packet_d_));
      packet_pool_.Release(packet_d_);
      packet_d_ = nullptr;
    } else if (!stall_) {
      packet_e_ = packet_d_;
      packet_d_ = nullptr;
      WITH_TRACER(StartStage(packet_e_, 0, "E"));
    }
  }

  if (packet_f_) {
    if (flush_) {
      WITH_TRACER(Flush(packet_f_));
      packet_pool_.Release(packet_f_);
      packet_f_ = nullptr;
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
    dmem_busy_ = dmem_port_.req_port->nb_write(&dmem_trans_) ? true : false;
  }
}

void ScalarCore::Execute() {
  if (!packet_e_) {
    return;
  }

  const auto has_matching_destination = [](const auto *packet, uint8_t reg_id) {
    return packet != nullptr && packet->instr.rd() != Instr::kNullReg &&
           packet->instr.rd() == reg_id;
  };

  const auto get_source_op = [&](uint8_t reg_id, int64_t *data_buf) {
    if (data_buf == nullptr || reg_id == Instr::kNullReg || reg_id == 0) {
      return;
    }

    // Forward from the writeback stage.
    if (has_matching_destination(packet_w_, reg_id)) {
      *data_buf = packet_w_->data_buf;
    }

    // Forward from the memory stage.
    if (!has_matching_destination(packet_m_, reg_id)) {
      return;
    }

    if (HasFlag(packet_m_->flag, ExecFlag::RD_DATA)) {
      *data_buf = packet_m_->data_buf;
      return;
    }

    if (!HasFlag(packet_m_->flag, ExecFlag::LOAD)) {
      return;
    }

    if (dmem_busy_) {
      stall_ = true;
      return;
    }

    *data_buf = packet_m_->data_buf;
  };

  get_source_op(packet_e_->instr.rs1(), &packet_e_->rs1_data);
  get_source_op(packet_e_->instr.rs2(), &packet_e_->rs2_data);
  get_source_op(packet_e_->instr.rs3(), &packet_e_->rs3_data);

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
  packet_e_->flag = simtix::Execute(&ctx, packet_e_->instr);

  if (HasFlag(packet_e_->flag, ExecFlag::NEXT_PC)) {
    uint64_t target_pc = packet_e_->addr_buf;
    if (target_pc != packet_e_->pc + 4) {
      pc_ = target_pc;
      flush_ = true;
      ++imem_epoch_;
      InvalidateOutstandingFetches();
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
    packet_d_->instr = simtix::Decode(packet_d_->iword);
    packet_d_->decoded = true;
    WITH_TRACER(AddMnemonic(
        packet_d_,
        fmt::format("{:#x} {}", packet_d_->pc, Mnemonic(packet_d_->instr))));
  }

  auto get_source_op = [&](uint8_t reg_id, int64_t &data_buf) {
    if (reg_id == Instr::kNullReg || reg_id == 0) {
      data_buf = 0;
      return;
    }

    // No hazard, read from regfile
    data_buf = regfile_[reg_id];
  };

  get_source_op(packet_d_->instr.rs1(), packet_d_->rs1_data);
  get_source_op(packet_d_->instr.rs2(), packet_d_->rs2_data);
  get_source_op(packet_d_->instr.rs3(), packet_d_->rs3_data);
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
    .method("enable_konata_trace", &ScalarCore::enable_konata_trace,
            lv::params("path"), lv::doc("Enable Konata trace output"))
    .method("disable_konata_trace", &ScalarCore::disable_konata_trace,
            lv::doc("Disable Konata trace output"));

}  // namespace simtix::scalar
