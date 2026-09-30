// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include <liblv/axi/axi_adapters.h>
#include <liblv/common/tlm_source.h>
#include <liblv/mm/pool.h>
#include <systemc.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include "lv/src/lv/bindings/simple/memory.h"

int RunAxiSafetyCase(const char *scenario, bool split, bool read,
                     unsigned beats);
int RunAxiWriteAwAfterWCase();
int RunAxiTargetResetCase(bool read);
int RunAxiInitiatorResetCase();
int RunAxiWriteResponseGapCase(bool reset);
int RunAxiSerializationResetCase(bool old_read, bool new_read,
                                 bool cross_channel);

namespace {

using lv::axi::AxiAdapterConfig;
using lv::axi::AxiBurstType;
using lv::axi::AxiExtension;
using lv::axi::AxiInitiatorAdapter;
using lv::axi::AxiSignals;
using lv::axi::AxiTargetAdapter;
using lv::axi::kDecErrResp;
using lv::axi::kOkayResp;
using lv::axi::kSlvErrResp;
using lv::stats::Integer;

class TestInitiator : public sc_core::sc_module {
 public:
  using Target = lv::TlmSource::Target;

  explicit TestInitiator(const sc_core::sc_module_name &name)
      : sc_core::sc_module(name), source_("source", 16) {}

  void set_target(Target *target) { source_.set_target(target); }

  void Write(uint64_t addr, const std::vector<uint8_t> &data, size_t beat_bytes,
             uint32_t txn_id, uint8_t burst_type = AxiBurstType::kIncrBurst,
             const std::vector<uint8_t> *byte_enable = nullptr) {
    auto *trans = MakeTransaction(tlm::TLM_WRITE_COMMAND, addr, data,
                                  beat_bytes, txn_id, burst_type, byte_enable);
    source_.req_port->write(trans);
    trans = source_.resp_port->read();
    if (trans->get_response_status() != tlm::TLM_OK_RESPONSE) {
      CleanupTransaction(trans);
      throw std::runtime_error("AXI write transaction failed");
    }
    CleanupTransaction(trans);
  }

  std::vector<uint8_t> Read(uint64_t addr, size_t total_bytes,
                            size_t beat_bytes, uint32_t txn_id,
                            uint8_t burst_type = AxiBurstType::kIncrBurst) {
    std::vector<uint8_t> data(total_bytes, 0);
    auto *trans = MakeTransaction(tlm::TLM_READ_COMMAND, addr, data, beat_bytes,
                                  txn_id, burst_type, nullptr);
    source_.req_port->write(trans);
    trans = source_.resp_port->read();
    if (trans->get_response_status() != tlm::TLM_OK_RESPONSE) {
      CleanupTransaction(trans);
      throw std::runtime_error("AXI read transaction failed");
    }

    auto it = payload_data_.find(trans);
    if (it != payload_data_.end()) {
      data = it->second;
    }
    CleanupTransaction(trans);
    return data;
  }

 private:
  lv::TlmSource source_;
  std::unordered_map<tlm::tlm_generic_payload *, std::vector<uint8_t>>
      payload_data_;
  std::unordered_map<tlm::tlm_generic_payload *, std::vector<uint8_t>>
      byte_enable_;

  tlm::tlm_generic_payload *MakeTransaction(
      tlm::tlm_command command, uint64_t addr, const std::vector<uint8_t> &data,
      size_t beat_bytes, uint32_t txn_id, uint8_t burst_type,
      const std::vector<uint8_t> *byte_enable) {
    auto *trans = lv::mm::Pool::Allocate();
    trans->acquire();

    payload_data_[trans] = data;
    if (byte_enable != nullptr) {
      byte_enable_[trans] = *byte_enable;
    }

    auto *ext = new AxiExtension;
    ext->txn_id = txn_id;
    ext->beat_size = static_cast<uint8_t>(lv::axi::BytesToSizeCode(beat_bytes));
    ext->burst_type = burst_type;
    ext->burst_len =
        static_cast<uint32_t>((data.size() + beat_bytes - 1) / beat_bytes) - 1;
    trans->set_extension(ext);

    trans->set_command(command);
    trans->set_address(addr);
    trans->set_data_ptr(payload_data_[trans].data());
    trans->set_data_length(payload_data_[trans].size());
    trans->set_streaming_width(payload_data_[trans].size());
    if (byte_enable != nullptr) {
      trans->set_byte_enable_ptr(byte_enable_[trans].data());
      trans->set_byte_enable_length(byte_enable_[trans].size());
    } else {
      trans->set_byte_enable_ptr(nullptr);
      trans->set_byte_enable_length(0);
    }
    trans->set_dmi_allowed(false);
    trans->set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
    return trans;
  }

