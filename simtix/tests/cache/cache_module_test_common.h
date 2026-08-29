// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include <systemc.h>
#include <tlm.h>
#include <tlm_core/tlm_2/tlm_generic_payload/tlm_gp.h>
#include <tlm_utils/simple_initiator_socket.h>
#include <tlm_utils/simple_target_socket.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <exception>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

#include "cache/cache.h"
#include "cache/param.h"
#include "cache/policies.h"
#include "tlm_extensions/atomic_extension.h"

namespace {

using simtix::cache::Cache;
using simtix::cache::Param;
using simtix::cache::ReplacementPolicy;
using simtix::cache::WriteHitPolicy;
using simtix::cache::WriteMissPolicy;

Param MakeParam(
    WriteMissPolicy write_miss_policy = WriteMissPolicy::kWriteAllocate,
    WriteHitPolicy write_hit_policy = WriteHitPolicy::kWriteThrough,
    size_t ways = 1, size_t mshr_entries = 2, size_t mshr_subentries = 1,
    size_t write_buffer_entries = 2, size_t pipeline_queue_size = 1) {
  Param param;
  param.cache_size_bytes = 64;
  param.block_size_bytes = 16;
  param.ways = ways;
  param.replacement_policy = ReplacementPolicy::kFIFO;
  param.write_hit_policy = write_hit_policy;
  param.write_miss_policy = write_miss_policy;
  param.mshr_entries = mshr_entries;
  param.mshr_subentries = mshr_subentries;
  param.write_buffer_entries = write_buffer_entries;
  param.pipeline_queue_size = pipeline_queue_size;
  return param;
}

Param MakeNonCacheableParam() {
  Param param = MakeParam();
  simtix::cache::NonCacheableEntry region;
  region.addr = 0x20;
  region.size = 0x10;
  param.non_cacheable_regions.push_back(region);
  return param;
}

Param MakeAtomicParam(bool atomic_linearization) {
  Param param = MakeParam(WriteMissPolicy::kWriteAllocate,
                          WriteHitPolicy::kWriteBack, 1, 2, 2, 4, 2);
  param.atomic_linearization = atomic_linearization;
  return param;
}

Param MakeAtomicNonCacheableParam() {
  Param param = MakeAtomicParam(true);
  simtix::cache::NonCacheableEntry region;
  region.addr = 0x40;
  region.size = 0x10;
  param.non_cacheable_regions.push_back(region);
  return param;
}

std::vector<uint8_t> Sequence(size_t size, uint8_t first) {
  std::vector<uint8_t> data(size);
  for (size_t i = 0; i < data.size(); ++i) {
    data[i] = static_cast<uint8_t>(first + i);
  }
  return data;
}

std::vector<uint8_t> PayloadData(tlm::tlm_generic_payload *gp) {
  return std::vector<uint8_t>(gp->get_data_ptr(),
                              gp->get_data_ptr() + gp->get_data_length());
}

std::vector<uint8_t> Slice(const std::vector<uint8_t> &data, size_t offset,
                           size_t size) {
  return std::vector<uint8_t>(data.begin() + offset,
                              data.begin() + offset + size);
}

std::vector<uint8_t> WithPatch(std::vector<uint8_t> data, size_t offset,
                               const std::vector<uint8_t> &patch) {
  std::copy(patch.begin(), patch.end(), data.begin() + offset);
  return data;
}

std::vector<uint8_t> LineWriteData(size_t block_size, size_t offset,
                                   const std::vector<uint8_t> &data) {
  std::vector<uint8_t> line(block_size, 0);
  std::copy(data.begin(), data.end(), line.begin() + offset);
  return line;
}

std::vector<uint8_t> LineWriteMask(size_t block_size, size_t offset,
                                   size_t size) {
  std::vector<uint8_t> mask(block_size, TLM_BYTE_DISABLED);
  std::fill(mask.begin() + offset, mask.begin() + offset + size,
            TLM_BYTE_ENABLED);
  return mask;
}

std::vector<uint8_t> AllEnabledMask(size_t size) {
  return std::vector<uint8_t>(size, TLM_BYTE_ENABLED);
}

std::vector<uint8_t> U32Bytes(uint32_t value) {
  std::vector<uint8_t> bytes(sizeof(value));
  std::memcpy(bytes.data(), &value, sizeof(value));
  return bytes;
}

std::vector<uint8_t> U64Bytes(uint64_t value) {
  std::vector<uint8_t> bytes(sizeof(value));
  for (size_t i = 0; i < bytes.size(); ++i) {
    bytes[i] = static_cast<uint8_t>((value >> (i * 8)) & 0xFF);
  }
  return bytes;
}

uint32_t LoadU32(const std::vector<uint8_t> &bytes, size_t offset = 0) {
  uint32_t value = 0;
  std::memcpy(&value, bytes.data() + offset, sizeof(value));
  return value;
}

uint64_t LoadU64(const std::vector<uint8_t> &bytes, size_t offset = 0) {
  uint64_t value = 0;
  for (size_t i = 0; i < sizeof(value); ++i) {
    value |= static_cast<uint64_t>(bytes.at(offset + i)) << (i * 8);
  }
  return value;
}

constexpr uint64_t kMmioStartOffset = 0x0;
constexpr uint64_t kMmioAddrOffset = 0x8;
constexpr uint64_t kMmioSizeOffset = 0x10;
constexpr uint64_t kMmioOpOffset = 0x18;
constexpr uint64_t kMmioFlushOp = 1;
constexpr uint64_t kMmioInvalidateOp = 2;

std::vector<uint8_t> StoreU32At(std::vector<uint8_t> data, size_t offset,
                                uint32_t value) {
  std::memcpy(data.data() + offset, &value, sizeof(value));
  return data;
}

}  // namespace

