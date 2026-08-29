/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <liblv/common/ip_extension.h>
#include <liblv/common/tlm_source.h>
#include <systemc.h>
#include <tlm.h>
#include <tlm_utils/simple_initiator_socket.h>
#include <tlm_utils/simple_target_socket.h>

#include <deque>
#include <sol/sol.hpp>
#include <unordered_map>
#include <vector>

namespace simple {

class Initiator : sc_module {
 public:
  using Target =
      tlm_utils::simple_initiator_socket<Initiator>::base_target_socket_type;

  explicit Initiator(const sc_module_name &name);

  void ThreadProcess();

  void set_target(Target *t);
  Target *target() const;

  void set_clock(sc_clock *clock);
  sc_clock *clock() const;

  // Lua API implementations
  using LuaBytes = sol::as_table_t<std::vector<uint8_t>>;

  void add_payload(const sol::table &payload);
  sol::optional<LuaBytes> get_read_data();
  uint64_t completed_count() const;

 private:
  void AddPayloadImpl(tlm::tlm_command command, uint64_t addr,
                      const std::vector<uint8_t> &payload_data, uint64_t ip);

  Target *target_;
  sc_clock *clock_;

  sc_in<bool> clock_i_;

  std::deque<tlm::tlm_generic_payload *> payload_q_;
  std::unordered_map<tlm::tlm_generic_payload *, std::vector<uint8_t>>
      payload_data_map_;
  std::deque<std::vector<uint8_t>> read_data_;

  lv::TlmSource source_;

  bool poll_flag_;
  uint64_t poll_addr_;
  uint64_t poll_value_;
  uint64_t completed_count_;
};

}  // namespace simple
