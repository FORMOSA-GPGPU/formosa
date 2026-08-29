// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include "cache_module_test_common.h"

namespace {

class CacheModuleHazardTestRunner : public CacheModuleTestRunnerBase {
 public:
  explicit CacheModuleHazardTestRunner(sc_core::sc_module_name name)
      : CacheModuleTestRunnerBase(name),
        mshr_deadlock_bench_(
            "mshr_deadlock_bench",
            MakeParam(WriteMissPolicy::kWriteAllocate,
                      WriteHitPolicy::kWriteThrough, 1, 1, 1, 2, 1)),
        write_resp_hol_bench_(
            "write_resp_hol_bench",
            MakeParam(WriteMissPolicy::kWriteNoAllocate,
                      WriteHitPolicy::kWriteThrough, 1, 2, 1, 1, 2)),
        write_buffer_pressure_bench_(
            "write_buffer_pressure_bench",
            MakeParam(WriteMissPolicy::kWriteNoAllocate,
                      WriteHitPolicy::kWriteThrough, 1, 2, 1, 1, 2)),
        dirty_refill_deadlock_bench_(
            "dirty_refill_deadlock_bench",
            [] {
              Param param =
                  MakeParam(WriteMissPolicy::kWriteAllocate,
                            WriteHitPolicy::kWriteBack, 1, 8, 1, 2, 2);
              param.cache_size_bytes = 128;
              param.victim_buffer_entries = 2;
              return param;
            }()),
        replay_hazard_refill_bench_(
            "replay_hazard_refill_bench",
            MakeParam(WriteMissPolicy::kWriteAllocate,
                      WriteHitPolicy::kWriteThrough, 1, 2, 2, 1, 2)),
        replay_visibility_bench_(
            "replay_visibility_bench",
            MakeParam(WriteMissPolicy::kWriteAllocate,
                      WriteHitPolicy::kWriteThrough, 1, 2, 1, 2, 2)),
        tag_priority_bench_(
            "tag_priority_bench",
            MakeParam(WriteMissPolicy::kWriteAllocate,
                      WriteHitPolicy::kWriteThrough, 1, 3, 1, 4)),
        write_hazard_wt_wa_bench_(
            "write_hazard_wt_wa_bench",
            MakeParam(WriteMissPolicy::kWriteAllocate,
                      WriteHitPolicy::kWriteThrough, 1, 2, 1, 4, 2)),
        write_hazard_wb_wa_bench_(
            "write_hazard_wb_wa_bench",
            MakeParam(WriteMissPolicy::kWriteAllocate,
                      WriteHitPolicy::kWriteBack, 1, 2, 1, 4, 2)),
        write_hazard_wt_wna_bench_(
            "write_hazard_wt_wna_bench",
            MakeParam(WriteMissPolicy::kWriteNoAllocate,
                      WriteHitPolicy::kWriteThrough, 1, 2, 1, 4, 2)),
        write_hazard_wb_wna_bench_(
            "write_hazard_wb_wna_bench",
            MakeParam(WriteMissPolicy::kWriteNoAllocate,
                      WriteHitPolicy::kWriteBack, 1, 2, 1, 4, 2)),
        wna_stalled_hazard_bench_(
            "wna_stalled_hazard_bench",
            MakeParam(WriteMissPolicy::kWriteNoAllocate,
                      WriteHitPolicy::kWriteThrough, 1, 2, 1, 4, 2)),
        dirty_victim_stalled_hazard_bench_(
            "dirty_victim_stalled_hazard_bench",
            MakeParam(WriteMissPolicy::kWriteAllocate,
                      WriteHitPolicy::kWriteBack, 1, 2, 1, 4, 2)) {
    SC_THREAD(Run);
  }

