// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include <cassert>
#include <cstdint>
#include <limits>

#include "cache.h"

namespace {

constexpr uint64_t kMmioStartOffset = 0x0;
constexpr uint64_t kMmioAddrOffset = 0x8;
constexpr uint64_t kMmioSizeOffset = 0x10;
constexpr uint64_t kMmioOpOffset = 0x18;
constexpr unsigned int kMmioRegisterBytes = sizeof(uint64_t);

/**
 * @brief Decode a little-endian 64-bit MMIO register value.
 *
 * @param data Source byte storage with at least kMmioRegisterBytes bytes.
 * @return Decoded register value.
 */
uint64_t LoadLe64(const unsigned char *data) {
  uint64_t value = 0;
  for (unsigned int i = 0; i < kMmioRegisterBytes; ++i) {
    value |= static_cast<uint64_t>(data[i]) << (i * 8);
  }
  return value;
}

/**
 * @brief Encode a little-endian 64-bit MMIO register value.
 *
 * @param data Destination byte storage with at least kMmioRegisterBytes bytes.
 * @param value Register value to encode.
 */
void StoreLe64(unsigned char *data, uint64_t value) {
  for (unsigned int i = 0; i < kMmioRegisterBytes; ++i) {
    data[i] = static_cast<unsigned char>((value >> (i * 8)) & 0xFF);
  }
}

}  // namespace

