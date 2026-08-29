/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <liblv/interfaces/warp_ctrl.h>
#include <liblv/statistics.h>
#include <systemc.h>

#include <vector>

#include "cores/param.h"
#include "cores/warp_mask.h"

namespace simtix {

class BaseCore : public sc_module, public lv::formosa::WarpCtrl {
 public:
  sc_in<bool> SC_NAMED(clock);

  BaseCore(const sc_module_name &name, const ArchParam &p)
      : sc_module(name),
        num_lanes_(p.num_lanes),
        num_warps_(p.num_warps),
        num_threads_(num_lanes_ * num_warps_),
        idle_mask_(true, num_warps_),
        active_mask_(false, num_warps_),
        barrier_mask_(false, num_warps_),
        exception_mask_(false, num_warps_),
        regfile_(num_threads_ * 32),
        ptpc_(num_threads_),
        ptpri_(num_threads_),
        ptfrm_(num_threads_, 0),
        ptfflags_(num_threads_, 0),
        cwid_(num_warps_),
        mepc_(num_warps_),
        mcause_(num_warps_),
        mtval_(num_warps_),
        mscratch_(num_warps_),
        minstret_(num_warps_) {
    SC_METHOD(McycleIncrement);
    sensitive << clock.pos();
    SC_THREAD(WarpCtrlCommandThread);
  }

  virtual ~BaseCore() = default;

  // WarpCtrl implementation
  const sc_dt::sc_bv_base &idle_mask() override { return idle_mask_; }

  const sc_dt::sc_bv_base &active_mask() override { return active_mask_; }

  const sc_dt::sc_bv_base &barrier_mask() override { return barrier_mask_; }

  const sc_dt::sc_bv_base &exception_mask() override { return exception_mask_; }

  const sc_core::sc_event &idle_mask_changed_event() override {
    return idle_mask_.value_changed_event();
  }

  const sc_core::sc_event &active_mask_changed_event() override {
    return active_mask_.value_changed_event();
  }

  const sc_core::sc_event &barrier_mask_changed_event() override {
    return barrier_mask_.value_changed_event();
  }

  const sc_core::sc_event &exception_mask_changed_event() override {
    return exception_mask_.value_changed_event();
  }

  const std::vector<uint64_t> &mcause() override { return mcause_; }

  const std::vector<uint64_t> &mepc() override { return mepc_; }

  const std::vector<uint64_t> &mtval() override { return mtval_; }

  uint64_t mcycle() const { return mcycle_.read(); }

  // lua bindings
  void set_clock(sc_clock *clk) { clock.bind(*clk); }

  struct Stats : public lv::stats::Group {
    Array warp_instret;
    Array lane_instret;

    Array committed_arithmetic_instr;
    Array committed_cond_branch_instr;
    Array committed_uncond_jump_instr;
    Array committed_load_instr;
    Array committed_store_instr;
    Array committed_amo_instr;
    Array committed_system_instr;
    Array committed_fp_instr;
    Array committed_custom_instr;

    Array committed_stack_load_instr;
    Array committed_stack_store_instr;
    Array committed_non_stack_store_instr;
    Array committed_non_stack_load_instr;

    Formula<Integer> total_instret;
    Formula<Integer> total_arithmetic_instr;
    Formula<Integer> total_cond_branch_instr;
    Formula<Integer> total_uncond_jump_instr;
    Formula<Integer> total_load_instr;
    Formula<Integer> total_store_instr;
    Formula<Integer> total_amo_instr;
    Formula<Integer> total_system_instr;
    Formula<Integer> total_fp_instr;
    Formula<Integer> total_custom_instr;

    Formula<Integer> total_stack_load_instr;
    Formula<Integer> total_stack_store_instr;
    Formula<Integer> total_non_stack_load_instr;
    Formula<Integer> total_non_stack_store_instr;

