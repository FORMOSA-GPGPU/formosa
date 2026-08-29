// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include <systemc.h>
#include <tlm_core/tlm_1/tlm_req_rsp/tlm_channels/tlm_fifo/tlm_fifo.h>
#include <tlm_core/tlm_2/tlm_generic_payload/tlm_gp.h>

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "cache/mshr_file.h"
#include "cache/packet.h"
#include "fake_packet_lifecycle.h"

namespace {

using simtix::cache::FakePacketLifecycle;
using simtix::cache::MshrFile;
using simtix::cache::Packet;
using simtix::cache::PacketType;
using simtix::cache::Param;
using simtix::cache::VictimReservation;

Param MakeParam(size_t block_size, size_t mshr_entries,
                size_t mshr_subentries) {
  Param param;
  param.block_size_bytes = block_size;
  param.mshr_entries = mshr_entries;
  param.mshr_subentries = mshr_subentries;
  return param;
}

class PacketFactory {
 public:
  Packet *ReadMiss(uint64_t address, size_t data_length = 0) {
    return MakePacket(PacketType::kCoreReq, address, false, data_length);
  }

 private:
  Packet *MakePacket(PacketType type, uint64_t address, bool is_write,
                     size_t data_length = 0) {
    auto payload = std::make_unique<tlm::tlm_generic_payload>();
    payload->set_address(address);
    payload->set_command(is_write ? tlm::TLM_WRITE_COMMAND
                                  : tlm::TLM_READ_COMMAND);
    if (data_length > 0) {
      auto data = std::make_unique<std::vector<uint8_t>>(data_length, 0xA5);
      payload->set_data_ptr(data->data());
      payload->set_data_length(data->size());
      payload->set_streaming_width(data->size());
      payload_data_.push_back(std::move(data));
    }

    auto packet = std::make_unique<Packet>();
    packet->unique_id = next_unique_id_++;
    packet->type = type;
    packet->SetPayload(payload.get());

    Packet *packet_ptr = packet.get();
    payloads_.push_back(std::move(payload));
    packets_.push_back(std::move(packet));
    return packet_ptr;
  }

  size_t next_unique_id_ = 0;
  std::vector<std::unique_ptr<tlm::tlm_generic_payload>> payloads_;
  std::vector<std::unique_ptr<std::vector<uint8_t>>> payload_data_;
  std::vector<std::unique_ptr<Packet>> packets_;
};

class MshrFileTester : public sc_core::sc_module {
 public:
  explicit MshrFileTester(sc_core::sc_module_name name)
      : sc_core::sc_module(name),
        clock_("clock"),
        packet_lifecycle_(kBlockSize),
        dut_("mshr_file", MakeParam(kBlockSize, 2, 2), packet_lifecycle_),
        mshr_mem_req_("mshr_mem_req", 1),
        mshr_refill_notify_("mshr_refill_notify", 8),
        mshr_replay_("mshr_replay", 1) {
    dut_.clock(clock_);
    dut_.mshr_mem_req(mshr_mem_req_);
    dut_.mshr_refill_notify(mshr_refill_notify_);
    dut_.mshr_replay(mshr_replay_);

    SC_THREAD(Run);
  }

  bool failed() const { return failed_; }

 private:
  static constexpr size_t kBlockSize = 16;

