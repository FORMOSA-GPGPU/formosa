// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include <formosa_addr_map.h>
#include <real/real.h>

#include <cctype>
#include <cstdlib>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>

namespace formosa::real {
namespace {

uint64_t read_value(const char *name, bool required, uint64_t default_value) {
  const char *raw = std::getenv(name);
  if (raw == nullptr) {
    if (required) {
      throw std::runtime_error(std::string("Environment variable ") + name +
                               " not set.");
    }
    return default_value;
  }
  const char *first = raw;
  while (std::isspace(static_cast<unsigned char>(*first))) ++first;
  if (*raw == '\0' || *first == '-') {
    throw std::runtime_error(std::string("Invalid environment variable ") +
                             name);
  }

  size_t consumed = 0;
  const std::string text(raw);
  const unsigned long long value = std::stoull(text, &consumed, 10);
  if (consumed != text.size() || value > std::numeric_limits<uint64_t>::max()) {
    throw std::runtime_error(std::string("Invalid environment variable ") +
                             name);
  }
  return static_cast<uint64_t>(value);
}

ConfigurationSnapshot read_snapshot() {
  ConfigurationSnapshot snapshot;
  snapshot.num_cores = read_value("LV_FORMOSA_NUM_SM", true, 0);
  snapshot.threads_per_warp =
      read_value("LV_FORMOSA_THREADS_PER_WARP", true, 0);
  snapshot.warps_per_core = read_value("LV_FORMOSA_WARPS_PER_CORE", true, 0);
  snapshot.local_mem_size = read_value("LV_FORMOSA_LOCAL_MEM_SIZE", true, 0);
  snapshot.shared_cache_size =
      read_value("LV_FORMOSA_SHARED_CACHE_SIZE", false, 0x20000);
  snapshot.cache_line_size =
      read_value("LV_FORMOSA_CACHE_BLOCK_SIZE", false, 64);
  snapshot.global_mem_base =
      read_value("LV_FORMOSA_GLOBAL_MEM_BASE", false, FSA_GLOBAL_MEM_BASE);
  snapshot.global_mem_alloc_base = read_value(
      "LV_FORMOSA_GLOBAL_MEM_ALLOC_BASE", false, FSA_GLOBAL_ALLOC_BASE);
  snapshot.global_mem_size =
      read_value("LV_FORMOSA_GLOBAL_MEM_SIZE", false, FSA_GLOBAL_MEM_SIZE);
  snapshot.global_mem_noncache_alloc_base =
      read_value("LV_FORMOSA_GLOBAL_MEM_NONCACHE_ALLOC_BASE", false,
                 FSA_NONCACHE_ALLOC_BASE);
  snapshot.global_mem_noncache_alloc_size =
      read_value("LV_FORMOSA_GLOBAL_MEM_NONCACHE_ALLOC_SIZE", false,
                 FSA_NONCACHE_ALLOC_SIZE);
  snapshot.fsa_mmio_base =
      read_value("LV_FORMOSA_FSA_MMIO_BASE", false, FSA_MMIO_BASE);
  snapshot.clint_base =
      read_value("LV_FORMOSA_CLINT_BASE", false, FSA_CLINT_BASE);
  snapshot.stack_size_per_thread = read_value(
      "LV_FORMOSA_STACK_SIZE_PER_THREAD", false, FSA_PER_THREAD_STACK_SIZE);
  return snapshot;
}

}  // namespace

const ConfigurationSnapshot *configuration_snapshot() noexcept {
  static const std::optional<ConfigurationSnapshot> state =
      []() -> std::optional<ConfigurationSnapshot> {
    try {
      return read_snapshot();
    } catch (...) {
      return std::nullopt;
    }
  }();
  return state ? &*state : nullptr;
}

}  // namespace formosa::real
