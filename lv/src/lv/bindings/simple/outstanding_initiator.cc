// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include "outstanding_initiator.h"

#include <liblv/binding.h>
#include <liblv/mm/pool.h>
#include <liblv/output.h>

#include <string_view>
#include <utility>

namespace simple {

OutstandingInitiator::OutstandingInitiator(const sc_module_name &name)
    : sc_module(name), source_("source") {
  SC_THREAD(RequestThread);

  SC_THREAD(ResponseThread);
}

OutstandingInitiator::~OutstandingInitiator() {
  request_queue_.clear();
  while (!payload_data_map_.empty()) {
    ReleasePayload(payload_data_map_.begin()->first);
  }
}

void OutstandingInitiator::set_target(Target *target) {
  target_ = target;
  source_.set_target(target_);
}

OutstandingInitiator::Target *OutstandingInitiator::target() const {
  return target_;
}

using namespace std::literals::string_view_literals;  // NOLINT

void OutstandingInitiator::add_payload(const sol::table &payload) {
  if (sol::optional<LuaBytes> data = payload["data"]) {
    const uint64_t addr = payload.get_or("addr"sv, 0);
    const uint64_t ip = payload.get_or("ip"sv, 0);
    AddPayloadImpl(tlm::TLM_WRITE_COMMAND, addr, data->value(), ip);
  } else if (sol::optional<size_t> size = payload["size"]) {
    const uint64_t addr = payload.get_or("addr"sv, 0);
    const uint64_t ip = payload.get_or("ip"sv, 0);
    AddPayloadImpl(tlm::TLM_READ_COMMAND, addr, std::vector<uint8_t>(*size),
                   ip);
  } else {
    lv::Warning("Incorrect payload format, ignoring\n");
  }
}

sol::optional<OutstandingInitiator::LuaBytes>
OutstandingInitiator::get_read_data() {
  if (read_data_.empty()) {
    return std::nullopt;
  }

  auto data = std::move(read_data_.front());
  read_data_.pop_front();
  return sol::as_table(data);
}

uint64_t OutstandingInitiator::completed_count() const {
  return completed_count_;
}

void OutstandingInitiator::AddPayloadImpl(
    tlm::tlm_command command, uint64_t addr,
    const std::vector<uint8_t> &payload_data, uint64_t ip) {
  if (target_ == nullptr) {
    lv::Fatal("OutstandingInitiator target is not bound\n");
  }

  tlm::tlm_generic_payload *trans = lv::mm::Pool::Allocate();
  trans->acquire();

  auto &owned_data = payload_data_map_[trans] = payload_data;

  if (ip != 0) {
    auto *ext = new lv::IpExtension;
    ext->ip = ip;
    trans->set_extension(ext);
  }

  trans->set_command(command);
  trans->set_address(addr);
  trans->set_data_ptr(owned_data.data());
  trans->set_data_length(owned_data.size());
  trans->set_streaming_width(owned_data.size());
  trans->set_byte_enable_ptr(nullptr);
  trans->set_dmi_allowed(false);
  trans->set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);

  request_queue_.push_back(trans);
  request_event_.notify(SC_ZERO_TIME);
}

void OutstandingInitiator::RequestThread() {
  while (true) {
    while (!request_queue_.empty()) {
      tlm::tlm_generic_payload *trans = request_queue_.front();
      request_queue_.pop_front();

      source_.req_port->write(trans);
    }

    wait(request_event_);
  }
}

void OutstandingInitiator::ResponseThread() {
  while (true) {
    tlm::tlm_generic_payload *trans = source_.resp_port->read();
    auto it = payload_data_map_.find(trans);
    if (it == payload_data_map_.end()) {
      SC_REPORT_ERROR("OutstandingInitiator",
                      "Received response for unknown transaction");
      continue;
    }

    if (trans->is_read() &&
        trans->get_response_status() == tlm::TLM_OK_RESPONSE) {
      read_data_.push_back(std::move(it->second));
    }

    ++completed_count_;
    ReleasePayload(trans);
  }
}

void OutstandingInitiator::ReleasePayload(tlm::tlm_generic_payload *trans) {
  lv::IpExtension *ext = nullptr;
  trans->get_extension(ext);
  if (ext != nullptr) {
    trans->clear_extension(ext);
    delete ext;
  }

  payload_data_map_.erase(trans);
  trans->release();
}

LV_BINDING(simple, OutstandingInitiator)
    .constructor(
        [](const char *name) {
          return std::make_shared<OutstandingInitiator>(name);
        },
        lv::params("name"),
        lv::doc("Create a scripted TLM initiator with multiple outstanding "
                "requests"))
    .property("target", &OutstandingInitiator::target,
              &OutstandingInitiator::set_target, lv::doc("Memory target"))
    .method("add_payload", &OutstandingInitiator::add_payload,
            lv::params("payload"), lv::doc("Issue a read or write payload"))
    .method("get_read_data", &OutstandingInitiator::get_read_data,
            lv::doc("Get the next completed read response"))
    .method("completed_count", &OutstandingInitiator::completed_count,
            lv::doc("Get the number of completed payloads"));

}  // namespace simple