  void CleanupTransaction(tlm::tlm_generic_payload *trans) {
    auto *ext = static_cast<AxiExtension *>(nullptr);
    trans->get_extension(ext);
    if (ext != nullptr) {
      trans->clear_extension(ext);
      delete ext;
    }
    payload_data_.erase(trans);
    byte_enable_.erase(trans);
    trans->release();
  }
};

template <bool SplitBursts>
class RoundTripHarness : public sc_core::sc_module {
 public:
  explicit RoundTripHarness(const sc_core::sc_module_name &name)
      : sc_core::sc_module(name),
        clock_("clock", sc_core::sc_time(10, sc_core::SC_NS)),
        reset_n_("reset_n"),
        host_("host"),
        target_adapter_("target_adapter", MakeTargetConfig()),
        initiator_adapter_("initiator_adapter"),
        memory_("memory", MakeMemoryParam()) {
    BindSignals();
    host_.set_target(target_adapter_.target_socket());
    auto *memory_port =
        const_cast<tlm_utils::simple_target_socket<lv::TlmSink> *>(
            memory_.port());
    initiator_adapter_.set_target(memory_port);
    memory_.set_clock(&clock_);
    reset_n_.write(false);
  }

  void Reset() {
    reset_n_.write(false);
    sc_core::wait(clock_.posedge_event());
    sc_core::wait(clock_.posedge_event());
    reset_n_.write(true);
    sc_core::wait(clock_.posedge_event());
  }

  void ResetStats() { memory_.stats()->reset(); }

  void Write(uint64_t addr, const std::vector<uint8_t> &data, size_t beat_bytes,
             uint32_t txn_id,
             const std::vector<uint8_t> *byte_enable = nullptr) {
    host_.Write(addr, data, beat_bytes, txn_id, AxiBurstType::kIncrBurst,
                byte_enable);
  }

  std::vector<uint8_t> Read(uint64_t addr, size_t total_bytes,
                            size_t beat_bytes, uint32_t txn_id) {
    return host_.Read(addr, total_bytes, beat_bytes, txn_id,
                      AxiBurstType::kIncrBurst);
  }

  Integer total_reads() const {
    return memory_.stats()
        ->tabularize()[memory_.stats()->name()]["total_reads"]["val"]
        .value<Integer>()
        .value();
  }
  Integer total_writes() const {
    return memory_.stats()
        ->tabularize()[memory_.stats()->name()]["total_writes"]["val"]
        .value<Integer>()
        .value();
  }

  void Trace(sc_core::sc_trace_file *tf, const std::string &prefix) {
    sc_core::sc_trace(tf, clock_, prefix + ".clock");
    sc_core::sc_trace(tf, reset_n_, prefix + ".reset_n");

    sc_core::sc_trace(tf, ar_valid_, prefix + ".ar_valid");
    sc_core::sc_trace(tf, ar_ready_, prefix + ".ar_ready");
    sc_core::sc_trace(tf, ar_id_, prefix + ".ar_id");
    sc_core::sc_trace(tf, ar_addr_, prefix + ".ar_addr");
    sc_core::sc_trace(tf, ar_len_, prefix + ".ar_len");
    sc_core::sc_trace(tf, ar_size_, prefix + ".ar_size");
    sc_core::sc_trace(tf, ar_burst_, prefix + ".ar_burst");

    sc_core::sc_trace(tf, r_valid_, prefix + ".r_valid");
    sc_core::sc_trace(tf, r_ready_, prefix + ".r_ready");
    sc_core::sc_trace(tf, r_id_, prefix + ".r_id");
    sc_core::sc_trace(tf, r_data_, prefix + ".r_data");
    sc_core::sc_trace(tf, r_resp_, prefix + ".r_resp");
    sc_core::sc_trace(tf, r_last_, prefix + ".r_last");

    sc_core::sc_trace(tf, aw_valid_, prefix + ".aw_valid");
    sc_core::sc_trace(tf, aw_ready_, prefix + ".aw_ready");
    sc_core::sc_trace(tf, aw_id_, prefix + ".aw_id");
    sc_core::sc_trace(tf, aw_addr_, prefix + ".aw_addr");
    sc_core::sc_trace(tf, aw_len_, prefix + ".aw_len");
    sc_core::sc_trace(tf, aw_size_, prefix + ".aw_size");
    sc_core::sc_trace(tf, aw_burst_, prefix + ".aw_burst");

    sc_core::sc_trace(tf, w_valid_, prefix + ".w_valid");
    sc_core::sc_trace(tf, w_ready_, prefix + ".w_ready");
    sc_core::sc_trace(tf, w_data_, prefix + ".w_data");
    sc_core::sc_trace(tf, w_strb_, prefix + ".w_strb");
    sc_core::sc_trace(tf, w_last_, prefix + ".w_last");

    sc_core::sc_trace(tf, b_valid_, prefix + ".b_valid");
    sc_core::sc_trace(tf, b_ready_, prefix + ".b_ready");
    sc_core::sc_trace(tf, initiator_b_id_, prefix + ".initiator_b_id");
    sc_core::sc_trace(tf, b_resp_, prefix + ".b_resp");
  }

 private:
  static AxiAdapterConfig MakeTargetConfig() {
    AxiAdapterConfig config;
    config.allow_burst_split = SplitBursts;
    return config;
  }

  static simple::Memory::Param MakeMemoryParam() {
    simple::Memory::Param param;
    param.size = 4096;
    param.latency = 1;
    param.fifo_size = 4;
    return param;
  }

