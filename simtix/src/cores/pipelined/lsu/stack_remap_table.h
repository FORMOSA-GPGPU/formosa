/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <liblv/common/tlm_sink.h>
#include <liblv/schema.h>

#include <cstdint>
#include <vector>

namespace simtix::pipelined {

class StackRemapTable : public sc_module {
 public:
  struct Param {
    uint32_t entries = 8;
    uint64_t region_size = 64 * 1024;

    LV_SCHEMA(StackRemapTable, Param,
              LV_FIELD(entries,
                       "Number of firmware-programmable stack regions"),
              LV_FIELD(region_size, "Size and alignment of each stack region"))
  };

  StackRemapTable(const sc_module_name &name, const Param &param);

  auto port() const { return &sink_.port; }
  bool Matches(uint64_t addr) const;

 private:
  static constexpr uint64_t kValidBitMask = 1;
  static constexpr uint64_t kAddressMask = (uint64_t{1} << 48) - 1;

  void HandleRequests();
  bool IsDescriptorValid(uint64_t descriptor) const;
  uint64_t NormalizeDescriptor(uint64_t descriptor) const;

  const uint64_t region_mask_;
  std::vector<uint64_t> descriptors_;
  lv::TlmSink sink_;
};

}  // namespace simtix::pipelined
