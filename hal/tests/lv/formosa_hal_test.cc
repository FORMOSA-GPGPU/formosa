// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include <addr_map/formosa_addr_map.h>
#include <formosa-hal/api.h>
#include <formosa-hal/hal.h>
#include <real/real.h>

#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>

#include "gpufw.elf.h"

/* LV mmio_server test controls (see mmio_server.cc). */
constexpr uint64_t kEnableFailNextCmdPacket = 0x20000FF0ULL;
constexpr uint64_t kEnableFailNextWrPtr = 0x20000FF8ULL;
constexpr uint64_t kEnableFreeRingOnComplete = 0x20000FE8ULL;

template <typename T>
void test_return(T rc) {
  if (rc != 0) {
    std::cerr << "[FORMOSA HAL Test] Error: " << rc << std::endl;
    std::exit(static_cast<int>(rc));
  }
  std::cout << "[FORMOSA HAL Test] Success: " << rc << std::endl;
}

FsaCommandSubmitStatus submit_h2d(const void *host, uint64_t dest,
                                  uint64_t size, FsaCompletionToken *token) {
  FsaMemoryCopyInfo info{};
  info.struct_size = sizeof(info);
  info.source.domain = kMemoryDomainHost;
  info.source.range.address = reinterpret_cast<uintptr_t>(host);
  info.source.range.size = size;
  info.destination.domain = kMemoryDomainDevice;
  info.destination.range.address = dest;
  info.destination.range.size = size;
  return fsa_cmd_memory_copy(&info, token);
}

FsaCommandSubmitStatus submit_kernel(uint32_t local_mem, FsaDeviceAddress entry,
                                     FsaDeviceAddress kernarg,
                                     FsaDeviceAddress trampoline,
                                     FsaDeviceAddress status,
                                     FsaCompletionToken *token) {
  FsaKernelLaunchInfo info{};
  info.struct_size = sizeof(info);
  info.dimensions = 3;
  info.local_size[0] = 1;
  info.local_size[1] = 1;
  info.local_size[2] = 1;
  info.num_groups[0] = 1;
  info.num_groups[1] = 1;
  info.num_groups[2] = 1;
  info.local_mem_size = local_mem;
  info.has_printf_meta = 1;
  info.kernel_entry = entry;
  info.kernarg_address = kernarg;
  info.kernel_trampoline = trampoline;
  info.kernel_status = status;
  return fsa_cmd_start_kernel(&info, token);
}

bool same_description(const FsaDeviceDescription &lhs,
                      const FsaDeviceDescription &rhs) {
  return lhs.num_cores == rhs.num_cores &&
         lhs.max_threads_per_work_group == rhs.max_threads_per_work_group &&
         lhs.local_mem_size_per_core == rhs.local_mem_size_per_core &&
         lhs.cache_size == rhs.cache_size &&
         lhs.cache_line_size == rhs.cache_line_size &&
         lhs.global_mem_size == rhs.global_mem_size &&
         lhs.max_allocation_size == rhs.max_allocation_size;
}

int test_invalid_configuration() {
  const FsaDeviceDescription untouched = {
      11, 22, 33, 44, 55, 66, 77,
  };
  FsaDeviceDescription description = untouched;
  if (fsa_hal_init(&description) == 0 || fsa_hal_is_available() ||
      !same_description(description, untouched)) {
    std::cerr
        << "[FORMOSA HAL Test] Invalid configuration changed HAL state or "
           "copy-out"
        << std::endl;
    return 1;
  }
  return 0;
}

void test_fsa_probe() {
  std::cout << "\n[FORMOSA HAL Test] Calling fsa_probe()..." << std::endl;
  int ret = fsa_probe();
  test_return(ret);
}

void test_fsa_hal_init() {
  std::cout << "\n[FORMOSA HAL Test] Calling fsa_hal_init()..." << std::endl;
  if (std::getenv("LV_FORMOSA_FAIL_INIT_ONCE") != nullptr) {
    const FsaDeviceDescription untouched = {
        101, 202, 303, 404, 505, 606, 707,
    };
    FsaDeviceDescription failed_description = untouched;
    if (fsa_hal_init(&failed_description) == 0 || fsa_hal_is_available() ||
        !same_description(failed_description, untouched)) {
      std::cerr << "[FORMOSA HAL Test] Late init failure did not roll back"
                << std::endl;
      exit(-1);
    }
  }

  FsaDeviceDescription description = {};
  int ret = fsa_hal_init(&description);
  test_return(ret);
  if (!fsa_hal_is_available()) {
    std::cerr << "[FORMOSA HAL Test] HAL is not available after init"
              << std::endl;
    exit(-1);
  }
  if (description.num_cores != 1 ||
      description.max_threads_per_work_group != 64 ||
      description.local_mem_size_per_core != 65536 ||
      description.cache_size != 1048576 || description.cache_line_size != 64 ||
      description.global_mem_size != 2147483648ULL ||
      description.max_allocation_size != 2113929216ULL) {
    std::cerr << "[FORMOSA HAL Test] Unexpected Device Description"
              << std::endl;
    exit(-1);
  }
}