 private:
  void Run() {
    mshr_deadlock_bench_.clock_.write(false);
    write_resp_hol_bench_.clock_.write(false);
    write_buffer_pressure_bench_.clock_.write(false);
    dirty_refill_deadlock_bench_.clock_.write(false);
    replay_hazard_refill_bench_.clock_.write(false);
    replay_visibility_bench_.clock_.write(false);
    tag_priority_bench_.clock_.write(false);
    write_hazard_wt_wa_bench_.clock_.write(false);
    write_hazard_wb_wa_bench_.clock_.write(false);
    write_hazard_wt_wna_bench_.clock_.write(false);
    write_hazard_wb_wna_bench_.clock_.write(false);
    wna_stalled_hazard_bench_.clock_.write(false);
    dirty_victim_stalled_hazard_bench_.clock_.write(false);
    wait(sc_core::SC_ZERO_TIME);

    RunBench("mshr_deadlock_bench", [this] {
      TestMshrFullPipelineRefillDeadlock();
    });
    RunBench("write_resp_hol_bench", [this] {
      TestWriteResponseBypassesRefillHeadOfLine();
    });
    RunBench("write_buffer_pressure_bench", [this] {
      TestWriteBufferPressureDefersMshrReads();
    });
    RunBench("dirty_refill_deadlock_bench", [this] {
      TestDirtyRefillsMakeForwardProgressUnderWriteBufferPressure();
    });
    RunBench("replay_hazard_refill_bench", [this] {
      TestBlockedReplayPreventsYoungerRefillEviction();
    });
    RunBench("replay_visibility_bench", [this] {
      TestRefillWaitsForReplayVisibility();
    });
    RunBench("tag_priority_bench", [this] {
      TestTagStagePriority();
    });
    RunBench("write_hazard_wt_wa_bench", [this] {
      TestReadMissObservesOlderSameLineWrite(write_hazard_wt_wa_bench_,
                                             WriteMissPolicy::kWriteAllocate,
                                             WriteHitPolicy::kWriteThrough);
    });
    RunBench("write_hazard_wb_wa_bench", [this] {
      TestReadMissObservesOlderSameLineWrite(write_hazard_wb_wa_bench_,
                                             WriteMissPolicy::kWriteAllocate,
                                             WriteHitPolicy::kWriteBack);
    });
    RunBench("write_hazard_wt_wna_bench", [this] {
      TestReadMissObservesOlderSameLineWrite(write_hazard_wt_wna_bench_,
                                             WriteMissPolicy::kWriteNoAllocate,
                                             WriteHitPolicy::kWriteThrough);
    });
    RunBench("write_hazard_wb_wna_bench", [this] {
      TestReadMissObservesOlderSameLineWrite(write_hazard_wb_wna_bench_,
                                             WriteMissPolicy::kWriteNoAllocate,
                                             WriteHitPolicy::kWriteBack);
    });
    RunBench("wna_stalled_hazard_bench", [this] {
      TestWriteNoAllocateStalledMissFencesSameLineRead();
    });
    RunBench("dirty_victim_stalled_hazard_bench", [this] {
      TestDirtyVictimStalledWritebackFencesSameLineRead();
    });

    sc_core::sc_stop();
  }

  void TestMshrFullPipelineRefillDeadlock() {
    CacheModuleTester view(mshr_deadlock_bench_.cache_);
    const auto block_a = Sequence(16, 0x20);

    auto *first = mshr_deadlock_bench_.core_.SendRead(0x00, 4);
    Expect(mshr_deadlock_bench_.WaitForMemoryRequests(1),
           "deadlock regression first read miss reaches memory");
    ExpectReadRequest(mshr_deadlock_bench_, 0, 0x00, 16,
                      "deadlock regression first miss issues a block read");

    mshr_deadlock_bench_.core_.SendRead(0x10, 4);
    mshr_deadlock_bench_.core_.SendRead(0x20, 4);
    for (size_t i = 0; i < 8; ++i) {
      mshr_deadlock_bench_.AdvanceCycle();
    }

    auto mshr = view.Mshr();
    Expect(mshr.pending_refill_entries == 1,
           "deadlock regression keeps the only MSHR entry occupied");
    Expect(mshr_deadlock_bench_.memory_.request_count() == 1,
           "deadlock regression has no spare MSHR entry for later misses");

    mshr_deadlock_bench_.memory_.RespondAt(0, block_a);
    for (size_t i = 0; i < 32; ++i) {
      mshr_deadlock_bench_.AdvanceCycle();
    }

    Expect(mshr_deadlock_bench_.core_.HasResponse(first),
           "refill must make progress even when MSHR is full and later misses "
           "are already in the pipeline");
    Expect(mshr_deadlock_bench_.memory_.request_count() >= 2,
           "a later miss should issue after the first refill frees the MSHR");
    auto queues = view.Queues();
    Expect(queues.mem_resp == 0,
           "refill response should not remain stuck in the memory response "
           "queue");
  }

