// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include "cache_module_test_common.h"

namespace {

class CacheModuleBasicTestRunner : public CacheModuleTestRunnerBase {
 public:
  explicit CacheModuleBasicTestRunner(sc_core::sc_module_name name)
      : CacheModuleTestRunnerBase(name),
        read_bench_("read_bench", MakeParam()),
        read_hit_bench_("read_hit_bench", MakeParam()),
        merge_bench_("merge_bench",
                     MakeParam(WriteMissPolicy::kWriteAllocate,
                               WriteHitPolicy::kWriteThrough, 1, 2, 2)),
        write_bench_("write_bench",
                     MakeParam(WriteMissPolicy::kWriteNoAllocate)),
        write_no_allocate_bench_(
            "write_no_allocate_bench",
            MakeParam(WriteMissPolicy::kWriteNoAllocate,
                      WriteHitPolicy::kWriteThrough, 1, 2, 1, 4)),
        write_through_bench_(
            "write_through_bench",
            MakeParam(WriteMissPolicy::kWriteAllocate,
                      WriteHitPolicy::kWriteThrough, 1, 2, 1, 4)),
        dirty_victim_bench_("dirty_victim_bench",
                            MakeParam(WriteMissPolicy::kWriteAllocate,
                                      WriteHitPolicy::kWriteBack, 1, 2, 1, 4)),
        non_cacheable_bench_("non_cacheable_bench", MakeNonCacheableParam()),
        arbitration_bench_("arbitration_bench",
                           MakeParam(WriteMissPolicy::kWriteNoAllocate)) {
    SC_THREAD(Run);
  }

 private:
  void Run() {
    read_bench_.clock_.write(false);
    read_hit_bench_.clock_.write(false);
    merge_bench_.clock_.write(false);
    write_bench_.clock_.write(false);
    write_no_allocate_bench_.clock_.write(false);
    write_through_bench_.clock_.write(false);
    dirty_victim_bench_.clock_.write(false);
    non_cacheable_bench_.clock_.write(false);
    arbitration_bench_.clock_.write(false);
    wait(sc_core::SC_ZERO_TIME);

    RunBench("read_bench", [this] {
      TestReadMissBackpressure();
    });
    RunBench("read_hit_bench", [this] {
      TestReadMissRefillAndHit();
    });
    RunBench("merge_bench", [this] {
      TestMshrSecondaryMissReplay();
    });
    RunBench("write_bench", [this] {
      TestWriteBufferBackpressure();
    });
    RunBench("write_no_allocate_bench", [this] {
      TestWriteNoAllocateMissBypassesCache();
    });
    RunBench("write_through_bench", [this] {
      TestWriteThroughHitUpdatesCacheAndMemory();
    });
    RunBench("dirty_victim_bench", [this] {
      TestWriteBackDirtyVictimWriteback();
    });
    RunBench("non_cacheable_bench", [this] {
      TestNonCacheableBypassPriority();
    });
    RunBench("arbitration_bench", [this] {
      TestMemoryRequestPriority();
    });

    sc_core::sc_stop();
  }