void test_fsa_hal_occupancy() {
  uint64_t max_local_mem = 0;
  if (fsa_hal_check_occupancy(65, 0, &max_local_mem) == 0) {
    std::cerr << "[FORMOSA HAL Test] Invalid occupancy was accepted"
              << std::endl;
    exit(-1);
  }
  if (fsa_hal_check_occupancy(64, 0, &max_local_mem) != 0 ||
      max_local_mem != 65536) {
    std::cerr << "[FORMOSA HAL Test] Valid occupancy was rejected" << std::endl;
    exit(-1);
  }
}

void test_semantic_descriptor_validation() {
  std::cout << "\n[FORMOSA HAL Test] semantic descriptor validation..."
            << std::endl;
  FsaCompletionToken token = 0xdead;
  FsaKernelLaunchInfo kernel{};
  kernel.struct_size = sizeof(kernel);
  kernel.dimensions = 1;
  kernel.local_size[0] = 1;
  kernel.num_groups[0] = 1;
  if (fsa_cmd_start_kernel(nullptr, &token) !=
          kFsaCommandSubmitInvalidArgument ||
      token != 0) {
    std::cerr << "[FORMOSA HAL Test] null kernel info was accepted\n";
    exit(-1);
  }
  token = 0xdead;
  kernel.struct_size = sizeof(kernel) - 1;
  if (fsa_cmd_start_kernel(&kernel, &token) !=
          kFsaCommandSubmitInvalidArgument ||
      token != 0) {
    std::cerr << "[FORMOSA HAL Test] wrong kernel struct_size was accepted\n";
    exit(-1);
  }
  kernel.struct_size = sizeof(kernel);
  kernel.dimensions = 0;
  token = 0xdead;
  if (fsa_cmd_start_kernel(&kernel, &token) !=
      kFsaCommandSubmitInvalidArgument) {
    std::cerr << "[FORMOSA HAL Test] dimension 0 was accepted\n";
    exit(-1);
  }
  kernel.dimensions = 4;
  if (fsa_cmd_start_kernel(&kernel, &token) !=
      kFsaCommandSubmitInvalidArgument) {
    std::cerr << "[FORMOSA HAL Test] dimension 4 was accepted\n";
    exit(-1);
  }
  kernel.dimensions = 1;
  kernel.local_size[0] = static_cast<uint32_t>(UINT16_MAX) + 1u;
  if (fsa_cmd_start_kernel(&kernel, &token) !=
      kFsaCommandSubmitInvalidArgument) {
    std::cerr << "[FORMOSA HAL Test] packet-width overflow was accepted\n";
    exit(-1);
  }
  kernel.local_size[0] = 0;
  if (fsa_cmd_start_kernel(&kernel, &token) !=
      kFsaCommandSubmitInvalidArgument) {
    std::cerr << "[FORMOSA HAL Test] zero local size was accepted\n";
    exit(-1);
  }
  kernel.local_size[0] = 1;
  kernel.local_size[1] = UINT32_MAX;
  kernel.local_size[2] = UINT32_MAX;
  kernel.num_groups[1] = UINT32_MAX;
  kernel.num_groups[2] = UINT32_MAX;
  kernel.global_offset[1] = UINT64_MAX;
  kernel.global_offset[2] = UINT64_MAX;
  kernel.kernel_entry = 0;
  kernel.has_printf_meta = 1;
  kernel.enable_stack_remap = 1;
  if (fsa_cmd_start_kernel(&kernel, &token) != kFsaCommandSubmitAccepted) {
    std::cerr << "[FORMOSA HAL Test] 1D launch with unused garbage rejected\n";
    exit(-1);
  }
  FsaCompletionResult result = FSA_COMPLETION_RESULT_PENDING;
  test_return(fsa_wait_completion(token, 100, &result));

  FsaMemoryCopyInfo copy{};
  copy.struct_size = sizeof(copy) + 1;
  copy.source.domain = kMemoryDomainHost;
  copy.source.range.size = 4;
  copy.destination.domain = kMemoryDomainDevice;
  copy.destination.range.size = 4;
  token = 0xdead;
  if (fsa_cmd_memory_copy(&copy, &token) != kFsaCommandSubmitInvalidArgument ||
      token != 0) {
    std::cerr << "[FORMOSA HAL Test] wrong copy struct_size was accepted\n";
    exit(-1);
  }
  copy.struct_size = sizeof(copy);
  copy.destination.range.size = 8;
  token = 0xdead;
  if (fsa_cmd_memory_copy(&copy, &token) != kFsaCommandSubmitInvalidArgument ||
      token != 0) {
    std::cerr << "[FORMOSA HAL Test] mismatched copy sizes were accepted\n";
    exit(-1);
  }
  token = 0xdead;
  if (fsa_cmd_cache_flush(nullptr, &token) !=
          kFsaCommandSubmitInvalidArgument ||
      token != 0) {
    std::cerr << "[FORMOSA HAL Test] null cache flush range was accepted\n";
    exit(-1);
  }
  token = 0xdead;
  if (fsa_cmd_cache_invalidate(nullptr, &token) !=
          kFsaCommandSubmitInvalidArgument ||
      token != 0) {
    std::cerr
        << "[FORMOSA HAL Test] null cache invalidate range was accepted\n";
    exit(-1);
  }
}

