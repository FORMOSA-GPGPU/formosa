// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include "lv/src/lv/bindings/nic/address_router.h"

#include <liblv/common/ip_extension.h>
#include <liblv/log.h>
#include <tlm_utils/simple_initiator_socket.h>
#include <tlm_utils/simple_target_socket.h>

#include <array>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace {
using Payload = tlm::tlm_generic_payload;
using nic::detail::AddressMapEntry;
using nic::detail::AddressRouter;

void Check(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}

struct Probe : tlm::tlm_extension<Probe> {
  Payload *original = nullptr;
  uint64_t address = 0;
  int response = 0;
  tlm::tlm_extension_base *clone() const override { return new Probe(*this); }
  void copy_from(const tlm::tlm_extension_base &other) override {
    *this = static_cast<const Probe &>(other);
  }
};

// Exercise four legal downstream paths, including shortcuts not produced by
// simple.Memory. The target checks request/response exclusion independently.
class Target : public sc_module {
 public:
  tlm_utils::simple_target_socket<Target> socket{"socket"};
  int requests = 0;
  int retained = 0;
  int retired = 0;
  Target(sc_module_name name, int mode)
      : sc_module(name),
        mode_(mode),
        events_(this, &Target::Event),
        completions_(this, &Target::Complete) {
    socket.register_nb_transport_fw(this, &Target::Forward);
    socket.register_b_transport(this, &Target::Blocking);
    socket.register_transport_dbg(this, &Target::Debug);
  }

 private:
  int mode_;
  Payload *request_ = nullptr;
  Payload *response_ = nullptr;
  sc_time request_ready_ = SC_ZERO_TIME;
  std::deque<Payload *> ready_;
  tlm_utils::peq_with_cb_and_phase<Target> events_;
  tlm_utils::peq_with_cb_and_phase<Target> completions_;
  std::unordered_map<Payload *, uint64_t> held_;

  void Retain(Payload &trans) {
    trans.acquire();
    Check(held_.emplace(&trans, trans.get_address()).second,
          "target received a payload that is still retained");
    ++retained;
  }

  void Complete(Payload &trans, const tlm::tlm_phase &phase) {
    Check(trans.get_address() == held_.at(&trans),
          "downstream address changed before target released its reference");
    if (const auto *probe = trans.get_extension<Probe>()) {
      Check(trans.get_data_ptr()[0] == mode_ + 1 &&
                (!trans.get_byte_enable_ptr() ||
                 trans.get_byte_enable_ptr()[0] == 0xff) &&
                probe->response == mode_ + 1 &&
                trans.get_extension<lv::IpExtension>()->ip ==
                    static_cast<uint64_t>(mode_ + 1),
            "downstream buffers/extensions retired with the upstream payload");
    }
    if (phase == tlm::BEGIN_RESP) {
      // Retain the payload beyond the upstream END_RESP as well as the next
      // delta, so a delayed address restoration cannot hide the regression.
      completions_.notify(trans, tlm::END_RESP, sc_time(20, SC_NS));
    } else {
      held_.erase(&trans);
      ++retired;
      trans.release();
    }
  }

