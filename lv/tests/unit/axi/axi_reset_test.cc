// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include <liblv/axi/axi_adapters.h>
#include <liblv/common/tlm_sink.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>

namespace {
using lv::axi::AxiAdapterConfig;
using lv::axi::AxiInitiatorAdapter;
using lv::axi::AxiSignalBundle;
using lv::axi::kIncrBurst;
using lv::axi::kOkayResp;

void Require(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}

class WriteResponseGapHarness : public sc_core::sc_module {
 public:
  WriteResponseGapHarness(sc_core::sc_module_name name, bool reset)
      : sc_module(name),
        reset_(reset),
        clock_("clock", sc_core::sc_time(10, sc_core::SC_NS)),
        adapter_("adapter"),
        sink_("sink") {
    adapter_.bind_clock(clock_);
    adapter_.bind_reset_n(reset_n_);
    adapter_.bind_pins(pins_.pins());
    adapter_.set_target(&sink_.port);
    SC_THREAD(Run);
    SC_THREAD(Watchdog);
    SC_METHOD(Monitor);
    sensitive << pins_.b_valid << pins_.b_id;
  }

  bool passed() const { return passed_; }

 private:
  bool reset_;
  bool discard_old_ = false;
  bool passed_ = false;
  sc_core::sc_clock clock_;
  sc_core::sc_signal<bool> reset_n_;
  AxiSignalBundle<32, 64, 8> pins_;
  AxiInitiatorAdapter<32, 64, 8> adapter_;
  lv::TlmSink sink_;

  void Monitor() {
    Require(!(discard_old_ && pins_.b_valid.read() && pins_.b_id.read() == 1),
            "stale BVALID escaped reset during write-response gap");
  }

  void Watchdog() {
    wait(10, sc_core::SC_US);
    throw std::runtime_error("write-response gap test timed out");
  }

  void Reset() {
    wait(clock_.negedge_event());
    reset_n_.write(false);
    wait(clock_.negedge_event());
    wait(clock_.negedge_event());
    reset_n_.write(true);
    wait(clock_.posedge_event());
  }

  tlm::tlm_generic_payload *SendWrite(uint32_t id) {
    pins_.aw_id.write(id);
    pins_.aw_addr.write(0x100 + id * 8);
    pins_.aw_len.write(0);
    pins_.aw_size.write(3);
    pins_.aw_burst.write(kIncrBurst);
    pins_.aw_valid.write(true);
    do {
      wait(clock_.posedge_event());
    } while (!pins_.aw_ready.read());
    pins_.aw_valid.write(false);
    pins_.w_data.write(0x1122334455667788ULL);
    pins_.w_strb.write(0xff);
    pins_.w_last.write(true);
    pins_.w_valid.write(true);
    do {
      wait(clock_.posedge_event());
    } while (!pins_.w_ready.read());
    pins_.w_valid.write(false);
    auto *trans = sink_.req_port->read();
    Require(trans->is_write() && trans->get_address() == 0x100 + id * 8,
            "wrong TLM write request");
    return trans;
  }

  void Complete(tlm::tlm_generic_payload *trans) {
    trans->set_response_status(tlm::TLM_OK_RESPONSE);
    sink_.resp_port->write(trans);
  }

  void Run() {
    Reset();
    auto *trans = SendWrite(1);
    Complete(trans);
    auto completed_at = sc_core::sc_time_stamp();
    uint32_t expected_id = 1;
    if (reset_) {
      // The response has reached the adapter before resetting inside its gap.
      wait(clock_.posedge_event());
      wait(clock_.posedge_event());
      discard_old_ = true;
      Reset();
      for (unsigned i = 0; i < 10; ++i) wait(clock_.posedge_event());
      trans = SendWrite(2);
      Complete(trans);
      completed_at = sc_core::sc_time_stamp();
      expected_id = 2;
    }
    do {
      wait(clock_.posedge_event());
    } while (!pins_.b_valid.read());
    // Eight waiting clock edges span at least seven full periods.
    Require(sc_core::sc_time_stamp() - completed_at >=
                sc_core::sc_time(70, sc_core::SC_NS),
            "write-response gap was not honored");
    for (unsigned i = 0; i < 2; ++i) {
      Require(pins_.b_valid.read() && pins_.b_id.read() == expected_id &&
                  pins_.b_resp.read() == kOkayResp,
              "wrong or unstable post-gap response");
      wait(clock_.posedge_event());
    }
    pins_.b_ready.write(true);
    wait(clock_.posedge_event());
    passed_ = true;
    sc_core::sc_stop();
  }
};
}  // namespace

