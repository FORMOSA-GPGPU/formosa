/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <systemc.h>
#include <tlm_core/tlm_1/tlm_req_rsp/tlm_1_interfaces/tlm_fifo_ifs.h>

#include <cstddef>
#include <vector>

#include "cache/packet.h"
#include "cache/param.h"

namespace simtix::cache {

class PacketLifecycleIntf;

class WriteBuffer : public sc_core::sc_module {
  friend class CacheModuleTester;

 public:
  WriteBuffer(sc_module_name name, const Param &param,
              PacketLifecycleIntf &packet_lifecycle);
  ~WriteBuffer() override;
  bool HasPendingWork() const;
  size_t PendingEntryCount() const;
  size_t InflightEntryCount() const;

  sc_in<bool> SC_NAMED(clock);
  sc_port<tlm::tlm_fifo_get_if<Packet *>> SC_NAMED(write_buffer_in);
  sc_port<tlm::tlm_fifo_put_if<Packet *>> SC_NAMED(mem_req_out);
  sc_port<tlm::tlm_fifo_get_if<Packet *>> SC_NAMED(mem_resp_in);

 private:
  void Tick();
  void ProcessWriteRequest();
  void ProcessMemReq();
  void ProcessMemResp();
  bool HasFreeEntry() const;
  size_t Occupancy() const;
  void CheckInvariants() const;

  const Param config_;
  PacketLifecycleIntf &packet_lifecycle_;

  // Queue to store pending payloads that have been accepted but not yet sent
  // out as memory requests. This ensures in-order issue of memory requests.
  std::vector<Packet *> pending_entries_;

  std::vector<Packet *> inflight_entries_;
};

}  // namespace simtix::cache
