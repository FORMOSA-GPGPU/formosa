// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

// Protects generic_parser's enum contract: a provided value must be one of the
// declared names, and only an absent field keeps the default. No lv binding
// declares an enum field, so the parser is driven directly from C++.

#include <liblv/log.h>
#include <liblv/schema.h>
#include <systemc.h>

#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

enum class Color { kRed, kBlue };

struct Param {
  Color color = Color::kBlue;

  LV_SCHEMA_SHARED(test, Param,
                   LV_FIELD_ENUM(color, "Color",
                                 {
                                     {Color::kRed, "Red"},
                                     {Color::kBlue, "Blue"},
                                 }))
};

void Check(bool condition, const std::string &message) {
  if (!condition) throw std::runtime_error(message);
}

template <typename T>
void CheckRejected(sol::state &lua, const T &value, const std::string &label) {
  sol::table table = lua.create_table();
  table["color"] = value;
  try {
    lv::generic_parser<Param>(table);
  } catch (const lv::fatal_error &) {
    return;
  }
  throw std::runtime_error(label + " must be rejected");
}

}  // namespace

int sc_main(int, char *[]) {
  try {
    sol::state lua;

    CheckRejected(lua, "Green", "unknown name");
    CheckRejected(lua, "red", "name with different case");
    CheckRejected(lua, 1, "number");

    Check(lv::generic_parser<Param>(lua.create_table()).color == Color::kBlue,
          "absent field must keep the default");

    sol::table table = lua.create_table();
    table["color"] = "Red";
    Check(lv::generic_parser<Param>(table).color == Color::kRed,
          "declared name must be parsed");

    std::cout << "Pass!\n";
  } catch (const std::exception &error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
  return 0;
}