  void Fill(Payload &trans, bool debug = false) {
    Check(trans.get_address() < 0x100, "target did not receive local address");
    if (auto *probe = trans.get_extension<Probe>()) {
      Check(probe != probe->original->get_extension<Probe>(),
            "downstream extension was not cloned");
      Check(probe->original != &trans &&
                probe->original->get_address() == probe->address,
            "upstream payload address changed during downstream lifetime");
      Check(trans.get_data_ptr() != probe->original->get_data_ptr(),
            "downstream data buffer was not isolated");
      if (debug) {
        Check(
            !trans.get_byte_enable_ptr() && trans.get_byte_enable_length() == 0,
            "debug copy retained timed byte enables");
      } else {
        Check(trans.get_byte_enable_ptr() !=
                      probe->original->get_byte_enable_ptr() &&
                  trans.get_byte_enable_length() == 1 &&
                  trans.get_byte_enable_ptr()[0] == 0xff,
              "downstream byte-enable buffer was not isolated");
      }
      probe->response = mode_ + 1;
      Check(trans.get_extension<lv::IpExtension>()->ip == probe->address,
            "IP extension was not forwarded");
      trans.get_extension<lv::IpExtension>()->ip = mode_ + 1;
    }
    trans.get_data_ptr()[0] = static_cast<unsigned char>(mode_ + 1);
    trans.set_response_status(tlm::TLM_OK_RESPONSE);
    trans.set_dmi_allowed(true);
  }
  void Blocking(Payload &trans, sc_time &delay) {
    Fill(trans);
    delay += sc_time(3, SC_NS);
    if (trans.has_mm()) {
      Retain(trans);
      completions_.notify(trans, tlm::BEGIN_RESP, delay + sc_time(1, SC_NS));
    }
  }
  unsigned Debug(Payload &trans) {
    Fill(trans, true);
    if (trans.has_mm()) {
      Retain(trans);
      completions_.notify(trans, tlm::BEGIN_RESP, SC_ZERO_TIME);
    }
    return trans.get_data_length();
  }
  tlm::tlm_sync_enum Forward(Payload &trans, tlm::tlm_phase &phase,
                             sc_time &delay) {
    if (phase == tlm::END_RESP) {
      Check(response_ == &trans, "END_RESP for wrong target transaction");
      Check(trans.get_address() < 0x100,
            "END_RESP address was restored too early");
      completions_.notify(trans, tlm::BEGIN_RESP, SC_ZERO_TIME);
      response_ = nullptr;
      if (!ready_.empty())
        events_.notify(*ready_.front(), tlm::END_RESP, SC_ZERO_TIME);
      return tlm::TLM_COMPLETED;
    }
    Check(phase == tlm::BEGIN_REQ && !request_ &&
              sc_time_stamp() >= request_ready_,
          "target request exclusion violated");
    ++requests;
    Retain(trans);
    if (mode_ == 0) {
      Fill(trans);
      delay += sc_time(3, SC_NS);
      request_ready_ = sc_time_stamp() + delay;
      completions_.notify(trans, tlm::BEGIN_RESP, delay + sc_time(1, SC_NS));
      return tlm::TLM_COMPLETED;
    }
    if (mode_ == 1 && !response_) {
      response_ = &trans;
      Fill(trans);
      phase = tlm::BEGIN_RESP;
      delay += sc_time(4, SC_NS);
      return tlm::TLM_UPDATED;
    }
    events_.notify(trans, tlm::BEGIN_RESP, sc_time(9, SC_NS));
    if (mode_ == 2) {
      phase = tlm::END_REQ;
      delay += sc_time(2, SC_NS);
      request_ready_ = sc_time_stamp() + delay;
      return tlm::TLM_UPDATED;
    }
    request_ = &trans;
    events_.notify(trans, tlm::END_REQ, sc_time(2, SC_NS));
    return tlm::TLM_ACCEPTED;
  }
  void Event(Payload &trans, const tlm::tlm_phase &event) {
    if (event == tlm::END_REQ) {
      Check(request_ == &trans, "wrong request completed");
      request_ = nullptr;
      if (mode_ != 2) {
        tlm::tlm_phase phase = tlm::END_REQ;
        sc_time delay = SC_ZERO_TIME;
        socket->nb_transport_bw(trans, phase, delay);
      }
    } else {
      if (event == tlm::BEGIN_RESP) ready_.push_back(&trans);
      if (response_ || ready_.empty()) return;
      auto *next = ready_.front();
      ready_.pop_front();
      response_ = next;
      Fill(*next);
      tlm::tlm_phase phase = tlm::BEGIN_RESP;
      sc_time delay = sc_time(1, SC_NS);
      socket->nb_transport_bw(*next, phase, delay);
    }
  }
};

