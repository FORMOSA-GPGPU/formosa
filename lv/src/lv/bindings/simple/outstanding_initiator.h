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

#include <cstdint>
#include <deque>
#include <sol/sol.hpp>
#include <unordered_map>
#include <vector>

namespace simple {

class OutstandingInitiator : public sc_module {
 public:
  using Target = lv::TlmSource::Target;
  using LuaBytes = sol::as_table_t<std::vector<uint8_t>>;

  explicit OutstandingInitiator(const sc_module_name &name);
  ~OutstandingInitiator() override;

  void set_target(Target *target);
  Target *target() const;

  void add_payload(const sol::table &payload);
  sol::optional<LuaBytes> get_read_data();
  uint64_t completed_count() const;

 private:
  void AddPayloadImpl(tlm::tlm_command command, uint64_t addr,
                      const std::vector<uint8_t> &payload_data, uint64_t ip);
  void RequestThread();
  void ResponseThread();
  void ReleasePayload(tlm::tlm_generic_payload *trans);

  Target *target_ = nullptr;
  lv::TlmSource source_;

  std::unordered_map<tlm::tlm_generic_payload *, std::vector<uint8_t>>
      payload_data_map_;
  std::deque<tlm::tlm_generic_payload *> request_queue_;
  std::deque<std::vector<uint8_t>> read_data_;
  sc_event request_event_;
  uint64_t completed_count_ = 0;
};

}  // namespace simple