namespace simtix::cache {

class CacheModuleTester {
 public:
  struct QueueSnapshot {
    int core_req = 0;
    int core_resp = 0;
    int tag_array_resp = 0;
    int mshr_file_mem_req = 0;
    int mshr_file_refill_notify = 0;
    int mshr_file_replay = 0;
    int write_buffer_mem_req = 0;
    int write_buffer_mem_req_out = 0;
    int write_buffer_mem_resp = 0;
    int bypass_req = 0;
    int mem_resp = 0;
    size_t mem_inflight_packets = 0;
  };

  struct TagLineSnapshot {
    Location location;
    uint64_t address = 0;
    bool dirty = false;
    bool locked = false;
    SC_TIME_DT access_timestamp = 0;
  };

  struct CachedLineSnapshot {
    TagLineSnapshot tag;
    std::vector<uint8_t> data;
  };

  struct MshrSnapshot {
    size_t invalid_entries = 0;
    size_t pending_refill_entries = 0;
    size_t ready_to_replay_entries = 0;
    size_t replaying_entries = 0;
    size_t tracked_sub_entries = 0;
    size_t ready_to_replay_queue_size = 0;
    bool has_active_replay = false;
  };

  struct WriteBufferSnapshot {
    size_t pending_entries = 0;
    size_t inflight_entries = 0;
    size_t capacity = 0;
  };

  explicit CacheModuleTester(Cache &dut) : dut_(dut) {}

  void StartMmioOperation(uint64_t operation, uint64_t address, uint64_t size) {
    dut_.mmio_op_ = static_cast<Cache::MmioOperation>(operation);
    dut_.mmio_addr_ = address;
    dut_.mmio_size_ = size;
    dut_.StartMmioSequencer();
  }

  void ArmWatchdog(uint64_t threshold_cycles) {
    auto &watchdog = dut_.deadlock_watchdog_;
    watchdog.enabled = true;
    watchdog.fatal = false;
    watchdog.threshold_cycles = threshold_cycles;
    watchdog.stalled_cycles = 0;
    watchdog.tripped = false;
    watchdog.last_progress_epoch = dut_.progress_epoch_;
    watchdog.last_progress_reason = "test_arm";
    watchdog.last_block_reason = "none";
    watchdog.last_external_wait = false;
    watchdog.tick_internal_block_seen = false;
    watchdog.tick_external_block_seen = false;
    watchdog.last_snapshot = dut_.MakeWatchdogSnapshot();
  }

  void InjectWatchdogInternalStall(uint64_t line_address) {
    dut_.line_escape_hazards_[line_address] = 1;
    dut_.deadlock_watchdog_.last_snapshot = dut_.MakeWatchdogSnapshot();
    dut_.MarkBlockReason("test_internal_stall", false);
  }

  bool WatchdogTripped() const { return dut_.deadlock_watchdog_.tripped; }

  uint64_t WatchdogStalledCycles() const {
    return dut_.deadlock_watchdog_.stalled_cycles;
  }

