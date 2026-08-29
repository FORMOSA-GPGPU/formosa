// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#define SC_INCLUDE_DYNAMIC_PROCESSES

#include <liblv/binding.h>
#include <liblv/common/tlm_sink.h>
#include <liblv/common/tlm_source.h>
#include <liblv/output.h>
#include <liblv/trace.h>
#include <systemc.h>
#include <tlm_core/tlm_2/tlm_generic_payload/tlm_gp.h>

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace simple {

using lv::Fatal;
using lv::TlmSink;
using lv::TlmSource;
using lv::Warning;
using lv::trace::Track;

template <bool EnablePerfetto = false>
class XBarImpl : public sc_module {
 public:
  struct map_entry {
    uint64_t addr;
    uint64_t size;
    bool subtract_start_addr;
  };

  /**
   * @details Construct a TlmSource and TlmSink for each master and slave port.
   * For each core/memory-side port, dynamically create a corresponding PEQ and
   * register a separate thread to handle transactions from that PEQ.
   */
  explicit XBarImpl(const sc_module_name &name, uint32_t num_masters,
                    const std::vector<map_entry> &addr_map, bool enable_trace)
      : sc_module(name) {
    // Create core-side sinks and corresponding backward PEQs
    for (uint32_t i = 0; i < num_masters; ++i) {
      core_side_.emplace_back(new TlmSink(
          fmt::format("core_side_{}", i).c_str(),
          [this](tlm::tlm_generic_payload &trans) {
            return PutRequestDbg(trans);
          },
          16));

      // Create a backward PEQ for each core side port
      backward_peqs_.emplace_back(
          new PayloadPeq(fmt::format("backward_peq_{}", i).c_str()));
      backward_peq_map_[core_side_.back()] = backward_peqs_.back();
      // Register backward PEQ threads
      sc_process_handle h =
          sc_spawn(sc_bind(&XBarImpl::BackwardPeqThread, this, i),
                   sc_gen_unique_name("bwd_peq_thread"));
    }

    if (addr_map.empty()) {
      Fatal("Empty address map configuration is invalid");
    }

    // Create mem-side sources and corresponding forward PEQs based on the
    // address map
    for (uint32_t i = 0; i < addr_map.size(); ++i) {
      const auto &entry = addr_map[i];
      mem_side_.emplace_back(
          new TlmSource(fmt::format("mem_side_{}", i).c_str()));

      if (!AddSlave(mem_side_[i], entry.addr, entry.size,
                    entry.subtract_start_addr)) {
        Fatal("Address space {:#x}:{:#x} overlaps with existing slaves",
              entry.addr, entry.size);
      }

      // Create a forward PEQ for each mem side port
      forward_peqs_.emplace_back(
          new PayloadPeq(fmt::format("forward_peq_{}", i).c_str()));
      forward_peq_map_[mem_side_.back()] = forward_peqs_.back();
      // Register forward PEQ threads
      sc_process_handle h =
          sc_spawn(sc_bind(&XBarImpl::ForwardPeqThread, this, i),
                   sc_gen_unique_name("fwd_peq_thread"));
    }

    SC_METHOD(ForwardMethod);
    SC_METHOD(BackwardMethod);

    // Update sensitivity lists for core-side requests and mem-side responses
    for (auto core_side : core_side_) {
      core_request_events_ |= core_side->req_port->data_written_event();
    }
    for (auto mem_side : mem_side_) {
      mem_response_events_ |= mem_side->resp_port->data_written_event();
    }

    // Create trace tracks if Perfetto tracing is enabled
    if constexpr (EnablePerfetto) {
      pf_tracks_.resize(num_masters);

      for (uint32_t i = 0; i < num_masters; ++i) {
        pf_tracks_[i].reserve(addr_map.size());
        for (uint32_t j = 0; j < addr_map.size(); ++j) {
          auto track = LV_NEW_MODULE_TRACK(
              fmt::format("CoreSide{}->MemSide{}", i, j).c_str());
          track.set_enabled(enable_trace);
          pf_tracks_[i].emplace_back(std::move(track));
        }
      }
    }
  }

  ~XBarImpl() override {
    for (auto sink : core_side_) {
      delete sink;
    }
    for (auto source : mem_side_) {
      delete source;
    }
    for (auto peq : forward_peqs_) {
      delete peq;
    }
    for (auto peq : backward_peqs_) {
      delete peq;
    }
  }

  unsigned int PutRequestDbg(tlm::tlm_generic_payload &trans) {
    const uint64_t orig_addr = trans.get_address();
    auto slave = Route(&trans);

    if (!slave.has_value()) {
      auto addr = trans.get_address();
      auto size = trans.get_data_length();
      Fatal("Address {:#x}:{:#x} not routed to any slave", addr, size);
      return 0;
    }

    auto ret = slave.value()->next_level_port->PutRequestDbg(trans);
    trans.set_address(orig_addr);
    return ret;
  }

  sol::as_table_t<std::vector<TlmSource *>> mem_side() {
    return sol::as_table(mem_side_);
  }

  sol::as_table_t<std::vector<TlmSink *>> core_side() {
    return sol::as_table(core_side_);
  }

  void set_clock(sc_clock *clock) {
    clock_ = clock;
    clock_i_.bind(*clock_);
  }

  sc_clock *clock() const { return clock_; }

 private:
  struct slave_t {
    TlmSource *next_level_port;
    uint64_t addr;
    uint64_t upper;
    bool subtract_start_addr;
  };

  using SlaveIter = typename std::vector<slave_t>::iterator;
  using PayloadPeq = tlm_utils::peq_with_get<tlm::tlm_generic_payload>;

  // Type alias for Perfetto tracks (only enabled when XBar EnablePerfetto is
  // true)
  using PfTracksT = std::conditional_t<
      EnablePerfetto,
      std::vector<std::vector<Track>>,  // [core_side][mem_side]
      std::monostate>;

  std::vector<TlmSource *> mem_side_;
  std::vector<TlmSink *> core_side_;
  PfTracksT pf_tracks_;
  std::vector<PayloadPeq *> forward_peqs_, backward_peqs_;
  sc_clock *clock_;
  sc_in<bool> clock_i_;
  std::vector<slave_t> slaves_;  // Routing table
  std::unordered_map<tlm::tlm_generic_payload *, TlmSink *>
      route_back_map_;  // For backward routing
  std::unordered_map<tlm::tlm_generic_payload *, uint64_t> orig_addr_map_;
  std::unordered_map<TlmSource *, PayloadPeq *> forward_peq_map_;
  std::unordered_map<TlmSink *, PayloadPeq *> backward_peq_map_;

  // Sensitivity lists for core-side requests and mem-side responses
  sc_event_or_list core_request_events_;
  sc_event_or_list mem_response_events_;

  bool AddSlave(TlmSource *port, uint64_t addr, uint64_t size,
                bool subtract_start_addr) {
    if (size == 0) {
      return false;
    }

    /*
     * Overlap Detection:
     * Two ranges are disjoint (Safe) if:
     * 1. New range is strictly above: (addr > s.upper)
     * 2. New range is strictly below: (end_addr < s.addr)
     *
     * Safe = (addr > s.upper) || (end_addr < s.addr)
     *
     * Therefore, they overlap if !Safe:
     * Overlap = !(addr > s.upper || end_addr < s.addr)
     * =  (addr <= s.upper) && (end_addr >= s.addr)
     */

    uint64_t end_addr = addr + size - 1;
    if (std::any_of(slaves_.cbegin(), slaves_.cend(),
                    [addr, end_addr](const slave_t &s) {
                      return addr <= s.upper && end_addr >= s.addr;
                    })) {
      // Address overlaps with an existing slave
      return false;
    }

    slaves_.emplace_back(
        slave_t{port, addr, addr + size - 1, subtract_start_addr});
    return true;
  }

  std::optional<SlaveIter> Route(tlm::tlm_generic_payload *trans) {
    auto addr = trans->get_address();
    auto size = trans->get_data_length();
    auto it = std::find_if(
        slaves_.begin(), slaves_.end(), [addr, size](const slave_t &s) {
          return addr >= s.addr && (addr + size - 1) <= s.upper;
        });
    if (it == slaves_.end()) {
      // No matching slave found
      return std::nullopt;
    }

    if (it->subtract_start_addr) {
      trans->set_address(addr - it->addr);
    }

    return it;
  }

  /**
   * @details In each cycle, the XBar collects at most one request from
   * each core-side port and notifies the corresponding PEQ to enqueue
   * the transaction.
   * **Note**: `Calling peq->notify(trans, SC_ZERO_TIME)` does not guarantee the
   * ordering of transactions within the same delta cycle.
   */
  void ForwardMethod() {
    bool has_any_request =
        std::any_of(core_side_.begin(), core_side_.end(), [](TlmSink *sink) {
          return sink->req_port->num_available() > 0;
        });

    if (!has_any_request) {
      // No request to process, wait for any request event from core sides
      next_trigger(core_request_events_);
      return;
    }

    // Only process on positive clock edge
    if (clock_->posedge()) {
      for (size_t i = 0; i < core_side_.size(); ++i) {
        auto core_side = core_side_[i];

        if (!core_side->req_port->num_available()) {
          // This sink has no request to process
          continue;
        }

        // Get the request from core side
        tlm::tlm_generic_payload *trans;
        core_side->req_port->nb_read(trans);
        trans->acquire();

        // original address and size before routing
        const auto orig_addr = trans->get_address();
        const auto size = trans->get_data_length();

        auto result = Route(trans);

        if (!result.has_value()) {
          Warning("Address {:#x}:{:#x} not routed to any slave ({})", orig_addr,
                  size, name());
          trans->set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
          // Response the payload back to the core side
          backward_peq_map_[core_side]->notify(*trans, SC_ZERO_TIME);
          continue;
        }

        auto it = result.value();

        // Perfetto trace
        if constexpr (EnablePerfetto) {
          auto cmd = trans->get_command();
          bool is_write = (cmd == tlm::TLM_WRITE_COMMAND);
          bool is_read = (cmd == tlm::TLM_READ_COMMAND);
          std::string_view op_str =
              is_write ? "Write" : (is_read ? "Read" : "Other");

          size_t slave_idx = std::distance(slaves_.begin(), it);
          LV_TRACE_INSTANT(pf_tracks_[i][slave_idx], "XBar", op_str,
                           LV_TRACE_ARG("address", orig_addr),
                           LV_TRACE_ARG("size", size),
                           LV_TRACE_ARG("core_side_idx", i),
                           LV_TRACE_ARG("mem_side_idx", slave_idx));
        }

        // Store the sink for backward routing
        route_back_map_[trans] = core_side;
        orig_addr_map_[trans] = orig_addr;
        // Notify the corresponding peq to process the request
        forward_peq_map_[it->next_level_port]->notify(*trans, SC_ZERO_TIME);
      }
    }

    next_trigger(clock_->posedge_event());
  }

  /**
   * @brief Shared forward PEQ thread for each mem-side port.
   * @param index The index of the mem-side port.
   * @details This PEQ thread is used to blockingly send requests to its
   * corresponding mem-side device.
   */
  void ForwardPeqThread(size_t index) {
    auto &peq = *forward_peqs_[index];
    auto &mem_side = mem_side_[index];

    while (true) {
      wait(peq.get_event());
      tlm::tlm_generic_payload *trans = nullptr;
      while ((trans = peq.get_next_transaction()) != nullptr) {
        // Blocking put the request to mem side port
        mem_side->req_port->write(trans);
      }
    }
  }

  /**
   * @details In each cycle, the XBar collects at most one response from
   * each mem-side port and notifies the corresponding PEQ to enqueue
   * the transaction.
   * **Note**: `Calling peq->notify(trans, SC_ZERO_TIME)` does not guarantee the
   * ordering of transactions within the same delta cycle.
   */
  void BackwardMethod() {
    bool has_any_response =
        std::any_of(mem_side_.begin(), mem_side_.end(), [](TlmSource *source) {
          return source->resp_port->num_available() > 0;
        });

    if (!has_any_response) {
      // No response to process, wait for any response event from mem sides
      next_trigger(mem_response_events_);
      return;
    }

    // Only process on positive clock edge
    if (clock_->posedge()) {
      for (size_t mem_idx = 0; mem_idx < mem_side_.size(); ++mem_idx) {
        auto mem_side = mem_side_[mem_idx];
        if (mem_side->resp_port->num_available() == 0) {
          // This source has no response to process
          continue;
        }
        // Get the response from mem side
        tlm::tlm_generic_payload *trans;
        mem_side->resp_port->nb_read(trans);

        auto core_side = route_back_map_.find(trans);
        if (core_side == route_back_map_.end()) {
          Fatal("No core side found for response with address {:#x}",
                trans->get_address());
        }

        auto orig_addr_it = orig_addr_map_.find(trans);
        if (orig_addr_it != orig_addr_map_.end()) {
          trans->set_address(orig_addr_it->second);
          orig_addr_map_.erase(orig_addr_it);
        }

        // Notify the corresponding peq to process the response
        backward_peq_map_[core_side->second]->notify(*trans);
        // Remove the mapping
        route_back_map_.erase(core_side);
      }
    }

    next_trigger(clock_->posedge_event());
  }

  /**
   * @brief Shared backward PEQ thread for each core-side port.
   * @param index The index of the core-side port.
   * @details This PEQ thread is used to blockingly send responses to its
   * corresponding core-side device.
   */
  void BackwardPeqThread(size_t index) {
    auto &peq = *backward_peqs_[index];
    auto &core_side = core_side_[index];

    while (true) {
      wait(peq.get_event());
      tlm::tlm_generic_payload *trans = nullptr;
      while ((trans = peq.get_next_transaction()) != nullptr) {
        // Blocking put the response to core side port
        core_side->resp_port->write(trans);
        trans->release();
      }
    }
  }
};

using XBar = XBarImpl<ENABLE_PERFETTO>;

// A helper function to extract value from table, allowing ULL (cdata) from Lua.
sol::optional<uint64_t> get_uint64_or_cdata(const sol::table &table,
                                            const char *key) {
  sol::object obj = table[key];
  assert(obj.valid());
  if (obj.is<uint64_t>()) {
    return obj.as<uint64_t>();
  }

  sol::state_view state = obj.lua_state();

  std::string type_name = state["type"](obj);

  if (type_name == "cdata") {
    const void *raw_ptr = obj.pointer();
    assert(raw_ptr);
    return *static_cast<const uint64_t *>(raw_ptr);
  }
  return sol::nullopt;
}

std::vector<XBar::map_entry> ParseAddressMap(
    const sol::nested<std::vector<sol::table>> &table) {
  const auto &addr_map_lua = table.value();
  std::vector<XBar::map_entry> addr_map;
  addr_map.reserve(addr_map_lua.size());
  for (const auto &entry : addr_map_lua) {
    sol::optional<uint64_t> addr = entry.get<uint64_t>("addr");
    sol::optional<uint64_t> size = get_uint64_or_cdata(entry, "size");
    bool subtract_start_addr = entry.get_or("subtract_start_addr", true);
    if (!(addr.has_value() && size.has_value())) {
      Fatal("Cannot parse address map configuration for XBar");
    }

    addr_map.push_back({addr.value(), size.value(), subtract_start_addr});
  }
  return addr_map;
}

LV_BINDING(simple, XBar)
    .constructor(
        [](const char *name, uint32_t num_masters,
           sol::nested<std::vector<sol::table>> table) {
          return std::make_shared<XBar>(name, num_masters,
                                        ParseAddressMap(table), false);
        },
        lv::params(
            lv::param("name"), lv::param("num_masters"),
            lv::param("addr_map",
                      lv::lua_type("{ addr: integer, size: integer, "
                                   "subtract_start_addr?: boolean }[]"))),
        lv::doc("Create a TLM crossbar"))
    .constructor(
        [](const char *name, uint32_t num_masters,
           sol::nested<std::vector<sol::table>> table, bool enable_trace) {
          return std::make_shared<XBar>(name, num_masters,
                                        ParseAddressMap(table), enable_trace);
        },
        lv::params(lv::param("name"), lv::param("num_masters"),
                   lv::param("addr_map",
                             lv::lua_type("{ addr: integer, size: integer, "
                                          "subtract_start_addr?: boolean }[]")),
                   lv::param("enable_trace")),
        lv::doc("Create a traced TLM crossbar"))
    .property("clock", &XBar::clock, &XBar::set_clock, lv::doc("SystemC clock"))
    .property("mem_side", &XBar::mem_side, lv::doc("Memory-side source ports"))
    .property("core_side", &XBar::core_side, lv::doc("Core-side sink ports"));

}  // namespace simple
