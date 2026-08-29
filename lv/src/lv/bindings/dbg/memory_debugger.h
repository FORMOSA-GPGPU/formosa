/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <systemc.h>
#include <tlm.h>
#include <tlm_utils/simple_initiator_socket.h>

#include <elfio/elfio.hpp>
#include <sol/types.hpp>

namespace dbg {

class MemoryDebugger : public sc_module {
 public:
  using Socket = tlm_utils::simple_initiator_socket<MemoryDebugger>;
  using Target = Socket::base_target_socket_type;

  explicit MemoryDebugger(const sc_module_name &name);
  ~MemoryDebugger() = default;

  // Lua API implementations
  using LuaBytes = sol::as_table_t<std::vector<uint8_t>>;

  LuaBytes read_bytes(uint64_t addr, size_t size);
  size_t write_bytes(uint64_t addr, const LuaBytes &data);

  bool load_elf(ELFIO::elfio *elfio);

  void set_target(Target *target);
  Target *target() const;

  Socket mem_port_;

 protected:
  size_t SendTransactionDbg(tlm::tlm_command cmd, uint64_t addr, uint8_t *bytes,
                            size_t size);
  Target *target_;
};

}  // namespace dbg
