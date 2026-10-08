// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include <liblv/axi/axi_adapters.h>
#include <liblv/log.h>

#include <algorithm>
#include <array>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "lv/src/lv/bindings/simple/memory.h"

namespace {
using lv::axi::AxiAdapterConfig;
using lv::axi::AxiExtension;
using lv::axi::AxiInitiatorAdapter;
using lv::axi::AxiSignalBundle;
using lv::axi::AxiTargetAdapter;
using lv::axi::kFixedBurst;
using lv::axi::kIncrBurst;
using lv::axi::kOkayResp;
using lv::axi::kSlvErrResp;
using lv::axi::kWrapBurst;

void Require(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}

class SafetyHarness : public sc_core::sc_module {
 public:
  SafetyHarness(sc_core::sc_module_name name, std::string scenario, bool split,
                bool read, unsigned beats)
      : sc_module(name),
        scenario_(std::move(scenario)),
        split_(split),
        read_(read),
        beats_(beats),
        clock_("clock", sc_core::sc_time(10, sc_core::SC_NS)),
        initiator_("initiator"),
        memory_("memory", simple::Memory::Param{8192}) {
    initiator_.bind_clock(clock_);
    initiator_.bind_reset_n(reset_n_);
    initiator_.bind_pins(pins_.pins());
    initiator_.set_target(
        const_cast<tlm_utils::simple_target_socket<lv::TlmSink> *>(
            memory_.port()));
    memory_.set_clock(&clock_);
    if (scenario_.find("-initiator") == std::string::npos) {
      AxiAdapterConfig config;
      config.allow_burst_split = split_;
      target_ = std::make_unique<AxiTargetAdapter<32, 64, 8>>("target", config);
      target_->bind_clock(clock_);
      target_->bind_reset_n(reset_n_);
      target_->bind_pins(pins_.pins());
      host_ = std::make_unique<lv::TlmSource>("host");
      host_->set_target(target_->target_socket());
    }
    SC_THREAD(Run);
    SC_THREAD(Monitor);
    SC_THREAD(Watchdog);
  }

  bool passed() const { return passed_; }
  bool NoDownstreamAccess() const {
    const auto stats = memory_.stats().tabularize();
    const auto group = stats[memory_.stats().name()];
    if (group["total_reads"]["val"].value<lv::stats::Integer>().value() != 0 ||
        group["total_writes"]["val"].value<lv::stats::Integer>().value() != 0)
      return false;
    if (memory_.read_bytes(0, 8192).value() != std::vector<uint8_t>(8192, 0) &&
        scenario_.find("-initiator") != std::string::npos)
      return false;
    return !target_ ||
           (addresses_ == 0 && strobes_.empty() && !pins_.ar_valid.read() &&
            !pins_.aw_valid.read() && !pins_.w_valid.read());
  }

 private:
  std::string scenario_;
  bool split_;
  bool read_;
  unsigned beats_;
  bool passed_ = false;
  unsigned addresses_ = 0;
  std::vector<uint32_t> strobes_;
  std::vector<std::array<uint32_t, 3>> bursts_;
  sc_core::sc_clock clock_;
  sc_core::sc_signal<bool> reset_n_;
  AxiSignalBundle<32, 64, 8> pins_;
  AxiInitiatorAdapter<32, 64, 8> initiator_;
  simple::Memory memory_;
  std::unique_ptr<AxiTargetAdapter<32, 64, 8>> target_;
  std::unique_ptr<lv::TlmSource> host_;

  void Monitor() {
    for (;;) {
      wait(clock_.posedge_event());
      if (!reset_n_.read()) continue;
      addresses_ += pins_.ar_valid.read() && pins_.ar_ready.read();
      addresses_ += pins_.aw_valid.read() && pins_.aw_ready.read();
      if (pins_.ar_valid.read() && pins_.ar_ready.read())
        bursts_.push_back(
            {pins_.ar_addr.read(), pins_.ar_len.read(), pins_.ar_size.read()});
      if (pins_.aw_valid.read() && pins_.aw_ready.read())
        bursts_.push_back(
            {pins_.aw_addr.read(), pins_.aw_len.read(), pins_.aw_size.read()});
      if (pins_.w_valid.read() && pins_.w_ready.read())
        strobes_.push_back(pins_.w_strb.read());
    }
  }

