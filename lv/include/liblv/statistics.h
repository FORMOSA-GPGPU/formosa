/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <fmt/format.h>
#include <liblv/output.h>
#include <toml++/toml.h>

#include <numeric>
#include <string>
#include <type_traits>

namespace lv {
namespace stats {

using Integer = int64_t;
using Real = double;

// Base class for anything that can be displayed in the TOML tree
// (Metrics, Arrays, Formulas, Group)
class MetricBase {
 public:
  MetricBase(const char *name, const char *desc) : name_(name), desc_(desc) {}
  virtual ~MetricBase() = default;

  virtual toml::table tabularize() const = 0;
  virtual void reset() = 0;

  const std::string &name() const { return name_; }
  const std::string &desc() const { return desc_; }

 protected:
  std::string name_;
  std::string desc_;
};

// Forward declarations
class Array;
class Metric;
template <typename T>
class Formula;

class Group {
 public:
  using Integer = lv::stats::Integer;
  using Real = lv::stats::Real;
  using Array = lv::stats::Array;
  using Metric = lv::stats::Metric;
  template <typename T>
  using Formula = lv::stats::Formula<T>;

  explicit Group(const char *name) : name_(name) {}

  void add_child(MetricBase *item) { children_.push_back(item); }
  void add_sub_group(Group *sub) {
    if (sub == nullptr) {
      lv::Fatal("Nullptr passed to add_sub_group of Group '{}'", name_);
    }
    if (sub != this) {
      sub_groups_.push_back(sub);
    }
  }

  void set_name(const char *name) { name_ = name; }
  const std::string &name() const { return name_; }

  toml::table tabularize() const {
    toml::table curr;
    // Recurse into sub-groups
    for (auto *g : sub_groups_) {
      toml::table child = g->tabularize();
      auto it = child.find(g->name());
      if (it != child.end()) {
        curr.insert(g->name(), *(it->second.as_table()));
      }
    }
    // Add leaf metrics/arrays/formulas
    for (auto c : children_) {
      curr.insert(c->name(), c->tabularize());
    }
    return toml::table{{name_, curr}};
  }

  std::string dump_toml() const {
    std::stringstream ss;
    ss << toml::toml_formatter{tabularize()};
    return ss.str();
  }

  std::string to_string() const { return dump_toml(); }

  void reset() {
    for (auto *c : children_) {
      c->reset();
    }
    for (auto *g : sub_groups_) {
      g->reset();
    }
  }

 private:
  std::string name_;
  std::vector<MetricBase *> children_;
  std::vector<Group *> sub_groups_;
};

// Interface for anything that evaluates to a number
// (Metric, Formula, Expression, Constant)
class Expr {
 public:
  virtual ~Expr() = default;
  virtual operator Integer() const = 0;
  virtual operator Real() const = 0;
  virtual std::string expr() const = 0;
};

// Interface for anything that can be modified
// (Metric, Array Element)
class Writable : public Expr {
 public:
  virtual void incr(Integer val) = 0;
  virtual void decr(Integer val) = 0;
  virtual void set(Integer val) = 0;

  // Prefix
  Writable &operator++() {
    incr(1);
    return *this;
  }
  Writable &operator--() {
    decr(1);
    return *this;
  }

  // Postfix
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

  Writable &operator+=(Integer val) {
    incr(val);
    return *this;
  }
  Writable &operator-=(Integer val) {
    decr(val);
    return *this;
  }
  Writable &operator=(Integer val) {
    set(val);
    return *this;
  }
};

// 1. The Atomic Metric (Counter/Gauge)
class Metric : public MetricBase, public Writable {
 public:
  explicit Metric(Group *parent, const char *name, const char *desc)
      : MetricBase(name, desc) {
    parent->add_child(this);
  }

  void incr(Integer val) override { val_ += val; }
  void decr(Integer val) override { val_ -= val; }
  void set(Integer val) override { val_ = val; }

  // MetricBase
  toml::table tabularize() const override {
    return toml::table{
        {"val", val_},
        {"desc", desc()},
    };
  }
  void reset() override { val_ = 0; }

  // Expr
  operator Integer() const override { return val_; }
  operator Real() const override { return static_cast<Real>(val_); }
  std::string expr() const override { return name(); }

