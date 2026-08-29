/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <liblv/trace.h>

#include "cores/base.h"
#include "cores/pipelined/backend.h"
#include "cores/pipelined/frontend.h"
#include "cores/pipelined/ghost_scheduler.h"
#include "cores/pipelined/packet.h"
#include "cores/pipelined/param.h"
#include "cores/pipelined/stats.h"
#include "konata/konata.h"
#include "utils/moving_average.h"

namespace simtix::pipelined {

class PipelinedCore : public BaseCore,
                      private ToFrontendIntf,
                      private ToGhostSchedulerIntf,
                      private ToBackendIntf {
 public:
  PipelinedCore(const sc_module_name &name, const ArchParam &param,
                const Param &pipe_param)
      : BaseCore(name, param),
        enable_ghost_scheduler_(pipe_param.enable_ghost_scheduler),
        frontend_("frontend", param, pipe_param, this),
        wmask_(false, num_warps_),
        packet_pool_(param, 10),
        stats_(name, param),
        heartbeat_frequency_(pipe_param.heartbeat_frequency) {
    frontend_.clock.bind(clock);
    backends_.init(pipe_param.num_subcores, [&](const char *name, size_t i) {
      Backend *backend = new Backend(name, param, pipe_param, i, this);
      backend->clock.bind(clock);
      return backend;
    });
    // Ghost schedulers obtain their scoreboards from the initialized backends.
    if (pipe_param.enable_ghost_scheduler) {
      ghost_schedulers_.init(
          pipe_param.num_subcores, [&](const char *name, size_t i) {
            GhostScheduler *ghost = new GhostScheduler(
                name, param, pipe_param, pipe_param.ghost_param, i, this);
            ghost->clock.bind(clock);
            return ghost;
          });
    }

    if (pipe_param.konata_trace_out) {
      try {
        tracer_ = std::make_unique<konata::KonataTracer<Packet>>(
            "tracer", std::string(*pipe_param.konata_trace_out));
        tracer_->clock.bind(clock);
      } catch (const std::exception &e) {
        LV_WARNING("Failed to open Konata trace file {}: {}",
                   *pipe_param.konata_trace_out, e.what());
      }
    }

    // Binding ibuffer to backends
    for (uint32_t i = 0; i < num_warps_; ++i) {
      uint32_t subcore_id = i % pipe_param.num_subcores;
      uint32_t local_wid = i / pipe_param.num_subcores;
      if (pipe_param.enable_ghost_scheduler) {
        ghost_schedulers_[subcore_id].from_frontend[local_wid].bind(
            frontend_.to_backend[i]);
        backends_[subcore_id].from_frontend[local_wid].bind(
            ghost_schedulers_[subcore_id].to_backend[local_wid]);
      } else {
        backends_[subcore_id].from_frontend[local_wid].bind(
            frontend_.to_backend[i]);
      }
    }
    cmd_ready_ = true;
    SC_METHOD(Tick);
    sensitive << clock.pos();

    // Perfetto tracing
    dynamic_ipc_track_.set_enabled(pipe_param.pftrace);

    // The first heartbeat fires once retired warp instructions reach the
    // configured interval.
    next_heartbeat_instret_ = heartbeat_frequency_;
  }

  // Lua bindings
  void set_imem(Frontend::Target *target) { frontend_.set_target(target); }

  sol::as_table_t<std::vector<Backend *>> subcores() {
    std::vector<Backend *> subcores;
    for (auto &subcore : backends_) {
      subcores.push_back(&subcore);
    }
    return subcores;
  }

  lv::formosa::WarpCtrl *warp_ctrl() {
    return static_cast<lv::formosa::WarpCtrl *>(this);
  }

  pipelined::Stats *stats() override { return &stats_; }
  lv::stats::Group *stats_group() { return &stats_; }

 protected:
  // Implementation of ToFrontendIntf
  const WarpMask &active_warps() const override { return active_mask_; }
  Packet *AllocatePacket() override { return packet_pool_.Acquire(); }
  void FreePacket(Packet *packet) override { packet_pool_.Release(packet); }
  konata::KonataTracer<Packet> *tracer() override { return tracer_.get(); }

  // Implementation of ToBackendIntf
  void ReadRegFile(int64_t *data, uint32_t wid, uint32_t reg_id) override {
    for (uint32_t i = 0; i < num_lanes_; ++i) {
      data[i] = regfile_[ToRegfileIndex(wid, reg_id) + i];
    }
  }

  void WriteRegFile(const int64_t *data, uint32_t wid, uint32_t reg_id,
                    const sc_bv_base &tmask) override {
    for (size_t i = 0; i < num_lanes_; ++i) {
      if (tmask[i]) {
        regfile_[ToRegfileIndex(wid, reg_id) + i] = data[i];
      }
    }
  }

  void NotifyBarrier(uint32_t wid) override {
    wmask_ = 1 << wid;
    WarpStateTransition(&barrier_mask_, &active_mask_, wmask_);
  }

  void NotifyEcall(uint32_t wid, uint64_t wpc) override {
    wmask_ = 1 << wid;
    mcause_[wid] = DecodeExceptionCause(ExecFlag::ECALL);
    mepc_[wid] = wpc;
    mtval_[wid] = 0;
    frontend_.NoteFlush(wid, FlushReason::kMisc, wpc, 0);
    frontend_.Flush(wid);
    WarpStateTransition(&exception_mask_, &active_mask_, wmask_);
  }

  void NotifyException(uint32_t wid, uint64_t wpc, uint64_t cause,
                       uint64_t tval) override {
    wmask_ = 1 << wid;
    mcause_[wid] = cause;
    mepc_[wid] = wpc;
    mtval_[wid] = tval;
    frontend_.NoteFlush(wid, FlushReason::kMisc, wpc, 0);
    frontend_.Flush(wid);
    WarpStateTransition(&exception_mask_, &active_mask_, wmask_);

    RequestSchedulerFlush(wid);
  }

  void NotifyControlResolved(uint32_t wid, uint64_t unique_id) override {
    if (!enable_ghost_scheduler_) {
      return;
    }

    ghost_for(wid).ResolveControl(wid, unique_id);
  }

  void RequestSchedulerFlush(uint32_t wid) override {
    if (!enable_ghost_scheduler_) {
      return;
    }

    ghost_for(wid).RequestFlush(wid);
  }

  void CaptureThreadMask(Packet *packet) override {
    packet->tmask = 0;
    uint64_t *ptpc = &ptpc_[packet->wid * num_lanes_];
    for (uint32_t lane = 0; lane < num_lanes_; ++lane) {
      packet->tmask[lane] = ptpc[lane] == packet->wpc;
      if (packet->tmask[lane]) {
        ptpc[lane] = packet->wpc + 4;
      }
    }
  }

  Scoreboard *scoreboard(uint32_t subcore_id) override {
    return backends_[subcore_id].scoreboard();
  }

  void NoteFlush(uint32_t wid, FlushReason reason, uint64_t wpc,
                 uint64_t producer_id) override {
    frontend_.NoteFlush(wid, reason, wpc, producer_id);
  }

  void Redirect(uint32_t wid, uint64_t wpc) override {
    frontend_.Redirect(wid, wpc);
  }

  std::vector<uint64_t> &ptpc() override { return ptpc_; }
  std::vector<uint8_t> &ptpri() override { return ptpri_; }
  std::vector<uint8_t> &ptfrm() override { return ptfrm_; }
  std::vector<uint8_t> &ptfflags() override { return ptfflags_; }
  std::vector<uint32_t> &cwid() override { return cwid_; }
  std::vector<uint64_t> &mepc() override { return mepc_; }
  std::vector<uint64_t> &mcause() override { return mcause_; }
  std::vector<uint64_t> &mtval() override { return mtval_; }
  std::vector<uint64_t> &mscratch() override { return mscratch_; }
  std::vector<uint64_t> &minstret() override { return minstret_; }
  uint64_t mcycle() const override { return BaseCore::mcycle(); }

  pipelined::Stats stats_;

 private:
  GhostScheduler &ghost_for(uint32_t wid) {
    return ghost_schedulers_[wid % backends_.size()];
  }

  void Activate(const sc_dt::sc_bv_base &cwm, uint64_t pc, uint64_t wg_info,
                uint64_t cwid_base);
  void Resume(const sc_dt::sc_bv_base &cwm);
  void Release(const sc_dt::sc_bv_base &cwm);
  void Abort(const sc_dt::sc_bv_base &cwm);

  void Tick();
  void EmitHeartbeat(uint64_t instret_now, uint64_t active_cycles_now);
  void ExecuteWarpCtrlCommand(const lv::formosa::WarpCtrlCommand &cmd) override;

  bool enable_ghost_scheduler_;
  Frontend frontend_;
  sc_vector<GhostScheduler> SC_NAMED(ghost_schedulers_);
  sc_vector<Backend> SC_NAMED(backends_);

  sc_bv_base wmask_;
  PacketPool packet_pool_;

  std::unique_ptr<konata::KonataTracer<Packet>> tracer_;

  // Perfetto tracing support
  MovingAverage<int64_t> dynamic_ipc_ma_;
  lv::trace::Track dynamic_ipc_track_{
      LV_NEW_MODULE_COUNTER_TRACK("dynamic_ipc")};
  int64_t last_instret_ = 0;

  // Heartbeat progress is keyed by retired warp instructions, while IPC is
  // reported against cycles where at least one warp is active.
  const uint64_t heartbeat_frequency_ = 0;
  uint64_t next_heartbeat_instret_ = 0;
  uint64_t last_heartbeat_instret_ = 0;
  uint64_t last_heartbeat_active_cycles_ = 0;

  sc_signal<bool> cmd_ready_;
};

}  // namespace simtix::pipelined
