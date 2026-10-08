/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <toml++/toml.h>

#include <cstdint>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace lv::stats {

using Integer = int64_t;
using Real = double;

class Expr;
class Metric;
class Array;
template <typename T>
class Formula;

namespace detail {
struct Node;
struct Statistic;
struct ArrayData;
struct ArrayElement;
Expr make_const(Integer value);
Expr make_const(Real value);
Expr add(const Expr &lhs, const Expr &rhs);
Expr sub(const Expr &lhs, const Expr &rhs);
Expr mul(const Expr &lhs, const Expr &rhs);
Expr div(const Expr &lhs, const Expr &rhs);
Expr mod(const Expr &lhs, const Expr &rhs);
}  // namespace detail

// A read-only value handle. Copies share live data, not a numeric snapshot.
// The consumer selects integer or real arithmetic throughout the expression.
class Expr final {
 public:
  Expr() = default;
  operator Integer() const;
  operator Real() const;
  std::string expr() const;

 private:
  explicit Expr(std::shared_ptr<detail::Node> node);
  friend struct detail::Node;
  friend class Group;
  friend class Array;
  friend class Metric;
  template <typename T>
  friend class Formula;
  friend Expr operator-(const Expr &value);
  friend Expr detail::make_const(Integer value);
  friend Expr detail::make_const(Real value);
  friend Expr detail::add(const Expr &lhs, const Expr &rhs);
  friend Expr detail::sub(const Expr &lhs, const Expr &rhs);
  friend Expr detail::mul(const Expr &lhs, const Expr &rhs);
  friend Expr detail::div(const Expr &lhs, const Expr &rhs);
  friend Expr detail::mod(const Expr &lhs, const Expr &rhs);

  std::shared_ptr<detail::Node> node_;
  // A named lookup prints its qualified name while native formulas expand.
  std::string label_;
};

// A registered statistic and its storage. Groups retain the same storage even
// when the native member that registered it has been destroyed.
class MetricBase {
 public:
  virtual ~MetricBase() = default;
  toml::table tabularize() const;
  void reset();
  const std::string &name() const;
  const std::string &desc() const;

 protected:
  explicit MetricBase(std::shared_ptr<detail::Statistic> data);
  // Registration fixes storage identity; only the stored values may change.
  const std::shared_ptr<detail::Statistic> data_;

 private:
  friend class Group;
};

// A group is also a value handle; copying it retains its statistics hierarchy.
class Group {
 public:
  using Integer = lv::stats::Integer;
  using Real = lv::stats::Real;
  using Array = lv::stats::Array;
  using Metric = lv::stats::Metric;
  template <typename T>
  using Formula = lv::stats::Formula<T>;

  explicit Group(const char *name);
  Expr get(const std::string &name) const;

  template <typename T>
  Expr add_formula(const char *name, const char *desc, const Expr &expression);

  // Retain the child's data, without owning its native object or module.
  void add_child(MetricBase *item);
  void add_sub_group(Group *sub);
  // Existing parent entries retain the name used when they were registered.
  void set_name(const char *name);
  const std::string &name() const;
  toml::table tabularize() const;
  std::string dump_toml() const;
  std::string to_string() const { return dump_toml(); }
  void reset();

 private:
  struct Data;
  std::shared_ptr<Data> data_;
  void validate_child_name(const std::string &name) const;
  toml::table contents() const;
};

// Integer mutation, numeric reads and expression views share the same storage.
class Writable {
 public:
  virtual ~Writable() = default;
  virtual operator Expr() const = 0;
  virtual operator Integer() const = 0;
  operator Real() const {
    return static_cast<Real>(static_cast<Integer>(*this));
  }
  virtual void incr(Integer value) = 0;
  virtual void decr(Integer value) = 0;
  virtual void set(Integer value) = 0;

  Writable &operator++() {
    incr(1);
    return *this;
  }
  Writable &operator--() {
    decr(1);
    return *this;
  }
  Integer operator++(int) {
    Integer old = static_cast<Integer>(*this);
    incr(1);
    return old;
  }
  Integer operator--(int) {
    Integer old = static_cast<Integer>(*this);
    decr(1);
    return old;
  }
  Writable &operator+=(Integer value) {
    incr(value);
    return *this;
  }
  Writable &operator-=(Integer value) {
    decr(value);
    return *this;
  }
  Writable &operator=(Integer value) {
    set(value);
    return *this;
  }

 protected:
  Writable() = default;
  Writable(const Writable &) = default;
  Writable &operator=(const Writable &) = default;
};

class Metric : public MetricBase, public Writable {
 public:
  Metric(Group *parent, const char *name, const char *desc);
  operator Expr() const override;
  operator Integer() const override;
  void incr(Integer value) override;
  void decr(Integer value) override;
  void set(Integer value) override;
};

class Array : public MetricBase {
 public:
  class Element : public Writable {
   public:
    Element(Array *parent, uint32_t index);
    operator Expr() const override;
    operator Integer() const override;
    void incr(Integer value) override;
    void decr(Integer value) override;
    void set(Integer value) override;