 private:
  Integer val_ = 0;
};

// 2. The Array (Vector of Metrics)
class Array : public MetricBase {
 public:
  class Element : public Writable {
   public:
    Element(Array *parent, uint32_t index) : parent_(parent), index_(index) {}

    // Writable interface
    void incr(Integer val) override { parent_->incr(index_, val); }
    void decr(Integer val) override { parent_->decr(index_, val); }
    void set(Integer val) override { parent_->set(index_, val); }

    // Expr interface
    operator Integer() const override { return parent_->val_[index_]; }
    operator Real() const override {
      return static_cast<Real>(parent_->val_[index_]);
    }
    std::string expr() const override {
      return fmt::format("{}[{}]", parent_->name(), index_);
    }

   private:
    Array *parent_;
    uint32_t index_;
  };

  explicit Array(Group *parent, const char *name, const char *desc, uint32_t n)
      : MetricBase(name, desc), n_(n), val_(n, 0) {
    parent->add_child(this);
    elements_.reserve(n);
    for (uint32_t i = 0; i < n; ++i) {
      elements_.emplace_back(this, i);
    }
  }

  void incr(uint32_t i, Integer val) {
    if (i < n_) val_[i] += val;
  }
  void decr(uint32_t i, Integer val) {
    if (i < n_) val_[i] -= val;
  }
  void set(uint32_t i, Integer val) {
    if (i < n_) val_[i] = val;
  }
  void reset(uint32_t i) {
    if (i < n_) val_[i] = 0;
  }

  Element &operator[](uint32_t i) { return elements_[i]; }
  const Element &operator[](uint32_t i) const { return elements_[i]; }

  const Expr &sum() const { return sum_; }
  const Expr &avg() const { return avg_; }

  toml::table tabularize() const override {
    toml::array arr;
    for (auto e : val_) {
      arr.push_back(e);  // cppcheck-suppress useStlAlgorithm
    }
    return toml::table{
        {"val", arr},
        {"desc", desc()},
    };
  }
  void reset() override { std::fill(val_.begin(), val_.end(), 0); }

 private:
  // Helper classes for Aggregation
  class Reduction : public Expr {
   public:
    explicit Reduction(const Array *parent) : parent_(parent) {}

   protected:
    const Array *parent_;
  };

  class Sum : public Reduction {
   public:
    explicit Sum(const Array *parent) : Reduction(parent) {}
    operator Integer() const override {
      return std::accumulate(parent_->val_.begin(), parent_->val_.end(), 0LL);
    };
    operator Real() const override {
      return static_cast<Real>(operator Integer());
    }
    std::string expr() const override {
      return fmt::format("sum({})", parent_->name());
    }
  } sum_{this};

  class Avg : public Reduction {
   public:
    explicit Avg(const Array *parent) : Reduction(parent) {}
    operator Integer() const override {
      Integer sum =
          std::accumulate(parent_->val_.begin(), parent_->val_.end(), 0LL);
      return sum / parent_->n_;
    };
    operator Real() const override {
      Real sum =
          std::accumulate(parent_->val_.begin(), parent_->val_.end(), 0.0);
      return sum / parent_->n_;
    };
    std::string expr() const override {
      return fmt::format("avg({})", parent_->name());
    }
  } avg_{this};

  uint32_t n_;
  std::vector<Integer> val_;
  std::vector<Element> elements_;
  friend class Sum;
  friend class Avg;
};

// --- Math Expressions (Internal Nodes for Formulas) ---

const Expr &operator-(const Expr &s);

namespace detail {

const Expr &make_const(Integer val);
const Expr &make_const(Real val);

/* Internal sink functions for AST node creation */
const Expr &add(const Expr &lhs, const Expr &rhs);
const Expr &sub(const Expr &lhs, const Expr &rhs);
const Expr &mul(const Expr &lhs, const Expr &rhs);
const Expr &div(const Expr &lhs, const Expr &rhs);
const Expr &mod(const Expr &lhs, const Expr &rhs);

class UnaryExpr : public Expr {
 protected:
  explicit UnaryExpr(const Expr *s) : s_(s) {}
  const Expr *s_;
};

class NegExpr : public UnaryExpr {
 public:
  operator Integer() const override { return -static_cast<Integer>(*s_); }
  operator Real() const override { return -static_cast<Real>(*s_); }
  std::string expr() const override { return fmt::format("-{}", s_->expr()); }

