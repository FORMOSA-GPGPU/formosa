/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <liblv/common/tlm_sink.h>
#include <liblv/schema.h>
#include <systemc.h>
#include <tlm.h>
#include <tlm_utils/simple_initiator_socket.h>
#include <tlm_utils/simple_target_socket.h>

#include <array>
#include <cstdint>
#include <string>

#include "gdbstub.h"
#include "rv64/mem_if.h"
#include "rv64/rv64.h"

namespace cp {

class TLM_MemIF : public instr_memory_if,
                  public data_memory_if,
                  public sc_module {
 public:
  explicit TLM_MemIF(const sc_module_name &name);
  uint32_t load_instr(uint64_t pc) override;
  int64_t load_double(uint64_t addr) override;
  int64_t load_word(uint64_t addr) override;
  int64_t load_half(uint64_t addr) override;
  int64_t load_byte(uint64_t addr) override;
  uint64_t load_uword(uint64_t addr) override;
  uint64_t load_uhalf(uint64_t addr) override;
  uint64_t load_ubyte(uint64_t addr) override;

  void store_double(uint64_t addr, uint64_t value) override;
  void store_word(uint64_t addr, uint32_t value) override;
  void store_half(uint64_t addr, uint16_t value) override;
  void store_byte(uint64_t addr, uint8_t value) override;

  tlm::tlm_sync_enum nb_transport_bw(tlm::tlm_generic_payload &trans,
                                     tlm::tlm_phase &phase, sc_time &delay);

  using Target =
      tlm_utils::simple_initiator_socket<TLM_MemIF>::base_target_socket_type;
  void bind_mem_port(Target *t);

  tlm_utils::simple_initiator_socket<TLM_MemIF> mem_port_;

 private:
  inline void setup_trans(tlm::tlm_generic_payload &trans,
                          tlm::tlm_command command, uint64_t addr, uint64_t len,
                          unsigned char *ptr);

  template <uint64_t len, typename retT, typename castT>
  inline retT load_(uint64_t addr);
  template <uint64_t len, typename T>
  inline void store_(uint64_t addr, T value);
  std::array<unsigned char, 8> byte_enable_;

  void mem_req_(tlm::tlm_generic_payload &trans);
  void mem_resp_ack_(tlm::tlm_generic_payload &trans);
  sc_event req_ack_;
  sc_event resp_recv_;
};

enum {
  CP_RESET,
  CP_FW_ADDR,
  CP_FW_SIZE,
  CP_CMD_RING_BASE,
  CP_CMD_SIZE,
  CP_CMD_RING_SIZE,
  CP_RD_PTR,
  CP_WR_PTR,
  CP_CSR_END,
};

const std::array<std::string, CP_CSR_END> CP_CSR_NAME = {
    "RESET",    "FW_ADDR",       "FW_SIZE", "CMD_RING_BASE",
    "CMD_SIZE", "CMD_RING_SIZE", "RD_PTR",  "WR_PTR",
};

class CommandProcessor : public sc_module {
 public:
  struct Param {
    bool core_trace = false;
    bool rsp_enable = false;
    int rsp_port = 0;
    bool rsp_trace = false;

    LV_SCHEMA(CommandProcessor, Param,
              LV_FIELD(core_trace, "Enable instruction trace for core"),
              LV_FIELD(rsp_enable, "Enable RSP debug server"),
              LV_FIELD(rsp_port, "RSP server port number"),
              LV_FIELD(rsp_trace, "Enable RSP packet tracing"));
  };

  using Target =
      tlm_utils::simple_initiator_socket<TLM_MemIF>::base_target_socket_type;
  using Source = const tlm_utils::simple_target_socket<lv::TlmSink>;

  CommandProcessor(const sc_module_name &name, uint64_t mhart_id,
                   const Param &param);

  void thread_proc();

  void read_ext_interrupt();
  void read_sw_interrupt();
  void read_timer_interrupt();

  void set_clk(sc_clock *t);
  sc_clock *get_clk() const;

  void set_target(Target *t);
  Target *get_target() const;

  void set_pc(uint64_t pc);
  void set_ext_int(sc_signal<bool> *s);
  void set_sw_int(sc_signal<bool> *s);
  void set_timer_int(sc_signal<bool> *s);

 private:
  sc_in<bool> SC_NAMED(clk);
  sc_in<bool> SC_NAMED(ext_interrupt);
  sc_in<bool> SC_NAMED(sw_interrupt);
  sc_in<bool> SC_NAMED(timer_interrupt);

  sc_clock *clk_;
  Target *target_;
  rv64::Core core;
  TLM_MemIF mem_if;
  bool debug_mode;
  GDBStub stub;
  int stub_port_;
};

}  // namespace cp
