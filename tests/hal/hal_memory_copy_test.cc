// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include <addr_map/formosa_addr_map.h>
#include <formosa-hal/api.h>
#include <formosa-hal/hal.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

namespace {

constexpr size_t kCopySize = 37;
constexpr uintptr_t kOnchipAddress = FSA_ONCHIP_GMEM_BASE + 0x100;
constexpr uintptr_t kDdrAddress = FSA_GLOBAL_MEM_BASE + 0x02000100;

FsaMemoryCopyInfo make_copy(MemoryDomain src_domain, uint64_t src_addr,
                            MemoryDomain dst_domain, uint64_t dst_addr,
                            uint64_t size) {
  FsaMemoryCopyInfo info{};
  info.struct_size = sizeof(info);
  info.source.domain = src_domain;
  info.source.range.address = src_addr;
  info.source.range.size = size;
  info.destination.domain = dst_domain;
  info.destination.range.address = dst_addr;
  info.destination.range.size = size;
  return info;
}

FsaCommandSubmitStatus submit_copy(MemoryDomain src_domain, uint64_t src_addr,
                                   MemoryDomain dst_domain, uint64_t dst_addr,
                                   uint64_t size,
                                   FsaCompletionToken *completion) {
  FsaMemoryCopyInfo info =
      make_copy(src_domain, src_addr, dst_domain, dst_addr, size);
  return fsa_cmd_memory_copy(&info, completion);
}

bool submit_memory_copy(MemoryDomain src_domain, uintptr_t src_addr,
                        MemoryDomain dst_domain, uintptr_t dst_addr,
                        size_t size, FsaCompletionResult expected) {
  FsaCompletionToken completion = 0;
  const FsaCommandSubmitStatus submit_status = submit_copy(
      src_domain, src_addr, dst_domain, dst_addr, size, &completion);
  if (submit_status != kFsaCommandSubmitAccepted) return false;

  FsaCompletionResult result = FSA_COMPLETION_RESULT_PENDING;
  const FsaCompletionWaitStatus wait_status =
      fsa_wait_completion(completion, 60000, &result);
  const bool passed =
      wait_status == kFsaCompletionWaitSuccess && result == expected;
  if (!passed) {
    std::cerr << "memory copy outcome=" << static_cast<int>(result)
              << " wait_status=" << wait_status
              << " expected=" << static_cast<int>(expected) << "\n";
  }
  return passed;
}

bool run_host_to_device_copy(uintptr_t destination,
                             const std::array<uint8_t, kCopySize> &input) {
  return submit_memory_copy(kMemoryDomainHost,
                            reinterpret_cast<uintptr_t>(input.data()),
                            kMemoryDomainDevice, destination, input.size(),
                            FSA_COMPLETION_RESULT_SUCCESS);
}

bool run_device_to_host_copy(uintptr_t source,
                             std::array<uint8_t, kCopySize> *output) {
  return submit_memory_copy(kMemoryDomainDevice, source, kMemoryDomainHost,
                            reinterpret_cast<uintptr_t>(output->data()),
                            output->size(), FSA_COMPLETION_RESULT_SUCCESS);
}

bool run_copy(uintptr_t source, uintptr_t destination,
              const std::array<uint8_t, kCopySize> &input) {
  std::array<uint8_t, kCopySize> output{};
  if (!run_host_to_device_copy(source, input)) {
    std::cerr << "run_copy H2D failed src=0x" << std::hex << source << std::dec
              << "\n";
    return false;
  }

  if (!submit_memory_copy(kMemoryDomainDevice, source, kMemoryDomainDevice,
                          destination, input.size(),
                          FSA_COMPLETION_RESULT_SUCCESS)) {
    std::cerr << "run_copy D2D failed src=0x" << std::hex << source << " dst=0x"
              << destination << std::dec << "\n";
    return false;
  }
  if (!run_device_to_host_copy(destination, &output)) {
    std::cerr << "run_copy D2H failed dst=0x" << std::hex << destination
              << std::dec << "\n";
    return false;
  }
  return output == input;
}

}  // namespace