  void TestWriteResponseBypassesRefillHeadOfLine() {
    CacheModuleTester view(write_resp_hol_bench_.cache_);
    const auto refill_block = Sequence(16, 0x80);

    auto *read = write_resp_hol_bench_.core_.SendRead(0x40, 4);
    (void)read;
    write_resp_hol_bench_.core_.SendRead(0x60, 4);
    Expect(write_resp_hol_bench_.WaitForMemoryRequests(2, 32),
           "head-of-line regression read misses reach memory");
    ExpectReadRequest(write_resp_hol_bench_, 0, 0x40, 16,
                      "head-of-line regression first read miss is block "
                      "aligned");
    ExpectReadRequest(write_resp_hol_bench_, 1, 0x60, 16,
                      "head-of-line regression second read miss is block "
                      "aligned");

    write_resp_hol_bench_.core_.SendWrite(0x00, Sequence(4, 0x10));
    Expect(write_resp_hol_bench_.WaitForMemoryRequests(3, 32),
           "head-of-line regression first write reaches memory");
    ExpectWriteRequest(write_resp_hol_bench_, 2, 0x00,
                       LineWriteData(16, 0, Sequence(4, 0x10)),
                       "head-of-line regression first write is line masked",
                       LineWriteMask(16, 0, 4));

    write_resp_hol_bench_.core_.SendWrite(0x10, Sequence(4, 0x20));
    write_resp_hol_bench_.core_.SendWrite(0x20, Sequence(4, 0x30));
    bool write_buffer_input_full = false;
    for (size_t cycle = 0; cycle < 32; ++cycle) {
      const auto queues = view.Queues();
      if (queues.write_buffer_mem_req == 2) {
        write_buffer_input_full = true;
        break;
      }
      write_resp_hol_bench_.AdvanceCycle();
    }
    Expect(write_buffer_input_full,
           "head-of-line regression fills the cache-to-write-buffer queue");
    Expect(write_resp_hol_bench_.memory_.request_count() == 3,
           "queued writes wait for the only write-buffer entry");

    write_resp_hol_bench_.core_.SendWrite(0x30, Sequence(4, 0x40));
    write_resp_hol_bench_.core_.SendWrite(0x50, Sequence(4, 0x50));
    bool tag_pipeline_full = false;
    for (size_t cycle = 0; cycle < 32; ++cycle) {
      const auto queues = view.Queues();
      if (queues.tag_array_resp == 2) {
        tag_pipeline_full = true;
        break;
      }
      write_resp_hol_bench_.AdvanceCycle();
    }
    Expect(tag_pipeline_full,
           "head-of-line regression fills the tag-to-data pipeline");

    write_resp_hol_bench_.memory_.RespondAt(0, refill_block);
    write_resp_hol_bench_.memory_.RespondAt(1, refill_block);
    bool refill_responses_blocked = false;
    for (size_t cycle = 0; cycle < 32; ++cycle) {
      const auto queues = view.Queues();
      if (queues.mem_resp == 2 && queues.tag_array_resp == 2) {
        refill_responses_blocked = true;
        break;
      }
      write_resp_hol_bench_.AdvanceCycle();
    }
    Expect(refill_responses_blocked,
           "refills fill the memory-response queue while tag output is full");

    write_resp_hol_bench_.memory_.RespondAt(2);
    Expect(write_resp_hol_bench_.WaitForMemoryRequests(4, 32),
           "write response bypasses the full memory-response queue and frees "
           "a write-buffer entry");
  }

  void TestWriteBufferPressureDefersMshrReads() {
    CacheModuleTester view(write_buffer_pressure_bench_.cache_);

    write_buffer_pressure_bench_.core_.SendWrite(0x00, Sequence(4, 0x10));
    Expect(write_buffer_pressure_bench_.WaitForMemoryRequests(1),
           "pressure regression first write reaches memory");
    ExpectWriteRequest(write_buffer_pressure_bench_, 0, 0x00,
                       LineWriteData(16, 0, Sequence(4, 0x10)),
                       "pressure regression first write is line masked",
                       LineWriteMask(16, 0, 4));

    write_buffer_pressure_bench_.core_.SendWrite(0x10, Sequence(4, 0x20));
    bool write_buffer_input_blocked = false;
    for (size_t cycle = 0; cycle < 16; ++cycle) {
      const auto queues = view.Queues();
      const auto write_buffer = view.WriteBufferState();
      if (queues.write_buffer_mem_req == 1 &&
          write_buffer.inflight_entries == write_buffer.capacity) {
        write_buffer_input_blocked = true;
        break;
      }
      write_buffer_pressure_bench_.AdvanceCycle();
    }
    Expect(write_buffer_input_blocked,
           "pressure regression creates cache-to-write-buffer backpressure");
    Expect(write_buffer_pressure_bench_.memory_.request_count() == 1,
           "pressure regression second write waits for a write-buffer entry");

    write_buffer_pressure_bench_.core_.SendRead(0x40, 4);
    bool mshr_read_deferred = false;
    for (size_t cycle = 0; cycle < 16; ++cycle) {
      write_buffer_pressure_bench_.AdvanceCycle();
      const auto queues = view.Queues();
      if (queues.mshr_file_mem_req == 1 &&
          write_buffer_pressure_bench_.memory_.request_count() == 1) {
        mshr_read_deferred = true;
        break;
      }
      if (write_buffer_pressure_bench_.memory_.request_count() > 1) {
        break;
      }
    }
    Expect(mshr_read_deferred,
           "pressure regression defers MSHR reads while write-buffer input is "
           "backpressured");

    write_buffer_pressure_bench_.memory_.RespondAt(0);
    Expect(write_buffer_pressure_bench_.WaitForMemoryRequests(2, 32),
           "pressure regression write response frees the blocked write");
    ExpectWriteRequest(write_buffer_pressure_bench_, 1, 0x10,
                       LineWriteData(16, 0, Sequence(4, 0x20)),
                       "pressure regression waiting write issues before read",
                       LineWriteMask(16, 0, 4));
    Expect(write_buffer_pressure_bench_.WaitForMemoryRequests(3, 32),
           "pressure regression deferred read eventually reaches memory");
    ExpectReadRequest(write_buffer_pressure_bench_, 2, 0x40, 16,
                      "pressure regression read is issued after write-buffer "
                      "pressure clears");
  }