  void Run() {
    clock_.write(false);
    wait(sc_core::SC_ZERO_TIME);

    auto first_primary = packets_.ReadMiss(0x00);
    auto first_secondary = packets_.ReadMiss(0x04);
    auto second_primary = packets_.ReadMiss(0x10);
    auto second_secondary = packets_.ReadMiss(0x14);
    auto next_primary = packets_.ReadMiss(0x20);

    // Fill the memory-request FIFO to verify primary-miss backpressure.
    Packet *mem_req_blocker =
        packet_lifecycle_.AllocatePacketWithOwnedPayload();
    mem_req_blocker->type = PacketType::kMshrReadReq;
    mem_req_blocker->GetCacheOwnedPayload()->InitRead(0, kBlockSize);
    PutMemReq(mem_req_blocker);
    FlushDelta();

    Expect(AcceptReadMiss(first_primary) == MshrFile::AcceptStatus::kRejected,
           "primary miss waits while memory request FIFO is full");
    Expect(GetMemReq() == mem_req_blocker,
           "memory request blocker remains at FIFO head");
    FlushDelta();
    ReleaseMemReq(mem_req_blocker);

    // Release the memory-request FIFO and let the first primary allocate.
    Expect(AcceptReadMiss(first_primary) ==
               MshrFile::AcceptStatus::kAcceptedPrimary,
           "first primary miss is accepted once memory request FIFO has room");
    FlushDelta();

    Packet *mem_req = GetMemReq();
    ExpectReadReq(mem_req, 0x00, "first primary memory request");
    FlushDelta();
    ReleaseMemReq(mem_req);

    // Merge a secondary miss into the first pending MSHR entry.
    Expect(AcceptReadMiss(first_secondary) ==
               MshrFile::AcceptStatus::kAcceptedSecondary,
           "secondary miss is accepted by the pending MSHR entry");

    Expect(!HasMemReq(), "secondary miss does not emit memory request");

    // Allocate a second independent MSHR entry.
    Expect(AcceptReadMiss(second_primary) ==
               MshrFile::AcceptStatus::kAcceptedPrimary,
           "second primary miss is accepted while a free MSHR entry exists");
    FlushDelta();

    mem_req = GetMemReq();
    ExpectReadReq(mem_req, 0x10, "second primary memory request");
    FlushDelta();
    ReleaseMemReq(mem_req);

    // Merge a secondary miss into the second pending MSHR entry.
    Expect(AcceptReadMiss(second_secondary) ==
               MshrFile::AcceptStatus::kAcceptedSecondary,
           "second secondary miss is accepted by its pending MSHR entry");

    Expect(!HasMemReq(), "second secondary miss does not emit memory request");

    // Start replay for the first entry; its first sub-entry is emitted.
    PutRefillNotify(0x00);
    AdvanceCycle();

    Expect(RefillNotifyCount() == 0, "first refill notify is consumed");
    Expect(HasReplay(), "first replay packet is produced");

    // While replay output is full, the second refill notify is still accepted.
    PutRefillNotify(0x10);
    AdvanceCycle();

    Expect(RefillNotifyCount() == 0,
           "active replay entry does not block later refill notify");
    Expect(GetReplay() == first_primary,
           "first entry primary miss replays first");
    Expect(first_primary->type == PacketType::kReplay,
           "first primary packet is marked as replay");

    Expect(AcceptReadMiss(next_primary) == MshrFile::AcceptStatus::kRejected,
           "new primary miss waits while all MSHR entries are occupied");

    AdvanceCycle();

    Expect(GetReplay() == first_secondary,
           "first entry secondary miss replays second");
    Expect(first_secondary->type == PacketType::kReplay,
           "first secondary packet is marked as replay");

    // Once the first entry drains, the new line can allocate a primary miss.
    Expect(AcceptReadMiss(next_primary) ==
               MshrFile::AcceptStatus::kAcceptedPrimary,
           "new primary miss allocates after a replay entry frees");
    FlushDelta();
    mem_req = GetMemReq();
    ExpectReadReq(mem_req, 0x20, "new primary memory request");
    FlushDelta();
    ReleaseMemReq(mem_req);

    AdvanceCycle();

    Expect(GetReplay() == second_primary,
           "second entry primary miss replays after first entry drains");
    Expect(second_primary->type == PacketType::kReplay,
           "second primary packet is marked as replay");

    // Drain the remaining sub-entry from the second replaying MSHR entry.
    AdvanceCycle();

    Expect(GetReplay() == second_secondary,
           "second entry secondary miss replays last");
    Expect(second_secondary->type == PacketType::kReplay,
           "second secondary packet is marked as replay");
    Expect(!HasReplay(), "replay FIFO is empty after draining second entry");

    sc_core::sc_stop();
  }

