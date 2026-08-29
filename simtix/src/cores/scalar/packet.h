/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <systemc.h>

#include "cores/instr.h"

namespace simtix::scalar {

struct Packet {
  uint64_t unique_id;
  uint64_t pc;
  uint32_t wid;
  uint32_t iword;
  Instr instr;
  bool decoded = false;

  int64_t rs1_data;
  int64_t rs2_data;
  int64_t rs3_data;

  ExecFlag flag;
  uint32_t csr_buf;
  uint64_t addr_buf;
  int64_t data_buf;
  uint8_t fflags_buf;
};

class PacketPool {
 public:
  PacketPool(uint32_t initial_size) {
    pool_.reserve(initial_size);
    free_list_.reserve(initial_size);
    for (uint32_t i = 0; i < initial_size; ++i) {
      AddNewPacket();
    }
  }

  Packet *Acquire() {
    if (free_list_.empty()) {
      AddNewPacket();
    }
    Packet *p = free_list_.back();
    free_list_.pop_back();
    p->unique_id = unique_id_++;
    return p;
  }

  void Release(Packet *p) {
    p->unique_id = 0;
    p->iword = 0;
    p->instr.Reset();
    p->decoded = false;
    free_list_.push_back(p);
  }

 private:
  void AddNewPacket() {
    auto new_packet = std::unique_ptr<Packet>(new Packet);
    free_list_.push_back(new_packet.get());
    pool_.push_back(std::move(new_packet));
  }

  std::vector<Packet *> free_list_;
  std::vector<std::unique_ptr<Packet>> pool_;

  inline static uint64_t unique_id_ = 0;
};
}  // namespace simtix::scalar
