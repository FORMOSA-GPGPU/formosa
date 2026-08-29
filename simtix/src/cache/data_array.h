/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <systemc.h>
#include <tlm_core/tlm_1/tlm_req_rsp/tlm_1_interfaces/tlm_fifo_ifs.h>

#include <cstddef>
#include <cstdint>
#include <vector>

#include "cache/packet.h"
#include "cache/param.h"

namespace simtix::cache {

class DataArray {
  friend class CacheModuleTester;
  friend class DataArrayTester;

 public:
  explicit DataArray(const Param &p);

  struct VictimBuffer {
    uint64_t address = 0;
    uint8_t *data = nullptr;
    size_t size = 0;
  };

  struct ProcessResult {
    bool ok = true;
    bool victim_generated = false;
  };

  /**
   * @brief Process a cache-hit packet through the data array.
   *
   * @param packet Packet already annotated with hit and location metadata.
   * @param victim_buffer Optional caller-owned buffer for a dirty victim block.
   * @return Result describing access success and dirty-victim generation.
   */
  ProcessResult Process(Packet *packet, VictimBuffer *victim_buffer = nullptr);
  bool ReadBytes(Packet *packet, uint8_t *dst, size_t size);
  bool WriteBytes(Packet *packet, const uint8_t *src, size_t size);
  bool ReadBlock(Location location, uint8_t *dst, size_t size);

 private:
  ProcessResult Write(Packet *packet, VictimBuffer *victim_buffer);
  bool Read(Packet *packet);
  uint8_t *GetBlockData(Location loc);

  const Param config_;
  const size_t num_sets_;

  // Storage layout: [sets][ways][block_size].
  std::vector<uint8_t> data_array_;
};

}  // namespace simtix::cache
