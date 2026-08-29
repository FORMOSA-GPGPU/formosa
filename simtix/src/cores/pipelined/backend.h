/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <systemc.h>
#include <tlm_core/tlm_1/tlm_req_rsp/tlm_1_interfaces/tlm_core_ifs.h>
#include <tlm_core/tlm_1/tlm_req_rsp/tlm_channels/tlm_fifo/tlm_fifo.h>

#include "cores/pipelined/arbitrator/base.h"
#include "cores/pipelined/lsu/base.h"
#include "cores/pipelined/packet.h"
#include "cores/pipelined/param.h"
#include "cores/pipelined/scoreboard.h"
#include "cores/pipelined/stats.h"
#include "cores/sched/sched.h"
#include "cores/warp_mask.h"
#include "konata/konata.h"
#include "utils/delay_queue.h"

namespace simtix::pipelined {

class ToBackendIntf : public ToArbitratorIntf {
 public:
  virtual ~ToBackendIntf() = default;
  virtual const WarpMask &active_warps() const = 0;
  virtual void NotifyBarrier(uint32_t wid) = 0;
  virtual void NotifyEcall(uint32_t wid, uint64_t wpc) = 0;
  virtual void NotifyException(uint32_t wid, uint64_t wpc, uint64_t cause,
                               uint64_t tval) = 0;
  virtual void NoteFlush(uint32_t wid, FlushReason reason, uint64_t wpc,
                         uint64_t producer_id) = 0;
  virtual void NotifyControlResolved(uint32_t wid, uint64_t unique_id) = 0;
  virtual void RequestSchedulerFlush(uint32_t wid) = 0;
  virtual void CaptureThreadMask(Packet *packet) = 0;
  virtual void Redirect(uint32_t wid, uint64_t wpc) = 0;
  virtual void FreePacket(Packet *packet) = 0;
  virtual konata::KonataTracer<Packet> *tracer() = 0;
  virtual Stats *stats() = 0;

  // CSRs
  virtual std::vector<uint64_t> &ptpc() = 0;
  virtual std::vector<uint8_t> &ptpri() = 0;
  virtual std::vector<uint8_t> &ptfrm() = 0;
  virtual std::vector<uint8_t> &ptfflags() = 0;
  virtual std::vector<uint32_t> &cwid() = 0;
  virtual std::vector<uint64_t> &mepc() = 0;
  virtual std::vector<uint64_t> &mcause() = 0;
  virtual std::vector<uint64_t> &mtval() = 0;
  virtual std::vector<uint64_t> &mscratch() = 0;
  virtual std::vector<uint64_t> &minstret() = 0;
  virtual uint64_t mcycle() const = 0;
};

class Backend : public sc_module {
 public:
  sc_in<bool> SC_NAMED(clock);
  sc_vector<sc_port<tlm::tlm_get_peek_if<Packet *>>> SC_NAMED(from_frontend);

  Backend(const sc_module_name &name, const ArchParam &p, const Param &pp,
          const uint32_t subcore_id, ToBackendIntf *core)
      : sc_module(name),
        from_frontend("from_frontend", p.num_warps / pp.num_subcores),
        num_warps_(p.num_warps),
        num_local_warps_(p.num_warps / pp.num_subcores),
        num_lanes_(p.num_lanes),
        num_subcores_(pp.num_subcores),
        subcore_id_(subcore_id),
        core_(core),
        ready_warps_(false, p.num_warps),
        issue_suppressed_warps_(false, p.num_warps),
        scoreboard_(num_local_warps_, pp.num_subcores),
        inflight_counter_(num_local_warps_),
        pending_barrier_tmask_(
            num_local_warps_, sc_bv_base{false, static_cast<int>(p.num_lanes)}),
        pending_ecall_tmask_(num_local_warps_,
                             sc_bv_base{false, static_cast<int>(p.num_lanes)}),
        pending_ecall_wpc_(num_local_warps_),
        pending_exceptions_(num_local_warps_),
        alu_delay_q_("alu_delay_q_", pp.alu_latency, pp.alu_ticks_per_output),
        mdu_delay_q_("mdu_delay_q_", pp.mdu_latency, pp.mdu_ticks_per_output),
        fpu_delay_q_("fpu_delay_q_", pp.fpu_latency, pp.fpu_ticks_per_output),
        stats_(name) {
    SC_METHOD(Tick);
    sensitive << clock.pos();
  }

  using Target = lv::TlmSource::Target;
  void set_target(Target *target) {
    target_ = target;
    if (lsu_) {
      lsu_->set_target(target);
    }
  }

  void arbitrator_init(sol::function arbitrator_init) {
    arbitrator_init_ = std::move(arbitrator_init);
  }

  void lsu_init(sol::function lsu_init) { lsu_init_ = std::move(lsu_init); }

  void sched_init(sol::function sched_init) {
    sched_init_ = std::move(sched_init);
  }

