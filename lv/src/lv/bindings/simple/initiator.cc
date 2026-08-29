// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include "initiator.h"

#include <liblv/binding.h>
#include <liblv/mm/pool.h>
#include <liblv/output.h>
#include <sysc/kernel/sc_simcontext.h>
#include <tlm_core/tlm_2/tlm_2_interfaces/tlm_fw_bw_ifs.h>

#include <memory>
#include <utility>
#include <vector>

namespace simple {

Initiator::Initiator(const sc_module_name &name)
    : sc_module(name),
      clock_i_("clock"),
      source_("source", 16),
      poll_flag_(false),
      completed_count_(0) {
  SC_THREAD(ThreadProcess);
  sensitive << clock_i_.pos();
}

void Initiator::ThreadProcess() {
  // Generate a sequence of read/write transactions according to elements in the
  // payload queue
  while (true) {
    while (true) {
      if (payload_q_.size() == 0) {
        if (poll_flag_) break;
        wait();
        continue;
      }
      tlm::tlm_generic_payload *trans = payload_q_.front();

      source_.req_port->write(trans);
      payload_q_.pop_front();
      trans = source_.resp_port->read();
      if (trans->is_read() &&
          trans->get_response_status() == tlm::TLM_OK_RESPONSE) {
        read_data_.push_back(std::move(payload_data_map_[trans]));
      }
      ++completed_count_;

      // Clean up the extension
      lv::IpExtension *ext = nullptr;
      trans->get_extension(ext);
      if (ext) {
        trans->clear_extension(ext);
        delete ext;
      }

      payload_data_map_.erase(trans);
      trans->release();
    }

    while (true) {
      wait();
      auto *trans = lv::mm::Pool::Allocate();
      trans->acquire();
      std::vector<uint8_t> data(8, 0);
      trans->set_command(tlm::TLM_READ_COMMAND);
      trans->set_address(poll_addr_);
      trans->set_data_ptr(data.data());
      trans->set_data_length(data.size());
      trans->set_byte_enable_ptr(nullptr);
      trans->set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
      source_.req_port->write(trans);

      trans = source_.resp_port->read();
      uint64_t val = *reinterpret_cast<uint64_t *>(trans->get_data_ptr());
      trans->release();
      if (val == poll_value_) {
        sc_pause();
        poll_flag_ = false;
        wait();
        break;
      }
    }
  }
}

void Initiator::set_target(Target *target) {
  target_ = target;
  source_.set_target(target_);
}

Initiator::Target *Initiator::target() const { return target_; }

void Initiator::set_clock(sc_clock *clock) {
  clock_ = clock;
  clock_i_.bind(*clock_);
}

sc_clock *Initiator::clock() const { return clock_; }

using namespace std::literals::string_view_literals;  // NOLINT

void Initiator::add_payload(const sol::table &payload) {
  if (sol::optional<bool> poll = payload["poll"]) {
    /* Will not add a payload to the queue */
    poll_addr_ = payload.get_or("addr"sv, 0);
    poll_flag_ = true;
    poll_value_ = payload.get_or("value"sv, 0);
  } else if (sol::optional<LuaBytes> data = payload["data"]) {
    // The payload is to write
    uint64_t addr = payload.get_or("addr"sv, 0);
    uint64_t ip = payload.get_or("ip"sv, 0);
    AddPayloadImpl(tlm::TLM_WRITE_COMMAND, addr, data->value(), ip);
  } else if (sol::optional<size_t> size = payload["size"]) {
    // The payload is to read
    uint64_t addr = payload.get_or("addr"sv, 0);
    uint64_t ip = payload.get_or("ip"sv, 0);
    std::vector<uint8_t> data(*size);
    AddPayloadImpl(tlm::TLM_READ_COMMAND, addr, data, ip);
  } else {
    LV_WARNING("Incorrect payload format, ignoring");
  }
}

sol::optional<Initiator::LuaBytes> Initiator::get_read_data() {
  if (read_data_.empty()) {
    return std::nullopt;
  } else {
    auto data = read_data_.front();
    read_data_.pop_front();
    return sol::as_table(data);
  }
}

uint64_t Initiator::completed_count() const { return completed_count_; }

void Initiator::AddPayloadImpl(tlm::tlm_command cmd, uint64_t addr,
                               const std::vector<uint8_t> &data, uint64_t ip) {
  tlm::tlm_generic_payload *trans = lv::mm::Pool::Allocate();
  trans->acquire();

  // Copy data to payload data
  auto &payload_data = payload_data_map_[trans] = data;

  if (ip != 0) {
    auto *ext = new lv::IpExtension;
    ext->ip = ip;
    trans->set_extension(ext);
  }

  trans->set_command(cmd);
  trans->set_address(addr);
  trans->set_data_ptr(payload_data.data());
  trans->set_data_length(payload_data.size());
  trans->set_streaming_width(payload_data.size());  // no streaming
  trans->set_byte_enable_ptr(0);                    // 0 indicates unused
  trans->set_dmi_allowed(false);                    // Mandatory initial value
  trans->set_response_status(
      tlm::TLM_INCOMPLETE_RESPONSE);  // Mandatory initial value
  payload_q_.push_back(trans);
}

LV_BINDING(simple, Initiator)
    .constructor(
        [](const char *name) {
          return std::make_shared<Initiator>(name);
        },
        lv::params("name"), lv::doc("Create a scripted TLM initiator"))
    .property("target", &Initiator::target, &Initiator::set_target,
              lv::doc("Memory target"))
    .property("clock", &Initiator::clock, &Initiator::set_clock,
              lv::doc("SystemC clock"))
    .method("add_payload", &Initiator::add_payload, lv::params("payload"),
            lv::doc("Queue a read, write, or polling payload"))
    .method("get_read_data", &Initiator::get_read_data,
            lv::doc("Get the next completed read response"))
    .method("completed_count", &Initiator::completed_count,
            lv::doc("Get the number of completed queued payloads"));

}  // namespace simple