class Master : public sc_module, public tlm::tlm_mm_interface {
 public:
  tlm_utils::simple_initiator_socket<Master> socket{"socket"};
  int completed = 0;
  int freed = 0;
  int synchronous = 0;
  static constexpr int count = 32;
  Master(sc_module_name name, int mode)
      : sc_module(name), mode_(mode), events_(this, &Master::Event) {
    socket.register_nb_transport_bw(this, &Master::Backward);
    SC_THREAD(Run);
  }
  void free(Payload *trans) override {
    ++freed;
    // Model an initiator retiring its buffers/extensions at completion. A
    // retained downstream copy must remain independent of this storage.
    trans->get_data_ptr()[0] = 0xee;
    trans->get_byte_enable_ptr()[0] = 0;
    trans->get_extension<Probe>()->response = -1;
    trans->get_extension<lv::IpExtension>()->ip = 0xdead;
  }
  void CheckManagedSynchronous();

 private:
  struct Request {
    Payload payload;
    unsigned char byte = 0;
    unsigned char enable = 0xff;
    std::unique_ptr<lv::IpExtension> ip = std::make_unique<lv::IpExtension>();
    uint64_t address = 0;
    sc_time issued;
    explicit Request(tlm::tlm_mm_interface *mm) : payload(mm) {
      payload.set_data_ptr(&byte);
      payload.set_data_length(1);
      payload.set_streaming_width(1);
      payload.set_byte_enable_ptr(&enable);
      payload.set_byte_enable_length(1);
      auto *probe = new Probe;
      probe->original = &payload;
      payload.set_extension(probe);
      payload.set_extension(ip.get());
    }
    ~Request() { payload.clear_extension<lv::IpExtension>(); }
  };
  int mode_;
  std::vector<std::unique_ptr<Request>> requests_;
  sc_event acknowledged_;
  bool response_active_ = false;
  sc_time response_ready_ = SC_ZERO_TIME;
  tlm_utils::peq_with_cb_and_phase<Master> events_;

  void Run() {
    for (int n = 0; n < count; ++n) {
      auto request = std::make_unique<Request>(this);
      request->address = (n % 4 + 1) * 0x1000 + n;
      request->issued = sc_time_stamp();
      auto &trans = request->payload;
      trans.acquire();
      trans.set_address(request->address);
      trans.get_extension<Probe>()->address = request->address;
      request->ip->ip = request->address;
      trans.set_command(tlm::TLM_READ_COMMAND);
      requests_.push_back(std::move(request));
      tlm::tlm_phase phase = tlm::BEGIN_REQ;
      sc_time delay(2, SC_NS);
      Check(socket->nb_transport_fw(trans, phase, delay) == tlm::TLM_ACCEPTED,
            "expected buffered request acceptance");
      wait(acknowledged_);
    }
  }
  tlm::tlm_sync_enum Backward(Payload &trans, tlm::tlm_phase &phase,
                              sc_time &delay) {
    if (phase == tlm::END_REQ) {
      acknowledged_.notify(delay);
      return tlm::TLM_ACCEPTED;
    }
    Check(phase == tlm::BEGIN_RESP &&
              (mode_ == 0 ? !response_active_
                          : sc_time_stamp() >= response_ready_),
          "master response exclusion violated");
    response_active_ = true;
    response_ready_ = sc_time_stamp() + sc_time(5, SC_NS);
    auto it = std::find_if(requests_.begin(), requests_.end(),
                           [&](const auto &request) {
                             return &request->payload == &trans;
                           });
    Check(it != requests_.end(), "response sent to wrong master");
    const auto &request = **it;
    Check(trans.get_address() == request.address,
          "original address not restored");
    Check(trans.is_response_ok() && request.byte == request.address / 0x1000,
          "incorrect response data/status");
    Check(trans.get_extension<Probe>()->response == request.byte,
          "response extension was not copied back");
    Check(request.ip->ip == request.byte, "IP response was not copied back");
    Check(!trans.is_dmi_allowed(), "unsupported DMI advertised");
    Check(sc_time_stamp() >= request.issued + sc_time(5, SC_NS),
          "annotated delay lost");
    // Keep the response channel occupied after the callback to expose premature
    // retirement and verify the router retains ownership throughout this delay.
    events_.notify(trans, tlm::BEGIN_RESP, sc_time(4, SC_NS));
    events_.notify(trans, tlm::END_RESP, sc_time(5, SC_NS));
    if (mode_ == 0) return tlm::TLM_ACCEPTED;
    delay = sc_time(5, SC_NS);
    phase = tlm::END_RESP;
    return mode_ == 1 ? tlm::TLM_UPDATED : tlm::TLM_COMPLETED;
  }
  void Event(Payload &trans, const tlm::tlm_phase &phase_event) {
    if (phase_event == tlm::BEGIN_RESP) {
      Check(trans.get_ref_count() >= 2,
            "router released payload before END_RESP");
      return;
    }
    response_active_ = false;
    if (mode_ == 0) {
      tlm::tlm_phase phase = tlm::END_RESP;
      sc_time delay = SC_ZERO_TIME;
      socket->nb_transport_fw(trans, phase, delay);
    }
    ++completed;
    trans.release();
  }
};

