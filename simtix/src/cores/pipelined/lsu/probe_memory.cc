// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include <liblv/binding.h>
#include <liblv/common/tlm_sink.h>
#include <liblv/schema.h>
#include <systemc.h>
#include <tlm.h>

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <memory>
#include <sol/sol.hpp>
#include <string>
#include <vector>

namespace simtix::pipelined {

class LsuProbeMemory : public sc_module {
 public:
  struct Param {
    uint64_t size = 1024;
    uint32_t fifo_size = 16;

    LV_SCHEMA(LsuProbeMemory, Param,
              LV_FIELD(size, "Size of the backing memory in bytes"),
              LV_FIELD(fifo_size, "Request and response FIFO size"))
  };

  using LuaBytes = sol::as_table_t<std::vector<uint8_t>>;

  LsuProbeMemory(const sc_module_name &name, const Param &param)
      : sc_module(name),
        mem_(param.size, 0),
        sink_(
            "sink",
            [this](tlm::tlm_generic_payload &trans) {
              return ProcessRequest(&trans);
            },
            param.fifo_size) {
    SC_THREAD(ProcessThread);
  }

  auto port() const { return &sink_.port; }
  size_t size() const { return mem_.size(); }

  void clear_requests() { requests_.clear(); }
  size_t num_requests() const { return requests_.size(); }

  std::string request_command(size_t index) const {
    const auto &req = RequestAt(index);
    if (req.command == tlm::TLM_READ_COMMAND) return "read";
    if (req.command == tlm::TLM_WRITE_COMMAND) return "write";
    return "unknown";
  }

  uint64_t request_addr(size_t index) const { return RequestAt(index).addr; }
  uint32_t request_length(size_t index) const {
    return RequestAt(index).length;
  }

  LuaBytes request_data(size_t index) const {
    return sol::as_table(RequestAt(index).data);
  }

  LuaBytes request_byte_enable(size_t index) const {
    return sol::as_table(RequestAt(index).byte_enable);
  }

  LuaBytes read_bytes(uint64_t addr, size_t size) const {
    assert(addr <= mem_.size());
    auto offset = static_cast<size_t>(addr);
    assert(size <= mem_.size() - offset);
    return sol::as_table(std::vector<uint8_t>(mem_.begin() + offset,
                                              mem_.begin() + offset + size));
  }

  void write_bytes(uint64_t addr, const LuaBytes &data) {
    auto bytes = data.value();
    assert(addr <= mem_.size());
    auto offset = static_cast<size_t>(addr);
    assert(bytes.size() <= mem_.size() - offset);
    std::copy(bytes.begin(), bytes.end(), mem_.begin() + offset);
  }

 private:
  struct RequestRecord {
    tlm::tlm_command command = tlm::TLM_IGNORE_COMMAND;
    uint64_t addr = 0;
    uint32_t length = 0;
    uint32_t byte_enable_length = 0;
    std::vector<uint8_t> data;
    std::vector<uint8_t> byte_enable;
  };

  const RequestRecord &RequestAt(size_t index) const {
    assert(index >= 1);
    assert(index <= requests_.size());
    return requests_[index - 1];
  }

  void ProcessThread() {
    for (;;) {
      auto *trans = sink_.req_port->read();
      ProcessRequest(trans);
      sink_.resp_port->write(trans);
    }
  }

  unsigned int ProcessRequest(tlm::tlm_generic_payload *trans) {
    auto command = trans->get_command();
    auto addr = trans->get_address();
    auto *data_ptr = trans->get_data_ptr();
    auto length = trans->get_data_length();
    auto *byte_enable_ptr = trans->get_byte_enable_ptr();
    auto byte_enable_length = trans->get_byte_enable_length();

    RequestRecord record;
    record.command = command;
    record.addr = addr;
    record.length = length;
    record.byte_enable_length = byte_enable_length;
    if (byte_enable_ptr != nullptr && byte_enable_length > 0) {
      record.byte_enable.assign(byte_enable_ptr,
                                byte_enable_ptr + byte_enable_length);
    }

    if (addr > mem_.size()) {
      trans->set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
      requests_.push_back(std::move(record));
      return 0;
    }
    auto offset = static_cast<size_t>(addr);
    if (length > mem_.size() - offset) {
      trans->set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
      requests_.push_back(std::move(record));
      return 0;
    }

    if (data_ptr == nullptr && length != 0) {
      trans->set_response_status(tlm::TLM_GENERIC_ERROR_RESPONSE);
      requests_.push_back(std::move(record));
      return 0;
    }

    if (command == tlm::TLM_READ_COMMAND) {
      std::copy(mem_.begin() + offset, mem_.begin() + offset + length,
                data_ptr);
      record.data.assign(data_ptr, data_ptr + length);
    } else if (command == tlm::TLM_WRITE_COMMAND) {
      record.data.assign(data_ptr, data_ptr + length);
      if (byte_enable_ptr == nullptr) {
        std::copy(data_ptr, data_ptr + length, mem_.begin() + offset);
      } else if (byte_enable_length == 0) {
        trans->set_response_status(tlm::TLM_GENERIC_ERROR_RESPONSE);
        requests_.push_back(std::move(record));
        return 0;
      } else {
        for (uint32_t i = 0; i < length; ++i) {
          if (byte_enable_ptr[i % byte_enable_length] == TLM_BYTE_ENABLED) {
            mem_[offset + i] = data_ptr[i];
          }
        }
      }
    } else {
      trans->set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
      requests_.push_back(std::move(record));
      return 0;
    }

    trans->set_response_status(tlm::TLM_OK_RESPONSE);
    requests_.push_back(std::move(record));
    return length;
  }

  std::vector<uint8_t> mem_;
  lv::TlmSink sink_;
  std::vector<RequestRecord> requests_;
};

LV_BINDING(simtix, LsuProbeMemory)
    .constructor(
        [](const char *name, const LsuProbeMemory::Param &param) {
          return std::make_shared<LsuProbeMemory>(name, param);
        },
        lv::params("name", "param"),
        lv::doc("Create a memory target that records LSU requests"))
    .property("port", &LsuProbeMemory::port, lv::doc("Memory request port"))
    .property("size", &LsuProbeMemory::size, lv::doc("Memory size in bytes"))
    .method("clear_requests", &LsuProbeMemory::clear_requests,
            lv::doc("Clear recorded LSU requests"))
    .method("num_requests", &LsuProbeMemory::num_requests,
            lv::doc("Return the number of recorded LSU requests"))
    .method("request_command", &LsuProbeMemory::request_command,
            lv::params("index"), lv::doc("Return recorded request command"))
    .method("request_addr", &LsuProbeMemory::request_addr, lv::params("index"),
            lv::doc("Return recorded request address"))
    .method("request_length", &LsuProbeMemory::request_length,
            lv::params("index"), lv::doc("Return recorded request length"))
    .method("request_data", &LsuProbeMemory::request_data, lv::params("index"),
            lv::doc("Return recorded request data"))
    .method("request_byte_enable", &LsuProbeMemory::request_byte_enable,
            lv::params("index"),
            lv::doc("Return recorded request byte-enable data"))
    .method("read_bytes", &LsuProbeMemory::read_bytes,
            lv::params("addr", "size"), lv::doc("Read bytes from memory"))
    .method("write_bytes", &LsuProbeMemory::write_bytes,
            lv::params("addr", "data"), lv::doc("Write bytes to memory"));

}  // namespace simtix::pipelined
