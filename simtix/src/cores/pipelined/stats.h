/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <fmt/format.h>
#include <liblv/statistics.h>

#include <cassert>
#include <cstdint>
#include <string_view>

#include "cores/base.h"

namespace simtix::pipelined {

enum class FlushReason : uint8_t {
  kUnknown,
  kJbOthers,     // other non-divergent CTI (e.g. cond branch, non-ret jalr)
  kJalForward,   // jal with imm >= 0
  kJalBackward,  // jal with imm < 0
  kJalrReturn,   // jalr x0, 0(ra) — RISC-V ret
  kBranchDivergence,
  kDuplicateInst,
  // activate / priority / barrier / empty_tmask / ecall* / exception /
  // warp_inactive
  kMisc,
};

inline std::string_view FlushCause(FlushReason reason) {
  switch (reason) {
    case FlushReason::kJbOthers:
      return "jb_others";
    case FlushReason::kJalForward:
      return "jal_forward";
    case FlushReason::kJalBackward:
      return "jal_backward";
    case FlushReason::kJalrReturn:
      return "jalr_return";
    case FlushReason::kBranchDivergence:
      return "branch_divergence";
    case FlushReason::kDuplicateInst:
      return "duplicate_inst";
    case FlushReason::kMisc:
      return "misc";
    default:
      return "unknown";
  }
}

inline bool IsIwisDuplicateBroadcastReject(uint64_t wpc, uint64_t next_pc,
                                           FlushReason last_reason,
                                           uint64_t last_redirect_wpc) {
  if (wpc >= next_pc) {
    return false;
  }
  if (last_reason == FlushReason::kUnknown || last_redirect_wpc == 0) {
    return true;
  }
  return wpc >= last_redirect_wpc;
}

inline FlushReason AttributeBroadcastReject(uint64_t wpc, uint64_t next_pc,
                                            FlushReason last_reason,
                                            uint64_t last_redirect_wpc) {
  if (IsIwisDuplicateBroadcastReject(wpc, next_pc, last_reason,
                                     last_redirect_wpc)) {
    return FlushReason::kDuplicateInst;
  }
  if (last_reason != FlushReason::kUnknown && last_redirect_wpc != 0) {
    return last_reason;
  }
  return FlushReason::kUnknown;
}

struct Stats : public BaseCore::Stats {
  Metric active_cycles;
  Formula<Real> ipc;

  // Frontend
  Metric fetch_due_to_starving;
  Metric fetch_due_to_issuing;
  Metric num_fetches_filtered;
  Metric can_share_instr;
  Metric instr_shared;
  Metric instr_filled;
  Metric ibuffer_full;
  Formula<Real> avg_instr_shared;
  Formula<Real> instr_sharing_opp;
  Metric flush_jb_others;
  Metric flush_jal_forward;
  Metric flush_jal_backward;
  Metric flush_jalr_return;
  Metric flush_branch_divergence;
  Metric flush_duplicate_inst;
  Metric flush_misc;
  Formula<Integer> instr_control_flow_flushed;
  Formula<Integer> instr_flushed;
  Formula<Real> instr_flushed_ratio;

  // Issue
  Metric scheduler_cycles;
  Metric issue_cycles;
  Metric no_ready_warp_stall_cycles;
  Metric scoreboard_stall_cycles;
  Metric collect_queue_full_stall_cycles;
  Metric ready_warp_count_sum;
  Metric scoreboard_stall_with_control_hazard_cycles;
  Metric scoreboard_stall_with_data_hazard_cycles;
  Metric scoreboard_stall_with_mem_hazard_cycles;
  Metric scoreboard_stall_with_readbin_full_cycles;

  Formula<Real> avg_ready_warps;
  Formula<Real> issue_rate;
  Formula<Real> no_ready_warp_stall_ratio;
  Formula<Real> scoreboard_stall_ratio;
  Formula<Real> scoreboard_stall_with_control_hazard_ratio;
  Formula<Real> scoreboard_stall_with_data_hazard_ratio;
  Formula<Real> scoreboard_stall_with_mem_hazard_ratio;
  Formula<Real> scoreboard_stall_with_readbin_full_ratio;

  // Execution time
  Array arithmetic_instr_time;
  Array cond_branch_instr_time;
  Array uncond_jump_instr_time;
  Array load_instr_time;
  Array store_instr_time;
  Array amo_instr_time;
  Array system_instr_time;
  Array fp_instr_time;
  Array custom_instr_time;