namespace simtix::cache {

/**
 * @brief Accept one pending MMIO request and enqueue its response.
 */
void Cache::AcceptMmioRequest() {
  if (mmio_sink_.req_port->num_available() <= 0) {
    return;
  }
  if (!mmio_resp_queue_.nb_can_put()) {
    MarkBlockReason("mmio_resp_queue_full", false);
    return;
  }

  tlm::tlm_generic_payload *trans = nullptr;
  const bool success = mmio_sink_.req_port->nb_read(trans);
  assert(success);
  assert(trans != nullptr);

  HandleMmioRequest(trans);
  const bool put_response = mmio_resp_queue_.nb_put(trans);
  assert(put_response);
  MarkProgress("mmio_accept");
}

/**
 * @brief Send one completed MMIO response back to the MMIO initiator.
 */
void Cache::SendMmioResponse() {
  if (!mmio_resp_queue_.nb_can_get()) {
    return;
  }
  if (mmio_sink_.resp_port->num_free() <= 0) {
    MarkBlockReason("mmio_resp_backpressure", true);
    return;
  }

  tlm::tlm_generic_payload *trans = nullptr;
  const bool got_response = mmio_resp_queue_.nb_get(trans);
  assert(got_response);
  assert(trans != nullptr);

  const bool sent_response = mmio_sink_.resp_port->nb_write(trans);
  assert(sent_response);
  MarkProgress("mmio_response_sent");
}

/**
 * @brief Read a cache MMIO control register.
 *
 * @param offset MMIO register byte offset.
 * @return Current register value, or zero for unknown offsets.
 */
uint64_t Cache::ReadMmioRegister(uint64_t offset) const {
  switch (offset) {
    case kMmioStartOffset:
      return (mmio_sequencer_.IsBusy() || mmio_start_ != 0) ? 1 : 0;
    case kMmioAddrOffset:
      return mmio_addr_;
    case kMmioSizeOffset:
      return mmio_size_;
    case kMmioOpOffset:
      return static_cast<uint64_t>(mmio_op_);
    default:
      return 0;
  }
}

/**
 * @brief Write a cache MMIO control register.
 *
 * @param offset MMIO register byte offset.
 * @param value Register value to write.
 */
void Cache::WriteMmioRegister(uint64_t offset, uint64_t value) {
  switch (offset) {
    case kMmioStartOffset:
      if (value != 0) {
        StartMmioSequencer();
      } else if (!mmio_sequencer_.IsBusy()) {
        mmio_start_ = 0;
      }
      break;
    case kMmioAddrOffset:
      mmio_addr_ = value;
      break;
    case kMmioSizeOffset:
      mmio_size_ = value;
      break;
    case kMmioOpOffset:
      switch (value) {
        case static_cast<uint64_t>(MmioOperation::kFlush):
          mmio_op_ = MmioOperation::kFlush;
          break;
        case static_cast<uint64_t>(MmioOperation::kInvalidate):
          mmio_op_ = MmioOperation::kInvalidate;
          break;
        default:
          mmio_op_ = MmioOperation::kNop;
          break;
      }
      break;
    default:
      break;
  }
}

/**
 * @brief Validate and handle a single MMIO read or write payload.
 *
 * @param payload MMIO-side TLM payload to complete.
 */
void Cache::HandleMmioRequest(tlm::tlm_generic_payload *payload) {
  assert(payload != nullptr);

  const uint64_t offset = payload->get_address();
  const bool known_register =
      offset == kMmioStartOffset || offset == kMmioAddrOffset ||
      offset == kMmioSizeOffset || offset == kMmioOpOffset;
  if (!known_register) {
    payload->set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
    return;
  }
  if (payload->get_data_ptr() == nullptr ||
      payload->get_data_length() != kMmioRegisterBytes) {
    payload->set_response_status(tlm::TLM_BURST_ERROR_RESPONSE);
    return;
  }
  /* A CPU MMIO load/store can arrive through an interconnect with a
   * streaming width smaller than the register width (the interconnect may
   * reuse a byte-lane view for a polling load).  The cache MMIO aperture is a
   * scalar 64-bit register interface, so data_length is the authoritative
   * shape check; do not reject an otherwise complete 8-byte transaction based
   * on the transport's streaming hint. */

  // MMIO registers are 64-bit and require all byte lanes to be enabled.
  const unsigned char *byte_enable = payload->get_byte_enable_ptr();
  const unsigned int byte_enable_length = payload->get_byte_enable_length();
  if (byte_enable != nullptr && byte_enable_length == 0) {
    payload->set_response_status(tlm::TLM_BYTE_ENABLE_ERROR_RESPONSE);
    return;
  }
  for (unsigned int i = 0; byte_enable != nullptr && i < kMmioRegisterBytes;
       ++i) {
    if (byte_enable[i % byte_enable_length] != TLM_BYTE_ENABLED) {
      payload->set_response_status(tlm::TLM_BYTE_ENABLE_ERROR_RESPONSE);
      return;
    }
  }

  if (payload->is_read()) {
    StoreLe64(payload->get_data_ptr(), ReadMmioRegister(offset));
    payload->set_response_status(tlm::TLM_OK_RESPONSE);
    return;
  }
  if (payload->is_write()) {
    WriteMmioRegister(offset, LoadLe64(payload->get_data_ptr()));
    payload->set_response_status(tlm::TLM_OK_RESPONSE);
    return;
  }

  payload->set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
}

/**
 * @brief Start a flush or invalidate sequencer operation from MMIO registers.
 */
void Cache::StartMmioSequencer() {
  if (mmio_sequencer_.IsBusy()) {
    mmio_start_ = 1;
    MarkBlockReason("mmio_busy", false);
    LogQueueSnapshot(cache_log::Category::kMmio, "start_while_busy",
                     "mmio_busy");
    return;
  }

  if (mmio_size_ != 0) {
    const uint64_t max_address = std::numeric_limits<uint64_t>::max();
    if (mmio_size_ > max_address - mmio_addr_) {
      lv::Fatal(
          "Invalid cache MMIO range: address {:#x} plus size {:#x} "
          "overflows the 64-bit address space\n",
          mmio_addr_, mmio_size_);
    }

    const uint64_t end = mmio_addr_ + mmio_size_;
    const uint64_t round_up =
        (config_.block_size_bytes - (end % config_.block_size_bytes)) %
        config_.block_size_bytes;
    if (round_up > max_address - end) {
      lv::Fatal(
          "Invalid cache MMIO range: block-aligned end for address {:#x} "
          "and size {:#x} overflows the 64-bit address space\n",
          mmio_addr_, mmio_size_);
    }
  }

  mmio_start_ = 1;

  mmio_sequencer_.Start(mmio_op_, mmio_addr_, mmio_size_,
                        config_.block_size_bytes, tag_array_.EntryCount());
  mmio_pipeline_wait_log_count_ = 0;
  mmio_writeback_wait_log_count_ = 0;
  MarkProgress("mmio_start");
  LogQueueSnapshot(cache_log::Category::kMmio, "start",
                   MmioOperationName(mmio_op_));
}

/**
 * @brief Check whether ordinary cache pipeline work must drain before MMIO.
 *
 * @return true while existing cache work can still affect MMIO scan safety.
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
 * @brief Check whether MMIO-issued writeback work still needs to drain.
 *
 * @return true while flush writeback traffic or hazards remain outstanding.
 */
bool Cache::HasPendingMmioWritebackWork() const {
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
 * @param line Dirty tag-array line selected by the MMIO flush scan.
 * @return true when the writeback packet was queued.
 */
bool Cache::TryIssueMmioFlushWriteback(const TagArray::DirtyLine &line) {
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
  MarkProgress("mmio_flush_writeback_issue");
  return true;
}

/**
 * @brief Advance the active MMIO scan by at most one cache line or entry.
 *
 * @return true when the scan range is complete.
 */
bool Cache::TryAdvanceMmioScan() {
  assert(mmio_sequencer_.phase == MmioSequencer::Phase::kScan);

  // A zero-size operation scans every tag entry in index order.
  if (mmio_sequencer_.full_cache) {
    if (mmio_sequencer_.scan_index >= mmio_sequencer_.entry_count) {
      return true;
    }

    if (mmio_sequencer_.operation == MmioOperation::kFlush) {
      if (!write_buffer_mem_req_queue_.nb_can_put()) {
        MarkBlockReason("write_buffer_mem_req_full", false);
        return false;
      }
      auto dirty_line = tag_array_.ProbeDirtyEntry(mmio_sequencer_.scan_index);
      if (dirty_line && !TryIssueMmioFlushWriteback(*dirty_line)) {
        return false;
      }
    } else {
      assert(mmio_sequencer_.operation == MmioOperation::kInvalidate);
      tag_array_.InvalidateEntry(mmio_sequencer_.scan_index);
    }

    ++mmio_sequencer_.scan_index;
    MarkProgress("mmio_scan_step");
    return false;
  }

  // Nonzero-size operations scan the requested block-aligned address range.
  if (mmio_sequencer_.scan_address >= mmio_sequencer_.end_address) {
    return true;
  }

  const uint64_t address = mmio_sequencer_.scan_address;
  if (mmio_sequencer_.operation == MmioOperation::kFlush) {
    if (!write_buffer_mem_req_queue_.nb_can_put()) {
      MarkBlockReason("write_buffer_mem_req_full", false);
      return false;
    }
    auto dirty_line = tag_array_.ProbeDirtyLine(address);
    if (dirty_line && !TryIssueMmioFlushWriteback(*dirty_line)) {
      return false;
    }
  } else {
    assert(mmio_sequencer_.operation == MmioOperation::kInvalidate);
    tag_array_.InvalidateLine(address);
  }

  mmio_sequencer_.scan_address += config_.block_size_bytes;
  MarkProgress("mmio_scan_step");
  return false;
}

/**
 * @brief Advance the MMIO flush/invalidate sequencer by one cache tick.
 */
void Cache::AdvanceMmioSequencer() {
  switch (mmio_sequencer_.phase) {
    case MmioSequencer::Phase::kIdle:
      return;
    case MmioSequencer::Phase::kWaitPipelineDrain:
      if (HasPendingPipelineWork()) {
        LogRepeatedStall("mmio_wait_pipeline_drain",
                         &mmio_pipeline_wait_log_count_);
        return;
      }
      mmio_sequencer_.phase = MmioSequencer::Phase::kScan;
      mmio_pipeline_wait_log_count_ = 0;
      MarkProgress("mmio_phase_scan");
      LogQueueSnapshot(cache_log::Category::kMmio, "phase_transition",
                       "pipeline_drained");
      return;
    case MmioSequencer::Phase::kScan:
      if (TryAdvanceMmioScan()) {
        mmio_sequencer_.phase = MmioSequencer::Phase::kWaitWritebackDrain;
        mmio_writeback_wait_log_count_ = 0;
        MarkProgress("mmio_phase_wait_writeback");
        LogQueueSnapshot(cache_log::Category::kMmio, "phase_transition",
                         "scan_complete");
      }
      return;
    case MmioSequencer::Phase::kWaitWritebackDrain:
      if (!HasPendingMmioWritebackWork()) {
        mmio_sequencer_.phase = MmioSequencer::Phase::kComplete;
        mmio_writeback_wait_log_count_ = 0;
        MarkProgress("mmio_phase_complete");
        LogQueueSnapshot(cache_log::Category::kMmio, "phase_transition",
                         "writeback_drained");
      } else {
        LogRepeatedStall("mmio_wait_writeback_drain",
                         &mmio_writeback_wait_log_count_);
      }
      return;
    case MmioSequencer::Phase::kComplete:
      LogQueueSnapshot(cache_log::Category::kMmio, "complete", "mmio_done");
      mmio_start_ = 0;
      mmio_sequencer_.Reset();
      MarkProgress("mmio_complete");
      return;
  }
}

}  // namespace simtix::cache
