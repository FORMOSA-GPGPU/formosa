// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include <liblv/binding.h>

#include <algorithm>
#include <string>

#include "cores/pipelined/lsu/base.h"

namespace simtix::pipelined {

class LsuTester : public sc_module {
 public:
  LsuTester(const sc_module_name &name, const ArchParam &param)
      : sc_module(name),
        num_lanes_(param.num_lanes),
        pool_(param, 64),
        lsu_req_(64),
        lsu_resp_(64) {
    SC_METHOD(Pause);
    sensitive << lsu_resp_.ok_to_get();
    dont_initialize();
  }

  // Lua bindings
  void lsu_init(sol::function lsu_init) { lsu_init_ = std::move(lsu_init); }

  void set_clock(sc_clock *clock) { clock_ = clock; }

  void set_target(Lsu::Target *target) {
    target_ = target;
    if (lsu_) {
      lsu_->set_target(target);
    }
  }

  void before_end_of_elaboration() override {
    lsu_ = lsu_init_("lsu");
    lsu_init_ = sol::lua_nil;
    lsu_->clock.bind(*clock_);
    lsu_->lsu_req.bind(lsu_req_);
    lsu_->lsu_resp.bind(lsu_resp_);
    if (target_) {
      lsu_->set_target(target_);
    }
  }

  template <class T>
  using LuaArr = sol::as_table_t<std::vector<T>>;

  void store(uint64_t ip, const LuaArr<uint64_t> &addr,
             const LuaArr<uint8_t> &data, size_t size, const char *tmask) {
    Packet *packet = pool_.Acquire();
    packet->wpc = ip;
    packet->tmask = tmask;
    packet->addr_buf = addr.value();
    std::memcpy(packet->data_buf.data(), data.value().data(), 8 * num_lanes_);
    packet->flag = ExecFlag::STORE | Size2Flag(size);
    lsu_req_.put(packet);
    sc_start();

    packet = lsu_resp_.get();
    pool_.Release(packet);
  }

  LuaArr<uint8_t> load(uint64_t ip, const LuaArr<uint64_t> &addr, size_t size,
                       bool is_signed, const char *tmask) {
    Packet *packet = pool_.Acquire();
    packet->wpc = ip;
    packet->tmask = tmask;
    packet->addr_buf = addr.value();
    packet->flag = ExecFlag::LOAD | Size2Flag(size) |
                   (is_signed ? ExecFlag::SIGNED : ExecFlag::NONE);
    lsu_req_.put(packet);
    sc_start();

    packet = lsu_resp_.get();
    std::vector<uint8_t> data_buf(8 * num_lanes_);
    std::memcpy(data_buf.data(), packet->data_buf.data(), 8 * num_lanes_);

    pool_.Release(packet);
    return data_buf;
  }

  uint64_t issue_load(uint64_t id, const LuaArr<uint64_t> &addr, size_t size,
                      bool is_signed, const char *tmask) {
    Packet *packet = pool_.Acquire();
    packet->wpc = id;
    packet->tmask = tmask;
    packet->addr_buf = addr.value();
    std::fill(packet->data_buf.begin(), packet->data_buf.end(), 0);
    packet->flag = ExecFlag::LOAD | Size2Flag(size) |
                   (is_signed ? ExecFlag::SIGNED : ExecFlag::NONE);
    outstanding_.push_back(packet);
    lsu_req_.put(packet);
    return id;
  }

  uint64_t issue_store(uint64_t id, const LuaArr<uint64_t> &addr,
                       const LuaArr<uint8_t> &data, size_t size,
                       const char *tmask) {
    Packet *packet = pool_.Acquire();
    packet->wpc = id;
    packet->tmask = tmask;
    packet->addr_buf = addr.value();
    std::memcpy(packet->data_buf.data(), data.value().data(), 8 * num_lanes_);
    packet->flag = ExecFlag::STORE | Size2Flag(size);
    outstanding_.push_back(packet);
    lsu_req_.put(packet);
    return id;
  }

