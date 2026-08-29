// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include "cache.h"

#include <liblv/binding.h>
#include <liblv/output.h>

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>

#include "tlm_extensions/atomic_extension.h"

namespace {

/**
 * @brief Copy a 32-bit scalar out of byte storage.
 *
 * @param data Source storage with at least four readable bytes.
 * @return Value copied from `data`.
 */
uint32_t LoadU32(const uint8_t *data) {
  uint32_t value = 0;
  std::memcpy(&value, data, sizeof(value));
  return value;
}

/**
 * @brief Copy a 64-bit scalar out of byte storage.
 *
 * @param data Source storage with at least eight readable bytes.
 * @return Value copied from `data`.
 */
uint64_t LoadU64(const uint8_t *data) {
  uint64_t value = 0;
  std::memcpy(&value, data, sizeof(value));
  return value;
}

/**
 * @brief Copy a 32-bit scalar into byte storage.
 *
 * @param data Destination storage with at least four writable bytes.
 * @param value Value to store.
 */
void StoreU32(uint8_t *data, uint32_t value) {
  std::memcpy(data, &value, sizeof(value));
}

/**
 * @brief Copy a 64-bit scalar into byte storage.
 *
 * @param data Destination storage with at least eight writable bytes.
 * @param value Value to store.
 */
void StoreU64(uint8_t *data, uint64_t value) {
  std::memcpy(data, &value, sizeof(value));
}

constexpr uint64_t kDefaultWatchdogCycles = 1000000;

bool EnvTruthy(const char *name) {
  const char *value = std::getenv(name);
  if (value == nullptr || value[0] == '\0') {
    return false;
  }
  std::string_view view(value);
  return view != "0" && view != "false" && view != "FALSE" && view != "off" &&
         view != "OFF";
}

uint64_t ParseWatchdogCycles() {
  const char *value = std::getenv("SIMTIX_CACHE_WATCHDOG_CYCLES");
  if (value == nullptr || value[0] == '\0') {
    return kDefaultWatchdogCycles;
  }

  char *end = nullptr;
  const unsigned long long parsed = std::strtoull(value, &end, 10);
  if (end == value || parsed == 0) {
    return kDefaultWatchdogCycles;
  }
  return static_cast<uint64_t>(parsed);
}

bool WatchdogFilterMatches(const char *module_name) {
  const char *filter = std::getenv("SIMTIX_CACHE_WATCHDOG_FILTER");
  if (filter == nullptr || filter[0] == '\0') {
    return true;
  }
  return module_name != nullptr &&
         std::string_view(module_name).find(filter) != std::string_view::npos;
}

/**
 * @brief Compute the result of a 32-bit atomic memory operation.
 *
 * @param op Atomic operation selected by the request extension.
 * @param old_value Value read from the cache line.
 * @param operand Operand carried by the atomic request.
 * @return New value to write back to the cache line.
 */
uint32_t ApplyAtomicOp32(simtix::AtomicExtension::Op op, uint32_t old_value,
                         uint32_t operand) {
  switch (op) {
    case simtix::AtomicExtension::Op::kSwap:
      return operand;
    case simtix::AtomicExtension::Op::kAdd:
      return old_value + operand;
    case simtix::AtomicExtension::Op::kXor:
      return old_value ^ operand;
    case simtix::AtomicExtension::Op::kAnd:
      return old_value & operand;
    case simtix::AtomicExtension::Op::kOr:
      return old_value | operand;
    case simtix::AtomicExtension::Op::kMin:
      return static_cast<uint32_t>(std::min(static_cast<int32_t>(old_value),
                                            static_cast<int32_t>(operand)));
    case simtix::AtomicExtension::Op::kMax:
      return static_cast<uint32_t>(std::max(static_cast<int32_t>(old_value),
                                            static_cast<int32_t>(operand)));
    case simtix::AtomicExtension::Op::kMinU:
      return std::min(old_value, operand);
    case simtix::AtomicExtension::Op::kMaxU:
      return std::max(old_value, operand);
  }
  return old_value;
}

/**
 * @brief Compute the result of a 64-bit atomic memory operation.
 *
 * @param op Atomic operation selected by the request extension.
 * @param old_value Value read from the cache line.
 * @param operand Operand carried by the atomic request.
 * @return New value to write back to the cache line.
 */
uint64_t ApplyAtomicOp64(simtix::AtomicExtension::Op op, uint64_t old_value,
                         uint64_t operand) {
  switch (op) {
    case simtix::AtomicExtension::Op::kSwap:
      return operand;
    case simtix::AtomicExtension::Op::kAdd:
      return old_value + operand;
    case simtix::AtomicExtension::Op::kXor:
      return old_value ^ operand;
    case simtix::AtomicExtension::Op::kAnd:
      return old_value & operand;
    case simtix::AtomicExtension::Op::kOr:
      return old_value | operand;
    case simtix::AtomicExtension::Op::kMin:
      return static_cast<uint64_t>(std::min(static_cast<int64_t>(old_value),
                                            static_cast<int64_t>(operand)));
    case simtix::AtomicExtension::Op::kMax:
      return static_cast<uint64_t>(std::max(static_cast<int64_t>(old_value),
                                            static_cast<int64_t>(operand)));
    case simtix::AtomicExtension::Op::kMinU:
      return std::min(old_value, operand);
    case simtix::AtomicExtension::Op::kMaxU:
      return std::max(old_value, operand);
  }
  return old_value;
}

}  // namespace