void Master::CheckManagedSynchronous() {
  for (int i = 0; i < 2; ++i) {
    auto request = std::make_unique<Request>(this);
    request->address = 0x1000 + i;
    auto &trans = request->payload;
    trans.acquire();
    trans.set_address(request->address);
    trans.set_command(tlm::TLM_READ_COMMAND);
    trans.get_extension<Probe>()->address = request->address;
    request->ip->ip = request->address;
    requests_.push_back(std::move(request));
    sc_time delay = SC_ZERO_TIME;
    if (i == 0)
      socket->b_transport(trans, delay);
    else
      Check(socket->transport_dbg(trans) == 1,
            "managed debug transport failed");
    Check(trans.get_address() == 0x1000 + i && trans.is_response_ok() &&
              trans.get_data_ptr()[0] == 1 &&
              trans.get_extension<Probe>()->response == 1 &&
              trans.get_extension<lv::IpExtension>()->ip == 1 &&
              !trans.is_dmi_allowed() && trans.get_ref_count() == 1,
          "managed synchronous transport lost response or buffer ownership");
    trans.release();
    ++synchronous;
  }
}

void CheckSynchronous(Master &master) {
  Payload trans;
  std::array<unsigned char, 2> bytes{};
  auto &byte = bytes[0];
  trans.set_command(tlm::TLM_READ_COMMAND);
  trans.set_data_ptr(bytes.data());
  trans.set_data_length(1);
  trans.set_streaming_width(1);
  for (unsigned target = 1; target <= 4; ++target) {
    const auto address = target * 0x1000 + 0xff;
    trans.set_address(address);
    sc_time delay(1, SC_NS);
    master.socket->b_transport(trans, delay);
    Check(byte == target && trans.get_address() == address &&
              delay == sc_time(4, SC_NS) && !trans.is_dmi_allowed(),
          "blocking transport corrupted address, delay, or data");
    byte = 0;
    Check(master.socket->transport_dbg(trans) == 1 && byte == target &&
              trans.get_address() == address,
          "debug routing failed");
    trans.set_data_length(2);
    Check(master.socket->transport_dbg(trans) == 0 &&
              trans.get_response_status() == tlm::TLM_ADDRESS_ERROR_RESPONSE,
          "cross-region debug access accepted");
    trans.set_data_length(1);
  }
  tlm::tlm_dmi dmi;
  Check(!master.socket->get_direct_mem_ptr(trans, dmi),
        "DMI should be disabled");

  trans.set_address(UINT64_MAX);
  Check(master.socket->transport_dbg(trans) == 1 && byte == 5 &&
            trans.get_address() == UINT64_MAX,
        "last byte of 64-bit address space was not routed");
  trans.set_data_length(2);
  Check(master.socket->transport_dbg(trans) == 0 &&
            trans.get_response_status() == tlm::TLM_ADDRESS_ERROR_RESPONSE,
        "overflowing debug transfer accepted");
  sc_time delay = SC_ZERO_TIME;
  // A two-byte timed stream with width one touches only the last byte.
  master.socket->b_transport(trans, delay);
  Check(
      trans.is_response_ok() && trans.get_address() == UINT64_MAX && byte == 5,
      "valid streaming window was rejected");
  trans.set_streaming_width(2);
  master.socket->b_transport(trans, delay);
  Check(trans.get_response_status() == tlm::TLM_ADDRESS_ERROR_RESPONSE,
        "overflowing blocking transfer accepted");
  trans.set_data_length(0);
  Check(master.socket->transport_dbg(trans) == 0 &&
            trans.get_response_status() == tlm::TLM_ADDRESS_ERROR_RESPONSE,
        "zero-length transfer accepted");
}