  void Watchdog() {
    wait(100, sc_core::SC_US);
    std::cerr << "SAFETY_TIMEOUT\n";
    sc_core::sc_stop();
  }

  void Run() {
    reset_n_.write(false);
    wait(clock_.posedge_event());
    wait(clock_.posedge_event());
    reset_n_.write(true);
    wait(clock_.posedge_event());
    if (scenario_.find("-initiator") != std::string::npos) {
      DriveUnsupported();
    } else if (scenario_.find("geometry-") == 0) {
      CheckGeometry();
    } else {
      Submit();
    }
    passed_ = true;
    sc_core::sc_stop();
  }

  void DriveUnsupported() {
    const bool unaligned = scenario_ == "unaligned-initiator";
    const bool wrap = scenario_ == "wrap-initiator";
    uint32_t address = unaligned ? 0x41 : (wrap ? 0x18 : 0x40);
    uint32_t size = unaligned ? 1 : 3;
    uint32_t length = beats_ - 1;
    const auto burst = scenario_ == "fixed-initiator" ? kFixedBurst
                       : wrap                         ? kWrapBurst
                                                      : kIncrBurst;
    if (scenario_ == "oversized-initiator") size = 4;
    if (scenario_ == "raw-size-initiator") size = 256;
    if (scenario_ == "len-initiator") length = 256;
    if (scenario_ == "raw-len-initiator") length = 0xffffffffu;
    if (scenario_ == "page-initiator") address = 0xff8;
    if (scenario_ == "range-initiator") address = 0xfffffff8u;
    if (read_) {
      pins_.ar_id.write(7);
      pins_.ar_addr.write(address);
      pins_.ar_size.write(size);
      pins_.ar_len.write(length);
      pins_.ar_burst.write(burst);
      pins_.ar_valid.write(true);
      pins_.r_ready.write(true);
    } else {
      pins_.aw_id.write(7);
      pins_.aw_addr.write(address);
      pins_.aw_size.write(size);
      pins_.aw_len.write(length);
      pins_.aw_burst.write(burst);
      pins_.aw_valid.write(true);
      pins_.b_ready.write(true);
    }
    if (scenario_ == "early-wlast-initiator" ||
        scenario_ == "missing-wlast-initiator") {
      do {
        wait(clock_.posedge_event());
      } while (!pins_.aw_ready.read());
      pins_.aw_valid.write(false);
      const bool early = scenario_ == "early-wlast-initiator";
      for (unsigned beat = 0; beat < (early ? 1u : beats_); ++beat) {
        pins_.w_data.write(0x123456789abcdef0ULL);
        pins_.w_strb.write(0xff);
        pins_.w_last.write(early);
        pins_.w_valid.write(true);
        do {
          wait(clock_.posedge_event());
        } while (!pins_.w_ready.read());
      }
      pins_.w_valid.write(false);
    }
    // No W data is necessary to diagnose an unsupported AW burst.
    // Without the guard the read completes or the write stalls; neither is
    // fatal.
    for (unsigned cycle = 0; cycle < 20; ++cycle) wait(clock_.posedge_event());
  }

