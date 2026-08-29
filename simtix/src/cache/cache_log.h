/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <liblv/log.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>

#include "cache/packet.h"

namespace simtix::cache::cache_log {

enum class Category {
  kMmio = 0,
  kArb,
  kMshr,
  kMem,
  kHazard,
  kStall,
  kStage,
  kCount,
};

inline const char *CategoryName(Category category) {
  switch (category) {
    case Category::kMmio:
      return "mmio";
    case Category::kArb:
      return "arb";
    case Category::kMshr:
      return "mshr";
    case Category::kMem:
      return "mem";
    case Category::kHazard:
      return "hazard";
    case Category::kStall:
      return "stall";
    case Category::kStage:
      return "stage";
    case Category::kCount:
      break;
  }
  return "unknown";
}

inline bool AsciiEqualFold(std::string_view lhs, std::string_view rhs) {
  if (lhs.size() != rhs.size()) {
    return false;
  }

  for (size_t i = 0; i < lhs.size(); ++i) {
    char left = lhs[i];
    char right = rhs[i];
    if (left >= 'A' && left <= 'Z') {
      left = static_cast<char>(left - 'A' + 'a');
    }
    if (right >= 'A' && right <= 'Z') {
      right = static_cast<char>(right - 'A' + 'a');
    }
    if (left != right) {
      return false;
    }
  }
  return true;
}

inline std::string_view Trim(std::string_view value) {
  while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) {
    value.remove_prefix(1);
  }
  while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) {
    value.remove_suffix(1);
  }
  return value;
}

struct Settings {
  std::array<bool, static_cast<size_t>(Category::kCount)> enabled{};
  const char *filter = nullptr;
  uint64_t period = 100000;
};

inline void EnableAll(Settings *settings) {
  for (bool &enabled : settings->enabled) {
    enabled = true;
  }
}

inline void EnableToken(Settings *settings, std::string_view token) {
  token = Trim(token);
  if (token.empty()) {
    return;
  }

  if (AsciiEqualFold(token, "all") || AsciiEqualFold(token, "1") ||
      AsciiEqualFold(token, "true")) {
    EnableAll(settings);
    return;
  }

  for (size_t i = 0; i < static_cast<size_t>(Category::kCount); ++i) {
    const auto category = static_cast<Category>(i);
    if (AsciiEqualFold(token, CategoryName(category))) {
      settings->enabled[i] = true;
      return;
    }
  }
}

inline uint64_t ParsePeriod(const char *period_env) {
  if (period_env == nullptr || period_env[0] == '\0') {
    return 100000;
  }

  char *end = nullptr;
  const unsigned long long value = std::strtoull(period_env, &end, 10);
  if (end == period_env || value == 0) {
    return 100000;
  }
  return static_cast<uint64_t>(value);
}

inline Settings ParseSettings() {
  Settings settings;

  const char *log_env = std::getenv("SIMTIX_CACHE_LOG");
  if (log_env != nullptr) {
    std::string_view remaining(log_env);
    while (!remaining.empty()) {
      const size_t comma = remaining.find(',');
      const std::string_view token = remaining.substr(0, comma);
      EnableToken(&settings, token);
      if (comma == std::string_view::npos) {
        break;
      }
      remaining.remove_prefix(comma + 1);
    }
  }

  const char *filter_env = std::getenv("SIMTIX_CACHE_LOG_FILTER");
  settings.filter =
      filter_env != nullptr && filter_env[0] != '\0' ? filter_env : nullptr;
  settings.period = ParsePeriod(std::getenv("SIMTIX_CACHE_LOG_PERIOD"));
  return settings;
}

inline const Settings &GetSettings() {
  static const Settings settings = ParseSettings();
  return settings;
}

inline bool Enabled(const char *module_name, Category category) {
  const Settings &settings = GetSettings();
  const size_t index = static_cast<size_t>(category);
  if (index >= settings.enabled.size() || !settings.enabled[index]) {
    return false;
  }
  if (settings.filter == nullptr) {
    return true;
  }
  return module_name != nullptr &&
         std::string_view(module_name).find(settings.filter) !=
             std::string_view::npos;
}

