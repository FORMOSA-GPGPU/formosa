// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include <formosa-hal/api.h>
#include <formosa-hal/hal.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <iostream>
#include <thread>
#include <vector>

constexpr int kNumThreads = 10;
constexpr int kNumIterations = 10;

std::atomic<int> g_successful_requests{0};
std::atomic<int> g_failed_requests{0};
std::atomic<int> g_unexpected_failures{0};
std::atomic<bool> g_seen_full{false};

void worker() {
  uintptr_t dev_addr = 0x20000000;
  size_t data_size = 64;

  for (int i = 0; i < kNumIterations && !g_seen_full.load(); ++i) {
    FsaCompletionToken completion = 0;
    const FsaMemoryRange range = {dev_addr, data_size};
    const FsaCommandSubmitStatus ret =
        fsa_cmd_cache_invalidate(&range, &completion);
    if (ret == kFsaCommandSubmitAccepted) {
      g_successful_requests.fetch_add(1);
      (void)fsa_wait_completion(completion, 0, nullptr);
      continue;
    }

    g_failed_requests.fetch_add(1);
    // The mock LV server does not advance cmd_rd_ptr; cache invalidate/flush
    // requests eventually fill the Command Ring / Completion Pool.
    if (ret == kFsaCommandSubmitWouldBlock) {
      g_seen_full.store(true);
      std::cout << "[Race Condition Test] Thread " << std::this_thread::get_id()
                << " hit queue-full condition at iteration " << i << "."
                << std::endl;
    } else {
      g_unexpected_failures.fetch_add(1);
      g_seen_full.store(true);
      std::cerr << "[Race Condition Test] Thread " << std::this_thread::get_id()
                << " failed with unexpected error " << ret << " at iteration "
                << i << std::endl;
    }
    break;
  }
}

static bool check_allocations() {
  constexpr size_t count = 128, bytes = 64;
  std::vector<std::vector<void *>> allocations(kNumThreads,
                                               std::vector<void *>(count));
  std::vector<std::thread> workers;
  std::atomic<bool> failed{false};
  for (int t = 0; t < kNumThreads; t++)
    workers.emplace_back([&, t] {
      for (auto &address : allocations[t]) {
        const int result = fsa_malloc(&address, bytes);
        if (result != 0 || !address) failed = true;
      }
    });
  for (auto &thread : workers) thread.join();
  std::vector<uintptr_t> addresses;
  for (const auto &group : allocations)
    for (auto address : group)
      addresses.push_back(reinterpret_cast<uintptr_t>(address));
  std::sort(addresses.begin(), addresses.end());
  for (size_t i = 1; i < addresses.size(); i++)
    if (addresses[i] < addresses[i - 1] + bytes) failed = true;
  workers.clear();
  for (int t = 0; t < kNumThreads; t++)
    workers.emplace_back([&, t] {
      for (auto address : allocations[t]) {
        if (!address) continue;
        const int result = fsa_free(address);
        if (result != 0) failed = true;
      }
    });
  for (auto &thread : workers) thread.join();
  return !failed;
}

int main() {
  std::cout << "\n[Race Condition Test] Starting..." << std::endl;

  FsaDeviceDescription description = {};
  int ret = fsa_hal_init(&description);
  if (ret != 0) {
    std::cerr << "[Race Condition Test] fsa_hal_init() failed with error: "
              << ret << std::endl;
    return 1;
  }

  const bool allocations_valid = check_allocations();
  if (!allocations_valid) {
    std::cerr << "Concurrent allocations overlapped or failed" << std::endl;
    fsa_hal_cleanup();
    return 1;
  }
  std::vector<std::thread> threads;
  for (int i = 0; i < kNumThreads; ++i) {
    threads.emplace_back(worker);
  }

  for (auto &t : threads) {
    t.join();
  }

  int success = g_successful_requests.load();
  int failed = g_failed_requests.load();
  int unexpected = g_unexpected_failures.load();
  bool seen_full = g_seen_full.load();

  if (success <= 0) {
    std::cerr << "[Race Condition Test] Error: no successful cache invalidate "
                 "request observed."
              << std::endl;
    return 1;
  }
  if (!seen_full || failed <= 0) {
    std::cerr << "[Race Condition Test] Error: did not observe expected queue "
                 "full condition from cache invalidate/flush requests."
              << std::endl;
    return 1;
  }
  if (unexpected > 0) {
    std::cerr << "[Race Condition Test] Error: observed " << unexpected
              << " unexpected failures." << std::endl;
    return 1;
  }

  std::cout << "[Race Condition Test] Completed with expected queue-full "
               "behavior. successful="
            << success << " failed=" << failed << std::endl;
  return 0;
}