  Formula<Real> avg_arithmetic_instr_time;
  Formula<Real> avg_cond_branch_instr_time;
  Formula<Real> avg_uncond_jump_instr_time;
  Formula<Real> avg_load_instr_time;
  Formula<Real> avg_store_instr_time;
  Formula<Real> avg_amo_instr_time;
  Formula<Real> avg_system_instr_time;
  Formula<Real> avg_fp_instr_time;
  Formula<Real> avg_custom_instr_time;

  Array stack_load_instr_time;
  Array stack_store_instr_time;
  Array non_stack_load_instr_time;
  Array non_stack_store_instr_time;

  Formula<Real> avg_stack_load_instr_time;
  Formula<Real> avg_stack_store_instr_time;
  Formula<Real> avg_non_stack_load_instr_time;
  Formula<Real> avg_non_stack_store_instr_time;

  // Backend - LSU
  Stats(const char *name, const ArchParam &param)
      : BaseCore::Stats(name, param),
        LV_STAT(active_cycles, "Number of active cycles"),
        LV_STAT(ipc, "Number of warp instructions per cycle"),
        LV_STAT(scheduler_cycles, "Number of backend scheduler cycles"),
        LV_STAT(issue_cycles, "Number of backend issue cycles"),
        LV_STAT(no_ready_warp_stall_cycles,
                "Number of scheduler cycles with no ready warp"),
        LV_STAT(scoreboard_stall_cycles,
                "Number of scheduler cycles blocked only by scoreboard"),
        LV_STAT(
            scoreboard_stall_with_control_hazard_cycles,
            "Number of scheduler cycles blocked only by scoreboard involving "
            "control hazard"),
        LV_STAT(
            scoreboard_stall_with_data_hazard_cycles,
            "Number of scheduler cycles blocked only by scoreboard involving "
            "data hazard"),
        LV_STAT(
            scoreboard_stall_with_mem_hazard_cycles,
            "Number of scheduler cycles blocked only by scoreboard involving "
            "memory access hazard"),
        LV_STAT(
            scoreboard_stall_with_readbin_full_cycles,
            "Number of scheduler cycles blocked only by scoreboard involving "
            "read bin is full"),
        LV_STAT(
            collect_queue_full_stall_cycles,
            "Number of scheduler cycles blocked by collect queue backpressure"),
        LV_STAT(ready_warp_count_sum,
                "Accumulated number of ready warps observed by schedulers"),
        LV_STAT(avg_ready_warps,
                "Average number of ready warps per scheduler cycle"),
        LV_STAT(issue_rate, "Backend issue cycles per scheduler cycle"),
        LV_STAT(no_ready_warp_stall_ratio,
                "Fraction of scheduler cycles with no ready warp"),
        LV_STAT(scoreboard_stall_ratio,
                "Fraction of scheduler cycles blocked only by scoreboard"),
        LV_STAT(scoreboard_stall_with_control_hazard_ratio,
                "Fraction of scoreboard stall cycles with control hazard"),
        LV_STAT(scoreboard_stall_with_data_hazard_ratio,
                "Fraction of scoreboard stall cycles with data hazard"),
        LV_STAT(
            scoreboard_stall_with_mem_hazard_ratio,
            "Fraction of scoreboard stall cycles with memory access hazard"),
        LV_STAT(scoreboard_stall_with_readbin_full_ratio,
                "Fraction of scoreboard stall cycles with read bin is full"),
        LV_STAT(fetch_due_to_starving,
                "Number of fetch requests due to warp starving"),
        LV_STAT(fetch_due_to_issuing,
                "Number of fetch requests due to warp issuing"),
        LV_STAT(num_fetches_filtered,
                "Number of fetch requests filtered by fetch filter"),
        LV_STAT(can_share_instr,
                "Number of events that the filling instruction can be shared "
                "with other warps"),
        LV_STAT(instr_shared,
                "Number of instructions that can be shared between warps"),
        LV_STAT(instr_filled, "Number of instructions filled to I-Buffer"),
        LV_STAT(ibuffer_full,
                "Number of cycles that when an instruction is to fill the "
                "I-Buffer but it is full"),
        LV_STAT(avg_instr_shared,
                "Average number of instructions that can be shared between "
                "warps"),
        LV_STAT(instr_sharing_opp,
                "Opportunity of an instruction being able to be shared with "
                "other warps"),
        LV_STAT(flush_jb_others,
                "Originals invalidated by other non-divergent jump/branch "
                "redirects (e.g. cond branch, non-return jalr)"),
        LV_STAT(flush_jal_forward,
                "Originals invalidated by jal forward (imm >= 0) redirect"),
        LV_STAT(flush_jal_backward,
                "Originals invalidated by jal backward (imm < 0) redirect"),
        LV_STAT(flush_jalr_return,
                "Originals invalidated by jalr-return (ret) redirect"),
        LV_STAT(flush_branch_divergence,
                "Originals invalidated by branch-divergence redirect"),
        LV_STAT(flush_duplicate_inst,
                "IWIS duplicate fetches discarded at broadcast"),
        LV_STAT(flush_misc,
                "Originals invalidated by non-control-flow, non-duplicate "
                "flushes (activate/priority/barrier/ecall/exception/...)"),
        LV_STAT(instr_control_flow_flushed,
                "Originals invalidated by control-flow redirects"),
        LV_STAT(instr_flushed,
                "Total invalidated originals (control-flow + duplicate + "
                "misc)"),
        LV_STAT(instr_flushed_ratio,
                "Ratio of invalidated originals to I-Buffer fills"),
        LV_STAT(
            arithmetic_instr_time,
            "Accumulated execution time of arithmetic instructions per warp",
            param.num_warps),
        LV_STAT(cond_branch_instr_time,
                "Accumulated execution time of conditional branch instructions "
                "per warp",
                param.num_warps),
        LV_STAT(uncond_jump_instr_time,
                "Accumulated execution time of unconditional jump instructions "
                "per warp",
                param.num_warps),
        LV_STAT(load_instr_time,
                "Accumulated execution time of load instructions per warp",
                param.num_warps),
        LV_STAT(store_instr_time,
                "Accumulated execution time of store instructions per warp",
                param.num_warps),
        LV_STAT(amo_instr_time,
                "Accumulated execution time of AMO instructions per warp",
                param.num_warps),
        LV_STAT(system_instr_time,
                "Accumulated execution time of system instructions per warp",
                param.num_warps),
        LV_STAT(fp_instr_time,
                "Accumulated execution time of floating-point instructions per "
                "warp",
                param.num_warps),
        LV_STAT(custom_instr_time,
                "Accumulated execution time of custom instructions per warp",
                param.num_warps),
        LV_STAT(
            stack_load_instr_time,
            "Accumulated execution time of stack load instructions per warp",
            param.num_warps),
        LV_STAT(
            stack_store_instr_time,
            "Accumulated execution time of stack store instructions per warp",
            param.num_warps),
        LV_STAT(non_stack_load_instr_time,
                "Accumulated execution time of non-stack load instructions per "
                "warp",
                param.num_warps),
        LV_STAT(
            non_stack_store_instr_time,
            "Accumulated execution time of non-stack store instructions per "
            "warp",
            param.num_warps),
        LV_STAT(avg_arithmetic_instr_time,
                "Average execution time of arithmetic instructions (ns)"),
        LV_STAT(
            avg_cond_branch_instr_time,
            "Average execution time of conditional branch instructions (ns)"),
        LV_STAT(
            avg_uncond_jump_instr_time,
            "Average execution time of unconditional jump instructions (ns)"),
        LV_STAT(avg_load_instr_time,
                "Average execution time of load instructions (ns)"),
        LV_STAT(avg_store_instr_time,
                "Average execution time of store instructions (ns)"),
        LV_STAT(avg_amo_instr_time,
                "Average execution time of AMO instructions (ns)"),
        LV_STAT(avg_system_instr_time,
                "Average execution time of system instructions (ns)"),
        LV_STAT(avg_fp_instr_time,
                "Average execution time of floating-point instructions (ns)"),
        LV_STAT(avg_custom_instr_time,
                "Average execution time of custom instructions (ns)"),
        LV_STAT(avg_stack_load_instr_time,
                "Average execution time of stack load instructions (ns)"),
        LV_STAT(avg_stack_store_instr_time,
                "Average execution time of stack store instructions (ns)"),
        LV_STAT(avg_non_stack_load_instr_time,
                "Average execution time of non-stack load instructions (ns)"),
        LV_STAT(avg_non_stack_store_instr_time,
                "Average execution time of non-stack store instructions (ns)") {
    ipc = total_instret / active_cycles;
    avg_ready_warps = ready_warp_count_sum / scheduler_cycles;
    issue_rate = issue_cycles / scheduler_cycles;
    no_ready_warp_stall_ratio = no_ready_warp_stall_cycles / scheduler_cycles;
    scoreboard_stall_ratio = scoreboard_stall_cycles / scheduler_cycles;
    scoreboard_stall_with_control_hazard_ratio =
        scoreboard_stall_with_control_hazard_cycles / scoreboard_stall_cycles;
    scoreboard_stall_with_data_hazard_ratio =
        scoreboard_stall_with_data_hazard_cycles / scoreboard_stall_cycles;
    scoreboard_stall_with_mem_hazard_ratio =
        scoreboard_stall_with_mem_hazard_cycles / scoreboard_stall_cycles;
    scoreboard_stall_with_readbin_full_ratio =
        scoreboard_stall_with_readbin_full_cycles / scoreboard_stall_cycles;
    avg_instr_shared = instr_shared / instr_filled;
    instr_sharing_opp = can_share_instr / instr_filled;
    instr_control_flow_flushed = flush_jb_others + flush_jal_forward +
                                 flush_jal_backward + flush_jalr_return +
                                 flush_branch_divergence;
    instr_flushed =
        instr_control_flow_flushed + flush_duplicate_inst + flush_misc;
    instr_flushed_ratio = instr_flushed / instr_filled;

    // Execution time conversion factor (ticks to nanoseconds)
    Real tick_to_ns =
        sc_get_time_resolution().to_seconds() / sc_time(1, SC_NS).to_seconds();

    avg_arithmetic_instr_time =
        (arithmetic_instr_time.sum() / committed_arithmetic_instr.sum()) *
        tick_to_ns;
    avg_cond_branch_instr_time =
        (cond_branch_instr_time.sum() / committed_cond_branch_instr.sum()) *
        tick_to_ns;
    avg_uncond_jump_instr_time =
        (uncond_jump_instr_time.sum() / committed_uncond_jump_instr.sum()) *
        tick_to_ns;
    avg_load_instr_time =
        (load_instr_time.sum() / committed_load_instr.sum()) * tick_to_ns;
    avg_store_instr_time =
        (store_instr_time.sum() / committed_store_instr.sum()) * tick_to_ns;
    avg_amo_instr_time =
        (amo_instr_time.sum() / committed_amo_instr.sum()) * tick_to_ns;
    avg_system_instr_time =
        (system_instr_time.sum() / committed_system_instr.sum()) * tick_to_ns;
    avg_fp_instr_time =
        (fp_instr_time.sum() / committed_fp_instr.sum()) * tick_to_ns;
    avg_custom_instr_time =
        (custom_instr_time.sum() / committed_custom_instr.sum()) * tick_to_ns;

    avg_stack_load_instr_time =
        (stack_load_instr_time.sum() / committed_stack_load_instr.sum()) *
        tick_to_ns;
    avg_stack_store_instr_time =
        (stack_store_instr_time.sum() / committed_stack_store_instr.sum()) *
        tick_to_ns;
    avg_non_stack_load_instr_time = (non_stack_load_instr_time.sum() /
                                     committed_non_stack_load_instr.sum()) *
                                    tick_to_ns;
    avg_non_stack_store_instr_time = (non_stack_store_instr_time.sum() /
                                      committed_non_stack_store_instr.sum()) *
                                     tick_to_ns;
  }

  void RecordInvalidation(FlushReason reason, uint64_t count = 1) {
    switch (reason) {
      case FlushReason::kJbOthers:
        flush_jb_others += count;
        break;
      case FlushReason::kJalForward:
        flush_jal_forward += count;
        break;
      case FlushReason::kJalBackward:
        flush_jal_backward += count;
        break;
      case FlushReason::kJalrReturn:
        flush_jalr_return += count;
        break;
      case FlushReason::kBranchDivergence:
        flush_branch_divergence += count;
        break;
      case FlushReason::kDuplicateInst:
        flush_duplicate_inst += count;
        break;
      case FlushReason::kMisc:
        flush_misc += count;
        break;
      case FlushReason::kUnknown:
        break;
      default:
        assert(false && "unhandled FlushReason in RecordInvalidation");
        break;
    }
  }
};

}  // namespace simtix::pipelined

template <>
struct fmt::formatter<simtix::pipelined::FlushReason>
    : formatter<std::string_view> {
  auto format(simtix::pipelined::FlushReason reason,
              format_context &ctx) const {
    return formatter<std::string_view>::format(
        simtix::pipelined::FlushCause(reason), ctx);
  }
};