  QueueSnapshot Queues() const {
    return QueueSnapshot{
        .core_req = dut_.core_req_queue_.used(),
        .core_resp = dut_.core_resp_queue_.used(),
        .tag_array_resp = dut_.tag_array_resp_queue_.used(),
        .mshr_file_mem_req = dut_.mshr_file_mem_req_queue_.used(),
        .mshr_file_refill_notify = dut_.mshr_file_refill_notify_queue_.used(),
        .mshr_file_replay = dut_.mshr_file_replay_queue_.used(),
        .write_buffer_mem_req = dut_.write_buffer_mem_req_queue_.used(),
        .write_buffer_mem_req_out = dut_.write_buffer_mem_req_out_queue_.used(),
        .write_buffer_mem_resp = dut_.write_buffer_mem_resp_queue_.used(),
        .bypass_req = dut_.bypass_req_queue_.used(),
        .mem_resp = dut_.mem_resp_queue_.used(),
        .mem_inflight_packets = dut_.mem_inflight_packets_.size(),
    };
  }

  std::optional<CachedLineSnapshot> CachedLine(uint64_t address) const {
    const TagArray &tag_array = dut_.tag_array_;
    const uint64_t line_address = address / tag_array.config_.block_size_bytes;
    const size_t set = line_address % tag_array.num_sets_;
    const size_t set_base = set * tag_array.config_.ways;

    for (size_t way = 0; way < tag_array.config_.ways; ++way) {
      const TagArray::TagEntry &entry = tag_array.tag_array_[set_base + way];
      if (entry.valid && entry.tag == line_address) {
        const Location location{.set = set, .way = way};
        return CachedLineSnapshot{
            .tag =
                TagLineSnapshot{
                    .location = location,
                    .address = entry.tag * tag_array.config_.block_size_bytes,
                    .dirty = entry.dirty,
                    .locked = entry.locked,
                    .access_timestamp = entry.access_timestamp,
                },
            .data = DataBlock(dut_.data_array_, location),
        };
      }
    }

    return std::nullopt;
  }

  MshrSnapshot Mshr() const { return InspectMshrCore(dut_.mshr_file_.core_); }

  WriteBufferSnapshot WriteBufferState() const {
    const WriteBuffer &write_buffer = dut_.write_buffer_;
    return WriteBufferSnapshot{
        .pending_entries = write_buffer.pending_entries_.size(),
        .inflight_entries = write_buffer.inflight_map_.size(),
        .capacity = write_buffer.config_.write_buffer_entries,
    };
  }

  void FillCoreResponseQueueWithTestPackets(uint64_t address) {
    while (dut_.core_resp_queue_.nb_can_put()) {
      Packet *packet = AllocateTestWritePacket(address);
      packet->type = PacketType::kCoreReq;
      const bool put = dut_.core_resp_queue_.nb_put(packet);
      assert(put);
    }
  }

  void ClearCoreResponseQueue() {
    Packet *packet = nullptr;
    while (dut_.core_resp_queue_.nb_get(packet)) {
      dut_.ReleasePacket(packet);
    }
  }

  void FillWriteBufferInputQueueWithTestPackets(uint64_t address) {
    while (dut_.write_buffer_mem_req_queue_.nb_can_put()) {
      Packet *packet = AllocateTestWritePacket(address);
      const bool put = dut_.write_buffer_mem_req_queue_.nb_put(packet);
      assert(put);
    }
  }

  void ClearWriteBufferInputQueue() {
    Packet *packet = nullptr;
    while (dut_.write_buffer_mem_req_queue_.nb_get(packet)) {
      dut_.ReleasePacket(packet);
    }
  }

  void AcceptCoreRequest() { dut_.AcceptCoreRequest(); }
  void AcceptMemResponse() { dut_.AcceptMemResponse(); }
  bool HasMemoryResponse() const {
    return dut_.source_.resp_port->num_available() > 0;
  }
  void AccessDataArrayStage() { dut_.AccessDataArrayStage(); }
  void AccessTagArrayStage() { dut_.AccessTagArrayStage(); }
  void SendMemRequest() { dut_.SendMemRequest(); }

 private:
  Packet *AllocateTestWritePacket(uint64_t address) {
    Packet *packet = dut_.AllocatePacketWithOwnedPayload();
    packet->GetCacheOwnedPayload()->InitLineWrite(
        address, dut_.config_.block_size_bytes);
    packet->type = PacketType::kMemWriteReq;
    return packet;
  }