 private:
  explicit NegExpr(const Expr *s) : UnaryExpr(s) {}
  friend const Expr &lv::stats::operator-(const Expr &s);
};

/* Helper to wrap literals into Expressions while passing through real
 * Expressions */
template <typename T>
const Expr &wrap_expr(T &&val) {
  using U = std::decay_t<T>;
  if constexpr (std::is_base_of_v<Expr, U>) {
    return val;
  } else {
    if constexpr (std::is_floating_point_v<U>)
      return make_const(static_cast<Real>(val));
    else
      return make_const(static_cast<Integer>(val));
  }
}

/* SFINAE trait: True if both are Expr/Arithmetic AND at least one is an Expr */
template <typename T1, typename T2>
using is_stat_op_applicable =
    std::enable_if_t<(std::is_base_of_v<Expr, std::decay_t<T1>> ||
                      (std::is_arithmetic_v<std::decay_t<T1>> &&
                       !std::is_same_v<std::decay_t<T1>, bool>)) &&
                     (std::is_base_of_v<Expr, std::decay_t<T2>> ||
                      (std::is_arithmetic_v<std::decay_t<T2>> &&
                       !std::is_same_v<std::decay_t<T2>, bool>)) &&
                     (std::is_base_of_v<Expr, std::decay_t<T1>> ||
                      std::is_base_of_v<Expr, std::decay_t<T2>>)>;

}  // namespace detail

template <typename T1, typename T2,
          typename = detail::is_stat_op_applicable<T1, T2>>
const Expr &operator+(T1 &&lhs, T2 &&rhs) {
  return detail::add(detail::wrap_expr(std::forward<T1>(lhs)),
                     detail::wrap_expr(std::forward<T2>(rhs)));
}

template <typename T1, typename T2,
          typename = detail::is_stat_op_applicable<T1, T2>>
const Expr &operator-(T1 &&lhs, T2 &&rhs) {
  return detail::sub(detail::wrap_expr(std::forward<T1>(lhs)),
                     detail::wrap_expr(std::forward<T2>(rhs)));
}

template <typename T1, typename T2,
          typename = detail::is_stat_op_applicable<T1, T2>>
const Expr &operator*(T1 &&lhs, T2 &&rhs) {
  return detail::mul(detail::wrap_expr(std::forward<T1>(lhs)),
                     detail::wrap_expr(std::forward<T2>(rhs)));
}

template <typename T1, typename T2,
          typename = detail::is_stat_op_applicable<T1, T2>>
const Expr &operator/(T1 &&lhs, T2 &&rhs) {
  return detail::div(detail::wrap_expr(std::forward<T1>(lhs)),
                     detail::wrap_expr(std::forward<T2>(rhs)));
}

template <
    typename T1, typename T2,
    typename = std::enable_if_t<(std::is_base_of_v<Expr, std::decay_t<T1>> ||
                                 std::is_integral_v<std::decay_t<T1>>) &&
                                (std::is_base_of_v<Expr, std::decay_t<T2>> ||
                                 std::is_integral_v<std::decay_t<T2>>) &&
                                (std::is_base_of_v<Expr, std::decay_t<T1>> ||
                                 std::is_base_of_v<Expr, std::decay_t<T2>>)>>
const Expr &operator%(T1 &&lhs, T2 &&rhs) {
  return detail::mod(detail::wrap_expr(std::forward<T1>(lhs)),
                     detail::wrap_expr(std::forward<T2>(rhs)));
}

namespace detail {

class BinaryExpr : public Expr {
 protected:
  BinaryExpr(const Expr *lhs, const Expr *rhs) : lhs_(lhs), rhs_(rhs) {}
  const Expr *lhs_;
  const Expr *rhs_;
};

class AddExpr : public BinaryExpr {
 public:
  operator Integer() const override {
    return static_cast<Integer>(*lhs_) + static_cast<Integer>(*rhs_);
  };

  operator Real() const override {
    return static_cast<Real>(*lhs_) + static_cast<Real>(*rhs_);
  };

  std::string expr() const override {
    return fmt::format("({} + {})", lhs_->expr(), rhs_->expr());
  }

