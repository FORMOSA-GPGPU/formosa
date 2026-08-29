// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include <liblv/binding.h>
#include <systemc.h>

#include <memory>
#include <sol/sol.hpp>

namespace dbg {

SC_MODULE(TickAgent) {
 public:
  SC_CTOR(TickAgent, sol::function cb) : cb_(cb) {
    SC_METHOD(Tick);
    sensitive << clock_i_.pos();
    dont_initialize();
  }

  void Tick() { cb_(); }

  void set_clock(sc_clock * clock) {
    clock_ = clock;
    clock_i_.bind(*clock_);
  }
  sc_clock *clock() const { return clock_; }

  void set_cb(const sol::function &cb) { cb_ = cb; }
  sol::function cb() const { return cb_; }

 private:
  sc_clock *clock_;
  sc_in<bool> SC_NAMED(clock_i_);
  sol::function cb_;
};

LV_BINDING(dbg, TickAgent)
    .constructor(
        [](const char *name, const sol::function &cb) {
          return std::make_shared<TickAgent>(name, cb);
        },
        lv::params("name", "cb"), lv::doc("Create a per-clock callback agent"))
    .property("clock", &TickAgent::clock, &TickAgent::set_clock,
              lv::doc("SystemC clock"))
    .property("cb", &TickAgent::cb, &TickAgent::set_cb,
              lv::doc("Callback invoked on each clock tick"));

}  // namespace dbg