  static std::vector<uint8_t> DataBlock(const DataArray &data_array,
                                        Location loc) {
    const size_t offset = (loc.set * data_array.config_.ways + loc.way) *
                          data_array.config_.block_size_bytes;
    const auto begin = data_array.data_array_.begin() + offset;
    return std::vector<uint8_t>(begin,
                                begin + data_array.config_.block_size_bytes);
  }

  static MshrSnapshot InspectMshrCore(const MshrCore &mshr) {
    MshrSnapshot snapshot;
    snapshot.ready_to_replay_queue_size = mshr.ready_to_replay_.size();
    snapshot.has_active_replay = mshr.replaying_mshr_index_.has_value();

    for (const MshrCore::MshrEntry &entry : mshr.mshr_entries_) {
      snapshot.tracked_sub_entries += entry.sub_entries.size();
      switch (entry.state) {
        case MshrCore::MshrEntry::State::kInvalid:
          ++snapshot.invalid_entries;
          break;
        case MshrCore::MshrEntry::State::kPendingRefill:
          ++snapshot.pending_refill_entries;
          break;
        case MshrCore::MshrEntry::State::kReadyToReplay:
          ++snapshot.ready_to_replay_entries;
          break;
        case MshrCore::MshrEntry::State::kReplaying:
          ++snapshot.replaying_entries;
          break;
      }
    }

    return snapshot;
  }

  Cache &dut_;
};

}  // namespace simtix::cache

namespace {

using simtix::cache::CacheModuleTester;

class CoreEndpoint : public sc_core::sc_module {
 public:
  tlm_utils::simple_initiator_socket<CoreEndpoint> socket;

  explicit CoreEndpoint(sc_core::sc_module_name name)
      : sc_core::sc_module(name), socket("socket") {
    socket.register_nb_transport_bw(this, &CoreEndpoint::NbTransportBw);

    SC_METHOD(SendEndResp);
    sensitive << end_resp_event_;
    dont_initialize();
  }

  ~CoreEndpoint() override {
    for (auto &payload : payloads_) {
      payload->clear_extension<simtix::AtomicExtension>();
    }
  }

  tlm::tlm_generic_payload *SendRead(uint64_t address, size_t size) {
    return Send(address, tlm::TLM_READ_COMMAND, std::vector<uint8_t>(size, 0));
  }

  tlm::tlm_generic_payload *SendWrite(uint64_t address,
                                      std::vector<uint8_t> data) {
    return Send(address, tlm::TLM_WRITE_COMMAND, std::move(data));
  }

  tlm::tlm_generic_payload *SendAtomic(uint64_t address,
                                       std::vector<uint8_t> operand,
                                       simtix::AtomicExtension::Op op) {
    return Send(address, tlm::TLM_READ_COMMAND, std::move(operand), op);
  }

  size_t response_count() const { return responses_.size(); }

  tlm::tlm_generic_payload *response(size_t index) const {
    return responses_.at(index);
  }

  bool HasResponse(tlm::tlm_generic_payload *gp) const {
    for (auto *response : responses_) {
      if (response == gp) {
        return response->get_response_status() == tlm::TLM_OK_RESPONSE;
      }
    }
    return false;
  }

 private:
  tlm::tlm_generic_payload *Send(
      uint64_t address, tlm::tlm_command command, std::vector<uint8_t> data,
      std::optional<simtix::AtomicExtension::Op> atomic_op = std::nullopt) {
    auto payload = std::make_unique<tlm::tlm_generic_payload>();
    auto owned_data = std::make_unique<std::vector<uint8_t>>(std::move(data));
    payload->set_address(address);
    payload->set_command(command);
    payload->set_data_ptr(owned_data->data());
    payload->set_data_length(owned_data->size());
    payload->set_streaming_width(owned_data->size());
    payload->set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);

    if (atomic_op.has_value()) {
      auto ext = std::make_unique<simtix::AtomicExtension>();
      ext->op = *atomic_op;
      payload->set_extension(ext.get());
      atomic_exts_.push_back(std::move(ext));
    }

    tlm::tlm_generic_payload *gp = payload.get();
    owned_data_.push_back(std::move(owned_data));
    payloads_.push_back(std::move(payload));