void test_fsa_generation_and_faults() {
  uint8_t input[8] = {0, 1, 2, 3, 4, 5, 6, 7};
  FsaCompletionToken completion = 0;
  test_return(submit_h2d(input, 0x00100100, sizeof(input), &completion));

  const uint16_t first_generation =
      fsa_completion_token_boot_generation(completion);
  if (first_generation == 0) {
    std::cerr << "[FORMOSA HAL Test] Missing boot generation" << std::endl;
    exit(-1);
  }

  /* A reset must advance the firmware generation before READY is exposed. */
  test_return(fsa_hal_reset(100));
  FsaCompletionToken new_completion = 0;
  test_return(submit_h2d(input, 0x00100140, sizeof(input), &new_completion));
  if (fsa_completion_token_boot_generation(new_completion) ==
      first_generation) {
    std::cerr << "[FORMOSA HAL Test] Boot generation did not advance"
              << std::endl;
    exit(-1);
  }
  FsaCompletionResult new_result = FSA_COMPLETION_RESULT_PENDING;
  test_return(fsa_wait_completion(new_completion, 100, &new_result));
  if (new_result != FSA_COMPLETION_RESULT_SUCCESS) {
    std::cerr << "[FORMOSA HAL Test] New completion failed" << std::endl;
    exit(-1);
  }

  FsaCompletionResult result = FSA_COMPLETION_RESULT_PENDING;
  FsaCompletionPollStatus status = fsa_poll_completion(completion, &result);
  if (status != kFsaCompletionPollTerminal ||
      result != FSA_COMPLETION_RESULT_FIRMWARE_REBOOT) {
    std::cerr << "[FORMOSA HAL Test] Stale completion was accepted"
              << std::endl;
    exit(-1);
  }
  test_return(fsa_release_completion(completion));

  /* A firmware fault is not itself a confirmed reset.  The pending completion
   * remains pending until reset establishes a new Boot Generation. */
  FsaCompletionToken fault_completion = 0;
  test_return(submit_h2d(input, 0x00100180, sizeof(input), &fault_completion));
  FsaCompletionSlot pending = fsa_completion_slot_encode(
      fsa_completion_token_alloc_tag(fault_completion),
      FSA_COMPLETION_RESULT_PENDING);
  const uintptr_t pending_address =
      FSA_COMPLETION_POOL_BASE +
      fsa_completion_token_slot_index(fault_completion) *
          FSA_COMPLETION_SLOT_BYTES;
  test_return(
      fsa_real_copy_to_scratchpad(pending_address, &pending, sizeof(pending)));
  test_return(fsa_real_mmio(FSA_CP_OFF_FW_FAULT_CODE, kFirmwareFaultDmaProtocol,
                            nullptr));
  test_return(
      fsa_real_mmio(FSA_CP_OFF_FW_STATUS, kFirmwareStatusFault, nullptr));

  result = FSA_COMPLETION_RESULT_PENDING;
  status = fsa_poll_completion(fault_completion, &result);
  if (status != kFsaCompletionPollPending ||
      result != FSA_COMPLETION_RESULT_PENDING ||
      fsa_release_completion(fault_completion) !=
          kFsaCompletionReleasePending) {
    std::cerr << "[FORMOSA HAL Test] Fault changed pending completion"
              << std::endl;
    exit(-1);
  }

  test_return(
      fsa_real_mmio(FSA_CP_OFF_FW_FAULT_CODE, kFirmwareFaultNone, nullptr));
  test_return(
      fsa_real_mmio(FSA_CP_OFF_FW_STATUS, kFirmwareStatusReady, nullptr));
  test_return(fsa_hal_reset(100));
  result = FSA_COMPLETION_RESULT_PENDING;
  status = fsa_poll_completion(fault_completion, &result);
  if (status != kFsaCompletionPollTerminal ||
      result != FSA_COMPLETION_RESULT_FIRMWARE_REBOOT) {
    std::cerr << "[FORMOSA HAL Test] Reset did not invalidate fault token"
              << std::endl;
    exit(-1);
  }
  test_return(fsa_release_completion(fault_completion));
}

