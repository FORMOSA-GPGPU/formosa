// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include <liblv/binding.h>
#include <liblv/common/tlm_sink.h>
#include <systemc.h>
#include <tlm_core/tlm_2/tlm_generic_payload/tlm_gp.h>

#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

#include "Vvclint.h"

namespace simple {

using lv::TlmSink;

class VClint : public sc_module {
 public:
  using IntSignal = sc_signal<bool>;

  explicit VClint(const sc_module_name &name)
      : sc_module(name),
        timer_irq_("timer_irq", 1),
        msip_irq_("msip_irq", 1),
        clock_i_("clock"),
        top_(std::make_unique<Vvclint>(&context_)),
        sink_(
            "sink",
            [this](tlm::tlm_generic_payload &trans) {
              return ProcessRequest(&trans);
            },
            16) {
    timer_irq_[0].write(false);
    msip_irq_[0].write(false);
    timer_irq_ptrs_.push_back(&timer_irq_[0]);
    msip_irq_ptrs_.push_back(&msip_irq_[0]);
    top_->clk = 0;
    top_->mtime_tick = 0;
    top_->we = 0;
    top_->re = 0;
    top_->addr = 0;
    top_->wdata = 0;
    top_->eval();
    top_->rst_n = 0;
    Pulse(false);
    Pulse(false);
    top_->rst_n = 1;
    Pulse(false);

    SC_METHOD(Tick);
    sensitive << clock_i_.pos();
    dont_initialize();

    SC_METHOD(RequestMethod);
  }

  void set_clock(sc_clock *clock) {
    sc_assert(clock != nullptr);
    clock_ = clock;
    clock_i_.bind(*clock_);
  }

  sc_clock *clock() const { return clock_; }

  auto port() const { return &sink_.port; }

  sol::as_table_t<std::vector<IntSignal *>> timer_irq() {
    return sol::as_table(timer_irq_ptrs_);
  }

  sol::as_table_t<std::vector<IntSignal *>> msip_irq() {
    return sol::as_table(msip_irq_ptrs_);
  }

 private:
  void Pulse(bool mtime_tick) {
    top_->mtime_tick = mtime_tick ? 1 : 0;
    top_->clk = 1;
    top_->eval();
    top_->clk = 0;
    top_->eval();
    top_->mtime_tick = 0;
  }

  void UpdateIrq() {
    timer_irq_[0].write((top_->timer_irq & 1) != 0);
    msip_irq_[0].write((top_->msip_irq & 1) != 0);
  }

  void Tick() {
    Pulse(true);
    UpdateIrq();
  }

  struct BusResult {
    std::uint32_t rdata;
    bool err;
  };

  BusResult BusOp(std::uint16_t addr, std::uint32_t wdata, bool we, bool re) {
    top_->addr = addr;
    top_->wdata = wdata;
    top_->we = we ? 1 : 0;
    top_->re = re ? 1 : 0;
    if (we) {
      Pulse(false);
    } else {
      top_->eval();
    }
    BusResult result{top_->rdata, top_->err_addr != 0};
    top_->we = 0;
    top_->re = 0;
    top_->eval();
    if (we) UpdateIrq();
    return result;
  }

  void RequestMethod() {
    bool is_ready =
        sink_.req_port->num_available() > 0 && sink_.resp_port->num_free() > 0;
    if (!is_ready) {
      next_trigger(sink_.req_port->data_written_event() |
                   sink_.resp_port->data_read_event());
      return;
    }

    if (clock_->posedge()) {
      tlm::tlm_generic_payload *trans;
      sink_.req_port->nb_read(trans);
      trans->acquire();
      ProcessRequest(trans);
      sink_.resp_port->nb_write(trans);
      trans->release();
    }

    next_trigger(clock_->posedge_event());
  }

  unsigned int ProcessRequest(tlm::tlm_generic_payload *trans) {
    auto cmd = trans->get_command();
    auto addr = trans->get_address();
    auto ptr = trans->get_data_ptr();
    auto len = trans->get_data_length();

    if (addr % 4 != 0 || addr + len > 0x10000) {
      trans->set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
      return 0;
    }
    if (len != 4 && len != 8) {
      trans->set_response_status(tlm::TLM_GENERIC_ERROR_RESPONSE);
      return 0;
    }
    if (len == 8 && addr < 0x4000) {
      trans->set_response_status(tlm::TLM_GENERIC_ERROR_RESPONSE);
      return 0;
    }

    if (cmd == tlm::TLM_READ_COMMAND) {
      for (unsigned off = 0; off < len; off += 4) {
        BusResult r =
            BusOp(static_cast<std::uint16_t>(addr + off), 0, false, true);
        if (r.err) {
          trans->set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
          return 0;
        }
        std::memcpy(ptr + off, &r.rdata, 4);
      }
    } else if (cmd == tlm::TLM_WRITE_COMMAND) {
      for (unsigned off = 0; off < len; off += 4) {
        std::uint32_t w = 0;
        std::memcpy(&w, ptr + off, 4);
        BusResult r =
            BusOp(static_cast<std::uint16_t>(addr + off), w, true, false);
        if (r.err) {
          trans->set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
          return 0;
        }
      }
    } else {
      SC_REPORT_ERROR("TLM-2",
                      "Illegal transaction command received by VClint");
    }
    trans->set_response_status(tlm::TLM_OK_RESPONSE);
    return len;
  }

  sc_vector<IntSignal> timer_irq_;
  sc_vector<IntSignal> msip_irq_;
  std::vector<IntSignal *> timer_irq_ptrs_;
  std::vector<IntSignal *> msip_irq_ptrs_;

  sc_clock *clock_ = nullptr;
  sc_in<bool> clock_i_;
  VerilatedContext context_;
  std::unique_ptr<Vvclint> top_;
  TlmSink sink_;
};

LV_BINDING(simple, VClint)
    .constructor(
        [](const char *name) {
          return std::make_shared<VClint>(name);
        },
        lv::params("name"), lv::doc("Create a Verilated CLINT"))
    .property("port", &VClint::port, lv::doc("CLINT request port"))
    .property("clock", &VClint::clock, &VClint::set_clock,
              lv::doc("SystemC clock"))
    .property("timer_irq", &VClint::timer_irq,
              lv::doc("Timer interrupt signals"))
    .property("msip_irq", &VClint::msip_irq,
              lv::doc("Software interrupt signals"));

}  // namespace simple