    tlm::tlm_phase phase = tlm::BEGIN_REQ;
    sc_core::sc_time delay = sc_core::SC_ZERO_TIME;
    socket->nb_transport_fw(*gp, phase, delay);
    return gp;
  }

  tlm::tlm_sync_enum NbTransportBw(tlm::tlm_generic_payload &trans,
                                   tlm::tlm_phase &phase,
                                   sc_core::sc_time &delay) {
    (void)delay;
    if (phase == tlm::END_REQ) {
      return tlm::TLM_ACCEPTED;
    }
    if (phase == tlm::BEGIN_RESP) {
      responses_.push_back(&trans);
      pending_end_resp_.push_back(&trans);
      end_resp_event_.notify(sc_core::SC_ZERO_TIME);
      return tlm::TLM_ACCEPTED;
    }

    SC_REPORT_ERROR("CacheModuleTest", "core received an unexpected phase");
    return tlm::TLM_COMPLETED;
  }

  void SendEndResp() {
    while (!pending_end_resp_.empty()) {
      tlm::tlm_generic_payload *trans = pending_end_resp_.front();
      pending_end_resp_.pop_front();

      tlm::tlm_phase phase = tlm::END_RESP;
      sc_core::sc_time delay = sc_core::SC_ZERO_TIME;
      socket->nb_transport_fw(*trans, phase, delay);
    }
  }

  std::vector<std::unique_ptr<simtix::AtomicExtension>> atomic_exts_;
  std::vector<std::unique_ptr<tlm::tlm_generic_payload>> payloads_;
  std::vector<std::unique_ptr<std::vector<uint8_t>>> owned_data_;
  std::vector<tlm::tlm_generic_payload *> responses_;
  std::deque<tlm::tlm_generic_payload *> pending_end_resp_;
  sc_core::sc_event end_resp_event_;
};

class MemoryEndpoint : public sc_core::sc_module {
 public:
  tlm_utils::simple_target_socket<MemoryEndpoint> socket;

  explicit MemoryEndpoint(sc_core::sc_module_name name)
      : sc_core::sc_module(name), socket("socket") {
    socket.register_nb_transport_fw(this, &MemoryEndpoint::NbTransportFw);

    SC_METHOD(SendEndReq);
    sensitive << end_req_event_;
    dont_initialize();
  }

  size_t request_count() const { return requests_.size(); }

  tlm::tlm_generic_payload *request(size_t index) const {
    return requests_.at(index);
  }

  std::vector<uint8_t> RequestData(size_t index) const {
    return PayloadData(request(index));
  }

  bool RequestHasAtomicExtension(size_t index) const {
    simtix::AtomicExtension *ext = nullptr;
    request(index)->get_extension(ext);
    return ext != nullptr;
  }

  void RespondAt(size_t index, std::vector<uint8_t> data = {}) {
    RespondAtWithStatus(index, tlm::TLM_OK_RESPONSE, std::move(data));
  }

  void Respond(tlm::tlm_generic_payload *gp, std::vector<uint8_t> data = {}) {
    RespondWithStatus(gp, tlm::TLM_OK_RESPONSE, std::move(data));
  }

  void RespondAtWithStatus(size_t index, tlm::tlm_response_status status,
                           std::vector<uint8_t> data = {}) {
    RespondWithStatus(request(index), status, std::move(data));
  }

  void RespondWithStatus(tlm::tlm_generic_payload *gp,
                         tlm::tlm_response_status status,
                         std::vector<uint8_t> data = {}) {
    if (status == tlm::TLM_OK_RESPONSE && gp->is_read() && !data.empty()) {
      const size_t size = std::min<size_t>(gp->get_data_length(), data.size());
      std::copy(data.begin(), data.begin() + size, gp->get_data_ptr());
    }
    gp->set_response_status(status);

    tlm::tlm_phase phase = tlm::BEGIN_RESP;
    sc_core::sc_time delay = sc_core::SC_ZERO_TIME;
    socket->nb_transport_bw(*gp, phase, delay);
  }

 private:
  tlm::tlm_sync_enum NbTransportFw(tlm::tlm_generic_payload &trans,
                                   tlm::tlm_phase &phase,
                                   sc_core::sc_time &delay) {
    (void)delay;
    if (phase == tlm::BEGIN_REQ) {
      requests_.push_back(&trans);
      pending_end_req_.push_back(&trans);
      end_req_event_.notify(sc_core::SC_ZERO_TIME);
      return tlm::TLM_ACCEPTED;
    }
    if (phase == tlm::END_RESP) {
      return tlm::TLM_ACCEPTED;
    }

    SC_REPORT_ERROR("CacheModuleTest", "memory received an unexpected phase");
    return tlm::TLM_COMPLETED;
  }

