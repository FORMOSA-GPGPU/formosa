/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <liblv/common/ip_extension.h>
#include <liblv/mm/static.h>
#include <tlm_core/tlm_2/tlm_generic_payload/tlm_gp.h>

#include "cores/scalar/packet.h"

namespace simtix::scalar {

class FetchEntry;

struct FetchEntryExtension : lv::IpExtension {
  FetchEntry *entry = nullptr;

  tlm_extension_base *clone() const override {
    auto *ext = new FetchEntryExtension;
    ext->entry = entry;
    return ext;
  }

  void copy_from(const tlm_extension_base &ext) override {
    entry = static_cast<const FetchEntryExtension &>(ext).entry;
  }

  void free() override {}
};

class FetchEntry {
 public:
  FetchEntry() {
    ext_.entry = this;
    payload_.set_command(tlm::TLM_READ_COMMAND);
    payload_.set_data_length(sizeof(iword_));
    payload_.set_data_ptr(reinterpret_cast<unsigned char *>(&iword_));
    payload_.set_byte_enable_ptr(nullptr);
    payload_.set_byte_enable_length(0);
    payload_.set_mm(lv::mm::Static);
    payload_.set_extension(&ext_);
  }

  void Issue(Packet *packet, uint64_t pc, uint64_t epoch) {
    packet_ = packet;
    epoch_ = epoch;
    iword_ = 0;
    ready_ = false;
    inflight_ = true;
    discard_ = false;
    payload_.set_address(pc);
    payload_.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
    ext_.ip = pc;
  }

  void NotifyFill() {
    ready_ = true;
    inflight_ = false;
    if (packet_) {
      packet_->iword = iword_;
    }
  }

  Packet *ReleasePacket() {
    Packet *packet = packet_;
    packet_ = nullptr;
    return packet;
  }

  void MarkDiscarded() { discard_ = true; }

  void Reset() {
    packet_ = nullptr;
    epoch_ = 0;
    iword_ = 0;
    ready_ = false;
    inflight_ = false;
    discard_ = false;
    payload_.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
  }

  bool ready() const { return ready_; }
  bool inflight() const { return inflight_; }
  bool discard() const { return discard_; }
  uint64_t epoch() const { return epoch_; }
  tlm::tlm_generic_payload *payload() { return &payload_; }

 private:
  Packet *packet_ = nullptr;
  uint64_t epoch_ = 0;
  uint32_t iword_ = 0;
  bool ready_ = false;
  bool inflight_ = false;
  bool discard_ = false;
  tlm::tlm_generic_payload payload_;
  FetchEntryExtension ext_;
};

}  // namespace simtix::scalar
