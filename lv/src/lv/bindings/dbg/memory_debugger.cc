// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include "memory_debugger.h"

#include <liblv/binding.h>
#include <liblv/mm/pool.h>
#include <liblv/output.h>

#include <memory>
#include <vector>

namespace dbg {

MemoryDebugger::MemoryDebugger(const sc_module_name &name)
    : sc_module(name), mem_port_("mem_port") {}

MemoryDebugger::LuaBytes MemoryDebugger::read_bytes(uint64_t addr,
                                                    size_t size) {
  std::vector<uint8_t> data(size);
  SendTransactionDbg(tlm::TLM_READ_COMMAND, addr, data.data(), size);
  return sol::as_table(data);
}

size_t MemoryDebugger::write_bytes(uint64_t addr, const LuaBytes &bytes) {
  std::vector<uint8_t> data = bytes.value();
  return SendTransactionDbg(tlm::TLM_WRITE_COMMAND, addr, data.data(),
                            data.size());
}

void MemoryDebugger::set_target(Target *target) {
  target_ = target;
  mem_port_.bind(*target_);
}

bool MemoryDebugger::load_elf(ELFIO::elfio *elfio) {
  for (auto &pseg : elfio->segments) {
    if (pseg->get_type() == ELFIO::PT_LOAD) {
      size_t size = pseg->get_file_size();
      uint64_t addr = pseg->get_virtual_address();

      auto data = std::make_unique<uint8_t[]>(size);
      memcpy(data.get(), pseg->get_data(), size);
      size_t size_ret =
          SendTransactionDbg(tlm::TLM_WRITE_COMMAND, addr, data.get(), size);
      if (size_ret != size) {
        LV_WARNING("{} != {}", size_ret, size);
        return false;
      }
    }
  }
  return true;
}

size_t MemoryDebugger::SendTransactionDbg(tlm::tlm_command cmd, uint64_t addr,
                                          uint8_t *data_ptr,
                                          size_t data_length) {
  auto *trans = lv::mm::Pool::Allocate();
  trans->acquire();
  trans->set_command(cmd);
  trans->set_address(addr);
  trans->set_data_length(data_length);
  trans->set_byte_enable_ptr(0);  // 0 indicates unused
  trans->set_data_ptr(data_ptr);

  size_t size_ret = mem_port_->transport_dbg(*trans);

  trans->release();
  return size_ret;
}

MemoryDebugger::Target *MemoryDebugger::target() const { return target_; }

LV_BINDING(dbg, MemoryDebugger)
    .constructor(
        [](const char *name) {
          return std::make_shared<MemoryDebugger>(name);
        },
        lv::params("name"), lv::doc("Create a memory debugger"))
    .property("target", &MemoryDebugger::target, &MemoryDebugger::set_target,
              lv::doc("Debug transport target"))
    .method("read_bytes", &MemoryDebugger::read_bytes,
            lv::params("addr", "size"), lv::doc("Read bytes from memory"))
    .method("write_bytes", &MemoryDebugger::write_bytes,
            lv::params("addr", "bytes"), lv::doc("Write bytes to memory"))
    .method("load_elf", &MemoryDebugger::load_elf, lv::params("elf"),
            lv::doc("Load an ELF into memory"));

}  // namespace dbg
