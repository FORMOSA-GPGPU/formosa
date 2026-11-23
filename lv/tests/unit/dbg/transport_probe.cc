// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include <liblv/binding.h>
#include <liblv/log.h>
#include <liblv/schema.h>
#include <systemc.h>
#include <tlm.h>

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <sol/sol.hpp>
#include <string>
#include <vector>

#include "lv/tests/unit/tlm_endpoints.h"

namespace dbg {

namespace {

using tlm_test::Initiator;
using tlm_test::Payload;

sc_time Ns(double ns) { return sc_time(ns, SC_NS); }
double ToNs(const sc_time &time) { return time.to_seconds() * 1e9; }

/** @brief What a TransportProbe call observed. */
struct ProbeResult {
  /** @brief Target response status, such as TLM_OK_RESPONSE. */
  std::string response;
  /** @brief Annotated delay after the call, in ns. */
  double delay_ns = 0;
  /** @brief Simulated time that passed during the call, in ns. */
  double elapsed_ns = 0;
  /** @brief DMI hint the payload carries after the call. */
  bool dmi_allowed = false;
  /** @brief Byte count that debug transport returned. */
  unsigned int count = 0;
  /** @brief Payload data after the call. */
  std::vector<uint8_t> data;
};

/** @brief The DMI descriptor a TransportProbe request got. */
struct ProbeGrant {
  bool granted = false;
  /** @brief Query address after forwarding through the target. */
  uint64_t address = 0;
  /** @brief DMI hint the query payload carries afterwards. */
  bool dmi_allowed = false;
  uint64_t start = 0;
  uint64_t end = 0;
  bool read = false;
  bool write = false;
  double read_latency_ns = 0;
  double write_latency_ns = 0;
};

/**
 * @brief Drives one target socket from Lua with a single reused payload. A
 * test-only binding, built into lv from lv/tests.
 *
 * Every call goes through the same tlm_generic_payload, as a real initiator
 * that recycles payloads would, so a test sees a target leave a stale DMI
 * hint from an earlier response. Request fields are written to the payload
 * exactly as given: a test that needs an illegal field gets it, and the probe
 * never normalizes one first. Calls run in the caller's context, such as Lua
 * between sc.start calls, and report the simulated time they took.
 */
class TransportProbe : public sc_module {
 public:
  using TargetSocket = Initiator::TargetSocket;

  struct Request {
    std::string command = "read";
    uint64_t addr = 0;
    std::vector<uint8_t> data;
    std::vector<uint8_t> byte_enables;
    std::optional<unsigned int> byte_enable_length;
    std::optional<unsigned int> streaming_width;
    bool stale_byte_enables = false;
    double delay_ns = 0;
    // clang-format off
    LV_SCHEMA(TransportProbe, Request,
              LV_FIELD(command, "read, write or ignore"),
              LV_FIELD(addr, "Transaction address"),
              LV_FIELD(data,
                       "Payload bytes; their count is the data length"),
              LV_FIELD(byte_enables,
                       "Byte enables; empty leaves the pointer null"),
              LV_FIELD(byte_enable_length,
                       "Byte-enable length; defaults to the enables' count"),
              LV_FIELD(streaming_width,
                       "Streaming width; defaults to the data length"),
              LV_FIELD(stale_byte_enables,
                       "Leave a non-null, invalid byte-enable pointer, as a "
                       "recycled payload can carry into debug transport"),
              LV_FIELD(delay_ns, "Annotated delay the call starts with, in ns"))
    // clang-format on
  };

  explicit TransportProbe(const sc_module_name &name)
      : sc_module(name), initiator_("initiator") {
    initiator_.on_invalidate = [this](uint64_t start, uint64_t end) {
      invalidations_.push_back({start, end});
      if (start <= grant_.get_end_address() &&
          end >= grant_.get_start_address())
        granted_ = false;
    };
  }

  sol::table invalidations(sol::this_state state) const {
    auto result = sol::state_view(state).create_table();
    for (const auto &range : invalidations_) result.add(sol::as_table(range));
    return result;
  }
  bool dmi_valid() const { return granted_; }