  void TestReadMissBackpressure() {
    CacheModuleTester view(read_bench_.cache_);

    auto *first = read_bench_.core_.SendRead(0x00, 4);
    Expect(read_bench_.WaitForMemoryRequests(1),
           "first read miss reaches memory");
    Expect(read_bench_.memory_.request(0)->is_read(),
           "first memory request is a read");
    Expect(read_bench_.memory_.request(0)->get_address() == 0x00,
           "first memory request is block aligned");
    auto mshr = view.Mshr();
    Expect(mshr.pending_refill_entries == 1,
           "first read miss allocates one MSHR entry");
    Expect(mshr.tracked_sub_entries == 1,
           "first read miss tracks one replay packet");
    Expect(view.Queues().mem_inflight_packets == 1,
           "first read miss is tracked as an inflight memory packet");
    Expect(!view.CachedLine(0x00).has_value(),
           "read miss does not allocate the cache line before refill");

    read_bench_.core_.SendRead(0x10, 4);
    Expect(read_bench_.WaitForMemoryRequests(2),
           "second read miss reaches memory");
    mshr = view.Mshr();
    Expect(mshr.pending_refill_entries == 2,
           "second independent read miss allocates another MSHR entry");
    Expect(mshr.tracked_sub_entries == 2,
           "two pending read misses are tracked for replay");
    Expect(view.Queues().mem_inflight_packets == 2,
           "two read misses are tracked as inflight memory packets");

    read_bench_.core_.SendRead(0x20, 4);
    for (size_t i = 0; i < 6; ++i) {
      read_bench_.AdvanceCycle();
    }
    Expect(read_bench_.memory_.request_count() == 2,
           "third read miss waits while MSHR entries are full");
    mshr = view.Mshr();
    Expect(mshr.pending_refill_entries == 2,
           "MSHR stays full while the third read miss is stalled");
    Expect(mshr.tracked_sub_entries == 2,
           "stalled third read miss is not merged before an entry frees");

    read_bench_.memory_.Respond(read_bench_.memory_.request(0),
                                Sequence(16, 0x40));
    Expect(read_bench_.WaitForCoreResponses(1),
           "read refill replays the blocked core request");
    Expect(read_bench_.core_.HasResponse(first),
           "first read response returns to the core");
    Expect(PayloadData(first) == Sequence(4, 0x40),
           "first read response data comes from the refill block");
    auto line = view.CachedLine(0x00);
    Expect(line.has_value(), "read refill installs the cache line");
    if (line.has_value()) {
      Expect(line->data == Sequence(16, 0x40),
             "refill data is stored in the data array");
      Expect(!line->tag.dirty, "read refill installs a clean cache line");
    }

    Expect(read_bench_.WaitForMemoryRequests(3),
           "third read miss issues after a refill frees an MSHR entry");
    mshr = view.Mshr();
    Expect(
        mshr.pending_refill_entries == 2,
        "second and third read misses remain pending after the first refill");
    Expect(view.Queues().mem_inflight_packets == 2,
           "inflight memory packet map drops the refilled read and tracks the "
           "remaining reads");
  }

  void TestReadMissRefillAndHit() {
    CacheModuleTester view(read_hit_bench_.cache_);
    const auto block = Sequence(16, 0x40);

    auto *first = FillLineWithRead(read_hit_bench_, 0x00, block);
    auto line = view.CachedLine(0x00);
    Expect(line.has_value(), "read miss refill installs the cache line");
    if (line.has_value()) {
      Expect(line->data == block, "read miss refill stores the whole block");
      Expect(!line->tag.dirty, "read miss refill leaves the line clean");
    }
    Expect(PayloadData(first) == Slice(block, 0, 4),
           "first read miss returns the first bytes of the block");

    const size_t request_count = read_hit_bench_.memory_.request_count();
    auto *second = read_hit_bench_.core_.SendRead(0x04, 4);
    Expect(read_hit_bench_.WaitForCoreResponses(2),
           "second same-line read hits and returns to the core");
    Expect(read_hit_bench_.core_.HasResponse(second),
           "second read response is observed");
    Expect(PayloadData(second) == Slice(block, 4, 4),
           "second read hit returns data from the cached block offset");
    for (size_t i = 0; i < 4; ++i) {
      read_hit_bench_.AdvanceCycle();
    }
    Expect(read_hit_bench_.memory_.request_count() == request_count,
           "second same-line read hit does not issue a memory request");
  }

  void TestMshrSecondaryMissReplay() {
    CacheModuleTester view(merge_bench_.cache_);
    const auto block = Sequence(16, 0x70);

    auto *first = merge_bench_.core_.SendRead(0x00, 4);
    Expect(merge_bench_.WaitForMemoryRequests(1),
           "first same-line read miss reaches memory");
    ExpectReadRequest(merge_bench_, 0, 0x00, 16,
                      "first same-line miss issues one block read");

    auto *second = merge_bench_.core_.SendRead(0x08, 4);
    for (size_t i = 0; i < 6; ++i) {
      merge_bench_.AdvanceCycle();
    }
    Expect(merge_bench_.memory_.request_count() == 1,
           "secondary same-line read miss merges without another memory read");
    auto mshr = view.Mshr();
    Expect(mshr.pending_refill_entries == 1,
           "merged same-line misses share one MSHR entry");
    Expect(mshr.tracked_sub_entries == 2,
           "merged same-line misses keep both replay packets");

    merge_bench_.memory_.RespondAt(0, block);
    Expect(merge_bench_.WaitForCoreResponses(2, 32),
           "refill replays both same-line read misses");
    Expect(merge_bench_.core_.HasResponse(first),
           "primary same-line read response is observed");
    Expect(merge_bench_.core_.HasResponse(second),
           "secondary same-line read response is observed");
    Expect(PayloadData(first) == Slice(block, 0, 4),
           "primary same-line read returns refill data");
    Expect(PayloadData(second) == Slice(block, 8, 4),
           "secondary same-line read returns refill data at its offset");

    mshr = view.Mshr();
    Expect(mshr.pending_refill_entries == 0,
           "MSHR entry is released after both replays drain");
    Expect(mshr.tracked_sub_entries == 0,
           "MSHR has no tracked sub-entries after replay drain");
  }