  void BindSignals() {
    target_adapter_.bind_clock(clock_);
    target_adapter_.bind_reset_n(reset_n_);
    target_adapter_.ar_valid.bind(ar_valid_);
    target_adapter_.ar_ready.bind(ar_ready_);
    target_adapter_.ar_id.bind(ar_id_);
    target_adapter_.ar_addr.bind(ar_addr_);
    target_adapter_.ar_len.bind(ar_len_);
    target_adapter_.ar_size.bind(ar_size_);
    target_adapter_.ar_burst.bind(ar_burst_);
    target_adapter_.r_valid.bind(r_valid_);
    target_adapter_.r_ready.bind(r_ready_);
    target_adapter_.r_id.bind(r_id_);
    target_adapter_.r_data.bind(r_data_);
    target_adapter_.r_resp.bind(r_resp_);
    target_adapter_.r_last.bind(r_last_);
    target_adapter_.aw_valid.bind(aw_valid_);
    target_adapter_.aw_ready.bind(aw_ready_);
    target_adapter_.aw_id.bind(aw_id_);
    target_adapter_.aw_addr.bind(aw_addr_);
    target_adapter_.aw_len.bind(aw_len_);
    target_adapter_.aw_size.bind(aw_size_);
    target_adapter_.aw_burst.bind(aw_burst_);
    target_adapter_.w_valid.bind(w_valid_);
    target_adapter_.w_ready.bind(w_ready_);
    target_adapter_.w_data.bind(w_data_);
    target_adapter_.w_strb.bind(w_strb_);
    target_adapter_.w_last.bind(w_last_);
    target_adapter_.b_valid.bind(b_valid_);
    target_adapter_.b_ready.bind(b_ready_);
    target_adapter_.b_id.bind(initiator_b_id_);
    target_adapter_.b_resp.bind(b_resp_);

    initiator_adapter_.bind_clock(clock_);
    initiator_adapter_.bind_reset_n(reset_n_);
    initiator_adapter_.ar_valid.bind(ar_valid_);
    initiator_adapter_.ar_ready.bind(ar_ready_);
    initiator_adapter_.ar_id.bind(ar_id_);
    initiator_adapter_.ar_addr.bind(ar_addr_);
    initiator_adapter_.ar_len.bind(ar_len_);
    initiator_adapter_.ar_size.bind(ar_size_);
    initiator_adapter_.ar_burst.bind(ar_burst_);
    initiator_adapter_.r_valid.bind(r_valid_);
    initiator_adapter_.r_ready.bind(r_ready_);
    initiator_adapter_.r_id.bind(r_id_);
    initiator_adapter_.r_data.bind(r_data_);
    initiator_adapter_.r_resp.bind(r_resp_);
    initiator_adapter_.r_last.bind(r_last_);
    initiator_adapter_.aw_valid.bind(aw_valid_);
    initiator_adapter_.aw_ready.bind(aw_ready_);
    initiator_adapter_.aw_id.bind(aw_id_);
    initiator_adapter_.aw_addr.bind(aw_addr_);
    initiator_adapter_.aw_len.bind(aw_len_);
    initiator_adapter_.aw_size.bind(aw_size_);
    initiator_adapter_.aw_burst.bind(aw_burst_);
    initiator_adapter_.w_valid.bind(w_valid_);
    initiator_adapter_.w_ready.bind(w_ready_);
    initiator_adapter_.w_data.bind(w_data_);
    initiator_adapter_.w_strb.bind(w_strb_);
    initiator_adapter_.w_last.bind(w_last_);
    initiator_adapter_.b_valid.bind(b_valid_);
    initiator_adapter_.b_ready.bind(b_ready_);
    initiator_adapter_.b_id.bind(initiator_b_id_);
    initiator_adapter_.b_resp.bind(b_resp_);
  }

  sc_core::sc_clock clock_;
  sc_core::sc_signal<bool> reset_n_;
  TestInitiator host_;
  AxiTargetAdapter<32, 64, 8> target_adapter_;
  AxiInitiatorAdapter<32, 64, 8> initiator_adapter_;
  simple::Memory memory_;

  sc_core::sc_signal<bool> ar_valid_{"ar_valid"};
  sc_core::sc_signal<bool> ar_ready_{"ar_ready"};
  sc_core::sc_signal<uint32_t> ar_id_{"ar_id"};
  sc_core::sc_signal<uint32_t> ar_addr_{"ar_addr"};
  sc_core::sc_signal<uint32_t> ar_len_{"ar_len"};
  sc_core::sc_signal<uint32_t> ar_size_{"ar_size"};
  sc_core::sc_signal<uint32_t> ar_burst_{"ar_burst"};

  sc_core::sc_signal<bool> r_valid_{"r_valid"};
  sc_core::sc_signal<bool> r_ready_{"r_ready"};
  sc_core::sc_signal<uint32_t> r_id_{"r_id"};
  sc_core::sc_signal<uint64_t> r_data_{"r_data"};
  sc_core::sc_signal<uint32_t> r_resp_{"r_resp"};
  sc_core::sc_signal<bool> r_last_{"r_last"};