inline bool ShouldLogRepeated(uint64_t count) {
  const uint64_t period = GetSettings().period;
  return count < 10 || count % period == 0;
}

inline const char *PacketTypeName(PacketType type) {
  switch (type) {
    case PacketType::kUnused:
      return "unused";
    case PacketType::kBypassCoreReq:
      return "bypass_core_req";
    case PacketType::kBypassCoreResp:
      return "bypass_core_resp";
    case PacketType::kCoreReq:
      return "core_req";
    case PacketType::kRefill:
      return "refill";
    case PacketType::kReplay:
      return "replay";
    case PacketType::kMshrReadReq:
      return "mshr_read_req";
    case PacketType::kVictimWriteReq:
      return "victim_write_req";
    case PacketType::kVictimWriteResp:
      return "victim_write_resp";
    case PacketType::kMemWriteReq:
      return "mem_write_req";
    case PacketType::kMemWriteResp:
      return "mem_write_resp";
  }
  return "unknown";
}

inline const char *PacketCommandName(const Packet *packet) {
  if (packet == nullptr || packet->payload_type() == PayloadType::kNone) {
    return "none";
  }
  const tlm::tlm_generic_payload *payload = packet->GetTlmGp();
  if (payload->is_read()) {
    return "read";
  }
  if (payload->is_write()) {
    return "write";
  }
  return "ignore";
}

inline uint64_t PacketLineAddress(const Packet *packet, size_t block_size) {
  if (packet == nullptr || block_size == 0 ||
      packet->payload_type() == PayloadType::kNone) {
    return 0;
  }
  return packet->GetAddress() / block_size;
}

inline std::string PacketParentId(const Packet *packet) {
  if (packet == nullptr || !packet->trace_parent_id.has_value()) {
    return "none";
  }
  return fmt::format("{}", *packet->trace_parent_id);
}

inline uint64_t PacketAddress(const Packet *packet) {
  if (packet == nullptr || packet->payload_type() == PayloadType::kNone) {
    return 0;
  }
  return packet->GetAddress();
}

}  // namespace simtix::cache::cache_log

#if QUILL_COMPILE_ACTIVE_LOG_LEVEL <= QUILL_COMPILE_ACTIVE_LOG_LEVEL_DEBUG
#define SIMTIX_CACHE_LOG_DEBUG(module_name_, category_, fmt_, ...)             \
  do {                                                                         \
    const char *cache_log_module_name__ = (module_name_);                      \
    const auto cache_log_category__ = (category_);                             \
    if (::simtix::cache::cache_log::Enabled(cache_log_module_name__,           \
                                            cache_log_category__)) {           \
      LV_DEBUG("{} cache cat={} " fmt_, cache_log_module_name__,               \
               ::simtix::cache::cache_log::CategoryName(cache_log_category__), \
               ##__VA_ARGS__);                                                 \
    }                                                                          \
  } while (0)
#else
#define SIMTIX_CACHE_LOG_DEBUG(module_name_, category_, fmt_, ...) \
  do {                                                             \
  } while (0)
#endif

#if QUILL_COMPILE_ACTIVE_LOG_LEVEL <= QUILL_COMPILE_ACTIVE_LOG_LEVEL_TRACE_L1
#define SIMTIX_CACHE_LOG_TRACE(module_name_, category_, fmt_, ...)             \
  do {                                                                         \
    const char *cache_log_module_name__ = (module_name_);                      \
    const auto cache_log_category__ = (category_);                             \
    if (::simtix::cache::cache_log::Enabled(cache_log_module_name__,           \
                                            cache_log_category__)) {           \
      LV_TRACE("{} cache cat={} " fmt_, cache_log_module_name__,               \
               ::simtix::cache::cache_log::CategoryName(cache_log_category__), \
               ##__VA_ARGS__);                                                 \
    }                                                                          \
  } while (0)
#else
#define SIMTIX_CACHE_LOG_TRACE(module_name_, category_, fmt_, ...) \
  do {                                                             \
  } while (0)
#endif
