// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include <liblv/binding.h>
#include <liblv/log.h>
#include <systemc.h>
#include <tlm_core/tlm_1/tlm_req_rsp/tlm_channels/tlm_fifo/tlm_fifo.h>

#include <cstdint>
#include <memory>
#include <unordered_set>

#include "cores/decode.h"
#include "cores/pipelined/ghost_param.h"
#include "cores/pipelined/ghost_scheduler.h"

namespace simtix::pipelined {

class GhostSchedulerTester : public sc_module, private ToGhostSchedulerIntf {
 public:
  GhostSchedulerTester(const sc_module_name &name, const ArchParam &param,
                       const Param &pipe_param, const GhostParam &ghost_param,
                       uint32_t subcore_id)
      : sc_module(name),
        num_warps_(param.num_warps),
        num_lanes_(param.num_lanes),
        num_subcores_(pipe_param.num_subcores),
        subcore_id_(subcore_id),
        num_local_warps_(ValidateTopology(param, pipe_param, subcore_id)),
        active_warps_(false, param.num_warps),
        stats_(name),
        scoreboard_(num_local_warps_, num_subcores_),
        packet_pool_(param, 16),
        frontend_queues_("frontend_queue"),
        scheduler_("ghost_scheduler", param, pipe_param, ghost_param,
                   subcore_id, this) {
    frontend_queues_.init(num_local_warps_, [](const char *queue_name, size_t) {
      return new tlm::tlm_fifo<Packet *>(queue_name, 16);
    });

    for (uint32_t local_wid = 0; local_wid < num_local_warps_; ++local_wid) {
      scheduler_.from_frontend[local_wid].bind(frontend_queues_[local_wid]);
    }
  }

  void set_clock(std::shared_ptr<sc_clock> clock) {
    clock_ = std::move(clock);
    scheduler_.clock.bind(*clock_);
  }

  void activate(uint32_t wid) {
    CheckWid(wid);
    sc_bv_base mask = active_warps_;
    mask[wid] = 1;
    active_warps_ = mask;
  }

  void enqueue(uint32_t wid, uint32_t iword, uint64_t tag) {
    CheckWid(wid);

    Packet *packet = packet_pool_.Acquire();
    packet->wid = wid;
    packet->wpc = tag;
    packet->iword = iword;
    packet->instr = Decode(iword);

    if (!frontend_queues_[GetLocalWid(wid)].nb_put(packet)) {
      packet_pool_.Release(packet);
      LV_FATAL("GhOST tester frontend queue is full for wid={}", wid);
    }

    ++outstanding_packets_;
  }

  Packet *peek(uint32_t wid) const {
    CheckWid(wid);
    Packet *packet = nullptr;
    scheduler_.to_backend[GetLocalWid(wid)]->nb_peek(packet);
    return packet;
  }

  Packet *issue(uint32_t wid) {
    CheckWid(wid);

    Packet *packet = nullptr;
    if (!scheduler_.to_backend[GetLocalWid(wid)]->nb_get(packet)) {
      return nullptr;
    }

    scoreboard_.Issue(packet);
    issued_packets_.insert(packet);
    return packet;
  }

  void finish(Packet *packet) {
    CheckIssued(packet);
    scoreboard_.RegReadDone(packet);
    scoreboard_.Commit(packet);
    issued_packets_.erase(packet);
    ReleasePacket(packet);
  }

  void scoreboard_issue(uint32_t wid, uint32_t iword) {
    WithTemporaryPacket(wid, iword, [this](Packet *packet) {
      scoreboard_.Issue(packet);
    });
  }

  void scoreboard_finish(uint32_t wid, uint32_t iword) {
    WithTemporaryPacket(wid, iword, [this](Packet *packet) {
      scoreboard_.RegReadDone(packet);
      scoreboard_.Commit(packet);
    });
  }

  uint32_t outstanding_packets() const { return outstanding_packets_; }

  lv::stats::Group *stats_group_for_lua() { return &stats_; }

 protected:
  const WarpMask &active_warps() const override { return active_warps_; }

  void CaptureThreadMask(Packet *packet) override {
    packet->tmask = sc_bv_base(true, num_lanes_);
  }

  Scoreboard *scoreboard(uint32_t subcore_id) override {
    if (subcore_id != subcore_id_) {
      LV_FATAL(
          "GhOST tester requested scoreboard for subcore_id={}, expected "
          "subcore_id={}",
          subcore_id, subcore_id_);
    }
    return &scoreboard_;
  }

  void FreePacket(Packet *packet) override { ReleasePacket(packet); }

  konata::KonataTracer<Packet> *tracer() override { return nullptr; }

  lv::stats::Group *stats_group() override { return &stats_; }