  sc_core::sc_signal<bool> aw_valid_{"aw_valid"};
  sc_core::sc_signal<bool> aw_ready_{"aw_ready"};
  sc_core::sc_signal<uint32_t> aw_id_{"aw_id"};
  sc_core::sc_signal<uint32_t> aw_addr_{"aw_addr"};
  sc_core::sc_signal<uint32_t> aw_len_{"aw_len"};
  sc_core::sc_signal<uint32_t> aw_size_{"aw_size"};
  sc_core::sc_signal<uint32_t> aw_burst_{"aw_burst"};

  sc_core::sc_signal<bool> w_valid_{"w_valid"};
  sc_core::sc_signal<bool> w_ready_{"w_ready"};
  sc_core::sc_signal<uint64_t> w_data_{"w_data"};
  sc_core::sc_signal<uint32_t> w_strb_{"w_strb"};
  sc_core::sc_signal<bool> w_last_{"w_last"};

  sc_core::sc_signal<bool> b_valid_{"b_valid"};
  sc_core::sc_signal<bool> b_ready_{"b_ready"};
  sc_core::sc_signal<uint32_t> initiator_b_id_{"initiator_b_id"};
  sc_core::sc_signal<uint32_t> b_resp_{"b_resp"};
};

class OutOfOrderTarget : public sc_core::sc_module {
 public:
  using Source = const tlm_utils::simple_target_socket<lv::TlmSink>;

  SC_HAS_PROCESS(OutOfOrderTarget);

  explicit OutOfOrderTarget(const sc_core::sc_module_name &name)
      : sc_core::sc_module(name), sink_("sink") {
    SC_THREAD(Run);
  }

  Source *port() const { return &sink_.port; }
  unsigned contract_requests() const { return contract_requests_; }
  unsigned mmio_writes() const { return mmio_writes_; }
  sc_core::sc_time received_at() const { return received_at_; }

 private:
  lv::TlmSink sink_;
  unsigned contract_requests_ = 0;
  unsigned mmio_writes_ = 0;
  sc_core::sc_time received_at_;
  std::array<uint8_t, 16> memory_{};
  std::array<uint8_t, 8> mmio_{};

  // A strict target: RAM accepts bursts and sparse enables; the MMIO register
  // accepts exactly eight bytes with all bytes enabled. Reject before effects.
  void ContractRequests() {
    for (;;) {
      auto *trans = sink_.req_port->read();
      received_at_ = sc_core::sc_time_stamp();
      ++contract_requests_;
      const auto length = trans->get_data_length();
      const auto *ext = trans->get_extension<AxiExtension>();
      const bool mmio = trans->get_address() == 0x600;
      const auto *enables = trans->get_byte_enable_ptr();
      if ((trans->get_address() != 0x500 && !mmio) || !ext ||
          ext->beat_size != 3 || ext->NumBeats() * 8 != length ||
          trans->get_streaming_width() != length ||
          (trans->is_write() &&
           (!enables || trans->get_byte_enable_length() != length)) ||
          (trans->is_read() &&
           (enables || trans->get_byte_enable_length() != 0))) {
        SC_REPORT_FATAL("axi_adapters_test",
                        "incorrect burst payload contract");
      }
      auto status = tlm::TLM_OK_RESPONSE;
      if (length > (mmio ? mmio_.size() : memory_.size())) {
        status = tlm::TLM_BURST_ERROR_RESPONSE;
      } else if (mmio && enables &&
                 !std::all_of(enables, enables + length, [](uint8_t e) {
                   return e == TLM_BYTE_ENABLED;
                 })) {
        status = tlm::TLM_BYTE_ENABLE_ERROR_RESPONSE;
      } else {
        auto *storage = mmio ? mmio_.data() : memory_.data();
        for (unsigned i = 0; i < length; ++i) {
          if (trans->is_read())
            trans->get_data_ptr()[i] = storage[i];
          else if (enables[i] == TLM_BYTE_ENABLED)
            storage[i] = trans->get_data_ptr()[i];
        }
        if (mmio && trans->is_write()) ++mmio_writes_;
      }
      // One latency payment for the complete transaction, independent of LEN.
      wait(50, sc_core::SC_NS);
      Respond(trans, status);
    }
  }

  std::array<tlm::tlm_generic_payload *, 3> CollectBatch(
      tlm::tlm_command command) {
    std::array<tlm::tlm_generic_payload *, 3> batch{};
    for (auto &trans : batch) {
      trans = sink_.req_port->read();
      if (trans->get_command() != command) {
        SC_REPORT_FATAL("axi_adapters_test", "unexpected TLM command");
      }
    }
    return batch;
  }

  void Respond(tlm::tlm_generic_payload *trans,
               tlm::tlm_response_status status) {
    trans->set_response_status(status);
    sink_.resp_port->write(trans);
  }

