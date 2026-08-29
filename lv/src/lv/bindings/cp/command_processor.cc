// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include "command_processor.h"

#include <fmt/format.h>
#include <liblv/binding.h>
#include <liblv/mm/pool.h>
#include <liblv/schema.h>
#include <systemc.h>
#include <tlm.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <sol/sol.hpp>
#include <string>

namespace cp {
CommandProcessor::CommandProcessor(const sc_module_name &modname,
                                   uint64_t mhart_id, const Param &param)
    : sc_module(modname),
      core(mhart_id, param.core_trace),
      mem_if("mem_if"),
      debug_mode(param.rsp_enable),
      stub(false, param.rsp_port, param.rsp_trace) {
  core.set_instr_mem(&mem_if);
  core.set_data_mem(&mem_if);
  /* Set pc if required */
  SC_THREAD(thread_proc);
  sensitive << clk.pos();
  SC_METHOD(read_ext_interrupt);
  sensitive << ext_interrupt;
  SC_METHOD(read_sw_interrupt);
  sensitive << sw_interrupt;
  SC_METHOD(read_timer_interrupt);
  sensitive << timer_interrupt;
  dont_initialize();

  stub_port_ = param.rsp_port;
  stub.set_debug_if(&core);
  if (param.core_trace) {
    core.enable_debug();
  }
}

void CommandProcessor::thread_proc() {
  while (true) {
    wait();
    if (debug_mode &&
        core.get_status() == rv64::CoreExecStatus::HitBreakpoint) {
      /* Connect to port only when needed */
      if (!stub.connected()) stub.connect(stub_port_);
      stub.gdb_main();
    } else {
      core.run_step();
    }
  }
}

void CommandProcessor::read_ext_interrupt() {
  core.ext_interrupt(ext_interrupt.read());
}

void CommandProcessor::read_sw_interrupt() {
  core.sw_interrupt(sw_interrupt.read());
}
void CommandProcessor::read_timer_interrupt() {
  core.timer_interrupt(timer_interrupt.read());
}

TLM_MemIF::TLM_MemIF(const sc_module_name &name) : sc_module(name) {
  mem_port_.register_nb_transport_bw(this, &TLM_MemIF::nb_transport_bw);
  for (auto &c : byte_enable_) {
    c = TLM_BYTE_ENABLED;
  }
}

void TLM_MemIF::mem_req_(tlm::tlm_generic_payload &trans) {
  tlm::tlm_phase phase = tlm::BEGIN_REQ;
  sc_time delay = SC_ZERO_TIME;
  mem_port_->nb_transport_fw(trans, phase, delay);
  wait(req_ack_);
  wait(resp_recv_);
  return;
}

void TLM_MemIF::mem_resp_ack_(tlm::tlm_generic_payload &trans) {
  tlm::tlm_phase phase = tlm::END_RESP;
  sc_time delay = SC_ZERO_TIME;
  mem_port_->nb_transport_fw(trans, phase, delay);
  return;
}

inline void TLM_MemIF::setup_trans(tlm::tlm_generic_payload &trans,
                                   tlm::tlm_command command, uint64_t addr,
                                   uint64_t len, unsigned char *ptr) {
  trans.set_command(command);
  trans.set_address(addr);
  trans.set_data_length(len);
  trans.set_data_ptr(ptr);
  trans.set_byte_enable_ptr(byte_enable_.data());
  trans.set_byte_enable_length(len);
}

template <uint64_t len, typename retT, typename castT>
inline retT TLM_MemIF::load_(uint64_t addr) {
  tlm::tlm_generic_payload &trans = *lv::mm::Pool::Allocate();
  unsigned char data_arr[len];
  setup_trans(trans, tlm::TLM_READ_COMMAND, addr, len, data_arr);

  trans.acquire();
  mem_req_(trans);

  retT data =
      static_cast<retT>(*reinterpret_cast<castT *>(trans.get_data_ptr()));

  mem_resp_ack_(trans);
  trans.release();
  return data;
}

uint32_t TLM_MemIF::load_instr(uint64_t pc) {
  return load_<4, uint32_t, uint32_t>(pc);
}

int64_t TLM_MemIF::load_double(uint64_t addr) {
  return load_<8, int64_t, int64_t>(addr);
}

int64_t TLM_MemIF::load_word(uint64_t addr) {
  return load_<4, int64_t, int32_t>(addr);
}

int64_t TLM_MemIF::load_half(uint64_t addr) {
  return load_<2, int64_t, int16_t>(addr);
}

int64_t TLM_MemIF::load_byte(uint64_t addr) {
  return load_<1, int64_t, int8_t>(addr);
}

uint64_t TLM_MemIF::load_uword(uint64_t addr) {
  return load_<4, uint64_t, uint32_t>(addr);
}

uint64_t TLM_MemIF::load_uhalf(uint64_t addr) {
  return load_<2, uint64_t, uint16_t>(addr);
}

uint64_t TLM_MemIF::load_ubyte(uint64_t addr) {
  return load_<1, uint64_t, uint8_t>(addr);
}

template <uint64_t len, typename T>
inline void TLM_MemIF::store_(uint64_t addr, T value) {
  tlm::tlm_generic_payload *trans = lv::mm::Pool::Allocate();
  setup_trans(*trans, tlm::TLM_WRITE_COMMAND, addr, len,
              reinterpret_cast<unsigned char *>(&value));
  trans->acquire();
  mem_req_(*trans);
  mem_resp_ack_(*trans);
  trans->release();
}

void TLM_MemIF::store_double(uint64_t addr, uint64_t value) {
  store_<8, uint64_t>(addr, value);
}

void TLM_MemIF::store_word(uint64_t addr, uint32_t value) {
  store_<4, uint64_t>(addr, value);
}

void TLM_MemIF::store_half(uint64_t addr, uint16_t value) {
  store_<2, uint64_t>(addr, value);
}

void TLM_MemIF::store_byte(uint64_t addr, uint8_t value) {
  store_<1, uint64_t>(addr, value);
}

tlm::tlm_sync_enum TLM_MemIF::nb_transport_bw(tlm::tlm_generic_payload &trans,
                                              tlm::tlm_phase &phase,
                                              sc_time &delay) {
  if (phase == tlm::END_REQ) {
    req_ack_.notify();
  } else if (phase == tlm::BEGIN_RESP) {
    resp_recv_.notify();
  } else {
    SC_REPORT_ERROR("TLM-2", "Illegal transaction phase recievd by MEM_IF");
  }
  return tlm::TLM_ACCEPTED;
}

void TLM_MemIF::bind_mem_port(Target *t) { mem_port_.bind(*t); }

void CommandProcessor::set_clk(sc_clock *c) {
  clk_ = c;
  clk.bind(*clk_);
}

sc_clock *CommandProcessor::get_clk() const { return clk_; }

CommandProcessor::Target *CommandProcessor::get_target() const {
  return target_;
}

void CommandProcessor::set_target(Target *t) {
  target_ = t;
  mem_if.bind_mem_port(t);
}

void CommandProcessor::set_pc(uint64_t pc) { core.set_pc(pc); }

void CommandProcessor::set_ext_int(sc_signal<bool> *s) {
  ext_interrupt.bind(*s);
}

void CommandProcessor::set_sw_int(sc_signal<bool> *s) { sw_interrupt.bind(*s); }

void CommandProcessor::set_timer_int(sc_signal<bool> *s) {
  timer_interrupt.bind(*s);
}

LV_BINDING(cp, CommandProcessor)
    .constructor(
        [](const char *name, const uint64_t mhart_id,
           const CommandProcessor::Param &param) {
          return std::make_shared<CommandProcessor>(name, mhart_id, param);
        },
        lv::params("name", "mhart_id", "param"),
        lv::doc("Create a RISC-V command processor"))
    .property("clock", &CommandProcessor::get_clk, &CommandProcessor::set_clk,
              lv::doc("SystemC clock"))
    .property("target", &CommandProcessor::get_target,
              &CommandProcessor::set_target, lv::doc("Memory target"))
    .method("set_pc", &CommandProcessor::set_pc, lv::params("pc"),
            lv::doc("Set the initial program counter"))
    .property("ext_int", &CommandProcessor::set_ext_int,
              lv::doc("External interrupt signal"))
    .property("sw_int", &CommandProcessor::set_sw_int,
              lv::doc("Software interrupt signal"))
    .property("timer_int", &CommandProcessor::set_timer_int,
              lv::doc("Timer interrupt signal"));

}  // namespace cp