 private:
  AddExpr(const Expr *lhs, const Expr *rhs) : BinaryExpr(lhs, rhs) {}
  friend const Expr &add(const Expr &lhs, const Expr &rhs);
};

class SubExpr : public BinaryExpr {
 public:
  operator Integer() const override {
    return static_cast<Integer>(*lhs_) - static_cast<Integer>(*rhs_);
  };

  operator Real() const override {
    return static_cast<Real>(*lhs_) - static_cast<Real>(*rhs_);
  };

  std::string expr() const override {
    return fmt::format("({} - {})", lhs_->expr(), rhs_->expr());
  }

 private:
  SubExpr(const Expr *lhs, const Expr *rhs) : BinaryExpr(lhs, rhs) {}
  friend const Expr &sub(const Expr &lhs, const Expr &rhs);
};

class MulExpr : public BinaryExpr {
 public:
  operator Integer() const override {
    return static_cast<Integer>(*lhs_) * static_cast<Integer>(*rhs_);
  };

  operator Real() const override {
    return static_cast<Real>(*lhs_) * static_cast<Real>(*rhs_);
  };

  std::string expr() const override {
    return fmt::format("({} * {})", lhs_->expr(), rhs_->expr());
  }

 private:
  MulExpr(const Expr *lhs, const Expr *rhs) : BinaryExpr(lhs, rhs) {}
  friend const Expr &mul(const Expr &lhs, const Expr &rhs);
};

class DivExpr : public BinaryExpr {
 public:
  operator Integer() const override {
    return static_cast<Integer>(*lhs_) / static_cast<Integer>(*rhs_);
  };

  operator Real() const override {
    return static_cast<Real>(*lhs_) / static_cast<Real>(*rhs_);
  };

  std::string expr() const override {
    return fmt::format("({} / {})", lhs_->expr(), rhs_->expr());
  }

 private:
  DivExpr(const Expr *lhs, const Expr *rhs) : BinaryExpr(lhs, rhs) {}
  friend const Expr &div(const Expr &lhs, const Expr &rhs);
};

class ModExpr : public BinaryExpr {
 public:
  operator Integer() const override {
    return static_cast<Integer>(*lhs_) % static_cast<Integer>(*rhs_);
  };

  operator Real() const override {
    return static_cast<Real>(static_cast<Integer>(*this));
  };

  std::string expr() const override {
    return fmt::format("({} % {})", lhs_->expr(), rhs_->expr());
  }

 private:
  ModExpr(const Expr *lhs, const Expr *rhs) : BinaryExpr(lhs, rhs) {}
  friend const Expr &mod(const Expr &lhs, const Expr &rhs);
};

template <typename T>
class Constant : public Expr {
  static_assert(std::is_same_v<T, Integer> || std::is_same_v<T, Real>,
                "Constant only supports Integer or Real");

 public:
  explicit Constant(T val) : val_(val) {}
  operator Integer() const override { return static_cast<Integer>(val_); }
  operator Real() const override { return static_cast<Real>(val_); }
  std::string expr() const override { return fmt::format("{}", val_); }

 private:
  const T val_;
};

}  // namespace detail

// 3. The Formula (Named Derived Value)
template <class T>
class Formula : public Expr, public MetricBase {
  static_assert(std::is_same_v<T, Integer> || std::is_same_v<T, Real>,
                "Formula only supports lv::stats::Integer or lv::stats::Real");

 public:
  Formula(Group *parent, const char *name, const char *desc)
      : MetricBase(name, desc), val_(nullptr) {
    parent->add_child(this);
  }

  Formula &operator=(const Expr &val) {
    val_ = &val;
    return *this;
  }

  // MetricBase
  toml::table tabularize() const override {
    return toml::table{
        {"val", static_cast<T>(*this)},
        {"desc", desc()},
        {"formula", expr()},
    };
  }
  void reset() override {}

  operator Integer() const override {
    return val_ ? static_cast<Integer>(*val_) : 0;
  }
  operator Real() const override {
    return val_ ? static_cast<Real>(*val_) : 0.0;
  }
  std::string expr() const override {
    return val_ ? val_->expr() : "undefined";
  }

 private:
  const Expr *val_ = nullptr;
};

/* Unified binding macro for constructors */
#define LV_STAT(var, ...) var(this, #var, ##__VA_ARGS__)

}  // namespace stats
}  // namespace lv