  void TestWriteBufferBackpressure() {
    CacheModuleTester view(write_bench_.cache_);

    auto *first = write_bench_.core_.SendWrite(0x00, Sequence(4, 0x10));
    (void)first;
    Expect(write_bench_.WaitForMemoryRequests(1),
           "first write-no-allocate miss reaches memory");
    Expect(write_bench_.memory_.request(0)->is_write(),
           "first write-buffer request is a write");
    auto write_buffer = view.WriteBufferState();
    Expect(write_buffer.inflight_entries == 1,
           "first write occupies one write-buffer inflight entry");
    Expect(!view.CachedLine(0x00).has_value(),
           "write-no-allocate miss does not install a cache line");

    write_bench_.core_.SendWrite(0x10, Sequence(4, 0x20));
    Expect(write_bench_.WaitForMemoryRequests(2),
           "second write-no-allocate miss reaches memory");
    write_buffer = view.WriteBufferState();
    Expect(write_buffer.inflight_entries == write_buffer.capacity,
           "two writes fill the write-buffer inflight capacity");

    write_bench_.core_.SendWrite(0x20, Sequence(4, 0x30));
    for (size_t i = 0; i < 6; ++i) {
      write_bench_.AdvanceCycle();
    }
    Expect(write_bench_.memory_.request_count() == 2,
           "third write waits while write buffer entries are full");
    write_buffer = view.WriteBufferState();
    Expect(write_buffer.pending_entries + write_buffer.inflight_entries ==
               write_buffer.capacity,
           "write-buffer internal occupancy remains at capacity");
    Expect(view.Queues().write_buffer_mem_req == 1,
           "third write waits in the cache-to-write-buffer queue");

    write_bench_.memory_.Respond(write_bench_.memory_.request(0));
    Expect(write_bench_.WaitForMemoryRequests(3),
           "write response frees a write-buffer entry");
    write_buffer = view.WriteBufferState();
    Expect(write_buffer.inflight_entries == write_buffer.capacity,
           "third write occupies the entry freed by the write response");
    Expect(view.Queues().write_buffer_mem_req == 0,
           "cache-to-write-buffer queue drains after an entry frees");
  }

  void TestWriteNoAllocateMissBypassesCache() {
    CacheModuleTester view(write_no_allocate_bench_.cache_);
    const auto write_data = Sequence(4, 0x90);

    auto *write = write_no_allocate_bench_.core_.SendWrite(0x04, write_data);
    Expect(write_no_allocate_bench_.WaitForCoreResponses(1),
           "write-no-allocate miss completes to the core");
    Expect(write_no_allocate_bench_.core_.HasResponse(write),
           "write-no-allocate core write response is observed");
    Expect(write_no_allocate_bench_.WaitForMemoryRequests(1),
           "write-no-allocate miss reaches memory");
    ExpectWriteRequest(
        write_no_allocate_bench_, 0, 0x00, LineWriteData(16, 4, write_data),
        "write-no-allocate memory write is a masked line request",
        LineWriteMask(16, 4, write_data.size()));
    Expect(!view.CachedLine(0x04).has_value(),
           "write-no-allocate miss does not install the written line");

    auto *read = write_no_allocate_bench_.core_.SendRead(0x04, 4);
    (void)read;

    for (size_t i = 0; i < 4; ++i) {
      write_no_allocate_bench_.AdvanceCycle();
    }
    Expect(write_no_allocate_bench_.memory_.request_count() == 1,
           "same-line read waits for write-no-allocate memory response");

    write_no_allocate_bench_.memory_.RespondAt(0);
    Expect(write_no_allocate_bench_.WaitForMemoryRequests(2, 32),
           "read after write-no-allocate miss still misses in cache");
    ExpectReadRequest(write_no_allocate_bench_, 1, 0x00, 16,
                      "read after write-no-allocate miss issues block read");
  }

