/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "cores/pipelined/packet.h"
#include "liblv/common/ip_extension.h"
#include "liblv/mm/static.h"

namespace simtix::pipelined {

// The extension allow the TLM generic payload to find the corresponding fetch
// entry in the frontend quickly
class FetchEntry;
struct FetchEntryExtension : lv::IpExtension {
  FetchEntry *entry = nullptr;

  tlm_extension_base *clone() const override {
    FetchEntryExtension *ext = new FetchEntryExtension;
    ext->entry = entry;
    return ext;
  }

  void copy_from(const tlm_extension_base &ext) override {
    entry = static_cast<const FetchEntryExtension &>(ext).entry;
  }

  // Don't delete this
  virtual void free() override {}
};

class FetchEntry {
 public:
  explicit FetchEntry(uint32_t fetch_width)
      : ready_(false), bytes_(fetch_width * 4) {
    ext_.entry = this;
    packets_.reserve(fetch_width);
    payload_.set_command(tlm::TLM_READ_COMMAND);
    payload_.set_data_length(bytes_.size());
    payload_.set_data_ptr(bytes_.data());
    payload_.set_byte_enable_ptr(nullptr);
    payload_.set_byte_enable_length(0);
    payload_.set_mm(lv::mm::Static);
    payload_.set_extension(&ext_);
    packets_.reserve(fetch_width);
  }

  void SetAddress(uint64_t addr) {
    payload_.set_address(addr);
    ext_.ip = addr;
  }
  void AddPacket(Packet *packet) { packets_.push_back(packet); }
  void NotifyFill() {
    ready_ = true;
    for (uint32_t i = 0; i < packets_.size(); ++i) {
      uint8_t *bytes = bytes_.data() + i * 4;
      packets_[i]->iword =
          (bytes[3] << 24) | (bytes[2] << 16) | (bytes[1] << 8) | bytes[0];
    }
  }
  void Reset() {
    packets_.clear();
    ready_ = false;
  }

  bool ready() const { return ready_; }
  tlm::tlm_generic_payload *payload() { return &payload_; }
  const std::vector<Packet *> &packets() const { return packets_; }

 private:
  bool ready_;
  std::vector<Packet *> packets_;
  tlm::tlm_generic_payload payload_;
  std::vector<uint8_t> bytes_;
  FetchEntryExtension ext_;
};

}  // namespace simtix::pipelined
