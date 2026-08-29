/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <cstdint>
#include <vector>

namespace simtix::pipelined {

enum class SRFValueKind : uint8_t {
  kUnknown = 0,
  kVector,
  kUniform,
  kAffine,
  kAbsent,
};

struct ScalarEntry {
  SRFValueKind kind = SRFValueKind::kUnknown;
  int64_t base = 0;
  int64_t stride = 0;

  bool IsCompressed() const {
    return kind == SRFValueKind::kUniform || kind == SRFValueKind::kAffine;
  }
};

class ScalarRegFile {
 public:
  ScalarRegFile(uint32_t num_local_warps, uint32_t regs_per_warp = 32)
      : entries_(num_local_warps * regs_per_warp),
        regs_per_warp_(regs_per_warp) {}

  const ScalarEntry& Read(uint32_t local_wid, uint8_t reg_id) const;
  void WriteVector(uint32_t local_wid, uint8_t reg_id);
  void WriteCompressed(uint32_t local_wid, uint8_t reg_id, int64_t base,
                       int64_t stride);

 private:
  void WriteUniform(uint32_t local_wid, uint8_t reg_id, int64_t base);
  void WriteAffine(uint32_t local_wid, uint8_t reg_id, int64_t base,
                   int64_t stride);

  std::vector<ScalarEntry> entries_;

  const uint32_t regs_per_warp_;
};
}  // namespace simtix::pipelined
