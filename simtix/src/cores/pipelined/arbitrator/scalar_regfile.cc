// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include "scalar_regfile.h"

namespace simtix::pipelined {

const ScalarEntry& ScalarRegFile::Read(uint32_t local_wid,
                                       uint8_t reg_id) const {
  return entries_[local_wid * regs_per_warp_ + reg_id];
}

void ScalarRegFile::WriteVector(uint32_t local_wid, uint8_t reg_id) {
  auto& entry = entries_[local_wid * regs_per_warp_ + reg_id];
  entry.kind = SRFValueKind::kVector;
  entry.base = 0;
  entry.stride = 0;
}

void ScalarRegFile::WriteCompressed(uint32_t local_wid, uint8_t reg_id,
                                    int64_t base, int64_t stride) {
  if (stride == 0) {
    WriteUniform(local_wid, reg_id, base);
  } else {
    WriteAffine(local_wid, reg_id, base, stride);
  }
}

void ScalarRegFile::WriteUniform(uint32_t local_wid, uint8_t reg_id,
                                 int64_t base) {
  auto& entry = entries_[local_wid * regs_per_warp_ + reg_id];
  entry.kind = SRFValueKind::kUniform;
  entry.base = base;
  entry.stride = 0;
}

void ScalarRegFile::WriteAffine(uint32_t local_wid, uint8_t reg_id,
                                int64_t base, int64_t stride) {
  auto& entry = entries_[local_wid * regs_per_warp_ + reg_id];
  entry.kind = SRFValueKind::kAffine;
  entry.base = base;
  entry.stride = stride;
}

}  // namespace simtix::pipelined
