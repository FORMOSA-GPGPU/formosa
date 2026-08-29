// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include <liblv/output.h>

#include <cassert>

#include "cache.h"

namespace simtix::cache {

void Cache::LogRepeatedStall(const char *reason, uint64_t *counter) {
  assert(counter != nullptr);
  MarkBlockReason(reason, false);
  if (cache_log::ShouldLogRepeated(*counter)) {
    LogQueueSnapshot(cache_log::Category::kStall, "blocked", reason);
  }
  ++(*counter);
}

void Cache::BeginDeadlockWatchdogTick() {
  if (!deadlock_watchdog_.enabled) {
    return;
  }
  deadlock_watchdog_.tick_internal_block_seen = false;
  deadlock_watchdog_.tick_external_block_seen = false;
}

void Cache::MarkProgress(const char *reason) {
  if (!deadlock_watchdog_.enabled) {
    return;
  }
  ++progress_epoch_;
  deadlock_watchdog_.last_progress_reason =
      reason != nullptr ? reason : "unknown";
}

void Cache::MarkBlockReason(const char *reason, bool external_wait) {
  if (!deadlock_watchdog_.enabled) {
    return;
  }

  if (external_wait) {
    deadlock_watchdog_.tick_external_block_seen = true;
    if (!deadlock_watchdog_.tick_internal_block_seen) {
      deadlock_watchdog_.last_block_reason =
          reason != nullptr ? reason : "unknown";
      deadlock_watchdog_.last_external_wait = true;
    }
    return;
  }

  deadlock_watchdog_.tick_internal_block_seen = true;
  deadlock_watchdog_.last_block_reason = reason != nullptr ? reason : "unknown";
  deadlock_watchdog_.last_external_wait = false;
}

Cache::WatchdogSnapshot Cache::MakeWatchdogSnapshot() const {
  return WatchdogSnapshot{
      .core_req = core_req_queue_.used(),
      .core_resp = core_resp_queue_.used(),
      .mmio_resp = mmio_resp_queue_.used(),
      .tag_array_resp = tag_array_resp_queue_.used(),
      .mshr_file_mem_req = mshr_file_mem_req_queue_.used(),
      .mshr_file_refill_notify = mshr_file_refill_notify_queue_.used(),
      .mshr_file_replay = mshr_file_replay_queue_.used(),
      .write_buffer_mem_req = write_buffer_mem_req_queue_.used(),
      .write_buffer_mem_req_out = write_buffer_mem_req_out_queue_.used(),
      .write_buffer_mem_resp = write_buffer_mem_resp_queue_.used(),
      .bypass_req = bypass_req_queue_.used(),
      .mem_resp = mem_resp_queue_.used(),
      .mem_inflight_packets = mem_inflight_packets_.size(),
      .line_escape_hazards = line_escape_hazards_.size(),
      .mshr_pending = mshr_file_.HasPendingWork(),
      .mshr_replay = mshr_file_.HasReplayWork(),
      .write_buffer_pending = write_buffer_.HasPendingWork(),
      .write_buffer_pending_entries = write_buffer_.PendingEntryCount(),
      .write_buffer_inflight_entries = write_buffer_.InflightEntryCount(),
      .victim_buffer_pending = victim_buffer_.HasPendingWork(),
      .victim_buffer_reserved_entries = victim_buffer_.ReservedEntryCount(),
      .victim_buffer_committed_entries = victim_buffer_.CommittedEntryCount(),
      .victim_buffer_inflight_entries = victim_buffer_.InflightEntryCount(),
      .atomic_busy = atomic_sequencer_.IsBusy(),
      .mmio_busy = mmio_sequencer_.IsBusy(),
      .mmio_phase = mmio_sequencer_.phase,
      .mmio_start = mmio_start_,
      .mmio_scan_address = mmio_sequencer_.scan_address,
      .mmio_scan_index = mmio_sequencer_.scan_index,
  };
}

bool Cache::HasWatchdogPendingWork(const WatchdogSnapshot &snapshot) const {
  return snapshot.core_req > 0 || snapshot.core_resp > 0 ||
         snapshot.mmio_resp > 0 || snapshot.tag_array_resp > 0 ||
         snapshot.mshr_file_mem_req > 0 ||
         snapshot.mshr_file_refill_notify > 0 ||
         snapshot.mshr_file_replay > 0 || snapshot.write_buffer_mem_req > 0 ||
         snapshot.write_buffer_mem_req_out > 0 ||
         snapshot.write_buffer_mem_resp > 0 || snapshot.bypass_req > 0 ||
         snapshot.mem_resp > 0 || snapshot.mem_inflight_packets > 0 ||
         snapshot.line_escape_hazards > 0 || snapshot.mshr_pending ||
         snapshot.mshr_replay || snapshot.write_buffer_pending ||
         snapshot.victim_buffer_pending || snapshot.atomic_busy ||
         snapshot.mmio_busy;
}

bool Cache::WatchdogSnapshotsEqual(const WatchdogSnapshot &lhs,
                                   const WatchdogSnapshot &rhs) {
  return lhs.core_req == rhs.core_req && lhs.core_resp == rhs.core_resp &&
         lhs.mmio_resp == rhs.mmio_resp &&
         lhs.tag_array_resp == rhs.tag_array_resp &&
         lhs.mshr_file_mem_req == rhs.mshr_file_mem_req &&
         lhs.mshr_file_refill_notify == rhs.mshr_file_refill_notify &&
         lhs.mshr_file_replay == rhs.mshr_file_replay &&
         lhs.write_buffer_mem_req == rhs.write_buffer_mem_req &&
         lhs.write_buffer_mem_req_out == rhs.write_buffer_mem_req_out &&
         lhs.write_buffer_mem_resp == rhs.write_buffer_mem_resp &&
         lhs.bypass_req == rhs.bypass_req && lhs.mem_resp == rhs.mem_resp &&
         lhs.mem_inflight_packets == rhs.mem_inflight_packets &&
         lhs.line_escape_hazards == rhs.line_escape_hazards &&
         lhs.mshr_pending == rhs.mshr_pending &&
         lhs.mshr_replay == rhs.mshr_replay &&
         lhs.write_buffer_pending == rhs.write_buffer_pending &&
         lhs.write_buffer_pending_entries == rhs.write_buffer_pending_entries &&
         lhs.write_buffer_inflight_entries ==
             rhs.write_buffer_inflight_entries &&
         lhs.victim_buffer_pending == rhs.victim_buffer_pending &&
         lhs.victim_buffer_reserved_entries ==
             rhs.victim_buffer_reserved_entries &&
         lhs.victim_buffer_committed_entries ==
             rhs.victim_buffer_committed_entries &&
         lhs.victim_buffer_inflight_entries ==
             rhs.victim_buffer_inflight_entries &&
         lhs.atomic_busy == rhs.atomic_busy && lhs.mmio_busy == rhs.mmio_busy &&
         lhs.mmio_phase == rhs.mmio_phase && lhs.mmio_start == rhs.mmio_start &&
         lhs.mmio_scan_address == rhs.mmio_scan_address &&
         lhs.mmio_scan_index == rhs.mmio_scan_index;
}

void Cache::CheckDeadlockWatchdog() {
  if (!deadlock_watchdog_.enabled) {
    return;
  }

  if (progress_epoch_ != deadlock_watchdog_.last_progress_epoch) {
    deadlock_watchdog_.stalled_cycles = 0;
    deadlock_watchdog_.tripped = false;
    deadlock_watchdog_.last_progress_epoch = progress_epoch_;
    return;
  }

  WatchdogSnapshot snapshot = MakeWatchdogSnapshot();
  if (!HasWatchdogPendingWork(snapshot)) {
    deadlock_watchdog_.stalled_cycles = 0;
    deadlock_watchdog_.tripped = false;
    deadlock_watchdog_.last_snapshot = snapshot;
    deadlock_watchdog_.last_external_wait = false;
    return;
  }

  const bool snapshot_changed =
      !WatchdogSnapshotsEqual(snapshot, deadlock_watchdog_.last_snapshot);
  if (snapshot_changed) {
    deadlock_watchdog_.stalled_cycles = 0;
    deadlock_watchdog_.tripped = false;
    deadlock_watchdog_.last_snapshot = snapshot;
    return;
  }

  const bool external_only = deadlock_watchdog_.last_external_wait &&
                             !deadlock_watchdog_.tick_internal_block_seen;
  if (external_only) {
    deadlock_watchdog_.stalled_cycles = 0;
    return;
  }

  ++deadlock_watchdog_.stalled_cycles;
  if (deadlock_watchdog_.tripped ||
      deadlock_watchdog_.stalled_cycles < deadlock_watchdog_.threshold_cycles) {
    return;
  }

  deadlock_watchdog_.tripped = true;
  LogQueueSnapshot(cache_log::Category::kStall, "watchdog",
                   deadlock_watchdog_.last_block_reason);
  if (deadlock_watchdog_.fatal) {
    LV_FATAL(
        "{} cache cat=stall event=deadlock_suspect stalled_cycles={} "
        "threshold={} last_progress={} last_block={} external_wait={} "
        "q_core_req={} q_tag_resp={} q_mshr_mem={} q_mshr_notify={} "
        "q_mshr_replay={} q_wb_in={} q_wb_out={} q_wb_resp={} q_bypass={} "
        "q_mem_resp={} inflight={} hazards={} victim_reserved={} "
        "victim_committed={} victim_inflight={}",
        name(), deadlock_watchdog_.stalled_cycles,
        deadlock_watchdog_.threshold_cycles,
        deadlock_watchdog_.last_progress_reason,
        deadlock_watchdog_.last_block_reason,
        deadlock_watchdog_.last_external_wait, snapshot.core_req,
        snapshot.tag_array_resp, snapshot.mshr_file_mem_req,
        snapshot.mshr_file_refill_notify, snapshot.mshr_file_replay,
        snapshot.write_buffer_mem_req, snapshot.write_buffer_mem_req_out,
        snapshot.write_buffer_mem_resp, snapshot.bypass_req, snapshot.mem_resp,
        snapshot.mem_inflight_packets, snapshot.line_escape_hazards,
        snapshot.victim_buffer_reserved_entries,
        snapshot.victim_buffer_committed_entries,
        snapshot.victim_buffer_inflight_entries);
  }

  LV_ERROR(
      "{} cache cat=stall event=deadlock_suspect stalled_cycles={} "
      "threshold={} last_progress={} last_block={} external_wait={} "
      "q_core_req={} q_tag_resp={} q_mshr_mem={} q_mshr_notify={} "
      "q_mshr_replay={} q_wb_in={} q_wb_out={} q_wb_resp={} q_bypass={} "
      "q_mem_resp={} inflight={} hazards={} victim_reserved={} "
      "victim_committed={} victim_inflight={}",
      name(), deadlock_watchdog_.stalled_cycles,
      deadlock_watchdog_.threshold_cycles,
      deadlock_watchdog_.last_progress_reason,
      deadlock_watchdog_.last_block_reason,
      deadlock_watchdog_.last_external_wait, snapshot.core_req,
      snapshot.tag_array_resp, snapshot.mshr_file_mem_req,
      snapshot.mshr_file_refill_notify, snapshot.mshr_file_replay,
      snapshot.write_buffer_mem_req, snapshot.write_buffer_mem_req_out,
      snapshot.write_buffer_mem_resp, snapshot.bypass_req, snapshot.mem_resp,
      snapshot.mem_inflight_packets, snapshot.line_escape_hazards,
      snapshot.victim_buffer_reserved_entries,
      snapshot.victim_buffer_committed_entries,
      snapshot.victim_buffer_inflight_entries);
}

}  // namespace simtix::cache
