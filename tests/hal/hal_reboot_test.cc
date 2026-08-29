// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include <addr_map/formosa_addr_map.h>
#include <formosa-hal/api.h>
#include <formosa-hal/hal.h>

#include <array>
#include <cstdint>
#include <iostream>

namespace {

constexpr size_t kCopySize = 16;
constexpr uintptr_t kDeviceAddr = FSA_GLOBAL_MEM_BASE + 0x02001000;

bool host_to_device(const std::array<uint8_t, kCopySize> &input,
                    FsaCompletionToken *completion) {
  FsaMemoryCopyInfo info{};
  info.struct_size = sizeof(info);
  info.source.domain = kMemoryDomainHost;
  info.source.range.address = reinterpret_cast<uintptr_t>(input.data());
  info.source.range.size = input.size();
  info.destination.domain = kMemoryDomainDevice;
  info.destination.range.address = kDeviceAddr;
  info.destination.range.size = input.size();
  const FsaCommandSubmitStatus submit = fsa_cmd_memory_copy(&info, completion);
  if (submit != kFsaCommandSubmitAccepted) {
    std::cerr << "submit failed: " << submit << "\n";
    return false;
  }
  FsaCompletionResult result = FSA_COMPLETION_RESULT_PENDING;
  const FsaCompletionWaitStatus wait =
      fsa_wait_completion(*completion, 60000, &result);
  if (wait != kFsaCompletionWaitSuccess ||
      result != FSA_COMPLETION_RESULT_SUCCESS) {
    std::cerr << "wait failed: wait=" << wait
              << " result=" << static_cast<int>(result) << "\n";
    return false;
  }
  return true;
}

}  // namespace

int main() {
  FsaDeviceDescription description = {};
  if (fsa_probe() != 0 || fsa_hal_init(&description) != 0) {
    std::cerr << "HAL init failed\n";
    return 1;
  }

  std::array<uint8_t, kCopySize> input{};
  for (size_t i = 0; i < input.size(); ++i) {
    input[i] = static_cast<uint8_t>(0xa0 + i);
  }

  /* Pre-reset copy must succeed under live firmware. */
  FsaCompletionToken before = 0;
  if (!host_to_device(input, &before)) {
    std::cerr << "pre-reset memory copy failed\n";
    return 1;
  }
  if (fsa_completion_token_boot_generation(before) == 0) {
    std::cerr << "missing boot generation before reset\n";
    return 1;
  }
  std::cout << "pre-reset copy ok boot_generation="
            << fsa_completion_token_boot_generation(before) << "\n";

  /* Leave an outstanding completion so post-reset poll can observe reboot. */
  FsaCompletionToken pending = 0;
  FsaMemoryCopyInfo pending_info{};
  pending_info.struct_size = sizeof(pending_info);
  pending_info.source.domain = kMemoryDomainHost;
  pending_info.source.range.address = reinterpret_cast<uintptr_t>(input.data());
  pending_info.source.range.size = input.size();
  pending_info.destination.domain = kMemoryDomainDevice;
  pending_info.destination.range.address = kDeviceAddr + 0x40;
  pending_info.destination.range.size = input.size();
  if (fsa_cmd_memory_copy(&pending_info, &pending) !=
      kFsaCommandSubmitAccepted) {
    std::cerr << "pending submit failed\n";
    return 1;
  }
  std::cout << "pending copy submitted alloc_generation="
            << fsa_completion_token_alloc_generation(pending) << "\n";

  /* Cooperative Firmware Reboot: MSIP → FW drain → ROM → re-upload. */
  const int reset_status = fsa_hal_reset(30000);
  if (reset_status != 0) {
    std::cerr << "fsa_hal_reset failed: " << reset_status << "\n";
    return 1;
  }
  std::cout << "fsa_hal_reset ok\n";

  FsaCompletionResult stale_result = FSA_COMPLETION_RESULT_PENDING;
  const FsaCompletionPollStatus stale_status =
      fsa_poll_completion(pending, &stale_result);
  if (stale_status != kFsaCompletionPollTerminal ||
      stale_result != FSA_COMPLETION_RESULT_FIRMWARE_REBOOT) {
    std::cerr << "stale completion not treated as FirmwareReboot: status="
              << stale_status << " result=" << static_cast<int>(stale_result)
              << "\n";
    return 1;
  }
  (void)fsa_release_completion(pending);
  std::cout << "stale completion -> FirmwareReboot ok\n";

  /* Post-reset copy must succeed with a new boot generation. */
  FsaCompletionToken after = 0;
  if (!host_to_device(input, &after)) {
    std::cerr << "post-reset memory copy failed\n";
    return 1;
  }
  if (fsa_completion_token_boot_generation(after) ==
      fsa_completion_token_boot_generation(before)) {
    std::cerr << "boot generation did not advance after reset ("
              << fsa_completion_token_boot_generation(after) << ")\n";
    return 1;
  }
  if (fsa_completion_token_alloc_generation(after) !=
      FSA_COMPLETION_ALLOC_GENERATION_INITIAL) {
    std::cerr << "alloc generation did not restart after reset ("
              << fsa_completion_token_alloc_generation(after) << ")\n";
    return 1;
  }
  std::cout << "post-reset copy ok boot_generation="
            << fsa_completion_token_boot_generation(after) << "\n";

  if (fsa_hal_cleanup() != 0) {
    std::cerr << "cleanup failed\n";
    return 1;
  }
  std::cout << "hal_reboot_test passed\n";
  return 0;
}
