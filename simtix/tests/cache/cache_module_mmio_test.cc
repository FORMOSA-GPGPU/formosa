// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include "cache_module_test_common.h"

namespace {

class CacheModuleMmioTestRunner : public CacheModuleTestRunnerBase {
 public:
  explicit CacheModuleMmioTestRunner(sc_core::sc_module_name name)
      : CacheModuleTestRunnerBase(name),
        mmio_flush_bench_("mmio_flush_bench",
                          MakeParam(WriteMissPolicy::kWriteAllocate,
                                    WriteHitPolicy::kWriteBack, 1, 2, 1, 4, 2)),
        mmio_invalidate_bench_(
            "mmio_invalidate_bench",
            MakeParam(WriteMissPolicy::kWriteAllocate,
                      WriteHitPolicy::kWriteBack, 1, 2, 1, 4, 2)),
        mmio_busy_bench_("mmio_busy_bench",
                         MakeParam(WriteMissPolicy::kWriteAllocate,
                                   WriteHitPolicy::kWriteBack, 1, 2, 1, 4, 2)),
        mmio_full_bench_("mmio_full_bench",
                         MakeParam(WriteMissPolicy::kWriteAllocate,
                                   WriteHitPolicy::kWriteBack, 1, 2, 1, 4, 2)),
        mmio_overflow_bench_(
            "mmio_overflow_bench",
            MakeParam(WriteMissPolicy::kWriteAllocate,
                      WriteHitPolicy::kWriteBack, 1, 2, 1, 4, 2)) {
    SC_THREAD(Run);
  }

 private:
  void Run() {
    mmio_flush_bench_.clock_.write(false);
    mmio_invalidate_bench_.clock_.write(false);
    mmio_busy_bench_.clock_.write(false);
    mmio_full_bench_.clock_.write(false);
    mmio_overflow_bench_.clock_.write(false);
    wait(sc_core::SC_ZERO_TIME);

    RunBench("mmio_flush_bench", [this] {
      TestMmioRangedFlushWritesBackDirtyLines();
    });
    RunBench("mmio_invalidate_bench", [this] {
      TestMmioRangedInvalidateDropsLinesWithoutWriteback();
    });
    RunBench("mmio_busy_bench", [this] {
      TestMmioBusyStopsCoreAdmission();
    });
    RunBench("mmio_full_bench", [this] {
      TestMmioFullFlushAndInvalidateScanWholeCache();
    });
    RunBench("mmio_overflow_bench", [this] {
      TestMmioOverflowingRangeIsFatal();
    });

    sc_core::sc_stop();
  }

  void TestMmioRangedFlushWritesBackDirtyLines() {
    CacheModuleTester view(mmio_flush_bench_.cache_);
    const auto block0 = Sequence(16, 0x10);
    const auto block1 = Sequence(16, 0x30);
    const auto block2 = Sequence(16, 0x50);
    const auto dirty0 =
        FillAndDirtyLine(mmio_flush_bench_, 0x00, block0, 4, Sequence(4, 0xA0));
    const auto dirty1 =
        FillAndDirtyLine(mmio_flush_bench_, 0x10, block1, 4, Sequence(4, 0xB0));
    const auto dirty2 =
        FillAndDirtyLine(mmio_flush_bench_, 0x20, block2, 4, Sequence(4, 0xC0));

    auto line = view.CachedLine(0x00);
    Expect(line.has_value() && line->tag.dirty,
           "ranged flush starts with first line dirty");
    line = view.CachedLine(0x10);
    Expect(line.has_value() && line->tag.dirty,
           "ranged flush starts with second line dirty");
    line = view.CachedLine(0x20);
    Expect(line.has_value() && line->tag.dirty,
           "ranged flush starts with out-of-range line dirty");

    const size_t writeback_begin = mmio_flush_bench_.memory_.request_count();
    StartMmioOperation(mmio_flush_bench_, kMmioFlushOp, 0x00, 0x20);
    Expect(mmio_flush_bench_.WaitForMemoryRequests(writeback_begin + 2, 64),
           "ranged flush writes back dirty lines in the requested range");
    ExpectWriteRequest(mmio_flush_bench_, writeback_begin, 0x00, dirty0,
                       "ranged flush writes back first dirty line",
                       AllEnabledMask(dirty0.size()));
    ExpectWriteRequest(mmio_flush_bench_, writeback_begin + 1, 0x10, dirty1,
                       "ranged flush writes back second dirty line",
                       AllEnabledMask(dirty1.size()));

    line = view.CachedLine(0x00);
    Expect(line.has_value(), "ranged flush keeps first line valid");
    if (line.has_value()) {
      Expect(line->data == dirty0, "ranged flush preserves first line data");
      Expect(!line->tag.dirty, "ranged flush cleans first line");
    }
    line = view.CachedLine(0x10);
    Expect(line.has_value(), "ranged flush keeps second line valid");
    if (line.has_value()) {
      Expect(line->data == dirty1, "ranged flush preserves second line data");
      Expect(!line->tag.dirty, "ranged flush cleans second line");
    }
    line = view.CachedLine(0x20);
    Expect(line.has_value(), "ranged flush keeps out-of-range line valid");
    if (line.has_value()) {
      Expect(line->data == dirty2,
             "ranged flush does not modify out-of-range line data");
      Expect(line->tag.dirty,
             "ranged flush leaves out-of-range dirty line dirty");
    }

    mmio_flush_bench_.memory_.RespondAt(writeback_begin);
    mmio_flush_bench_.memory_.RespondAt(writeback_begin + 1);
    Expect(WaitForMmioIdle(mmio_flush_bench_, 64),
           "ranged flush clears START after writebacks complete");
    Expect(mmio_flush_bench_.memory_.request_count() == writeback_begin + 2,
           "ranged flush does not write back out-of-range dirty lines");
  }