int RunAxiWriteResponseGapCase(bool reset) {
  try {
    WriteResponseGapHarness harness("write_response_gap", reset);
    sc_core::sc_start();
    return harness.passed() ? 0 : 1;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}

namespace {
class SerializationResetHarness : public sc_core::sc_module {
 public:
  SerializationResetHarness(sc_core::sc_module_name name, bool old_read,
                            bool new_read, bool cross_channel)
      : sc_module(name),
        old_read_(old_read),
        new_read_(new_read),
        cross_channel_(cross_channel),
        clock_("clock", sc_core::sc_time(10, sc_core::SC_NS)),
        adapter_("adapter", Config()),
        sink_("sink") {
    adapter_.bind_clock(clock_);
    adapter_.bind_reset_n(reset_n_);
    adapter_.bind_pins(pins_.pins());
    adapter_.set_target(&sink_.port);
    SC_THREAD(Run);
    SC_THREAD(Watchdog);
    SC_METHOD(Monitor);
    sensitive << pins_.r_valid << pins_.r_id << pins_.b_valid << pins_.b_id;
  }

  bool passed() const { return passed_; }

 private:
  bool old_read_;
  bool new_read_;
  bool cross_channel_;
  bool discard_old_ = false;
  bool passed_ = false;
  sc_core::sc_clock clock_;
  sc_core::sc_signal<bool> reset_n_;
  AxiSignalBundle<32, 64, 8> pins_;
  AxiInitiatorAdapter<32, 64, 8> adapter_;
  lv::TlmSink sink_;

  static AxiAdapterConfig Config() {
    AxiAdapterConfig config;
    config.serialize_transactions = true;
    return config;
  }

  void Monitor() {
    Require(
        !discard_old_ || !(pins_.r_valid.read() && pins_.r_id.read() == 1) &&
                             !(pins_.b_valid.read() && pins_.b_id.read() == 1),
        "stale AXI response escaped serialization reset");
  }

  void Watchdog() {
    wait(10, sc_core::SC_US);
    throw std::runtime_error("serialization reset test timed out");
  }

  void Reset() {
    wait(clock_.negedge_event());
    reset_n_.write(false);
    wait(clock_.negedge_event());
    wait(clock_.negedge_event());
    reset_n_.write(true);
    wait(clock_.posedge_event());
  }

  void StartAddress(bool read, uint32_t id) {
    if (read) {
      pins_.ar_id.write(id);
      pins_.ar_addr.write(0x100 + id * 8);
      pins_.ar_len.write(0);
      pins_.ar_size.write(3);
      pins_.ar_burst.write(kIncrBurst);
      pins_.ar_valid.write(true);
    } else {
      pins_.aw_id.write(id);
      pins_.aw_addr.write(0x100 + id * 8);
      pins_.aw_len.write(0);
      pins_.aw_size.write(3);
      pins_.aw_burst.write(kIncrBurst);
      pins_.aw_valid.write(true);
    }
  }

  void FinishAddress(bool read) {
    do {
      wait(clock_.posedge_event());
    } while (!(read ? pins_.ar_ready.read() : pins_.aw_ready.read()));
    if (read) {
      pins_.ar_valid.write(false);
    } else {
      pins_.aw_valid.write(false);
      pins_.w_data.write(0x1122334455667788ULL);
      pins_.w_strb.write(0xff);
      pins_.w_last.write(true);
      pins_.w_valid.write(true);
      do {
        wait(clock_.posedge_event());
      } while (!pins_.w_ready.read());
      pins_.w_valid.write(false);
    }
  }

  tlm::tlm_generic_payload *ReceiveRequest(bool read, uint32_t id) {
    auto *trans = sink_.req_port->read();
    Require(trans->is_read() == read &&
                trans->get_address() == 0x100 + id * 8 &&
                trans->get_data_length() == 8,
            "incorrect serialized TLM request");
    if (!read) {
      const std::array<uint8_t, 8> expected{0x88, 0x77, 0x66, 0x55,
                                            0x44, 0x33, 0x22, 0x11};
      Require(
          std::equal(expected.begin(), expected.end(), trans->get_data_ptr()),
          "incorrect serialized write data");
    }
    return trans;
  }

  tlm::tlm_generic_payload *SendRequest(bool read, uint32_t id) {
    StartAddress(read, id);
    FinishAddress(read);
    return ReceiveRequest(read, id);
  }

  void Complete(tlm::tlm_generic_payload *trans, uint8_t byte) {
    if (trans->is_read())
      std::fill_n(trans->get_data_ptr(), trans->get_data_length(), byte);
    trans->set_response_status(tlm::TLM_OK_RESPONSE);
    sink_.resp_port->write(trans);
  }

  void CheckBlocked(bool read) {
    Require(!(read ? pins_.ar_ready.read() : pins_.aw_ready.read()),
            "third request accepted before the current transaction completed");
    Require(
        sink_.req_port->num_available() == 0,
        "third request reached TLM before the current transaction completed");
  }

  void CheckResponse(bool read, uint32_t id) {
    if (read) {
      Require(pins_.r_valid.read() && pins_.r_id.read() == id &&
                  pins_.r_last.read() && pins_.r_resp.read() == kOkayResp &&
                  pins_.r_data.read() ==
                      (id == 2 ? 0x2222222222222222ULL : 0x3333333333333333ULL),
              "incorrect serialized read response");
    } else {
      Require(pins_.b_valid.read() && pins_.b_id.read() == id &&
                  pins_.b_resp.read() == kOkayResp,
              "incorrect serialized write response");
    }
  }

  void AcceptResponse(bool read) {
    if (read)
      pins_.r_ready.write(true);
    else
      pins_.b_ready.write(true);
    wait(clock_.posedge_event());
    if (read)
      pins_.r_ready.write(false);
    else
      pins_.b_ready.write(false);
  }

  void Run() {
    Reset();
    auto *old_trans = SendRequest(old_read_, 1);
    discard_old_ = true;
    Reset();
    auto *new_trans = SendRequest(new_read_, 2);
    const bool third_read = cross_channel_ ? !new_read_ : new_read_;
    StartAddress(third_read, 3);
    Complete(old_trans, 0x11);
    for (unsigned i = 0; i < 8; ++i) {
      wait(clock_.posedge_event());
      CheckBlocked(third_read);
    }
    Complete(new_trans, 0x22);
    do {
      wait(clock_.posedge_event());
      CheckBlocked(third_read);
    } while (!(new_read_ ? pins_.r_valid.read() : pins_.b_valid.read()));
    // A completed TLM request still owns the slot until its AXI response is
    // accepted.
    for (unsigned i = 0; i < 3; ++i) {
      CheckResponse(new_read_, 2);
      CheckBlocked(third_read);
      wait(clock_.posedge_event());
    }
    AcceptResponse(new_read_);
    FinishAddress(third_read);
    auto *third_trans = ReceiveRequest(third_read, 3);
    Complete(third_trans, 0x33);
    do {
      wait(clock_.posedge_event());
    } while (!(third_read ? pins_.r_valid.read() : pins_.b_valid.read()));
    CheckResponse(third_read, 3);
    AcceptResponse(third_read);
    passed_ = true;
    sc_core::sc_stop();
  }
};
}  // namespace

int RunAxiSerializationResetCase(bool old_read, bool new_read,
                                 bool cross_channel) {
  try {
    SerializationResetHarness harness("serialization_reset", old_read, new_read,
                                      cross_channel);
    sc_core::sc_start();
    return harness.passed() ? 0 : 1;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
