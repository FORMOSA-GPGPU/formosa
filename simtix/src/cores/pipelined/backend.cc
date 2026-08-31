// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include "cores/pipelined/backend.h"

#include <fmt/ostream.h>
#include <liblv/binding.h>

#include <cassert>

#include "cores/exec_context.h"
#include "cores/formosa.h"
#include "cores/pipelined/konata_labels.h"

#define WITH_TRACER(code)            \
  do {                               \
    if (auto *t = core_->tracer()) { \
      t->code;                       \
    }                                \
  } while (0)

namespace simtix::pipelined {

namespace {

uint32_t CountBits(const sc_bv_base &bits, uint32_t width) {
  uint32_t count = 0;
  for (uint32_t i = 0; i < width; ++i) count += bits[i] == 1;
  return count;
}

/**
 * Decode mtval for traps observed by the pipelined backend.
 *
 * - ECALL reports zero.
 * - Illegal instructions report the raw instruction word.
 * - Instruction-address misalignment reports the first active lane's misaligned
 * target address.
 * - Load/store address misalignment reports the first active lane's
 *   misaligned effective address.
 */
uint64_t DecodeTrapVal(const Packet *packet, uint32_t num_lanes) {
  switch (GetException(packet->flag)) {
    case ExecFlag::ECALL:
      return 0;
    case ExecFlag::ILLEGAL:
      return packet->iword;
    case ExecFlag::INSTR_ADDR_MISALIGNED:
      for (uint32_t i = 0; i < num_lanes; ++i) {
        if (packet->tmask[i] == 1 && packet->addr_buf[i] % 4 != 0) {
          return packet->addr_buf[i];
        }
      }
      return 0;
    case ExecFlag::LOAD_ADDR_MISALIGNED:
    case ExecFlag::STORE_ADDR_MISALIGNED: {
      uint32_t alignment = GetMemSize(packet->flag);
      for (uint32_t i = 0; i < num_lanes; ++i) {
        if (packet->tmask[i] == 1 && packet->addr_buf[i] % alignment != 0) {
          return packet->addr_buf[i];
        }
      }
      return 0;
    }
    default:
      return 0;
  }
}

}  // namespace

void Backend::Issue() {
  if (!sched_) {
    assert(0);  // scheduler is not set!
  }

  UpdateReadyWarps();

  if (!core_->active_warps().val().or_reduce()) {
    // no active warps
    return;
  }

  auto *stats = core_->stats();
  stats->scheduler_cycles++;
  stats->ready_warp_count_sum += CountBits(ready_warps_, num_warps_);

  if (ready_warps_ == 0) {
    stats->no_ready_warp_stall_cycles++;
    return;
  }

  if (!collect_q_.nb_can_put()) {
    stats->collect_queue_full_stall_cycles++;
    return;
  }

  uint32_t wid = sched_->SelectWarp(ready_warps_);
  stats->issue_cycles++;
  Packet *packet = from_frontend[get_local_wid(wid)]->get();
  WITH_TRACER(StartStage(packet, 0, "Is"));

  // A zero tmask means capture is pending. GhOST captures at in-order
  // dispatch and guarantees a nonzero mask; PacketPool resets it on release.
  if (packet->tmask == 0) {
    core_->CaptureThreadMask(packet);
  }

  WITH_TRACER(AddComment(
      packet, fmt::format("tmask = {}\\n", fmt::streamed(packet->tmask))));

  // TODO: This is primarily resulted by the resolved branch plus immediate
  // issue. To fix this, either prevent from issuing the same warp as the one
  // that commits a branch, or clear the scoreboard 1 cycle after the flush.
  if (packet->tmask == 0) {
    FlushPacket(packet, FlushReason::kMisc);
    return;
  }

  scoreboard_.Issue(packet);
  collect_q_.nb_put(packet);
  inflight_counter_[get_local_wid(packet->wid)]++;
}

void Backend::Execute1() {
  if (!execute1_q_.nb_can_get() || !execute2_q_.nb_can_put()) {
    return;
  }

  Packet *packet = execute1_q_.get();
  WITH_TRACER(StartStage(packet, 0, "X1"));

  ExecContext ctx = {
      .pc = packet->wpc,
      .tmask = packet->tmask,
      .rs1_data = packet->rs1_data.data(),
      .rs2_data = packet->rs2_data.data(),
      .rs3_data = packet->rs3_data.data(),
      .dynamic_rm = &core_->ptfrm()[packet->wid * num_lanes_],
      .num_lanes = num_lanes_,
      .rd_data = packet->data_buf.data(),
      .next_pc = packet->addr_buf.data(),
      .mem = {.addr = packet->addr_buf.data(), .data = packet->data_buf.data()},
      .csr = {.addr = &packet->csr_buf, .data = packet->data_buf.data()},
      .pri = &packet->pri_buf,
      .fflags = packet->fflags_buf.data(),
  };
  packet->flag = simtix::Execute(&ctx, packet->instr);
  packet->timestamp = sc_time_stamp();
  execute2_q_.put(packet);
}

void Backend::Execute2() {
  Packet *packet = nullptr;
  if (!execute2_q_.nb_peek(packet)) {
    return;
  }

  uint32_t wid = packet->wid;
  ExecFlag flag = packet->flag;
  bool retire = false;

  if (HasFlag(flag, ExecFlag::RD_DATA)) {
    // Route the packet to the function unit selected by its flags. FPU and MDU
    // have dedicated selectors; everything else (including JAL and JALR) falls
    // back to the ALU.
    if (HasFlag(flag, ExecFlag::FPU)) {
      if (!fpu_req_q_.nb_put(packet)) {
        return;
      }
    } else if (HasFlag(flag, ExecFlag::MDU)) {
      if (!mdu_req_q_.nb_put(packet)) {
        return;
      }
    } else {
      if (!alu_req_q_.nb_put(packet)) {
        return;
      }
    }
  }

  if (HasFlag(flag, ExecFlag::NEXT_PC)) {
    UpdatePC(packet);
    bool is_uncond_jump = HasFlag(flag, ExecFlag::RD_DATA);
    // If this instruction is not a JAL or JALR, it can be retired directly
    // after updating PC.
    retire = !is_uncond_jump;
  }

  // Load, store, and atomic instructions go to LSU.
  if (HasFlag(flag, ExecFlag::ATOMIC)) {
    if (!lsu_req_q_.nb_put(packet)) {
      return;
    }
  }

  if (HasFlag(flag, ExecFlag::LOAD)) {
    if (!lsu_req_q_.nb_put(packet)) {
      return;
    }
  }

  if (HasFlag(flag, ExecFlag::STORE)) {
    if (!lsu_req_q_.nb_put(packet)) {
      return;
    }
  }

  // CSR instructions goes to ALU after executing the CSR operation.
  if (HasFlag(flag, ExecFlag::CSR)) {
    if (!alu_req_q_.nb_put(packet)) {
      return;
    }
    ExecuteCsrOp(packet);
  }

  // Accrue floating point exceptions
  if (HasFlag(flag, ExecFlag::FP_EXC)) {
    UpdateFflags(packet);
  }

  // Priority instructions retire directly after updating the priority.
  if (HasFlag(flag, ExecFlag::PRIORITY)) {
    UpdatePri(packet);
    // Only pri.lower and pri.reset require PC arbitration and redirection
    if (GetPriOp(flag) == ExecFlag::PRI_LOWER ||
        GetPriOp(flag) == ExecFlag::PRI_RESET) {
      uint64_t next_pc = ArbitratePC(packet->wid);
      core_->NoteFlush(packet->wid, FlushReason::kMisc, next_pc, 0);
      core_->Redirect(packet->wid, next_pc);
      issue_suppressed_warps_[packet->wid] = 1;
    }
    retire = true;
  }

  if (HasFlag(flag, ExecFlag::BARRIER)) {
    uint32_t local_wid = get_local_wid(packet->wid);

    // If there are still threads awaiting execution, arbitrate PC for them
    if (!pending_barrier_threads_.MarkReachedAndCheckAll(local_wid,
                                                         packet->tmask)) {
      uint64_t next_pc = ArbitratePC(packet->wid);
      core_->NoteFlush(packet->wid, FlushReason::kMisc, next_pc, 0);
      core_->Redirect(packet->wid, next_pc);
      issue_suppressed_warps_[packet->wid] = 1;
    }

    retire = true;
  }

  if (HasFlag(flag, ExecFlag::ECALL)) {
    uint32_t local_wid = get_local_wid(packet->wid);
    pending_ecall_wpc_[local_wid] = packet->wpc;

    // If there are still threads awaiting execution, arbitrate PC for them
    if (!pending_ecall_threads_.MarkReachedAndCheckAll(local_wid,
                                                       packet->tmask)) {
      uint64_t next_pc = ArbitratePC(packet->wid);
      core_->NoteFlush(packet->wid, FlushReason::kMisc, next_pc, 0);
      core_->Redirect(packet->wid, next_pc);
      issue_suppressed_warps_[packet->wid] = 1;
    }

    retire = true;
  }

  if (HasException(flag) && !HasFlag(flag, ExecFlag::ECALL)) {
    uint32_t local_wid = get_local_wid(packet->wid);
    auto &pending_exception = pending_exceptions_[local_wid];
    if (!pending_exception.valid) {
      // Freeze the warp at the first observed non-ECALL exception, then wait
      // until all older in-flight work drains before transitioning warp state.
      pending_exception.valid = true;
      pending_exception.wpc = packet->wpc;
      pending_exception.cause = DecodeExceptionCause(flag);
      pending_exception.tval = DecodeTrapVal(packet, num_lanes_);
    }
    retire = true;
  }

  if (packet->instr.is_control()) {
    core_->NotifyControlResolved(packet->wid, packet->unique_id);
  }

  WITH_TRACER(StartStage(packet, 0, "X2"));
  execute2_q_.get();

  if (retire) {
    scoreboard_.Commit(packet);
    RetirePacket(packet);
  }
}

void Backend::Writeback() {
  if (!writeback_q_.nb_can_put()) {
    return;
  }

  // Choose a function unit for writeback in round-robin order
  Packet *packet = nullptr;
  for (uint32_t i = 0; i < fus_.size(); ++i) {
    uint32_t fuid = (prioritized_fu_ + i) % fus_.size();
    if (fus_[fuid]->nb_get(packet)) {
      writeback_q_.put(packet);
      prioritized_fu_ = (fuid + 1) % fus_.size();
      break;
    }
  }
}

void Backend::Retire() {
  Packet *packet = nullptr;
  if (retire_q_.nb_get(packet)) {
    scoreboard_.Commit(packet);
    RetirePacket(packet);
    WITH_TRACER(StartStage(packet, 0, "Cm"));
  }
}

void Backend::ProcessPendingEcalls() {
  const auto &active_warps = core_->active_warps().val();

  for (uint32_t local_wid = 0; local_wid < num_local_warps_; ++local_wid) {
    uint32_t wid = get_wid(local_wid);
    bool is_active = active_warps[wid] == 1;

    if (!is_active) {
      // Ignore any pending ecall state for warps that are no longer active.
      pending_ecall_threads_.ClearIfPending(local_wid);
      continue;
    }

    // Notify ecall only when
    // 1. All threads in this warp reach the ecall state
    // 2. There is not any inflight instruction for this warp
    // 3. There is not any pending non-ECALL exception for this warp, which
    // would have a higher priority than ecall
    if (pending_ecall_threads_.AllReached(local_wid) &&
        inflight_counter_[local_wid] == 0 &&
        !pending_exceptions_[local_wid].valid) {
      core_->NotifyEcall(wid, pending_ecall_wpc_[local_wid]);
      pending_ecall_threads_.ClearIfPending(local_wid);
    }
  }
}

void Backend::ProcessPendingExceptions() {
  // Notify exceptions only when
  // 1. Any thread in this warp reaches the exception state
  // 2. There is not any inflight instruction for this warp
  const auto &active_warps = core_->active_warps().val();

  for (uint32_t local_wid = 0; local_wid < num_local_warps_; ++local_wid) {
    uint32_t wid = get_wid(local_wid);
    bool is_active = active_warps[wid] == 1;
    auto &pending_exception = pending_exceptions_[local_wid];

    if (pending_exception.valid && inflight_counter_[local_wid] == 0) {
      if (is_active) {
        core_->NotifyException(wid, pending_exception.wpc,
                               pending_exception.cause, pending_exception.tval);
      }

      // Clear any lower-priority pending state after the warp's final trap is
      // resolved so later passes do not try to transition it again.
      pending_exception.valid = false;
      pending_ecall_threads_.ClearIfPending(local_wid);
      pending_barrier_threads_.ClearIfPending(local_wid);
    }
  }
}

void Backend::ProcessPendingBarriers() {
  const auto &active_warps = core_->active_warps().val();

  for (uint32_t local_wid = 0; local_wid < num_local_warps_; ++local_wid) {
    uint32_t wid = get_wid(local_wid);
    bool is_active = active_warps[wid] == 1;

    if (!is_active) {
      // Ignore any pending barrier state for warps that are no longer active.
      pending_barrier_threads_.ClearIfPending(local_wid);
      continue;
    }

    // Notify barrier only when
    // 1. All threads in this warp reach the barrier state
    // 2. There is not any inflight instruction for this warp
    // 3. There is not any pending non-ECALL exception for this warp, which
    // would have a higher priority than barrier
    if (pending_barrier_threads_.AllReached(local_wid) &&
        inflight_counter_[local_wid] == 0 &&
        !pending_exceptions_[local_wid].valid) {
      core_->NotifyBarrier(wid);
      pending_barrier_threads_.ClearIfPending(local_wid);
    }
  }
}

void Backend::KonataRetire() {
  for (const auto &entry : retire_pool_) {
    if (entry.is_flush) {
      WITH_TRACER(ArchitecturalFlush(entry.packet,
                                     KonataFlushLabel(entry.flush_reason)));
    } else {
      WITH_TRACER(Retire(entry.packet));
    }
    core_->FreePacket(entry.packet);
  }
  retire_pool_.clear();
}

void Backend::UpdateReadyWarps() {
  auto *stats = core_->stats();
  const auto &active_warps = core_->active_warps().val();

  ready_warps_ = 0;
  bool has_scoreboard_blocked_warp = false;

  bool has_control_hazards = false;
  bool has_data_hazards = false;
  bool has_mem_hazards = false;
  bool has_readbin_full = false;

  for (uint32_t local_wid = 0; local_wid < num_local_warps_; ++local_wid) {
    uint32_t wid = get_wid(local_wid);
    bool is_active = active_warps[wid] == 1;
    bool is_barrier = pending_barrier_threads_.AllReached(local_wid);
    bool is_ecall = pending_ecall_threads_.AllReached(local_wid);
    bool is_exception = pending_exceptions_[local_wid].valid;
    bool is_suppressed = issue_suppressed_warps_[wid] == 1;

    // The warp is ready for execution only when it is active, not barrier, and
    // not ecall or exception
    if (is_active && !is_barrier && !is_ecall && !is_exception &&
        !is_suppressed) {
      // Furthermore, the first instruction in the ibuffer must be ready (i.e.,
      // has no dependencies)
      Packet *packet = nullptr;
      bool has_packet = from_frontend[local_wid]->nb_peek(packet);
      IssueStallReason reason = IssueStallReason::kNone;
      bool can_issue = has_packet && scoreboard_.CanIssue(packet, &reason);
      ready_warps_[wid] = can_issue;

      if (has_packet && !can_issue) {
        has_scoreboard_blocked_warp = true;
        has_control_hazards |= reason == IssueStallReason::kControlHazard;
        has_data_hazards |= reason == IssueStallReason::kDataHazard;
        has_mem_hazards |= reason == IssueStallReason::kMemHazard;
        has_readbin_full |= reason == IssueStallReason::kReadBinFull;
      }
    }
  }
  if (ready_warps_ == 0 && has_scoreboard_blocked_warp) {
    stats->scoreboard_stall_cycles++;
    stats->scoreboard_stall_with_control_hazard_cycles += has_control_hazards;
    stats->scoreboard_stall_with_data_hazard_cycles += has_data_hazards;
    stats->scoreboard_stall_with_mem_hazard_cycles += has_mem_hazards;
    stats->scoreboard_stall_with_readbin_full_cycles += has_readbin_full;
  }
}

void Backend::UpdatePC(Packet *packet) {
  uint64_t *pwpc = &core_->ptpc()[packet->wid * num_lanes_];
  for (uint32_t i = 0; i < num_lanes_; ++i) {
    if (packet->tmask[i] == 1) {
      pwpc[i] = packet->addr_buf[i];
    }
  }
  uint64_t wpc = ArbitratePC(packet->wid);
  const FlushReason reason = ClassifyRedirectReason(*packet, num_lanes_);
  // Skip Konata dependency arrows for branch/jalr divergence (one producer
  // would otherwise need to fan out to multiple targets).
  const uint64_t producer_id =
      reason == FlushReason::kBranchDivergence ? 0 : packet->unique_id;
  core_->NoteFlush(packet->wid, reason, wpc, producer_id);
  core_->Redirect(packet->wid, wpc);
  issue_suppressed_warps_[packet->wid] = 1;
}

uint64_t Backend::ArbitratePC(uint32_t wid) {
  bool found = false;
  uint32_t local_wid = get_local_wid(wid);
  uint32_t lane_id = 0;
  uint8_t max_pri = 0;
  uint8_t *pwpri = &core_->ptpri()[wid * num_lanes_];
  for (uint32_t i = 0; i < num_lanes_; ++i) {
    // Skip this thread if it already reaches barrier or ecall state.
    if (pending_barrier_threads_.LaneReached(local_wid, i) ||
        pending_ecall_threads_.LaneReached(local_wid, i)) {
      continue;
    }

    if (!found || pwpri[i] > max_pri) {
      found = true;
      lane_id = i;
      max_pri = pwpri[i];
    }
  }
  assert(found);
  return core_->ptpc()[wid * num_lanes_ + lane_id];
}

void Backend::ExecuteCsrOp(Packet *packet) {
  ExecFlag op = GetCsrOp(packet->flag);
  uint32_t wid = packet->wid;
  for (uint32_t i = 0; i < num_lanes_; ++i) {
    if (packet->tmask[i] == 1) {
      uint64_t old_val = 0;
      uint64_t mask = packet->data_buf[i];

      auto apply_op = [&](uint64_t current) {
        if (op == ExecFlag::CSRRW) return mask;
        if (op == ExecFlag::CSRRS) return current | mask;
        if (op == ExecFlag::CSRRC) return current & ~mask;
        return current;
      };

      switch (packet->csr_buf) {
        case CSR_CYCLE:
        case CSR_MCYCLE:
          old_val = core_->mcycle();
          break;
        case CSR_MHARTID:
          old_val = core_->cwid()[wid] * num_lanes_ + i;
          break;
        case CSR_MSCRATCH:
          old_val = core_->mscratch()[wid];
          core_->mscratch()[wid] = apply_op(old_val);
          break;
        case CSR_MCAUSE:
          old_val = core_->mcause()[wid];
          core_->mcause()[wid] = apply_op(old_val);
          break;
        case CSR_MEPC:
          old_val = core_->mepc()[wid];
          core_->mepc()[wid] = apply_op(old_val);
          break;
        case CSR_MTVAL:
          old_val = core_->mtval()[wid];
          core_->mtval()[wid] = apply_op(old_val);
          break;
        case CSR_MINSTRET:
          old_val = core_->minstret()[wid];
          core_->minstret()[wid] = apply_op(old_val);
          break;
        case CSR_FFLAGS:
          old_val = core_->ptfflags()[wid * num_lanes_ + i] & 0x1f;
          core_->ptfflags()[wid * num_lanes_ + i] = apply_op(old_val) & 0x1f;
          break;
        case CSR_FRM:
          old_val = core_->ptfrm()[wid * num_lanes_ + i] & 0x7;
          core_->ptfrm()[wid * num_lanes_ + i] = apply_op(old_val) & 0x7;
          break;
        case CSR_FCSR: {
          old_val = (core_->ptfrm()[wid * num_lanes_ + i] << 5) |
                    (core_->ptfflags()[wid * num_lanes_ + i] & 0x1f);
          uint64_t new_val = apply_op(old_val);
          core_->ptfrm()[wid * num_lanes_ + i] = (new_val >> 5) & 0x7;
          core_->ptfflags()[wid * num_lanes_ + i] = new_val & 0x1f;
          break;
        }
        case CSR_XWID:
          old_val = wid;
          break;
        case CSR_XLANEID:
          old_val = i;
          break;
        case CSR_XLANES:
          old_val = num_lanes_;
          break;
        default:
          LV_FATAL("Unsupported CSR address: {:#x}", packet->csr_buf);
          break;
      }
      packet->data_buf[i] = old_val;
    }
  }
}

void Backend::UpdateFflags(Packet *packet) {
  uint8_t *pwfflags = &core_->ptfflags()[packet->wid * num_lanes_];
  for (uint32_t i = 0; i < num_lanes_; ++i) {
    if (packet->tmask[i] == 1) {
      pwfflags[i] |= packet->fflags_buf[i] & 0x1f;
    }
  }
}

void Backend::UpdatePri(Packet *packet) {
  ExecFlag op = GetPriOp(packet->flag);
  int8_t factor = op != ExecFlag::PRI_RESET;
  int8_t sign = op == ExecFlag::PRI_RAISE ? 1 : -1;
  uint8_t *pwpri = &core_->ptpri()[packet->wid * num_lanes_];
  for (uint32_t i = 0; i < num_lanes_; ++i) {
    if (packet->tmask[i] == 1) {
      pwpri[i] = factor * (pwpri[i] + sign * packet->pri_buf);
    }
  }
}

void Backend::FlushPacket(Packet *packet, FlushReason reason) {
  if (!packet->is_shared) {
    core_->stats()->RecordInvalidation(reason);
  }
  retire_pool_.push_back(
      {.is_flush = true, .flush_reason = reason, .packet = packet});
}

void Backend::RetirePacket(Packet *packet) {
  uint32_t wid = packet->wid;
  ExecFlag flag = packet->flag;
  inflight_counter_[get_local_wid(wid)]--;
  retire_pool_.push_back({.is_flush = false, .packet = packet});

  // Pre-calculate flags to avoid redundant function calls
  bool has_rd_data = HasFlag(flag, ExecFlag::RD_DATA);
  bool has_next_pc = HasFlag(flag, ExecFlag::NEXT_PC);
  bool is_arithmetic = (has_rd_data && !has_next_pc);
  bool is_cond_branch = (has_next_pc && !has_rd_data);
  bool is_uncond_jump = (has_next_pc && has_rd_data);
  bool is_load = HasFlag(flag, ExecFlag::LOAD);
  bool is_store = HasFlag(flag, ExecFlag::STORE);
  bool is_amo = HasFlag(flag, ExecFlag::ATOMIC);
  bool is_csr = HasFlag(flag, ExecFlag::CSR);
  bool is_fp = HasFlag(flag, ExecFlag::FPU);
  bool is_priority = HasFlag(flag, ExecFlag::PRIORITY);
  bool is_barrier = HasFlag(flag, ExecFlag::BARRIER);
  bool is_ecall = HasFlag(flag, ExecFlag::ECALL);
  bool is_custom = (is_priority || is_barrier);
  bool is_stack = packet->is_stack_access;

  pipelined::Stats *stats = core_->stats();
  uint64_t elapsed = (sc_time_stamp() - packet->timestamp).value();

  // 1. Control Flow & Arithmetic (Mutually Exclusive Logic)
  stats->committed_arithmetic_instr[wid] += is_arithmetic;
  stats->arithmetic_instr_time[wid] += is_arithmetic * elapsed;

  stats->committed_cond_branch_instr[wid] += is_cond_branch;
  stats->cond_branch_instr_time[wid] += is_cond_branch * elapsed;

  stats->committed_uncond_jump_instr[wid] += is_uncond_jump;
  stats->uncond_jump_instr_time[wid] += is_uncond_jump * elapsed;

  // 2. Memory Operations
  stats->committed_load_instr[wid] += is_load;
  stats->load_instr_time[wid] += is_load * elapsed;

  stats->committed_non_stack_load_instr[wid] += is_load * !is_stack;
  stats->non_stack_load_instr_time[wid] += is_load * !is_stack * elapsed;

  stats->committed_stack_load_instr[wid] += is_load * is_stack;
  stats->stack_load_instr_time[wid] += is_load * is_stack * elapsed;

  stats->committed_store_instr[wid] += is_store;
  stats->store_instr_time[wid] += is_store * elapsed;

  stats->committed_non_stack_store_instr[wid] += is_store * !is_stack;
  stats->non_stack_store_instr_time[wid] += is_store * !is_stack * elapsed;

  stats->committed_stack_store_instr[wid] += is_store * is_stack;
  stats->stack_store_instr_time[wid] += is_store * is_stack * elapsed;

  stats->committed_amo_instr[wid] += is_amo;
  stats->amo_instr_time[wid] += is_amo * elapsed;

  // 3. System Instructions (Consolidated)
  bool is_system = (is_csr || is_ecall);
  stats->committed_system_instr[wid] += is_system;
  stats->system_instr_time[wid] += is_system * elapsed;

  // 4. Floating Point & Custom
  stats->committed_fp_instr[wid] += is_fp;
  stats->fp_instr_time[wid] += is_fp * elapsed;

  stats->committed_custom_instr[wid] += is_custom;
  stats->custom_instr_time[wid] += is_custom * elapsed;

  core_->minstret()[wid]++;
  core_->stats()->warp_instret[wid]++;
  for (uint32_t i = 0; i < num_lanes_; ++i) {
    if (packet->tmask[i]) {
      core_->stats()->lane_instret[i]++;
    }
  }
}

#undef WITH_TRACER

LV_BINDING(simtix, Backend)
    .method("sched_init", &Backend::sched_init,
            lv::params(
                lv::param("sched_init",
                          lv::lua_type("fun(name: string): simtix.WarpSched"))),
            lv::doc("Register the scheduler factory"))
    .method("arbitrator_init", &Backend::arbitrator_init,
            lv::params(lv::param(
                "arbitrator_init",
                lv::lua_type("fun(name: string): simtix.ArbitratorIntf"))),
            lv::doc("Register the arbitrator factory"))
    .method("lsu_init", &Backend::lsu_init,
            lv::params(lv::param(
                "lsu_init", lv::lua_type("fun(name: string): simtix.Lsu"))),
            lv::doc("Register the LSU factory"))
    .property("dmem", &Backend::set_target, lv::doc("Data memory target"));

}  // namespace simtix::pipelined
