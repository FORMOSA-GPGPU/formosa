// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include <cassert>
#include <cstdint>
#include <limits>

#include "cache.h"

namespace simtix::cache {

/**
 * @brief Accept one decoded cache command.
 */
void Cache::AcceptCommand() {
  if (cmd_sequencer_.IsBusy() ||
      !cmd_channel_.get_request_export->nb_can_get()) {
    return;
  }

  ilha::CacheCommand command;
  const bool got_command = cmd_channel_.get_request_export->nb_get(command);
  assert(got_command);

  cmd_addr_ = command.addr;
  cmd_size_ = command.size;
  switch (command.opcode) {
    case static_cast<uint64_t>(CommandOpcode::kFlush):
      cmd_opcode_ = CommandOpcode::kFlush;
      break;
    case static_cast<uint64_t>(CommandOpcode::kInvalidate):
      cmd_opcode_ = CommandOpcode::kInvalidate;
      break;
    default:
      cmd_opcode_ = CommandOpcode::kNop;
      break;
  }

  StartCommandSequencer();
  MarkProgress("command_accept");
}

/**
 * @brief Start a flush or invalidate sequencer command.
 */
void Cache::StartCommandSequencer() {
  if (cmd_sequencer_.IsBusy()) {
    cmd_active_ = 1;
    MarkBlockReason("command_busy", false);
    LogQueueSnapshot(cache_log::Category::kCommand, "start_while_busy",
                     "command_busy");
    return;
  }

  if (cmd_size_ != 0) {
    const uint64_t max_address = std::numeric_limits<uint64_t>::max();
    if (cmd_size_ > max_address - cmd_addr_) {
      lv::Fatal(
          "Invalid cache command range: address {:#x} plus size {:#x} "
          "overflows the 64-bit address space\n",
          cmd_addr_, cmd_size_);
    }

    const uint64_t end = cmd_addr_ + cmd_size_;
    const uint64_t round_up =
        (config_.block_size_bytes - (end % config_.block_size_bytes)) %
        config_.block_size_bytes;
    if (round_up > max_address - end) {
      lv::Fatal(
          "Invalid cache command range: block-aligned end for address {:#x} "
          "and size {:#x} overflows the 64-bit address space\n",
          cmd_addr_, cmd_size_);
    }
  }

  cmd_active_ = 1;

  cmd_sequencer_.Start(cmd_opcode_, cmd_addr_, cmd_size_,
                       config_.block_size_bytes, tag_array_.EntryCount());
  cmd_pipeline_wait_log_count_ = 0;
  cmd_writeback_wait_log_count_ = 0;
  MarkProgress("command_start");
  LogQueueSnapshot(cache_log::Category::kCommand, "start",
                   CommandOpcodeName(cmd_opcode_));
}

/**
 * @brief Check whether ordinary cache work must drain before command execution.
 *
 * @return true while existing cache work can still affect command scan safety.
 */
bool Cache::HasPendingPipelineWork() const {
  return core_req_queue_.used() > 0 || tag_array_resp_queue_.used() > 0 ||
         mshr_file_mem_req_queue_.used() > 0 ||
         mshr_file_refill_notify_queue_.used() > 0 ||
         mshr_file_replay_queue_.used() > 0 || mshr_file_.HasPendingWork() ||
         write_buffer_mem_req_queue_.used() > 0 ||
         write_buffer_mem_req_out_queue_.used() > 0 ||
         write_buffer_mem_resp_queue_.used() > 0 ||
         write_buffer_.HasPendingWork() ||
         victim_buffer_mem_req_out_queue_.used() > 0 ||
         victim_buffer_mem_resp_queue_.used() > 0 ||
         victim_buffer_.HasPendingWork() || bypass_req_queue_.used() > 0 ||
         mem_resp_queue_.used() > 0 || !mem_inflight_packets_.empty() ||
         atomic_sequencer_.IsBusy() || !line_escape_hazards_.empty();
}

/**
 * @brief Check whether command-issued writeback work still needs to drain.
 *
 * @return true while flush writeback traffic or hazards remain outstanding.
 */
bool Cache::HasPendingCommandWritebackWork() const {
  return write_buffer_mem_req_queue_.used() > 0 ||
         write_buffer_mem_req_out_queue_.used() > 0 ||
         write_buffer_mem_resp_queue_.used() > 0 ||
         write_buffer_.HasPendingWork() ||
         victim_buffer_mem_req_out_queue_.used() > 0 ||
         victim_buffer_mem_resp_queue_.used() > 0 ||
         victim_buffer_.HasPendingWork() || mem_resp_queue_.used() > 0 ||
         !mem_inflight_packets_.empty() || !line_escape_hazards_.empty();
}

/**
 * @brief Issue one dirty cache line writeback for a flush scan step.
 *
 * @param line Dirty tag-array line selected by the command flush scan.
 * @return true when the writeback packet was queued.
 */
bool Cache::TryIssueCommandFlushWriteback(const TagArray::DirtyLine &line) {
  if (!write_buffer_mem_req_queue_.nb_can_put()) {
    return false;
  }

  Packet *packet = AllocatePacketWithOwnedPayload();
  assert(packet != nullptr);
  MemPayload *payload = packet->GetCacheOwnedPayload();
  payload->InitLineWrite(line.address, config_.block_size_bytes);
  const bool read_ok = data_array_.ReadBlock(
      line.location, payload->buffer.data(), payload->buffer.size());
  assert(read_ok);
  packet->type = PacketType::kMemWriteReq;
  packet->is_atomic = false;

  IncrementLineEscapeHazard(packet);
  const bool put_writeback = write_buffer_mem_req_queue_.nb_put(packet);
  assert(put_writeback);
  MarkProgress("command_flush_writeback_issue");
  return true;
}

/**
 * @brief Advance the active command scan by at most one cache line or entry.
 *
 * @return true when the scan range is complete.
 */
bool Cache::TryAdvanceCommandScan() {
  assert(cmd_sequencer_.phase == CommandSequencer::Phase::kScan);

  // A zero-size command scans every tag entry in index order.
  if (cmd_sequencer_.full_cache) {
    if (cmd_sequencer_.scan_index >= cmd_sequencer_.entry_count) {
      return true;
    }

    if (cmd_sequencer_.opcode == CommandOpcode::kFlush) {
      if (!write_buffer_mem_req_queue_.nb_can_put()) {
        MarkBlockReason("write_buffer_mem_req_full", false);
        return false;
      }
      auto dirty_line = tag_array_.ProbeDirtyEntry(cmd_sequencer_.scan_index);
      if (dirty_line && !TryIssueCommandFlushWriteback(*dirty_line)) {
        return false;
      }
    } else {
      assert(cmd_sequencer_.opcode == CommandOpcode::kInvalidate);
      tag_array_.InvalidateEntry(cmd_sequencer_.scan_index);
    }

    ++cmd_sequencer_.scan_index;
    MarkProgress("command_scan_step");
    return false;
  }

  // Nonzero-size commands scan the requested block-aligned address range.
  if (cmd_sequencer_.scan_address >= cmd_sequencer_.end_address) {
    return true;
  }

  const uint64_t address = cmd_sequencer_.scan_address;
  if (cmd_sequencer_.opcode == CommandOpcode::kFlush) {
    if (!write_buffer_mem_req_queue_.nb_can_put()) {
      MarkBlockReason("write_buffer_mem_req_full", false);
      return false;
    }
    auto dirty_line = tag_array_.ProbeDirtyLine(address);
    if (dirty_line && !TryIssueCommandFlushWriteback(*dirty_line)) {
      return false;
    }
  } else {
    assert(cmd_sequencer_.opcode == CommandOpcode::kInvalidate);
    tag_array_.InvalidateLine(address);
  }

  cmd_sequencer_.scan_address += config_.block_size_bytes;
  MarkProgress("command_scan_step");
  return false;
}

/**
 * @brief Advance the command flush/invalidate sequencer by one cache tick.
 */
void Cache::AdvanceCommandSequencer() {
  switch (cmd_sequencer_.phase) {
    case CommandSequencer::Phase::kIdle:
      return;
    case CommandSequencer::Phase::kWaitPipelineDrain:
      if (HasPendingPipelineWork()) {
        LogRepeatedStall("command_wait_pipeline_drain",
                         &cmd_pipeline_wait_log_count_);
        return;
      }
      cmd_sequencer_.phase = CommandSequencer::Phase::kScan;
      cmd_pipeline_wait_log_count_ = 0;
      MarkProgress("command_phase_scan");
      LogQueueSnapshot(cache_log::Category::kCommand, "phase_transition",
                       "pipeline_drained");
      return;
    case CommandSequencer::Phase::kScan:
      if (TryAdvanceCommandScan()) {
        cmd_sequencer_.phase = CommandSequencer::Phase::kWaitWritebackDrain;
        cmd_writeback_wait_log_count_ = 0;
        MarkProgress("command_phase_wait_writeback");
        LogQueueSnapshot(cache_log::Category::kCommand, "phase_transition",
                         "scan_complete");
      }
      return;
    case CommandSequencer::Phase::kWaitWritebackDrain:
      if (!HasPendingCommandWritebackWork()) {
        cmd_sequencer_.phase = CommandSequencer::Phase::kComplete;
        cmd_writeback_wait_log_count_ = 0;
        MarkProgress("command_phase_complete");
        LogQueueSnapshot(cache_log::Category::kCommand, "phase_transition",
                         "writeback_drained");
      } else {
        LogRepeatedStall("command_wait_writeback_drain",
                         &cmd_writeback_wait_log_count_);
      }
      return;
    case CommandSequencer::Phase::kComplete:
      if (!cmd_channel_.put_response_export->nb_can_put()) {
        MarkBlockReason("command_response_full", false);
        return;
      }
      LogQueueSnapshot(cache_log::Category::kCommand, "complete",
                       "command_done");
      const bool put_response = cmd_channel_.put_response_export->nb_put(true);
      assert(put_response);
      cmd_active_ = 0;
      cmd_sequencer_.Reset();
      MarkProgress("command_complete");
      return;
  }
}

}  // namespace simtix::cache
