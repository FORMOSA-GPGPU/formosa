// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include <liblv/binding.h>
#include <systemc.h>

namespace {

// Ports and channels remain owned by the module and platform respectively.
void BindInput(sc_in<bool> &port, sc_signal<bool> &signal) {
  port.bind(signal);
}

void BindOutput(sc_out<bool> &port, sc_signal<bool> &signal) {
  port.bind(signal);
}

}  // namespace

LV_BINDING_WITH_NAME(sc, sc_in<bool>, "BoolIn")
    .method("bind", &BindInput, lv::params("signal"),
            lv::doc("Bind a platform-owned boolean channel before sc.start"))
    .method("__call", &BindInput, lv::params("signal"),
            lv::doc("Connect this input: pin(signal) is pin:bind(signal)"));

LV_BINDING_WITH_NAME(sc, sc_out<bool>, "BoolOut")
    .method("bind", &BindOutput, lv::params("signal"),
            lv::doc("Bind a platform-owned boolean channel before sc.start"))
    .method("__call", &BindOutput, lv::params("signal"),
            lv::doc("Connect this output: pin(signal) is pin:bind(signal)"));
