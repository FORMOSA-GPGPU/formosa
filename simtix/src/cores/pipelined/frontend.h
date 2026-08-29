/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <liblv/common/tlm_source.h>
#include <systemc.h>
#include <tlm_core/tlm_1/tlm_req_rsp/tlm_1_interfaces/tlm_core_ifs.h>
#include <tlm_core/tlm_1/tlm_req_rsp/tlm_channels/tlm_fifo/tlm_fifo.h>
#include <tlm_core/tlm_2/tlm_generic_payload/tlm_gp.h>

#include <queue>

#include "cores/pipelined/fetch_entry.h"
#include "cores/pipelined/packet.h"
#include "cores/pipelined/param.h"
#include "cores/pipelined/stats.h"
#include "cores/warp_mask.h"
#include "konata/konata.h"

namespace simtix::pipelined {

class ToFrontendIntf {
 protected:
  virtual ~ToFrontendIntf() = default;
  virtual const WarpMask &active_warps() const = 0;
  virtual Packet *AllocatePacket() = 0;
  virtual void FreePacket(Packet *packet) = 0;
  virtual konata::KonataTracer<Packet> *tracer() = 0;
  virtual Stats *stats() = 0;
  friend class Frontend;
};

class Frontend : public sc_module {
 public:
  using Target = lv::TlmSource::Target;

  sc_in<bool> clock;
  sc_vector<sc_export<tlm::tlm_get_peek_if<Packet *>>> to_backend;

  Frontend(const sc_module_name &name, const ArchParam &param,
           const Param &pipe_param, ToFrontendIntf *core)
      : sc_module(name),
        to_backend("to_backend", param.num_warps),
        num_warps_(param.num_warps),
        fetch_width_(pipe_param.fetch_width),
        decode_width_(pipe_param.decode_width),
        num_fetch_filter_entries_(pipe_param.num_fetch_filter_entries),
        enable_iwis_(pipe_param.enable_iwis),
        core_(core),
        prioritized_(0),
        imem_port_("imem_port"),
        pc_gen_q_("pc_gen_q", 2),
        fetch_q_("fetch_q", fetch_width_ * 2),
        decode_q_("decode_q", decode_width_ * 2),
        fetch_pc_(num_warps_),
        next_pc_(num_warps_),
        starving_warps_(true, num_warps_),
        issuing_warps_(false, num_warps_),
        pending_flush_mask_(false, num_warps_),
        pending_flush_reason_(num_warps_, FlushReason::kUnknown),
        pending_flush_wpc_(num_warps_, 0),
        last_invalidation_reason_(num_warps_, FlushReason::kUnknown),
        last_invalidation_wpc_(num_warps_, 0),
        pending_edge_valid_(num_warps_, false),
        pending_edge_producer_id_(num_warps_, 0),
        pending_edge_target_wpc_(num_warps_, 0),
        ibuffer_("ibuffer") {
    ibuffer_.init(param.num_warps, [this](const char *name, size_t id) {
      return new tlm::tlm_fifo<Packet *>(name, fetch_width_ * 3);
    });
    to_backend.bind(ibuffer_);

    SC_METHOD(Tick);
    sensitive << clock.pos();

    SC_METHOD(CollectReadyFetchEntries);

    SC_METHOD(UpdateFetchPC);
    sensitive << update_fetch_pc_;
    dont_initialize();

    SC_METHOD(ExecutePendingFlushes);
    sensitive << flush_event_;
    dont_initialize();

    fetch_entries_.reserve(pipe_param.num_fetch_entries);
    for (uint32_t i = 0; i < pipe_param.num_fetch_entries; ++i) {
      fetch_entries_.emplace_back(std::make_unique<FetchEntry>(fetch_width_));
      free_fetch_entries_.push(fetch_entries_.back().get());
    }

    eligible_warps_.push_back(&starving_warps_);
    // TODO: issuing warps should be prioritized in IWIS, however, fronetend
    // test indicates that if issuing warps are considiered first, it may lead
    // to huge pipeline stall when fetch width is larger than the per-subcore
    // issue width, which is 1.
    //
    // Include "issuing_warps_" only when IWIS is enabled
    if (enable_iwis_) {
      eligible_warps_.push_back(&issuing_warps_);
    }

    for (uint32_t i = 0; i < num_warps_; ++i) {
      sc_spawn_options opts;
      opts.spawn_method();
      // ok_to_put is data read event
      opts.set_sensitivity(&ibuffer_[i].ok_to_put());
      opts.dont_initialize();
      sc_spawn(sc_bind(&Frontend::OnIssue, this, i),
               sc_gen_unique_name("OnIssue"), &opts);
    }
  }

  void NoteFlush(uint32_t wid, FlushReason reason, uint64_t wpc,
                 uint64_t producer_id);

  void Flush(uint32_t wid) {
    pending_flush_mask_[wid] = 1;
    flush_event_.notify(SC_ZERO_TIME);
  }

