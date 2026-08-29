// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include <liblv/binding.h>
#include <systemc.h>

#include <memory>

LV_BINDING_WITH_NAME(sc, sc_signal<bool>, "signal")
    .constructor(
        [](const char *name) {
          return std::make_shared<sc_signal<bool>>(name);
        },
        lv::params("name"), lv::doc("Create a boolean SystemC signal"))
    .constructor(
        [](const char *name, bool initial_value) {
          return std::make_shared<sc_signal<bool>>(name, initial_value);
        },
        lv::params("name", "initial_value"),
        lv::doc("Create a boolean SystemC signal with an initial value"))
    .method("write", &sc_signal<bool>::write, lv::params("value"),
            lv::doc("Write a value to the signal"))
    .method("read", &sc_signal<bool>::read,
            lv::doc("Read the current signal value"));
