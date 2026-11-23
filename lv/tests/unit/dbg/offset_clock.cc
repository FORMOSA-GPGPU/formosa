// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include <liblv/binding.h>
#include <systemc.h>

#include <memory>

// A test-only function, built into lv from lv/tests. sc.clock always starts
// at time 0, where a component that ignores its first edge starts at the same
// time as one that waits for it; a later first edge tells them apart.
LV_MODULE(dbg).function(
    "offset_clock",
    [](const char *name, const sc_time &period, const sc_time &start_time) {
      return std::make_shared<sc_clock>(name, period, 0.5, start_time, true);
    },
    lv::params("name", "period", "start_time"),
    lv::doc("Create a SystemC clock whose first rising edge is at start_time"));