 private:
  static uint32_t ValidateTopology(const ArchParam &param,
                                   const Param &pipe_param,
                                   uint32_t subcore_id) {
    if (param.num_warps == 0 || param.num_lanes == 0 ||
        pipe_param.num_subcores == 0 || pipe_param.decode_width == 0 ||
        param.num_warps % pipe_param.num_subcores != 0 ||
        subcore_id >= pipe_param.num_subcores) {
      LV_FATAL(
          "Invalid GhOST scheduler tester topology: num_warps={}, "
          "num_lanes={}, num_subcores={}, decode_width={}, subcore_id={}",
          param.num_warps, param.num_lanes, pipe_param.num_subcores,
          pipe_param.decode_width, subcore_id);
    }

    return param.num_warps / pipe_param.num_subcores;
  }

  void CheckWid(uint32_t wid) const {
    if (wid >= num_warps_ || wid % num_subcores_ != subcore_id_) {
      LV_FATAL(
          "Warp wid={} does not belong to GhOST tester subcore_id={} "
          "(num_subcores={}, num_warps={})",
          wid, subcore_id_, num_subcores_, num_warps_);
    }
  }

  void CheckIssued(Packet *packet) const {
    if (packet == nullptr || issued_packets_.count(packet) == 0) {
      LV_FATAL("Packet is null or not issued by this GhOST tester");
    }
  }

  uint32_t GetLocalWid(uint32_t wid) const { return wid / num_subcores_; }

  void ReleasePacket(Packet *packet) {
    if (packet == nullptr || outstanding_packets_ == 0) {
      LV_FATAL(
          "GhOST tester packet accounting underflow: outstanding_packets_={}",
          outstanding_packets_);
    }

    packet_pool_.Release(packet);
    --outstanding_packets_;
  }

  template <typename Callback>
  void WithTemporaryPacket(uint32_t wid, uint32_t iword, Callback callback) {
    CheckWid(wid);

    Packet *packet = packet_pool_.Acquire();
    packet->wid = wid;
    packet->iword = iword;
    packet->instr = Decode(iword);
    callback(packet);
    packet_pool_.Release(packet);
  }

  const uint32_t num_warps_;
  const uint32_t num_lanes_;
  const uint32_t num_subcores_;
  const uint32_t subcore_id_;
  const uint32_t num_local_warps_;

  std::shared_ptr<sc_clock> clock_;
  WarpMask active_warps_;
  lv::stats::Group stats_;
  Scoreboard scoreboard_;
  PacketPool packet_pool_;
  sc_vector<tlm::tlm_fifo<Packet *>> frontend_queues_;
  GhostScheduler scheduler_;
  std::unordered_set<Packet *> issued_packets_;
  uint32_t outstanding_packets_ = 0;
};

LV_BINDING(simtix, GhostSchedulerTester)
    .constructor(
        [](const char *name, const ArchParam &param, const Param &pipe_param,
           const GhostParam &ghost_param, uint32_t subcore_id) {
          return std::make_shared<GhostSchedulerTester>(
              name, param, pipe_param, ghost_param, subcore_id);
        },
        lv::params("name", "param", "pipe_param", "ghost_param", "subcore_id"),
        lv::doc("Create a GhOST scheduler tester"))
    .property("clock", &GhostSchedulerTester::set_clock,
              lv::doc("SystemC clock"))
    .method("activate", &GhostSchedulerTester::activate, lv::params("wid"),
            lv::doc("Activate one warp"))
    .method("enqueue", &GhostSchedulerTester::enqueue,
            lv::params("wid", "iword", "tag"),
            lv::doc("Inject one instruction into the frontend queue"))
    .method("peek", &GhostSchedulerTester::peek, lv::params("wid"),
            lv::doc("Peek the current GhOST issue candidate"))
    .method("issue", &GhostSchedulerTester::issue, lv::params("wid"),
            lv::doc("Issue the current GhOST candidate"))
    .method("finish", &GhostSchedulerTester::finish, lv::params("packet"),
            lv::doc("Finish and release an issued packet"))
    .method("scoreboard_issue", &GhostSchedulerTester::scoreboard_issue,
            lv::params("wid", "iword"),
            lv::doc("Create an artificial scoreboard hazard"))
    .method("scoreboard_finish", &GhostSchedulerTester::scoreboard_finish,
            lv::params("wid", "iword"),
            lv::doc("Release an artificial scoreboard hazard"))
    .property("outstanding_packets", &GhostSchedulerTester::outstanding_packets,
              lv::doc("Number of tester-owned packets not yet released"))
    .property("stats", &GhostSchedulerTester::stats_group_for_lua,
              lv::doc("GhOST scheduler statistics"));

}  // namespace simtix::pipelined
