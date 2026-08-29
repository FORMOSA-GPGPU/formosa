// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include "atomic_memory.h"

#include <fmt/core.h>
#include <liblv/binding.h>

#include <algorithm>
#include <cstdint>
#include <cstring>

#include "tlm_extensions/atomic_extension.h"

namespace {

uint32_t LoadU32(const uint8_t *data) {
  uint32_t value = 0;
  std::memcpy(&value, data, sizeof(value));
  return value;
}

uint64_t LoadU64(const uint8_t *data) {
  uint64_t value = 0;
  std::memcpy(&value, data, sizeof(value));
  return value;
}

void StoreU32(uint8_t *data, uint32_t value) {
  std::memcpy(data, &value, sizeof(value));
}

void StoreU64(uint8_t *data, uint64_t value) {
  std::memcpy(data, &value, sizeof(value));
}

uint32_t ApplyAtomicOp32(simtix::AtomicExtension::Op op, uint32_t old_value,
                         uint32_t operand) {
  switch (op) {
    case simtix::AtomicExtension::Op::kSwap:
      return operand;
    case simtix::AtomicExtension::Op::kAdd:
      return old_value + operand;
    case simtix::AtomicExtension::Op::kXor:
      return old_value ^ operand;
    case simtix::AtomicExtension::Op::kAnd:
      return old_value & operand;
    case simtix::AtomicExtension::Op::kOr:
      return old_value | operand;
    case simtix::AtomicExtension::Op::kMin:
      return static_cast<uint32_t>(std::min(static_cast<int32_t>(old_value),
                                            static_cast<int32_t>(operand)));
    case simtix::AtomicExtension::Op::kMax:
      return static_cast<uint32_t>(std::max(static_cast<int32_t>(old_value),
                                            static_cast<int32_t>(operand)));
    case simtix::AtomicExtension::Op::kMinU:
      return std::min(old_value, operand);
    case simtix::AtomicExtension::Op::kMaxU:
      return std::max(old_value, operand);
  }
  return old_value;
}

uint64_t ApplyAtomicOp64(simtix::AtomicExtension::Op op, uint64_t old_value,
                         uint64_t operand) {
  switch (op) {
    case simtix::AtomicExtension::Op::kSwap:
      return operand;
    case simtix::AtomicExtension::Op::kAdd:
      return old_value + operand;
    case simtix::AtomicExtension::Op::kXor:
      return old_value ^ operand;
    case simtix::AtomicExtension::Op::kAnd:
      return old_value & operand;
    case simtix::AtomicExtension::Op::kOr:
      return old_value | operand;
    case simtix::AtomicExtension::Op::kMin:
      return static_cast<uint64_t>(std::min(static_cast<int64_t>(old_value),
                                            static_cast<int64_t>(operand)));
    case simtix::AtomicExtension::Op::kMax:
      return static_cast<uint64_t>(std::max(static_cast<int64_t>(old_value),
                                            static_cast<int64_t>(operand)));
    case simtix::AtomicExtension::Op::kMinU:
      return std::min(old_value, operand);
    case simtix::AtomicExtension::Op::kMaxU:
      return std::max(old_value, operand);
  }
  return old_value;
}

}  // namespace

