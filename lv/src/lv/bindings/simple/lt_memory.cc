// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include <liblv/binding.h>
#include <liblv/schema.h>
#include <systemc.h>
#include <tlm.h>
#include <tlm_utils/simple_target_socket.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <elfio/elfio.hpp>
#include <memory>
#include <sol/sol.hpp>
#include <stdexcept>
#include <vector>

namespace simple {

// A stable, byte-addressed LT target. No process, clock or request queue:
// standard socket adaptation supplies NB transport for AT initiators.
class LtMemory : public sc_module {
 public:
  struct Param {
    int size = 1024;
    sc_time latency = SC_ZERO_TIME;
    LV_SCHEMA(LtMemory, Param, LV_FIELD(size, "Size in bytes (positive)"),
              LV_FIELD(latency, "Read/write access latency"))
  };

  LtMemory(const sc_module_name &name, const Param &param);
  using LuaBytes = sol::as_table_t<std::vector<uint8_t>>;
  auto port() { return &socket_; }
  size_t size() const { return memory_.size(); }
  LuaBytes read_bytes(uint64_t addr, size_t size) const;
  void write_bytes(uint64_t addr, const LuaBytes &data);
  void load_elf(ELFIO::elfio &elf, uint64_t base = 0);

 private:
  tlm_utils::simple_target_socket<LtMemory> socket_;
  // Never resize: outstanding DMI grants refer into this allocation.
  std::vector<uint8_t> memory_;
  const sc_time latency_;
  bool InBounds(uint64_t addr, uint64_t length) const;
  unsigned Transfer(tlm::tlm_generic_payload &trans, bool debug);
  void Blocking(tlm::tlm_generic_payload &trans, sc_time &delay);
  bool Dmi(tlm::tlm_generic_payload &trans, tlm::tlm_dmi &dmi);
  unsigned Debug(tlm::tlm_generic_payload &trans);
};

LtMemory::LtMemory(const sc_module_name &name, const Param &param)
    : sc_module(name), socket_("port"), latency_(param.latency) {
  if (param.size <= 0)
    throw std::invalid_argument("LtMemory size must be positive");
  memory_.resize(param.size, 0);
  socket_.register_b_transport(this, &LtMemory::Blocking);
  socket_.register_get_direct_mem_ptr(this, &LtMemory::Dmi);
  socket_.register_transport_dbg(this, &LtMemory::Debug);
}

bool LtMemory::InBounds(uint64_t addr, uint64_t length) const {
  return addr <= size() && length <= size() - addr;
}

unsigned LtMemory::Transfer(tlm::tlm_generic_payload &trans, bool debug) {
  trans.set_dmi_allowed(false);
  const auto addr = trans.get_address();
  const auto length = trans.get_data_length();
  if (!InBounds(addr, length)) {
    trans.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
    return 0;
  }
  if (!trans.is_read() && !trans.is_write()) {
    trans.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
    return 0;
  }
  if (!length || !trans.get_data_ptr()) {
    trans.set_response_status(tlm::TLM_GENERIC_ERROR_RESPONSE);
    return 0;
  }
  if (!debug && trans.get_streaming_width() < length) {
    trans.set_response_status(tlm::TLM_BURST_ERROR_RESPONSE);
    return 0;
  }
  const auto *enable = debug ? nullptr : trans.get_byte_enable_ptr();
  const auto enable_length = trans.get_byte_enable_length();
  if (enable) {
    if (!enable_length) {
      trans.set_response_status(tlm::TLM_BYTE_ENABLE_ERROR_RESPONSE);
      return 0;
    }
    for (unsigned i = 0; i < length; ++i) {
      if (enable[i % enable_length] != TLM_BYTE_ENABLED &&
          enable[i % enable_length] != TLM_BYTE_DISABLED) {
        trans.set_response_status(tlm::TLM_BYTE_ENABLE_ERROR_RESPONSE);
        return 0;
      }
    }
  }
  auto *data = trans.get_data_ptr();
  for (unsigned i = 0; i < length; ++i) {
    if (enable && enable[i % enable_length] == TLM_BYTE_DISABLED) continue;
    if (trans.is_read())
      data[i] = memory_[addr + i];
    else
      memory_[addr + i] = data[i];
  }
  trans.set_response_status(tlm::TLM_OK_RESPONSE);
  trans.set_dmi_allowed(true);
  return length;
}

void LtMemory::Blocking(tlm::tlm_generic_payload &trans, sc_time &delay) {
  const auto transferred = Transfer(trans, false);
  if (transferred) delay += latency_;
}

bool LtMemory::Dmi(tlm::tlm_generic_payload &trans, tlm::tlm_dmi &dmi) {
  trans.set_dmi_allowed(false);
  if (trans.get_address() >= size() ||
      (!trans.is_read() && !trans.is_write())) {
    return false;
  }
  dmi.set_start_address(0);
  dmi.set_end_address(size() - 1);
  dmi.set_dmi_ptr(memory_.data());
  dmi.allow_read_write();
  dmi.set_read_latency(latency_);
  dmi.set_write_latency(latency_);
  trans.set_dmi_allowed(true);
  return true;
}

unsigned LtMemory::Debug(tlm::tlm_generic_payload &trans) {
  return Transfer(trans, true);
}

LtMemory::LuaBytes LtMemory::read_bytes(uint64_t addr, size_t length) const {
  if (!InBounds(addr, length)) throw std::out_of_range("LtMemory read_bytes");
  return sol::as_table(std::vector<uint8_t>(memory_.begin() + addr,
                                            memory_.begin() + addr + length));
}

void LtMemory::write_bytes(uint64_t addr, const LuaBytes &data) {
  if (!InBounds(addr, data.value().size()))
    throw std::out_of_range("LtMemory write_bytes");
  std::copy(data.value().begin(), data.value().end(), memory_.begin() + addr);
}

void LtMemory::load_elf(ELFIO::elfio &elf, uint64_t base) {
  for (const auto &segment : elf.segments) {
    if (segment->get_type() != ELFIO::PT_LOAD) continue;
    const auto paddr = segment->get_physical_address();
    const auto addr = paddr - base;
    const auto filesz = segment->get_file_size();
    const auto memsz = segment->get_memory_size();
    if (paddr < base || filesz > memsz || !InBounds(addr, memsz)) {
      throw std::out_of_range("LtMemory PT_LOAD physical address/size");
    }
    if (filesz) std::memcpy(memory_.data() + addr, segment->get_data(), filesz);
    std::fill(memory_.begin() + addr + filesz, memory_.begin() + addr + memsz,
              0);
  }
}

LV_BINDING(simple, LtMemory)
    .constructor(
        [](const char *name, const LtMemory::Param &param) {
          return std::make_shared<LtMemory>(name, param);
        },
        lv::params("name", "param"),
        lv::doc("Create a loosely timed byte memory"))
    .property("port", &LtMemory::port, lv::doc("Standard TLM memory target"))
    .property("size", &LtMemory::size, lv::doc("Memory size in bytes"))
    .method("read_bytes", &LtMemory::read_bytes, lv::params("addr", "size"),
            lv::doc("Read memory bytes"))
    .method("write_bytes", &LtMemory::write_bytes, lv::params("addr", "data"),
            lv::doc("Write memory bytes"))
    .method(
        "load_elf",
        [](LtMemory &self, ELFIO::elfio &elf) {
          self.load_elf(elf);
        },
        lv::params("elf"),
        lv::doc("Load PT_LOAD physical segments and zero BSS"))
    .method("load_elf", &LtMemory::load_elf, lv::params("elf", "base"),
            lv::doc("Load PT_LOAD physical segments relative to base and zero "
                    "BSS"));

}  // namespace simple