  void SendEndReq() {
    while (!pending_end_req_.empty()) {
      tlm::tlm_generic_payload *trans = pending_end_req_.front();
      pending_end_req_.pop_front();

      tlm::tlm_phase phase = tlm::END_REQ;
      sc_core::sc_time delay = sc_core::SC_ZERO_TIME;
      socket->nb_transport_bw(*trans, phase, delay);
    }
  }

  std::vector<tlm::tlm_generic_payload *> requests_;
  std::deque<tlm::tlm_generic_payload *> pending_end_req_;
  sc_core::sc_event end_req_event_;
};

class CacheBench : public sc_core::sc_module {
 public:
  CacheBench(sc_core::sc_module_name name, Param param)
      : sc_core::sc_module(name),
        clock_("clock"),
        core_("core"),
        mmio_("mmio"),
        memory_("memory"),
        cache_("cache", param) {
    cache_.clock(clock_);
    core_.socket.bind(cache_.sink()->port);
    mmio_.socket.bind(cache_.mmio_sink()->port);
    cache_.set_target(&memory_.socket);
  }

  void AdvanceCycle() {
    clock_.write(true);
    wait(sc_core::SC_ZERO_TIME);
    clock_.write(false);
    wait(sc_core::sc_time(1, sc_core::SC_NS));
  }

  bool WaitForMemoryRequests(size_t count, size_t max_cycles = 16) {
    for (size_t cycle = 0; cycle < max_cycles; ++cycle) {
      if (memory_.request_count() >= count) {
        return true;
      }
      AdvanceCycle();
    }
    return memory_.request_count() >= count;
  }

  bool WaitForCoreResponses(size_t count, size_t max_cycles = 16) {
    for (size_t cycle = 0; cycle < max_cycles; ++cycle) {
      if (core_.response_count() >= count) {
        return true;
      }
      AdvanceCycle();
    }
    return core_.response_count() >= count;
  }

  bool WaitForMmioResponses(size_t count, size_t max_cycles = 16) {
    for (size_t cycle = 0; cycle < max_cycles; ++cycle) {
      if (mmio_.response_count() >= count) {
        return true;
      }
      AdvanceCycle();
    }
    return mmio_.response_count() >= count;
  }

  sc_core::sc_signal<bool> clock_;
  CoreEndpoint core_;
  CoreEndpoint mmio_;
  MemoryEndpoint memory_;
  Cache cache_;
};

class CacheModuleTestRunnerBase : public sc_core::sc_module {
 public:
  explicit CacheModuleTestRunnerBase(sc_core::sc_module_name name)
      : sc_core::sc_module(name) {}

  bool failed() const { return failed_; }

 protected:
  template <typename TestFn>
  void RunBench(std::string_view bench_name, TestFn test_fn) {
    current_bench_ = bench_name;
    try {
      test_fn();
    } catch (const sc_core::sc_report &report) {
      FailCurrentBench(report.what());
    } catch (const std::exception &ex) {
      FailCurrentBench(ex.what());
    } catch (...) {
      FailCurrentBench("unknown exception");
    }
    current_bench_ = {};
  }

  void FailCurrentBench(std::string_view message) {
    failed_ = true;
    std::cerr << "Cache module test failed in ";
    if (current_bench_.empty()) {
      std::cerr << "<unknown bench>";
    } else {
      std::cerr << current_bench_;
    }
    std::cerr << ": " << message << '\n';
  }

  void ExpectReadRequest(CacheBench &bench, size_t index, uint64_t address,
                         size_t size, std::string_view message) {
    Expect(bench.memory_.request_count() > index, message);
    if (bench.memory_.request_count() <= index) {
      return;
    }

    tlm::tlm_generic_payload *request = bench.memory_.request(index);
    Expect(request->is_read(), message);
    Expect(request->get_address() == address, message);
    Expect(request->get_data_length() == size, message);
  }