// Compare a reused timed/debug payload over direct and translated paths. Debug
// accesses must ignore the timed transaction's byte enables and streaming
// width.
class DebugComparison : public sc_module {
 public:
  tlm_utils::simple_initiator_socket<DebugComparison> direct{"direct"};
  tlm_utils::simple_initiator_socket<DebugComparison> routed{"routed"};
  tlm_utils::simple_target_socket<DebugComparison> direct_target{
      "direct_target"};
  tlm_utils::simple_target_socket<DebugComparison> routed_target{
      "routed_target"};

  explicit DebugComparison(sc_module_name name)
      : sc_module(name),
        router_("router", 1, {{0x5000, 0x100, true}}),
        relay_("relay", 1, {{0, 0x100, true}}) {
    direct_target.register_b_transport(this, &DebugComparison::Blocking);
    direct_target.register_transport_dbg(this, &DebugComparison::Debug);
    routed_target.register_b_transport(this, &DebugComparison::Blocking);
    routed_target.register_transport_dbg(this, &DebugComparison::Debug);
    direct.bind(direct_target);
    routed.bind(router_.from);
    router_.to.bind(relay_.from);
    relay_.to.bind(routed_target);
  }

  void Run() {
    Payload trans;
    std::array<unsigned char, 2> direct_bytes{}, routed_bytes{};
    for (std::array<unsigned char, 2> enables :
         {std::array<unsigned char, 2>{0x00, 0xff}, {0x00, 0x00}}) {
      trans.set_command(tlm::TLM_READ_COMMAND);
      trans.set_data_length(2);
      trans.set_streaming_width(2);
      trans.set_byte_enable_ptr(enables.data());
      trans.set_byte_enable_length(2);
      direct_bytes.fill(0xee);
      trans.set_data_ptr(direct_bytes.data());
      trans.set_address(0x20);
      sc_time delay = SC_ZERO_TIME;
      direct->b_transport(trans, delay);
      routed_bytes.fill(0xee);
      trans.set_data_ptr(routed_bytes.data());
      trans.set_address(0x5020);
      routed->b_transport(trans, delay);
      const std::array<unsigned char, 2> expected_timed{
          0xee, static_cast<unsigned char>(enables[1] ? 0xa1 : 0xee)};
      Check(direct_bytes == expected_timed && routed_bytes == expected_timed,
            "timed reads did not honor byte enables");

      // Reuse the same payload without clearing its timed byte-enable fields.
      trans.set_streaming_width(1);
      direct_bytes.fill(0xee);
      trans.set_data_ptr(direct_bytes.data());
      trans.set_address(0x20);
      const auto direct_count = direct->transport_dbg(trans);
      routed_bytes.fill(0xee);
      trans.set_data_ptr(routed_bytes.data());
      trans.set_address(0x5020);
      const auto routed_count = routed->transport_dbg(trans);
      const std::array<unsigned char, 2> expected_debug{0xa0, 0xa1};
      Check(direct_count == 2 && direct_bytes == expected_debug,
            "direct debug read did not return both bytes");
      Check(routed_count == direct_count && routed_bytes == direct_bytes,
            "routed debug read was masked by stale timed byte enables");
      Check(trans.get_address() == 0x5020 &&
                trans.get_byte_enable_ptr() == enables.data() &&
                trans.get_byte_enable_length() == 2 &&
                trans.get_streaming_width() == 1,
            "debug transport modified the original request attributes");
    }

    // Debug callers need not keep an old timed byte-enable array alive. An
    // unreadable stale pointer must never be dereferenced while making a copy.
    trans.set_byte_enable_ptr(reinterpret_cast<unsigned char *>(uintptr_t{1}));
    trans.set_byte_enable_length(1);
    trans.set_streaming_width(0);
    direct_bytes.fill(0xee);
    trans.set_data_ptr(direct_bytes.data());
    trans.set_address(0x20);
    const auto direct_count = direct->transport_dbg(trans);
    routed_bytes.fill(0xee);
    trans.set_data_ptr(routed_bytes.data());
    trans.set_address(0x5020);
    const auto routed_count = routed->transport_dbg(trans);
    Check(direct_count == 2 && routed_count == direct_count &&
              direct_bytes == std::array<unsigned char, 2>{0xa0, 0xa1} &&
              routed_bytes == direct_bytes,
          "debug request construction used a stale byte-enable array");
  }

