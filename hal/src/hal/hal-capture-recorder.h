/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string_view>

namespace formosa::hal {

class CaptureRecorder {
 public:
  ~CaptureRecorder();

  void InitIfNeeded();
  bool enabled();
  void Close();

  void ManifestU64(const char *key, uint64_t value);
  void Event(std::string_view type, uint64_t addr = 0, uint64_t size = 0,
             int64_t value = 0, int64_t status = 0, std::string_view blob = "");
  void CompletionSlotEvent(uint64_t slot_addr, uint64_t alloc_tag,
                           int64_t result);
  void BlobEvent(std::string_view type, uint64_t addr, const void *data,
                 size_t size, int64_t value = 0, int64_t status = 0);

 private:
  void EventLocked(std::string_view type, uint64_t addr, uint64_t size,
                   uint64_t alloc_tag, int64_t value, int64_t status,
                   std::string_view blob);
  static std::string Hex(uint64_t value);

  bool initialized_ = false;
  bool enabled_ = false;
  uint64_t seq_ = 0;
  uint64_t blob_seq_ = 0;
  std::filesystem::path dir_;
  std::ofstream events_;
  std::ofstream manifest_;
  std::mutex mtx_;
};

}  // namespace formosa::hal