  void TestDirtyRefillsMakeForwardProgressUnderWriteBufferPressure() {
    CacheBench &bench = dirty_refill_deadlock_bench_;
    constexpr size_t kLineCount = 8;
    constexpr uint64_t kReplacementOffset = 0x80;

    for (size_t line = 0; line < kLineCount; ++line) {
      const uint64_t address = line * 16;
      (void)FillAndDirtyLine(bench, address,
                             Sequence(16, static_cast<uint8_t>(0x10 + line)), 0,
                             Sequence(4, static_cast<uint8_t>(0x80 + line)));
    }

    const size_t request_begin = bench.memory_.request_count();
    const size_t response_begin = bench.core_.response_count();
    for (size_t line = 0; line < kLineCount; ++line) {
      const uint64_t address = kReplacementOffset + line * 16;
      bench.core_.SendRead(address, 4);
    }

    size_t next_response = request_begin;
    size_t previous_request_count = request_begin;
    size_t stable_request_cycles = 0;
    bool responder_started = false;
    for (size_t cycle = 0; cycle < 512; ++cycle) {
      const size_t request_count = bench.memory_.request_count();
      if (!responder_started) {
        if (request_count == previous_request_count) {
          ++stable_request_cycles;
        } else {
          stable_request_cycles = 0;
          previous_request_count = request_count;
        }
        responder_started = request_count >= request_begin + kLineCount ||
                            stable_request_cycles >= 8;
      }

      while (responder_started &&
             next_response < bench.memory_.request_count()) {
        auto *request = bench.memory_.request(next_response);
        if (request->is_read()) {
          const uint8_t first =
              static_cast<uint8_t>(0x40 + (request->get_address() / 16));
          bench.memory_.RespondAt(next_response, Sequence(16, first));
        } else {
          bench.memory_.RespondAt(next_response);
        }
        ++next_response;
      }

      if (bench.core_.response_count() >= response_begin + kLineCount) {
        break;
      }
      bench.AdvanceCycle();
    }

    Expect(bench.core_.response_count() >= response_begin + kLineCount,
           "all dirty refills complete with ordered memory responses under "
           "write-buffer pressure");
  }

  void TestBlockedReplayPreventsYoungerRefillEviction() {
    CacheModuleTester view(replay_hazard_refill_bench_.cache_);
    const auto block_a = Sequence(16, 0x10);
    const auto block_b = Sequence(16, 0x60);
    const auto patch = Sequence(4, 0xA0);

    auto *write_a = replay_hazard_refill_bench_.core_.SendWrite(0x04, patch);
    auto *read_a = replay_hazard_refill_bench_.core_.SendRead(0x04, 4);
    (void)read_a;
    Expect(replay_hazard_refill_bench_.WaitForMemoryRequests(1, 32),
           "first same-line miss reaches memory");
    ExpectReadRequest(replay_hazard_refill_bench_, 0, 0x00, 16,
                      "same-line replay regression first miss is block "
                      "aligned");

    auto *read_b = replay_hazard_refill_bench_.core_.SendRead(0x40, 4);
    (void)read_b;
    Expect(replay_hazard_refill_bench_.WaitForMemoryRequests(2, 32),
           "younger same-set miss reaches memory");
    ExpectReadRequest(replay_hazard_refill_bench_, 1, 0x40, 16,
                      "younger same-set miss is block aligned");

    replay_hazard_refill_bench_.memory_.RespondAt(0, block_a);
    Expect(replay_hazard_refill_bench_.WaitForCoreResponses(1, 32),
           "first write replay responds to the core");
    Expect(replay_hazard_refill_bench_.core_.HasResponse(write_a),
           "first write replay response is observed");
    Expect(replay_hazard_refill_bench_.WaitForMemoryRequests(3, 32),
           "write-through replay emits a writeback request");
    ExpectWriteRequest(replay_hazard_refill_bench_, 2, 0x00,
                       LineWriteData(16, 4, patch),
                       "write-through replay emits the patched line write",
                       LineWriteMask(16, 4, patch.size()));

    replay_hazard_refill_bench_.memory_.RespondAt(1, block_b);
    bool younger_refill_waits = false;
    for (size_t cycle = 0; cycle < 16; ++cycle) {
      const auto queues = view.Queues();
      if (queues.mem_resp == 1 &&
          !replay_hazard_refill_bench_.core_.HasResponse(read_a)) {
        younger_refill_waits = true;
        break;
      }
      replay_hazard_refill_bench_.AdvanceCycle();
    }
    Expect(younger_refill_waits,
           "younger same-set refill waits while same-line replay is blocked "
           "by the write-through response");

    replay_hazard_refill_bench_.memory_.RespondAt(2);
    Expect(replay_hazard_refill_bench_.WaitForCoreResponses(3, 64),
           "blocked replay and younger refill both complete after write "
           "response");
    Expect(replay_hazard_refill_bench_.core_.HasResponse(read_a),
           "blocked same-line replay response is observed");
    Expect(PayloadData(read_a) == patch,
           "blocked same-line replay observes the older write replay");
    Expect(replay_hazard_refill_bench_.core_.HasResponse(read_b),
           "younger same-set refill response is observed");
    Expect(PayloadData(read_b) == Slice(block_b, 0, 4),
           "younger same-set refill returns its block data");
  }