  void CheckGeometry() {
    const bool plain = scenario_.find("-plain-") != std::string::npos;
    const bool page = scenario_.find("page-") != std::string::npos;
    const bool too_many = scenario_.find("too-many-beats") != std::string::npos;
    const bool crossing = scenario_.find("page-cross") != std::string::npos;
    const uint32_t address = page ? 0xff8 : 0x40;
    const size_t length = page ? (crossing ? 16 : 8) : (too_many ? 2056 : 2048);
    const bool reject = !split_ && (too_many || crossing);
    std::vector<uint8_t> initial(8192, 0x5a);
    memory_.write_bytes(0, simple::Memory::LuaBytes(initial));
    std::vector<uint8_t> data(length + 2, 0xa5);
    if (!read_) std::fill(data.begin() + 1, data.end() - 1, 0xc3);
    const auto before = data;
    tlm::tlm_generic_payload trans;
    AxiExtension *ext = nullptr;
    if (!plain) {
      ext = new AxiExtension;
      ext->txn_id = 7;
      ext->beat_size = 3;
      ext->burst_len = length / 8 - 1;
      trans.set_extension(ext);
    }
    trans.set_command(read_ ? tlm::TLM_READ_COMMAND : tlm::TLM_WRITE_COMMAND);
    trans.set_address(address);
    trans.set_data_ptr(data.data() + 1);
    trans.set_data_length(length);
    trans.set_streaming_width(length);
    host_->req_port->write(&trans);
    Require(host_->resp_port->read() == &trans,
            "wrong geometry response payload");
    Require(trans.get_response_status() ==
                (reject ? tlm::TLM_BURST_ERROR_RESPONSE : tlm::TLM_OK_RESPONSE),
            "incorrect burst geometry status");
    if (reject) {
      Require(!ext || ext->axi_resp == kSlvErrResp,
              "missing geometry AXI error");
      Require(NoDownstreamAccess(), "rejected geometry reached downstream");
      Require(data == before, "rejected geometry changed payload");
      Require(memory_.read_bytes(0, initial.size()).value() == initial,
              "rejected geometry changed memory");
      return;
    }
    auto expected_data = before;
    auto expected_memory = initial;
    if (read_)
      std::fill(expected_data.begin() + 1, expected_data.end() - 1, 0x5a);
    else
      std::fill(expected_memory.begin() + address,
                expected_memory.begin() + address + length, 0xc3);
    Require(data == expected_data,
            "incorrect geometry read data or guard bytes");
    Require(memory_.read_bytes(0, initial.size()).value() == expected_memory,
            "incorrect geometry memory contents");
    const size_t count = split_ ? length / 8 : 1;
    Require(bursts_.size() == count, "incorrect geometry burst count");
    for (size_t i = 0; i < count; ++i) {
      const std::array<uint32_t, 3> expected{
          address + static_cast<uint32_t>(split_ ? 8 * i : 0),
          static_cast<uint32_t>(split_ ? 0 : length / 8 - 1), 3};
      Require(bursts_[i] == expected, "incorrect AXI address, length or size");
    }
    if (!read_)
      Require(strobes_ == std::vector<uint32_t>(length / 8, 0xff),
              "incorrect geometry write beats or strobes");
  }