int main() {
  FsaDeviceDescription description = {};
  if (fsa_probe() != 0 || fsa_hal_init(&description) != 0) {
    std::cerr << "HAL initialization failed\n";
    return 1;
  }

  std::array<uint8_t, kCopySize> input{};
  for (size_t i = 0; i < input.size(); ++i) {
    input[i] = static_cast<uint8_t>((i * 13 + 7) & 0xff);
  }

  std::array<uint8_t, kCopySize> roundtrip{};
  if (!run_host_to_device_copy(kDdrAddress + 0x100, input) ||
      !run_device_to_host_copy(kDdrAddress + 0x100, &roundtrip) ||
      roundtrip != input) {
    std::cerr << "firmware-managed H2D copy failed\n";
    return 1;
  }

  void *noncache_address = nullptr;
  const int noncache_alloc_status =
      fsa_malloc_noncache(&noncache_address, kCopySize);
  const uintptr_t noncache_address_value =
      reinterpret_cast<uintptr_t>(noncache_address);
  const bool noncache_address_valid =
      noncache_alloc_status == 0 &&
      noncache_address_value >= FSA_NONCACHE_ALLOC_BASE &&
      noncache_address_value + kCopySize <=
          FSA_NONCACHE_ALLOC_BASE + FSA_NONCACHE_ALLOC_SIZE;
  if (!noncache_address_valid) {
    if (noncache_address != nullptr) fsa_free(noncache_address);
    std::cerr << "non-cache allocation failed\n";
    return 1;
  }
  std::array<uint8_t, kCopySize> noncache_roundtrip{};
  const bool noncache_roundtrip_passed =
      run_host_to_device_copy(noncache_address_value, input) &&
      run_device_to_host_copy(noncache_address_value, &noncache_roundtrip) &&
      noncache_roundtrip == input;
  const bool noncache_to_ddr_passed =
      run_copy(noncache_address_value, kDdrAddress + 0x200, input);
  const bool ddr_to_noncache_passed =
      run_copy(kDdrAddress + 0x300, noncache_address_value, input);
  fsa_free(noncache_address);
  if (!noncache_roundtrip_passed || !noncache_to_ddr_passed ||
      !ddr_to_noncache_passed) {
    std::cerr << "non-cache memory copy failed\n";
    return 1;
  }
  if (!run_copy(kOnchipAddress, kDdrAddress, input)) {
    std::cerr << "GMEM/DDR device-memory copy failed\n";
    return 1;
  }
  if (!run_copy(kDdrAddress, kOnchipAddress + 0x100, input)) {
    std::cerr << "DDR/GMEM device-memory copy failed\n";
    return 1;
  }

  // Keep several completion slots live at once.  This exercises the
  // completion pool independently of the command-ring slot that firmware
  // consumes while the first copy is still retiring.
  std::array<FsaCompletionToken, 8> outstanding{};
  for (size_t i = 0; i < outstanding.size(); ++i) {
    // Keep the completion-pool pressure test on the small on-chip aperture so
    // its latency is independent of the DRAM model.  DDR is covered by the
    // round-trip and cross-aperture checks above.
    if (submit_copy(
            kMemoryDomainHost, reinterpret_cast<uintptr_t>(input.data()),
            kMemoryDomainDevice, kOnchipAddress + 0x1000 + i * 0x40,
            input.size(), &outstanding[i]) != kFsaCommandSubmitAccepted) {
      std::cerr << "outstanding memory-copy submission failed\n";
      return 1;
    }
  }
  for (size_t i = 0; i < outstanding.size(); ++i) {
    const auto &completion = outstanding[i];
    FsaCompletionResult result = FSA_COMPLETION_RESULT_PENDING;
    const FsaCompletionWaitStatus wait_status =
        fsa_wait_completion(completion, 60000, &result);
    if (wait_status != kFsaCompletionWaitSuccess ||
        result != FSA_COMPLETION_RESULT_SUCCESS) {
      std::cerr << "outstanding completion " << i
                << " failed (status=" << wait_status
                << ", result=" << static_cast<int>(result) << ")\n";
      return 1;
    }
  }

  // Concurrent submitters must still produce valid, independently matched
  // records at the single HAL command-ring seam.
  std::mutex completion_mutex;
  std::vector<FsaCompletionToken> concurrent;
  std::vector<std::thread> submitters;
  for (size_t i = 0; i < 4; ++i) {
    submitters.emplace_back([&, i] {
      FsaCompletionToken completion = 0;
      if (submit_copy(kMemoryDomainHost,
                      reinterpret_cast<uintptr_t>(input.data()),
                      kMemoryDomainDevice, kOnchipAddress + 0x1800 + i * 0x40,
                      input.size(), &completion) == kFsaCommandSubmitAccepted) {
        std::lock_guard<std::mutex> lock(completion_mutex);
        concurrent.push_back(completion);
      }
    });
  }
  for (auto &submitter : submitters) submitter.join();
  if (concurrent.size() != 4) {
    std::cerr << "concurrent memory-copy submission failed\n";
    return 1;
  }
  for (const auto &completion : concurrent) {
    FsaCompletionResult result = FSA_COMPLETION_RESULT_PENDING;
    if (fsa_wait_completion(completion, 60000, &result) !=
            kFsaCompletionWaitSuccess ||
        result != FSA_COMPLETION_RESULT_SUCCESS) {
      std::cerr << "concurrent completion failed\n";
      return 1;
    }
  }

  // Poll leaves terminal ownership with the caller; release then invalidates
  // the token so a later poll cannot observe a recycled slot.
  FsaCompletionToken polled = 0;
  if (submit_copy(kMemoryDomainHost, reinterpret_cast<uintptr_t>(input.data()),
                  kMemoryDomainDevice, kOnchipAddress + 0x1c00, input.size(),
                  &polled) != kFsaCommandSubmitAccepted) {
    std::cerr << "polled memory-copy submission failed\n";
    return 1;
  }
  FsaCompletionResult polled_result = FSA_COMPLETION_RESULT_PENDING;
  FsaCompletionPollStatus polled_status = kFsaCompletionPollPending;
  for (size_t i = 0; i < 60000 && polled_status == kFsaCompletionPollPending;
       ++i) {
    polled_status = fsa_poll_completion(polled, &polled_result);
    if (polled_status == kFsaCompletionPollPending) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }
  if (polled_status != kFsaCompletionPollTerminal ||
      polled_result != FSA_COMPLETION_RESULT_SUCCESS ||
      fsa_release_completion(polled) != kFsaCompletionReleaseAccepted ||
      fsa_poll_completion(polled, nullptr) != kFsaCompletionPollInvalidToken) {
    std::cerr << "generic poll/release lifecycle failed\n";
    return 1;
  }

  // Fill the shared pool while letting firmware retire the commands.  The
  // next submission must report retryable backpressure, not publish a packet.
  std::array<FsaCompletionToken, FSA_COMPLETION_POOL_ENTRIES> pool_tokens{};
  size_t pool_token_count = 0;
  for (size_t attempts = 0;
       pool_token_count < pool_tokens.size() && attempts < 60000; ++attempts) {
    FsaCompletionToken token = 0;
    const FsaCommandSubmitStatus submit_status = submit_copy(
        kMemoryDomainHost, reinterpret_cast<uintptr_t>(input.data()),
        kMemoryDomainDevice, kOnchipAddress + 0x3000 + pool_token_count * 0x40,
        input.size(), &token);
    if (submit_status == kFsaCommandSubmitAccepted) {
      pool_tokens[pool_token_count++] = token;
    } else if (submit_status != kFsaCommandSubmitWouldBlock) {
      std::cerr << "completion-pool fill failed: " << submit_status << "\n";
      return 1;
    } else {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }
  if (pool_token_count != pool_tokens.size()) {
    std::cerr << "completion-pool fill timed out\n";
    return 1;
  }
  for (const FsaCompletionToken token : pool_tokens) {
    FsaCompletionResult result = FSA_COMPLETION_RESULT_PENDING;
    FsaCompletionPollStatus status = kFsaCompletionPollPending;
    for (size_t attempts = 0;
         attempts < 60000 && status == kFsaCompletionPollPending; ++attempts) {
      status = fsa_poll_completion(token, &result);
      if (status == kFsaCompletionPollPending) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
    }
    if (status != kFsaCompletionPollTerminal ||
        result != FSA_COMPLETION_RESULT_SUCCESS) {
      std::cerr << "completion-pool terminal poll failed\n";
      return 1;
    }
  }
  FsaCompletionToken rejected = 0;
  if (submit_copy(kMemoryDomainHost, reinterpret_cast<uintptr_t>(input.data()),
                  kMemoryDomainDevice, kOnchipAddress + 0x5000, input.size(),
                  &rejected) != kFsaCommandSubmitWouldBlock) {
    std::cerr << "completion-pool exhaustion was not retryable\n";
    return 1;
  }
  for (const FsaCompletionToken token : pool_tokens) {
    if (fsa_release_completion(token) != kFsaCompletionReleaseAccepted) {
      std::cerr << "completion-pool terminal release failed\n";
      return 1;
    }
  }

  /* Barrier Success after preceding copy + cache are terminal. */
  {
    FsaCompletionToken pred_copy = 0;
    FsaCompletionToken pred_cache = 0;
    FsaCompletionToken barrier = 0;
    const FsaMemoryRange cache_range = {kOnchipAddress + 0x6000, input.size()};
    if (submit_copy(kMemoryDomainHost,
                    reinterpret_cast<uintptr_t>(input.data()),
                    kMemoryDomainDevice, kOnchipAddress + 0x6000, input.size(),
                    &pred_copy) != kFsaCommandSubmitAccepted ||
        fsa_cmd_cache_flush(&cache_range, &pred_cache) !=
            kFsaCommandSubmitAccepted ||
        fsa_cmd_barrier(&barrier) != kFsaCommandSubmitAccepted) {
      std::cerr << "barrier predecessor submit failed\n";
      return 1;
    }
    FsaCompletionResult bar_result = FSA_COMPLETION_RESULT_PENDING;
    if (fsa_wait_completion(barrier, 60000, &bar_result) !=
            kFsaCompletionWaitSuccess ||
        bar_result != FSA_COMPLETION_RESULT_SUCCESS) {
      std::cerr << "barrier wait failed\n";
      return 1;
    }
    for (FsaCompletionToken t : {pred_copy, pred_cache}) {
      FsaCompletionResult r = FSA_COMPLETION_RESULT_PENDING;
      if (fsa_wait_completion(t, 60000, &r) != kFsaCompletionWaitSuccess ||
          r != FSA_COMPLETION_RESULT_SUCCESS) {
        std::cerr << "barrier predecessor wait failed\n";
        return 1;
      }
    }
  }

  /* A malformed cache packet still terminates its valid Host token. */
  {
    FsaCompletionToken invalid_cache = 0;
    const FsaMemoryRange invalid_range = {0, 0};
    if (fsa_cmd_cache_flush(&invalid_range, &invalid_cache) !=
        kFsaCommandSubmitAccepted) {
      std::cerr << "malformed cache submit failed\n";
      return 1;
    }
    FsaCompletionResult invalid_result = FSA_COMPLETION_RESULT_PENDING;
    if (fsa_wait_completion(invalid_cache, 60000, &invalid_result) !=
            kFsaCompletionWaitSuccess ||
        invalid_result != FSA_COMPLETION_RESULT_COMMAND_FAILURE_MIN) {
      std::cerr << "malformed cache did not terminate with command failure\n";
      return 1;
    }
  }

  /* Packet-width overflow is rejected before encoding. */
  {
    FsaKernelLaunchInfo overflow{};
    overflow.struct_size = sizeof(overflow);
    overflow.dimensions = 1;
    overflow.local_size[0] = static_cast<uint32_t>(UINT16_MAX) + 1u;
    overflow.num_groups[0] = 1;
    FsaCompletionToken invalid_kernel = 0xdead;
    if (fsa_cmd_start_kernel(&overflow, &invalid_kernel) !=
            kFsaCommandSubmitInvalidArgument ||
        invalid_kernel != 0) {
      std::cerr << "packet-width overflow was not rejected\n";
      return 1;
    }
  }

  if (!submit_memory_copy(kMemoryDomainDevice, 0, kMemoryDomainDevice,
                          kDdrAddress, 8, kMemoryCopyStatusInvalidRange)) {
    std::cerr << "invalid range was not propagated\n";
    return 1;
  }
  // WGI, the firmware stack pool, and MMIO/SM apertures are not device-memory
  // payload apertures.  They must be rejected before either physical engine
  // is started.
  if (!submit_memory_copy(kMemoryDomainDevice, FSA_GLOBAL_MEM_BASE,
                          kMemoryDomainDevice, kDdrAddress, 8,
                          kMemoryCopyStatusInvalidRange) ||
      !submit_memory_copy(kMemoryDomainDevice, FSA_MMIO_BASE,
                          kMemoryDomainDevice, kDdrAddress, 8,
                          kMemoryCopyStatusInvalidRange) ||
      !submit_memory_copy(kMemoryDomainDevice, FSA_SM_MMIO_BASE,
                          kMemoryDomainDevice, kDdrAddress, 8,
                          kMemoryCopyStatusInvalidRange)) {
    std::cerr << "control aperture was not rejected\n";
    return 1;
  }
  if (!submit_memory_copy(kMemoryDomainDevice,
                          FSA_NONCACHE_ALLOC_BASE + FSA_NONCACHE_ALLOC_SIZE - 4,
                          kMemoryDomainDevice, kDdrAddress, 8,
                          kMemoryCopyStatusInvalidRange) ||
      !submit_memory_copy(kMemoryDomainDevice, FSA_STACK_BASE,
                          kMemoryDomainDevice, kDdrAddress, 8,
                          kMemoryCopyStatusInvalidRange)) {
    std::cerr << "reserved DDR aperture was not rejected\n";
    return 1;
  }
  if (!submit_memory_copy(kMemoryDomainDevice,
                          FSA_GLOBAL_ALLOC_BASE + FSA_GLOBAL_ALLOC_SIZE - 4,
                          kMemoryDomainDevice, kDdrAddress, 8,
                          kMemoryCopyStatusInvalidRange)) {
    std::cerr << "aperture-crossing range was not rejected\n";
    return 1;
  }
  if (!submit_memory_copy(kMemoryDomainHost, 1, kMemoryDomainHost, 2, 8,
                          kMemoryCopyStatusInvalidDomainPair)) {
    std::cerr << "Host-to-Host domain pair was not rejected\n";
    return 1;
  }
  if (!submit_memory_copy(kMemoryDomainDevice, kOnchipAddress,
                          kMemoryDomainDevice, kOnchipAddress + 1, 8,
                          kMemoryCopyStatusOverlap)) {
    std::cerr << "overlapping D2D range was not rejected\n";
    return 1;
  }
  if (!submit_memory_copy(kMemoryDomainDevice, UINT64_MAX - 1,
                          kMemoryDomainDevice, kDdrAddress, 8,
                          kMemoryCopyStatusInvalidRange)) {
    std::cerr << "overflowing device range was not rejected\n";
    return 1;
  }

  std::cout << "HAL memory copy passed\n";
  return 0;
}
