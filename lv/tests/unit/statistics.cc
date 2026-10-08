// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include <liblv/binding.h>
#include <liblv/log.h>
#include <liblv/statistics.h>
#include <systemc.h>

#include <exception>
#include <iostream>
#include <optional>
#include <string>
#include <type_traits>

namespace {
using lv::stats::Array;
using lv::stats::Expr;
using lv::stats::Formula;
using lv::stats::Group;
using lv::stats::Integer;
using lv::stats::Metric;
using lv::stats::Real;

// Registered statistics expose expression values, never a rebindable base.
static_assert(!std::is_convertible_v<Formula<Integer> &, Expr &>);
static_assert(!std::is_convertible_v<Metric &, Expr &>);
static_assert(!std::is_convertible_v<Array::Element &, Expr &>);
static_assert(!std::is_assignable_v<Array::Element &, const Array::Element &>);
static_assert(!std::is_assignable_v<lv::stats::MetricBase &,
                                    const lv::stats::MetricBase &>);
static_assert(
    !std::is_assignable_v<lv::stats::Writable &, const lv::stats::Writable &>);
static_assert(std::is_convertible_v<const Formula<Integer> &, Expr>);
static_assert(std::is_convertible_v<const Metric &, Expr>);
static_assert(std::is_convertible_v<const Array::Element &, Expr>);

template <typename Action>
void expect_fatal(Action action, const std::string &message) {
  try {
    action();
  } catch (const lv::fatal_error &) {
    return;
  }
  LV_FATAL("Expected fatal error: {}", message);
}

// Native construction and renaming use the same registration rules as Lua.
void check_group_names() {
  Group group("summary");
  Metric count(&group, "count", "Request count");
  count.set(7);
  const auto original = group.dump_toml();
  expect_fatal(
      [&] {
        Metric duplicate(&group, "count", "Duplicate");
      },
      "Duplicate statistic");
  expect_fatal(
      [&] {
        Array duplicate(&group, "count", "Duplicate", 2);
      },
      "Duplicate statistic");
  expect_fatal(
      [&] {
        Formula<Integer> duplicate(&group, "count", "Duplicate");
      },
      "Duplicate statistic");
  expect_fatal(
      [&] {
        Metric unnamed(&group, "", "Empty name");
      },
      "must not be empty");
  expect_fatal(
      [&] {
        group.add_formula<Integer>(nullptr, "Null name", count);
      },
      "must not be empty");
  expect_fatal(
      [&] {
        group.add_child(nullptr);
      },
      "Null statistic");
  expect_fatal(
      [&] {
        group.add_sub_group(nullptr);
      },
      "Null statistics group");
  Group collision("count");
  expect_fatal(
      [&] {
        group.add_sub_group(&collision);
      },
      "Duplicate statistic");
  if (group.dump_toml() != original)
    LV_FATAL("Rejected registrations changed the statistics");

  Group child("child");
  child.set_name("details");
  Metric detail(&child, "value", "Child value");
  detail.set(3);
  group.add_sub_group(&child);
  const auto with_child = group.dump_toml();
  expect_fatal(
      [&] {
        Metric duplicate(&group, "details", "Duplicate");
      },
      "Duplicate statistics group");
  Group duplicate_child("details");
  expect_fatal(
      [&] {
        group.add_sub_group(&duplicate_child);
      },
      "Duplicate statistics group");
  expect_fatal(
      [] {
        Group unnamed("");
      },
      "must not be empty");
  expect_fatal(
      [] {
        Group unnamed(nullptr);
      },
      "must not be empty");
  expect_fatal(
      [&] {
        group.set_name("");
      },
      "must not be empty");
  expect_fatal(
      [&] {
        group.set_name(nullptr);
      },
      "must not be empty");

  // Parent registration keys are stable without locking the child's name.
  child.set_name("count");
  if (child.name() != "count" || group.dump_toml() != with_child) {
    LV_FATAL("Rename changed a parent's registered key");
  }
  Group grandchild("grandchild");
  child.add_sub_group(&grandchild);
  expect_fatal(
      [&] {
        grandchild.add_sub_group(&group);
      },
      "Cyclic statistics group");
  group.add_sub_group(&group);  // Existing self-attachment remains a no-op.
}

void check_descriptions() {
  Group group("descriptions");
  Metric count(&group, "count", nullptr);
  count.set(7);
  group.add_formula<Integer>("integer", nullptr, count);
  group.add_formula<lv::stats::Real>("real", nullptr, count);

  const auto table = group.tabularize();
  for (const char *name : {"count", "integer", "real"}) {
    if (table["descriptions"][name]["desc"].value<std::string>() != "") {
      LV_FATAL("Null description was not stored as an empty string");
    }
    if (table["descriptions"][name]["val"].value<Integer>() != 7) {
      LV_FATAL("Null description changed the statistic value");
    }
  }
}

void check_scalar_lookup() {
  auto &lua = lv::Runtime();
  {
    Group group("source");
    Array lanes(&group, "lanes", "Per-lane counters", 2);
    Group child("child");
    group.add_sub_group(&child);
    Metric reset_count(&group, "reset", "Reset count");
    reset_count.set(3);
    lua["test_stats"] = group;
  }
  // A copied Group owns the data, including arrays and subgroups, after the
  // native handles leave scope. Lookup and methods have separate namespaces.
  lua.script(R"(
    local source = test_stats
    local function expect_error(name)
      local ok, err = pcall(function() return source:get(name) end)
      assert(not ok, name .. " lookup unexpectedly succeeded")
      assert(tostring(err):find("std::exception", 1, true), tostring(err))
    end
    expect_error("missing")
    expect_error("lanes")
    expect_error("child")
    assert(type(source.reset) == "function")
    source:add_integer_formula("reset_alias", "Alias", source:get("reset"))
  )");
  auto retained = lua["test_stats"].get<Group>();
  if (static_cast<Integer>(retained.get("reset_alias")) != 3) {
    LV_FATAL("Group did not retain its registered data");
  }
  retained.reset();
  if (static_cast<Integer>(retained.get("reset_alias")) != 0) {
    LV_FATAL("Retained data did not reset");
  }
  lua["test_stats"] = sol::nil;
}

void check_expression_ownership() {
  // Observe release without exposing storage inspection in the public API.
  struct TrackedFormula : Formula<Integer> {
    using Formula<Integer>::Formula;
    using Formula<Integer>::operator=;
    std::weak_ptr<void> storage() const { return data_; }
  };
  std::weak_ptr<void> formula_storage;
  {
    Group group("release");
    Metric count(&group, "count", "");
    TrackedFormula f(&group, "f", "");
    f = count + 1;
    formula_storage = f.storage();
    Expr handle = f;
    handle = Expr{};
    expect_fatal(
        [&] {
          f = group.get("f");
        },
        "Self-reference after handle rebind");
  }
  if (!formula_storage.expired()) LV_FATAL("Formula storage leaked");

  Expr retained;
  std::optional<Array::Element> element;
  {
    Group group("source");
    Metric count(&group, "count", "Request count");
    count.set(7);
    auto reference = group.get("count");
    auto formula = group.add_formula<Integer>("alias", "Alias", reference);
    group.reset();
    count.set(9);
    for (int i = 0; i < 100; ++i) {
      if (static_cast<Integer>(group.get("count")) != 9 ||
          static_cast<Integer>(formula) != 9) {
        LV_FATAL("Lookups did not share live counter data");
      }
    }
    group.set_name("renamed");
    if (reference.expr() != "source.count" ||
        group.get("count").expr() != "renamed.count") {
      LV_FATAL("Lookup labels changed behind existing expressions");
    }
    Group other("renamed");
    Metric other_count(&other, "count", "Other count");
    other_count.set(12);
    if (static_cast<Integer>(group.get("count")) != 9)
      LV_FATAL("Distinct groups shared data");

    Formula<Integer> f(&group, "f", "Mutable formula");
    Formula<Integer> g(&group, "g", "Dependent formula");
    Formula<Real> ratio(&group, "ratio", "Consumer arithmetic");
    f = count + 1;
    Expr detached = f;
    detached = count;
    detached = Expr{};
    if (static_cast<Integer>(f) != 10 ||
        static_cast<Integer>(group.get("f")) != 10) {
      LV_FATAL("Rebinding an expression changed a registered formula");
    }
    g = f * 2;
    auto f_reference = group.get("f");
    f = count + 2;
    if (static_cast<Integer>(g) != 22 ||
        static_cast<Integer>(f_reference) != 11) {
      LV_FATAL("Formula reassignment replaced its identity");
    }
    // Same-type assignment must also preserve identity and follow later edits.
    Formula<Integer> alias(&group, "formula_alias", "Formula alias");
    alias = f;
    f = count / 2;
    ratio = alias;
    if (static_cast<Integer>(g) != 8 || static_cast<Integer>(alias) != 4 ||
        static_cast<Real>(ratio) != 4.5)
      LV_FATAL("Formula alias changed arithmetic or identity");
    expect_fatal(
        [&] {
          f = g + 1;
        },
        "Cyclic statistic formula");
    expect_fatal(
        [&] {
          f = f + 1;
        },
        "Cyclic statistic formula");
    expect_fatal(
        [&] {
          f = f;
        },
        "Cyclic statistic formula");
    expect_fatal(
        [&] {
          f = group.get("f");
        },
        "Cyclic statistic formula via lookup");
    if (static_cast<Integer>(f_reference) != 4)
      LV_FATAL("Rejected assignment changed formula");

    Array lanes(&group, "lanes", "Lanes", 2);
    lanes.set(0, -7);
    lanes[1].set(2);
    if (static_cast<Integer>(lanes.avg()) != -2 ||
        static_cast<Real>(lanes.avg()) != -2.5) {
      LV_FATAL("Array average changed arithmetic");
    }
    // Native scalar operands must build expressions, including unary minus.
    Expr negative_metric = -count;
    Expr negative_formula = -f;
    Expr negative_element = -lanes[1];
    Expr remainder = count % 4;
    count.set(11);
    if (static_cast<Integer>(negative_metric) != -11 ||
        static_cast<Integer>(negative_formula) != -5 ||
        static_cast<Integer>(negative_element) != -2 ||
        static_cast<Integer>(remainder) != 3) {
      LV_FATAL("Native arithmetic captured a snapshot or changed arithmetic");
    }
    count.set(9);

    // Both direct Element construction and Array writes retain the old no-op
    // behavior for invalid indices. Test an empty array and the upper boundary.
    for (uint32_t size : {0u, 2u}) {
      Group invalid_group("bounds");
      Array bounded(&invalid_group, "values", "", size);
      const auto original = invalid_group.dump_toml();
      Array::Element invalid(&bounded, size);
      invalid.set(1);
      invalid.incr(1);
      invalid.decr(1);
      bounded.set(size, 1);
      bounded.incr(size, 1);
      bounded.decr(size, 1);
      if (invalid_group.dump_toml() != original) {
        LV_FATAL("Out-of-bounds writes changed array data");
      }
    }
    element.emplace(&lanes, 1);
    retained = lanes.sum() + lanes.avg() + lanes[1] + g;
    if (static_cast<Integer>(retained) != 3)
      LV_FATAL("Array expression is incorrect");
  }
  if (static_cast<Integer>(retained) != 3)
    LV_FATAL("Expression lost its source storage");
  element->set(4);
  if (static_cast<Integer>(retained) != 8)
    LV_FATAL("Array element lost shared storage");
}
}  // namespace

int sc_main(int, char *[]) {
  try {
    check_group_names();
    check_descriptions();
    check_scalar_lookup();
    check_expression_ownership();
    std::cout << "Pass!\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
