/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <liblv/common/tlm_sink.h>
#include <liblv/common/tlm_source.h>
#include <liblv/statistics.h>
#include <systemc.h>
#include <tlm_core/tlm_2/tlm_generic_payload/tlm_gp.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "cache/cache_log.h"
#include "cache/data_array.h"
#include "cache/mem_payload_pool.h"
#include "cache/mshr_file.h"
#include "cache/packet_lifecycle_intf.h"
#include "cache/packet_pool.h"
#include "cache/param.h"
#include "cache/tag_array.h"
#include "cache/victim_buffer.h"
#include "cache/write_buffer.h"

namespace simtix::cache {

class Cache : public sc_module, public PacketLifecycleIntf {
  friend class CacheModuleTester;

 public:
  sc_in<bool> SC_NAMED(clock);

  Cache(const sc_module_name &name, const Param &p);

  void set_clock(sc_clock *clk) { clock.bind(*clk); }

  lv::TlmSink *sink() { return &sink_; }

  lv::TlmSink *mmio_sink() { return &mmio_sink_; }

  lv::TlmSource *source() { return &source_; }

  auto port() const { return &sink_.port; }

  auto mmio_port() const { return &mmio_sink_.port; }

  lv::TlmSource::Target *target() const { return source_.target(); }

  void set_target(lv::TlmSource::Target *target) { source_.set_target(target); }

  lv::stats::Group *stats() const { return &stats_; }

  /**
   * @brief Allocate a cache packet that borrows a core-owned TLM payload.
   *
   * @param payload Borrowed core-side TLM payload.
   * @return Packet wrapper for the borrowed payload.
   */
  Packet *AllocatePacketWithCorePayload(tlm::tlm_generic_payload *payload) {
    return packet_pool_.Acquire(payload);
  }

  /**
   * @brief Allocate a packet with cache-owned memory payload storage.
   *
   * @return Packet wrapper with cache-owned payload storage.
   */
  Packet *AllocatePacketWithOwnedPayload() override {
    auto mem_payload = mem_payload_pool_.Acquire();
    return packet_pool_.Acquire(mem_payload);
  }

  /**
   * @brief Return a packet and any cache-owned payload to their pools.
   *
   * @param packet Packet previously allocated by this cache.
   */
  void ReleasePacket(Packet *packet) override {
    if (packet->payload_type() == PayloadType::kCorePayload) {
      packet_pool_.Release(packet);
    } else {
      mem_payload_pool_.Release(packet->GetCacheOwnedPayload());
      packet_pool_.Release(packet);
    }
  }

 private:
  enum class MmioOperation : uint64_t {
    kNop = 0,
    kFlush = 1,
    kInvalidate = 2,
  };

  struct MmioSequencer {
    enum class Phase {
      kIdle,
      kWaitPipelineDrain,
      kScan,
      kWaitWritebackDrain,
      kComplete,
    };

    bool IsBusy() const { return phase != Phase::kIdle; }
    void Reset() {
      phase = Phase::kIdle;
      operation = MmioOperation::kNop;
      full_cache = false;
      scan_address = 0;
      end_address = 0;
      scan_index = 0;
      entry_count = 0;
    }
    void Start(MmioOperation new_operation, uint64_t address, uint64_t size,
               size_t block_size, size_t new_entry_count) {
      assert(!IsBusy());
      operation = new_operation;
      full_cache = size == 0;
      scan_index = 0;
      entry_count = new_entry_count;

      if (full_cache) {
        scan_address = 0;
        end_address = 0;
      } else {
        scan_address = address - (address % block_size);
        const uint64_t end = address + size;
        const uint64_t rounded =
            end + ((block_size - (end % block_size)) % block_size);
        end_address = rounded;
      }

      phase = operation == MmioOperation::kNop ? Phase::kComplete
                                               : Phase::kWaitPipelineDrain;
    }

    Phase phase = Phase::kIdle;
    MmioOperation operation = MmioOperation::kNop;
    bool full_cache = false;
    uint64_t scan_address = 0;
    uint64_t end_address = 0;
    size_t scan_index = 0;
    size_t entry_count = 0;
  };

  struct AtomicSequencer {
    enum class Phase {
      kIdle,             ///< No atomic request is currently being sequenced.
      kWaitEscapeFence,  ///< Wait for same-line bypass/write-buffer hazards.
      kProbeOrMiss,      ///< Probe the tag array or enqueue a miss to MSHR.
      kWaitReplay,       ///< Wait for the atomic miss to refill and replay.
      kReadOld,          ///< Read the old value from the locked cache line.
      kWriteNewRespond,  ///< Write the new value, respond, and unlock the line.
    };

