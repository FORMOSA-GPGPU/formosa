// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include "memory.h"

#include <fmt/core.h>
#include <liblv/binding.h>
#include <sysc/kernel/sc_event.h>
#include <sysc/kernel/sc_simcontext.h>
#include <sysc/kernel/sc_thread_process.h>
#include <tlm_core/tlm_2/tlm_2_interfaces/tlm_fw_bw_ifs.h>
#include <tlm_core/tlm_2/tlm_generic_payload/tlm_gp.h>
#include <tlm_core/tlm_2/tlm_generic_payload/tlm_phase.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

namespace simple {

Memory::Memory(const sc_module_name &name, const Param &param)
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
  read_track_.set_enabled(param.trace);
  write_track_.set_enabled(param.trace);
  mem_ = new uint8_t[param.size];
  std::fill(mem_, mem_ + param.size, 0);
  SC_THREAD(ProcessThread);
}

Memory::~Memory() { delete[] mem_; }

void Memory::ProcessThread() {
  while (true) {
    // blocking call to get a request
    tlm::tlm_generic_payload *trans = sink_.req_port->read();

    const lv::trace::Track &track =
        trans->is_read() ? read_track_ : write_track_;
    LV_TRACE_BEGIN(track, trans->is_read() ? "Read" : "Write",
                   fmt::format("{:#x}:{:#x}", trans->get_address(),
                               trans->get_data_length()));
    ProcessRequest(trans);
    // wait for the latency before writing the response
    for (unsigned i = 0; i < latency_; ++i) {
      wait(clock_i_->posedge_event());
    }
    LV_TRACE_END(track);
    stats_.fifo_full_events += sink_.resp_port->num_free() == 0;
    sink_.resp_port->write(trans);
  }
}

unsigned int Memory::ProcessRequest(tlm::tlm_generic_payload *trans) {
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

  if (cmd == tlm::TLM_READ_COMMAND) {
    memcpy(ptr, &mem_[addr], len);
    stats_.total_reads++;
  } else if (cmd == tlm::TLM_WRITE_COMMAND) {
    if (!byte) {
      memcpy(&mem_[addr], ptr, len);
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

Memory::LuaBytes Memory::read_bytes(uint64_t addr, size_t size) const {
  std::vector<uint8_t> data;
  data.insert(data.end(), mem_ + addr, mem_ + addr + size);
  return sol::as_table(data);
}

void Memory::write_bytes(uint64_t addr, const Memory::LuaBytes &data) {
  std::copy(data.value().begin(), data.value().end(), mem_ + addr);
}

void Memory::set_clock(sc_clock *clock) {
  clock_ = clock;
  clock_i_.bind(*clock_);
}

sc_clock *Memory::clock() const { return clock_; }

void Memory::load_elf(ELFIO::elfio &elf) {
  /* Load PT_LOAD sections to memory */
  for (auto &seg : elf.segments) {
    if (seg->get_type() != ELFIO::PT_LOAD) continue;
    uint64_t filesz = seg->get_file_size();
    uint64_t memsz = seg->get_memory_size();
    uint64_t addr = seg->get_physical_address();
    const char *dp = seg->get_data();
    for (uint64_t size = 0; size < filesz; size++) {
      mem_[addr + size] = *(dp + size);
    }
    for (uint64_t i = filesz; i < memsz; i++) {
      mem_[addr + i] = 0;
    }
  }
}

LV_BINDING(simple, Memory)
    .constructor(
        [](const char *name, const Memory::Param &param) {
          return std::make_shared<Memory>(name, param);
        },
        lv::params("name", "param"), lv::doc("Create a byte-addressed memory"))
    .property("port", &Memory::port, lv::doc("Memory request port"))
    .property("size", &Memory::size, lv::doc("Memory size in bytes"))
    .property("clock", &Memory::clock, &Memory::set_clock,
              lv::doc("SystemC clock"))
    .method("read_bytes", &Memory::read_bytes, lv::params("addr", "size"),
            lv::doc("Read bytes from memory"))
    .method("write_bytes", &Memory::write_bytes, lv::params("addr", "data"),
            lv::doc("Write bytes to memory"))
    .method("load_elf", &Memory::load_elf, lv::params("elf"),
            lv::doc("Load an ELF into memory"))
    .property("stats", &Memory::stats, lv::doc("Statistics group"));

}  // namespace simple
