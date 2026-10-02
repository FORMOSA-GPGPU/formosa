// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include <ilha/cache_command.h>
#include <liblv/binding.h>
#include <liblv/output.h>
#include <systemc.h>
#include <tlm_core/tlm_1/tlm_req_rsp/tlm_1_interfaces/tlm_master_slave_ifs.h>
#include <tlm_core/tlm_1/tlm_req_rsp/tlm_channels/tlm_req_rsp_channels/tlm_req_rsp_channels.h>

#include <cstdint>
#include <memory>
#include <vector>

namespace ilha {

// Test-only command endpoint used by cache_controller.lua. Owns a
// tlm_req_rsp_channel<CacheCommand, bool> so tests can inspect decoded commands
// and control completion without a production tester API.
class CacheCommandEndpoint : public sc_module {
 public:
  using Channel = tlm::tlm_req_rsp_channel<CacheCommand, bool>;

  explicit CacheCommandEndpoint(const sc_module_name &name)
      : sc_module(name), channel_("channel") {
    gated_.inner = channel_.master_export.operator->();
    gated_.accept = &accept_;
  }

  tlm::tlm_master_if<CacheCommand, bool> *port() { return &gated_; }

  void set_accept(bool accept) { accept_ = accept; }
  bool accept() const { return accept_; }

  sol::optional<sol::table> try_take() {
    CacheCommand cmd;
    if (!channel_.get_request_export->nb_get(cmd)) {
      return sol::nullopt;
    }
    received_.push_back(cmd);
    sol::table table = lv::Runtime().create_table();
    table["addr"] = cmd.addr;
    table["size"] = cmd.size;
    table["opcode"] = cmd.opcode;
    return table;
  }

  void complete(bool ok) {
    if (!channel_.put_response_export->nb_put(ok)) {
      lv::Fatal("CacheCommandEndpoint response fifo is full");
    }
  }

  uint64_t received_count() const { return received_.size(); }

 private:
  class GatedMaster : public tlm::tlm_master_if<CacheCommand, bool> {
   public:
    tlm::tlm_master_if<CacheCommand, bool> *inner = nullptr;
    bool *accept = nullptr;

    tlm::tlm_master_if<CacheCommand, bool> &If() const { return *inner; }

    void put(const CacheCommand &cmd) override { If().put(cmd); }
    bool nb_put(const CacheCommand &cmd) override {
      if (!nb_can_put(nullptr)) {
        return false;
      }
      return If().nb_put(cmd);
    }
    bool nb_can_put(tlm::tlm_tag<CacheCommand> *tag = nullptr) const override {
      return *accept && If().nb_can_put(tag);
    }
    const sc_core::sc_event &ok_to_put(
        tlm::tlm_tag<CacheCommand> *tag) const override {
      return If().ok_to_put(tag);
    }

    bool get(tlm::tlm_tag<bool> *tag) override { return If().get(tag); }
    bool nb_get(bool &value) override { return If().nb_get(value); }
    bool nb_can_get(tlm::tlm_tag<bool> *tag) const override {
      return If().nb_can_get(tag);
    }
    const sc_core::sc_event &ok_to_get(tlm::tlm_tag<bool> *tag) const override {
      return If().ok_to_get(tag);
    }

    bool peek(tlm::tlm_tag<bool> *tag) const override { return If().peek(tag); }
    bool nb_peek(bool &value) const override { return If().nb_peek(value); }
    bool nb_can_peek(tlm::tlm_tag<bool> *tag) const override {
      return If().nb_can_peek(tag);
    }
    const sc_core::sc_event &ok_to_peek(
        tlm::tlm_tag<bool> *tag) const override {
      return If().ok_to_peek(tag);
    }
  };

  bool accept_ = true;
  Channel channel_;
  GatedMaster gated_;
  std::vector<CacheCommand> received_;
};

LV_BINDING(ilha, CacheCommandEndpoint)
    .constructor(
        [](const char *name) {
          return std::make_shared<CacheCommandEndpoint>(name);
        },
        lv::params("name"),
        lv::doc("Test-only CacheCommand bank endpoint for controller tests"))
    .property("port", &CacheCommandEndpoint::port,
              lv::doc("Master interface bound to CacheController.bank"))
    .property("accept", &CacheCommandEndpoint::accept,
              &CacheCommandEndpoint::set_accept,
              lv::doc("When false, nb_can_put reports backpressure"))
    .method("try_take", &CacheCommandEndpoint::try_take,
            lv::doc("Non-blocking take of one decoded CacheCommand"))
    .method("complete", &CacheCommandEndpoint::complete, lv::params("ok"),
            lv::doc("Put a completion response"))
    .method("received_count", &CacheCommandEndpoint::received_count,
            lv::doc("Number of commands taken by the test"));

}  // namespace ilha
