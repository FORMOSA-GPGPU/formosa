// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include <systemc.h>
#include <tlm_core/tlm_1/tlm_req_rsp/tlm_channels/tlm_fifo/tlm_fifo.h>
#include <tlm_core/tlm_2/tlm_generic_payload/tlm_gp.h>

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string_view>
#include <vector>

#include "cache/packet.h"
#include "cache/write_buffer.h"
#include "fake_packet_lifecycle.h"

namespace {

using simtix::cache::FakePacketLifecycle;
using simtix::cache::MemPayload;
using simtix::cache::Packet;
using simtix::cache::PacketType;
using simtix::cache::Param;
using simtix::cache::WriteBuffer;

Param MakeParam(size_t block_size, size_t write_buffer_entries) {
  Param param;
  param.block_size_bytes = block_size;
  param.write_buffer_entries = write_buffer_entries;
  return param;
}

class WriteBufferModuleTester : public sc_core::sc_module {
 public:
  explicit WriteBufferModuleTester(sc_core::sc_module_name name)
      : sc_core::sc_module(name),
        clock_("clock"),
        packet_lifecycle_(kBlockSize),
        dut_("write_buffer", MakeParam(kBlockSize, 2), packet_lifecycle_),
        write_buffer_in_("write_buffer_in", 8),
        mem_req_out_("mem_req_out", 1),
        mem_resp_in_("mem_resp_in", 8) {
    dut_.clock(clock_);
    dut_.write_buffer_in(write_buffer_in_);
    dut_.mem_req_out(mem_req_out_);
    dut_.mem_resp_in(mem_resp_in_);

    SC_THREAD(Run);
  }

  bool failed() const { return failed_; }

 private:
  static constexpr size_t kBlockSize = 16;

  void Run() {
    clock_.write(false);
    wait(sc_core::SC_ZERO_TIME);

    // Fill the two write-buffer entries.
    Packet *first = MakeWrite(0x100, 0x10);
    Packet *second = MakeWrite(0x200, 0x20, AlternatingByteEnable());
    Packet *third = MakeWrite(0x300, 0x30);
    const std::vector<uint8_t> first_data =
        first->GetCacheOwnedPayload()->buffer;
    const std::vector<uint8_t> first_byte_enable =
        first->GetCacheOwnedPayload()->byte_enable;
    const std::vector<uint8_t> second_data =
        second->GetCacheOwnedPayload()->buffer;
    const std::vector<uint8_t> second_byte_enable =
        second->GetCacheOwnedPayload()->byte_enable;
    const std::vector<uint8_t> third_data =
        third->GetCacheOwnedPayload()->buffer;
    const std::vector<uint8_t> third_byte_enable =
        third->GetCacheOwnedPayload()->byte_enable;

    PutWrite(first);
    PutWrite(second);
    AdvanceCycle();

    // The first write emits a memory request while the second remains queued.
    Expect(HasMemReq(), "first write request is sent to memory");
    Expect(WriteFrontIs(second), "second write remains queued for next cycle");

    AdvanceCycle();

    PutWrite(third);
    AdvanceCycle();

    // The third write waits while all entries are occupied.
    Expect(WriteFrontIs(third),
           "third write waits while all write-buffer entries are occupied");

    Packet *first_req = GetMemReq();
    ExpectWriteReq(first_req, 0x100, first_data, "first memory request",
                   first_byte_enable);
    MemPayload *first_payload = first_req->GetCacheOwnedPayload();
    Expect(!packet_lifecycle_.WasReleased(first_req),
           "first memory request is not released before its response");
    PutMemResp(first_req);
    AdvanceCycle();
    Expect(packet_lifecycle_.WasReleased(first_req),
           "first packet is released after its memory response");
    Expect(packet_lifecycle_.WasPayloadReleased(first_payload),
           "first mem payload is released after its memory response");

    // A memory response frees one entry and lets the third write enter.
    Expect(WriteQueueEmpty(), "third write is consumed after one entry frees");
    Packet *second_req = GetMemReq();
    ExpectWriteReq(second_req, 0x200, second_data,
                   "second memory request uses its mem payload data",
                   second_byte_enable);
    MemPayload *second_payload = second_req->GetCacheOwnedPayload();
    Expect(!packet_lifecycle_.WasReleased(second_req),
           "second memory request is not released before its response");

    PutMemResp(second_req);
    AdvanceCycle();
    Expect(packet_lifecycle_.WasReleased(second_req),
           "second packet is released after its memory response");
    Expect(packet_lifecycle_.WasPayloadReleased(second_payload),
           "second mem payload is released after its memory response");

    Packet *third_req = GetMemReq();
    ExpectWriteReq(third_req, 0x300, third_data,
                   "third memory request uses its mem payload data",
                   third_byte_enable);
    MemPayload *third_payload = third_req->GetCacheOwnedPayload();
    Expect(!packet_lifecycle_.WasReleased(third_req),
           "third memory request is not released before its response");

    PutMemResp(third_req);
    AdvanceCycle();
    Expect(packet_lifecycle_.WasReleased(third_req),
           "third packet is released after its memory response");
    Expect(packet_lifecycle_.WasPayloadReleased(third_payload),
           "third mem payload is released after its memory response");

    // Drain all pending writes.
    Expect(!HasMemReq(), "all memory requests are drained");
    Expect(WriteQueueEmpty(), "write input queue is empty");

    sc_core::sc_stop();
  }

