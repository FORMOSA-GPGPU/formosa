// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include <liblv/binding.h>
#include <liblv/common/annotation.h>
#include <liblv/log.h>
#include <liblv/schema.h>
#include <systemc.h>
#include <tlm.h>
#include <tlm_utils/simple_target_socket.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "lv/tests/unit/tlm_endpoints.h"

namespace dbg {

namespace {

using tlm_test::Initiator;
using tlm_test::Payload;
using tlm_test::Target;
using tlm_test::Transaction;

/** @brief What a PassThroughTester call found. */
struct PassThroughResult {
  bool ok = true;
  /** @brief Why the call failed; empty when ok. */
  std::string reason;
  /** @brief Sync status the initiator got: accepted, updated or completed. */
  std::string status;
  /** @brief Annotated delay the initiator got back, in ns. */
  double delay_ns = 0;
  /** @brief Whether the payload ended with an OK response. */
  bool response_ok = false;

  /** @brief Record a failure unless @p condition; the first reason stays. */
  void Expect(bool condition, const std::string &failure) {
    if (condition || !ok) return;
    ok = false;
    reason = failure;
  }
};

const char *StatusName(tlm::tlm_sync_enum status) {
  switch (status) {
    case tlm::TLM_ACCEPTED:
      return "accepted";
    case tlm::TLM_UPDATED:
      return "updated";
    case tlm::TLM_COMPLETED:
      return "completed";
  }
  return "unknown";
}

sc_time Ns(double ns) { return sc_time(ns, SC_NS); }
double ToNs(const sc_time &time) { return time.to_seconds() * 1e9; }

/**
 * @brief Tests a component built on dbg::PassThrough, such as dbg.Monitor or
 * dbg.AccessWatcher. A test-only binding, built into lv from lv/tests.
 *
 * A script builds the component through its own binding and puts it between
 * the tester's initiator and target. Each call sends one transaction along the
 * path that the target's mode selects and checks, in C++, that the component
 * passed it on as dbg::PassThrough promises: the same payload reaches the
 * target once, statuses, phases and annotated delays cross unchanged in both
 * directions, no call or return moves the transaction's effective time back,
 * b_transport takes no simulated time, and a DMI request is refused before it
 * reaches the target. Phases stay inside the tester; the
 * script gets a PassThroughResult.
 */
class PassThroughTester : public sc_module {
 public:
  using TargetSocket = Initiator::TargetSocket;

  struct Range {
    uint64_t addr = 0;
    uint64_t size = 0;
    // clang-format off
    LV_SCHEMA(PassThroughTester, Range,
              LV_FIELD(addr, "First address of the range"),
              LV_FIELD(size, "Size of the range in bytes"))
    // clang-format on
  };

  struct Param {
    std::string mode = "complete";
    double response_latency_ns = 0;
    double added_delay_ns = 0;
    std::vector<Range> failing;
    // clang-format off
    LV_SCHEMA(PassThroughTester, Param,
              LV_FIELD(mode,
                       "Path the target answers on: complete returns "
                       "TLM_COMPLETED for BEGIN_REQ, update returns "
                       "TLM_UPDATED with BEGIN_RESP, respond_later returns "
                       "TLM_ACCEPTED and sends BEGIN_RESP later"),
              LV_FIELD(response_latency_ns,
                       "respond_later: delay before BEGIN_RESP, in ns"),
              LV_FIELD(added_delay_ns,
                       "Annotated delay the target adds, in ns"),
              LV_FIELD(failing,
                       "Ranges the target answers with an address error"))
    // clang-format on
  };

  struct Request {
    uint64_t addr = 0;
    std::vector<uint8_t> data;
    std::vector<uint8_t> byte_enables;
    unsigned int streaming_width = 0;
    std::optional<std::string> annotation;
    double delay_ns = 0;
    // clang-format off
    LV_SCHEMA(PassThroughTester, Request,
              LV_FIELD(addr, "Transaction address"),
              LV_FIELD(data, "Payload bytes"),
              LV_FIELD(byte_enables,
                       "Byte enables, which repeat over the data; empty for "
                       "none"),
              LV_FIELD(streaming_width,
                       "Streaming width; 0 for the data length"),
              LV_FIELD(annotation, "Text of an lv::Annotation on the payload"),
              LV_FIELD(delay_ns, "Annotated delay the initiator sends, in ns"))
    // clang-format on
  };

  PassThroughTester(const sc_module_name &name, const Param &param)
      : sc_module(name),
        initiator_("initiator"),
        target_("target", TargetOptions(param)) {}

  tlm_utils::simple_target_socket<Target> *from() { return &target_.socket; }
  TargetSocket *to() const { return to_; }
  void set_to(TargetSocket *target) {
    to_ = target;
    initiator_.socket.bind(*to_);
  }

  PassThroughResult write(const Request &request) {
    if (busy_) return Busy();
    const Scope scope(busy_);
    return NonBlocking(tlm::TLM_WRITE_COMMAND, request);
  }