  void Submit() {
    tlm::tlm_generic_payload trans;
    auto *ext = new AxiExtension;
    trans.set_extension(ext);  // The payload owns the extension.
    ext->txn_id = 7;
    ext->beat_size = 3;
    ext->burst_len = 1;
    std::array<uint8_t, 34> buffer;
    buffer.fill(0xa5);
    std::vector<uint8_t> initial(32, 0x5a);
    for (size_t i = 0; i < 16; ++i) {
      initial[i + 1] = static_cast<uint8_t>(0x30 + i);
      if (!read_) buffer[i + 1] = static_cast<uint8_t>(0xb0 + i);
    }
    memory_.write_bytes(0x3f, simple::Memory::LuaBytes(initial));
    const auto before = buffer;
    trans.set_command(read_ ? tlm::TLM_READ_COMMAND : tlm::TLM_WRITE_COMMAND);
    trans.set_address(0x40);
    trans.set_data_ptr(buffer.data() + 1);
    trans.set_data_length(16);
    trans.set_streaming_width(16);
    trans.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
    auto expected = tlm::TLM_BURST_ERROR_RESPONSE;
    std::vector<uint8_t> enables;
    bool plain = false;
    if (scenario_ == "fixed-target") {
      ext->burst_type = kFixedBurst;
      ext->burst_len = beats_ - 1;
      trans.set_data_length(8 * beats_);
    } else if (scenario_ == "wrap-target") {
      ext->burst_type = kWrapBurst;
    } else if (scenario_ == "streaming-zero") {
      trans.set_streaming_width(0);
    } else if (scenario_ == "streaming-short") {
      trans.set_streaming_width(8);
    } else if (scenario_ == "streaming-equal" ||
               scenario_ == "streaming-long") {
      trans.set_streaming_width(scenario_ == "streaming-equal" ? 16 : 32);
      expected = tlm::TLM_OK_RESPONSE;
    } else if (scenario_ == "unaligned" || scenario_ == "aligned-narrow") {
      trans.set_address(scenario_ == "unaligned" ? 0x41 : 0x42);
      trans.set_data_length(4);
      ext->beat_size = 1;
      if (scenario_ == "aligned-narrow") expected = tlm::TLM_OK_RESPONSE;
    } else if (scenario_ == "plain9") {
      plain = true;
      trans.set_data_length(9);
    } else if (scenario_ == "plain3") {
      plain = true;
      trans.set_data_length(3);
    } else if (scenario_ == "short") {
      trans.set_data_length(8);
    } else if (scenario_ == "long") {
      trans.set_data_length(24);
    } else if (scenario_ == "zero" || scenario_ == "plain-zero") {
      plain = scenario_ == "plain-zero";
      trans.set_data_length(0);
    } else if (scenario_ == "null") {
      trans.set_data_ptr(nullptr);
      expected = tlm::TLM_GENERIC_ERROR_RESPONSE;
    } else if (scenario_ == "size") {
      ext->beat_size = 255;
    } else if (scenario_ == "huge-burst") {
      ext->burst_len = std::numeric_limits<uint32_t>::max();
    } else if (scenario_ == "cross") {
      trans.set_address(0x47);
    } else if (scenario_ == "later-cross") {
      trans.set_address(0x41);
      trans.set_data_length(8);
      ext->beat_size = 1;
      ext->burst_len = 3;
    } else if (scenario_ == "address-overflow") {
      trans.set_address(std::numeric_limits<uint64_t>::max() - 7);
    } else if (scenario_ == "address-width") {
      trans.set_address(uint64_t{1} << 32);
    } else if (scenario_ == "byte-zero") {
      enables = {0xff};
      expected = tlm::TLM_BYTE_ENABLE_ERROR_RESPONSE;
    } else if (scenario_ == "byte1" || scenario_ == "byte2" ||
               scenario_ == "byte3") {
      enables = {0xff};
      if (scenario_ != "byte1") enables.push_back(0);
      if (scenario_ == "byte3") enables.push_back(0xff);
      expected = tlm::TLM_OK_RESPONSE;
    } else if (scenario_ == "plain-valid" || scenario_ == "null-enables") {
      plain = scenario_ == "plain-valid";
      expected = tlm::TLM_OK_RESPONSE;
      trans.set_byte_enable_length(7);  // Ignored with a null pointer.
    } else {
      throw std::runtime_error("unknown safety case");
    }
    if (plain) {
      trans.clear_extension(ext);
      delete ext;
      ext = nullptr;
    }
    if (!enables.empty()) {
      trans.set_byte_enable_ptr(enables.data());
      trans.set_byte_enable_length(scenario_ == "byte-zero" ? 0
                                                            : enables.size());
    }
    host_->req_port->write(&trans);
    Require(host_->resp_port->read() == &trans, "wrong response payload");
    if (scenario_ == "fixed-target")
      return;  // Returning normally must fail the death test.
    Require(trans.get_response_status() == expected,
            "incorrect TLM response status");
    if (expected != tlm::TLM_OK_RESPONSE) {
      Require(!ext || ext->axi_resp == kSlvErrResp, "missing AXI error status");
      Require(NoDownstreamAccess(), "rejected payload reached downstream");
      Require(buffer == before, "rejected payload changed caller buffer");
      Require(memory_.read_bytes(0x3f, 32).value() == initial,
              "rejected payload changed memory");
      return;
    }
    if (scenario_ == "aligned-narrow") {
      auto expected_buffer = before;
      auto expected_memory = initial;
      for (size_t i = 0; i < 4; ++i) {
        if (read_)
          expected_buffer[i + 1] = initial[i + 3];
        else
          expected_memory[i + 3] = before[i + 1];
      }
      Require(buffer == expected_buffer, "incorrect narrow read lanes");
      Require(memory_.read_bytes(0x3f, 32).value() == expected_memory,
              "incorrect narrow write lanes");
      Require(addresses_ == (split_ ? 2u : 1u), "incorrect narrow burst count");
      if (!read_)
        Require(strobes_ == std::vector<uint32_t>{0x0c, 0x30},
                "incorrect narrow WSTRB");
      return;
    }
    auto expected_buffer = before;
    auto expected_memory = initial;
    for (size_t i = 0; i < 16; ++i) {
      if (enables.empty() || enables[i % enables.size()] == 0xff) {
        if (read_)
          expected_buffer[i + 1] = initial[i + 1];
        else
          expected_memory[i + 1] = before[i + 1];
      }
    }
    Require(buffer == expected_buffer,
            "read data, disabled bytes or guard bytes changed incorrectly");
    Require(memory_.read_bytes(0x3f, 32).value() == expected_memory,
            "incorrect byte-enable memory effects");
    Require(addresses_ == (split_ ? 2u : 1u), "incorrect AXI address count");
    if (!read_) {
      Require(strobes_.size() == 2, "incorrect write beat count");
      for (size_t beat = 0; beat < 2; ++beat) {
        uint32_t expected_strobe = 0;
        for (size_t lane = 0; lane < 8; ++lane) {
          if (enables.empty() ||
              enables[(8 * beat + lane) % enables.size()] == 0xff)
            expected_strobe |= 1u << lane;
        }
        Require(strobes_[beat] == expected_strobe, "incorrect WSTRB pattern");
      }
    }
  }
};
}  // namespace