  void Run() {
    auto reads = CollectBatch(tlm::TLM_READ_COMMAND);
    for (auto *trans : reads) {
      if (trans->get_data_length() != sizeof(uint64_t) ||
          trans->get_data_ptr() == nullptr) {
        SC_REPORT_FATAL("axi_adapters_test", "invalid TLM read payload");
      }
      const uint64_t value = trans->get_address();
      std::memcpy(trans->get_data_ptr(), &value, sizeof(value));
    }
    Respond(reads[1], tlm::TLM_OK_RESPONSE);
    Respond(reads[2], tlm::TLM_OK_RESPONSE);
    wait(200, sc_core::SC_NS);
    Respond(reads[0], tlm::TLM_OK_RESPONSE);

    auto writes = CollectBatch(tlm::TLM_WRITE_COMMAND);
    Respond(writes[1], tlm::TLM_ADDRESS_ERROR_RESPONSE);
    Respond(writes[2], tlm::TLM_GENERIC_ERROR_RESPONSE);
    wait(200, sc_core::SC_NS);
    Respond(writes[0], tlm::TLM_OK_RESPONSE);
    ContractRequests();
  }
};

class OutOfOrderAdapterHarness : public sc_core::sc_module {
 public:
  explicit OutOfOrderAdapterHarness(const sc_core::sc_module_name &name)
      : sc_core::sc_module(name),
        clock_("clock", sc_core::sc_time(10, sc_core::SC_NS)),
        reset_n_("reset_n"),
        adapter_("adapter"),
        target_("target") {
    adapter_.bind_clock(clock_);
    adapter_.bind_reset_n(reset_n_);
    adapter_.bind_pins(signals_.pins());
    adapter_.set_target(
        const_cast<tlm_utils::simple_target_socket<lv::TlmSink> *>(
            target_.port()));
    ResetSignals();
  }

  void Run() {
    reset_n_.write(false);
    wait(clock_.posedge_event());
    wait(clock_.posedge_event());
    reset_n_.write(true);
    wait(clock_.posedge_event());

    signals_.r_ready.write(true);
    IssueRead(0, 0x100);
    IssueRead(0, 0x108);
    IssueRead(1, 0x200);
    CheckReadResponses();
    signals_.r_ready.write(false);

    signals_.b_ready.write(true);
    IssueWrite(0, 0x300, 0x30);
    IssueWrite(0, 0x308, 0x31);
    IssueWrite(1, 0x400, 0x40);
    CheckWriteResponses();
    signals_.b_ready.write(false);
    CheckTransactionContract();
  }

 private:
  sc_core::sc_clock clock_;
  sc_core::sc_signal<bool> reset_n_;
  AxiInitiatorAdapter<32, 64, 4> adapter_;
  OutOfOrderTarget target_;
  lv::axi::AxiSignalBundle<32, 64, 4> signals_;

  void ResetSignals() {
    signals_.ar_valid.write(false);
    signals_.ar_id.write(0);
    signals_.ar_addr.write(0);
    signals_.ar_len.write(0);
    signals_.ar_size.write(lv::axi::BytesToSizeCode(sizeof(uint64_t)));
    signals_.ar_burst.write(AxiBurstType::kIncrBurst);
    signals_.r_ready.write(false);
    signals_.aw_valid.write(false);
    signals_.aw_id.write(0);
    signals_.aw_addr.write(0);
    signals_.aw_len.write(0);
    signals_.aw_size.write(lv::axi::BytesToSizeCode(sizeof(uint64_t)));
    signals_.aw_burst.write(AxiBurstType::kIncrBurst);
    signals_.w_valid.write(false);
    signals_.w_data.write(0);
    signals_.w_strb.write(0xff);
    signals_.w_last.write(false);
    signals_.b_ready.write(false);
  }

  void IssueRead(uint32_t id, uint32_t address, unsigned beats = 1) {
    signals_.ar_len.write(beats - 1);
    signals_.ar_id.write(id);
    signals_.ar_addr.write(address);
    signals_.ar_valid.write(true);
    do {
      wait(clock_.posedge_event());
    } while (!signals_.ar_ready.read());
    signals_.ar_valid.write(false);
  }

  void IssueWrite(uint32_t id, uint32_t address, uint64_t data,
                  unsigned beats = 1, uint32_t strobe = 0xff) {
    signals_.aw_id.write(id);
    signals_.aw_addr.write(address);
    signals_.aw_len.write(beats - 1);
    signals_.aw_valid.write(true);
    do {
      wait(clock_.posedge_event());
    } while (!signals_.aw_ready.read());
    signals_.aw_valid.write(false);
    for (unsigned beat = 0; beat < beats; ++beat) {
      signals_.w_data.write(data);
      signals_.w_strb.write(strobe);
      signals_.w_last.write(beat + 1 == beats);
      signals_.w_valid.write(true);
      do {
        wait(clock_.posedge_event());
      } while (!signals_.w_ready.read());
    }
    signals_.w_valid.write(false);
    signals_.w_last.write(false);
  }

