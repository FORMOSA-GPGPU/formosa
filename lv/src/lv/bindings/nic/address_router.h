// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <liblv/schema.h>
#include <systemc.h>
#include <tlm.h>
#include <tlm_utils/multi_passthrough_initiator_socket.h>
#include <tlm_utils/multi_passthrough_target_socket.h>
#include <tlm_utils/peq_with_cb_and_phase.h>

#include <cstdint>
#include <deque>
#include <unordered_map>
#include <vector>

namespace nic::detail {

struct AddressMapEntry {
  uint64_t addr = 0;
  uint64_t size = 0;
  bool subtract_start_addr = true;
  // clang-format off
  LV_SCHEMA_SHARED(nic, AddressMapEntry,
                   LV_FIELD(addr, "Start address"),
                   LV_FIELD(size, "Size in bytes (non-zero)"),
                   LV_FIELD(subtract_start_addr,
                            "Translate requests to target-local addresses"))
  // clang-format on
};

// Keep LuaJIT's exact uint64_t memory-map values local to this schema; ordinary
// LV_SCHEMA fields use Lua numbers, which cannot represent every 64-bit
// address.
AddressMapEntry sol_lua_get(sol::types<AddressMapEntry>, lua_State *state,
                            int index, sol::stack::record &tracking);

// Implementation shared only by nic.Router and nic.XBar. Unclocked, buffered
// transport: preserves annotated delays and TLM exclusion rules, but models no
// arbitration latency, bandwidth limit, or finite buffer capacity.
class AddressRouter : public sc_module {
 public:
  using Payload = tlm::tlm_generic_payload;
  using TargetSocket =
      tlm_utils::multi_passthrough_target_socket<AddressRouter>;
  using InitiatorSocket =
      tlm_utils::multi_passthrough_initiator_socket<AddressRouter>;

  TargetSocket from;
  InitiatorSocket to;

  AddressRouter(const sc_module_name &name, unsigned int num_masters,
                const std::vector<AddressMapEntry> &addr_map);
  void set_to(InitiatorSocket::base_target_socket_type *target) {
    to.bind(*target);
  }

 private:
  struct Region {
    AddressMapEntry map;
    int target;
  };
  struct Transaction {
    int master;
    int target;
    uint64_t local_address;
    Payload *downstream = nullptr;
    bool request_ended = false;
  };

  std::vector<Region> regions_;
  std::vector<std::deque<Payload *>> requests_, responses_;
  std::vector<Payload *> active_requests_, active_responses_;
  std::unordered_map<Payload *, Transaction> transactions_;
  tlm_utils::peq_with_cb_and_phase<AddressRouter> events_;

  void end_of_elaboration() override;
  const Region *Route(const Payload &trans, bool debug = false) const;
  tlm::tlm_sync_enum Forward(int master, Payload &trans, tlm::tlm_phase &phase,
                             sc_time &delay);
  tlm::tlm_sync_enum Backward(int target, Payload &trans, tlm::tlm_phase &phase,
                              sc_time &delay);
  void Process(Payload &trans, const tlm::tlm_phase &phase);
  void StartRequest(int target);
  void EndRequest(Payload &trans);
  void QueueResponse(Payload &trans);
  void StartResponse(int master);
  void Blocking(int master, Payload &trans, sc_time &delay);
  unsigned int Debug(int master, Payload &trans);
  bool Dmi(int, Payload &trans, tlm::tlm_dmi &) {
    trans.set_dmi_allowed(false);
    return false;
  }
  void Invalidate(int, sc_dt::uint64, sc_dt::uint64) {}
};

}  // namespace nic::detail