  void TestRefillWaitsForReplayVisibility() {
    const auto block_a = Sequence(16, 0x30);
    const auto block_b = Sequence(16, 0x70);

    auto *read_a = replay_visibility_bench_.core_.SendRead(0x00, 4);
    auto *read_b = replay_visibility_bench_.core_.SendRead(0x40, 4);
    Expect(replay_visibility_bench_.WaitForMemoryRequests(2, 32),
           "same-set replay visibility regression misses reach memory");
    ExpectReadRequest(replay_visibility_bench_, 0, 0x00, 16,
                      "first same-set miss is block aligned");
    ExpectReadRequest(replay_visibility_bench_, 1, 0x40, 16,
                      "second same-set miss is block aligned");

    replay_visibility_bench_.memory_.RespondAt(0, block_a);
    replay_visibility_bench_.memory_.RespondAt(1, block_b);
    Expect(replay_visibility_bench_.WaitForCoreResponses(2, 64),
           "same-set refills wait for replay visibility before replacement");
    Expect(replay_visibility_bench_.core_.HasResponse(read_a),
           "first same-set replay response is observed");
    Expect(PayloadData(read_a) == Slice(block_a, 0, 4),
           "first same-set replay returns its block data");
    Expect(replay_visibility_bench_.core_.HasResponse(read_b),
           "second same-set replay response is observed");
    Expect(PayloadData(read_b) == Slice(block_b, 0, 4),
           "second same-set replay returns its block data");
  }

  void TestTagStagePriority() {
    CacheModuleTester view(tag_priority_bench_.cache_);
    const auto block_a = Sequence(16, 0x30);
    const auto block_b = Sequence(16, 0x50);

    auto *read_a = tag_priority_bench_.core_.SendRead(0x00, 4);
    auto *read_b = tag_priority_bench_.core_.SendRead(0x10, 4);
    (void)read_a;
    (void)read_b;
    Expect(tag_priority_bench_.WaitForMemoryRequests(2),
           "two independent read misses reach memory");
    ExpectReadRequest(tag_priority_bench_, 0, 0x00, 16,
                      "first priority read miss issues a block read");
    ExpectReadRequest(tag_priority_bench_, 1, 0x10, 16,
                      "second priority read miss issues a block read");

    tag_priority_bench_.memory_.RespondAt(0, block_a);
    tag_priority_bench_.memory_.RespondAt(1, block_b);
    tag_priority_bench_.core_.SendRead(0x20, 4);

    tag_priority_bench_.AdvanceCycle();
    tag_priority_bench_.AdvanceCycle();
    auto queues = view.Queues();
    Expect(queues.mem_resp == 1, "first refill waits in memory response queue");
    Expect(queues.core_req == 1,
           "new core request waits in core request queue");

    tag_priority_bench_.AdvanceCycle();
    queues = view.Queues();
    Expect(queues.tag_array_resp == 1,
           "first refill occupies the tag-to-data pipeline queue");
    Expect(queues.mem_resp == 0,
           "first refill is consumed before the second response is accepted");
    Expect(queues.core_req == 1, "core request waits behind refill priority");

    tag_priority_bench_.AdvanceCycle();
    // Refill completion now publishes replay only after the data stage has
    // committed or cancelled its victim reservation.
    tag_priority_bench_.AdvanceCycle();
    queues = view.Queues();
    Expect(queues.mshr_file_replay == 1,
           "first refill completion produces a replay candidate");
    Expect(queues.mem_resp == 1,
           "second refill is queued before replay arbitration");
    Expect(queues.core_req == 1,
           "core request is queued before replay arbitration");

    tag_priority_bench_.AdvanceCycle();
    queues = view.Queues();
    Expect(queues.tag_array_resp == 1,
           "replay enters the tag-to-data pipeline after first refill commits");
    Expect(queues.mshr_file_replay == 0,
           "replay is consumed before refill and core request");
    Expect(queues.mem_resp == 1,
           "second refill remains queued when replay has priority");
    Expect(queues.core_req == 1,
           "core request remains queued when replay has priority");

    tag_priority_bench_.AdvanceCycle();
    queues = view.Queues();
    Expect(queues.core_resp == 1,
           "replay reaches the core-response queue before later traffic");
    Expect(queues.mem_resp == 1,
           "second refill remains queued while replay leaves data stage");
    Expect(queues.core_req == 1,
           "core request remains queued while replay leaves data stage");

    tag_priority_bench_.AdvanceCycle();
    queues = view.Queues();
    Expect(queues.tag_array_resp == 1,
           "second refill enters the tag-to-data pipeline after replay");
    Expect(queues.mem_resp == 0,
           "second refill is consumed before the waiting core request");
    Expect(queues.core_req == 1,
           "core request still waits while refill has priority");
  }