  void TestWriteThroughHitUpdatesCacheAndMemory() {
    CacheModuleTester view(write_through_bench_.cache_);
    const auto original_block = Sequence(16, 0x10);
    const auto patch = Sequence(4, 0xA0);
    const auto patched_block = WithPatch(original_block, 4, patch);

    FillLineWithRead(write_through_bench_, 0x00, original_block);

    auto *write = write_through_bench_.core_.SendWrite(0x04, patch);
    Expect(write_through_bench_.WaitForCoreResponses(2),
           "write-through hit completes to the core");
    Expect(write_through_bench_.core_.HasResponse(write),
           "write-through core write response is observed");
    Expect(write_through_bench_.WaitForMemoryRequests(2),
           "write-through hit emits a memory write");
    ExpectWriteRequest(write_through_bench_, 1, 0x00,
                       LineWriteData(16, 4, patch),
                       "write-through memory write is a masked line request",
                       LineWriteMask(16, 4, patch.size()));

    auto line = view.CachedLine(0x00);
    Expect(line.has_value(), "write-through hit keeps the line installed");
    if (line.has_value()) {
      Expect(line->data == patched_block,
             "write-through hit updates the cached block");
      Expect(!line->tag.dirty, "write-through hit leaves the cache line clean");
    }

    const size_t request_count = write_through_bench_.memory_.request_count();
    auto *read = write_through_bench_.core_.SendRead(0x04, 4);

    for (size_t i = 0; i < 4; ++i) {
      write_through_bench_.AdvanceCycle();
    }
    Expect(write_through_bench_.core_.response_count() == 2,
           "same-line read waits for write-through memory response");

    write_through_bench_.memory_.RespondAt(1);
    Expect(write_through_bench_.WaitForCoreResponses(3, 32),
           "read after write-through hit returns from cache");
    Expect(write_through_bench_.core_.HasResponse(read),
           "read after write-through hit response is observed");
    Expect(PayloadData(read) == patch,
           "read after write-through hit observes the patched bytes");
    for (size_t i = 0; i < 4; ++i) {
      write_through_bench_.AdvanceCycle();
    }
    Expect(write_through_bench_.memory_.request_count() == request_count,
           "read after write-through hit does not issue a memory request");
  }

  void TestWriteBackDirtyVictimWriteback() {
    CacheModuleTester view(dirty_victim_bench_.cache_);
    const uint64_t victim_address = 0x00;
    const uint64_t refill_address = 0x40;
    const auto original_block = Sequence(16, 0x20);
    const auto patch = Sequence(4, 0xE0);
    const auto dirty_block = WithPatch(original_block, 4, patch);
    const auto replacement_block = Sequence(16, 0x60);

    FillLineWithRead(dirty_victim_bench_, victim_address, original_block);

    auto *write = dirty_victim_bench_.core_.SendWrite(0x04, patch);
    Expect(dirty_victim_bench_.WaitForCoreResponses(2),
           "write-back hit completes to the core");
    Expect(dirty_victim_bench_.core_.HasResponse(write),
           "write-back core write response is observed");
    for (size_t i = 0; i < 4; ++i) {
      dirty_victim_bench_.AdvanceCycle();
    }
    Expect(dirty_victim_bench_.memory_.request_count() == 1,
           "write-back hit does not emit an immediate memory write");

    auto line = view.CachedLine(victim_address);
    Expect(line.has_value(), "write-back hit keeps the victim line installed");
    if (line.has_value()) {
      Expect(line->data == dirty_block,
             "write-back hit updates the cached victim block");
      Expect(line->tag.dirty, "write-back hit marks the cache line dirty");
    }

    auto *replacement_read =
        dirty_victim_bench_.core_.SendRead(refill_address, 4);
    Expect(dirty_victim_bench_.WaitForMemoryRequests(2),
           "replacement read miss reaches memory");
    ExpectReadRequest(dirty_victim_bench_, 1, refill_address, 16,
                      "replacement read miss issues the new block read");

    dirty_victim_bench_.memory_.RespondAt(1, replacement_block);
    Expect(dirty_victim_bench_.WaitForCoreResponses(3, 32),
           "replacement read replays after refill");
    Expect(dirty_victim_bench_.core_.HasResponse(replacement_read),
           "replacement read response is observed");
    Expect(PayloadData(replacement_read) == Slice(replacement_block, 0, 4),
           "replacement read response contains replacement block data");
    Expect(dirty_victim_bench_.WaitForMemoryRequests(3, 32),
           "dirty victim is written back after replacement refill");
    ExpectWriteRequest(dirty_victim_bench_, 2, victim_address, dirty_block,
                       "dirty victim writeback preserves evicted address/data",
                       AllEnabledMask(dirty_block.size()));

    Expect(!view.CachedLine(victim_address).has_value(),
           "dirty victim line is no longer installed after replacement");
    line = view.CachedLine(refill_address);
    Expect(line.has_value(), "replacement line is installed after refill");
    if (line.has_value()) {
      Expect(line->data == replacement_block,
             "replacement line data is stored in the cache");
      Expect(!line->tag.dirty, "replacement read refill installs a clean line");
    }
  }

