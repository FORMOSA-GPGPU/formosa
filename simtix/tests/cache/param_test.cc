// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include "cache/param.h"

#include <cstddef>
#include <limits>

#include "cache/cache.h"
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

SCENARIO("Cache Param parses canonical Lua field names", "[cache][param]") {
  sol::state lua;
  sol::table table = lua.create_table();
  table["cache_size_bytes"] = std::size_t{65536};
  table["mshr_entries"] = std::size_t{12};
  table["victim_buffer_entries"] = std::size_t{6};
  table["set_index_shift"] = std::size_t{2};

  const Param param = ParseParam(table);

  CHECK(param.cache_size_bytes == 65536);
  CHECK(param.mshr_entries == 12);
  CHECK(param.victim_buffer_entries == 6);
  CHECK(param.set_index_shift == 2);
}

SCENARIO("Cache rejects invalid non-cacheable regions", "[cache][param]") {
  Param param;
  SECTION("unaligned start") { param.non_cacheable_regions = {{1, 64}}; }
  SECTION("unaligned size") { param.non_cacheable_regions = {{64, 63}}; }
  SECTION("last byte overflows") {
    param.non_cacheable_regions = {
        {std::numeric_limits<uint64_t>::max() - 63, 128}};
  }
  CHECK_THROWS_AS(
      simtix::cache::Cache(sc_core::sc_gen_unique_name("cache"), param),
      lv::fatal_error);
}

SCENARIO("Cache accepts whole-line non-cacheable regions", "[cache][param]") {
  Param param;
  // Empty entries are ignored, including unaligned addresses. Overlap and
  // ordering do not change cacheability; the last address may be UINT64_MAX.
  param.non_cacheable_regions = {
      {std::numeric_limits<uint64_t>::max() - 63, 64},
      {128, 128},
      {64, 128},
      {1, 0}};
  CHECK_NOTHROW(
      simtix::cache::Cache(sc_core::sc_gen_unique_name("cache"), param));
}

int sc_main(int argc, char *argv[]) { return Catch::Session().run(argc, argv); }