  std::optional<size_t> FindReadRequest(CacheBench &bench, size_t begin_index,
                                        uint64_t address, size_t size) {
    for (size_t index = begin_index; index < bench.memory_.request_count();
         ++index) {
      tlm::tlm_generic_payload *request = bench.memory_.request(index);
      if (request->is_read() && request->get_address() == address &&
          request->get_data_length() == size) {
        return index;
      }
    }
    return std::nullopt;
  }

  std::optional<size_t> FindWriteRequest(CacheBench &bench, size_t begin_index,
                                         uint64_t address, size_t size) {
    for (size_t index = begin_index; index < bench.memory_.request_count();
         ++index) {
      tlm::tlm_generic_payload *request = bench.memory_.request(index);
      if (request->is_write() && request->get_address() == address &&
          request->get_data_length() == size) {
        return index;
      }
    }
    return std::nullopt;
  }

  void CompleteReadAfterOlderSameLineWrite(
      CacheBench &bench, size_t write_request_index,
      tlm::tlm_generic_payload *read, size_t read_response_count,
      const std::vector<uint8_t> &old_block,
      const std::vector<uint8_t> &patched_block,
      const std::vector<uint8_t> &expected_read_data) {
    const uint64_t line_address = 0x00;
    const size_t block_size = old_block.size();
    const size_t read_request_begin = bench.memory_.request_count();
    bool read_request_responded = false;

    for (size_t cycle = 0; cycle < 12; ++cycle) {
      if (bench.core_.HasResponse(read)) {
        bench.memory_.RespondAt(write_request_index);
        Expect(PayloadData(read) == expected_read_data,
               "read forwarded before write ack observes older write");
        return;
      }

      const auto read_request =
          FindReadRequest(bench, read_request_begin, line_address, block_size);
      if (read_request.has_value()) {
        bench.memory_.RespondAt(*read_request, old_block);
        read_request_responded = true;
        break;
      }

      bench.AdvanceCycle();
    }

    bench.memory_.RespondAt(write_request_index);
    if (!read_request_responded) {
      for (size_t cycle = 0; cycle < 32; ++cycle) {
        if (bench.core_.HasResponse(read)) {
          break;
        }

        const auto read_request = FindReadRequest(bench, read_request_begin,
                                                  line_address, block_size);
        if (read_request.has_value()) {
          bench.memory_.RespondAt(*read_request, patched_block);
          read_request_responded = true;
          break;
        }

        bench.AdvanceCycle();
      }
    }

    Expect(bench.WaitForCoreResponses(read_response_count + 1, 64),
           "same-line read after older write returns to the core");
    Expect(bench.core_.HasResponse(read),
           "same-line read after older write response is observed");
    Expect(PayloadData(read) == expected_read_data,
           "same-line read after older write observes the written bytes");
  }

  void EvictLineWithRead(CacheBench &bench, uint64_t replacement_address,
                         const std::vector<uint8_t> &replacement_block) {
    const size_t request_index = bench.memory_.request_count();
    const size_t response_count = bench.core_.response_count();
    auto *replacement = bench.core_.SendRead(replacement_address, 4);

    Expect(bench.WaitForMemoryRequests(request_index + 1),
           "replacement read miss reaches memory");
    ExpectReadRequest(bench, request_index, replacement_address,
                      replacement_block.size(),
                      "replacement read miss issues a block read");

    bench.memory_.RespondAt(request_index, replacement_block);
    Expect(bench.WaitForCoreResponses(response_count + 1, 32),
           "replacement read response returns to core");
    Expect(bench.core_.HasResponse(replacement),
           "replacement read response is observed");
  }

