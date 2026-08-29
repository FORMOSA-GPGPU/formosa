// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include <liblv/binding.h>
#include <systemc.h>

#include <csignal>
#include <cstdlib>

namespace {
sol::function exit_hook;
}

LV_MODULE(sc)
    .init([] {
      // Run the registered hook when receiving SIGINT.
      std::signal(SIGINT, [](int) {
        exit_hook();
        std::exit(0);
      });
    })
    .function(
        "start",
        [] {
          sc_start();
        },
        lv::doc("Start or resume the SystemC simulation."))
    .function(
        "start",
        [](double t, sc_time_unit u) {
          sc_start(t, u);
        },
        lv::params(lv::param("t"), lv::param("u")),
        lv::doc("Run the SystemC simulation for a duration."))
    .function(
        "start",
        [](const sc_time &t) {
          sc_start(t);
        },
        lv::params(lv::param("t")),
        lv::doc("Run the SystemC simulation for a time value."))
    .function(
        "stop",
        [] {
          sc_stop();
        },
        lv::doc("Request the SystemC simulation to stop."))
    .function(
        "pause",
        [] {
          sc_pause();
        },
        lv::doc("Pause the SystemC simulation."))
    .function(
        "exit_hook",
        [](sol::function f) {
          exit_hook = f;
        },
        lv::params(lv::param("f")),
        lv::doc("Register a Lua callback invoked on SIGINT."))
    .function(
        "reset",
        [] {
          lv::Runtime().collect_garbage();
          if (sc_curr_simcontext) {
            delete sc_curr_simcontext;
          }
          sc_curr_simcontext = nullptr;
        },
        lv::doc("Reset the SystemC simulation context."));