    Formula<Real> arithmetic_instr_ratio;
    Formula<Real> cond_branch_instr_ratio;
    Formula<Real> uncond_jump_instr_ratio;
    Formula<Real> load_instr_ratio;
    Formula<Real> store_instr_ratio;
    Formula<Real> amo_instr_ratio;
    Formula<Real> system_instr_ratio;
    Formula<Real> fp_instr_ratio;
    Formula<Real> custom_instr_ratio;

    Formula<Real> lane_util;

    Stats(const char *name, const ArchParam &param)
        : Group(name),
          LV_STAT(warp_instret, "Number of retired instruction per warp",
                  param.num_warps),
          LV_STAT(lane_instret, "Number of retired instruction per lane",
                  param.num_lanes),
          LV_STAT(committed_arithmetic_instr,
                  "Number of committed arithmetic instructions per warp",
                  param.num_warps),
          LV_STAT(
              committed_cond_branch_instr,
              "Number of committed conditional branch instructions per warp",
              param.num_warps),
          LV_STAT(
              committed_uncond_jump_instr,
              "Number of committed unconditional jump instructions per warp",
              param.num_warps),
          LV_STAT(committed_load_instr,
                  "Number of committed load instructions per warp",
                  param.num_warps),
          LV_STAT(committed_store_instr,
                  "Number of committed store instructions per warp",
                  param.num_warps),
          LV_STAT(committed_amo_instr,
                  "Number of committed AMO instructions per warp",
                  param.num_warps),
          LV_STAT(committed_system_instr,
                  "Number of committed system instructions per warp",
                  param.num_warps),
          LV_STAT(committed_fp_instr,
                  "Number of committed floating-point instructions per warp",
                  param.num_warps),
          LV_STAT(committed_custom_instr,
                  "Number of committed custom instructions per warp",
                  param.num_warps),
          LV_STAT(committed_stack_load_instr,
                  "Number of committed stack load instructions per warp",
                  param.num_warps),
          LV_STAT(committed_stack_store_instr,
                  "Number of committed stack store instructions per warp",
                  param.num_warps),
          LV_STAT(committed_non_stack_load_instr,
                  "Number of committed non-stack load instructions per warp",
                  param.num_warps),
          LV_STAT(committed_non_stack_store_instr,
                  "Number of committed non-stack store instructions per warp",
                  param.num_warps),
          LV_STAT(total_instret, "Total number of retired instructions"),
          LV_STAT(total_arithmetic_instr,
                  "Total number of committed arithmetic instructions"),
          LV_STAT(total_cond_branch_instr,
                  "Total number of committed conditional branch instructions"),
          LV_STAT(total_uncond_jump_instr,
                  "Total number of committed jump instructions"),
          LV_STAT(total_load_instr,
                  "Total number of committed load instructions"),
          LV_STAT(total_store_instr,
                  "Total number of committed store instructions"),
          LV_STAT(total_amo_instr,
                  "Total number of committed amo instructions"),
          LV_STAT(total_system_instr,
                  "Total number of committed system instructions"),
          LV_STAT(total_fp_instr,
                  "Total number of committed floating-point instructions"),
          LV_STAT(total_custom_instr,
                  "Total number of committed custom instructions"),
          LV_STAT(total_stack_load_instr,
                  "Total number of committed stack load instructions"),
          LV_STAT(total_stack_store_instr,
                  "Total number of committed stack store instructions"),
          LV_STAT(total_non_stack_load_instr,
                  "Total number of committed non-stack load instructions"),
          LV_STAT(total_non_stack_store_instr,
                  "Total number of committed non-stack store instructions"),
          LV_STAT(arithmetic_instr_ratio,
                  "Ratio of committed arithmetic instructions"),
          LV_STAT(cond_branch_instr_ratio,
                  "Ratio of committed conditional branch instructions"),
          LV_STAT(uncond_jump_instr_ratio,
                  "Ratio of committed jump instructions"),
          LV_STAT(load_instr_ratio, "Ratio of committed load instructions"),
          LV_STAT(store_instr_ratio, "Ratio of committed store instructions"),
          LV_STAT(amo_instr_ratio, "Ratio of committed amo instructions"),
          LV_STAT(system_instr_ratio, "Ratio of committed system instructions"),
          LV_STAT(fp_instr_ratio,
                  "Ratio of committed floating-point instructions"),
          LV_STAT(custom_instr_ratio, "Ratio of committed custom instructions"),
          LV_STAT(lane_util, "Lane utilization") {
      total_instret = warp_instret.sum();
      total_arithmetic_instr = committed_arithmetic_instr.sum();
      total_cond_branch_instr = committed_cond_branch_instr.sum();
      total_uncond_jump_instr = committed_uncond_jump_instr.sum();
      total_load_instr = committed_load_instr.sum();
      total_store_instr = committed_store_instr.sum();
      total_amo_instr = committed_amo_instr.sum();
      total_system_instr = committed_system_instr.sum();
      total_fp_instr = committed_fp_instr.sum();
      total_custom_instr = committed_custom_instr.sum();

      total_stack_load_instr = committed_stack_load_instr.sum();
      total_stack_store_instr = committed_stack_store_instr.sum();
      total_non_stack_load_instr = committed_non_stack_load_instr.sum();
      total_non_stack_store_instr = committed_non_stack_store_instr.sum();

      arithmetic_instr_ratio = total_arithmetic_instr / total_instret;
      cond_branch_instr_ratio = total_cond_branch_instr / total_instret;
      uncond_jump_instr_ratio = total_uncond_jump_instr / total_instret;
      load_instr_ratio = total_load_instr / total_instret;
      store_instr_ratio = total_store_instr / total_instret;
      amo_instr_ratio = total_amo_instr / total_instret;
      system_instr_ratio = total_system_instr / total_instret;
      fp_instr_ratio = total_fp_instr / total_instret;
      custom_instr_ratio = total_custom_instr / total_instret;
      lane_util = lane_instret.sum() / (warp_instret.sum() * param.num_lanes);
    }
  };

