// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include <formosa_addr_map.h>
#include <real/real.h>

#include <optional>

namespace formosa::real {
namespace {

std::optional<ConfigurationSnapshot> snapshot;

int read_capability(uint64_t offset, uint64_t &value) {
  return fsa_real_get(FSA_SYSTEM_INFO_BASE + offset, &value, sizeof(value));
}

}  // namespace

int load_configuration_snapshot() noexcept {
  uint64_t version = 0;
  uint64_t length = 0;
  ConfigurationSnapshot next;
  if (read_capability(FSA_SYSTEM_INFO_OFF_ABI_VERSION, version) != 0 ||
      read_capability(FSA_SYSTEM_INFO_OFF_LENGTH, length) != 0 ||
      version != FSA_SYSTEM_INFO_ABI_VERSION ||
      length < FSA_SYSTEM_INFO_LENGTH ||
      read_capability(FSA_SYSTEM_INFO_OFF_NUM_SM, next.num_cores) != 0 ||
      read_capability(FSA_SYSTEM_INFO_OFF_THREADS_PER_WARP,
                      next.threads_per_warp) != 0 ||
      read_capability(FSA_SYSTEM_INFO_OFF_WARPS_PER_CORE,
                      next.warps_per_core) != 0 ||
      read_capability(FSA_SYSTEM_INFO_OFF_LOCAL_MEM_SIZE,
                      next.local_mem_size) != 0 ||
      read_capability(FSA_SYSTEM_INFO_OFF_SHARED_CACHE_SIZE,
                      next.shared_cache_size) != 0 ||
      read_capability(FSA_SYSTEM_INFO_OFF_CACHE_BLOCK_SIZE,
                      next.cache_line_size) != 0) {
    snapshot.reset();
    return -1;
  }

  next.global_mem_base = FSA_GLOBAL_MEM_BASE;
  next.global_mem_alloc_base = FSA_GLOBAL_ALLOC_BASE;
  next.global_mem_size = FSA_GLOBAL_MEM_SIZE;
  next.global_mem_noncache_alloc_base = FSA_NONCACHE_ALLOC_BASE;
  next.global_mem_noncache_alloc_size = FSA_NONCACHE_ALLOC_SIZE;
  next.fsa_mmio_base = FSA_MMIO_BASE;
  next.clint_base = FSA_CLINT_BASE;
  next.stack_size_per_thread = FSA_PER_THREAD_STACK_SIZE;
  snapshot = next;
  return 0;
}

const ConfigurationSnapshot *configuration_snapshot() noexcept {
  return snapshot ? &snapshot.value() : nullptr;
}

void reset_configuration_snapshot() noexcept { snapshot.reset(); }

}  // namespace formosa::real
