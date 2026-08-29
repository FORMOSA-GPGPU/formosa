// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include "cores/pipelined/pipelined_core.h"

#include <fmt/chrono.h>
#include <liblv/binding.h>
#include <liblv/output.h>
#include <liblv/time.h>

#include <algorithm>
#include <chrono>

namespace simtix::pipelined {

void PipelinedCore::Tick() {
  if (dynamic_ipc_track_.enabled()) {
    int64_t instret_now = stats_.total_instret;
    dynamic_ipc_ma_ << std::max<int64_t>(instret_now - last_instret_, 0);
    LV_TRACE_COUNTER(dynamic_ipc_track_, "dynamic_ipc", dynamic_ipc_ma_.get());
    last_instret_ = instret_now;
  }
  // Count only cycles with at least one active warp; heartbeat IPC uses this
  // as the denominator instead of raw mcycle.
  if (active_mask_.val() != 0) stats_.active_cycles++;

  if (heartbeat_frequency_ > 0) {
    uint64_t instret_now =
        static_cast<uint64_t>(std::max<int64_t>(stats_.total_instret, 0));
    if (instret_now >= next_heartbeat_instret_) {
      uint64_t active_cycles_now =
          static_cast<uint64_t>(std::max<int64_t>(stats_.active_cycles, 0));
      EmitHeartbeat(instret_now, active_cycles_now);
    }
  }
}

void PipelinedCore::EmitHeartbeat(uint64_t instret_now,
                                  uint64_t active_cycles_now) {
  uint64_t delta_instret = instret_now >= last_heartbeat_instret_
                               ? instret_now - last_heartbeat_instret_
                               : 0;
  uint64_t delta_active_cycles =
      active_cycles_now >= last_heartbeat_active_cycles_
          ? active_cycles_now - last_heartbeat_active_cycles_
          : 0;

  double heartbeat_ipc = delta_active_cycles != 0
                             ? static_cast<double>(delta_instret) /
                                   static_cast<double>(delta_active_cycles)
                             : 0.0;
  double cumulative_ipc = active_cycles_now != 0
                              ? static_cast<double>(instret_now) /
                                    static_cast<double>(active_cycles_now)
                              : 0.0;  // avoid divide by zero
  double elapsed_sec =
      std::chrono::duration<double>(lv::elapsed_duration()).count();
  double mips = elapsed_sec != 0
                    ? static_cast<double>(instret_now) / elapsed_sec / 1e6
                    : 0.0;
  // keep on shim: elapsed_time() returns a std::chrono::duration, which has no
  // Quill codec for lazy queue encoding, so the LV_INFO macro can't take it.
  // Cold path (heartbeat), so eager fmt::format via the function shim is fine.
  lv::Info(
      "Heartbeat {} insts: {} hb_ipc: {:.3f} avg_ipc: {:.3f} "
      "sim_mips: {:.3f} elapsed: {:%H:%M:%S}",
      name(), instret_now, heartbeat_ipc, cumulative_ipc, mips,
      lv::elapsed_time());

  last_heartbeat_instret_ = instret_now;
  last_heartbeat_active_cycles_ = active_cycles_now;

  // If retire jumps past multiple intervals in one tick, advance to the next
  // future boundary instead of emitting duplicate catch-up heartbeats.
  next_heartbeat_instret_ =
      (instret_now / heartbeat_frequency_ + 1) * heartbeat_frequency_;
}

void PipelinedCore::ExecuteWarpCtrlCommand(
    const lv::formosa::WarpCtrlCommand &cmd) {
  switch (cmd.op()) {
    case lv::formosa::WarpCtrlCommand::Op::kActivate:
      Activate(cmd.cwm(), cmd.pc(), cmd.wg_info(), cmd.cwid_base());
      break;
    case lv::formosa::WarpCtrlCommand::Op::kResume:
      Resume(cmd.cwm());
      break;
    case lv::formosa::WarpCtrlCommand::Op::kRelease:
      Release(cmd.cwm());
      break;
    case lv::formosa::WarpCtrlCommand::Op::kAbort:
      Abort(cmd.cwm());
      break;
  }
}

void PipelinedCore::Activate(const sc_dt::sc_bv_base &cwm, uint64_t pc,
                             uint64_t wg_info, uint64_t cwid_base) {
  assert(cmd_ready_);
  WarpStateTransition(&active_mask_, &idle_mask_, cwm);
  uint32_t active_warp_cnt = 0;
  for (int w = 0; w < num_warps_; ++w) {
    if (cwm[w].to_bool()) {
      // Setup the PC of all threads in the warp
      for (int l = 0; l < num_lanes_; ++l) {
        ptpc_[w * num_lanes_ + l] = pc;
        ptpri_[w * num_lanes_ + l] = 0;
      }

      // Setup the WGInfo
      mscratch_[w] = wg_info;

      // Setup the CWID
      cwid_[w] = cwid_base + active_warp_cnt;

      ++active_warp_cnt;
      NoteFlush(w, FlushReason::kMisc, pc, 0);
      Redirect(w, pc);
    }
  }
}

void PipelinedCore::Resume(const sc_dt::sc_bv_base &cwm) {
  assert(cmd_ready_);
  WarpStateTransition(&active_mask_, &barrier_mask_, cwm);

  // For all resumed warp, since they are not active for fetching, the fetch PC
  // may have already skewed. Sycn them so that they are fetching correctly.
  for (int w = 0; w < num_warps_; ++w) {
    if (cwm[w].to_bool()) {
      frontend_.SyncPC(w);
    }
  }
}

void PipelinedCore::Release(const sc_dt::sc_bv_base &cwm) {
  assert(cmd_ready_);

  for (uint32_t wid = 0; wid < num_warps_; ++wid) {
    if (!cwm[wid].to_bool()) {
      continue;
    }

    RequestSchedulerFlush(wid);
  }

  WarpStateTransition(&idle_mask_, &exception_mask_, cwm);
}

void PipelinedCore::Abort(const sc_dt::sc_bv_base &cwm) {
  assert(cmd_ready_);

  for (uint32_t wid = 0; wid < num_warps_; ++wid) {
    if (!cwm[wid].to_bool()) {
      continue;
    }

    RequestSchedulerFlush(wid);
  }

  active_mask_ = active_mask_.val() & ~cwm;
  barrier_mask_ = barrier_mask_.val() & ~cwm;
  idle_mask_ = idle_mask_.val() | cwm;
}

LV_BINDING(simtix, PipelinedCore)
    .constructor(
        [](const char *name, const ArchParam &param, const Param &pipe_param) {
          return std::make_shared<PipelinedCore>(name, param, pipe_param);
        },
        lv::params("name", "param", "pipe_param"),
        lv::doc("Create a pipelined core"))
    .property("clock", &BaseCore::set_clock, lv::doc("SystemC clock"))
    .property("imem", &PipelinedCore::set_imem,
              lv::doc("Instruction memory target"))
    .property("subcores", &PipelinedCore::subcores,
              lv::doc("Subcore instances"))
    .property("warp_ctrl", &PipelinedCore::warp_ctrl,
              lv::doc("Warp-control interface"))
    .property("stats", &PipelinedCore::stats_group,
              lv::doc("Statistics group"));

}  // namespace simtix::pipelined