  void TestMemoryRequestPriority() {
    CacheModuleTester view(arbitration_bench_.cache_);

    arbitration_bench_.core_.SendRead(0x00, 4);
    arbitration_bench_.core_.SendWrite(0x10, Sequence(4, 0x50));

    Expect(arbitration_bench_.WaitForMemoryRequests(2),
           "mixed read and write traffic reaches memory");
    Expect(arbitration_bench_.memory_.request(0)->is_read(),
           "MSHR read request is sent before write-buffer request");
    Expect(arbitration_bench_.memory_.request(1)->is_write(),
           "write-buffer request is sent after the MSHR read request");
    Expect(view.Queues().mem_inflight_packets == 2,
           "read and write memory requests are both tracked as inflight");
  }

  void TestNonCacheableBypassPriority() {
    CacheModuleTester view(non_cacheable_bench_.cache_);

    auto *cacheable_read = non_cacheable_bench_.core_.SendRead(0x00, 4);
    auto *non_cacheable_read = non_cacheable_bench_.core_.SendRead(0x20, 4);
    (void)cacheable_read;

    Expect(non_cacheable_bench_.WaitForMemoryRequests(2),
           "cacheable and non-cacheable requests both reach memory");
    ExpectReadRequest(non_cacheable_bench_, 0, 0x20, 4,
                      "non-cacheable request wins memory arbitration");
    ExpectReadRequest(non_cacheable_bench_, 1, 0x00, 16,
                      "cacheable miss request waits behind non-cacheable "
                      "bypass");
    Expect(!view.CachedLine(0x20).has_value(),
           "non-cacheable request does not allocate the cache line");

    const size_t response_count = non_cacheable_bench_.core_.response_count();
    non_cacheable_bench_.memory_.RespondAt(0, Sequence(4, 0x90));
    bool saw_mem_resp = false;
    for (size_t cycle = 0; cycle < 8; ++cycle) {
      non_cacheable_bench_.AdvanceCycle();
      const auto queues = view.Queues();
      if (queues.mem_resp > 0) {
        saw_mem_resp = true;
        Expect(queues.core_resp == 0,
               "non-cacheable response does not bypass directly to core "
               "response queue");
        break;
      }
      if (queues.core_resp > 0 ||
          non_cacheable_bench_.core_.response_count() > response_count) {
        break;
      }
    }
    Expect(saw_mem_resp,
           "non-cacheable response first enters the shared memory response "
           "queue");

    Expect(non_cacheable_bench_.WaitForCoreResponses(response_count + 1),
           "non-cacheable memory response returns to core");
    Expect(non_cacheable_bench_.core_.HasResponse(non_cacheable_read),
           "non-cacheable core request receives its original payload back");
    Expect(PayloadData(non_cacheable_read) == Sequence(4, 0x90),
           "non-cacheable read response uses memory data directly");
    Expect(!view.CachedLine(0x20).has_value(),
           "non-cacheable response still does not install a cache line");
  }

  CacheBench read_bench_;
  CacheBench read_hit_bench_;
  CacheBench merge_bench_;
  CacheBench write_bench_;
  CacheBench write_no_allocate_bench_;
  CacheBench write_through_bench_;
  CacheBench dirty_victim_bench_;
  CacheBench non_cacheable_bench_;
  CacheBench arbitration_bench_;
};

}  // namespace

int sc_main(int, char *[]) {
  CacheModuleBasicTestRunner tester("tester");
  sc_core::sc_start();
  return tester.failed() ? 1 : 0;
}