  uint64_t issue_atomic(uint64_t id, const LuaArr<uint64_t> &addr,
                        const LuaArr<uint8_t> &data, size_t size,
                        bool is_signed, const char *tmask,
                        const std::string &op) {
    Packet *packet = pool_.Acquire();
    packet->wpc = id;
    packet->tmask = tmask;
    packet->addr_buf = addr.value();
    std::memcpy(packet->data_buf.data(), data.value().data(), 8 * num_lanes_);
    ExecFlag amo = ExecFlag::AMO_ADD;
    if (op == "swap")
      amo = ExecFlag::AMO_SWAP;
    else if (op == "xor")
      amo = ExecFlag::AMO_XOR;
    else if (op == "and")
      amo = ExecFlag::AMO_AND;
    else if (op == "or")
      amo = ExecFlag::AMO_OR;
    else if (op == "min")
      amo = ExecFlag::AMO_MIN;
    else if (op == "max")
      amo = ExecFlag::AMO_MAX;
    else if (op == "minu")
      amo = ExecFlag::AMO_MINU;
    else if (op == "maxu")
      amo = ExecFlag::AMO_MAXU;
    packet->flag = ExecFlag::ATOMIC | Size2Flag(size) | amo |
                   (is_signed ? ExecFlag::SIGNED : ExecFlag::NONE);
    outstanding_.push_back(packet);
    lsu_req_.put(packet);
    return id;
  }

  bool response_available() const { return lsu_resp_.nb_can_get(); }
  uint64_t completed_id() { return lsu_resp_.peek()->wpc; }
  LuaArr<uint8_t> collect_response() {
    Packet *packet = lsu_resp_.get();
    std::vector<uint8_t> data(8 * num_lanes_);
    std::memcpy(data.data(), packet->data_buf.data(), data.size());
    auto it = std::find(outstanding_.begin(), outstanding_.end(), packet);
    assert(it != outstanding_.end());
    outstanding_.erase(it);
    pool_.Release(packet);
    return data;
  }

  LuaArr<uint8_t> inspect_data(uint64_t id) const {
    auto it =
        std::find_if(outstanding_.begin(), outstanding_.end(), [id](Packet *p) {
          return p->wpc == id;
        });
    assert(it != outstanding_.end());
    std::vector<uint8_t> data(8 * num_lanes_);
    std::memcpy(data.data(), (*it)->data_buf.data(), data.size());
    return sol::as_table(std::move(data));
  }

 private:
  void Pause() { sc_pause(); }

  ExecFlag Size2Flag(size_t size) {
    switch (size) {
      case 1:
        return ExecFlag::WIDTH_1;
      case 2:
        return ExecFlag::WIDTH_2;
      case 4:
        return ExecFlag::WIDTH_4;
      case 8:
        return ExecFlag::WIDTH_8;
    }
    return ExecFlag::NONE;
  }

  const uint32_t num_lanes_;
  PacketPool pool_;
  tlm::tlm_fifo<Packet *> lsu_req_;
  tlm::tlm_fifo<Packet *> lsu_resp_;

  std::shared_ptr<Lsu> lsu_;
  sol::function lsu_init_;
  Lsu::Target *target_ = nullptr;
  sc_clock *clock_ = nullptr;
  std::vector<Packet *> outstanding_;
};

LV_BINDING(simtix, LsuTester)
    .constructor(
        [](const char *name, const ArchParam &param) {
          return std::make_shared<LsuTester>(name, param);
        },
        lv::params("name", "param"), lv::doc("Create an LSU tester"))
    .method("lsu_init", &LsuTester::lsu_init,
            lv::params(lv::param(
                "lsu_init", lv::lua_type("fun(name: string): simtix.Lsu"))),
            lv::doc("Register the LSU factory"))
    .property("clock", &LsuTester::set_clock, lv::doc("SystemC clock"))
    .method("load", &LsuTester::load,
            lv::params("ip", "addr", "size", "is_signed", "tmask"),
            lv::doc("Issue a load packet"))
    .method("store", &LsuTester::store,
            lv::params("ip", "addr", "data", "size", "tmask"),
            lv::doc("Issue a store packet"))
    .method("issue_load", &LsuTester::issue_load,
            lv::params("id", "addr", "size", "is_signed", "tmask"),
            lv::doc("Issue a load without waiting for completion"))
    .method("issue_store", &LsuTester::issue_store,
            lv::params("id", "addr", "data", "size", "tmask"),
            lv::doc("Issue a store without waiting for completion"))
    .method("issue_atomic", &LsuTester::issue_atomic,
            lv::params("id", "addr", "data", "size", "is_signed", "tmask",
                       "op"),
            lv::doc("Issue an atomic operation without waiting for completion"))
    .method("response_available", &LsuTester::response_available,
            lv::doc("Whether a completed packet is available"))
    .method("completed_id", &LsuTester::completed_id,
            lv::doc("Return the identifier of the next completion"))
    .method("collect_response", &LsuTester::collect_response,
            lv::doc("Collect and release the next completed packet"))
    .method(
        "inspect_data", &LsuTester::inspect_data, lv::params("id"),
        lv::doc("Inspect an outstanding packet's data without modifying it"))
    .property("target", &LsuTester::set_target, lv::doc("Memory target"));

}  // namespace simtix::pipelined
