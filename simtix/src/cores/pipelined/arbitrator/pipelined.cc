// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include "cores/pipelined/arbitrator/pipelined.h"

#include <liblv/binding.h>

#define WITH_TRACER(code)            \
  do {                               \
    if (auto *t = core_->tracer()) { \
      t->code;                       \
    }                                \
  } while (0)

namespace simtix::pipelined {

PipelinedArbitrator::PipelinedArbitrator(const sc_module_name &name,
                                         const ArchParam &p, const Param &pp)
    : ArbitratorIntf(name),
      num_lanes_(p.num_lanes),
      rf_arch_(pp.rf_arch),
      num_regfile_banks_(pp.num_regfile_banks),
      num_subcores_(pp.num_subcores),
      num_shared_ports_(pp.num_shared_ports),
      num_read_ports_(pp.num_read_ports),
      num_write_ports_(pp.num_write_ports),
      scalar_regfile_(nullptr),
      working_read_collect_unit_ids_(),
      working_write_collect_unit_ids_(),
      read_collect_units_(pp.num_read_collect_units),
      write_collect_units_(pp.num_write_collect_units),
      stats_(name) {
  if (pp.num_shared_ports) {
    if (pp.num_write_ports || pp.num_read_ports) {
      LV_WARNING(
          "num_write_ports and num_read_ports are ignored when "
          "num_shared_ports is set");
    }
    // Both read and write share the same request buffer (req_buf_[0])
    read_req_buf_ = &req_buf_[0];
    write_req_buf_ = &req_buf_[0];

    bank_access_cnt_[0].resize(pp.num_regfile_banks, 0);
    read_bank_access_cnt_ = &bank_access_cnt_[0];
    write_bank_access_cnt_ = &bank_access_cnt_[0];

    // { S, S, ..., S }
    //   ^~~~~~~~~~~~ num_shared_ports
    req_buf_arr_.assign(pp.num_shared_ports, &req_buf_[0]);
    bank_access_cnt_arr_.assign(pp.num_shared_ports, &bank_access_cnt_[0]);

    port_utilized_.resize(num_shared_ports_);
  } else {
    // Read and write use different request buffers (req_buf_[0], and [1])
    read_req_buf_ = &req_buf_[0];
    write_req_buf_ = &req_buf_[1];

    bank_access_cnt_[0].resize(pp.num_regfile_banks, 0);
    bank_access_cnt_[1].resize(pp.num_regfile_banks, 0);
    read_bank_access_cnt_ = &bank_access_cnt_[0];
    write_bank_access_cnt_ = &bank_access_cnt_[1];

    // Write must be considered first during arbitration
    // { W, W, ..., W, W, R, R, ..., R }
    //   ^~~~~~~~~~~~~~~  ^~~~~~~~~~~~
    //   num_write_ports  num_read_ports
    req_buf_arr_.assign(pp.num_write_ports, write_req_buf_);
    req_buf_arr_.insert(req_buf_arr_.end(), pp.num_read_ports, read_req_buf_);

    bank_access_cnt_arr_.assign(pp.num_write_ports, write_bank_access_cnt_);
    bank_access_cnt_arr_.insert(bank_access_cnt_arr_.end(), pp.num_read_ports,
                                read_bank_access_cnt_);

    port_utilized_.resize(num_read_ports_ + num_write_ports_);
  }

  // Collect units
  for (uint32_t i = 0; i < pp.num_read_collect_units; ++i) {
    free_read_collect_unit_ids_.push(i);
  }
  for (uint32_t i = 0; i < pp.num_write_collect_units; ++i) {
    free_write_collect_unit_ids_.push(i);
  }

  if (rf_arch_ == RFArch::kDuplicateSRF) {
    scalar_regfile_ =
        std::make_unique<ScalarRegFile>(p.num_warps / pp.num_subcores);
  }

  SC_METHOD(Tick);
  sensitive << clock.pos();
  dont_initialize();

  // Perfetto Tracing
  rcu_tracks_.reserve(pp.num_read_collect_units);
  wcu_tracks_.reserve(pp.num_write_collect_units);

  for (uint32_t i = 0; i < pp.num_read_collect_units; ++i) {
    rcu_tracks_.emplace_back(LV_NEW_MODULE_TRACK(fmt::format("RCU {}", i)));
    rcu_tracks_.back().set_enabled(pp.pftrace);
  }
  for (uint32_t i = 0; i < pp.num_write_collect_units; ++i) {
    wcu_tracks_.emplace_back(LV_NEW_MODULE_TRACK(fmt::format("WCU {}", i)));
    wcu_tracks_.back().set_enabled(pp.pftrace);
  }

  bank_conflict_tracks_.reserve(pp.num_regfile_banks);

  for (uint32_t i = 0; i < pp.num_regfile_banks; ++i) {
    bank_conflict_tracks_.emplace_back(
        LV_NEW_MODULE_TRACK(fmt::format("Bank {}", i)));
    bank_conflict_tracks_.back().set_enabled(pp.pftrace);
  }
}

uint32_t PipelinedArbitrator::ToRegfileBank(uint32_t wid,
                                            uint32_t reg_id) const {
  return (wid + reg_id) % num_regfile_banks_;
}

std::tuple<bool, int64_t, int64_t> PipelinedArbitrator::AffineDetection(
    const std::vector<int64_t> &data_buf, const sc_bv_base &tmask) {
  // Fig. 2(a), step 2: detect whether the writeback vector can be represented
  // compactly as uniform/affine.
  if (data_buf.empty() || !tmask.and_reduce()) {
    return {false, 0, 0};
  }

  int64_t base = data_buf[0];
  int64_t stride = data_buf[1] - data_buf[0];
  for (uint32_t i = 2; i < num_lanes_; ++i) {
    if (data_buf[i] - data_buf[i - 1] != stride) {
      return {false, 0, 0};
    }
  }
  return {true, base, stride};
}

void PipelinedArbitrator::AllocateReadCollectUnit() {
  if (free_read_collect_unit_ids_.empty()) {
    return;
  }

  Packet *packet = nullptr;
  if (!operand_collect_req->nb_get(packet)) {
    return;
  }
  WITH_TRACER(StartStage(packet, 0, "Oc"));

  uint32_t cu_id = free_read_collect_unit_ids_.front();
  free_read_collect_unit_ids_.pop();
  working_read_collect_unit_ids_.push_back(cu_id);

  auto &cu = read_collect_units_[cu_id];
  cu.Allocate(packet);
  LV_TRACE_BEGIN(
      rcu_tracks_[cu_id], "Arbitrator", "Arbitrate",
      LV_TRACE_ARG("unique_id", packet->unique_id),
      LV_TRACE_ARG("wid", packet->wid),
      LV_TRACE_ARG("mnemonic", fmt::format("{}", Mnemonic(packet->instr))));

  if (rf_arch_ == RFArch::kDuplicateSRF) {
    assert(scalar_regfile_ != nullptr);

    // Fig. 2(b): scalar lookup followed by decompression. If the SRF entry is
    // compressed, expand base + lane * stride locally and bypass the vector
    // regfile access.
    auto srf_read = [&](bool ready, uint8_t reg_id, int64_t *data_buf,
                        OpPosition op_pos) {
      if (ready) {
        return;
      }

      const ScalarEntry &srf_entry =
          scalar_regfile_->Read(local_wid(packet->wid), reg_id);
      if (srf_entry.IsCompressed()) {
        for (uint32_t i = 0; i < num_lanes_; ++i) {
          data_buf[i] = srf_entry.base + i * srf_entry.stride;
        }
        cu.SetSourceKind(op_pos, srf_entry.kind);
        cu.SetReady(op_pos);
        LV_TRACE_INSTANT(
            rcu_tracks_[cu_id], "Arbitrator",
            fmt::format("{} Ready (SRF)", OpPositionToString(op_pos)),
            LV_TRACE_ARG("reg_id", reg_id));
      }
    };

    srf_read(cu.rs1_ready(), packet->instr.rs1(), packet->rs1_data.data(),
             OpPosition::kRs1);
    srf_read(cu.rs2_ready(), packet->instr.rs2(), packet->rs2_data.data(),
             OpPosition::kRs2);
    srf_read(cu.rs3_ready(), packet->instr.rs3(), packet->rs3_data.data(),
             OpPosition::kRs3);
  }

  auto enqueue_req = [&](bool ready, int64_t *data_ptr, OpPosition op_pos,
                         uint8_t reg_id) {
    if (!ready) {
      read_req_buf_->emplace_back(cu_id, packet->wid, &packet->tmask, data_ptr,
                                  ToRegfileBank(packet->wid, reg_id), op_pos,
                                  reg_id, false);
    }
  };

  enqueue_req(cu.rs1_ready(), packet->rs1_data.data(), OpPosition::kRs1,
              packet->instr.rs1());
  enqueue_req(cu.rs2_ready(), packet->rs2_data.data(), OpPosition::kRs2,
              packet->instr.rs2());
  enqueue_req(cu.rs3_ready(), packet->rs3_data.data(), OpPosition::kRs3,
              packet->instr.rs3());
}

void PipelinedArbitrator::AllocateWriteCollectUnit() {
  if (free_write_collect_unit_ids_.empty()) {
    return;
  }

  Packet *packet = nullptr;
  if (!writeback_req->nb_get(packet)) {
    return;
  }
  WITH_TRACER(StartStage(packet, 0, "W"));

  uint32_t cu_id = free_write_collect_unit_ids_.front();
  free_write_collect_unit_ids_.pop();
  working_write_collect_unit_ids_.push_back(cu_id);

  auto &cu = write_collect_units_[cu_id];
  cu.Allocate(packet);
  LV_TRACE_BEGIN(
      wcu_tracks_[cu_id], "Arbitrator", "Arbitrate",
      LV_TRACE_ARG("unique_id", packet->unique_id),
      LV_TRACE_ARG("wid", packet->wid),
      LV_TRACE_ARG("mnemonic", fmt::format("{}", Mnemonic(packet->instr))));

  const uint32_t wid = packet->wid;
  const uint8_t rd = packet->instr.rd();

  if (rf_arch_ == RFArch::kDuplicateSRF && !cu.rd_ready()) {
    assert(scalar_regfile_ != nullptr);

    // Fig. 2(a): read the old SRF metadata first, then run affine detection on
    // the new writeback vector to decide whether the destination stays compact
    // in SRF or falls back to the normal vector regfile path.
    const ScalarEntry old_entry = scalar_regfile_->Read(local_wid(wid), rd);
    auto [new_compressed, base, stride] =
        AffineDetection(packet->data_buf, packet->tmask);

    if (new_compressed) {
      // Fig. 2(a), step 3a: write a compact SRF entry for a uniform/affine
      // result. This model keeps the full vector regfile image coherent by
      // also updating the underlying core regfile.
      scalar_regfile_->WriteCompressed(local_wid(wid), rd, base, stride);

      core_->WriteRegFile(packet->data_buf.data(), packet->wid,
                          packet->instr.rd(), packet->tmask);

      cu.SetReady(OpPosition::kRd);
      LV_TRACE_INSTANT(wcu_tracks_[cu_id], "Arbitrator", "Rd Ready (SRF)",
                       LV_TRACE_ARG("reg_id", rd));
    } else {
      // Fig. 2(a), steps 3b/3c in simplified form: if the old value was
      // compressed, expand inactive lanes before falling back to vector
      // storage. This implementation marks the SRF entry as vector and uses
      // the core regfile as the backing vector store.
      if (old_entry.IsCompressed()) {
        for (uint32_t i = 0; i < num_lanes_; ++i) {
          if (packet->tmask[i] == 0) {
            packet->data_buf[i] = old_entry.base + i * old_entry.stride;
          }
        }
      }

      scalar_regfile_->WriteVector(local_wid(wid), rd);
    }
  }

  if (!cu.rd_ready()) {
    if (num_shared_ports_) {
      auto it = std::find_if(write_req_buf_->begin(), write_req_buf_->end(),
                             [](const Request &req) {
                               return !req.is_write;
                             });
      // Insert the new write request after existing write requests, but before
      // all read requests
      write_req_buf_->emplace(it, cu_id, wid, &packet->tmask,
                              packet->data_buf.data(), ToRegfileBank(wid, rd),
                              OpPosition::kRd, rd, true);
    } else {
      write_req_buf_->emplace_back(
          cu_id, wid, &packet->tmask, packet->data_buf.data(),
          ToRegfileBank(wid, rd), OpPosition::kRd, rd, true);
    }
  }
}

void PipelinedArbitrator::AccessRegfile(const Request &req) {
  if (req.is_write) {
    core_->WriteRegFile(req.data_ptr, req.wid, req.reg_id, *req.tmask);
    stats_.rf_write_reqs++;
  } else {
    core_->ReadRegFile(req.data_ptr, req.wid, req.reg_id);
    read_collect_units_[req.cu_id].SetSourceKind(req.op_pos,
                                                 SRFValueKind::kVector);
    stats_.rf_read_reqs++;
  }
}

void PipelinedArbitrator::ArbitrateRequests() {
  for (auto &cnt : bank_access_cnt_) {
    std::fill(cnt.begin(), cnt.end(), 0);
  }

  std::fill(port_utilized_.begin(), port_utilized_.end(), 0);

  // Iterate through every port of every bank and find a reqeust that access
  // this bank via the port.
  for (uint32_t bank = 0; bank < num_regfile_banks_; ++bank) {
    // For shared-port config: req_buf_arr_ = {S...}
    // For sperated-port config: req_buf_arr_ = {W..., R...}
    for (size_t port = 0; port < req_buf_arr_.size(); ++port) {
      auto &req_buf = *req_buf_arr_[port];
      auto &bank_mask = *bank_access_cnt_arr_[port];
      auto it = std::find_if(req_buf.begin(), req_buf.end(),
                             [bank](const Request &req) {
                               return req.req_bank == bank;
                             });
      if (it == req_buf.end()) continue;

      port_utilized_[port] = 1;
      bank_mask[bank]++;
      AccessRegfile(*it);

      const uint32_t cu_id = it->cu_id;
      const OpPosition op_pos = it->op_pos;
      const bool is_write = it->is_write;

      if (is_write) {
        write_collect_units_[cu_id].SetReady(op_pos);
      } else {
        read_collect_units_[cu_id].SetReady(op_pos);
      }

      Packet *packet = is_write ? write_collect_units_[cu_id].packet()
                                : read_collect_units_[cu_id].packet();
      LV_TRACE_INSTANT(
          is_write ? wcu_tracks_[cu_id] : rcu_tracks_[cu_id], "Arbitrator",
          fmt::format("{} Ready", OpPositionToString(op_pos)),
          LV_TRACE_ARG("reg_id", OpPositionToRegId(op_pos, packet)));

      it = req_buf.erase(it);

      // When all of ports are utilized, bank conflicts may occur
      bool may_conflict = false;
      if (num_shared_ports_ > 0) {
        may_conflict = (bank_mask[bank] == num_shared_ports_);
      } else {
        may_conflict = is_write ? (bank_mask[bank] == num_write_ports_)
                                : (bank_mask[bank] == num_read_ports_);
      }

      // Check bank conflict
      if (may_conflict) {
        auto conflict_it =
            std::find_if(it, req_buf.end(), [bank](const Request &req) {
              return req.req_bank == bank;
            });
        if (conflict_it != req_buf.end()) {
          // Updtate stats
          if (conflict_it->is_write) {
            stats_.rf_write_conflicts++;
          } else {
            stats_.rf_read_conflicts++;
          }
          stats_.rf_total_bank_conflicts++;

          // Update perfetto trace
          LV_TRACE_INSTANT(
              bank_conflict_tracks_[bank], "Arbitrator",
              conflict_it->is_write ? "WriteConflict" : "ReadConflict",
              LV_TRACE_ARG("cu_id", conflict_it->cu_id),
              LV_TRACE_ARG("wid", conflict_it->wid),
              LV_TRACE_ARG("reg_id", conflict_it->reg_id));
        }
      }
    }

    stats_.rf_total_available_banks +=
        std::accumulate(port_utilized_.begin(), port_utilized_.end(), 0) *
        num_regfile_banks_;
  }
}

void PipelinedArbitrator::ForwardReadPacket() {
  if (!operand_collect_resp->nb_can_put() ||
      working_read_collect_unit_ids_.empty()) {
    return;
  }

  auto it = std::find_if(working_read_collect_unit_ids_.begin(),
                         working_read_collect_unit_ids_.end(),
                         [this](uint32_t cu_id) {
                           return read_collect_units_[cu_id].IsReady();
                         });
  if (it == working_read_collect_unit_ids_.end()) {
    return;
  }

  const uint32_t cu_id = *it;
  auto &cu = read_collect_units_[cu_id];

  Packet *packet = cu.packet();
  const bool is_scalarizable = cu.IsScalarizable();
  packet->is_scalarizable = is_scalarizable;
  stats_.operand_collected_instrs++;
  stats_.scalarizable_instrs += is_scalarizable;
  cu.Deallocate();
  LV_TRACE_END(rcu_tracks_[cu_id]);

  free_read_collect_unit_ids_.push(cu_id);
  working_read_collect_unit_ids_.erase(it);

  if (scoreboard_) scoreboard_->RegReadDone(packet);
  operand_collect_resp->nb_put(packet);
}

void PipelinedArbitrator::ForwardWritePacket() {
  if (!writeback_resp->nb_can_put() ||
      working_write_collect_unit_ids_.empty()) {
    return;
  }

  auto it = std::find_if(working_write_collect_unit_ids_.begin(),
                         working_write_collect_unit_ids_.end(),
                         [this](uint32_t cu_id) {
                           return write_collect_units_[cu_id].IsReady();
                         });
  if (it == working_write_collect_unit_ids_.end()) {
    return;
  }

  const uint32_t cu_id = *it;
  auto &cu = write_collect_units_[cu_id];

  Packet *packet = cu.packet();
  cu.Deallocate();
  LV_TRACE_END(wcu_tracks_[cu_id]);

  free_write_collect_unit_ids_.push(cu_id);
  working_write_collect_unit_ids_.erase(it);

  writeback_resp->nb_put(packet);
}

void PipelinedArbitrator::Tick() {
  ForwardReadPacket();
  ArbitrateRequests();

  // Write packets can be forwarded directly after the arbitration because they
  // don't need to wait for the data from the register file bank
  ForwardWritePacket();

  AllocateReadCollectUnit();
  AllocateWriteCollectUnit();
}

#undef WITH_TRACER

LV_BINDING_WITH_BASES(simtix, PipelinedArbitrator, ArbitratorIntf)
    .constructor(
        [](const char *name, const ArchParam &param,
           const PipelinedArbitrator::Param &arb_param) {
          return std::make_shared<PipelinedArbitrator>(name, param, arb_param);
        },
        lv::params("name", "param", "arb_param"),
        lv::doc("Create a pipelined register-file arbitrator"));

}  // namespace simtix::pipelined