  TargetSocket *target() const { return target_; }
  void set_target(TargetSocket *target) {
    if (target_) LV_FATAL("{}: target is already bound", this->name());
    target_ = target;
    initiator_.socket.bind(*target_);
  }

  ProbeResult blocking(const Request &request) {
    if (request.stale_byte_enables) {
      LV_FATAL("{}: stale byte enables are only for debug transport", name());
    }
    Prepare(request);
    sc_time delay = Ns(request.delay_ns);
    const sc_time before = sc_time_stamp();
    initiator_.SendBlocking(payload_, delay);
    ProbeResult result = Observe(before);
    result.delay_ns = ToNs(delay);
    return result;
  }

  ProbeResult debug(const Request &request) {
    Prepare(request);
    const sc_time before = sc_time_stamp();
    const unsigned int count = initiator_.Debug(payload_);
    ProbeResult result = Observe(before);
    result.count = count;
    return result;
  }

  /** @brief Request DMI; the grant stays for dmi_read and dmi_write. */
  ProbeGrant request_dmi(const Request &request) {
    Prepare(request);
    grant_ = tlm::tlm_dmi();
    ProbeGrant result;
    result.granted = initiator_.RequestDmi(payload_, grant_);
    granted_ = result.granted;
    result.address = payload_.get_address();
    result.dmi_allowed = payload_.is_dmi_allowed();
    result.start = grant_.get_start_address();
    result.end = grant_.get_end_address();
    result.read = grant_.is_read_allowed();
    result.write = grant_.is_write_allowed();
    result.read_latency_ns = ToNs(grant_.get_read_latency());
    result.write_latency_ns = ToNs(grant_.get_write_latency());
    return result;
  }

  /** @brief Read through the latest grant's pointer, bypassing transport. */
  sol::as_table_t<std::vector<uint8_t>> dmi_read(uint64_t addr, size_t size) {
    const unsigned char *bytes = GrantPointer(addr, size, false);
    return sol::as_table(std::vector<uint8_t>(bytes, bytes + size));
  }

  /** @brief Write through the latest grant's pointer, bypassing transport. */
  void dmi_write(uint64_t addr,
                 const sol::as_table_t<std::vector<uint8_t>> &data) {
    const auto &bytes = data.value();
    unsigned char *pointer = GrantPointer(addr, bytes.size(), true);
    std::copy(bytes.begin(), bytes.end(), pointer);
  }

 private:
  // Write each field the request names; the DMI hint is the target's to
  // clear, so it persists from the previous response.
  void Prepare(const Request &request) {
    if (!target_) LV_FATAL("{}: target is not bound", name());
    if (request.command == "read") {
      payload_.set_command(tlm::TLM_READ_COMMAND);
    } else if (request.command == "write") {
      payload_.set_command(tlm::TLM_WRITE_COMMAND);
    } else if (request.command == "ignore") {
      payload_.set_command(tlm::TLM_IGNORE_COMMAND);
    } else {
      LV_FATAL("{}: unknown command {}", name(), request.command);
    }
    data_.assign(request.data.begin(), request.data.end());
    enables_.assign(request.byte_enables.begin(), request.byte_enables.end());
    payload_.set_address(request.addr);
    payload_.set_data_ptr(data_.empty() ? nullptr : data_.data());
    payload_.set_data_length(data_.size());
    payload_.set_streaming_width(request.streaming_width.value_or(
        static_cast<unsigned int>(data_.size())));
    if (request.stale_byte_enables) {
      // Debug transport must ignore this pointer: dereferencing it faults.
      payload_.set_byte_enable_ptr(
          reinterpret_cast<unsigned char *>(uintptr_t{1}));
    } else {
      payload_.set_byte_enable_ptr(enables_.empty() ? nullptr
                                                    : enables_.data());
    }
    payload_.set_byte_enable_length(request.byte_enable_length.value_or(
        static_cast<unsigned int>(enables_.size())));
    payload_.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
  }

  ProbeResult Observe(const sc_time &before) const {
    ProbeResult result;
    result.response = payload_.get_response_string();
    result.elapsed_ns = ToNs(sc_time_stamp() - before);
    result.dmi_allowed = payload_.is_dmi_allowed();
    result.data.assign(data_.begin(), data_.end());
    return result;
  }