namespace {

class WriteAwAfterWHarness : public sc_core::sc_module {
 public:
  explicit WriteAwAfterWHarness(sc_core::sc_module_name name,
                                std::string scenario = "aw-after-w")
      : sc_module(name),
        scenario_(std::move(scenario)),
        clock_("clock", sc_core::sc_time(10, sc_core::SC_NS)),
        target_("target"),
        host_("host") {
    target_.bind_clock(clock_);
    target_.bind_reset_n(reset_n_);
    auto pins = pins_.pins();
    if (scenario_ == "absent-bid-response") pins.b_id = nullptr;
    target_.bind_pins(pins);
    host_.set_target(target_.target_socket());
    SC_THREAD(Run);
    SC_THREAD(Slave);
    SC_THREAD(Watchdog);
  }

  bool passed() const { return passed_; }

 private:
  std::string scenario_;
  sc_core::sc_clock clock_;
  sc_core::sc_signal<bool> reset_n_;
  AxiSignalBundle<32, 64, 8> pins_;
  AxiTargetAdapter<32, 64, 8> target_;
  lv::TlmSource host_;
  bool passed_ = false;
  unsigned address_handshakes_ = 0;
  unsigned data_handshakes_ = 0;

  void Slave() {
    if (scenario_.find("rid") != std::string::npos ||
        scenario_.find("rlast") != std::string::npos ||
        scenario_ == "valid-read-response") {
      while (!reset_n_.read()) wait(clock_.posedge_event());
      pins_.ar_ready.write(true);
      do {
        wait(clock_.posedge_event());
      } while (!pins_.ar_valid.read());
      ++address_handshakes_;
      pins_.ar_ready.write(false);
      const unsigned beats = scenario_ == "early-rlast-response" ? 1 : 2;
      for (unsigned beat = 0; beat < beats; ++beat) {
        pins_.r_valid.write(true);
        pins_.r_id.write(
            scenario_ == "wrong-rid-response" ||
                    (scenario_ == "late-wrong-rid-response" && beat == 1)
                ? 9
                : 3);
        pins_.r_data.write(0x123456789abcdef0ULL + beat);
        pins_.r_resp.write(kOkayResp);
        pins_.r_last.write(
            scenario_ == "early-rlast-response" ||
            (beat == 1 && scenario_ != "missing-rlast-response"));
        do {
          wait(clock_.posedge_event());
        } while (!pins_.r_ready.read());
        ++data_handshakes_;
      }
      pins_.r_valid.write(false);
      return;
    }
    for (;;) {
      if (!reset_n_.read()) {
        pins_.aw_ready.write(false);
        pins_.w_ready.write(false);
        pins_.b_valid.write(false);
      } else {
        // This is legal AXI behavior: AWREADY waits for WVALID.
        pins_.aw_ready.write(pins_.w_valid.read());
        pins_.w_ready.write(true);
        if (pins_.aw_valid.read() && pins_.aw_ready.read()) {
          ++address_handshakes_;
        }
        if (pins_.w_valid.read() && pins_.w_ready.read()) {
          ++data_handshakes_;
          if (pins_.w_last.read()) {
            pins_.b_id.write(scenario_ == "wrong-bid-response" ? 9 : 3);
            pins_.b_resp.write(kOkayResp);
            pins_.b_valid.write(true);
          }
        }
        if (pins_.b_valid.read() && pins_.b_ready.read()) {
          pins_.b_valid.write(false);
        }
      }
      wait(clock_.posedge_event());
    }
  }

  void Watchdog() {
    wait(2, sc_core::SC_US);
    std::cerr << "AW_AFTER_W_TIMEOUT\n";
    sc_core::sc_stop();
  }

