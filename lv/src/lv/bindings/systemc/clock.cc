// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include <liblv/binding.h>
#include <systemc.h>

#include <memory>

LV_BINDING_WITH_NAME(sc, sc_clock, "clock")
    .constructor(
        [](const char *name, const sc_time &period) {
          return std::make_shared<sc_clock>(name, period);
        },
        lv::params("name", "period"), lv::doc("Create a SystemC clock"))
    .constructor(
        [](const char *name, double period_v, sc_time_unit period_tu) {
          return std::make_shared<sc_clock>(name, period_v, period_tu);
        },
        lv::params(lv::param("name"), lv::param("period_v"),
                   lv::param("period_tu")),
        lv::doc("Create a SystemC clock from a numeric period and time unit"));