  PassThroughResult read(const Request &request) {
    if (busy_) return Busy();
    const Scope scope(busy_);
    return NonBlocking(tlm::TLM_READ_COMMAND, request);
  }

  PassThroughResult write_blocking(const Request &request) {
    if (busy_) return Busy();
    const Scope scope(busy_);
    PassThroughResult result;
    const auto transaction = Make(tlm::TLM_WRITE_COMMAND, request);
    Payload &payload = transaction->payload();
    const int calls = target_.blocking_calls();
    const sc_time sent = Ns(request.delay_ns);
    const sc_time before = sc_time_stamp();
    sc_time delay = sent;
    initiator_.SendBlocking(payload, delay);
    result.status = "completed";
    result.delay_ns = ToNs(delay);
    result.response_ok = payload.is_response_ok();
    result.Expect(target_.blocking_calls() == calls + 1,
                  "the target did not get the b_transport call once");
    result.Expect(target_.last_payload() == &payload,
                  "the target got another payload");
    result.Expect(target_.received_delay() == sent,
                  "the call's delay changed on its way");
    result.Expect(delay == target_.returned_delay(),
                  "the target's delay changed on its way back");
    result.Expect(sc_time_stamp() == before, "b_transport took simulated time");
    ExpectNoViolations(result);
    return result;
  }

  PassThroughResult request_dmi(const Request &request) {
    if (busy_) return Busy();
    const Scope scope(busy_);
    PassThroughResult result;
    const auto transaction = Make(tlm::TLM_READ_COMMAND, request);
    const int requests = target_.dmi_requests();
    result.Expect(!initiator_.RequestDmi(transaction->payload()),
                  "a DMI request was granted");
    result.Expect(target_.dmi_requests() == requests,
                  "a DMI request reached the target");
    ExpectNoViolations(result);
    return result;
  }

  PassThroughResult debug(const Request &request) {
    if (busy_) return Busy();
    const Scope scope(busy_);
    PassThroughResult result;
    const auto transaction = Make(tlm::TLM_READ_COMMAND, request);
    Payload &payload = transaction->payload();
    const int calls = target_.debug_calls();
    const unsigned int count = initiator_.Debug(payload);
    result.Expect(target_.debug_calls() == calls + 1,
                  "the target did not get the debug call once");
    result.Expect(target_.last_payload() == &payload,
                  "the target got another payload");
    result.Expect(count == payload.get_data_length(),
                  "the debug byte count changed on its way back");
    ExpectNoViolations(result);
    return result;
  }

  /**
   * @brief Check the responses the target has sent since the last check.
   *
   * Each BEGIN_RESP must reach the initiator once with its delay, and the
   * initiator's TLM_COMPLETED must reach the target. A transaction still
   * waiting for its response fails the check, as does a check made while a
   * response is still crossing the component, which leaves it in place.
   */
  PassThroughResult check_responses() {
    if (busy_) return Busy();
    const Scope scope(busy_);
    PassThroughResult result;
    // Until the BEGIN_RESP call returns, the transaction is still in use.
    if (target_.responding()) {
      result.Expect(false, "a response is still crossing the component");
      return result;
    }
    const int sent = target_.responses_sent();
    result.Expect(initiator_.responses() == sent,
                  "a BEGIN_RESP did not reach the initiator once");
    if (sent > checked_responses_) {
      result.Expect(initiator_.response_delay() == target_.response_delay(),
                    "the response's delay changed on its way");
      result.Expect(target_.response_status() == tlm::TLM_COMPLETED,
                    "the initiator's status changed on its way back");
      if (pending_) {
        result.response_ok = pending_->payload().is_response_ok();
        pending_.reset();
      }
      checked_responses_ = sent;
    }
    result.Expect(!pending_, "the target has not responded yet");
    ExpectNoViolations(result);
    return result;
  }

 private:
  // Marks a tester call in progress; a callback that calls the tester again
  // gets Busy() and changes nothing.
  struct Scope {
    explicit Scope(bool &busy) : busy_(busy) { busy_ = true; }
    ~Scope() { busy_ = false; }
    bool &busy_;
  };

  static PassThroughResult Busy() {
    PassThroughResult result;
    result.Expect(false, "another tester call is still in progress");
    return result;
  }

  static Target::Options TargetOptions(const Param &param) {
    Target::Options options;
    if (param.mode == "complete") {
      options.mode = Target::Mode::kComplete;
    } else if (param.mode == "update") {
      options.mode = Target::Mode::kUpdate;
    } else if (param.mode == "respond_later") {
      options.mode = Target::Mode::kRespondLater;
    } else {
      LV_FATAL("PassThroughTester: unknown mode {}", param.mode);
    }
    options.response_latency = Ns(param.response_latency_ns);
    options.added_delay = Ns(param.added_delay_ns);
    options.response_status = [failing = param.failing](const Payload &trans) {
      for (const auto &range : failing) {
        if (trans.get_address() - range.addr < range.size) {
          return tlm::TLM_ADDRESS_ERROR_RESPONSE;
        }
      }
      return tlm::TLM_OK_RESPONSE;
    };
    return options;
  }

