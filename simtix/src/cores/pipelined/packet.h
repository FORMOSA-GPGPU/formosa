/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <systemc.h>

#include <cstdint>

#include "cores/instr.h"
#include "cores/param.h"
#include "utils/object_pool.h"

namespace simtix::pipelined {

struct Packet {
  // PC Gen stage
  uint64_t unique_id;
  uint64_t wpc;
  uint32_t wid;

  // Fetch stage
  uint32_t iword;

  // Decode stage
  Instr instr;

  // Issue stage
  sc_bv_base tmask;

  // Operand collect stage
  std::vector<int64_t> rs1_data;
  std::vector<int64_t> rs2_data;
  std::vector<int64_t> rs3_data;

  // Execute stage
  ExecFlag flag;
  uint32_t csr_buf;
  std::vector<uint64_t> addr_buf;
  std::vector<int64_t> data_buf;
  std::vector<uint8_t> fflags_buf;
  uint8_t pri_buf;
  sc_time timestamp;

  bool is_stack_access = false;
  bool is_scalarizable = false;
  // I-Buffer flush stats only count originals (!is_shared).
  bool is_shared = false;

 private:
  explicit Packet(const ArchParam &param)
      : tmask(false, static_cast<int>(param.num_lanes)),
        rs1_data(param.num_lanes),
        rs2_data(param.num_lanes),
        rs3_data(param.num_lanes),
        addr_buf(param.num_lanes),
        data_buf(param.num_lanes),
        fflags_buf(param.num_lanes) {}

  // Allow PacketPool.pool_ to access the private constructor of Packet
  template <typename T, typename... CtorArgs>
  friend class simtix::ObjectPool;
};

class PacketPool {
 public:
  PacketPool(const ArchParam &param, uint32_t initial_size)
      : pool_(initial_size, param) {}

  Packet *Acquire() {
    Packet *p = pool_.Acquire();

    p->unique_id = unique_id_++;
    return p;
  }

  void Release(Packet *p) {
    p->unique_id = 0;
    p->iword = 0;
    p->is_stack_access = false;
    p->is_scalarizable = false;
    p->is_shared = false;
    p->tmask = 0;
    p->instr.Reset();

    pool_.Release(p);
  }

 private:
  simtix::ObjectPool<Packet, ArchParam> pool_;

  inline static uint64_t unique_id_ = 0;
};

}  // namespace simtix::pipelined