  void TestMmioOverflowingRangeIsFatal() {
    CacheModuleTester view(mmio_overflow_bench_.cache_);

    bool addition_overflow_threw = false;
    try {
      view.StartMmioOperation(kMmioFlushOp,
                              std::numeric_limits<uint64_t>::max() - 7, 16);
    } catch (const lv::fatal_error &) {
      addition_overflow_threw = true;
    }
    Expect(addition_overflow_threw,
           "MMIO range whose address plus size overflows is fatal");

    bool alignment_overflow_threw = false;
    try {
      view.StartMmioOperation(kMmioInvalidateOp,
                              std::numeric_limits<uint64_t>::max() - 15, 8);
    } catch (const lv::fatal_error &) {
      alignment_overflow_threw = true;
    }
    Expect(alignment_overflow_threw,
           "MMIO range whose block-aligned end overflows is fatal");
  }

  void TestMmioRangedInvalidateDropsLinesWithoutWriteback() {
    CacheModuleTester view(mmio_invalidate_bench_.cache_);
    const auto block0 = Sequence(16, 0x20);
    const auto block1 = Sequence(16, 0x40);
    const auto block2 = Sequence(16, 0x60);
    (void)FillAndDirtyLine(mmio_invalidate_bench_, 0x00, block0, 4,
                           Sequence(4, 0xA0));
    (void)FillAndDirtyLine(mmio_invalidate_bench_, 0x10, block1, 4,
                           Sequence(4, 0xB0));
    const auto dirty2 = FillAndDirtyLine(mmio_invalidate_bench_, 0x20, block2,
                                         4, Sequence(4, 0xC0));

    const size_t request_count = mmio_invalidate_bench_.memory_.request_count();
    StartMmioOperation(mmio_invalidate_bench_, kMmioInvalidateOp, 0x00, 0x20);
    Expect(WaitForMmioIdle(mmio_invalidate_bench_, 64),
           "ranged invalidate clears START");
    Expect(mmio_invalidate_bench_.memory_.request_count() == request_count,
           "ranged invalidate does not write back dirty lines");

    Expect(!view.CachedLine(0x00).has_value(),
           "ranged invalidate invalidates first in-range line");
    Expect(!view.CachedLine(0x10).has_value(),
           "ranged invalidate invalidates second in-range line");
    auto line = view.CachedLine(0x20);
    Expect(line.has_value(), "ranged invalidate keeps out-of-range line valid");
    if (line.has_value()) {
      Expect(line->data == dirty2,
             "ranged invalidate keeps out-of-range line data");
      Expect(line->tag.dirty,
             "ranged invalidate keeps out-of-range dirty state");
    }

    auto *read = mmio_invalidate_bench_.core_.SendRead(0x04, 4);
    (void)read;
    Expect(mmio_invalidate_bench_.WaitForMemoryRequests(request_count + 1, 32),
           "read after invalidated line misses to memory");
    ExpectReadRequest(mmio_invalidate_bench_, request_count, 0x00, 16,
                      "invalidated line is fetched again on later read");
  }