void test_fsa_cmd_start_kernel() {
  std::cout << "\n[FORMOSA HAL Test] Calling fsa_cmd_start_kernel()..."
            << std::endl;
  FsaCompletionToken completion = 0;
  const FsaCommandSubmitStatus ret =
      submit_kernel(0, 0xCAFECAFE, 0xDEADBEEF, 0xBEEFCAFE, 0, &completion);
  test_return(ret);
}

void test_fsa_cmd_ring_control() {
  std::cout << "\n[FORMOSA HAL Test] Testing Command Ring control..."
            << std::endl;
  const uint32_t local_mem_size = 1048;

  // The simulated Command Ring size is 8 packets (from mmio_server.cc)
  int num_packets = 8;
  std::cout << "[FORMOSA HAL Test] Command Ring can hold " << num_packets
            << " packets" << std::endl;

  // Fill the Command Ring from current runtime state.
  int successful_dispatches = 0;
  bool seen_full = false;
  for (int i = 0; i < num_packets + 2;
       ++i) {  // Try to send more than the ring size
    FsaCompletionToken completion = 0;
    const FsaCommandSubmitStatus ret = submit_kernel(
        local_mem_size, 0xCAFECAFE, 0xDEADBEEF, 0xBEEFCAFE, 0, &completion);
    if (ret == kFsaCommandSubmitAccepted) {
      if (seen_full) {
        std::cout << "[FORMOSA HAL Test] Error: Dispatch succeeded after queue "
                     "was reported full."
                  << std::endl;
        exit(-1);
      }
      successful_dispatches++;
      std::cout << "[FORMOSA HAL Test] Successfully dispatched kernel " << i + 1
                << std::endl;
    } else {
      seen_full = true;
      std::cout << "[FORMOSA HAL Test] Failed to dispatch kernel " << i + 1
                << ": " << ret << std::endl;
    }
  }

  if (successful_dispatches <= 0 || !seen_full) {
    std::cout << "[FORMOSA HAL Test] Error: Command buffer control did not "
                 "reach expected full condition."
              << " successful_dispatches=" << successful_dispatches
              << " seen_full=" << seen_full << std::endl;
    exit(-1);
  }

  // Verify that subsequent calls fail
  FsaCompletionToken completion = 0;
  const FsaCommandSubmitStatus ret = submit_kernel(
      local_mem_size, 0xCAFECAFE, 0xDEADBEEF, 0xBEEFCAFE, 0, &completion);
  if (ret == kFsaCommandSubmitWouldBlock) {
    std::cout << "[FORMOSA HAL Test] Subsequent dispatch failed as expected "
                 "(buffer full)."
              << std::endl;
  } else {
    std::cout << "[FORMOSA HAL Test] Error: Subsequent dispatch did not fail "
                 "as expected."
              << std::endl;
    exit(-1);
  }
}

void test_fsa_reboot_failure_is_fail_stop() {
  uint8_t input[4] = {0};
  FsaCompletionToken ambiguous = 0;
  test_return(submit_h2d(input, 0x00100600, sizeof(input), &ambiguous));
  const FsaCompletionSlot pending = fsa_completion_slot_encode(
      fsa_completion_token_alloc_tag(ambiguous), FSA_COMPLETION_RESULT_PENDING);
  const uintptr_t pending_address =
      FSA_COMPLETION_POOL_BASE +
      fsa_completion_token_slot_index(ambiguous) * FSA_COMPLETION_SLOT_BYTES;
  test_return(
      fsa_real_copy_to_scratchpad(pending_address, &pending, sizeof(pending)));

  test_return(
      fsa_real_mmio(FSA_CP_OFF_FW_STATUS, kFirmwareStatusBooting, nullptr));
  if (fsa_hal_reset(1) == 0) {
    std::cerr << "[FORMOSA HAL Test] Expected reboot timeout" << std::endl;
    exit(-1);
  }
  if (fsa_poll_completion(ambiguous, nullptr) !=
          kFsaCompletionPollTransportError ||
      fsa_release_completion(ambiguous) !=
          kFsaCompletionReleaseTransportError) {
    std::cerr << "[FORMOSA HAL Test] Failed reset released ambiguous token"
              << std::endl;
    exit(-1);
  }

  test_return(
      fsa_real_mmio(FSA_CP_OFF_FW_STATUS, kFirmwareStatusReady, nullptr));
  if (fsa_hal_reset(100) == 0 || fsa_hal_is_available()) {
    std::cerr << "[FORMOSA HAL Test] Failed reboot did not latch fail-stop"
              << std::endl;
    exit(-1);
  }
}

