/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <liblv/common/tlm_sink.h>
#include <liblv/common/tlm_source.h>
#include <tlm.h>
#include <tlm_utils/simple_initiator_socket.h>
#include <tlm_utils/simple_target_socket.h>

#include <systemc>
#include <unordered_map>

#include "hal/dma.h"

namespace dma {

/**
 * Dual-port DMA engine.
 *
 * CSR layout: START / ADDR0 / ADDR1 / signed SIZE / STATUS.
 * SIZE > 0 copies port0 → port1; SIZE < 0 copies port1 → port0.
 * Host DMA: port0 = Host Agent, port1 = fab_sys.
 * Device DMA: both ports on fab_sys.
 */
class DMA : public sc_core::sc_module {
 public:
  using Target =
      tlm_utils::simple_initiator_socket<DMA>::base_target_socket_type;
  using Source = const tlm_utils::simple_target_socket<lv::TlmSink>;

  DMA(const sc_core::sc_module_name &name, int fifo_size);

  void set_port0_target(Target *t);
  Target *port0_target() const;
  void set_port1_target(Target *t);
  Target *port1_target() const;

  Source *slave_port() const;

 private:
  void mmio_proc();
  bool start_transfer();
  void finish_transfer(DmaStatus terminal_status);
  void drain_pending_transactions();
  void read_proc();
  void write_proc();

  lv::TlmSource port0_master_;
  lv::TlmSource port1_master_;
  lv::TlmSink slave_;

  uint64_t start_ = 0;
  uint64_t addr0_ = 0;
  uint64_t addr1_ = 0;
  int64_t size_ = 0;
  DmaStatus status_ = kDmaStatusIdle;

  sc_core::sc_event start_event_;
  bool is_processing_ = false;
  bool transfer_failed_ = false;
  sc_core::sc_fifo<tlm::tlm_generic_payload *> trans_fifo_;
  std::unordered_map<tlm::tlm_generic_payload *, uint64_t> data_map_;

  lv::TlmSource *read_master_ = nullptr;
  uint64_t *read_ptr_ = nullptr;
  lv::TlmSource *write_master_ = nullptr;
  uint64_t *write_ptr_ = nullptr;

  Target *port0_target_ = nullptr;
  Target *port1_target_ = nullptr;
};

}  // namespace dma