  void TestReadMissObservesOlderSameLineWrite(CacheBench &bench,
                                              WriteMissPolicy write_miss_policy,
                                              WriteHitPolicy write_hit_policy) {
    const uint64_t line_address = 0x00;
    const uint64_t write_address = 0x04;
    const uint64_t replacement_address = 0x40;
    const auto old_block = Sequence(16, 0x10);
    const auto patch = Sequence(4, 0xA0);
    const auto patched_block = WithPatch(old_block, 4, patch);
    const auto replacement_block = Sequence(16, 0x50);

    size_t write_request_index = 0;

    if (write_miss_policy == WriteMissPolicy::kWriteNoAllocate) {
      write_request_index = bench.memory_.request_count();
      const size_t write_response_count = bench.core_.response_count();
      auto *write = bench.core_.SendWrite(write_address, patch);

      Expect(bench.WaitForCoreResponses(write_response_count + 1, 32),
             "write-no-allocate miss responds to the core");
      Expect(bench.core_.HasResponse(write),
             "write-no-allocate write response is observed");
      Expect(bench.WaitForMemoryRequests(write_request_index + 1),
             "write-no-allocate miss reaches memory");
      ExpectWriteRequest(bench, write_request_index, line_address,
                         LineWriteData(16, 4, patch),
                         "write-no-allocate miss emits the older write",
                         LineWriteMask(16, 4, patch.size()));
    } else {
      FillLineWithRead(bench, line_address, old_block);

      const size_t write_response_count = bench.core_.response_count();
      write_request_index = bench.memory_.request_count();
      auto *write = bench.core_.SendWrite(write_address, patch);
      Expect(bench.WaitForCoreResponses(write_response_count + 1, 32),
             "write hit responds to the core");
      Expect(bench.core_.HasResponse(write), "write hit response is observed");

      if (write_hit_policy == WriteHitPolicy::kWriteThrough) {
        Expect(bench.WaitForMemoryRequests(write_request_index + 1),
               "write-through hit reaches memory");
        ExpectWriteRequest(bench, write_request_index, line_address,
                           LineWriteData(16, 4, patch),
                           "write-through hit emits the older write",
                           LineWriteMask(16, 4, patch.size()));
        EvictLineWithRead(bench, replacement_address, replacement_block);
      } else {
        write_request_index = bench.memory_.request_count() + 1;
        EvictLineWithRead(bench, replacement_address, replacement_block);
        Expect(bench.WaitForMemoryRequests(write_request_index + 1, 32),
               "dirty victim writeback reaches memory");
        ExpectWriteRequest(bench, write_request_index, line_address,
                           patched_block,
                           "dirty victim writeback emits the older write",
                           AllEnabledMask(patched_block.size()));
      }
    }

    const size_t read_response_count = bench.core_.response_count();
    auto *read = bench.core_.SendRead(write_address, patch.size());
    CompleteReadAfterOlderSameLineWrite(bench, write_request_index, read,
                                        read_response_count, old_block,
                                        patched_block, patch);
  }

  void TestWriteNoAllocateStalledMissFencesSameLineRead() {
    CacheBench &bench = wna_stalled_hazard_bench_;
    CacheModuleTester view(bench.cache_);
    const uint64_t line_address = 0x00;
    const uint64_t write_address = 0x04;
    const auto old_block = Sequence(16, 0x10);
    const auto patch = Sequence(4, 0xA0);

    auto *write = bench.core_.SendWrite(write_address, patch);
    bool write_reached_data_stage = false;
    for (size_t cycle = 0; cycle < 16; ++cycle) {
      bench.AdvanceCycle();
      if (view.Queues().tag_array_resp == 1) {
        write_reached_data_stage = true;
        break;
      }
    }
    Expect(write_reached_data_stage,
           "write-no-allocate miss reaches the data-stage queue");

    view.FillCoreResponseQueueWithTestPackets(0x1000);
    const size_t request_begin = bench.memory_.request_count();
    const size_t response_begin = bench.core_.response_count();
    auto *read = bench.core_.SendRead(write_address, patch.size());
    wait(sc_core::SC_ZERO_TIME);

    view.AcceptCoreRequest();
    view.AccessDataArrayStage();
    view.AccessTagArrayStage();
    view.SendMemRequest();

    bool early_read_responded = false;
    const auto early_read =
        FindReadRequest(bench, request_begin, line_address, old_block.size());
    if (early_read.has_value()) {
      bench.memory_.RespondAt(*early_read, old_block);
      early_read_responded = true;
    }

    view.ClearCoreResponseQueue();

    std::optional<size_t> write_request;
    for (size_t cycle = 0; cycle < 64; ++cycle) {
      write_request = FindWriteRequest(bench, request_begin, line_address,
                                       old_block.size());
      if (write_request.has_value()) {
        break;
      }
      bench.AdvanceCycle();
    }
    Expect(write_request.has_value(),
           "stalled write-no-allocate miss eventually reaches memory");
    if (write_request.has_value()) {
      ExpectWriteRequest(bench, *write_request, line_address,
                         LineWriteData(old_block.size(), 4, patch),
                         "stalled write-no-allocate miss emits older write",
                         LineWriteMask(old_block.size(), 4, patch.size()));
      bench.memory_.RespondAt(*write_request);
    }

    if (!early_read_responded) {
      for (size_t cycle = 0; cycle < 64; ++cycle) {
        const auto read_request = FindReadRequest(
            bench, request_begin, line_address, old_block.size());
        if (read_request.has_value()) {
          bench.memory_.RespondAt(*read_request,
                                  WithPatch(old_block, 4, patch));
          break;
        }
        bench.AdvanceCycle();
      }
    }

    Expect(bench.WaitForCoreResponses(response_begin + 2, 64),
           "same-line read after stalled write-no-allocate miss returns");
    Expect(bench.core_.HasResponse(write),
           "stalled write-no-allocate core write response is observed");
    Expect(bench.core_.HasResponse(read),
           "same-line read after stalled write-no-allocate miss is observed");
    Expect(PayloadData(read) == patch,
           "same-line read after stalled write-no-allocate miss is not stale");
  }