namespace simtix {

AtomicMemory::AtomicMemory(const sc_module_name &name, const Param &param)
    : sc_module(name),
      read_track_(LV_NEW_MODULE_TRACK("Read")),
      write_track_(LV_NEW_MODULE_TRACK("Write")),
      size_(param.size),
      clock_i_("clock"),
      latency_(param.latency),
      sink_(
          "sink",
          [this](tlm::tlm_generic_payload &trans) {
            return ProcessRequest(&trans);
          },
          param.fifo_size),
      stats_(name) {
  read_track_.set_enabled(param.pftrace);
  write_track_.set_enabled(param.pftrace);
  mem_ = new uint8_t[param.size];
  SC_THREAD(ProcessThread);
}

AtomicMemory::~AtomicMemory() { delete[] mem_; }

AtomicMemory::LuaBytes AtomicMemory::read_bytes(uint64_t addr,
                                                size_t size) const {
  std::vector<uint8_t> data;
  data.insert(data.end(), mem_ + addr, mem_ + addr + size);
  return sol::as_table(data);
}

void AtomicMemory::write_bytes(uint64_t addr, const LuaBytes &data) {
  std::copy(data.value().begin(), data.value().end(), mem_ + addr);
}

void AtomicMemory::set_clock(std::shared_ptr<sc_clock> clock) {
  clock_ = std::move(clock);
  clock_i_.bind(*clock_);
}

bool AtomicMemory::HandleAtomicRequest(tlm::tlm_generic_payload *trans,
                                       unsigned int len) {
  AtomicExtension *ext = nullptr;
  trans->get_extension(ext);
  if (ext == nullptr) {
    return false;
  }

  if (trans->get_command() != tlm::TLM_READ_COMMAND ||
      trans->get_byte_enable_ptr() != nullptr ||
      (len != sizeof(uint32_t) && len != sizeof(uint64_t))) {
    trans->set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
    return true;
  }

  auto *ptr = trans->get_data_ptr();
  const auto addr = trans->get_address();
  if (len == sizeof(uint32_t)) {
    const uint32_t old_value = LoadU32(&mem_[addr]);
    const uint32_t operand = LoadU32(ptr);
    const uint32_t new_value = ApplyAtomicOp32(ext->op, old_value, operand);
    StoreU32(&mem_[addr], new_value);
    StoreU32(ptr, old_value);
  } else {
    const uint64_t old_value = LoadU64(&mem_[addr]);
    const uint64_t operand = LoadU64(ptr);
    const uint64_t new_value = ApplyAtomicOp64(ext->op, old_value, operand);
    StoreU64(&mem_[addr], new_value);
    StoreU64(ptr, old_value);
  }

  trans->set_response_status(tlm::TLM_OK_RESPONSE);
  return true;
}

void AtomicMemory::ProcessThread() {
  while (true) {
    tlm::tlm_generic_payload *trans = sink_.req_port->read();

    const lv::trace::Track &track =
        trans->is_read() ? read_track_ : write_track_;
    LV_TRACE_BEGIN(track, trans->is_read() ? "Read" : "Write",
                   fmt::format("{:#x}:{:#x}", trans->get_address(),
                               trans->get_data_length()));
    ProcessRequest(trans);
    for (unsigned i = 0; i < latency_; ++i) {
      wait(clock_i_->posedge_event());
    }
    LV_TRACE_END(track);
    stats_.fifo_full_events += sink_.resp_port->num_free() == 0;
    sink_.resp_port->write(trans);
  }
}

unsigned int AtomicMemory::ProcessRequest(tlm::tlm_generic_payload *trans) {
  tlm::tlm_command cmd = trans->get_command();
  sc_dt::uint64 addr = trans->get_address();
  unsigned char *ptr = trans->get_data_ptr();
  unsigned int len = trans->get_data_length();
  unsigned char *byte = trans->get_byte_enable_ptr();
  unsigned int byte_len = trans->get_byte_enable_length();

  if (addr >= size_ || (addr + len) > size_) {
    trans->set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
    return 0;
  }

  if (byte != nullptr && byte_len == 0) {
    trans->set_response_status(tlm::TLM_GENERIC_ERROR_RESPONSE);
    return 0;
  }

  if (HandleAtomicRequest(trans, len)) {
    if (trans->get_response_status() == tlm::TLM_OK_RESPONSE) {
      stats_.total_reads++;
      stats_.total_writes++;
      return len;
    }
    return 0;
  }

  if (cmd == tlm::TLM_READ_COMMAND) {
    std::memcpy(ptr, &mem_[addr], len);
    stats_.total_reads++;
  } else if (cmd == tlm::TLM_WRITE_COMMAND) {
    if (!byte) {
      std::memcpy(&mem_[addr], ptr, len);
    } else {
      for (unsigned int i = 0; i < len; i++) {
        if (byte[i % byte_len] == TLM_BYTE_ENABLED) {
          mem_[addr + i] = ptr[i];
        }
      }
    }
    stats_.total_writes++;
  } else {
    trans->set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
    SC_REPORT_ERROR("TLM-2", "Illegal transaction command received by memory");
    return 0;
  }

  trans->set_response_status(tlm::TLM_OK_RESPONSE);
  return len;
}

LV_BINDING(simtix, AtomicMemory)
    .constructor(
        [](const char *name) {
          return std::make_shared<AtomicMemory>(name, AtomicMemory::Param{});
        },
        lv::params("name"), lv::doc("Create atomic memory"))
    .constructor(
        [](const char *name, const AtomicMemory::Param &param) {
          return std::make_shared<AtomicMemory>(name, param);
        },
        lv::params("name", "param"),
        lv::doc("Create atomic memory with parameters"))
    .property("port", &AtomicMemory::port, lv::doc("Memory request port"))
    .property("size", &AtomicMemory::size, lv::doc("Memory size in bytes"))
    .property("clock", &AtomicMemory::clock, &AtomicMemory::set_clock,
              lv::doc("SystemC clock"))
    .method("read_bytes", &AtomicMemory::read_bytes, lv::params("addr", "size"),
            lv::doc("Read bytes from memory"))
    .method("write_bytes", &AtomicMemory::write_bytes,
            lv::params("addr", "data"), lv::doc("Write bytes to memory"))
    .property("stats", &AtomicMemory::stats, lv::doc("Statistics group"));

}  // namespace simtix