    bool IsBusy() const { return phase != Phase::kIdle; }
    void Reset() {
      phase = Phase::kIdle;
      packet = nullptr;
      line_address = 0;
      size = 0;
      location = {};
      old_value.fill(0);
      new_value.fill(0);
    }

    void Start(Packet *new_packet, uint64_t new_line_address) {
      assert(!IsBusy());
      assert(new_packet != nullptr);
      phase = Phase::kWaitEscapeFence;
      packet = new_packet;
      line_address = new_line_address;
    }

    Phase phase = Phase::kIdle;
    Packet *packet = nullptr;
    uint64_t line_address = 0;
    size_t size = 0;
    Location location;
    std::array<uint8_t, sizeof(uint64_t)> old_value{};
    std::array<uint8_t, sizeof(uint64_t)> new_value{};
  };

  struct WatchdogSnapshot {
    int core_req = 0;
    int core_resp = 0;
    int mmio_resp = 0;
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
    size_t line_escape_hazards = 0;
    bool mshr_pending = false;
    bool mshr_replay = false;
    bool write_buffer_pending = false;
    size_t write_buffer_pending_entries = 0;
    size_t write_buffer_inflight_entries = 0;
    bool victim_buffer_pending = false;
    size_t victim_buffer_reserved_entries = 0;
    size_t victim_buffer_committed_entries = 0;
    size_t victim_buffer_inflight_entries = 0;
    bool atomic_busy = false;
    bool mmio_busy = false;
    MmioSequencer::Phase mmio_phase = MmioSequencer::Phase::kIdle;
    uint64_t mmio_start = 0;
    uint64_t mmio_scan_address = 0;
    size_t mmio_scan_index = 0;
  };

  struct DeadlockWatchdog {
    bool enabled = false;
    bool fatal = false;
    uint64_t threshold_cycles = 1000000;
    uint64_t stalled_cycles = 0;
    bool tripped = false;
    uint64_t last_progress_epoch = 0;
    const char *last_progress_reason = "init";
    const char *last_block_reason = "none";
    bool last_external_wait = false;
    bool tick_internal_block_seen = false;
    bool tick_external_block_seen = false;
    WatchdogSnapshot last_snapshot;
  };

  void Tick();
  void AcceptCoreRequest();
  void AcceptMmioRequest();
  void AcceptMemResponse();
  void ValidateMemoryResponseOrFatal(
      const Packet *packet, const tlm::tlm_generic_payload *transaction) const;
  void AccessTagArrayStage();
  void AccessDataArrayStage();
  void SendCoreResponse();
  void SendMmioResponse();
  void SendMemRequest();

  // Helper
  void HandleMmioRequest(tlm::tlm_generic_payload *payload);
  uint64_t ReadMmioRegister(uint64_t offset) const;
  void WriteMmioRegister(uint64_t offset, uint64_t value);
  void StartMmioSequencer();
  void AdvanceMmioSequencer();
  bool HasPendingPipelineWork() const;
  bool HasPendingMmioWritebackWork() const;
  bool TryAdvanceMmioScan();
  bool TryIssueMmioFlushWriteback(const TagArray::DirtyLine &line);
  bool IsNonCacheableRequest(const tlm::tlm_generic_payload &payload) const;
  bool IsAtomicRequest(const tlm::tlm_generic_payload &payload) const;
  bool IsValidAtomicRequest(const tlm::tlm_generic_payload &payload) const;
  Packet *AllocateWriteBufferPacketFrom(const Packet *source_packet);
  Packet *AllocateVictimPacketFrom(const Packet *refill_packet);
  MshrFile::AcceptStatus TryAcceptReadMiss(Packet *packet);
  Packet *SelectTagArrayPacket(Packet *mem_resp_packet, bool *selected_refill);
  uint64_t ToLineAddress(uint64_t address) const;
  bool TryRejectInvalidAtomicRequest(Packet *packet);
  bool TryStartAtomicSequencerFromCoreQueue(Packet *packet);
  bool TryAdvanceAtomicTagStage();
  bool TryProbeAtomicSequencer();
  bool TryConsumeAtomicReplay();
  bool TryAdvanceAtomicDataStage();
  bool TryProcessDataArrayMiss(Packet *packet);
  bool TryProcessDataArrayHit(Packet *packet);
  bool HasLineEscapeHazard(uint64_t line_address) const;
  bool ShouldStallForLineEscapeHazard(const Packet *packet) const;
  bool CoreBypassCanEnterMemoryRequestQueue();
  bool CoreInputCanFillEmptyRequestQueue();
  bool WriteBufferInputBackpressured() const;
  bool ShouldPrioritizeWriteBufferMemRequest() const;
  bool ShouldDeferMshrMemRequestForWriteBuffer() const;
  void IncrementLineEscapeHazardForLine(uint64_t line_address);
  void DecrementLineEscapeHazardForLine(uint64_t line_address);
  void IncrementLineEscapeHazard(const Packet *packet);
  void DecrementLineEscapeHazard(const Packet *packet);
  void LogPacketEvent(cache_log::Category category, const char *event,
                      const char *reason, const char *source,
                      const Packet *packet) const;
  void LogPacketTraceEvent(cache_log::Category category, const char *event,
                           const char *reason, const char *source,
                           const Packet *packet) const;
  void LogQueueSnapshot(cache_log::Category category, const char *event,
                        const char *reason) const;
  void LogRepeatedStall(const char *reason, uint64_t *counter);
  void BeginDeadlockWatchdogTick();
  void MarkProgress(const char *reason);
  void MarkBlockReason(const char *reason, bool external_wait);
  void CheckDeadlockWatchdog();
  WatchdogSnapshot MakeWatchdogSnapshot() const;
  bool HasWatchdogPendingWork(const WatchdogSnapshot &snapshot) const;
  static bool WatchdogSnapshotsEqual(const WatchdogSnapshot &lhs,
                                     const WatchdogSnapshot &rhs);
  static const char *MmioOperationName(MmioOperation operation);
  static const char *MmioPhaseName(MmioSequencer::Phase phase);