  void Run() {
    reset_n_.write(false);
    wait(clock_.posedge_event());
    wait(clock_.posedge_event());
    reset_n_.write(true);
    wait(clock_.posedge_event());

    std::array<uint8_t, 16> data{};
    tlm::tlm_generic_payload trans;
    auto *ext = new AxiExtension;
    trans.set_extension(ext);
    ext->txn_id = 3;
    ext->beat_size = 3;
    ext->burst_len = 1;
    const bool read = scenario_.find("rid") != std::string::npos ||
                      scenario_.find("rlast") != std::string::npos ||
                      scenario_ == "valid-read-response";
    trans.set_command(read ? tlm::TLM_READ_COMMAND : tlm::TLM_WRITE_COMMAND);
    trans.set_address(0x100);
    trans.set_data_ptr(data.data());
    trans.set_data_length(data.size());
    trans.set_streaming_width(data.size());
    trans.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);

    host_.req_port->write(&trans);
    Require(host_.resp_port->read() == &trans,
            "wrong AW-after-W response payload");
    Require(trans.get_response_status() == tlm::TLM_OK_RESPONSE,
            "AW-after-W write did not complete");
    if (read)
      Require(
          data == std::array<uint8_t, 16>{0xf0, 0xde, 0xbc, 0x9a, 0x78, 0x56,
                                          0x34, 0x12, 0xf1, 0xde, 0xbc, 0x9a,
                                          0x78, 0x56, 0x34, 0x12},
          "independent slave read data mismatch");
    Require(address_handshakes_ == 1, "AW handshake count mismatch");
    Require(data_handshakes_ == 2, "W handshake count mismatch");
    trans.clear_extension(ext);
    delete ext;
    passed_ = true;
    sc_core::sc_stop();
  }
};

}  // namespace

int RunAxiWriteAwAfterWCase() {
  try {
    WriteAwAfterWHarness harness("aw_after_w");
    sc_core::sc_start();
    return harness.passed() ? 0 : 1;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    lv::log::flush();
    return 1;
  }
}

namespace {

class TargetResetHarness : public sc_core::sc_module {
 public:
  TargetResetHarness(sc_core::sc_module_name name, bool read)
      : sc_module(name),
        read_(read),
        clock_("clock", sc_core::sc_time(10, sc_core::SC_NS)),
        target_("target"),
        host_("host") {
    target_.bind_clock(clock_);
    target_.bind_reset_n(reset_n_);
    target_.bind_pins(pins_.pins());
    host_.set_target(target_.target_socket());
    SC_THREAD(Run);
    SC_THREAD(Slave);
    SC_THREAD(ResetDriver);
    SC_THREAD(Watchdog);
  }

  bool passed() const { return passed_; }

 private:
  bool read_;
  sc_core::sc_clock clock_;
  sc_core::sc_signal<bool> reset_n_;
  AxiSignalBundle<32, 64, 8> pins_;
  AxiTargetAdapter<32, 64, 8> target_;
  lv::TlmSource host_;
  bool reset_triggered_ = false;
  bool passed_ = false;
  bool recovering_ = false;

  void Slave() {
    for (;;) {
      if (!reset_n_.read()) {
        pins_.ar_ready.write(false);
        pins_.aw_ready.write(false);
        pins_.w_ready.write(false);
        pins_.r_valid.write(false);
        pins_.b_valid.write(false);
      } else {
        pins_.ar_ready.write(true);
        pins_.aw_ready.write(true);
        pins_.w_ready.write(true);
        if (pins_.r_valid.read() && pins_.r_ready.read())
          pins_.r_valid.write(false);
        if (pins_.b_valid.read() && pins_.b_ready.read())
          pins_.b_valid.write(false);
        if (recovering_ && pins_.ar_valid.read() && pins_.ar_ready.read()) {
          pins_.r_id.write(pins_.ar_id.read());
          pins_.r_data.write(0x8877665544332211ULL);
          pins_.r_resp.write(kOkayResp);
          pins_.r_last.write(true);
          pins_.r_valid.write(true);
        }
        if (recovering_ && pins_.w_valid.read() && pins_.w_ready.read()) {
          Require(pins_.w_last.read(), "recovery WLAST missing");
          pins_.b_id.write(4);
          pins_.b_resp.write(kOkayResp);
          pins_.b_valid.write(true);
        }
      }
      wait(clock_.posedge_event());
    }
  }