   private:
    // An element stays bound to its array and index.
    const std::shared_ptr<detail::ArrayElement> element_;
  };

  Array(Group *parent, const char *name, const char *desc, uint32_t n);
  void incr(uint32_t i, Integer value);
  void decr(uint32_t i, Integer value);
  void set(uint32_t i, Integer value);
  void reset(uint32_t i);
  using MetricBase::reset;
  Element &operator[](uint32_t i) { return elements_[i]; }
  const Element &operator[](uint32_t i) const { return elements_[i]; }
  Expr sum() const { return sum_; }
  Expr avg() const { return avg_; }

 private:
  Expr sum_;
  Expr avg_;
  std::vector<Element> elements_;
};

Expr operator-(const Expr &value);

// Prefer expression construction over the built-in numeric unary operators.
template <typename T,
          typename = std::enable_if_t<std::is_convertible_v<const T &, Expr> &&
                                      !std::is_same_v<T, Expr>>>
Expr operator-(const T &value) {
  return -static_cast<Expr>(value);
}

namespace detail {
template <typename T>
Expr wrap_expr(T &&value) {
  using U = std::decay_t<T>;
  if constexpr (std::is_convertible_v<const U &, Expr>) {
    return value;
  } else if constexpr (std::is_floating_point_v<U>) {
    return make_const(static_cast<Real>(value));
  } else {
    return make_const(static_cast<Integer>(value));
  }
}

template <typename T1, typename T2>
using is_stat_op_applicable =
    std::enable_if_t<(std::is_convertible_v<T1, Expr> ||
                      (std::is_arithmetic_v<std::decay_t<T1>> &&
                       !std::is_same_v<std::decay_t<T1>, bool>)) &&
                     (std::is_convertible_v<T2, Expr> ||
                      (std::is_arithmetic_v<std::decay_t<T2>> &&
                       !std::is_same_v<std::decay_t<T2>, bool>)) &&
                     (std::is_convertible_v<T1, Expr> ||
                      std::is_convertible_v<T2, Expr>)>;
}  // namespace detail

template <typename T1, typename T2,
          typename = detail::is_stat_op_applicable<T1, T2>>
Expr operator+(T1 &&lhs, T2 &&rhs) {
  return detail::add(detail::wrap_expr(std::forward<T1>(lhs)),
                     detail::wrap_expr(std::forward<T2>(rhs)));
}
template <typename T1, typename T2,
          typename = detail::is_stat_op_applicable<T1, T2>>
Expr operator-(T1 &&lhs, T2 &&rhs) {
  return detail::sub(detail::wrap_expr(std::forward<T1>(lhs)),
                     detail::wrap_expr(std::forward<T2>(rhs)));
}
template <typename T1, typename T2,
          typename = detail::is_stat_op_applicable<T1, T2>>
Expr operator*(T1 &&lhs, T2 &&rhs) {
  return detail::mul(detail::wrap_expr(std::forward<T1>(lhs)),
                     detail::wrap_expr(std::forward<T2>(rhs)));
}
template <typename T1, typename T2,
          typename = detail::is_stat_op_applicable<T1, T2>>
Expr operator/(T1 &&lhs, T2 &&rhs) {
  return detail::div(detail::wrap_expr(std::forward<T1>(lhs)),
                     detail::wrap_expr(std::forward<T2>(rhs)));
}
template <typename T1, typename T2,
          typename = std::enable_if_t<(std::is_convertible_v<T1, Expr> ||
                                       std::is_integral_v<std::decay_t<T1>>) &&
                                      (std::is_convertible_v<T2, Expr> ||
                                       std::is_integral_v<std::decay_t<T2>>) &&
                                      (std::is_convertible_v<T1, Expr> ||
                                       std::is_convertible_v<T2, Expr>)>>
Expr operator%(T1 &&lhs, T2 &&rhs) {
  return detail::mod(detail::wrap_expr(std::forward<T1>(lhs)),
                     detail::wrap_expr(std::forward<T2>(rhs)));
}

template <typename T>
class Formula : public MetricBase {
  static_assert(std::is_same_v<T, Integer> || std::is_same_v<T, Real>,
                "Formula only supports lv::stats::Integer or lv::stats::Real");

 public:
  Formula(Group *parent, const char *name, const char *desc);
  operator Expr() const;
  operator Integer() const;
  operator Real() const;
  Formula &operator=(const Expr &expression);
  // Assignment changes the formula's expression, never its registered identity.
  Formula &operator=(const Formula &other) {
    return *this = static_cast<Expr>(other);
  }
};

template <typename T>
Expr Group::add_formula(const char *name, const char *desc,
                        const Expr &expression) {
  Formula<T> formula(this, name, desc);
  formula = expression;
  return get(name);
}

#define LV_STAT(var, ...) var(this, #var, ##__VA_ARGS__)

}  // namespace lv::stats
