// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include <systemc.h>
#include <tlm_core/tlm_1/tlm_req_rsp/tlm_channels/tlm_fifo/tlm_fifo.h>

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <string_view>

#include "cache/victim_buffer.h"
#include "fake_packet_lifecycle.h"

namespace {

using simtix::cache::FakePacketLifecycle;
using simtix::cache::Packet;
using simtix::cache::PacketType;
using simtix::cache::Param;
using simtix::cache::VictimBuffer;

class VictimBufferModuleTest : public sc_core::sc_module {
 public:
  explicit VictimBufferModuleTest(sc_core::sc_module_name name)
      : sc_core::sc_module(name),
        clock_("clock"),
        packet_lifecycle_(16),
        dut_("dut", MakeParam(), packet_lifecycle_),
        mem_req_("mem_req", 1),
        mem_resp_("mem_resp", 1) {
    dut_.clock(clock_);
    dut_.mem_req_out(mem_req_);
    dut_.mem_resp_in(mem_resp_);
    SC_THREAD(Run);
  }

  bool failed() const { return failed_; }

 private:
  static Param MakeParam() {
    Param param;
    param.block_size_bytes = 16;
    param.victim_buffer_entries = 2;
    return param;
  }

  Packet *MakeVictim(uint64_t address, uint8_t value) {
    Packet *packet = packet_lifecycle_.AllocatePacketWithOwnedPayload();
    packet->type = PacketType::kVictimWriteReq;
    packet->GetCacheOwnedPayload()->InitLineWrite(address, 16);
    std::fill(packet->GetTlmGp()->get_data_ptr(),
              packet->GetTlmGp()->get_data_ptr() + 16, value);
    return packet;
  }

  void Run() {
    clock_.write(false);
    wait(sc_core::SC_ZERO_TIME);

    auto first = dut_.TryReserve();
    auto second = dut_.TryReserve();
    Expect(first.has_value() && second.has_value(),
           "all configured victim entries can be reserved");
    Expect(!dut_.TryReserve().has_value(),
           "reservation backpressures when every entry is reserved");

    dut_.Cancel(std::move(*second));
    second = dut_.TryReserve();
    Expect(second.has_value(), "cancelled reservation can be reused");

    Packet *packet = MakeVictim(0x40, 0xA5);
    dut_.Commit(std::move(*first), packet);
    AdvanceCycle();

    Packet *request = nullptr;
    Expect(mem_req_.nb_get(request), "committed victim emits a memory write");
    Expect(request == packet, "victim buffer preserves packet ownership");
    Expect(request->GetAddress() == 0x40,
           "victim write preserves the evicted line address");
    Expect(request->GetTlmGp()->get_data_length() == 16,
           "victim write owns a complete cache line");
    Expect(request->GetTlmGp()->get_data_ptr()[0] == 0xA5,
           "victim write preserves full-line data");

    request->type = PacketType::kVictimWriteResp;
    Expect(mem_resp_.nb_put(request), "victim completion is queued");
    AdvanceCycle();
    Expect(dut_.InflightEntryCount() == 0,
           "write completion releases the inflight victim entry");

    dut_.Cancel(std::move(*second));
    Expect(!dut_.HasPendingWork(),
           "all reservations and committed victims are released");
    sc_core::sc_stop();
  }

  void AdvanceCycle() {
    clock_.write(true);
    wait(sc_core::SC_ZERO_TIME);
    clock_.write(false);
    wait(sc_core::sc_time(1, sc_core::SC_NS));
  }

  void Expect(bool condition, std::string_view message) {
    if (!condition) {
      failed_ = true;
      std::cerr << "Victim buffer module test failed: " << message << '\n';
    }
  }

  bool failed_ = false;
  sc_core::sc_signal<bool> clock_;
  FakePacketLifecycle packet_lifecycle_;
  VictimBuffer dut_;
  tlm::tlm_fifo<Packet *> mem_req_;
  tlm::tlm_fifo<Packet *> mem_resp_;
};

}  // namespace

int sc_main(int, char *[]) {
  VictimBufferModuleTest test("test");
  sc_core::sc_start();
  return test.failed() ? 1 : 0;
}
