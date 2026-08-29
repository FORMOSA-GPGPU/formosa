// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include <liblv/binding.h>
#include <sysc/kernel/sc_time.h>
#include <systemc.h>

#include <memory>

LV_MODULE(sc)
    .abstract_class("Socket",
                    lv::doc("Abstract Lua-facing SystemC/TLM socket endpoint"))
    .enumeration<sc_time_unit>("time_unit", "SEC", SC_SEC, "MS", SC_MS, "US",
                               SC_US, "NS", SC_NS, "PS", SC_PS, "FS", SC_FS,
                               "AS", SC_AS, "ZS", SC_ZS, "YS", SC_YS)
    .constant("ZERO_TIME", SC_ZERO_TIME, lv::doc("Zero simulation time"))
    .function("time_stamp", &sc_time_stamp,
              lv::doc("Return the current simulation time"));

LV_BINDING_WITH_NAME(sc, sc_time, "time")
    .constructor(
        [](double t, sc_time_unit u) {
          return std::make_shared<sc_time>(t, u);
        },
        lv::params(lv::param("t"), lv::param("u")),
        lv::doc("Create a SystemC time value"))
    .set(sol::meta_function::less_than,
         sol::object(lv::Runtime(), sol::in_place, &sc_time::operator<))
    .set(sol::meta_function::less_than_or_equal_to,
         sol::object(lv::Runtime(), sol::in_place, &sc_time::operator<=))
    .set(sol::meta_function::addition,
         sol::resolve<const sc_time(const sc_time &, const sc_time &)>(
             ::operator+))
    .set(sol::meta_function::subtraction,
         sol::resolve<const sc_time(const sc_time &, const sc_time &)>(
             ::operator-))
    .set(sol::meta_function::multiplication,
         sol::overload(
             sol::resolve<const sc_time(const sc_time &, double)>(::operator*),
             sol::resolve<const sc_time(double, const sc_time &)>(::operator*)))
    .set(
        sol::meta_function::division,
        sol::overload(
            sol::resolve<double(const sc_time &, const sc_time &)>(::operator/),
            sol::resolve<const sc_time(const sc_time &, double)>(::operator/)));