  void TestMmioBusyStopsCoreAdmission() {
    CacheModuleTester view(mmio_busy_bench_.cache_);
    const auto block = Sequence(16, 0x30);
    const auto dirty =
        FillAndDirtyLine(mmio_busy_bench_, 0x00, block, 4, Sequence(4, 0xD0));

    const size_t writeback_index = mmio_busy_bench_.memory_.request_count();
    StartMmioOperation(mmio_busy_bench_, kMmioFlushOp, 0x00, 0);
    Expect(mmio_busy_bench_.WaitForMemoryRequests(writeback_index + 1, 64),
           "full flush issues a writeback before completing");
    ExpectWriteRequest(mmio_busy_bench_, writeback_index, 0x00, dirty,
                       "busy test flush writes back the dirty line",
                       AllEnabledMask(dirty.size()));

    const size_t response_count = mmio_busy_bench_.core_.response_count();
    auto *blocked_read = mmio_busy_bench_.core_.SendRead(0x20, 4);
    for (size_t cycle = 0; cycle < 8; ++cycle) {
      mmio_busy_bench_.AdvanceCycle();
    }

    Expect(mmio_busy_bench_.memory_.request_count() == writeback_index + 1,
           "core read does not issue while MMIO operation is busy");
    Expect(mmio_busy_bench_.core_.response_count() == response_count,
           "core read does not respond while MMIO operation is busy");
    Expect(view.Queues().core_req == 0,
           "MMIO busy keeps new core request out of the cache pipeline");

    mmio_busy_bench_.memory_.RespondAt(writeback_index);
    Expect(WaitForMmioIdle(mmio_busy_bench_, 64),
           "busy test flush completes after writeback response");
    Expect(mmio_busy_bench_.WaitForMemoryRequests(writeback_index + 2, 64),
           "core read enters cache after MMIO operation completes");
    ExpectReadRequest(mmio_busy_bench_, writeback_index + 1, 0x20, 16,
                      "unblocked core read issues a normal refill request");
    (void)blocked_read;
  }

  void TestMmioFullFlushAndInvalidateScanWholeCache() {
    CacheModuleTester view(mmio_full_bench_.cache_);
    const auto block0 = Sequence(16, 0x40);
    const auto block1 = Sequence(16, 0x70);
    const auto dirty0 =
        FillAndDirtyLine(mmio_full_bench_, 0x00, block0, 4, Sequence(4, 0xA0));
    const auto dirty1 =
        FillAndDirtyLine(mmio_full_bench_, 0x10, block1, 4, Sequence(4, 0xB0));

    const size_t writeback_begin = mmio_full_bench_.memory_.request_count();
    StartMmioOperation(mmio_full_bench_, kMmioFlushOp, 0x80, 0);
    Expect(mmio_full_bench_.WaitForMemoryRequests(writeback_begin + 2, 64),
           "full flush scans all cache entries");
    ExpectWriteRequest(mmio_full_bench_, writeback_begin, 0x00, dirty0,
                       "full flush writes back first dirty line",
                       AllEnabledMask(dirty0.size()));
    ExpectWriteRequest(mmio_full_bench_, writeback_begin + 1, 0x10, dirty1,
                       "full flush writes back second dirty line",
                       AllEnabledMask(dirty1.size()));
    mmio_full_bench_.memory_.RespondAt(writeback_begin);
    mmio_full_bench_.memory_.RespondAt(writeback_begin + 1);
    Expect(WaitForMmioIdle(mmio_full_bench_, 64),
           "full flush completes after all writebacks");

    auto line = view.CachedLine(0x00);
    Expect(line.has_value(), "full flush keeps first line valid");
    if (line.has_value()) {
      Expect(!line->tag.dirty, "full flush cleans first line");
    }
    line = view.CachedLine(0x10);
    Expect(line.has_value(), "full flush keeps second line valid");
    if (line.has_value()) {
      Expect(!line->tag.dirty, "full flush cleans second line");
    }

    const size_t request_count = mmio_full_bench_.memory_.request_count();
    StartMmioOperation(mmio_full_bench_, kMmioInvalidateOp, 0x80, 0);
    Expect(WaitForMmioIdle(mmio_full_bench_, 64),
           "full invalidate completes after scanning all entries");
    Expect(mmio_full_bench_.memory_.request_count() == request_count,
           "full invalidate does not issue memory writes");
    Expect(!view.CachedLine(0x00).has_value(),
           "full invalidate removes first cached line");
    Expect(!view.CachedLine(0x10).has_value(),
           "full invalidate removes second cached line");
  }

  CacheBench mmio_flush_bench_;
  CacheBench mmio_invalidate_bench_;
  CacheBench mmio_busy_bench_;
  CacheBench mmio_full_bench_;
  CacheBench mmio_overflow_bench_;
};

}  // namespace

int sc_main(int, char *[]) {
  CacheModuleMmioTestRunner tester("tester");
  sc_core::sc_start();
  return tester.failed() ? 1 : 0;
}