void test_fsa_include_gpufw() {
  if (!gpufw_elf || gpufw_elf_len == 0) {
    std::cout << "[FORMOSA HAL Test] Cannot find gpufw_elf definition"
              << std::endl;
    exit(-1);
  }
}

void test_alloc_generation_wrap_rules() {
  std::cout << "\n[FORMOSA HAL Test] alloc_generation wrap / slot reuse..."
            << std::endl;
  if (fsa_completion_next_alloc_generation(0) !=
          FSA_COMPLETION_ALLOC_GENERATION_INITIAL ||
      fsa_completion_next_alloc_generation(UINT32_MAX) !=
          FSA_COMPLETION_ALLOC_GENERATION_INITIAL ||
      fsa_completion_next_alloc_generation(1) != 2 ||
      fsa_completion_next_alloc_generation(UINT32_MAX - 1) != UINT32_MAX) {
    std::cerr << "[FORMOSA HAL Test] alloc_generation wrap rules failed\n";
    exit(-1);
  }
  /* token_make rewrites alloc_generation 0 to INITIAL. */
  const FsaCompletionToken t = fsa_completion_token_make(1, 0, /*slot*/ 0);
  if (fsa_completion_token_alloc_generation(t) !=
      FSA_COMPLETION_ALLOC_GENERATION_INITIAL) {
    std::cerr << "[FORMOSA HAL Test] token_make did not skip zero alloc\n";
    exit(-1);
  }
  /* Boot generation uses low 16 bits modularly. */
  const FsaCompletionToken wrap = fsa_completion_token_make(0x10001, 1, 0);
  if (fsa_completion_token_boot_generation(wrap) != 1) {
    std::cerr << "[FORMOSA HAL Test] boot generation modular wrap failed\n";
    exit(-1);
  }

  /* Exercise the public allocator path as well as the packing helper.  A
   * released slot is reused with a new Alloc Generation. */
  uint8_t input[4] = {0};
  FsaCompletionToken first = 0;
  FsaCompletionToken reused = 0;
  FsaCompletionResult result = FSA_COMPLETION_RESULT_PENDING;
  if (submit_h2d(input, 0x00100020, sizeof(input), &first) !=
          kFsaCommandSubmitAccepted ||
      fsa_wait_completion(first, 100, &result) != kFsaCompletionWaitSuccess ||
      result != FSA_COMPLETION_RESULT_SUCCESS ||
      submit_h2d(input, 0x00100040, sizeof(input), &reused) !=
          kFsaCommandSubmitAccepted ||
      fsa_wait_completion(reused, 100, &result) != kFsaCompletionWaitSuccess ||
      result != FSA_COMPLETION_RESULT_SUCCESS ||
      fsa_completion_token_slot_index(first) !=
          fsa_completion_token_slot_index(reused) ||
      fsa_completion_token_alloc_generation(reused) !=
          fsa_completion_token_alloc_generation(first) + 1) {
    std::cerr << "[FORMOSA HAL Test] public slot reuse did not advance "
                 "alloc_generation\n";
    exit(-1);
  }
}

