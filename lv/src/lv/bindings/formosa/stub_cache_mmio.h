/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <liblv/common/tlm_sink.h>
#include <liblv/schema.h>

#include <systemc>

namespace lv {
namespace formosa {

class StubCacheMmio : public sc_module {
 public:
  struct Param {
    bool verbose = false;
    LV_SCHEMA(StubCacheMmio, Param,
              LV_FIELD(verbose, "Whether or not to output debug info"))
  };

  sc_in<bool> clk;

  StubCacheMmio(const sc_module_name &name, const Param &param)
      : sc_module(name),
        verbose_(param.verbose),
        csr_mem_(reinterpret_cast<uint8_t *>(&csr_)),
        port_("mmio_port") {
    SC_METHOD(HandleMmioReq);
    SC_METHOD(HandleCacheOperations);
  }

  void set_clock(std::shared_ptr<sc_clock> clock) {
    clk.bind(*clock);
    clock_ = clock;
  }
  sc_clock *clock() const { return clock_.get(); };

  const tlm_utils::simple_target_socket<TlmSink> *port() const {
    return &port_.port;
  };

 private:
  static constexpr std::array<std::string_view, 3> kOpName = {
      "NOP",
      "Flush",
      "Invalidate",
  };

  struct Csr {
    uint64_t start = 0;
    uint64_t addr = 0;
    uint64_t size = 0;
    uint64_t mode = 0;
  } csr_;

  void HandleMmioReq();
  void HandleCacheOperations();

  const bool verbose_;
  uint8_t *csr_mem_;
  std::shared_ptr<sc_clock> clock_;
  sc_event start_cache_op_;

  TlmSink port_;
};

}  // namespace formosa
}  // namespace lv