  void AdvanceCycle() {
    clock_.write(true);
    wait(sc_core::SC_ZERO_TIME);
    clock_.write(false);
    wait(sc_core::sc_time(1, sc_core::SC_NS));
  }

  void FlushDelta() { wait(sc_core::SC_ZERO_TIME); }

  void Expect(bool condition, std::string_view message) {
    if (!condition) {
      failed_ = true;
      std::cerr << "MSHR file timing test failed: " << message << '\n';
    }
  }

  void ExpectReadReq(Packet *packet, uint64_t address,
                     std::string_view message) {
    Expect(packet != nullptr, message);
    if (packet == nullptr) {
      return;
    }

    Expect(packet->type == PacketType::kMshrReadReq, message);
    tlm::tlm_generic_payload *gp = packet->GetTlmGp();
    Expect(gp->is_read(), message);
    Expect(gp->get_address() == address, message);
    Expect(gp->get_data_ptr() != nullptr, message);
    Expect(gp->get_data_length() == kBlockSize, message);
    Expect(gp->get_streaming_width() == kBlockSize, message);
  }

  void PutRefillNotify(uint64_t address) {
    const auto it = mshr_ids_.find(address);
    Expect(it != mshr_ids_.end(), "refill has a recorded MSHR identity");
    if (it == mshr_ids_.end()) {
      return;
    }
    (void)dut_.TakeVictimReservation(it->second);
    MshrFile::RefillNotify notify{.address = address, .mshr_id = it->second};
    Expect(mshr_refill_notify_.nb_put(notify), "refill notify is enqueued");
  }

  MshrFile::AcceptStatus AcceptReadMiss(Packet *packet) {
    if (dut_.ProbeReadMiss(*packet) !=
        MshrFile::ProbeStatus::kAcceptablePrimary) {
      return dut_.TryAcceptReadMiss(packet);
    }
    VictimReservation reservation(
        {next_reservation_index_++, next_reservation_index_});
    return dut_.TryAcceptReadMiss(packet, &reservation);
  }

  void PutMemReq(Packet *packet) {
    Expect(mshr_mem_req_.nb_put(packet), "memory request blocker is enqueued");
  }

  Packet *GetMemReq() {
    Packet *packet = nullptr;
    Expect(mshr_mem_req_.nb_get(packet), "memory request FIFO has an item");
    if (packet != nullptr && packet->mshr_id.has_value()) {
      mshr_ids_[packet->GetAddress()] = *packet->mshr_id;
    }
    return packet;
  }

  void ReleaseMemReq(Packet *packet) {
    packet_lifecycle_.ReleasePacket(packet);
  }

  Packet *GetReplay() {
    Packet *packet = nullptr;
    Expect(mshr_replay_.nb_get(packet), "replay FIFO has an item");
    return packet;
  }

  bool HasMemReq() const { return mshr_mem_req_.used() > 0; }
  bool HasReplay() const { return mshr_replay_.used() > 0; }

  int RefillNotifyCount() const { return mshr_refill_notify_.used(); }

  bool failed_ = false;
  PacketFactory packets_;
  sc_core::sc_signal<bool> clock_;
  FakePacketLifecycle packet_lifecycle_;
  MshrFile dut_;
  tlm::tlm_fifo<Packet *> mshr_mem_req_;
  tlm::tlm_fifo<MshrFile::RefillNotify> mshr_refill_notify_;
  tlm::tlm_fifo<Packet *> mshr_replay_;
  size_t next_reservation_index_ = 0;
  std::unordered_map<uint64_t, simtix::cache::MshrId> mshr_ids_;
};

}  // namespace

int sc_main(int, char *[]) {
  MshrFileTester tester("tester");
  sc_core::sc_start();
  return tester.failed() ? 1 : 0;
}