  unsigned char *GrantPointer(uint64_t addr, size_t size, bool write) {
    if (!granted_ || !grant_.get_dmi_ptr() || size == 0 ||
        addr < grant_.get_start_address() || addr > grant_.get_end_address() ||
        size - 1 > grant_.get_end_address() - addr ||
        !(write ? grant_.is_write_allowed() : grant_.is_read_allowed())) {
      LV_FATAL("{}: access {:#x}+{} is outside the latest DMI grant", name(),
               addr, size);
    }
    return grant_.get_dmi_ptr() + (addr - grant_.get_start_address());
  }

  Initiator initiator_;
  TargetSocket *target_ = nullptr;
  Payload payload_;
  std::vector<unsigned char> data_;
  std::vector<unsigned char> enables_;
  tlm::tlm_dmi grant_;
  bool granted_ = false;
  std::vector<std::array<uint64_t, 2>> invalidations_;
};

LV_BINDING(dbg, ProbeResult)
    .property("response", &ProbeResult::response,
              lv::doc("Target response status, such as TLM_OK_RESPONSE"))
    .property("delay_ns", &ProbeResult::delay_ns,
              lv::doc("Annotated delay after the call, in ns"))
    .property("elapsed_ns", &ProbeResult::elapsed_ns,
              lv::doc("Simulated time that passed during the call, in ns"))
    .property("dmi_allowed", &ProbeResult::dmi_allowed,
              lv::doc("DMI hint the payload carries after the call"))
    .property("count", &ProbeResult::count,
              lv::doc("Byte count that debug transport returned"))
    .property(
        "data",
        [](const ProbeResult &result) {
          return sol::as_table(result.data);
        },
        lv::doc("Payload data after the call"));

LV_BINDING(dbg, ProbeGrant)
    .property("granted", &ProbeGrant::granted,
              lv::doc("Whether the target granted DMI"))
    .property("address", &ProbeGrant::address,
              lv::doc("Query address after the call"))
    .property("dmi_allowed", &ProbeGrant::dmi_allowed,
              lv::doc("DMI hint the query payload carries afterwards"))
    .property("start", &ProbeGrant::start, lv::doc("First granted address"))
    .property("end_address", &ProbeGrant::end, lv::doc("Last granted address"))
    .property("read", &ProbeGrant::read, lv::doc("Whether reads are allowed"))
    .property("write", &ProbeGrant::write,
              lv::doc("Whether writes are allowed"))
    .property("read_latency_ns", &ProbeGrant::read_latency_ns,
              lv::doc("Granted read latency, in ns"))
    .property("write_latency_ns", &ProbeGrant::write_latency_ns,
              lv::doc("Granted write latency, in ns"));

LV_BINDING(dbg, TransportProbe)
    .constructor(
        [](const char *name) {
          return std::make_shared<TransportProbe>(name);
        },
        lv::params("name"),
        lv::doc("Create an initiator that drives one target socket from Lua "
                "with a single reused payload"))
    .property("target", &TransportProbe::target, &TransportProbe::set_target,
              lv::doc("Target socket the probe's initiator binds to"))
    .property(
        "invalidations", &TransportProbe::invalidations,
        lv::doc("Received DMI invalidation ranges, as {start, end} pairs"))
    .property("dmi_valid", &TransportProbe::dmi_valid,
              lv::doc("Whether the latest grant is still valid"))
    .method("blocking", &TransportProbe::blocking, lv::params("request"),
            lv::doc("Send the request with b_transport"))
    .method("debug", &TransportProbe::debug, lv::params("request"),
            lv::doc("Send the request with transport_dbg"))
    .method("request_dmi", &TransportProbe::request_dmi, lv::params("request"),
            lv::doc("Request DMI with the request's command and address"))
    .method("dmi_read", &TransportProbe::dmi_read, lv::params("addr", "size"),
            lv::doc("Read through the latest DMI grant's pointer"))
    .method("dmi_write", &TransportProbe::dmi_write, lv::params("addr", "data"),
            lv::doc("Write through the latest DMI grant's pointer"));

}  // namespace

}  // namespace dbg
