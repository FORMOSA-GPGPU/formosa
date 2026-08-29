/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <liblv/common/tlm_sink.h>
#include <systemc.h>
#include <tlm.h>

#include <sol/sol.hpp>
#include <string>

namespace simple {

class PrintBuf : public sc_module {
 public:
  PrintBuf(const sc_module_name &name, uint64_t num_entries,
           std::string filename);
  ~PrintBuf();

  // Lua API implementations
  using LuaBytes = sol::as_table_t<std::vector<uint8_t>>;
  void write_bytes(uint64_t addr, const LuaBytes &data);

  void set_clock(sc_clock *clock);
  sc_clock *clock() const;

  auto port() { return &sink_.port; }

  uint64_t num_entries() const { return num_entries_; }

  void FlushBuffer(uint64_t addr);
  void FlushAll();
  void CloseFile();

 private:
  void ProcessMethod();

  unsigned int ReceiveData(tlm::tlm_generic_payload *trans);

  uint64_t num_entries_;

  // Buffer for each entry
  std::unordered_map<uint64_t, std::string> buf_;

  sc_clock *clock_;    // pointer to the clock source
  sc_in_clk clock_i_;  // input clock signal

  lv::TlmSink sink_;

  std::ofstream file_;
};

}  // namespace simple
