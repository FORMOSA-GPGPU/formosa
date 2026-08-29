/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <liblv/common/tlm_sink.h>
#include <liblv/trace.h>
#include <tlm_utils/simple_target_socket.h>

#include <array>
#include <queue>
#include <string_view>

namespace formosa {

class SMTransactor : public sc_module {
 public:
  enum {
    /* Push */
    ST_ENQ_VALID,
    ST_ENQ_KERNEL_PC,
    ST_ENQ_INFO_PTR,
    ST_ENQ_GROUP_SIZE,
    /* Pop */
    ST_DEQ_VALID,
    ST_DEQ_STATUS,
    ST_DEQ_KERNEL_PC,
    ST_DEQ_INFO_PTR,
    ST_DEQ_MCAUSE,
    ST_DEQ_MEPC,
    ST_DEQ_MTVAL,
    ST_CSR_END
  };

  const std::array<std::string_view, ST_CSR_END> ST_CSR_NAME = {
      "ENQ_VALID",  "ENQ_KERNEL_PC", "ENQ_INFO_PTR",  "ENQ_GROUP_SIZE",
      "DEQ_VALID",  "DEQ_STATUS",    "DEQ_KERNEL_PC", "DEQ_INFO_PTR",
      "DEQ_MCAUSE", "DEQ_MEPC",      "DEQ_MTVAL",
  };

  struct KernelPacket {
    int delay;
    uint64_t kernel_pc;
    uint64_t info_ptr;
  };
  using Source = const tlm_utils::simple_target_socket<lv::TlmSink>;
  SMTransactor(const sc_core::sc_module_name &name, int limit, bool delay,
               bool trace);

  sc_in<bool> clk;

  void set_clock(sc_clock *clk);
  sc_clock *clock() const;

  Source *slave_port() const;

 protected:
  std::queue<KernelPacket> packet_queue_;

 private:
  lv::TlmSink slave_;
  sc_clock *clock_;

  /* process */
  void csr_proc();
  void kernel_proc();

  /* regs */
  std::array<uint64_t, ST_CSR_END> csr_mem_;
  uint64_t packet_limit_;
  bool delay_;

  lv::trace::Track pf_packet_;
};
}  // namespace formosa