void test_wait_poll_release_timeout_lifecycle() {
  std::cout << "\n[FORMOSA HAL Test] wait/poll/release/timeout lifecycle..."
            << std::endl;
  uint8_t input[8] = {1, 2, 3, 4, 5, 6, 7, 8};

  /* Wait auto-releases: use-after-wait is invalid. */
  FsaCompletionToken waited = 0;
  test_return(submit_h2d(input, 0x00100200, sizeof(input), &waited));
  FsaCompletionResult wr = FSA_COMPLETION_RESULT_PENDING;
  test_return(fsa_wait_completion(waited, 100, &wr));
  if (wr != FSA_COMPLETION_RESULT_SUCCESS ||
      fsa_poll_completion(waited, nullptr) != kFsaCompletionPollInvalidToken ||
      fsa_release_completion(waited) != kFsaCompletionReleaseInvalidToken) {
    std::cerr << "[FORMOSA HAL Test] wait did not auto-release\n";
    exit(-1);
  }

  /* Force Pending: submit (mock completes), rewrite slot Pending, then timeout.
   */
  FsaCompletionToken pending = 0;
  const FsaCommandSubmitStatus sub =
      submit_h2d(input, 0x00100240, sizeof(input), &pending);
  if (sub != kFsaCommandSubmitAccepted || pending == 0) {
    std::cerr << "[FORMOSA HAL Test] pending submit failed st=" << sub
              << " token=0x" << std::hex << pending << std::dec << "\n";
    exit(-1);
  }
  const FsaCompletionAllocTag tag = fsa_completion_token_alloc_tag(pending);
  const FsaCompletionSlot pending_slot =
      fsa_completion_slot_encode(tag, FSA_COMPLETION_RESULT_PENDING);
  const uintptr_t slot_addr =
      FSA_COMPLETION_POOL_BASE +
      static_cast<uintptr_t>(fsa_completion_token_slot_index(pending)) *
          FSA_COMPLETION_SLOT_BYTES;
  if (tag == 0 || pending_slot == 0) {
    std::cerr << "[FORMOSA HAL Test] bad token encode token=0x" << std::hex
              << pending << " tag=0x" << tag << std::dec << "\n";
    exit(-1);
  }
  if (fsa_real_copy_to_scratchpad(slot_addr, &pending_slot,
                                  sizeof(pending_slot)) != 0) {
    std::cerr << "[FORMOSA HAL Test] pending slot write failed\n";
    exit(-1);
  }
  FsaCompletionResult forced = FSA_COMPLETION_RESULT_SUCCESS;
  const FsaCompletionPollStatus ps0 = fsa_poll_completion(pending, &forced);
  if (ps0 != kFsaCompletionPollPending ||
      forced != FSA_COMPLETION_RESULT_PENDING) {
    std::cerr << "[FORMOSA HAL Test] could not force Pending slot (poll=" << ps0
              << " result=" << static_cast<int>(forced) << " token=0x"
              << std::hex << pending << " tag=0x" << tag << " addr=0x"
              << slot_addr << " word=0x" << pending_slot << std::dec << ")\n";
    exit(-1);
  }

  FsaCompletionResult timed = FSA_COMPLETION_RESULT_PENDING;
  const FsaCompletionWaitStatus ws = fsa_wait_completion(pending, 5, &timed);
  if (ws != kFsaCompletionWaitTimeout) {
    std::cerr << "[FORMOSA HAL Test] expected wait timeout, got " << ws
              << " result=" << static_cast<int>(timed) << "\n";
    exit(-1);
  }
  FsaCompletionResult still = FSA_COMPLETION_RESULT_PENDING;
  if (fsa_poll_completion(pending, &still) != kFsaCompletionPollPending ||
      still != FSA_COMPLETION_RESULT_PENDING) {
    std::cerr << "[FORMOSA HAL Test] timeout released or completed slot\n";
    exit(-1);
  }
  if (fsa_release_completion(pending) != kFsaCompletionReleasePending) {
    std::cerr << "[FORMOSA HAL Test] pending release must fail\n";
    exit(-1);
  }

  const FsaCompletionSlot success_slot =
      fsa_completion_slot_encode(tag, FSA_COMPLETION_RESULT_SUCCESS);
  if (fsa_real_copy_to_scratchpad(slot_addr, &success_slot,
                                  sizeof(success_slot)) != 0) {
    std::cerr << "[FORMOSA HAL Test] success slot write failed\n";
    exit(-1);
  }
  FsaCompletionResult pr = FSA_COMPLETION_RESULT_PENDING;
  if (fsa_poll_completion(pending, &pr) != kFsaCompletionPollTerminal ||
      pr != FSA_COMPLETION_RESULT_SUCCESS ||
      fsa_release_completion(pending) != kFsaCompletionReleaseAccepted ||
      fsa_poll_completion(pending, nullptr) != kFsaCompletionPollInvalidToken) {
    std::cerr << "[FORMOSA HAL Test] poll/release lifecycle failed\n";
    exit(-1);
  }
}