 private:
  AddressRouter router_, relay_;

  void Blocking(Payload &trans, sc_time &) {
    Check(trans.get_address() == 0x20, "timed address translation failed");
    for (unsigned i = 0; i < trans.get_data_length(); ++i) {
      const auto *enables = trans.get_byte_enable_ptr();
      if (!enables || enables[i % trans.get_byte_enable_length()])
        trans.get_data_ptr()[i] = static_cast<unsigned char>(0xa0 + i);
    }
    trans.set_response_status(tlm::TLM_OK_RESPONSE);
  }

  unsigned Debug(Payload &trans) {
    Check(trans.get_address() == 0x20, "debug address translation failed");
    for (unsigned i = 0; i < trans.get_data_length(); ++i)
      trans.get_data_ptr()[i] = static_cast<unsigned char>(0xa0 + i);
    return trans.get_data_length();
  }
};
}  // namespace

int sc_main(int argc, char **argv) {
  try {
    if (argc == 2) {
      std::vector<AddressMapEntry> map{{0, 0x100, true}};
      unsigned masters = 1;
      const std::string scenario = argv[1];
      if (scenario == "empty")
        map.clear();
      else if (scenario == "zero")
        map[0].size = 0;
      else if (scenario == "overflow")
        map[0].addr = UINT64_MAX;
      else if (scenario == "overlap")
        map.push_back({0xff, 2, false});
      else if (scenario == "masters")
        masters = 0;
      else
        throw std::runtime_error("unknown scenario");
      try {
        AddressRouter invalid("invalid", masters, map);
      } catch (const lv::fatal_error &) {
        return 0;
      }
      throw std::runtime_error("invalid configuration was accepted");
    }
    AddressRouter router("router", 3,
                         {{0x3000, 0x100, true},
                          {0x1000, 0x100, true},
                          {0x4000, 0x100, true},
                          {0x2000, 0x100, true},
                          {UINT64_MAX - 0xff, 0x100, true}});
    // A second routing hop must keep each hop's ownership metadata independent.
    AddressRouter relay("relay", 1, {{0, 0x100, true}});
    DebugComparison debug_comparison("debug_comparison");
    std::array<std::unique_ptr<Target>, 5> targets;
    std::array<std::unique_ptr<Master>, 3> masters;
    for (int i = 0; i < 5; ++i)
      targets[i] = std::make_unique<Target>(sc_gen_unique_name("target"), i);
    for (int target : {2, 0, 3, 1, 4}) {
      if (target == 0)
        router.to.bind(relay.from);
      else
        router.to.bind(targets[target]->socket);
    }
    relay.to.bind(targets[0]->socket);
    for (int i = 0; i < 3; ++i) {
      masters[i] = std::make_unique<Master>(sc_gen_unique_name("master"), i);
      masters[i]->socket.bind(router.from);
    }
    sc_start(SC_ZERO_TIME);
    debug_comparison.Run();
    CheckSynchronous(*masters[0]);
    masters[0]->CheckManagedSynchronous();
    sc_start(2, SC_US);
    for (const auto &master : masters) {
      Check(master->completed == Master::count, "transactions stalled/lost");
      Check(master->freed == Master::count + master->synchronous,
            "payload references leaked");
    }
    for (int i = 0; i < 5; ++i) {
      Check(targets[i]->requests == (i == 4 ? 0 : 24), "wrong target routing");
      Check(targets[i]->retired == targets[i]->retained,
            "deferred target bookkeeping did not finish");
    }
    std::cout << "Pass!\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
