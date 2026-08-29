/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <cstddef>
#include <cstdint>

extern "C" {

int fsa_real_init();
int fsa_real_probe();
int fsa_real_mmio(uint64_t offset, int64_t wr_val, uint64_t *rd_ptr);
int fsa_real_cp_reset();
int fsa_real_copy_to_scratchpad(uintptr_t dev_addr, const void *host_ptr,
                                size_t size);
int fsa_real_copy_from_scratchpad(uintptr_t dev_addr, void *host_ptr,
                                  size_t size);
void fsa_real_abort();
void fsa_real_cleanup();
}

namespace formosa::real {

struct ConfigurationSnapshot {
  uint64_t num_cores = 0;
  uint64_t threads_per_warp = 0;
  uint64_t warps_per_core = 0;
  uint64_t local_mem_size = 0;
  uint64_t shared_cache_size = 0;
  uint64_t cache_line_size = 0;
  uint64_t global_mem_base = 0;
  uint64_t global_mem_alloc_base = 0;
  uint64_t global_mem_size = 0;
  uint64_t global_mem_noncache_alloc_base = 0;
  uint64_t global_mem_noncache_alloc_size = 0;
  uint64_t fsa_mmio_base = 0;
  uint64_t clint_base = 0;
  uint64_t stack_size_per_thread = 0;
};

const ConfigurationSnapshot *configuration_snapshot() noexcept;

}  // namespace formosa::real