namespace simtix::cache {

Cache::Cache(const sc_module_name &name, const Param &p)
    : sc_module(name),
      config_(p),
      non_cacheable_regions_(p.non_cacheable_regions),
      sink_("sink"),
      mmio_sink_("mmio_sink"),
      source_("source"),
      stats_(name),
      packet_pool_(),
      mem_payload_pool_(p.block_size_bytes),
      tag_array_(p),
      data_array_(p),
      mshr_file_("mshr_file", p, *this),
      write_buffer_("write_buffer", p, *this),
      victim_buffer_("victim_buffer", p, *this),
      core_req_queue_("core_req_queue", config_.pipeline_queue_size),
      core_resp_queue_("core_resp_queue", config_.pipeline_queue_size),
      mmio_resp_queue_("mmio_resp_queue", config_.pipeline_queue_size),
      tag_array_resp_queue_("tag_array_resp_queue",
                            config_.pipeline_queue_size),
      mshr_file_mem_req_queue_("mshr_file_mem_req_queue",
                               config_.pipeline_queue_size),
      mshr_file_refill_notify_queue_("mshr_file_refill_notify_queue",
                                     config_.pipeline_queue_size),
      mshr_file_replay_queue_("mshr_file_replay_queue",
                              config_.pipeline_queue_size),
      write_buffer_mem_req_queue_("write_buffer_mem_req_queue",
                                  config_.pipeline_queue_size),
      write_buffer_mem_req_out_queue_("write_buffer_mem_req_out_queue",
                                      config_.pipeline_queue_size),
      write_buffer_mem_resp_queue_("write_buffer_mem_resp_queue",
                                   config_.pipeline_queue_size),
      victim_buffer_mem_req_out_queue_("victim_buffer_mem_req_out_queue",
                                       config_.pipeline_queue_size),
      victim_buffer_mem_resp_queue_("victim_buffer_mem_resp_queue",
                                    config_.pipeline_queue_size),
      bypass_req_queue_("bypass_req_queue", config_.pipeline_queue_size),
      mem_resp_queue_("mem_resp_queue", config_.pipeline_queue_size) {
  if (config_.atomic_linearization &&
      (config_.write_hit_policy != WriteHitPolicy::kWriteBack ||
       config_.write_miss_policy != WriteMissPolicy::kWriteAllocate)) {
    lv::Fatal(
        "Cache atomic_linearization currently requires WriteBack and "
        "WriteAllocate policies\n");
  }

  // Bind submodules to the cache-owned FIFOs before the first clock edge.
  mshr_file_.clock.bind(clock);
  mshr_file_.mshr_mem_req.bind(mshr_file_mem_req_queue_);
  mshr_file_.mshr_refill_notify.bind(mshr_file_refill_notify_queue_);
  mshr_file_.mshr_replay.bind(mshr_file_replay_queue_);
  write_buffer_.clock.bind(clock);
  write_buffer_.write_buffer_in.bind(write_buffer_mem_req_queue_);
  write_buffer_.mem_req_out.bind(write_buffer_mem_req_out_queue_);
  write_buffer_.mem_resp_in.bind(write_buffer_mem_resp_queue_);
  victim_buffer_.clock.bind(clock);
  victim_buffer_.mem_req_out.bind(victim_buffer_mem_req_out_queue_);
  victim_buffer_.mem_resp_in.bind(victim_buffer_mem_resp_queue_);

  SC_METHOD(Tick);
  sensitive << clock.pos();
  dont_initialize();

  deadlock_watchdog_.enabled =
      EnvTruthy("SIMTIX_CACHE_WATCHDOG") && WatchdogFilterMatches(this->name());
  deadlock_watchdog_.fatal = EnvTruthy("SIMTIX_CACHE_WATCHDOG_FATAL");
  deadlock_watchdog_.threshold_cycles = ParseWatchdogCycles();
  deadlock_watchdog_.last_snapshot = MakeWatchdogSnapshot();
}

void Cache::Tick() {
  BeginDeadlockWatchdogTick();
  // Output stage
  SendMmioResponse();
  SendCoreResponse();
  SendMemRequest();
  // Data Array stage
  AccessDataArrayStage();
  // Tag Array stage
  AccessTagArrayStage();
  AdvanceMmioSequencer();
  // Accept input
  AcceptMmioRequest();
  AcceptCoreRequest();
  AcceptMemResponse();
  CheckDeadlockWatchdog();
}

/**
 * @brief Check whether a core payload targets a non-cacheable region.
 *
 * @param payload Core-side TLM payload to inspect.
 * @return true when the payload address falls inside a configured
 * non-cacheable region.
 */
bool Cache::IsNonCacheableRequest(
    const tlm::tlm_generic_payload &payload) const {
  const uint64_t address = payload.get_address();
  for (const auto &region : non_cacheable_regions_) {
    if (region.size == 0) {
      continue;
    }
    if (address >= region.addr && address - region.addr < region.size) {
      return true;
    }
  }
  return false;
}

/**
 * @brief Check whether a payload carries an atomic-operation extension.
 *
 * @param payload Core-side TLM payload to inspect.
 * @return true when an AtomicExtension is attached.
 */
bool Cache::IsAtomicRequest(const tlm::tlm_generic_payload &payload) const {
  simtix::AtomicExtension *ext = nullptr;
  payload.get_extension(ext);
  return ext != nullptr;
}

/**
 * @brief Validate the payload shape supported by the atomic sequencer.
 *
 * @param payload Atomic core-side TLM payload to validate.
 * @return true for supported 32-bit or 64-bit atomic read-modify-write
 * requests.
 */
bool Cache::IsValidAtomicRequest(
    const tlm::tlm_generic_payload &payload) const {
  const unsigned int length = payload.get_data_length();
  return payload.is_read() && payload.get_data_ptr() != nullptr &&
         payload.get_byte_enable_ptr() == nullptr &&
         (length == sizeof(uint32_t) || length == sizeof(uint64_t));
}

void Cache::AcceptCoreRequest() {
  if (mmio_sequencer_.IsBusy()) {
    if (sink_.req_port->num_available() > 0) {
      MarkBlockReason("mmio_busy", false);
    }
    return;
  }
  if (sink_.req_port->num_available() <= 0) {
    // No request to accept, do nothing
    return;
  }
  if (!core_req_queue_.nb_can_put()) {
    MarkBlockReason("core_req_full", false);
    // No request to accept or core request queue is full, do nothing
    return;
  }

  // Accept a core request
  tlm::tlm_generic_payload *trans = nullptr;
  bool success = sink_.req_port->nb_read(trans);
  assert(success);
  assert(trans != nullptr);

  // Convert the tlm payload to cache packet
  auto packet = AllocatePacketWithCorePayload(trans);
  assert(packet != nullptr);
  packet->is_atomic = IsAtomicRequest(*trans);
  packet->type = (IsNonCacheableRequest(*trans) ||
                  (packet->is_atomic && !config_.atomic_linearization))
                     ? PacketType::kBypassCoreReq
                     : PacketType::kCoreReq;

  // Enqueue the packet to core request queue
  success = core_req_queue_.nb_put(packet);
  assert(success);
  MarkProgress("core_accept");
  LogPacketTraceEvent(cache_log::Category::kStage, "core_accept", "accepted",
                      "core_req_queue", packet);
}

/**
 * @brief Reject invalid responses for cache-owned memory transactions.
 *
 * Cache-owned reads and writes have no architectural error-return path. A
 * failed refill must not install its response buffer, and failed write-through
 * or victim writes must not silently lose data. Bypass requests retain explicit
 * TLM errors for the core, but an incomplete response is always a downstream
 * protocol violation.
 *
 * @param packet Inflight packet associated with the memory response.
 * @param transaction Completed downstream TLM transaction.
 */
void Cache::ValidateMemoryResponseOrFatal(
    const Packet *packet, const tlm::tlm_generic_payload *transaction) const {
  assert(packet != nullptr);
  assert(transaction != nullptr);
  assert(packet->GetTlmGp() == transaction);

  const tlm::tlm_response_status status = transaction->get_response_status();
  const bool is_bypass = packet->type == PacketType::kBypassCoreReq;
  if ((is_bypass && status != tlm::TLM_INCOMPLETE_RESPONSE) ||
      (!is_bypass && status == tlm::TLM_OK_RESPONSE)) {
    return;
  }

  const char *packet_type = cache_log::PacketTypeName(packet->type);
  const char *command = cache_log::PacketCommandName(packet);
  const std::string response = transaction->get_response_string();
  if (packet->mshr_id.has_value()) {
    lv::Fatal(
        "Cache '{}' received invalid memory response: packet={} type={} "
        "command={} address={:#x} size={} status={} ({}) mshr={}:{}\n",
        name(), packet->unique_id, packet_type, command,
        transaction->get_address(), transaction->get_data_length(),
        static_cast<int>(status), response, packet->mshr_id->index,
        packet->mshr_id->generation);
  }

  lv::Fatal(
      "Cache '{}' received invalid memory response: packet={} type={} "
      "command={} address={:#x} size={} status={} ({}) mshr=none\n",
      name(), packet->unique_id, packet_type, command,
      transaction->get_address(), transaction->get_data_length(),
      static_cast<int>(status), response);
}

void Cache::AcceptMemResponse() {
  if (source_.resp_port->num_available() <= 0) {
    if (!mem_inflight_packets_.empty()) {
      MarkBlockReason("memory_resp_wait", true);
    }
    // No response to accept, do nothing
    return;
  }

  // The single memory-response FIFO is handled strictly head-of-line. This
  // relies on downstream memory not reordering a read response ahead of an
  // earlier write response. If downstream memory gains such reordering, revisit
  // this routing because refill and write-buffer backpressure can form a cycle.
  tlm::tlm_generic_payload *peek_trans = nullptr;
  bool success = source_.peek_resp_port->nb_peek(peek_trans);
  assert(success);
  assert(peek_trans != nullptr);

  const auto peek_inflight = mem_inflight_packets_.find(peek_trans);
  assert(peek_inflight != mem_inflight_packets_.end());
  Packet *peek_packet = peek_inflight->second;
  assert(peek_packet != nullptr);
  ValidateMemoryResponseOrFatal(peek_packet, peek_trans);

  const bool write_buffer_response =
      peek_packet->type == PacketType::kMemWriteReq && peek_trans->is_write();
  const bool victim_buffer_response =
      peek_packet->type == PacketType::kVictimWriteReq &&
      peek_trans->is_write();
  if (victim_buffer_response) {
    if (!victim_buffer_mem_resp_queue_.nb_can_put()) {
      MarkBlockReason("victim_buffer_mem_resp_full", false);
      LogPacketEvent(cache_log::Category::kMem, "blocked",
                     "victim_buffer_mem_resp_full",
                     "victim_buffer_mem_resp_queue", peek_packet);
      return;
    }
  } else if (write_buffer_response) {
    if (!write_buffer_mem_resp_queue_.nb_can_put()) {
      MarkBlockReason("write_buffer_mem_resp_full", false);
      LogPacketEvent(cache_log::Category::kMem, "blocked",
                     "write_buffer_mem_resp_full",
                     "write_buffer_mem_resp_queue", peek_packet);
      return;
    }
  } else if (!mem_resp_queue_.nb_can_put()) {
    MarkBlockReason("mem_resp_full", false);
    LogPacketEvent(cache_log::Category::kMem, "blocked", "mem_resp_full",
                   "mem_resp_queue", peek_packet);
    return;
  }

  tlm::tlm_generic_payload *trans = nullptr;
  success = source_.resp_port->nb_read(trans);
  assert(success);
  assert(trans != nullptr);
  assert(trans == peek_trans);

  auto inflight = mem_inflight_packets_.find(trans);
  assert(inflight != mem_inflight_packets_.end());
  Packet *packet = inflight->second;
  assert(packet != nullptr);
  assert(packet->GetTlmGp() == trans);
  mem_inflight_packets_.erase(inflight);

  if (packet->type == PacketType::kBypassCoreReq) {
    packet->type = PacketType::kBypassCoreResp;
    LogPacketEvent(cache_log::Category::kMem, "response_route", "accepted",
                   "mem_resp_queue", packet);
  } else if (trans->is_write()) {
    tlm::tlm_fifo<Packet *> *response_queue = nullptr;
    const char *response_destination = nullptr;
    if (packet->type == PacketType::kVictimWriteReq) {
      packet->type = PacketType::kVictimWriteResp;
      response_queue = &victim_buffer_mem_resp_queue_;
      response_destination = "victim_buffer_mem_resp_queue";
    } else {
      assert(packet->type == PacketType::kMemWriteReq);
      packet->type = PacketType::kMemWriteResp;
      response_queue = &write_buffer_mem_resp_queue_;
      response_destination = "write_buffer_mem_resp_queue";
    }
    LogPacketEvent(cache_log::Category::kMem, "response_route", "accepted",
                   response_destination, packet);
    assert(response_queue->nb_can_put());
    success = response_queue->nb_put(packet);
    assert(success);
    DecrementLineEscapeHazard(packet);
    MarkProgress("mem_write_response_route");
    return;
  } else {
    assert(packet->type == PacketType::kMshrReadReq);
    packet->type = PacketType::kRefill;
    packet->GetCacheOwnedPayload()->PrepareRefillWrite();
    LogPacketEvent(cache_log::Category::kMem, "response_route", "accepted",
                   "mem_resp_queue", packet);
  }

  // Enqueue the packet to memory response queue
  success = mem_resp_queue_.nb_put(packet);
  assert(success);
  MarkProgress("mem_response_route");
}

/**
 * @brief Convert a byte address to its cache-line address.
 *
 * @param address Byte address.
 * @return Zero-based cache-line address.
 */
uint64_t Cache::ToLineAddress(uint64_t address) const {
  return address / config_.block_size_bytes;
}

/**
 * @brief Check whether the core queue head can become bypass memory traffic.
 *
 * SendMemRequest runs before the tag stage in a tick. Treating an immediately
 * admissible bypass core request as pending high-priority traffic preserves
 * bypass-before-MSHR arbitration without adding another MSHR input stage.
 */
bool Cache::CoreBypassCanEnterMemoryRequestQueue() {
  if (atomic_sequencer_.IsBusy() || !core_req_queue_.nb_can_peek() ||
      !bypass_req_queue_.nb_can_put()) {
    return false;
  }

  Packet *packet = nullptr;
  const bool success = core_req_queue_.nb_peek(packet);
  assert(success);
  assert(packet != nullptr);
  return packet->type == PacketType::kBypassCoreReq &&
         !ShouldStallForLineEscapeHazard(packet);
}

/**
 * @brief Check whether a sink request can fill an empty core queue this tick.
 *
 * The sink FIFO does not expose a request peek interface, so this is a narrow
 * one-cycle deferral used only when the core queue is empty. It gives an
 * immediately pending core request a chance to become visible to tag-stage
 * arbitration before a lower-priority MSHR memory request is sent.
 */
bool Cache::CoreInputCanFillEmptyRequestQueue() {
  return !mmio_sequencer_.IsBusy() && core_req_queue_.used() == 0 &&
         core_req_queue_.nb_can_put() && sink_.req_port->num_available() > 0;
}

/**
 * @brief Check whether cache-to-write-buffer input is waiting for an entry.
 *
 * A full write buffer can make read refills impossible to drain when those
 * refills need dirty-victim or write-through traffic to enter the same write
 * buffer. While this pressure exists, avoid sending more MSHR reads ahead of
 * write-buffer memory requests.
 */
bool Cache::WriteBufferInputBackpressured() const {
  const size_t write_buffer_occupancy =
      write_buffer_.PendingEntryCount() + write_buffer_.InflightEntryCount();
  return write_buffer_mem_req_queue_.used() > 0 &&
         write_buffer_occupancy >= config_.write_buffer_entries;
}

bool Cache::ShouldPrioritizeWriteBufferMemRequest() const {
  return write_buffer_mem_req_out_queue_.used() > 0 &&
         (defer_mshr_mem_req_for_write_buffer_ ||
          WriteBufferInputBackpressured());
}

bool Cache::ShouldDeferMshrMemRequestForWriteBuffer() const {
  if (WriteBufferInputBackpressured()) {
    return true;
  }
  return defer_mshr_mem_req_for_write_buffer_ &&
         (write_buffer_mem_req_queue_.used() > 0 ||
          write_buffer_mem_req_out_queue_.used() > 0 ||
          write_buffer_.HasPendingWork());
}

/**
 * @brief Reject an unsupported atomic request before it enters the sequencer.
 *
 * @param packet Atomic core request at the head of the core request queue.
 * @return true when the packet was rejected or rejection is stalled by response
 * backpressure; false when the packet is valid and remains queued.
 */
bool Cache::TryRejectInvalidAtomicRequest(Packet *packet) {
  assert(packet != nullptr);
  assert(packet->type == PacketType::kCoreReq);
  assert(packet->is_atomic);

  if (IsValidAtomicRequest(*packet->GetTlmGp())) {
    return false;
  }
  if (!core_resp_queue_.nb_can_put()) {
    MarkBlockReason("core_resp_backpressure", true);
    return true;
  }

  Packet *dequeued_packet = nullptr;
  const bool got_packet = core_req_queue_.nb_get(dequeued_packet);
  assert(got_packet);
  assert(dequeued_packet == packet);
  packet->GetTlmGp()->set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
  const bool put_response = core_resp_queue_.nb_put(packet);
  assert(put_response);
  MarkProgress("atomic_reject_response");
  return true;
}

/**
 * @brief Select the next packet allowed to enter the tag-array stage.
 *
 * @param mem_resp_packet Optional memory response currently at the response
 * queue head.
 * @param selected_refill Set to true when the selected packet is a refill
 * response.
 * @return Selected packet, or nullptr when all candidates are blocked.
 */
Packet *Cache::SelectTagArrayPacket(Packet *mem_resp_packet,
                                    bool *selected_refill) {
  assert(selected_refill != nullptr);
  *selected_refill = false;
  const bool tag_array_output_available = tag_array_resp_queue_.nb_can_put();

  Packet *packet = nullptr;
  bool success = false;
  bool replay_blocked_by_escape_hazard = false;

  // Tag-array arbitration is encoded by branch order.
  if (mshr_file_replay_queue_.nb_can_get()) {
    defer_tag_arbitration_for_refill_replay_cycles_ = 0;
    success = mshr_file_replay_queue_.nb_peek(packet);
    assert(success);
    assert(packet != nullptr);
    if (atomic_sequencer_.phase == AtomicSequencer::Phase::kWaitReplay &&
        packet == atomic_sequencer_.packet) {
      MarkBlockReason("atomic_wait_replay", false);
      LogPacketEvent(cache_log::Category::kArb, "blocked", "atomic_wait_replay",
                     "replay", packet);
      return nullptr;
    }
    if (ShouldStallForLineEscapeHazard(packet)) {
      MarkBlockReason("escape_hazard", false);
      LogPacketEvent(cache_log::Category::kArb, "blocked", "escape_hazard",
                     "replay", packet);
      replay_blocked_by_escape_hazard = true;
    } else if (!tag_array_output_available) {
      MarkBlockReason("tag_output_full", false);
      LogPacketEvent(cache_log::Category::kArb, "blocked", "tag_output_full",
                     "replay", packet);
      return nullptr;
    } else {
      // Replay arbitration path.
      success = mshr_file_replay_queue_.nb_get(packet);
      assert(success);
      MarkProgress("tag_select_replay");
      LogPacketEvent(cache_log::Category::kArb, "selected", "none", "replay",
                     packet);
      return packet;
    }
  }

  if (mem_resp_packet != nullptr &&
      mem_resp_packet->type == PacketType::kBypassCoreResp) {
    assert(mem_resp_packet != nullptr);
    if (!tag_array_output_available) {
      MarkBlockReason("tag_output_full", false);
      LogPacketEvent(cache_log::Category::kArb, "blocked", "tag_output_full",
                     "bypass_response", mem_resp_packet);
      return nullptr;
    }

    success = mem_resp_queue_.nb_get(packet);
    assert(success);
    assert(packet == mem_resp_packet);
    MarkProgress("tag_select_bypass_response");
    LogPacketEvent(cache_log::Category::kArb, "selected", "none",
                   "bypass_response", packet);
    return packet;
  }

  if (replay_blocked_by_escape_hazard) {
    return nullptr;
  }

  if (defer_tag_arbitration_for_refill_replay_cycles_ > 0) {
    // MSHR publishes replay packets from the refill notification on the next
    // clock edge. The tlm_fifo write is not visible to this cache tick yet, so
    // hold younger tag work for one cycle to keep the refilled line replayable.
    --defer_tag_arbitration_for_refill_replay_cycles_;
    MarkBlockReason("refill_replay_visibility", false);
    LogPacketEvent(cache_log::Category::kArb, "blocked",
                   "refill_replay_visibility", "refill", nullptr);
    return nullptr;
  }

  if (mem_resp_packet != nullptr &&
      mem_resp_packet->type == PacketType::kRefill) {
    // Refill arbitration path.
    assert(mem_resp_packet != nullptr);
    if (!tag_array_output_available) {
      MarkBlockReason("tag_output_full", false);
      LogPacketEvent(cache_log::Category::kArb, "blocked", "tag_output_full",
                     "refill", mem_resp_packet);
      return nullptr;
    }

    if (!mshr_file_refill_notify_queue_.nb_can_put()) {
      MarkBlockReason("refill_notify_full", false);
      LogPacketEvent(cache_log::Category::kArb, "blocked", "refill_notify_full",
                     "refill", mem_resp_packet);
      return nullptr;
    }

    *selected_refill = true;
    LogPacketEvent(cache_log::Category::kArb, "selected", "none", "refill",
                   mem_resp_packet);
    return mem_resp_packet;
  }

  if (core_req_queue_.nb_can_get()) {
    // Core-request arbitration path.
    success = core_req_queue_.nb_peek(packet);
    assert(success);
    assert(packet != nullptr);

    if (atomic_sequencer_.IsBusy()) {
      MarkBlockReason("atomic_busy", false);
      LogPacketEvent(cache_log::Category::kArb, "blocked", "atomic_busy",
                     "core", packet);
      return nullptr;
    }

    if (packet->type == PacketType::kBypassCoreReq) {
      if (ShouldStallForLineEscapeHazard(packet)) {
        MarkBlockReason("escape_hazard", false);
        LogPacketEvent(cache_log::Category::kArb, "blocked", "escape_hazard",
                       "bypass_request", packet);
        return nullptr;
      }
      if (!bypass_req_queue_.nb_can_put()) {
        MarkBlockReason("bypass_queue_full", false);
        LogPacketEvent(cache_log::Category::kArb, "blocked",
                       "bypass_queue_full", "bypass_request", packet);
        return nullptr;
      }

      success = core_req_queue_.nb_get(packet);
      assert(success);
      MarkProgress("tag_select_bypass_request");
      LogPacketEvent(cache_log::Category::kArb, "selected", "none",
                     "bypass_request", packet);
      return packet;
    }

    assert(packet->type == PacketType::kCoreReq);
    if (packet->is_atomic) {
      if (TryRejectInvalidAtomicRequest(packet)) {
        LogPacketEvent(cache_log::Category::kArb, "blocked",
                       "atomic_reject_or_response_full", "core", packet);
        return nullptr;
      }
      if (ShouldStallForLineEscapeHazard(packet)) {
        MarkBlockReason("escape_hazard", false);
        LogPacketEvent(cache_log::Category::kArb, "blocked", "escape_hazard",
                       "atomic", packet);
        return nullptr;
      }
      (void)TryStartAtomicSequencerFromCoreQueue(packet);
      LogPacketEvent(cache_log::Category::kArb, "selected", "none", "atomic",
                     packet);
      return nullptr;
    }

    if (ShouldStallForLineEscapeHazard(packet)) {
      MarkBlockReason("escape_hazard", false);
      LogPacketEvent(cache_log::Category::kArb, "blocked", "escape_hazard",
                     "core", packet);
      return nullptr;
    }

    if (!tag_array_output_available) {
      MarkBlockReason("tag_output_full", false);
      LogPacketEvent(cache_log::Category::kArb, "blocked", "tag_output_full",
                     "core", packet);
      return nullptr;
    }

    LogPacketEvent(cache_log::Category::kArb, "selected", "none", "core",
                   packet);
    return packet;
  }

  return nullptr;
}

void Cache::AccessTagArrayStage() {
  if (TryAdvanceAtomicTagStage()) {
    return;
  }

  Packet *mem_resp_packet = nullptr;
  bool success = false;

  // Peek the memory response queue to see if there is a memory response packet
  if (mem_resp_queue_.nb_can_peek()) {
    success = mem_resp_queue_.nb_peek(mem_resp_packet);
    assert(success);
    assert(mem_resp_packet != nullptr);
    assert(mem_resp_packet->type == PacketType::kRefill ||
           mem_resp_packet->type == PacketType::kBypassCoreResp);
  }

  bool tag_array_packet_is_refill = false;
  Packet *tag_array_packet =
      SelectTagArrayPacket(mem_resp_packet, &tag_array_packet_is_refill);

  if (tag_array_packet == nullptr) {
    return;
  }

  if (tag_array_packet->type == PacketType::kBypassCoreResp) {
    success = tag_array_resp_queue_.nb_put(tag_array_packet);
    assert(success);
    MarkProgress("tag_to_data_bypass_response");
    LogPacketTraceEvent(cache_log::Category::kStage, "tag_to_data", "bypass",
                        "tag_array", tag_array_packet);
    return;
  }

  if (tag_array_packet->type == PacketType::kBypassCoreReq) {
    IncrementLineEscapeHazard(tag_array_packet);
    success = bypass_req_queue_.nb_put(tag_array_packet);
    assert(success);
    MarkProgress("tag_to_mem_bypass_request");
    LogPacketTraceEvent(cache_log::Category::kStage, "tag_to_mem", "bypass",
                        "bypass_queue", tag_array_packet);
    return;
  }

  // Process the packet through tag array
  const TagArray::AccessStatus status = tag_array_.Process(tag_array_packet);
  if (tag_array_packet_is_refill) {
    if (status == TagArray::AccessStatus::kNoVictim) {
      // All victim candidates are locked, so the refill cannot be processed
      // this cycle.
      MarkBlockReason("no_victim", false);
      LogPacketEvent(cache_log::Category::kArb, "blocked", "no_victim",
                     "refill", tag_array_packet);
      return;
    }
    assert(status == TagArray::AccessStatus::kHit);

    Packet *dequeued_packet = nullptr;
    success = mem_resp_queue_.nb_get(dequeued_packet);
    assert(success);
    assert(dequeued_packet == tag_array_packet);
    if (tag_array_packet->is_write() && tag_array_packet->is_victim_dirty) {
      IncrementLineEscapeHazardForLine(
          ToLineAddress(tag_array_packet->victim_address));
    }
  } else if (tag_array_packet->type == PacketType::kCoreReq) {
    assert(status == TagArray::AccessStatus::kHit ||
           status == TagArray::AccessStatus::kMiss);

    if (status == TagArray::AccessStatus::kMiss &&
        !(tag_array_packet->is_write() &&
          config_.write_miss_policy == WriteMissPolicy::kWriteNoAllocate)) {
      const MshrFile::AcceptStatus accept_status =
          TryAcceptReadMiss(tag_array_packet);
      if (accept_status == MshrFile::AcceptStatus::kRejected) {
        MarkBlockReason("mshr_reject", false);
        LogPacketEvent(cache_log::Category::kArb, "blocked", "mshr_reject",
                       "core_miss", tag_array_packet);
        return;
      }
      if (accept_status == MshrFile::AcceptStatus::kAcceptedPrimary) {
        defer_mshr_mem_req_for_core_input_ = true;
      }

      Packet *dequeued_packet = nullptr;
      success = core_req_queue_.nb_get(dequeued_packet);
      assert(success);
      assert(dequeued_packet == tag_array_packet);
      MarkProgress("mshr_accept_core_miss");
      return;
    }

    const bool write_no_allocate_miss =
        status == TagArray::AccessStatus::kMiss &&
        tag_array_packet->is_write() &&
        config_.write_miss_policy == WriteMissPolicy::kWriteNoAllocate;
    const bool write_through_hit =
        status == TagArray::AccessStatus::kHit &&
        tag_array_packet->is_write() &&
        config_.write_hit_policy == WriteHitPolicy::kWriteThrough;
    if (write_no_allocate_miss || write_through_hit) {
      IncrementLineEscapeHazard(tag_array_packet);
    }

    Packet *dequeued_packet = nullptr;
    success = core_req_queue_.nb_get(dequeued_packet);
    assert(success);
    assert(dequeued_packet == tag_array_packet);
    MarkProgress("tag_accept_core_hit_or_wna");
  } else {
    assert(tag_array_packet->type == PacketType::kReplay);
    assert(status == TagArray::AccessStatus::kHit);
    if (tag_array_packet->is_write() &&
        config_.write_hit_policy == WriteHitPolicy::kWriteThrough) {
      IncrementLineEscapeHazard(tag_array_packet);
    }
  }

  // Enqueue the packet to tag array response queue
  success = tag_array_resp_queue_.nb_put(tag_array_packet);
  assert(success);
  MarkProgress("tag_to_data");
  LogPacketTraceEvent(cache_log::Category::kStage, "tag_to_data", "accepted",
                      "tag_array", tag_array_packet);
}

/**
 * @brief Allocate a cache-owned write-buffer packet from a write request.
 *
 * @param source_packet Core or replay write request packet to duplicate.
 * @return Cache-owned packet ready for the write buffer.
 */
Packet *Cache::AllocateWriteBufferPacketFrom(const Packet *source_packet) {
  assert(source_packet != nullptr);
  assert(source_packet->type == PacketType::kCoreReq ||
         source_packet->type == PacketType::kReplay);
  assert(source_packet->is_write());

  Packet *write_buffer_packet = AllocatePacketWithOwnedPayload();
  assert(write_buffer_packet != nullptr);
  write_buffer_packet->is_atomic = source_packet->is_atomic;
  SetTraceParent(write_buffer_packet, source_packet);
  write_buffer_packet->GetCacheOwnedPayload()->InitLineWriteFrom(
      *source_packet->GetTlmGp(), config_.block_size_bytes);
  write_buffer_packet->type = PacketType::kMemWriteReq;
  return write_buffer_packet;
}

/**
 * @brief Allocate a cache-owned packet for a dirty victim block.
 *
 * @param refill_packet Refill packet that evicted the dirty victim.
 * @return Cache-owned full-line packet ready for the victim buffer.
 */
Packet *Cache::AllocateVictimPacketFrom(const Packet *refill_packet) {
  assert(refill_packet != nullptr);
  assert(refill_packet->type == PacketType::kRefill);
  assert(refill_packet->is_victim_dirty);
  assert(refill_packet->victim_address % config_.block_size_bytes == 0);

  Packet *victim_packet = AllocatePacketWithOwnedPayload();
  assert(victim_packet != nullptr);
  victim_packet->is_atomic = refill_packet->is_atomic;
  SetTraceParent(victim_packet, refill_packet);
  victim_packet->GetCacheOwnedPayload()->InitLineWrite(
      refill_packet->victim_address, config_.block_size_bytes);
  victim_packet->type = PacketType::kVictimWriteReq;
  return victim_packet;
}

/**
 * @brief Admit a cacheable read miss with dirty-victim escape capacity.
 *
 * Secondary misses reuse the primary entry's reservation. Primary misses must
 * reserve a VictimBuffer slot before MSHR state or a memory request is created.
 *
 * @param packet Core request that missed in the tag array.
 * @return MSHR admission result; rejection leaves the packet in its queue.
 */
MshrFile::AcceptStatus Cache::TryAcceptReadMiss(Packet *packet) {
  const MshrFile::ProbeStatus probe = mshr_file_.ProbeReadMiss(*packet);
  if (probe == MshrFile::ProbeStatus::kRejected) {
    return MshrFile::AcceptStatus::kRejected;
  }
  if (probe == MshrFile::ProbeStatus::kAcceptableSecondary) {
    return mshr_file_.TryAcceptReadMiss(packet);
  }
  if (!mshr_file_mem_req_queue_.nb_can_put()) {
    LogPacketEvent(cache_log::Category::kMshr, "blocked", "mshr_mem_req_full",
                   "mshr_file_mem_req_queue", packet);
    return MshrFile::AcceptStatus::kRejected;
  }
  std::optional<VictimReservation> reservation = victim_buffer_.TryReserve();
  if (!reservation.has_value()) {
    MarkBlockReason("victim_reservation_full", false);
    LogPacketEvent(cache_log::Category::kMshr, "blocked",
                   "victim_reservation_full", "victim_buffer", packet);
    return MshrFile::AcceptStatus::kRejected;
  }
  LogPacketEvent(cache_log::Category::kMshr, "victim_reserve", "accepted",
                 "victim_buffer", packet);
  const MshrFile::AcceptStatus status =
      mshr_file_.TryAcceptReadMiss(packet, &*reservation);
  assert(status == MshrFile::AcceptStatus::kAcceptedPrimary);
  return status;
}

/**
 * @brief Start the atomic sequencer from the core request queue head.
 *
 * @param packet Atomic core request to dequeue and sequence.
 * @return true after the sequencer is started.
 */
bool Cache::TryStartAtomicSequencerFromCoreQueue(Packet *packet) {
  assert(packet != nullptr);
  assert(packet->type == PacketType::kCoreReq);
  assert(packet->is_atomic);
  assert(!atomic_sequencer_.IsBusy());

  Packet *dequeued_packet = nullptr;
  const bool got_packet = core_req_queue_.nb_get(dequeued_packet);
  assert(got_packet);
  assert(dequeued_packet == packet);

  atomic_sequencer_.Start(packet, ToLineAddress(packet->GetAddress()));
  MarkProgress("atomic_start");
  return true;
}

/**
 * @brief Probe or miss-handle the atomic packet in the tag-array stage.
 *
 * @return true when the atomic path used or held the tag stage; false when MSHR
 * backpressure leaves the stage available for other arbitration.
 */
bool Cache::TryProbeAtomicSequencer() {
  assert(atomic_sequencer_.IsBusy());
  assert(atomic_sequencer_.phase == AtomicSequencer::Phase::kProbeOrMiss);
  Packet *packet = atomic_sequencer_.packet;
  assert(packet != nullptr);
  assert(packet->is_atomic);
  assert(packet->type == PacketType::kCoreReq);

  const TagArray::AccessStatus status = tag_array_.ProbeAtomic(packet);
  switch (status) {
    case TagArray::AccessStatus::kLocked:
      MarkBlockReason("atomic_locked", false);
      return true;
    case TagArray::AccessStatus::kHit:
      tag_array_.LockEntry(packet->location);
      atomic_sequencer_.location = packet->location;
      atomic_sequencer_.phase = AtomicSequencer::Phase::kReadOld;
      MarkProgress("atomic_probe_hit");
      return true;
    case TagArray::AccessStatus::kMiss: {
      const MshrFile::AcceptStatus accept_status = TryAcceptReadMiss(packet);
      if (accept_status == MshrFile::AcceptStatus::kRejected) {
        MarkBlockReason("mshr_reject", false);
        LogPacketEvent(cache_log::Category::kArb, "blocked", "mshr_reject",
                       "atomic_miss", packet);
        return false;
      }
      if (accept_status == MshrFile::AcceptStatus::kAcceptedPrimary) {
        defer_mshr_mem_req_for_core_input_ = true;
      }
      atomic_sequencer_.phase = AtomicSequencer::Phase::kWaitReplay;
      MarkProgress("atomic_mshr_accept");
      return true;
    }
    case TagArray::AccessStatus::kNoVictim:
      assert(false && "Atomic probe cannot select a victim");
      return true;
  }
  return false;
}

/**
 * @brief Consume the replay packet awaited by the atomic sequencer.
 *
 * @return true when the matching replay used or held the tag stage; false when
 * no matching replay is ready.
 */
bool Cache::TryConsumeAtomicReplay() {
  assert(atomic_sequencer_.IsBusy());
  assert(atomic_sequencer_.phase == AtomicSequencer::Phase::kWaitReplay);

  if (!mshr_file_replay_queue_.nb_can_peek()) {
    return false;
  }

  Packet *packet = nullptr;
  bool success = mshr_file_replay_queue_.nb_peek(packet);
  assert(success);
  assert(packet != nullptr);
  if (packet != atomic_sequencer_.packet) {
    MarkBlockReason("atomic_replay_order", false);
    return false;
  }

  const TagArray::AccessStatus status = tag_array_.ProbeAtomic(packet);
  switch (status) {
    case TagArray::AccessStatus::kLocked:
      MarkBlockReason("atomic_locked", false);
      return true;
    case TagArray::AccessStatus::kHit:
      success = mshr_file_replay_queue_.nb_get(packet);
      assert(success);
      assert(packet == atomic_sequencer_.packet);
      tag_array_.LockEntry(packet->location);
      atomic_sequencer_.location = packet->location;
      atomic_sequencer_.phase = AtomicSequencer::Phase::kReadOld;
      MarkProgress("atomic_replay_consume");
      return true;
    case TagArray::AccessStatus::kMiss:
      assert(false && "Atomic replay should hit after refill");
      return true;
    case TagArray::AccessStatus::kNoVictim:
      assert(false && "Atomic replay cannot select a victim");
      return true;
  }
  return false;
}

/**
 * @brief Advance the tag-array half of the atomic sequencer.
 *
 * @return true when the atomic sequencer occupied the tag stage.
 */
bool Cache::TryAdvanceAtomicTagStage() {
  if (!atomic_sequencer_.IsBusy()) {
    return false;
  }

  switch (atomic_sequencer_.phase) {
    case AtomicSequencer::Phase::kIdle:
      return false;
    case AtomicSequencer::Phase::kWaitEscapeFence:
      if (HasLineEscapeHazard(atomic_sequencer_.line_address)) {
        MarkBlockReason("escape_hazard", false);
        return false;
      }
      atomic_sequencer_.phase = AtomicSequencer::Phase::kProbeOrMiss;
      MarkProgress("atomic_escape_fence_clear");
      return TryProbeAtomicSequencer();
    case AtomicSequencer::Phase::kProbeOrMiss:
      return TryProbeAtomicSequencer();
    case AtomicSequencer::Phase::kWaitReplay:
      return TryConsumeAtomicReplay();
    case AtomicSequencer::Phase::kReadOld:
    case AtomicSequencer::Phase::kWriteNewRespond:
      return false;
  }
  return false;
}

/**
 * @brief Advance the data-array half of the atomic sequencer.
 *
 * @return true when the atomic sequencer occupied the data stage.
 */
bool Cache::TryAdvanceAtomicDataStage() {
  if (!atomic_sequencer_.IsBusy()) {
    return false;
  }

  Packet *packet = atomic_sequencer_.packet;
  assert(packet != nullptr);
  assert(packet->is_atomic);

  if (atomic_sequencer_.phase == AtomicSequencer::Phase::kReadOld) {
    atomic_sequencer_.size = packet->GetTlmGp()->get_data_length();
    const bool read_ok = data_array_.ReadBytes(
        packet, atomic_sequencer_.old_value.data(), atomic_sequencer_.size);
    assert(read_ok);

    simtix::AtomicExtension *ext = nullptr;
    packet->GetTlmGp()->get_extension(ext);
    assert(ext != nullptr);
    const uint8_t *operand = packet->GetTlmGp()->get_data_ptr();
    if (atomic_sequencer_.size == sizeof(uint32_t)) {
      const uint32_t old_value = LoadU32(atomic_sequencer_.old_value.data());
      const uint32_t operand_value = LoadU32(operand);
      StoreU32(atomic_sequencer_.new_value.data(),
               ApplyAtomicOp32(ext->op, old_value, operand_value));
    } else {
      assert(atomic_sequencer_.size == sizeof(uint64_t));
      const uint64_t old_value = LoadU64(atomic_sequencer_.old_value.data());
      const uint64_t operand_value = LoadU64(operand);
      StoreU64(atomic_sequencer_.new_value.data(),
               ApplyAtomicOp64(ext->op, old_value, operand_value));
    }
    atomic_sequencer_.phase = AtomicSequencer::Phase::kWriteNewRespond;
    MarkProgress("atomic_read_old");
    return true;
  }

  if (atomic_sequencer_.phase != AtomicSequencer::Phase::kWriteNewRespond) {
    return false;
  }
  if (!core_resp_queue_.nb_can_put()) {
    MarkBlockReason("core_resp_backpressure", true);
    return true;
  }

  const bool write_ok = data_array_.WriteBytes(
      packet, atomic_sequencer_.new_value.data(), atomic_sequencer_.size);
  assert(write_ok);
  tag_array_.MarkDirty(atomic_sequencer_.location);
  std::memcpy(packet->GetTlmGp()->get_data_ptr(),
              atomic_sequencer_.old_value.data(), atomic_sequencer_.size);
  tag_array_.UnlockEntry(atomic_sequencer_.location);

  const bool put_response = core_resp_queue_.nb_put(packet);
  assert(put_response);
  atomic_sequencer_.Reset();
  MarkProgress("atomic_write_response");
  return true;
}

/**
 * @brief Handle a tag-array miss packet in the data-array stage.
 *
 * @param packet Miss packet currently at the tag-array response queue head.
 * @return true when the packet advanced to its next queue; false when the data
 * stage must stall.
 */
bool Cache::TryProcessDataArrayMiss(Packet *packet) {
  assert(packet != nullptr);
  assert(!packet->is_hit);

  bool success = false;

  if (packet->is_write() &&
      config_.write_miss_policy == WriteMissPolicy::kWriteNoAllocate) {
    // Write-no-allocate write miss packet
    if (!write_buffer_mem_req_queue_.nb_can_put()) {
      MarkBlockReason("write_buffer_mem_req_full", false);
      // Write buffer or core response request queue is full, stall the data
      // array stage
      return false;
    }
    if (!core_resp_queue_.nb_can_put()) {
      MarkBlockReason("core_resp_backpressure", true);
      // Write buffer or core response request queue is full, stall the data
      // array stage
      return false;
    }

    // Put the write request to write buffer for memory write and respond to
    // core with write completion
    Packet *write_buffer_packet = AllocateWriteBufferPacketFrom(packet);
    success = write_buffer_mem_req_queue_.nb_put(write_buffer_packet);
    assert(success);

    Packet *dequeued_packet = nullptr;
    success = tag_array_resp_queue_.nb_get(dequeued_packet);
    assert(success);
    assert(dequeued_packet == packet);
    success = core_resp_queue_.nb_put(packet);
    assert(success);
    MarkProgress("data_write_no_allocate");
    LogPacketTraceEvent(cache_log::Category::kStage, "data_to_core_response",
                        "write_no_allocate", "data_array", packet);
    return true;
  }

  if (packet->type == PacketType::kCoreReq) {
    assert(packet->is_write());
    assert(config_.write_miss_policy == WriteMissPolicy::kWriteNoAllocate);
  }

  lv::Fatal("Unexpected miss packet type in data array stage\n");
  return false;
}

/**
 * @brief Handle a tag-array hit packet in the data-array stage.
 *
 * @param packet Hit packet currently at the tag-array response queue head.
 * @return true when the packet was consumed or advanced; false when the data
 * stage must stall.
 */
bool Cache::TryProcessDataArrayHit(Packet *packet) {
  assert(packet != nullptr);
  assert(packet->is_hit);

  bool success = false;
  const bool is_refill_packet = packet->type == PacketType::kRefill;

  if (!is_refill_packet && !core_resp_queue_.nb_can_put()) {
    MarkBlockReason("core_resp_backpressure", true);
    // Core response request queue is full, stall the data array stage
    return false;
  }

  const bool is_write_through_packet =
      (packet->type == PacketType::kCoreReq ||
       packet->type == PacketType::kReplay) &&
      packet->is_write() &&
      config_.write_hit_policy == WriteHitPolicy::kWriteThrough;

  // Dirty-victim writeback is generated only by refill packets.
  const bool need_victim_packet =
      is_refill_packet && packet->is_write() && packet->is_victim_dirty;

  if (is_refill_packet && !mshr_file_refill_notify_queue_.nb_can_put()) {
    MarkBlockReason("refill_notify_full", false);
    LogPacketEvent(cache_log::Category::kMshr, "blocked", "refill_notify_full",
                   "mshr_file_refill_notify_queue", packet);
    return false;
  }

  if (is_write_through_packet) {
    if (!write_buffer_mem_req_queue_.nb_can_put()) {
      MarkBlockReason("write_buffer_mem_req_full", false);
      // Write buffer is full, stall the data array stage
      return false;
    }
  }

  // Process the hit packet through data array
  Packet *dequeued_packet = nullptr;
  success = tag_array_resp_queue_.nb_get(dequeued_packet);
  assert(success);
  assert(dequeued_packet == packet);

  Packet *victim_packet = nullptr;
  DataArray::VictimBuffer victim_buffer;
  if (need_victim_packet) {
    victim_packet = AllocateVictimPacketFrom(packet);
    MemPayload *victim_payload = victim_packet->GetCacheOwnedPayload();
    victim_buffer.data = victim_payload->buffer.data();
    victim_buffer.size = victim_payload->buffer.size();
  }

  const DataArray::ProcessResult data_result = data_array_.Process(
      packet, need_victim_packet ? &victim_buffer : nullptr);
  if (!data_result.ok) {
    assert(!data_result.victim_generated);
    if (victim_packet != nullptr) {
      ReleasePacket(victim_packet);
    }
    if (is_write_through_packet) {
      DecrementLineEscapeHazard(packet);
    }
    if (need_victim_packet) {
      DecrementLineEscapeHazardForLine(ToLineAddress(packet->victim_address));
    }

    if (is_refill_packet) {
      assert(packet->mshr_id.has_value());
      VictimReservation reservation =
          mshr_file_.TakeVictimReservation(*packet->mshr_id);
      victim_buffer_.Cancel(std::move(reservation));
      LogPacketEvent(cache_log::Category::kMshr, "victim_cancel",
                     "refill_error", "victim_buffer", packet);
      success = mshr_file_refill_notify_queue_.nb_put(
          {packet->GetAddress(), packet->unique_id, *packet->mshr_id});
      assert(success);
      defer_tag_arbitration_for_refill_replay_cycles_ = 2;
      LogPacketTraceEvent(cache_log::Category::kStage, "refill_error",
                          "data_array", "data_array", packet);
      ReleasePacket(packet);
      MarkProgress("refill_error");
      return true;
    }

    success = core_resp_queue_.nb_put(packet);
    assert(success);
    MarkProgress("data_array_error_response");
    LogPacketTraceEvent(cache_log::Category::kStage, "data_to_core_response",
                        "data_array_error", "data_array", packet);
    return true;
  }

  assert(need_victim_packet == data_result.victim_generated);
  if (is_write_through_packet) {
    // Write-through policy, duplicate the write packet to write buffer for
    // memory write only after the data-array write succeeds.
    Packet *write_buffer_packet = AllocateWriteBufferPacketFrom(packet);
    success = write_buffer_mem_req_queue_.nb_put(write_buffer_packet);
    assert(success);
  }

  if (need_victim_packet) {
    assert(victim_packet != nullptr);
    assert(victim_buffer.address == packet->victim_address);
  }

  if (is_refill_packet) {
    assert(packet->mshr_id.has_value());
    // Data installation has resolved the worst case reserved at miss admit.
    VictimReservation reservation =
        mshr_file_.TakeVictimReservation(*packet->mshr_id);
    if (need_victim_packet) {
      victim_buffer_.Commit(std::move(reservation), victim_packet);
      LogPacketEvent(cache_log::Category::kMem, "victim_commit", "dirty_refill",
                     "victim_buffer", victim_packet);
    } else {
      victim_buffer_.Cancel(std::move(reservation));
      LogPacketEvent(cache_log::Category::kMshr, "victim_cancel",
                     "clean_refill", "victim_buffer", packet);
    }
    success = mshr_file_refill_notify_queue_.nb_put(
        {packet->GetAddress(), packet->unique_id, *packet->mshr_id});
    assert(success);
    defer_tag_arbitration_for_refill_replay_cycles_ = 2;
    MarkProgress("refill_notify_enqueue");
    // Refill packet is consumed after processing, release the packet
    LogPacketTraceEvent(cache_log::Category::kStage, "refill_complete",
                        "data_array", "data_array", packet);
    ReleasePacket(packet);
    MarkProgress("refill_complete");
    return true;
  }

  // Put the hit packet to core response queue
  success = core_resp_queue_.nb_put(packet);
  assert(success);
  MarkProgress("data_to_core_response");
  LogPacketTraceEvent(cache_log::Category::kStage, "data_to_core_response",
                      "hit", "data_array", packet);
  return true;
}

void Cache::AccessDataArrayStage() {
  if (TryAdvanceAtomicDataStage()) {
    return;
  }

  if (!tag_array_resp_queue_.nb_can_peek()) {
    return;
  }

  Packet *packet = nullptr;
  bool success = tag_array_resp_queue_.nb_peek(packet);
  assert(success);
  assert(packet != nullptr);
  assert(packet->type == PacketType::kCoreReq ||
         packet->type == PacketType::kBypassCoreResp ||
         packet->type == PacketType::kRefill ||
         packet->type == PacketType::kReplay);

  if (packet->type == PacketType::kBypassCoreResp) {
    if (!core_resp_queue_.nb_can_put()) {
      MarkBlockReason("core_resp_backpressure", true);
      return;
    }

    Packet *dequeued_packet = nullptr;
    success = tag_array_resp_queue_.nb_get(dequeued_packet);
    assert(success);
    assert(dequeued_packet == packet);
    success = core_resp_queue_.nb_put(packet);
    assert(success);
    MarkProgress("data_bypass_to_core_response");
    LogPacketTraceEvent(cache_log::Category::kStage, "data_to_core_response",
                        "bypass", "data_array", packet);
    return;
  }

  if (!packet->is_hit) {
    (void)TryProcessDataArrayMiss(packet);
    return;
  }

  (void)TryProcessDataArrayHit(packet);
}

void Cache::SendCoreResponse() {
  if (!core_resp_queue_.nb_can_get()) {
    // No response to send, do nothing
    return;
  }
  if (sink_.resp_port->num_free() <= 0) {
    MarkBlockReason("core_resp_backpressure", true);
    // No response to send or core cannot accept response, do nothing
    return;
  }

  Packet *packet = nullptr;
  bool success = core_resp_queue_.nb_get(packet);
  assert(success);
  assert(packet != nullptr);
  assert(packet->type == PacketType::kCoreReq ||
         packet->type == PacketType::kBypassCoreResp ||
         packet->type == PacketType::kReplay);

  // Convert the cache packet to tlm payload and send out through sink port
  tlm::tlm_generic_payload *trans = packet->GetTlmGp();
  if (packet->type != PacketType::kBypassCoreResp &&
      trans->get_response_status() == tlm::TLM_INCOMPLETE_RESPONSE) {
    // Cache-generated responses did not pass through memory, so mark them OK.
    // Bypass responses keep the status returned by memory. Locally rejected
    // requests keep their explicit error status.
    trans->set_response_status(tlm::TLM_OK_RESPONSE);
  }
  success = sink_.resp_port->nb_write(trans);
  assert(success);
  MarkProgress("core_response_sent");
  LogPacketTraceEvent(cache_log::Category::kStage, "core_response", "sent",
                      "core_resp_queue", packet);

  if (packet->type == PacketType::kBypassCoreResp) {
    DecrementLineEscapeHazard(packet);
  }

  // Release the packet after sending out the response
  ReleasePacket(packet);
}

void Cache::SendMemRequest() {
  if (source_.req_port->num_free() <= 0) {
    if (bypass_req_queue_.nb_can_get() ||
        mshr_file_mem_req_queue_.nb_can_get() ||
        victim_buffer_mem_req_out_queue_.nb_can_get() ||
        write_buffer_mem_req_out_queue_.nb_can_get() ||
        CoreBypassCanEnterMemoryRequestQueue()) {
      MarkBlockReason("memory_req_full", true);
    }
    // Memory cannot accept request, do nothing
    return;
  }

  Packet *packet = nullptr;
  const char *request_source = "none";
  bool success = false;

  // Bypass has absolute priority, including the one-cycle stall while
  // CoreBypassCanEnterMemoryRequestQueue moves a request toward the bypass
  // queue. Saturated non-cacheable traffic can therefore indefinitely delay
  // victim, MSHR, and write-buffer requests. This is intentional for now; use
  // aging or round-robin if cores may saturate the non-cacheable path.
  // Memory-request arbitration is encoded by branch order.
  if (bypass_req_queue_.nb_can_get()) {
    // Core-request bypass path.
    success = bypass_req_queue_.nb_get(packet);
    assert(success);
    request_source = "bypass";
  } else if (CoreBypassCanEnterMemoryRequestQueue()) {
    MarkBlockReason("core_bypass_pending", false);
    LogPacketEvent(cache_log::Category::kMem, "blocked", "core_bypass_pending",
                   "mshr", nullptr);
    return;
  } else if (defer_mshr_mem_req_for_core_input_ &&
             CoreInputCanFillEmptyRequestQueue()) {
    defer_mshr_mem_req_for_core_input_ = false;
    MarkBlockReason("defer_mshr_for_core_input", false);
    LogPacketEvent(cache_log::Category::kMem, "blocked",
                   "defer_mshr_for_core_input", "mshr", nullptr);
    return;
  } else {
    defer_mshr_mem_req_for_core_input_ = false;
  }

  if (packet == nullptr && victim_buffer_mem_req_out_queue_.nb_can_get()) {
    success = victim_buffer_mem_req_out_queue_.nb_get(packet);
    assert(success);
    request_source = "victim_buffer";
  }

  if (packet == nullptr && ShouldPrioritizeWriteBufferMemRequest()) {
    // Write-buffer request under backpressure: send it before issuing more
    // MSHR reads so write responses can free write-buffer entries.
    success = write_buffer_mem_req_out_queue_.nb_get(packet);
    assert(success);
    request_source = "write_buffer";
    defer_mshr_mem_req_for_write_buffer_ = false;
  }

  if (packet == nullptr && mshr_file_mem_req_queue_.nb_can_get()) {
    if (ShouldDeferMshrMemRequestForWriteBuffer()) {
      defer_mshr_mem_req_for_write_buffer_ = true;
      MarkBlockReason("defer_mshr_for_write_buffer", false);
      LogPacketEvent(cache_log::Category::kMem, "blocked",
                     "defer_mshr_for_write_buffer", "mshr", nullptr);
      return;
    }
    // MSHR read-request arbitration path.
    success = mshr_file_mem_req_queue_.nb_get(packet);
    assert(success);
    request_source = "mshr";
  } else if (packet == nullptr &&
             write_buffer_mem_req_out_queue_.nb_can_get()) {
    // Write-buffer request arbitration path.
    success = write_buffer_mem_req_out_queue_.nb_get(packet);
    assert(success);
    request_source = "write_buffer";
    defer_mshr_mem_req_for_write_buffer_ = false;
  }

  if (packet == nullptr) {
    // No memory request to send, do nothing
    return;
  }

  // Convert the cache packet to tlm payload and send out through source port
  assert(packet->type == PacketType::kBypassCoreReq ||
         packet->type == PacketType::kMshrReadReq ||
         packet->type == PacketType::kVictimWriteReq ||
         packet->type == PacketType::kMemWriteReq);
  tlm::tlm_generic_payload *trans = packet->GetTlmGp();
  success = source_.req_port->nb_write(trans);
  assert(success);
  MarkProgress("mem_request_issue");
  LogPacketEvent(cache_log::Category::kMem, "request_issue", "sent",
                 request_source, packet);

  auto [_, inserted] = mem_inflight_packets_.emplace(trans, packet);
  assert(inserted);

  // Note that the packet cannot be released here because we need to keep track
  // of inflight memory requests for correct handling of memory responses. The
  // packet will be released by the refill path or write-buffer response path.
}

LV_BINDING(simtix, Cache)
    .constructor(
        [](const char *name, const Param &param) {
          return std::make_shared<Cache>(name, param);
        },
        lv::params("name", "param"),
        lv::doc("Create a cache module with the specified parameters"))
    .property("sink", &Cache::sink, lv::doc("Core-side TLM sink"))
    .property("source", &Cache::source, lv::doc("Memory-side TLM source"))
    .property("port", &Cache::port, lv::doc("Core-side request port"))
    .property("mmio_port", &Cache::mmio_port, lv::doc("MMIO target port"))
    .property("target", &Cache::target, &Cache::set_target,
              lv::doc("Memory-side target"))
    .property("stats", &Cache::stats, lv::doc("Statistics group"))
    .property("clock", &Cache::set_clock, lv::doc("SystemC clock"));

}  // namespace simtix::cache