  void ExpectWriteRequest(CacheBench &bench, size_t index, uint64_t address,
                          const std::vector<uint8_t> &data,
                          std::string_view message,
                          const std::vector<uint8_t> &byte_enable = {}) {
    Expect(bench.memory_.request_count() > index, message);
    if (bench.memory_.request_count() <= index) {
      return;
    }

    tlm::tlm_generic_payload *request = bench.memory_.request(index);
    Expect(request->is_write(), message);
    Expect(request->get_address() == address, message);
    Expect(PayloadData(request) == data, message);
    if (!byte_enable.empty()) {
      Expect(request->get_byte_enable_length() == byte_enable.size(), message);
      Expect(request->get_byte_enable_ptr() != nullptr, message);
      if (request->get_byte_enable_ptr() != nullptr) {
        const std::vector<uint8_t> actual_byte_enable(
            request->get_byte_enable_ptr(),
            request->get_byte_enable_ptr() + request->get_byte_enable_length());
        Expect(actual_byte_enable == byte_enable, message);
      }
    }
  }

  tlm::tlm_generic_payload *FillLineWithRead(CacheBench &bench,
                                             uint64_t address,
                                             const std::vector<uint8_t> &block,
                                             size_t read_size = 4) {
    const size_t request_index = bench.memory_.request_count();
    const size_t response_count = bench.core_.response_count();
    auto *read = bench.core_.SendRead(address, read_size);

    Expect(bench.WaitForMemoryRequests(request_index + 1),
           "read miss reaches memory");
    ExpectReadRequest(bench, request_index, address - (address % block.size()),
                      block.size(),
                      "read miss issues a block-aligned memory request");

    bench.memory_.RespondAt(request_index, block);
    Expect(bench.WaitForCoreResponses(response_count + 1),
           "read miss refill returns to the core");
    Expect(bench.core_.HasResponse(read), "read miss response is observed");
    Expect(PayloadData(read) == Slice(block, address % block.size(), read_size),
           "read miss response contains refill data");
    return read;
  }

  void WriteMmio64(CacheBench &bench, uint64_t offset, uint64_t value) {
    const size_t response_count = bench.mmio_.response_count();
    auto *write = bench.mmio_.SendWrite(offset, U64Bytes(value));
    Expect(bench.WaitForMmioResponses(response_count + 1, 32),
           "MMIO write receives a response");
    Expect(bench.mmio_.HasResponse(write), "MMIO write response is OK");
  }

  uint64_t ReadMmio64(CacheBench &bench, uint64_t offset) {
    const size_t response_count = bench.mmio_.response_count();
    auto *read = bench.mmio_.SendRead(offset, sizeof(uint64_t));
    Expect(bench.WaitForMmioResponses(response_count + 1, 32),
           "MMIO read receives a response");
    Expect(bench.mmio_.HasResponse(read), "MMIO read response is OK");
    return LoadU64(PayloadData(read));
  }

  void StartMmioOperation(CacheBench &bench, uint64_t op, uint64_t address,
                          uint64_t size) {
    WriteMmio64(bench, kMmioAddrOffset, address);
    WriteMmio64(bench, kMmioSizeOffset, size);
    WriteMmio64(bench, kMmioOpOffset, op);
    WriteMmio64(bench, kMmioStartOffset, 1);
  }

  bool WaitForMmioIdle(CacheBench &bench, size_t max_polls = 64) {
    for (size_t poll = 0; poll < max_polls; ++poll) {
      if (ReadMmio64(bench, kMmioStartOffset) == 0) {
        return true;
      }
      bench.AdvanceCycle();
    }
    return ReadMmio64(bench, kMmioStartOffset) == 0;
  }

  std::vector<uint8_t> FillAndDirtyLine(CacheBench &bench, uint64_t address,
                                        const std::vector<uint8_t> &block,
                                        size_t write_offset,
                                        const std::vector<uint8_t> &patch) {
    FillLineWithRead(bench, address, block);
    const size_t response_count = bench.core_.response_count();
    auto *write = bench.core_.SendWrite(address + write_offset, patch);
    Expect(bench.WaitForCoreResponses(response_count + 1, 32),
           "write-back hit responds to the core");
    Expect(bench.core_.HasResponse(write),
           "write-back hit response is observed");
    return WithPatch(block, write_offset, patch);
  }

  void Expect(bool condition, std::string_view message) {
    if (!condition) {
      FailCurrentBench(message);
    }
  }

 private:
  bool failed_ = false;
  std::string_view current_bench_;
};

}  // namespace