  void CheckContractResponse(bool read, unsigned beats, uint32_t response,
                             uint64_t data = 0) {
    const auto requests = target_.contract_requests();
    for (unsigned beat = 0; beat < (read ? beats : 1); ++beat) {
      do {
        wait(clock_.posedge_event());
      } while (!(read ? signals_.r_valid.read() : signals_.b_valid.read()));
      if (beat == 0) {
        const auto elapsed = sc_core::sc_time_stamp() - target_.received_at();
        if (target_.contract_requests() != requests + 1 ||
            elapsed < sc_core::sc_time(50, sc_core::SC_NS) ||
            elapsed > sc_core::sc_time(70, sc_core::SC_NS)) {
          SC_REPORT_FATAL("axi_adapters_test",
                          "burst transaction count or latency mismatch");
        }
      }
      if (read) {
        if (signals_.r_id.read() != 5 || signals_.r_resp.read() != response ||
            signals_.r_last.read() != (beat + 1 == beats) ||
            signals_.r_data.read() != data) {
          SC_REPORT_FATAL("axi_adapters_test", "burst read contract mismatch");
        }
      } else if (signals_.b_id.read() != 5 ||
                 signals_.b_resp.read() != response) {
        SC_REPORT_FATAL("axi_adapters_test", "burst write contract mismatch");
      }
    }
  }

  void CheckTransactionContract() {
    signals_.b_ready.write(true);
    IssueWrite(5, 0x500, 0x1122334455667788ULL, 2, 0x55);
    CheckContractResponse(false, 2, kOkayResp);
    signals_.r_ready.write(true);
    IssueRead(5, 0x500, 2);
    CheckContractResponse(true, 2, kOkayResp, 0x0022004400660088ULL);
    // Both rejected writes must leave the register untouched.
    IssueWrite(5, 0x600, 0xffffffffffffffffULL, 2);
    CheckContractResponse(false, 2, kSlvErrResp);
    IssueWrite(5, 0x600, 0xffffffffffffffffULL, 1, 0x55);
    CheckContractResponse(false, 1, kSlvErrResp);
    IssueRead(5, 0x600);
    CheckContractResponse(true, 1, kOkayResp, 0);
    if (target_.mmio_writes() != 0)
      SC_REPORT_FATAL("axi_adapters_test",
                      "rejected MMIO write had side effects");
    IssueWrite(5, 0x600, 0x1122334455667788ULL);
    CheckContractResponse(false, 1, kOkayResp);
    IssueRead(5, 0x600);
    CheckContractResponse(true, 1, kOkayResp, 0x1122334455667788ULL);
    IssueRead(5, 0x600, 2);
    CheckContractResponse(true, 2, kSlvErrResp);
    if (target_.mmio_writes() != 1 || target_.contract_requests() != 8)
      SC_REPORT_FATAL("axi_adapters_test",
                      "incorrect MMIO transaction effects");
  }

  void CheckReadResponses() {
    std::vector<uint64_t> id0_values;
    std::vector<uint64_t> id1_values;
    const auto start = sc_core::sc_time_stamp();
    for (size_t response = 0; response < 3; ++response) {
      do {
        wait(clock_.posedge_event());
      } while (!signals_.r_valid.read());
      if (!signals_.r_last.read() || signals_.r_resp.read() != kOkayResp) {
        SC_REPORT_FATAL("axi_adapters_test", "invalid AXI read response");
      }
      if (response == 0 && (signals_.r_id.read() != 1 ||
                            sc_core::sc_time_stamp() - start >=
                                sc_core::sc_time(150, sc_core::SC_NS))) {
        SC_REPORT_FATAL("axi_adapters_test", "independent ID was blocked");
      }
      auto &values = signals_.r_id.read() == 0 ? id0_values : id1_values;
      values.push_back(signals_.r_data.read());
    }
    if (id0_values != std::vector<uint64_t>{0x100, 0x108} ||
        id1_values != std::vector<uint64_t>{0x200}) {
      SC_REPORT_FATAL("axi_adapters_test",
                      "AXI read responses were not ordered per ID");
    }
  }

  void CheckWriteResponses() {
    std::vector<uint32_t> id0_responses;
    std::vector<uint32_t> id1_responses;
    const auto start = sc_core::sc_time_stamp();
    for (size_t response = 0; response < 3; ++response) {
      do {
        wait(clock_.posedge_event());
      } while (!signals_.b_valid.read());
      if (response == 0 && (signals_.b_id.read() != 1 ||
                            sc_core::sc_time_stamp() - start >=
                                sc_core::sc_time(150, sc_core::SC_NS))) {
        SC_REPORT_FATAL("axi_adapters_test", "independent ID was blocked");
      }
      auto &responses =
          signals_.b_id.read() == 0 ? id0_responses : id1_responses;
      responses.push_back(signals_.b_resp.read());
    }
    if (id0_responses != std::vector<uint32_t>{kOkayResp, kDecErrResp} ||
        id1_responses != std::vector<uint32_t>{kSlvErrResp}) {
      SC_REPORT_FATAL("axi_adapters_test",
                      "AXI write responses were not ordered per ID");
    }
  }
};

class AxiAdapterTest : public sc_core::sc_module {
 public:
  explicit AxiAdapterTest(const sc_core::sc_module_name &name)
      : sc_core::sc_module(name),
        out_of_order_("out_of_order"),
        full_burst_("full_burst"),
        split_burst_("split_burst") {
    SC_THREAD(Run);
    SC_THREAD(Watchdog);
  }

