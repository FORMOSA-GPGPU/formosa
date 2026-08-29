// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include "sm_transactor.h"

#include <fmt/format.h>
#include <liblv/binding.h>
#include <liblv/schema.h>
#include <liblv/trace.h>

#include <cstdlib>
#include <ctime>
#include <memory>

namespace formosa {

namespace {

struct Param {
  int limit = 16;
  bool delay = true;
  bool enable_trace = false;

  // clang-format off
  LV_SCHEMA(SMTransactor, Param,
            LV_FIELD(limit, "Maximum queued kernel packet count"),
            LV_FIELD(delay, "Randomize dequeue delay"),
            LV_FIELD(enable_trace, "Enable Perfetto trace"))
  // clang-format on
};

}  // namespace

SMTransactor::SMTransactor(const sc_module_name &name, int limit, bool delay,
                           bool trace)
    : slave_("slave", nullptr),
      packet_limit_(limit),
      delay_(delay),
      pf_packet_(LV_NEW_MODULE_TRACK("Kernel")) {
  SC_THREAD(csr_proc);
  SC_THREAD(kernel_proc);

  pf_packet_.set_enabled(trace);
}

void SMTransactor::csr_proc() {
  std::fill(csr_mem_.begin(), csr_mem_.end(), 0);
  while (true) {
    wait(clk->posedge_event());
    auto *trans = slave_.req_port->read();
    auto cmd = trans->get_command();
    auto addr = trans->get_address() / 8;
    auto size = trans->get_data_length();
    auto *data_ptr = reinterpret_cast<uint64_t *>(trans->get_data_ptr());

    if (addr >= ST_CSR_END) {
      SC_REPORT_ERROR(
          "SMTransactor",
          fmt::format("No CSR is mapped to 0x{:016x}", addr).c_str());
    }

    if (cmd == tlm::TLM_READ_COMMAND) {
      if (size == 1) {
        *reinterpret_cast<uint8_t *>(data_ptr) =
            (csr_mem_[addr] >> (8 * (trans->get_address() & 0x7))) & 0xFF;
      } else if (size == 8) {
        *data_ptr = csr_mem_[addr];
      } else {
        SC_REPORT_ERROR("SMTransactor", "CSR Read: Unsupported data size\n");
      }
    } else if (cmd == tlm::TLM_WRITE_COMMAND) {
      switch (addr) {
        case ST_ENQ_VALID:
          if (csr_mem_[ST_ENQ_VALID] == 0) {
            int delay = delay_ ? static_cast<uint64_t>(rand()) % 32 : 0;

            if (packet_queue_.size() == 0) {
              LV_TRACE_BEGIN(
                  pf_packet_, "SMTransactor",
                  fmt::format(
                      "SMTransactor:kernel_pc:0x{:016x}#info_ptr:0x{:016x}",
                      csr_mem_[ST_ENQ_KERNEL_PC], csr_mem_[ST_ENQ_INFO_PTR]));
            }

            packet_queue_.push(
                {delay, csr_mem_[ST_ENQ_KERNEL_PC], csr_mem_[ST_ENQ_INFO_PTR]});
          } else {
            SC_REPORT_WARNING("SMTransactor",
                              "Writing to ENQ_VALID when it is 1");
          }
          break;
        case ST_DEQ_VALID:
          if (csr_mem_[ST_DEQ_VALID] == 1) {
            LV_TRACE_END(pf_packet_);
            if (packet_queue_.size() != 1) {
              auto &p = packet_queue_.front();
              LV_TRACE_BEGIN(
                  pf_packet_, "SMTransactor",
                  fmt::format(
                      "SMTransactor:kernel_pc:0x{:016x}#info_ptr:0x{:016x}",
                      p.kernel_pc, p.info_ptr));
            }
            packet_queue_.pop();
          } else {
            SC_REPORT_WARNING("SMTransactor",
                              fmt::format("Writing to DEQ_VALID when it is {}",
                                          csr_mem_[ST_DEQ_VALID])
                                  .c_str());
          }
          break;
        case ST_ENQ_KERNEL_PC:
        case ST_ENQ_INFO_PTR:
        case ST_ENQ_GROUP_SIZE:
          csr_mem_[addr] = *data_ptr;
          break;
        case ST_DEQ_STATUS:
        case ST_DEQ_KERNEL_PC:
        case ST_DEQ_INFO_PTR:
        case ST_DEQ_MCAUSE:
        case ST_DEQ_MEPC:
        case ST_DEQ_MTVAL:
          SC_REPORT_WARNING(
              "SMTransactor",
              fmt::format(
                  "Writing to CSR:{} is probably not the desired behavior",
                  ST_CSR_NAME[addr])
                  .c_str());
          csr_mem_[addr] = *data_ptr;
          break;
      }
    } else {
      SC_REPORT_ERROR(
          "TLM-2",
          "Illegal transaction command received by SMTransactor slave");
    }

    trans->set_response_status(tlm::TLM_OK_RESPONSE);
    slave_.resp_port->write(trans);
  }
}

void SMTransactor::kernel_proc() {
  while (true) {
    wait(clk->posedge_event());
    if (packet_queue_.empty()) {
      csr_mem_[ST_DEQ_VALID] = 0;
      continue;
    }

    if (packet_queue_.size() == packet_limit_) {
      csr_mem_[ST_ENQ_VALID] = 1;
    } else {
      csr_mem_[ST_ENQ_VALID] = 0;
    }

    auto &p = packet_queue_.front();
    csr_mem_[ST_DEQ_KERNEL_PC] = p.kernel_pc;
    csr_mem_[ST_DEQ_INFO_PTR] = p.info_ptr;
    if (p.delay-- <= 0) {
      csr_mem_[ST_DEQ_VALID] = 1;
    } else {
      csr_mem_[ST_DEQ_VALID] = 0;
    }
  }
}

void SMTransactor::set_clock(sc_clock *clk) {
  clock_ = clk;
  this->clk(*clk);
}

sc_clock *SMTransactor::clock() const { return clock_; }

SMTransactor::Source *SMTransactor::slave_port() const { return &slave_.port; }

LV_BINDING(formosa, SMTransactor)
    .constructor(
        [](const char *name, const Param &param) {
          return std::make_shared<SMTransactor>(name, param.limit, param.delay,
                                                param.enable_trace);
        },
        lv::params("name", "param"),
        lv::doc("Create a stream-multiprocessor transactor"))
    .property("clock", &SMTransactor::clock, &SMTransactor::set_clock,
              lv::doc("SystemC clock"))
    .property("slave_port", &SMTransactor::slave_port,
              lv::doc("SM MMIO slave port"));
}  // namespace formosa