 protected:
  const uint32_t num_lanes_;
  const uint32_t num_warps_;
  const uint32_t num_threads_;

  WarpMask idle_mask_, active_mask_, barrier_mask_, exception_mask_;

  void WarpStateTransition(WarpMask *to, WarpMask *from,
                           const sc_bv_base &cwm) {
    assert((from->val() & cwm) == cwm);
    assert((to->val() & cwm) == 0);
    *from = from->val() ^ cwm;
    *to = to->val() ^ cwm;
  }

  uint32_t ToRegfileIndex(uint32_t wid, uint32_t reg_id) {
    //   | W0:R0 | W0:R1 | ... | W1:R0 | W1:R1 | ...
    //         ->|~~~~~~~|<- int64_t * num_lanes
    // ->|~~~~~~~~~~~~~~~~~~~~~|<- num_xregs * num_lanes
    return num_lanes_ * (wid * 32 + reg_id);
  }

  // W0:R0 | W0:R1 | ... | W1:R0 | W1:R1 | ...
  std::vector<int64_t> regfile_;

  // Per-thread states
  std::vector<uint64_t> ptpc_;
  std::vector<uint8_t> ptpri_;
  std::vector<uint8_t> ptfrm_;
  std::vector<uint8_t> ptfflags_;

  // Per-warp CSRs
  std::vector<uint32_t> cwid_;
  std::vector<uint64_t> mepc_;
  std::vector<uint64_t> mcause_;
  std::vector<uint64_t> mtval_;
  std::vector<uint64_t> mscratch_;
  std::vector<uint64_t> minstret_;

  // Per-core CSRs
  sc_signal<uint64_t> mcycle_;

  void McycleIncrement() { mcycle_ = mcycle_.read() + 1; }

  virtual void ExecuteWarpCtrlCommand(
      const lv::formosa::WarpCtrlCommand &cmd) = 0;

  void WarpCtrlCommandThread() {
    wait(SC_ZERO_TIME);
    if (warp_cmd.size() == 0) {
      return;
    }
    while (true) {
      ExecuteWarpCtrlCommand(warp_cmd->get());
    }
  }
};

}  // namespace simtix
