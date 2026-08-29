// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include "cache/param.h"

#include <cstddef>

#include "catch2/catch_session.hpp"
#include "catch2/catch_test_macros.hpp"
#include "sol/sol.hpp"
#include "systemc.h"

namespace {

using simtix::cache::Param;

Param ParseParam(const sol::table &table) {
  return lv::generic_parser<Param>(table);
}

}  // namespace

SCENARIO("Cache Param parses legacy Lua field names", "[cache][param]") {
  sol::state lua;
  sol::table table = lua.create_table();
  table["size_bytes"] = std::size_t{131072};
  table["mshrs"] = std::size_t{16};

  const Param param = ParseParam(table);

  CHECK(param.cache_size_bytes == 131072);
  CHECK(param.mshr_entries == 16);
}

SCENARIO("Cache Param parses canonical Lua field names", "[cache][param]") {
  sol::state lua;
  sol::table table = lua.create_table();
  table["cache_size_bytes"] = std::size_t{65536};
  table["mshr_entries"] = std::size_t{12};
  table["victim_buffer_entries"] = std::size_t{6};

  const Param param = ParseParam(table);

  CHECK(param.cache_size_bytes == 65536);
  CHECK(param.mshr_entries == 12);
  CHECK(param.victim_buffer_entries == 6);
}

SCENARIO("Cache Param canonical names override legacy aliases",
         "[cache][param]") {
  sol::state lua;
  sol::table table = lua.create_table();
  table["size_bytes"] = std::size_t{4096};
  table["cache_size_bytes"] = std::size_t{32768};
  table["mshrs"] = std::size_t{4};
  table["mshr_entries"] = std::size_t{10};

  const Param param = ParseParam(table);

  CHECK(param.cache_size_bytes == 32768);
  CHECK(param.mshr_entries == 10);
}

int sc_main(int argc, char *argv[]) { return Catch::Session().run(argc, argv); }