  // Configs
  const Param config_;
  const std::vector<NonCacheableEntry> non_cacheable_regions_;

  // Ports
  lv::TlmSink sink_;       // core side port
  lv::TlmSink mmio_sink_;  // mmio port
  lv::TlmSource source_;   // memory side port
  mutable lv::stats::Group stats_;

  // Cache packet
  PacketPool packet_pool_;
  MemPayloadPool mem_payload_pool_;
  std::unordered_map<tlm::tlm_generic_payload *, Packet *>
      mem_inflight_packets_;

  // Cache submodules
  TagArray tag_array_;
  DataArray data_array_;
  MshrFile mshr_file_;
  WriteBuffer write_buffer_;
  VictimBuffer victim_buffer_;

  // Pipelined queues
  tlm::tlm_fifo<Packet *> core_req_queue_;
  tlm::tlm_fifo<Packet *> core_resp_queue_;
  tlm::tlm_fifo<tlm::tlm_generic_payload *> mmio_resp_queue_;

  tlm::tlm_fifo<Packet *> tag_array_resp_queue_;

  tlm::tlm_fifo<Packet *> mshr_file_mem_req_queue_;
  tlm::tlm_fifo<MshrFile::RefillNotify> mshr_file_refill_notify_queue_;
  tlm::tlm_fifo<Packet *> mshr_file_replay_queue_;

  tlm::tlm_fifo<Packet *> write_buffer_mem_req_queue_;
  tlm::tlm_fifo<Packet *> write_buffer_mem_req_out_queue_;
  tlm::tlm_fifo<Packet *> write_buffer_mem_resp_queue_;

  tlm::tlm_fifo<Packet *> victim_buffer_mem_req_out_queue_;
  tlm::tlm_fifo<Packet *> victim_buffer_mem_resp_queue_;

  tlm::tlm_fifo<Packet *> bypass_req_queue_;
  tlm::tlm_fifo<Packet *> mem_resp_queue_;

  AtomicSequencer atomic_sequencer_;

  // MMIO
  MmioSequencer mmio_sequencer_;
  // MMIO control registers
  uint64_t mmio_start_ = 0;
  uint64_t mmio_addr_ = 0;
  uint64_t mmio_size_ = 0;
  MmioOperation mmio_op_ = MmioOperation::kNop;
  // Log counters for MMIO sequencer phases
  // Tracks pipeline drain wait cycles
  uint64_t mmio_pipeline_wait_log_count_ = 0;
  // Tracks writeback drain wait cycles
  uint64_t mmio_writeback_wait_log_count_ = 0;

  uint64_t progress_epoch_ = 0;
  DeadlockWatchdog deadlock_watchdog_;
  bool defer_mshr_mem_req_for_core_input_ = false;
  bool defer_mshr_mem_req_for_write_buffer_ = false;
  size_t defer_tag_arbitration_for_refill_replay_cycles_ = 0;
  std::unordered_map<uint64_t, size_t> line_escape_hazards_;
};

}  // namespace simtix::cache
