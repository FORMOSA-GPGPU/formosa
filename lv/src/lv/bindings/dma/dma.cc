// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include "dma.h"

#include <addr_map/formosa_addr_map.h>
#include <liblv/binding.h>
#include <liblv/mm/pool.h>
#include <liblv/schema.h>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <systemc>

namespace dma {

namespace {

struct Param {
  int fifo_size = 16;

  LV_SCHEMA(DMA, Param, LV_FIELD(fifo_size, "Internal transaction FIFO size"))
};

}  // namespace

DMA::DMA(const sc_core::sc_module_name &name, int fifo_size)
    : sc_core::sc_module(name),
      port0_master_("port0_master"),
      port1_master_("port1_master"),
      slave_("slave", nullptr),
      trans_fifo_(fifo_size) {
  SC_THREAD(mmio_proc);
  SC_THREAD(read_proc);
  SC_THREAD(write_proc);
}

void DMA::mmio_proc() {
  while (true) {
    auto *trans = slave_.req_port->read();
    const auto cmd = trans->get_command();
    const auto addr = trans->get_address();
    auto *data_ptr = reinterpret_cast<uint64_t *>(trans->get_data_ptr());

    if (data_ptr == nullptr || trans->get_data_length() != sizeof(uint64_t)) {
      trans->set_response_status(tlm::TLM_BURST_ERROR_RESPONSE);
      slave_.resp_port->write(trans);
      continue;
    }

    if (cmd == tlm::TLM_READ_COMMAND) {
      switch (addr) {
        case FSA_DMA_OFF_START:
          *data_ptr = start_;
          break;
        case FSA_DMA_OFF_ADDR0:
          *data_ptr = addr0_;
          break;
        case FSA_DMA_OFF_ADDR1:
          *data_ptr = addr1_;
          break;
        case FSA_DMA_OFF_SIZE:
          *data_ptr = static_cast<uint64_t>(size_);
          break;
        case FSA_DMA_OFF_STATUS:
          *data_ptr = static_cast<uint64_t>(status_);
          break;
        default:
          trans->set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
          slave_.resp_port->write(trans);
          continue;
      }
    } else if (cmd == tlm::TLM_WRITE_COMMAND) {
      if (is_processing_) {
        /* Second START while busy: reject MMIO, keep Busy status. */
        trans->set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
        slave_.resp_port->write(trans);
        continue;
      }
      switch (addr) {
        case FSA_DMA_OFF_START:
          start_ = *data_ptr ? 1 : 0;
          if (start_ && !start_transfer()) {
            trans->set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
            slave_.resp_port->write(trans);
            continue;
          }
          break;
        case FSA_DMA_OFF_ADDR0:
          addr0_ = *data_ptr;
          break;
        case FSA_DMA_OFF_ADDR1:
          addr1_ = *data_ptr;
          break;
        case FSA_DMA_OFF_SIZE:
          size_ = *reinterpret_cast<int64_t *>(data_ptr);
          break;
        case FSA_DMA_OFF_STATUS:
          trans->set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
          slave_.resp_port->write(trans);
          continue;
        default:
          trans->set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
          slave_.resp_port->write(trans);
          continue;
      }
    } else {
      SC_REPORT_ERROR("TLM-2", "Illegal transaction command received by DMA");
    }

    trans->set_response_status(tlm::TLM_OK_RESPONSE);
    slave_.resp_port->write(trans);
  }
}

bool DMA::start_transfer() {
  if (size_ == 0) {
    start_ = 0;
    status_ = kDmaStatusDone;
    return true;
  }
  if (size_ == std::numeric_limits<int64_t>::min()) {
    start_ = 0;
    status_ = kDmaStatusInvalidDescriptor;
    return false;
  }

  if (size_ > 0) {
    read_master_ = &port0_master_;
    write_master_ = &port1_master_;
    read_ptr_ = &addr0_;
    write_ptr_ = &addr1_;
  } else {
    read_master_ = &port1_master_;
    write_master_ = &port0_master_;
    read_ptr_ = &addr1_;
    write_ptr_ = &addr0_;
    size_ = -size_;
  }

  transfer_failed_ = false;
  status_ = kDmaStatusBusy;
  is_processing_ = true;
  start_event_.notify();
  return true;
}

void DMA::finish_transfer(DmaStatus terminal_status) {
  status_ = terminal_status;
  is_processing_ = false;
  start_ = 0;
  size_ = 0;
}

void DMA::drain_pending_transactions() {
  while (trans_fifo_.num_available() > 0) {
    auto *trans = trans_fifo_.read();
    if (trans != nullptr) {
      data_map_.erase(trans);
      trans->release();
    }
  }
}

namespace {

/* Cap beats so neither side crosses a 64 B cache block (L2 contract). */
int64_t next_chunk(uint64_t read_addr, uint64_t write_addr, int64_t remaining) {
  constexpr uint64_t kBlock = 64;
  constexpr int64_t kBeat = 8;
  const int64_t read_room = static_cast<int64_t>(kBlock - (read_addr % kBlock));
  const int64_t write_room =
      static_cast<int64_t>(kBlock - (write_addr % kBlock));
  return std::min({remaining, kBeat, read_room, write_room});
}

}  // namespace

void DMA::read_proc() {
  while (true) {
    wait(start_event_);
    int64_t pkt_left = size_;
    /* write_ptr_ lags while beats are in flight; track destination cursor
     * so chunk sizing never crosses a cache block on either side. */
    uint64_t write_cursor = *write_ptr_;
    while (pkt_left > 0) {
      if (transfer_failed_) break;
      const int64_t chunk = next_chunk(*read_ptr_, write_cursor, pkt_left);
      auto *trans = lv::mm::Pool::Allocate();
      data_map_[trans] = 0;
      trans->acquire();
      trans->set_command(tlm::TLM_READ_COMMAND);
      trans->set_address(*read_ptr_);
      trans->set_data_ptr(reinterpret_cast<unsigned char *>(&data_map_[trans]));
      trans->set_data_length(static_cast<unsigned int>(chunk));
      trans->set_streaming_width(static_cast<unsigned int>(chunk));
      trans->set_byte_enable_ptr(nullptr);
      trans->set_byte_enable_length(0);
      trans->set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
      read_master_->req_port->write(trans);
      trans = read_master_->resp_port->read();
      if (trans->get_response_status() != tlm::TLM_OK_RESPONSE) {
        data_map_.erase(trans);
        trans->release();
        transfer_failed_ = true;
        status_ = kDmaStatusBusError;
        trans_fifo_.write(nullptr);
        break;
      }
      if (transfer_failed_) {
        data_map_.erase(trans);
        trans->release();
        break;
      }
      trans_fifo_.write(trans);
      *read_ptr_ += static_cast<uint64_t>(chunk);
      write_cursor += static_cast<uint64_t>(chunk);
      pkt_left -= chunk;
    }
  }
}

void DMA::write_proc() {
  while (true) {
    auto *trans = trans_fifo_.read();
    if (trans == nullptr) {
      finish_transfer(kDmaStatusBusError);
      continue;
    }
    if (transfer_failed_) {
      data_map_.erase(trans);
      trans->release();
      continue;
    }
    const int64_t chunk = static_cast<int64_t>(trans->get_data_length());
    trans->set_command(tlm::TLM_WRITE_COMMAND);
    trans->set_address(*write_ptr_);
    trans->set_data_length(static_cast<unsigned int>(chunk));
    trans->set_streaming_width(static_cast<unsigned int>(chunk));
    trans->set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
    write_master_->req_port->write(trans);
    auto *resp_ptr = write_master_->resp_port->read();
    const bool success =
        resp_ptr->get_response_status() == tlm::TLM_OK_RESPONSE;
    data_map_.erase(resp_ptr);
    resp_ptr->release();
    if (!success) {
      transfer_failed_ = true;
      drain_pending_transactions();
      finish_transfer(kDmaStatusBusError);
      continue;
    }
    *write_ptr_ += static_cast<uint64_t>(chunk);
    size_ -= chunk;
    if (size_ <= 0) {
      finish_transfer(kDmaStatusDone);
    }
  }
}

void DMA::set_port0_target(Target *t) {
  port0_target_ = t;
  port0_master_.set_target(t);
}

DMA::Target *DMA::port0_target() const { return port0_target_; }

void DMA::set_port1_target(Target *t) {
  port1_target_ = t;
  port1_master_.set_target(t);
}

DMA::Target *DMA::port1_target() const { return port1_target_; }

DMA::Source *DMA::slave_port() const { return &slave_.port; }

LV_BINDING(dma, DMA)
    .constructor(
        [](const char *name, const Param &param) {
          return std::make_shared<DMA>(name, param.fifo_size);
        },
        lv::params("name", "param"), lv::doc("Create a dual-port DMA engine"))
    .property("port0_target", &DMA::port0_target, &DMA::set_port0_target,
              lv::doc("Port 0 memory target"))
    .property("port1_target", &DMA::port1_target, &DMA::set_port1_target,
              lv::doc("Port 1 memory target"))
    .property("slave_port", &DMA::slave_port, lv::doc("DMA CSR MMIO slave"));

}  // namespace dma