  void ResetDriver() {
    reset_n_.write(false);
    wait(clock_.posedge_event());
    wait(clock_.posedge_event());
    reset_n_.write(true);
    for (;;) {
      wait(clock_.posedge_event());
      const bool address_handshake =
          read_ ? pins_.ar_valid.read() && pins_.ar_ready.read()
                : pins_.aw_valid.read() && pins_.aw_ready.read();
      const bool data_handshake = !read_ && pins_.w_valid.read() &&
                                  pins_.w_ready.read() && pins_.w_last.read();
      if ((read_ ? address_handshake : data_handshake) && !reset_triggered_) {
        reset_triggered_ = true;
        reset_n_.write(false);
        wait(clock_.posedge_event());
        wait(clock_.posedge_event());
        reset_n_.write(true);
        return;
      }
    }
  }

  void Watchdog() {
    wait(2, sc_core::SC_US);
    std::cerr << "TARGET_RESET_TIMEOUT\n";
    sc_core::sc_stop();
  }

  void Run() {
    wait(clock_.posedge_event());
    wait(clock_.posedge_event());
    wait(clock_.posedge_event());

    std::array<uint8_t, 8> data{};
    tlm::tlm_generic_payload trans;
    auto *ext = new AxiExtension;
    trans.set_extension(ext);
    ext->txn_id = 4;
    ext->beat_size = 3;
    ext->burst_len = 0;
    trans.set_command(read_ ? tlm::TLM_READ_COMMAND : tlm::TLM_WRITE_COMMAND);
    trans.set_address(0x180);
    trans.set_data_ptr(data.data());
    trans.set_data_length(data.size());
    trans.set_streaming_width(data.size());
    trans.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);

    host_.req_port->write(&trans);
    Require(host_.resp_port->read() == &trans,
            "wrong target-reset response payload");
    Require(reset_triggered_, "target reset was not triggered");
    Require(trans.get_response_status() == tlm::TLM_GENERIC_ERROR_RESPONSE,
            "reset transaction did not return a generic error");
    Require(ext->axi_resp == kSlvErrResp,
            "reset transaction did not return an AXI slave error");
    wait(clock_.posedge_event());
    Require(!pins_.ar_valid.read() && !pins_.aw_valid.read() &&
                !pins_.w_valid.read() && !pins_.r_ready.read() &&
                !pins_.b_ready.read(),
            "target outputs remained active after reset");
    while (!reset_n_.read()) wait(clock_.posedge_event());
    recovering_ = true;
    trans.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
    host_.req_port->write(&trans);
    Require(host_.resp_port->read() == &trans, "wrong recovery payload");
    Require(trans.get_response_status() == tlm::TLM_OK_RESPONSE &&
                ext->axi_resp == kOkayResp,
            "post-reset transaction failed");
    if (read_)
      Require(data == std::array<uint8_t, 8>{0x11, 0x22, 0x33, 0x44, 0x55, 0x66,
                                             0x77, 0x88},
              "post-reset read data mismatch");
    trans.clear_extension(ext);
    delete ext;
    passed_ = true;
    sc_core::sc_stop();
  }
};

}  // namespace

int RunAxiTargetResetCase(bool read) {
  try {
    TargetResetHarness harness("target_reset", read);
    sc_core::sc_start();
    return harness.passed() ? 0 : 1;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    lv::log::flush();
    return 1;
  }
}

namespace {

class InitiatorResetHarness : public sc_core::sc_module {
 public:
  explicit InitiatorResetHarness(sc_core::sc_module_name name)
      : sc_module(name),
        clock_("clock", sc_core::sc_time(10, sc_core::SC_NS)),
        adapter_("adapter"),
        memory_("memory", simple::Memory::Param{}) {
    adapter_.bind_clock(clock_);
    adapter_.bind_reset_n(reset_n_);
    adapter_.bind_pins(pins_.pins());
    adapter_.set_target(
        const_cast<tlm_utils::simple_target_socket<lv::TlmSink> *>(
            memory_.port()));
    memory_.set_clock(&clock_);
    SC_THREAD(Run);
    SC_THREAD(Watchdog);
  }

  bool passed() const { return passed_; }

 private:
  sc_core::sc_clock clock_;
  sc_core::sc_signal<bool> reset_n_;
  AxiSignalBundle<32, 64, 8> pins_;
  AxiInitiatorAdapter<32, 64, 8> adapter_;
  simple::Memory memory_;
  bool passed_ = false;