  [[nodiscard]] bool passed() const { return passed_; }

  void Trace(sc_core::sc_trace_file *tf) {
    full_burst_.Trace(tf, "full_burst");
    split_burst_.Trace(tf, "split_burst");
  }

 private:
  OutOfOrderAdapterHarness out_of_order_;
  RoundTripHarness<false> full_burst_;
  RoundTripHarness<true> split_burst_;
  bool passed_ = true;

  void ExpectEqual(const std::vector<uint8_t> &lhs,
                   const std::vector<uint8_t> &rhs, const std::string &msg) {
    if (lhs != rhs) {
      std::cerr << "[axi-test] expected:";
      for (auto byte : rhs) {
        std::cerr << " " << std::hex << static_cast<unsigned>(byte);
      }
      std::cerr << std::dec << std::endl;
      std::cerr << "[axi-test] actual:";
      for (auto byte : lhs) {
        std::cerr << " " << std::hex << static_cast<unsigned>(byte);
      }
      std::cerr << std::dec << std::endl;
      passed_ = false;
      SC_REPORT_ERROR("axi_adapters_test", msg.c_str());
      sc_core::sc_stop();
    }
  }

  void ExpectMetric(Integer actual, Integer expected, const std::string &msg) {
    if (actual != expected) {
      passed_ = false;
      SC_REPORT_ERROR("axi_adapters_test", msg.c_str());
      sc_core::sc_stop();
    }
  }

  void Run() {
    full_burst_.Reset();
    split_burst_.Reset();

    std::cerr << "[axi-test] per-id-response-order" << std::endl;
    TestPerIdResponseOrder();
    std::cerr << "[axi-test] out-of-order-tlm-completion" << std::endl;
    out_of_order_.Run();
    std::cerr << "[axi-test] single-beat" << std::endl;
    TestSingleBeat();
    std::cerr << "[axi-test] burst-passthrough" << std::endl;
    TestBurstPassthrough();
    std::cerr << "[axi-test] burst-split" << std::endl;
    TestBurstSplit();
    std::cerr << "[axi-test] partial-write" << std::endl;
    TestPartialWrite();
    std::cerr << "[axi-test] done" << std::endl;

    sc_core::sc_stop();
  }

  void Watchdog() {
    wait(sc_core::sc_time(10, sc_core::SC_US));
    passed_ = false;
    SC_REPORT_ERROR("axi_adapters_test", "watchdog timeout");
    sc_core::sc_stop();
  }

  void TestSingleBeat() {
    full_burst_.ResetStats();
    const std::vector<uint8_t> data{0x10, 0x11, 0x12, 0x13,
                                    0x14, 0x15, 0x16, 0x17};
    full_burst_.Write(0x40, data, 8, 1);
    auto readback = full_burst_.Read(0x40, data.size(), 8, 2);
    ExpectEqual(readback, data, "single-beat roundtrip mismatch");
    ExpectMetric(full_burst_.total_writes(), 1,
                 "single-beat write count mismatch");
    ExpectMetric(full_burst_.total_reads(), 1,
                 "single-beat read count mismatch");
  }

  void TestPerIdResponseOrder() {
    lv::axi::detail::AxiResponseOrder order;
    tlm::tlm_generic_payload first_same_id;
    tlm::tlm_generic_payload second_same_id;
    tlm::tlm_generic_payload other_id;

    order.Register(0, &first_same_id);
    order.Register(0, &second_same_id);
    order.Register(1, &other_id);

    order.Complete(&second_same_id);
    ExpectPointer(order.TakeReady(), nullptr,
                  "later response with same ID completed early");
    order.Complete(&other_id);
    ExpectPointer(order.TakeReady(), &other_id,
                  "independent AXI ID should complete out of order");
    order.Complete(&first_same_id);
    ExpectPointer(order.TakeReady(), &first_same_id,
                  "first response with same ID was not released first");
    ExpectPointer(order.TakeReady(), &second_same_id,
                  "second response with same ID was not released second");
    ExpectPointer(order.TakeReady(), nullptr,
                  "response ordering queue did not drain");
  }

  void ExpectPointer(tlm::tlm_generic_payload *actual,
                     tlm::tlm_generic_payload *expected,
                     const std::string &msg) {
    if (actual != expected) {
      passed_ = false;
      SC_REPORT_ERROR("axi_adapters_test", msg.c_str());
      sc_core::sc_stop();
    }
  }

  void TestBurstPassthrough() {
    full_burst_.ResetStats();
    const std::vector<uint8_t> data{
        0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27,
        0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37,
    };
    std::cerr << "[axi-test] burst-passthrough write" << std::endl;
    full_burst_.Write(0x80, data, 8, 3);
    std::cerr << "[axi-test] burst-passthrough read" << std::endl;
    auto readback = full_burst_.Read(0x80, data.size(), 8, 4);
    std::cerr << "[axi-test] burst-passthrough check" << std::endl;
    ExpectEqual(readback, data, "burst passthrough mismatch");
    ExpectMetric(full_burst_.total_writes(), 1,
                 "burst passthrough should stay one write");
    ExpectMetric(full_burst_.total_reads(), 1,
                 "burst passthrough should stay one read");
  }