void test_pool_exhaustion_and_reset_reuse() {
  std::cout << "\n[FORMOSA HAL Test] 64-slot exhaustion + reset reuse..."
            << std::endl;
  uint64_t control_value = 1;
  test_return(fsa_real_copy_to_scratchpad(
      kEnableFreeRingOnComplete, &control_value, sizeof(control_value)));
  uint8_t input[4] = {9, 8, 7, 6};
  std::array<FsaCompletionToken, FSA_COMPLETION_POOL_ENTRIES> tokens{};
  for (size_t i = 0; i < tokens.size(); ++i) {
    if (submit_h2d(input, 0x00101000 + i * 0x20, sizeof(input), &tokens[i]) !=
        kFsaCommandSubmitAccepted) {
      std::cerr << "[FORMOSA HAL Test] pool fill failed at " << i << "\n";
      exit(-1);
    }
  }
  FsaCompletionToken rejected = 0;
  if (submit_h2d(input, 0x00102000, sizeof(input), &rejected) !=
          kFsaCommandSubmitWouldBlock ||
      rejected != 0) {
    std::cerr << "[FORMOSA HAL Test] expected WouldBlock backpressure\n";
    exit(-1);
  }
  test_return(fsa_hal_reset(1000));
  FsaCompletionToken reuse = 0;
  if (submit_h2d(input, 0x00102040, sizeof(input), &reuse) !=
      kFsaCommandSubmitAccepted) {
    std::cerr << "[FORMOSA HAL Test] reset did not reclaim completion pool\n";
    exit(-1);
  }
  FsaCompletionResult rr = FSA_COMPLETION_RESULT_PENDING;
  test_return(fsa_wait_completion(reuse, 100, &rr));
  for (FsaCompletionToken token : tokens) {
    FsaCompletionResult result = FSA_COMPLETION_RESULT_PENDING;
    if (fsa_poll_completion(token, &result) != kFsaCompletionPollTerminal ||
        result != FSA_COMPLETION_RESULT_FIRMWARE_REBOOT ||
        fsa_release_completion(token) != kFsaCompletionReleaseAccepted) {
      std::cerr << "[FORMOSA HAL Test] reset-stale completion failed\n";
      exit(-1);
    }
  }
  control_value = 0;
  test_return(fsa_real_copy_to_scratchpad(
      kEnableFreeRingOnComplete, &control_value, sizeof(control_value)));
}

void test_barrier_terminal_with_predecessors() {
  std::cout << "\n[FORMOSA HAL Test] barrier after kernel/cache/copy..."
            << std::endl;
  uint8_t input[8] = {0};
  FsaCompletionToken k = 0, c = 0, m = 0, b = 0;
  const FsaMemoryRange cache_range = {0x00100000, 64};
  if (submit_kernel(0, 0x1, 0x2, 0x3, 0, &k) != kFsaCommandSubmitAccepted ||
      fsa_cmd_cache_flush(&cache_range, &c) != kFsaCommandSubmitAccepted ||
      submit_h2d(input, 0x00100300, sizeof(input), &m) !=
          kFsaCommandSubmitAccepted ||
      fsa_cmd_barrier(&b) != kFsaCommandSubmitAccepted) {
    std::cerr << "[FORMOSA HAL Test] barrier predecessor submit failed\n";
    exit(-1);
  }
  FsaCompletionResult br = FSA_COMPLETION_RESULT_PENDING;
  if (fsa_wait_completion(b, 100, &br) != kFsaCompletionWaitSuccess ||
      br != FSA_COMPLETION_RESULT_SUCCESS) {
    std::cerr << "[FORMOSA HAL Test] barrier wait failed\n";
    exit(-1);
  }
  for (FsaCompletionToken t : {k, c, m}) {
    FsaCompletionResult r = FSA_COMPLETION_RESULT_PENDING;
    if (fsa_wait_completion(t, 100, &r) != kFsaCompletionWaitSuccess ||
        r != FSA_COMPLETION_RESULT_SUCCESS) {
      std::cerr << "[FORMOSA HAL Test] predecessor wait failed\n";
      exit(-1);
    }
  }
}

void test_pre_wp_failure_rolls_back_slot() {
  std::cout << "\n[FORMOSA HAL Test] pre-WP failure rolls back slot..."
            << std::endl;
  uint64_t control_value = 1;
  test_return(fsa_real_copy_to_scratchpad(
      kEnableFailNextCmdPacket, &control_value, sizeof(control_value)));
  uint8_t input[4] = {1, 2, 3, 4};
  FsaCompletionToken t = 0xdead;
  const FsaCommandSubmitStatus st =
      submit_h2d(input, 0x00100400, sizeof(input), &t);
  if (st != kFsaCommandSubmitTransportError || t != 0) {
    std::cerr << "[FORMOSA HAL Test] pre-WP expected TransportError + token 0, "
                 "got st="
              << st << " t=0x" << std::hex << t << std::dec << "\n";
    exit(-1);
  }
  if (!fsa_hal_is_available()) {
    std::cerr << "[FORMOSA HAL Test] pre-WP failure latched fail-stop\n";
    exit(-1);
  }
  test_return(fsa_hal_reset(1000));
  if (!fsa_hal_is_available()) {
    std::cerr << "[FORMOSA HAL Test] reset did not recover after pre-WP\n";
    exit(-1);
  }
  FsaCompletionToken ok = 0;
  test_return(submit_h2d(input, 0x00100440, sizeof(input), &ok));
  FsaCompletionResult r = FSA_COMPLETION_RESULT_PENDING;
  test_return(fsa_wait_completion(ok, 100, &r));
}