  void Watchdog() {
    wait(5, sc_core::SC_US);
    std::cerr << "INITIATOR_RESET_TIMEOUT\n";
    sc_core::sc_stop();
  }

  void SendAddress(uint32_t id, uint32_t address) {
    pins_.aw_id.write(id);
    pins_.aw_addr.write(address);
    pins_.aw_len.write(0);
    pins_.aw_size.write(3);
    pins_.aw_burst.write(kIncrBurst);
    pins_.aw_valid.write(true);
    do {
      wait(clock_.posedge_event());
    } while (!pins_.aw_ready.read());
    pins_.aw_valid.write(false);
  }

  void SendData(uint64_t data) {
    pins_.w_data.write(data);
    pins_.w_strb.write(0xff);
    pins_.w_last.write(true);
    pins_.w_valid.write(true);
    do {
      wait(clock_.posedge_event());
    } while (!pins_.w_ready.read());
    pins_.w_valid.write(false);
    pins_.w_last.write(false);
  }

  void Run() {
    reset_n_.write(false);
    pins_.aw_valid.write(false);
    pins_.w_valid.write(false);
    pins_.b_ready.write(false);
    wait(clock_.posedge_event());
    wait(clock_.posedge_event());
    reset_n_.write(true);
    wait(clock_.posedge_event());

    SendAddress(1, 0x100);
    wait(clock_.posedge_event());

    // Reset after AW but before its W data arrives.
    reset_n_.write(false);
    wait(clock_.posedge_event());
    wait(clock_.posedge_event());
    reset_n_.write(true);
    wait(clock_.posedge_event());

    SendAddress(2, 0x200);
    SendData(0x1122334455667788ULL);
    pins_.b_ready.write(true);
    do {
      wait(clock_.posedge_event());
    } while (!pins_.b_valid.read());
    const auto response_id = pins_.b_id.read();
    wait(clock_.posedge_event());
    pins_.b_ready.write(false);

    const auto old_bytes = memory_.read_bytes(0x100, 8).value();
    const auto new_bytes = memory_.read_bytes(0x200, 8).value();
    Require(response_id == 2, "reset paired W data with the old AW");
    Require(old_bytes == std::vector<uint8_t>(8, 0),
            "reset transaction modified the old address");
    Require(new_bytes == std::vector<uint8_t>{0x88, 0x77, 0x66, 0x55, 0x44,
                                              0x33, 0x22, 0x11},
            "post-reset write did not reach the new address");
    passed_ = true;
    sc_core::sc_stop();
  }
};

}  // namespace

int RunAxiInitiatorResetCase() {
  try {
    InitiatorResetHarness harness("initiator_reset");
    sc_core::sc_start();
    return harness.passed() ? 0 : 1;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    lv::log::flush();
    return 1;
  }
}

int RunAxiResponseCase(const char *scenario) {
  try {
    WriteAwAfterWHarness harness("response", scenario);
    sc_core::sc_start();
    return harness.passed() ? 0 : 1;
  } catch (const lv::fatal_error &) {
    lv::log::flush();
    return 23;
  } catch (const sc_core::sc_report &report) {
    std::cerr << report.what() << '\n';
    lv::log::flush();
    return report.get_msg_type() ==
                   std::string(sc_core::SC_ID_SIMULATION_UNCAUGHT_EXCEPTION_)
               ? 23
               : 1;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}

int RunAxiSafetyCase(const char *scenario, bool split, bool read,
                     unsigned beats) {
  if (std::string(scenario).find("-response") != std::string::npos)
    return RunAxiResponseCase(scenario);
  SafetyHarness harness("safety", scenario, split, read, beats);
  try {
    sc_core::sc_start();
  } catch (const lv::fatal_error &) {
    if (!harness.NoDownstreamAccess()) return 1;
    lv::log::flush();
    return 23;
  } catch (const sc_core::sc_report &report) {
    // SystemC translates an exception escaping an SC_THREAD into this report.
    std::cerr << report.what() << '\n';
    lv::log::flush();
    if (report.get_msg_type() ==
            std::string(sc_core::SC_ID_SIMULATION_UNCAUGHT_EXCEPTION_) &&
        harness.NoDownstreamAccess())
      return 23;
    return 1;
  }
  return harness.passed() ? 0 : 1;
}