  Packet *MakeWrite(uint64_t address, uint8_t seed,
                    std::vector<uint8_t> byte_enable = {}) {
    Packet *packet = packet_lifecycle_.AllocatePacketWithOwnedPayload();
    packet->type = PacketType::kMemWriteReq;
    MemPayload *payload = packet->GetCacheOwnedPayload();
    payload->InitLineWrite(address, kBlockSize);

    for (size_t i = 0; i < payload->buffer.size(); ++i) {
      payload->buffer[i] = static_cast<uint8_t>(seed + i);
    }

    if (!byte_enable.empty()) {
      Expect(byte_enable.size() == payload->byte_enable.size(),
             "line write byte-enable mask matches the payload size");
      if (byte_enable.size() == payload->byte_enable.size()) {
        for (size_t i = 0; i < byte_enable.size(); ++i) {
          payload->byte_enable[i] = byte_enable[i];
        }
      }
    }

    return packet;
  }

  std::vector<uint8_t> AlternatingByteEnable() const {
    std::vector<uint8_t> byte_enable(kBlockSize, TLM_BYTE_DISABLED);
    for (size_t i = 0; i < byte_enable.size(); i += 2) {
      byte_enable[i] = TLM_BYTE_ENABLED;
    }
    return byte_enable;
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
      std::cerr << "WriteBuffer module timing test failed: " << message << '\n';
    }
  }

  void ExpectWriteReq(Packet *packet, uint64_t address,
                      const std::vector<uint8_t> &expected_data,
                      std::string_view message,
                      const std::vector<uint8_t> &expected_byte_enable = {}) {
    Expect(packet != nullptr, message);
    if (packet == nullptr) {
      return;
    }

    Expect(packet->type == PacketType::kMemWriteReq, message);
    tlm::tlm_generic_payload *gp = packet->GetTlmGp();
    Expect(gp->is_write(), message);
    Expect(gp->get_address() == address, message);
    Expect(gp->get_data_length() == expected_data.size(), message);

    std::vector<uint8_t> actual_data(
        gp->get_data_ptr(), gp->get_data_ptr() + gp->get_data_length());
    Expect(actual_data == expected_data, message);

    Expect(gp->get_byte_enable_length() == expected_byte_enable.size(),
           message);
    if (expected_byte_enable.empty()) {
      Expect(gp->get_byte_enable_ptr() == nullptr, message);
      return;
    }

    Expect(gp->get_byte_enable_ptr() != nullptr, message);
    std::vector<uint8_t> actual_byte_enable(
        gp->get_byte_enable_ptr(),
        gp->get_byte_enable_ptr() + gp->get_byte_enable_length());
    Expect(actual_byte_enable == expected_byte_enable, message);
  }

  void PutWrite(Packet *packet) {
    Expect(write_buffer_in_.nb_put(packet), "write packet is enqueued");
  }

  void PutMemResp(Packet *packet) {
    packet->type = PacketType::kMemWriteResp;
    Expect(mem_resp_in_.nb_put(packet), "memory response is enqueued");
  }

  Packet *GetMemReq() {
    Packet *packet = nullptr;
    Expect(mem_req_out_.nb_get(packet), "memory request FIFO has an item");
    return packet;
  }

  bool HasMemReq() const { return mem_req_out_.used() > 0; }

  bool WriteFrontIs(Packet *expected) const {
    Packet *packet = nullptr;
    return write_buffer_in_.nb_peek(packet) && packet == expected;
  }

  bool WriteQueueEmpty() const { return write_buffer_in_.used() == 0; }

  bool failed_ = false;
  sc_core::sc_signal<bool> clock_;
  FakePacketLifecycle packet_lifecycle_;
  WriteBuffer dut_;
  tlm::tlm_fifo<Packet *> write_buffer_in_;
  tlm::tlm_fifo<Packet *> mem_req_out_;
  tlm::tlm_fifo<Packet *> mem_resp_in_;
};

}  // namespace

int sc_main(int, char *[]) {
  WriteBufferModuleTester tester("tester");
  sc_core::sc_start();
  return tester.failed() ? 1 : 0;
}