  static std::unique_ptr<Transaction> Make(tlm::tlm_command command,
                                           const Request &request) {
    auto transaction = std::make_unique<Transaction>(
        command, request.addr,
        std::vector<unsigned char>(request.data.begin(), request.data.end()));
    if (!request.byte_enables.empty()) {
      transaction->ByteEnables(
          {request.byte_enables.begin(), request.byte_enables.end()});
    }
    if (request.streaming_width != 0) {
      transaction->StreamingWidth(request.streaming_width);
    }
    if (request.annotation) {
      transaction->Extension(new lv::Annotation(*request.annotation));
    }
    return transaction;
  }

  PassThroughResult NonBlocking(tlm::tlm_command command,
                                const Request &request) {
    PassThroughResult result;
    if (pending_) {
      result.Expect(false, "a transaction is still waiting for its response");
      return result;
    }
    auto transaction = Make(command, request);
    Payload &payload = transaction->payload();
    const int requests = target_.requests();
    const int end_responses = target_.end_responses();
    const sc_time sent = Ns(request.delay_ns);
    sc_time delay = sent;
    const tlm::tlm_sync_enum status = initiator_.Send(payload, delay);
    result.status = StatusName(status);
    result.delay_ns = ToNs(delay);
    result.response_ok = payload.is_response_ok();
    result.Expect(target_.requests() == requests + 1,
                  "the target did not get the request once");
    result.Expect(target_.last_payload() == &payload,
                  "the target got another payload");
    result.Expect(target_.received_delay() == sent,
                  "the request's delay changed on its way");
    result.Expect(status == target_.returned_status(),
                  "the target's status changed on its way back");
    result.Expect(delay == target_.returned_delay(),
                  "the target's delay changed on its way back");
    if (status == tlm::TLM_UPDATED) {
      result.Expect(initiator_.returned_phase() == tlm::BEGIN_RESP,
                    "the target's phase changed on its way back");
      result.Expect(target_.end_responses() == end_responses + 1,
                    "the initiator's END_RESP did not reach the target once");
    }
    // The payload must outlive a response that comes later.
    if (status == tlm::TLM_ACCEPTED) pending_ = std::move(transaction);
    ExpectNoViolations(result);
    return result;
  }

  void ExpectNoViolations(PassThroughResult &result) const {
    for (const auto *violations :
         {&initiator_.violations(), &target_.violations()}) {
      result.Expect(violations->empty(),
                    violations->empty() ? "" : violations->front());
    }
  }

  Initiator initiator_;
  Target target_;
  TargetSocket *to_ = nullptr;
  std::unique_ptr<Transaction> pending_;
  int checked_responses_ = 0;
  bool busy_ = false;
};

LV_BINDING(dbg, PassThroughResult)
    .property("ok", &PassThroughResult::ok,
              lv::doc("Whether the component passed the call on unchanged"))
    .property("reason", &PassThroughResult::reason,
              lv::doc("Why the check failed; empty when ok"))
    .property("status", &PassThroughResult::status,
              lv::doc("Sync status the initiator got: accepted, updated or "
                      "completed"))
    .property("delay_ns", &PassThroughResult::delay_ns,
              lv::doc("Annotated delay the initiator got back, in ns"))
    .property("response_ok", &PassThroughResult::response_ok,
              lv::doc("Whether the payload ended with an OK response"));

LV_BINDING(dbg, PassThroughTester)
    .constructor(
        [](const char *name, const PassThroughTester::Param &param) {
          return std::make_shared<PassThroughTester>(name, param);
        },
        lv::params("name", "param"),
        lv::doc("Create endpoints that test a component built on "
                "dbg::PassThrough, such as dbg.Monitor or dbg.AccessWatcher"))
    .property("from", &PassThroughTester::from,
              lv::doc("Target socket the component's to binds to"))
    .property("to", &PassThroughTester::to, &PassThroughTester::set_to,
              lv::doc("The component's from, which the initiator binds to"))
    .method("write", &PassThroughTester::write, lv::params("request"),
            lv::doc("Send a write as BEGIN_REQ and check it crossed unchanged"))
    .method("read", &PassThroughTester::read, lv::params("request"),
            lv::doc("Send a read as BEGIN_REQ and check it crossed unchanged"))
    .method("write_blocking", &PassThroughTester::write_blocking,
            lv::params("request"),
            lv::doc("Send a write with b_transport and check it crossed "
                    "unchanged in no simulated time"))
    .method("request_dmi", &PassThroughTester::request_dmi,
            lv::params("request"),
            lv::doc("Request DMI and check it was refused before reaching the "
                    "target"))
    .method("debug", &PassThroughTester::debug, lv::params("request"),
            lv::doc("Send debug transport and check it crossed unchanged"))
    .method("check_responses", &PassThroughTester::check_responses,
            lv::doc("Check the responses sent since the last check"));

}  // namespace

}  // namespace dbg