  void SyncPC(uint32_t wid) {
    if (core_->active_warps().val()[wid]) {
      fetch_pc_[wid] = next_pc_[wid];
    } else {
      assert(0);
    }
  }

  void Redirect(uint32_t wid, uint64_t wpc) {
    if (core_->active_warps().val()[wid]) {
      Packet *packet = nullptr;
      // We can omit the flush when:
      // 1. The head of the I-Buffer has the exactly target WPC we need, or
      // 2. The I-Buffer is empty but the next PC is exactly we need
      if (ibuffer_[wid].nb_peek(packet) && packet->wpc == wpc ||
          !ibuffer_[wid].nb_can_get() && next_pc_[wid] == wpc) {
        return;
      }

      next_pc_[wid] = wpc;
      fetch_pc_[wid] = wpc;
      Flush(wid);
    }
  }

  void set_target(lv::TlmSource::Target *target) {
    imem_port_.set_target(target);
  }

 private:
  void Tick() {
    Broadcast();
    Decode();
    Fetch();
    PCGen();
  }

  void PCGen();
  void Fetch();
  void Decode();
  void Broadcast();

  void CollectReadyFetchEntries();
  void UpdateFetchPC();
  void ExecutePendingFlushes();
  void OnIssue(uint32_t wid) { issuing_warps_[wid] = 1; }
  void TryLinkRedirectTarget(uint32_t wid, Packet *consumer);

  void GenerateFetchRequest(uint32_t wid);

  struct FetchFilterEntry {
    uint64_t fg_addr;
    uint32_t remaining;  // words in this fetch group not yet broadcast
  };

  // Convert (align/round) a fetch PC to the fetch group's base address.
  inline constexpr uint64_t ToFetchGroupAddress(uint64_t pc) const {
    return pc & ~(fetch_width_ * 4 - 1);
  }

  std::vector<FetchFilterEntry>::iterator FindFetchFilterEntry(
      uint64_t fg_addr);

  FetchEntry *AllocateFetchEntry(uint64_t addr) {
    assert(!free_fetch_entries_.empty());
    FetchEntry *entry = free_fetch_entries_.front();
    entry->SetAddress(addr);
    pending_fetch_entries_.push(entry);
    free_fetch_entries_.pop();
    return entry;
  }

  FetchEntry *GetReadyFetchEntry() {
    if (pending_fetch_entries_.empty()) return nullptr;
    auto *entry = pending_fetch_entries_.front();
    if (!entry->ready()) return nullptr;
    pending_fetch_entries_.pop();
    return entry;
  }

  void FreeFetchEntry(FetchEntry *entry) {
    entry->Reset();
    free_fetch_entries_.push(entry);
  }

  const uint32_t num_warps_;
  const uint32_t fetch_width_;
  const uint32_t decode_width_;
  const uint32_t num_fetch_filter_entries_;
  const bool enable_iwis_;
  ToFrontendIntf *const core_;

  // Fetch buffer
  std::vector<std::unique_ptr<FetchEntry>> fetch_entries_;
  std::queue<FetchEntry *> free_fetch_entries_;
  std::queue<FetchEntry *> pending_fetch_entries_;
  lv::TlmSource imem_port_;

  // Pipelined registers
  sc_fifo<FetchEntry *> pc_gen_q_;
  sc_fifo<Packet *> fetch_q_;
  tlm::tlm_fifo<Packet *> decode_q_;

  // IWIS fetch PC arbitration
  uint32_t prioritized_;
  std::vector<uint64_t> fetch_pc_;
  std::vector<uint64_t> next_pc_;
  std::vector<FetchFilterEntry> fetch_filter_;

  // This class is used to notify the UpdateFetchPC process with two arguments:
  // wid and fg_addr.
  class {
   public:
    void notify(uint32_t wid, uint64_t fg_addr) {
      wid_ = wid;
      fg_addr_ = fg_addr;
      default_event_.notify(SC_ZERO_TIME);
    }
    operator sc_event &() { return default_event_; }

    uint32_t wid() const { return wid_; }
    uint64_t fg_addr() const { return fg_addr_; }

   private:
    uint32_t wid_;
    uint64_t fg_addr_;
    sc_event default_event_;
  } update_fetch_pc_;

  sc_bv_base starving_warps_;
  sc_bv_base issuing_warps_;
  std::vector<sc_bv_base *> eligible_warps_;

  // This sc_event + sc_bv_base is used to flush the ibuffer in the next delta
  // cycle
  sc_event flush_event_;
  sc_bv_base pending_flush_mask_;
  std::vector<FlushReason> pending_flush_reason_;
  std::vector<uint64_t> pending_flush_wpc_;
  std::vector<FlushReason> last_invalidation_reason_;
  std::vector<uint64_t> last_invalidation_wpc_;

  std::vector<bool> pending_edge_valid_;
  std::vector<uint64_t> pending_edge_producer_id_;
  std::vector<uint64_t> pending_edge_target_wpc_;

  sc_vector<tlm::tlm_fifo<Packet *>> ibuffer_;
};

}  // namespace simtix::pipelined