void test_wp_failure_preserves_slot_fail_stop() {
  std::cout << "\n[FORMOSA HAL Test] WP failure preserves slot + fail-stop..."
            << std::endl;
  uint64_t control_value = 1;
  test_return(fsa_real_copy_to_scratchpad(kEnableFailNextWrPtr, &control_value,
                                          sizeof(control_value)));
  uint8_t input[4] = {5, 6, 7, 8};
  FsaCompletionToken t = 0;
  const FsaCommandSubmitStatus st =
      submit_h2d(input, 0x00100500, sizeof(input), &t);
  if (st != kFsaCommandSubmitTransportError || t == 0) {
    std::cerr << "[FORMOSA HAL Test] WP fail expected TransportError + owned "
                 "token, st="
              << st << " t=0x" << std::hex << t << std::dec << "\n";
    exit(-1);
  }
  if (fsa_hal_is_available()) {
    std::cerr << "[FORMOSA HAL Test] WP fail should fail-stop\n";
    exit(-1);
  }
  /* Fail-stop: lifecycle ops surface transport error; slot stays owned. */
  if (fsa_release_completion(t) != kFsaCompletionReleaseTransportError ||
      fsa_poll_completion(t, nullptr) != kFsaCompletionPollTransportError) {
    std::cerr << "[FORMOSA HAL Test] expected transport error on owned token\n";
    exit(-1);
  }

  /* Reset: stale owned token observes FirmwareReboot, then release frees it. */
  test_return(fsa_hal_reset(1000));
  FsaCompletionResult stale = FSA_COMPLETION_RESULT_PENDING;
  const FsaCompletionPollStatus ps = fsa_poll_completion(t, &stale);
  if (ps == kFsaCompletionPollTerminal &&
      stale == FSA_COMPLETION_RESULT_FIRMWARE_REBOOT) {
    if (fsa_release_completion(t) != kFsaCompletionReleaseAccepted) {
      std::cerr << "[FORMOSA HAL Test] stale FirmwareReboot release failed\n";
      exit(-1);
    }
    (void)fsa_release_completion(t);
  } else {
    std::cerr << "[FORMOSA HAL Test] stale token after reset unexpected ps="
              << ps << " result=" << static_cast<int>(stale) << "\n";
    exit(-1);
  }
  if (!fsa_hal_is_available()) {
    std::cerr << "[FORMOSA HAL Test] session not available after WP recovery\n";
    exit(-1);
  }
  FsaCompletionToken ok = 0;
  test_return(submit_h2d(input, 0x00100540, sizeof(input), &ok));
  FsaCompletionResult r = FSA_COMPLETION_RESULT_PENDING;
  test_return(fsa_wait_completion(ok, 100, &r));
}

int main() {
  if (std::getenv("FORMOSA_HAL_INVALID_CONFIG") != nullptr)
    return test_invalid_configuration();
  test_fsa_probe();
  test_fsa_hal_init();
  test_fsa_hal_occupancy();
  test_semantic_descriptor_validation();
  test_fsa_include_gpufw();
  test_alloc_generation_wrap_rules();
  test_fsa_generation_and_faults();
  test_wait_poll_release_timeout_lifecycle();
  test_pool_exhaustion_and_reset_reuse();
  test_barrier_terminal_with_predecessors();
  /* Barrier ordering intentionally leaves the mock ring entries published;
   * start the ring-capacity test from a fresh session. */
  test_return(fsa_hal_reset(1000));
  test_fsa_cmd_start_kernel();
  test_fsa_cmd_ring_control();
  /* Ring is full after buffer-control; reset before transport-fault injects. */
  test_return(fsa_hal_reset(1000));
  test_pre_wp_failure_rolls_back_slot();
  test_wp_failure_preserves_slot_fail_stop();
  /* Fail-stop recovery leaves session up; reboot-timeout fail-stop is last. */
  test_fsa_reboot_failure_is_fail_stop();
  test_return(fsa_hal_cleanup());
  if (fsa_hal_is_available()) {
    std::cerr << "[FORMOSA HAL Test] HAL remained available after cleanup"
              << std::endl;
    return -1;
  }
  /* Unavailable session must reject runtime submission (not a data-path check).
   */
  char buffer[8] = {};
  if (fsa_copy_to_dev(0x20000000, buffer, sizeof(buffer)) == 0 ||
      fsa_copy_from_dev(0x20000000, buffer, sizeof(buffer)) == 0) {
    std::cerr << "[FORMOSA HAL Test] Runtime copy bypassed unavailable HAL"
              << std::endl;
    return -1;
  }

  std::cout << "\n[FORMOSA HAL Test] All tests completed." << std::endl;

  return 0;
}