  void before_end_of_elaboration() override {
    arbitrator_ = arbitrator_init_("arbitrator");
    arbitrator_init_ = sol::lua_nil;
    arbitrator_->set_core(core_);
    arbitrator_->set_scoreboard(&scoreboard_);
    arbitrator_->clock.bind(clock);
    arbitrator_->operand_collect_req.bind(collect_q_);
    arbitrator_->operand_collect_resp.bind(execute1_q_);
    arbitrator_->writeback_req.bind(writeback_q_);
    arbitrator_->writeback_resp.bind(retire_q_);
    if (arbitrator_->stats()) {
      stats_.add_sub_group(arbitrator_->stats());
    }

    lsu_ = lsu_init_("lsu");
    lsu_init_ = sol::lua_nil;
    lsu_->clock.bind(clock);
    lsu_->lsu_req.bind(lsu_req_q_);
    lsu_->lsu_resp.bind(lsu_resp_q_);
    if (lsu_->stats()) {
      stats_.add_sub_group(lsu_->stats());
    }
    if (target_) {
      lsu_->set_target(target_);
    }

    alu_delay_q_.clock.bind(clock);
    alu_delay_q_.req_q_.bind(alu_req_q_);
    alu_delay_q_.resp_q_.bind(alu_resp_q_);

    mdu_delay_q_.clock.bind(clock);
    mdu_delay_q_.req_q_.bind(mdu_req_q_);
    mdu_delay_q_.resp_q_.bind(mdu_resp_q_);

    fpu_delay_q_.clock.bind(clock);
    fpu_delay_q_.req_q_.bind(fpu_req_q_);
    fpu_delay_q_.resp_q_.bind(fpu_resp_q_);

    sched_ = sched_init_("sched");
    sched_init_ = sol::lua_nil;

    core_->stats()->add_sub_group(&stats_);
  }

  Scoreboard *scoreboard() { return &scoreboard_; }

 private:
  void Tick() {
    issue_suppressed_warps_ = 0;
    ProcessPendingExceptions();
    ProcessPendingEcalls();
    ProcessPendingBarriers();
    KonataRetire();
    Retire();
    Writeback();
    Execute2();
    Execute1();
    Issue();
  }

  void Issue();
  void Execute1();
  void Execute2();
  void Writeback();
  void Retire();
  void ProcessPendingBarriers();
  void ProcessPendingEcalls();
  void ProcessPendingExceptions();
  void KonataRetire();

  void UpdateReadyWarps();
  void UpdatePC(Packet *packet);
  uint64_t ArbitratePC(uint32_t wid);
  void ExecuteCsrOp(Packet *packet);
  void UpdateFflags(Packet *packet);
  void UpdatePri(Packet *packet);
  void FlushPacket(Packet *packet, FlushReason reason);
  void RetirePacket(Packet *packet);

  uint32_t get_local_wid(uint32_t wid) const { return wid / num_subcores_; }
  uint32_t get_wid(uint32_t local_wid) const {
    return local_wid * num_subcores_ + subcore_id_;
  }

  const uint32_t num_warps_;
  const uint32_t num_local_warps_;
  const uint32_t num_lanes_;
  const uint32_t num_subcores_;
  const uint32_t subcore_id_;
  ToBackendIntf *const core_;

  sc_bv_base ready_warps_;

  tlm::tlm_fifo<Packet *> SC_NAMED(collect_q_, 2);
  tlm::tlm_fifo<Packet *> SC_NAMED(execute1_q_, 2);
  tlm::tlm_fifo<Packet *> SC_NAMED(execute2_q_, 2);
  tlm::tlm_fifo<Packet *> SC_NAMED(writeback_q_, 2);
  tlm::tlm_fifo<Packet *> SC_NAMED(retire_q_, 2);

  simtix::DelayQueue<Packet *> alu_delay_q_;
  simtix::DelayQueue<Packet *> mdu_delay_q_;
  simtix::DelayQueue<Packet *> fpu_delay_q_;
  tlm::tlm_fifo<Packet *> SC_NAMED(alu_req_q_, 2);
  tlm::tlm_fifo<Packet *> SC_NAMED(alu_resp_q_, 2);
  tlm::tlm_fifo<Packet *> SC_NAMED(mdu_req_q_, 2);
  tlm::tlm_fifo<Packet *> SC_NAMED(mdu_resp_q_, 2);
  tlm::tlm_fifo<Packet *> SC_NAMED(fpu_req_q_, 2);
  tlm::tlm_fifo<Packet *> SC_NAMED(fpu_resp_q_, 2);

  tlm::tlm_fifo<Packet *> SC_NAMED(lsu_req_q_, 2);
  tlm::tlm_fifo<Packet *> SC_NAMED(lsu_resp_q_, 2);

  // Function units for writeback arbitration
  uint32_t prioritized_fu_ = 0;
  std::vector<tlm::tlm_fifo<Packet *> *> fus_{&alu_resp_q_, &mdu_resp_q_,
                                              &fpu_resp_q_, &lsu_resp_q_};

  Scoreboard scoreboard_;
  std::shared_ptr<WarpSched> sched_;
  std::shared_ptr<ArbitratorIntf> arbitrator_;
  std::shared_ptr<Lsu> lsu_;

  sol::function arbitrator_init_;
  sol::function lsu_init_;
  sol::function sched_init_;
  Target *target_ = nullptr;

  struct PendingException {
    bool valid = false;
    uint64_t wpc = 0;
    uint64_t cause = 0;
    uint64_t tval = 0;
  };

  std::vector<uint32_t> inflight_counter_;
  std::vector<sc_bv_base> pending_barrier_tmask_;
  std::vector<sc_bv_base> pending_ecall_tmask_;
  std::vector<uint64_t> pending_ecall_wpc_;

  // Deferred non-ECALL trap state for each warp.
  std::vector<PendingException> pending_exceptions_;

  struct RetireEntry {
    bool is_flush = false;
    FlushReason flush_reason = FlushReason::kUnknown;
    Packet *packet = nullptr;
  };
  std::vector<RetireEntry> retire_pool_;
  sc_bv_base issue_suppressed_warps_;

  struct Stats : lv::stats::Group {
    explicit Stats(const char *name) : Group(name) {}
  } mutable stats_;
};
}  // namespace simtix::pipelined