  void TestBurstSplit() {
    split_burst_.ResetStats();
    const std::vector<uint8_t> data{
        0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47,
        0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57,
    };
    split_burst_.Write(0xc0, data, 8, 5);
    auto readback = split_burst_.Read(0xc0, data.size(), 8, 6);
    ExpectEqual(readback, data, "split burst roundtrip mismatch");
    ExpectMetric(split_burst_.total_writes(), 2,
                 "split burst should fan out to two writes");
    ExpectMetric(split_burst_.total_reads(), 2,
                 "split burst should fan out to two reads");
  }

  void TestPartialWrite() {
    full_burst_.ResetStats();
    const std::vector<uint8_t> base{0xa0, 0xa1, 0xa2, 0xa3,
                                    0xa4, 0xa5, 0xa6, 0xa7};
    const std::vector<uint8_t> patch{0xb0, 0xb1, 0xb2, 0xb3,
                                     0xb4, 0xb5, 0xb6, 0xb7};
    const std::vector<uint8_t> byte_enable{0x00, 0x00, 0xff, 0xff,
                                           0x00, 0x00, 0xff, 0xff};
    const std::vector<uint8_t> expected{0xa0, 0xa1, 0xb2, 0xb3,
                                        0xa4, 0xa5, 0xb6, 0xb7};
    full_burst_.Write(0x100, base, 8, 7);
    full_burst_.Write(0x100, patch, 8, 8, &byte_enable);
    auto readback = full_burst_.Read(0x100, expected.size(), 8, 9);
    ExpectEqual(readback, expected, "partial write mismatch");
  }
};

struct VcdTraceCloser {
  void operator()(sc_core::sc_trace_file *tf) const {
    if (tf != nullptr) {
      sc_core::sc_close_vcd_trace_file(tf);
    }
  }
};

std::string StripVcdSuffix(std::string path) {
  constexpr const char *kVcdSuffix = ".vcd";
  const auto suffix_len = std::strlen(kVcdSuffix);
  if (path.size() >= suffix_len &&
      path.compare(path.size() - suffix_len, suffix_len, kVcdSuffix) == 0) {
    path.resize(path.size() - suffix_len);
  }
  return path;
}

std::string TracePathFromArgs(int argc, char **argv) {
  constexpr const char *kTraceArg = "--trace";
  constexpr const char *kTraceEqArg = "--trace=";
  constexpr const char *kDefaultTracePath = "axi_adapters_test";

  for (int i = 1; i < argc; ++i) {
    const std::string arg(argv[i]);
    if (arg == kTraceArg) {
      if (i + 1 < argc && argv[i + 1][0] != '-') {
        return StripVcdSuffix(argv[i + 1]);
      }
      return kDefaultTracePath;
    }
    if (arg.rfind(kTraceEqArg, 0) == 0) {
      auto trace_path = arg.substr(std::strlen(kTraceEqArg));
      return StripVcdSuffix(trace_path.empty() ? kDefaultTracePath
                                               : trace_path);
    }
  }
  return "";
}

}  // namespace

int sc_main(int argc, char **argv) {
  if (argc == 5 && std::string(argv[1]) == "--axi-reset-serialization") {
    return RunAxiSerializationResetCase(
        std::string(argv[2]) == "read", std::string(argv[3]) == "read",
        std::string(argv[4]) == "cross-channel");
  }
  if (argc == 3 && std::string(argv[1]) == "--axi-write-response-gap") {
    return RunAxiWriteResponseGapCase(std::string(argv[2]) == "reset");
  }
  if (argc == 6 && std::string(argv[1]) == "--axi-safety") {
    return RunAxiSafetyCase(argv[2], std::stoi(argv[3]) != 0,
                            std::stoi(argv[4]) != 0, std::stoul(argv[5]));
  }
  if (argc == 2 && std::string(argv[1]) == "--axi-write-aw-after-w") {
    return RunAxiWriteAwAfterWCase();
  }
  if (argc == 3 && std::string(argv[1]) == "--axi-target-reset") {
    return RunAxiTargetResetCase(std::string(argv[2]) == "read");
  }
  if (argc == 2 && std::string(argv[1]) == "--axi-initiator-reset") {
    return RunAxiInitiatorResetCase();
  }
  AxiAdapterTest test("axi_adapter_test");
  std::unique_ptr<sc_core::sc_trace_file, VcdTraceCloser> trace_file;
  const auto trace_path = TracePathFromArgs(argc, argv);
  if (!trace_path.empty()) {
    trace_file.reset(sc_core::sc_create_vcd_trace_file(trace_path.c_str()));
    test.Trace(trace_file.get());
    std::cerr << "[axi-test] writing VCD trace to " << trace_path << ".vcd"
              << std::endl;
  }
  sc_core::sc_start();
  return test.passed() ? 0 : 1;
}
