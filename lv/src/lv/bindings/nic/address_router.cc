// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include "address_router.h"

#include <liblv/log.h>
#include <liblv/mm/pool.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>

namespace nic::detail {
namespace {
TLM_DECLARE_EXTENDED_PHASE(TargetCompleted);
TLM_DECLARE_EXTENDED_PHASE(ResponseReady);

// This metadata belongs only to this hop. Initiators may retire their buffers
// at upstream completion, while a target can retain its copy beyond END_RESP.
// The pool frees these owned buffers with the extension at reference zero.
struct DownstreamState : tlm::tlm_extension<DownstreamState> {
  AddressRouter::Payload *original;
  std::unique_ptr<unsigned char[]> buffers;
  DownstreamState(AddressRouter::Payload &trans, bool debug)
      : original(&trans) {
    const size_t data = trans.get_data_ptr() ? trans.get_data_length() : 0;
    const size_t enables = !debug && trans.get_byte_enable_ptr()
                               ? trans.get_byte_enable_length()
                               : 0;
    if (data + enables) buffers.reset(new unsigned char[data + enables]);
  }
  tlm::tlm_extension_base *clone() const override { return nullptr; }
  void copy_from(const tlm::tlm_extension_base &) override {}
};

AddressRouter::Payload *CopyManagedRequest(AddressRouter::Payload &original,
                                           uint64_t address,
                                           bool debug = false) {
  auto *copy = lv::mm::Pool::Allocate();
  copy->acquire();
  copy->free_all_extensions();
  auto *state = new DownstreamState(original, debug);
  copy->set_data_ptr(original.get_data_ptr() ? state->buffers.get() : nullptr);
  copy->set_byte_enable_ptr(
      !debug && original.get_byte_enable_ptr() && state->buffers
          ? state->buffers.get() +
                (original.get_data_ptr() ? original.get_data_length() : 0)
          : nullptr);
  copy->deep_copy_from(original);
  if (debug) {
    // Only command, address, data length, and data pointer are required for
    // debug transport. Old timed byte-enable arrays may no longer be valid;
    // leaving the destination pointer null prevents deep_copy_from reading
    // them.
    copy->set_byte_enable_length(0);
    copy->set_streaming_width(copy->get_data_length());
  }
  copy->set_address(address);
  copy->set_auto_extension(state);
  return copy;
}

void CopyResponse(AddressRouter::Payload &original,
                  AddressRouter::Payload &copy,
                  bool use_byte_enable_on_read = true) {
  original.update_original_from(copy, use_byte_enable_on_read);
  // No backward callback is legal after downstream completion. Detach the
  // original before retiring it; retained copies need only their own state.
  copy.get_extension<DownstreamState>()->original = nullptr;
  copy.release();
}

uint64_t MapInteger(const sol::table &table, const char *key) {
  sol::object value = table[key];
  if (value.is<double>()) {
    const auto number = value.as<double>();
    if (number >= 0 && number < std::ldexp(1.0, 64) &&
        std::floor(number) == number) {
      return static_cast<uint64_t>(number);
    }
  } else if (value.valid()) {
    sol::state_view lua(table.lua_state());
    sol::table ffi = lua["require"]("ffi");
    if (ffi["istype"]("uint64_t", value).get<bool>()) {
      uint64_t result;
      std::memcpy(&result, value.pointer(), sizeof(result));
      return result;
    }
  }
  LV_FATAL("Memory-map {} must be an unsigned integer or uint64_t cdata", key);
  return 0;
}
}  // namespace

AddressMapEntry sol_lua_get(sol::types<AddressMapEntry>, lua_State *state,
                            int index, sol::stack::record &tracking) {
  sol::table table(state, index);
  tracking.use(1);
  auto entry = lv::generic_parser<AddressMapEntry>(table);
  entry.addr = MapInteger(table, "addr");
  entry.size = MapInteger(table, "size");
  return entry;
}

AddressRouter::AddressRouter(const sc_module_name &name,
                             unsigned int num_masters,
                             const std::vector<AddressMapEntry> &addr_map)
    : sc_module(name),
      from("from"),
      to("to"),
      requests_(addr_map.size()),
      responses_(num_masters),
      active_requests_(addr_map.size(), nullptr),
      active_responses_(num_masters, nullptr),
      events_("events", this, &AddressRouter::Process) {
  if (num_masters == 0 || addr_map.empty()) {
    LV_FATAL("{} requires at least one master and one address region",
             this->name());
  }
  for (size_t i = 0; i < addr_map.size(); ++i) {
    const auto &entry = addr_map[i];
    if (entry.size == 0 ||
        entry.size - 1 > std::numeric_limits<uint64_t>::max() - entry.addr) {
      LV_FATAL("{}: invalid address region {:#x}:{:#x}", this->name(),
               entry.addr, entry.size);
    }
    regions_.push_back({entry, static_cast<int>(i)});
  }
  std::sort(regions_.begin(), regions_.end(),
            [](const Region &a, const Region &b) {
              return a.map.addr < b.map.addr;
            });
  for (size_t i = 1; i < regions_.size(); ++i) {
    const auto &prev = regions_[i - 1].map;
    if (regions_[i].map.addr - prev.addr < prev.size) {
      LV_FATAL("{}: overlapping address regions", this->name());
    }
  }

  from.register_nb_transport_fw(this, &AddressRouter::Forward);
  from.register_b_transport(this, &AddressRouter::Blocking);
  from.register_transport_dbg(this, &AddressRouter::Debug);
  from.register_get_direct_mem_ptr(this, &AddressRouter::Dmi);
  to.register_nb_transport_bw(this, &AddressRouter::Backward);
  to.register_invalidate_direct_mem_ptr(this, &AddressRouter::Invalidate);
}

void AddressRouter::end_of_elaboration() {
  if (from.size() != responses_.size() || to.size() != regions_.size()) {
    LV_FATAL(
        "{}: expected {} master bindings and {} target bindings, got {} "
        "and {} (targets must follow address-map order)",
        name(), responses_.size(), regions_.size(), from.size(), to.size());
  }
}

const AddressRouter::Region *AddressRouter::Route(const Payload &trans,
                                                  bool debug) const {
  const auto addr = trans.get_address();
  auto it = std::upper_bound(regions_.begin(), regions_.end(), addr,
                             [](uint64_t address, const Region &region) {
                               return address < region.map.addr;
                             });
  if (it == regions_.begin()) return nullptr;
  --it;
  const auto offset = addr - it->map.addr;
  // Streaming transfers touch only one streaming window. Subtraction avoids
  // overflow even for maps whose last byte is UINT64_MAX.
  auto length = trans.get_data_length();
  if (!debug && trans.get_streaming_width() != 0) {
    length = std::min(length, trans.get_streaming_width());
  }
  if (length == 0 || offset >= it->map.size || length > it->map.size - offset) {
    return nullptr;
  }
  return &*it;
}

tlm::tlm_sync_enum AddressRouter::Forward(int master, Payload &trans,
                                          tlm::tlm_phase &phase,
                                          sc_time &delay) {
  if (phase == tlm::BEGIN_REQ) {
    const auto *region = Route(trans);
    const auto address = trans.get_address();
    Transaction state{master, region ? region->target : -1, address};
    if (region && region->map.subtract_start_addr) {
      state.local_address -= region->map.addr;
    }
    if (!transactions_.emplace(&trans, state).second) {
      LV_FATAL("{}: payload reused before transaction completion", name());
    }
    trans.acquire();
    events_.notify(trans, phase, delay);
    return tlm::TLM_ACCEPTED;
  }
  if (phase == tlm::END_RESP) {
    events_.notify(trans, phase, delay);
    return tlm::TLM_COMPLETED;
  }
  LV_FATAL("{}: illegal forward phase", name());
  return tlm::TLM_COMPLETED;
}

tlm::tlm_sync_enum AddressRouter::Backward(int target, Payload &trans,
                                           tlm::tlm_phase &phase,
                                           sc_time &delay) {
  const auto *reference = trans.get_extension<DownstreamState>();
  if (!reference || !reference->original ||
      (phase != tlm::END_REQ && phase != tlm::BEGIN_RESP)) {
    LV_FATAL("{}: illegal backward phase or payload", name());
  }
  const auto &state = transactions_.at(reference->original);
  if (state.target != target || state.downstream != &trans) {
    LV_FATAL("{}: illegal backward phase or target", name());
  }
  events_.notify(*reference->original, phase, delay);
  return tlm::TLM_ACCEPTED;
}

void AddressRouter::Process(Payload &trans, const tlm::tlm_phase &phase) {
  auto &state = transactions_.at(&trans);
  if (phase == tlm::BEGIN_REQ) {
    // Acknowledge asynchronously: existing TlmSource registers its pending
    // request only after nb_transport_fw returns.
    tlm::tlm_phase end = tlm::END_REQ;
    sc_time delay = SC_ZERO_TIME;
    from[state.master]->nb_transport_bw(trans, end, delay);
    if (state.target < 0) {
      trans.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
      QueueResponse(trans);
    } else {
      requests_[state.target].push_back(&trans);
      StartRequest(state.target);
    }
  } else if (phase == tlm::END_REQ) {
    EndRequest(trans);
  } else if (phase == tlm::BEGIN_RESP || phase == TargetCompleted) {
    EndRequest(trans);
    sc_time delay = SC_ZERO_TIME;
    if (phase == tlm::BEGIN_RESP) {
      tlm::tlm_phase end = tlm::END_RESP;
      to[state.target]->nb_transport_fw(*state.downstream, end, delay);
    }
    // Buffer the response here, retaining the payload until the upstream
    // handshake ends. A slow master need not hold a target's response channel.
    if (delay == SC_ZERO_TIME) {
      QueueResponse(trans);
    } else {
      events_.notify(trans, ResponseReady, delay);
    }
  } else if (phase == ResponseReady) {
    QueueResponse(trans);
  } else if (phase == tlm::END_RESP) {
    const int master = state.master;
    active_responses_[master] = nullptr;
    transactions_.erase(&trans);
    trans.release();
    StartResponse(master);
  }
}

void AddressRouter::StartRequest(int target) {
  auto &queue = requests_[target];
  if (active_requests_[target] || queue.empty()) return;
  auto *trans = queue.front();
  queue.pop_front();
  active_requests_[target] = trans;
  auto &state = transactions_.at(trans);
  state.downstream = CopyManagedRequest(*trans, state.local_address);
  tlm::tlm_phase phase = tlm::BEGIN_REQ;
  sc_time delay = SC_ZERO_TIME;
  const auto status =
      to[target]->nb_transport_fw(*state.downstream, phase, delay);
  if (status == tlm::TLM_COMPLETED) {
    events_.notify(*trans, TargetCompleted, delay);
  } else if (status == tlm::TLM_UPDATED) {
    if (phase != tlm::END_REQ && phase != tlm::BEGIN_RESP) {
      LV_FATAL("{}: illegal target phase update", name());
    }
    events_.notify(*trans, phase, delay);
  }
}

void AddressRouter::EndRequest(Payload &trans) {
  auto &state = transactions_.at(&trans);
  if (state.request_ended) return;
  state.request_ended = true;
  active_requests_[state.target] = nullptr;
  StartRequest(state.target);
}

void AddressRouter::QueueResponse(Payload &trans) {
  auto &state = transactions_.at(&trans);
  if (state.downstream) {
    CopyResponse(trans, *state.downstream);
    state.downstream = nullptr;
  }
  trans.set_dmi_allowed(false);
  responses_[state.master].push_back(&trans);
  StartResponse(state.master);
}

void AddressRouter::StartResponse(int master) {
  auto &queue = responses_[master];
  if (active_responses_[master] || queue.empty()) return;
  auto *trans = queue.front();
  queue.pop_front();
  active_responses_[master] = trans;
  tlm::tlm_phase phase = tlm::BEGIN_RESP;
  sc_time delay = SC_ZERO_TIME;
  const auto status = from[master]->nb_transport_bw(*trans, phase, delay);
  if (status == tlm::TLM_COMPLETED ||
      (status == tlm::TLM_UPDATED && phase == tlm::END_RESP)) {
    events_.notify(*trans, tlm::END_RESP, delay);
  } else if (status != tlm::TLM_ACCEPTED) {
    LV_FATAL("{}: illegal initiator response update", name());
  }
}

void AddressRouter::Blocking(int, Payload &trans, sc_time &delay) {
  const auto *region = Route(trans);
  if (!region) {
    trans.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
  } else {
    const auto address =
        trans.get_address() -
        (region->map.subtract_start_addr ? region->map.addr : 0);
    auto *copy = CopyManagedRequest(trans, address);
    to[region->target]->b_transport(*copy, delay);
    CopyResponse(trans, *copy);
  }
  trans.set_dmi_allowed(false);
}

unsigned int AddressRouter::Debug(int, Payload &trans) {
  const auto *region = Route(trans, true);
  if (!region) {
    trans.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
    trans.set_dmi_allowed(false);
    return 0;
  }
  const auto address = trans.get_address() -
                       (region->map.subtract_start_addr ? region->map.addr : 0);
  auto *copy = CopyManagedRequest(trans, address, true);
  const auto result = to[region->target]->transport_dbg(*copy);
  CopyResponse(trans, *copy, false);
  trans.set_dmi_allowed(false);
  return result;
}

}  // namespace nic::detail
