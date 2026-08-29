// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include "cache.h"

namespace simtix::cache {

const char *Cache::MmioOperationName(MmioOperation operation) {
  switch (operation) {
    case MmioOperation::kNop:
      return "nop";
    case MmioOperation::kFlush:
      return "flush";
    case MmioOperation::kInvalidate:
      return "invalidate";
  }
  return "unknown";
}

const char *Cache::MmioPhaseName(MmioSequencer::Phase phase) {
  switch (phase) {
    case MmioSequencer::Phase::kIdle:
      return "idle";
    case MmioSequencer::Phase::kWaitPipelineDrain:
      return "wait_pipeline_drain";
    case MmioSequencer::Phase::kScan:
      return "scan";
    case MmioSequencer::Phase::kWaitWritebackDrain:
      return "wait_writeback_drain";
    case MmioSequencer::Phase::kComplete:
      return "complete";
  }
  return "unknown";
}

void Cache::LogPacketEvent(cache_log::Category category, const char *event,
                           const char *reason, const char *source,
                           const Packet *packet) const {
  const char *safe_reason = reason != nullptr ? reason : "none";
  const char *safe_source = source != nullptr ? source : "none";
  if (packet == nullptr) {
    SIMTIX_CACHE_LOG_DEBUG(name(), category,
                           "event={} reason={} source={} pkt=none", event,
                           safe_reason, safe_source);
    return;
  }

  SIMTIX_CACHE_LOG_DEBUG(
      name(), category,
      "event={} reason={} source={} pkt={} parent={} type={} cmd={} "
      "addr={:#x} line={:#x}",
      event, safe_reason, safe_source, packet->unique_id,
      cache_log::PacketParentId(packet),
      cache_log::PacketTypeName(packet->type),
      cache_log::PacketCommandName(packet), cache_log::PacketAddress(packet),
      cache_log::PacketLineAddress(packet, config_.block_size_bytes));
}

void Cache::LogPacketTraceEvent(cache_log::Category category, const char *event,
                                const char *reason, const char *source,
                                const Packet *packet) const {
  const char *safe_reason = reason != nullptr ? reason : "none";
  const char *safe_source = source != nullptr ? source : "none";
  if (packet == nullptr) {
    SIMTIX_CACHE_LOG_TRACE(name(), category,
                           "event={} reason={} source={} pkt=none", event,
                           safe_reason, safe_source);
    return;
  }

  SIMTIX_CACHE_LOG_TRACE(
      name(), category,
      "event={} reason={} source={} pkt={} parent={} type={} cmd={} "
      "addr={:#x} line={:#x}",
      event, safe_reason, safe_source, packet->unique_id,
      cache_log::PacketParentId(packet),
      cache_log::PacketTypeName(packet->type),
      cache_log::PacketCommandName(packet), cache_log::PacketAddress(packet),
      cache_log::PacketLineAddress(packet, config_.block_size_bytes));
}

void Cache::LogQueueSnapshot(cache_log::Category category, const char *event,
                             const char *reason) const {
  SIMTIX_CACHE_LOG_DEBUG(
      name(), category,
      "event={} reason={} phase={} start={} op={} addr={:#x} size={:#x} "
      "scan_addr={:#x} scan_index={} q_core_req={} q_core_resp={} "
      "q_mmio_resp={} q_tag_resp={} q_mshr_mem={} q_mshr_notify={} "
      "q_mshr_replay={} q_wb_in={} q_wb_out={} q_wb_resp={} q_bypass={} "
      "q_mem_resp={} inflight={} hazards={} mshr_pending={} wb_pending={} "
      "victim_reserved={} victim_committed={} victim_inflight={} "
      "atomic_busy={}",
      event, reason, MmioPhaseName(mmio_sequencer_.phase), mmio_start_,
      MmioOperationName(mmio_op_), mmio_addr_, mmio_size_,
      mmio_sequencer_.scan_address, mmio_sequencer_.scan_index,
      core_req_queue_.used(), core_resp_queue_.used(), mmio_resp_queue_.used(),
      tag_array_resp_queue_.used(), mshr_file_mem_req_queue_.used(),
      mshr_file_refill_notify_queue_.used(), mshr_file_replay_queue_.used(),
      write_buffer_mem_req_queue_.used(),
      write_buffer_mem_req_out_queue_.used(),
      write_buffer_mem_resp_queue_.used(), bypass_req_queue_.used(),
      mem_resp_queue_.used(), mem_inflight_packets_.size(),
      line_escape_hazards_.size(), mshr_file_.HasPendingWork(),
      write_buffer_.HasPendingWork(), victim_buffer_.ReservedEntryCount(),
      victim_buffer_.CommittedEntryCount(), victim_buffer_.InflightEntryCount(),
      atomic_sequencer_.IsBusy());
}

}  // namespace simtix::cache