  void TestDirtyVictimStalledWritebackFencesSameLineRead() {
    CacheBench &bench = dirty_victim_stalled_hazard_bench_;
    CacheModuleTester view(bench.cache_);
    const uint64_t victim_address = 0x00;
    const uint64_t replacement_address = 0x40;
    const auto old_block = Sequence(16, 0x20);
    const auto patch = Sequence(4, 0xB0);
    const auto dirty_block = WithPatch(old_block, 4, patch);
    const auto replacement_block = Sequence(16, 0x60);

    FillLineWithRead(bench, victim_address, old_block);

    auto *write = bench.core_.SendWrite(victim_address + 4, patch);
    Expect(bench.WaitForCoreResponses(2, 32),
           "dirty-victim setup write hit returns to core");
    Expect(bench.core_.HasResponse(write),
           "dirty-victim setup write response is observed");

    auto *replacement = bench.core_.SendRead(replacement_address, 4);
    const size_t replacement_request = bench.memory_.request_count();
    Expect(bench.WaitForMemoryRequests(replacement_request + 1, 32),
           "dirty-victim replacement miss reaches memory");
    ExpectReadRequest(bench, replacement_request, replacement_address, 16,
                      "dirty-victim replacement miss issues a block read");
    bench.memory_.RespondAt(replacement_request, replacement_block);

    bool refill_reached_data_stage = false;
    for (size_t cycle = 0; cycle < 16; ++cycle) {
      bench.AdvanceCycle();
      if (view.Queues().tag_array_resp == 1) {
        refill_reached_data_stage = true;
        break;
      }
    }
    Expect(refill_reached_data_stage,
           "dirty-victim refill reaches the data-stage queue");

    view.FillWriteBufferInputQueueWithTestPackets(0x2000);
    const size_t request_begin = bench.memory_.request_count();
    const size_t response_begin = bench.core_.response_count();
    auto *read = bench.core_.SendRead(victim_address + 4, patch.size());
    wait(sc_core::SC_ZERO_TIME);

    view.AcceptCoreRequest();
    view.AccessDataArrayStage();
    view.AccessTagArrayStage();
    view.SendMemRequest();

    bool early_read_responded = false;
    const auto early_read =
        FindReadRequest(bench, request_begin, victim_address, old_block.size());
    if (early_read.has_value()) {
      bench.memory_.RespondAt(*early_read, old_block);
      early_read_responded = true;
    }

    view.ClearWriteBufferInputQueue();

    std::optional<size_t> writeback_request;
    for (size_t cycle = 0; cycle < 64; ++cycle) {
      writeback_request = FindWriteRequest(bench, request_begin, victim_address,
                                           old_block.size());
      if (writeback_request.has_value()) {
        break;
      }
      bench.AdvanceCycle();
    }
    Expect(writeback_request.has_value(),
           "stalled dirty-victim writeback eventually reaches memory");
    if (writeback_request.has_value()) {
      ExpectWriteRequest(bench, *writeback_request, victim_address, dirty_block,
                         "stalled dirty-victim writeback emits older data",
                         AllEnabledMask(dirty_block.size()));
      bench.memory_.RespondAt(*writeback_request);
    }

    if (!early_read_responded) {
      for (size_t cycle = 0; cycle < 64; ++cycle) {
        const auto read_request = FindReadRequest(
            bench, request_begin, victim_address, old_block.size());
        if (read_request.has_value()) {
          bench.memory_.RespondAt(*read_request, dirty_block);
          break;
        }
        bench.AdvanceCycle();
      }
    }

    Expect(bench.WaitForCoreResponses(response_begin + 2, 64),
           "same-line read after stalled dirty-victim writeback returns");
    Expect(bench.core_.HasResponse(replacement),
           "dirty-victim replacement response is observed");
    Expect(bench.core_.HasResponse(read),
           "same-line read after stalled dirty-victim writeback is observed");
    Expect(PayloadData(read) == patch,
           "same-line read after stalled dirty-victim writeback is not stale");
  }

  CacheBench mshr_deadlock_bench_;
  CacheBench write_resp_hol_bench_;
  CacheBench write_buffer_pressure_bench_;
  CacheBench dirty_refill_deadlock_bench_;
  CacheBench replay_hazard_refill_bench_;
  CacheBench replay_visibility_bench_;
  CacheBench tag_priority_bench_;
  CacheBench write_hazard_wt_wa_bench_;
  CacheBench write_hazard_wb_wa_bench_;
  CacheBench write_hazard_wt_wna_bench_;
  CacheBench write_hazard_wb_wna_bench_;
  CacheBench wna_stalled_hazard_bench_;
  CacheBench dirty_victim_stalled_hazard_bench_;
};

}  // namespace

int sc_main(int, char *[]) {
  CacheModuleHazardTestRunner tester("tester");
  sc_core::sc_start();
  return tester.failed() ? 1 : 0;
}
