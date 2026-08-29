// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include "stub_cache_mmio.h"

#include "liblv/binding.h"
#include "liblv/output.h"

namespace lv {
namespace formosa {

void StubCacheMmio::HandleMmioReq() {
  bool ready =
      port_.req_port->num_available() > 0 && port_.resp_port->num_free() > 0;

  if (!ready) {
    next_trigger(port_.req_port->data_written_event() |
                 port_.resp_port->data_read_event());
    return;
  }

  if (clk->posedge()) {
    auto *trans = port_.req_port->read();
    auto addr = trans->get_address();
    auto data = trans->get_data_ptr();
    auto size = trans->get_data_length();

    if (addr >= sizeof(csr_) || addr + size > sizeof(csr_)) {
      trans->set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
      port_.resp_port->write(trans);
      return;
    }

    if (trans->is_write()) {
      if (addr == 0 && csr_.start == 0 && data[0] == 1) {
        // Can only write the start CSR when it is 0
        start_cache_op_.notify();
        csr_.start = 1;
      } else {
        std::memcpy(&csr_mem_[addr], data, size);
      }
    } else if (trans->is_read()) {
      std::memcpy(data, &csr_mem_[addr], size);
    }

    trans->set_response_status(tlm::TLM_OK_RESPONSE);
    port_.resp_port->write(trans);
  }

  next_trigger(clk->posedge_event());
}

void StubCacheMmio::HandleCacheOperations() {
  if (csr_.start) {
    // Only process the request at posedge of the clock
    if (!clk->posedge()) {
      next_trigger(clk.posedge_event());
      return;
    }

    if (verbose_) {
      if (csr_.mode > 2) {
        LV_WARNING("Invalid operation ({}) on {:#x}:{:#x}", csr_.mode,
                   csr_.addr, csr_.size);
      } else {
        LV_INFO("{} on {:#x}:{:#x}", kOpName[csr_.mode], csr_.addr, csr_.size);
      }
    }
    csr_.start = 0;
  }

  // Wait for another request
  next_trigger(start_cache_op_);
}

LV_BINDING(formosa, StubCacheMmio)
    .constructor(
        [](const char *name, const StubCacheMmio::Param &param) {
          return std::make_shared<StubCacheMmio>(name, param);
        },
        lv::params("name", "param"), lv::doc("Create a cache MMIO stub"))
    .property("clock", &StubCacheMmio::clock, &StubCacheMmio::set_clock,
              lv::doc("SystemC clock"))
    .property("mmio_port", &StubCacheMmio::port, lv::doc("MMIO target port"));

}  // namespace formosa
}  // namespace lv
