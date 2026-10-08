// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include <fmt/format.h>
#include <liblv/binding.h>
#include <liblv/log.h>
#include <liblv/statistics.h>

#include <algorithm>
#include <numeric>
#include <sstream>
#include <unordered_set>

namespace lv::stats {
namespace detail {

const char *checked_name(const char *name) {
  if (name == nullptr || *name == '\0') {
    LV_FATAL("Statistic name must not be empty");
  }
  return name;
}

// Scalar evaluation behind an Expr handle: integer/real arithmetic and formula
// text. Composite nodes retain operands through Expr handles; they never retain
// native modules or groups. Registration, metadata and reset belong to
// Statistic.
struct Node {
  virtual ~Node() = default;
  virtual Integer integer() const = 0;
  virtual Real real() const = 0;
  virtual std::string expr() const = 0;
  // Append direct expression dependencies for cycle detection. Leaves append
  // nothing; these borrowed pointers are only used during graph traversal.
  virtual void append_operand_nodes(std::vector<const Node *> &) const {}

  static const Node *expression_node(const Expr &expression) {
    return expression.node_.get();
  }
  // Includes this node. Rejecting such a dependency before formula assignment
  // prevents both recursive evaluation and a shared_ptr ownership cycle.
  bool depends_on(const Node *target) const {
    std::vector<const Node *> pending{this};
    std::unordered_set<const Node *> visited;
    while (!pending.empty()) {
      const auto *current = pending.back();
      pending.pop_back();
      if (current == target) return true;
      if (current && visited.insert(current).second)
        current->append_operand_nodes(pending);
    }
    return false;
  }
};

// Registered data shared by MetricBase handles and Group entries. Provides
// metadata, TOML output and reset; a statistic need not be scalar (e.g.
// ArrayData).
struct Statistic {
  Statistic(const char *name, const char *desc)
      : name(checked_name(name)), desc(desc ? desc : "") {}
  virtual ~Statistic() = default;
  virtual toml::table tabularize() const = 0;
  virtual void reset() = 0;
  std::string name;
  std::string desc;
};

// Metric's mutable integer storage. Both bases view the same object: Statistic
// exposes it to the group, while Node exposes its live value to expressions.
struct Counter : Statistic, Node {
  using Statistic::Statistic;
  Integer value = 0;
  Integer integer() const override { return value; }
  Real real() const override { return static_cast<Real>(value); }
  std::string expr() const override { return name; }
  toml::table tabularize() const override {
    return toml::table{{"val", value}, {"desc", desc}};
  }
  void reset() override { value = 0; }
};

// Formula's stable identity. Assignment replaces value, so existing expressions
// still follow the formula. T selects dump arithmetic; consumers select theirs.
template <typename T>
struct FormulaData : Statistic, Node {
  using Statistic::Statistic;
  Expr value;
  Integer integer() const override { return static_cast<Integer>(value); }
  Real real() const override { return static_cast<Real>(value); }
  std::string expr() const override { return value.expr(); }
  void append_operand_nodes(std::vector<const Node *> &out) const override {
    out.push_back(expression_node(value));
  }
  toml::table tabularize() const override {
    return toml::table{
        {"val", static_cast<T>(value)}, {"desc", desc}, {"formula", expr()}};
  }
  void reset() override {}
};

// Array's shared counter storage. Scalar element/reduction nodes retain this
// data separately because the whole array cannot be evaluated as one number.
struct ArrayData : Statistic {
  ArrayData(const char *name, const char *desc, uint32_t n)
      : Statistic(name, desc), values(n, 0) {}
  std::vector<Integer> values;
  // Out-of-range writes are no-ops, whether made through Array or Element.
  void incr(uint32_t index, Integer value) {
    if (index < values.size()) values[index] += value;
  }
  void decr(uint32_t index, Integer value) {
    if (index < values.size()) values[index] -= value;
  }
  void set(uint32_t index, Integer value) {
    if (index < values.size()) values[index] = value;
  }
  toml::table tabularize() const override {
    toml::array array;
    for (auto value : values) array.push_back(value);
    return toml::table{{"val", array}, {"desc", desc}};
  }
  void reset() override { std::fill(values.begin(), values.end(), 0); }
};

struct ArrayElement : Node {
  ArrayElement(std::shared_ptr<ArrayData> array, uint32_t index)
      : array(std::move(array)), index(index) {}
  std::shared_ptr<ArrayData> array;
  uint32_t index;
  Integer integer() const override { return array->values[index]; }
  Real real() const override { return static_cast<Real>(integer()); }
  std::string expr() const override {
    return fmt::format("{}[{}]", array->name, index);
  }
};

struct Sum : Node {
  explicit Sum(std::shared_ptr<ArrayData> array) : array(std::move(array)) {}
  std::shared_ptr<ArrayData> array;
  Integer integer() const override {
    return std::accumulate(array->values.begin(), array->values.end(), 0LL);
  }
  Real real() const override { return static_cast<Real>(integer()); }
  std::string expr() const override {
    return fmt::format("sum({})", array->name);
  }
};

struct Average : Sum {
  using Sum::Sum;
  Integer integer() const override {
    return Sum::integer() / static_cast<Integer>(array->values.size());
  }
  Real real() const override {
    return std::accumulate(array->values.begin(), array->values.end(), 0.0) /
           array->values.size();
  }
  std::string expr() const override {
    return fmt::format("avg({})", array->name);
  }
};

template <typename T>
struct Constant : Node {
  explicit Constant(T value) : value(value) {}
  T value;
  Integer integer() const override { return static_cast<Integer>(value); }
  Real real() const override { return static_cast<Real>(value); }
  std::string expr() const override { return fmt::format("{}", value); }
};

struct Negate : Node {
  explicit Negate(Expr value) : value(std::move(value)) {}
  Expr value;
  Integer integer() const override { return -static_cast<Integer>(value); }
  Real real() const override { return -static_cast<Real>(value); }
  std::string expr() const override { return fmt::format("-{}", value.expr()); }
  void append_operand_nodes(std::vector<const Node *> &out) const override {
    out.push_back(expression_node(value));
  }
};

template <char Op>
struct Binary : Node {
  Binary(Expr lhs, Expr rhs) : lhs(std::move(lhs)), rhs(std::move(rhs)) {}
  Expr lhs, rhs;
  template <typename T>
  T evaluate() const {
    if constexpr (Op == '+') return static_cast<T>(lhs) + static_cast<T>(rhs);
    if constexpr (Op == '-') return static_cast<T>(lhs) - static_cast<T>(rhs);
    if constexpr (Op == '*') return static_cast<T>(lhs) * static_cast<T>(rhs);
    if constexpr (Op == '/') return static_cast<T>(lhs) / static_cast<T>(rhs);
    if constexpr (Op == '%')
      return static_cast<T>(static_cast<Integer>(lhs) %
                            static_cast<Integer>(rhs));
  }
  Integer integer() const override { return evaluate<Integer>(); }
  Real real() const override { return evaluate<Real>(); }
  std::string expr() const override {
    return fmt::format("({} {} {})", lhs.expr(), Op, rhs.expr());
  }
  void append_operand_nodes(std::vector<const Node *> &out) const override {
    out.push_back(expression_node(lhs));
    out.push_back(expression_node(rhs));
  }
};

Expr make_const(Integer value) {
  return Expr(std::make_shared<Constant<Integer>>(value));
}
Expr make_const(Real value) {
  return Expr(std::make_shared<Constant<Real>>(value));
}
Expr add(const Expr &lhs, const Expr &rhs) {
  return Expr(std::make_shared<Binary<'+'>>(lhs, rhs));
}
Expr sub(const Expr &lhs, const Expr &rhs) {
  return Expr(std::make_shared<Binary<'-'>>(lhs, rhs));
}
Expr mul(const Expr &lhs, const Expr &rhs) {
  return Expr(std::make_shared<Binary<'*'>>(lhs, rhs));
}
Expr div(const Expr &lhs, const Expr &rhs) {
  return Expr(std::make_shared<Binary<'/'>>(lhs, rhs));
}
Expr mod(const Expr &lhs, const Expr &rhs) {
  return Expr(std::make_shared<Binary<'%'>>(lhs, rhs));
}

// LuaJIT numbers are doubles; preserve fractions until the consumer evaluates.
auto binary_binding(Expr (*operation)(const Expr &, const Expr &)) {
  return sol::overload(
      operation,
      [operation](const Expr &lhs, Real rhs) {
        return operation(lhs, make_const(rhs));
      },
      [operation](Real lhs, const Expr &rhs) {
        return operation(make_const(lhs), rhs);
      });
}
}  // namespace detail

Expr::Expr(std::shared_ptr<detail::Node> node) : node_(std::move(node)) {}
Expr::operator Integer() const { return node_ ? node_->integer() : 0; }
Expr::operator Real() const { return node_ ? node_->real() : 0.0; }
std::string Expr::expr() const {
  return !label_.empty() ? label_ : node_ ? node_->expr() : "undefined";
}
Expr operator-(const Expr &value) {
  return Expr(std::make_shared<detail::Negate>(value));
}

MetricBase::MetricBase(std::shared_ptr<detail::Statistic> data)
    : data_(std::move(data)) {}
toml::table MetricBase::tabularize() const { return data_->tabularize(); }
void MetricBase::reset() { data_->reset(); }
const std::string &MetricBase::name() const { return data_->name; }
const std::string &MetricBase::desc() const { return data_->desc; }

Metric::Metric(Group *parent, const char *name, const char *desc)
    : MetricBase(std::make_shared<detail::Counter>(name, desc)) {
  parent->add_child(this);
}
Metric::operator Expr() const {
  return Expr(std::static_pointer_cast<detail::Counter>(data_));
}
Metric::operator Integer() const {
  return static_cast<detail::Counter *>(data_.get())->value;
}
void Metric::incr(Integer value) {
  static_cast<detail::Counter *>(data_.get())->value += value;
}
void Metric::decr(Integer value) {
  static_cast<detail::Counter *>(data_.get())->value -= value;
}
void Metric::set(Integer value) {
  static_cast<detail::Counter *>(data_.get())->value = value;
}

Array::Array(Group *parent, const char *name, const char *desc, uint32_t n)
    : MetricBase(std::make_shared<detail::ArrayData>(name, desc, n)),
      sum_(std::make_shared<detail::Sum>(
          std::static_pointer_cast<detail::ArrayData>(data_))),
      avg_(std::make_shared<detail::Average>(
          std::static_pointer_cast<detail::ArrayData>(data_))) {
  elements_.reserve(n);
  for (uint32_t i = 0; i < n; ++i) elements_.emplace_back(this, i);
  parent->add_child(this);
}
Array::Element::Element(Array *parent, uint32_t index)
    : element_(std::make_shared<detail::ArrayElement>(
          std::static_pointer_cast<detail::ArrayData>(parent->data_), index)) {}
Array::Element::operator Expr() const { return Expr(element_); }
Array::Element::operator Integer() const { return element_->integer(); }
void Array::Element::incr(Integer value) {
  element_->array->incr(element_->index, value);
}
void Array::Element::decr(Integer value) {
  element_->array->decr(element_->index, value);
}
void Array::Element::set(Integer value) {
  element_->array->set(element_->index, value);
}
void Array::incr(uint32_t i, Integer value) {
  static_cast<detail::ArrayData *>(data_.get())->incr(i, value);
}
void Array::decr(uint32_t i, Integer value) {
  static_cast<detail::ArrayData *>(data_.get())->decr(i, value);
}
void Array::set(uint32_t i, Integer value) {
  static_cast<detail::ArrayData *>(data_.get())->set(i, value);
}
void Array::reset(uint32_t i) { set(i, 0); }

template <typename T>
Formula<T>::Formula(Group *parent, const char *name, const char *desc)
    : MetricBase(std::make_shared<detail::FormulaData<T>>(name, desc)) {
  parent->add_child(this);
}
template <typename T>
Formula<T>::operator Expr() const {
  return Expr(std::static_pointer_cast<detail::FormulaData<T>>(data_));
}
template <typename T>
Formula<T>::operator Integer() const {
  return static_cast<detail::FormulaData<T> *>(data_.get())->integer();
}
template <typename T>
Formula<T>::operator Real() const {
  return static_cast<detail::FormulaData<T> *>(data_.get())->real();
}
template <typename T>
Formula<T> &Formula<T>::operator=(const Expr &expression) {
  auto *formula = static_cast<detail::FormulaData<T> *>(data_.get());
  if (expression.node_ && expression.node_->depends_on(formula)) {
    LV_FATAL("Cyclic statistic formula: {}", name());
  }
  formula->value = expression;
  return *this;
}
template class Formula<Integer>;
template class Formula<Real>;

// Private shared implementation of Group. Copies of the public handle refer to
// this same registration hierarchy; mutations are visible through every copy.
struct Group::Data {
  explicit Data(const char *name) : name(detail::checked_name(name)) {}
  std::string name;
  std::vector<std::shared_ptr<detail::Statistic>> children;
  // Registration keys belong to the parent; a child's display name may change.
  std::vector<std::pair<std::string, Group>> groups;
};

Group::Group(const char *name) : data_(std::make_shared<Data>(name)) {}
const std::string &Group::name() const { return data_->name; }
void Group::set_name(const char *name) {
  data_->name = detail::checked_name(name);
}

Expr Group::get(const std::string &name) const {
  for (const auto &child : data_->children) {
    if (child->name != name) continue;
    auto node = std::dynamic_pointer_cast<detail::Node>(child);
    if (!node) LV_FATAL("Statistic is not scalar: {}.{}", this->name(), name);
    Expr result(std::move(node));
    result.label_ = this->name() + "." + name;
    return result;
  }
  for (const auto &entry : data_->groups) {
    if (entry.first == name)
      LV_FATAL("Statistic is not scalar: {}.{}", this->name(), name);
  }
  LV_FATAL("Statistic not found: {}.{}", this->name(), name);
}

void Group::validate_child_name(const std::string &name) const {
  for (const auto &child : data_->children) {
    if (child->name == name)
      LV_FATAL("Duplicate statistic: {}.{}", this->name(), name);
  }
  for (const auto &entry : data_->groups) {
    if (entry.first == name)
      LV_FATAL("Duplicate statistics group: {}.{}", this->name(), name);
  }
}
void Group::add_child(MetricBase *item) {
  if (!item) LV_FATAL("Null statistic in group: {}", name());
  validate_child_name(item->name());
  data_->children.push_back(item->data_);
}
void Group::add_sub_group(Group *sub) {
  if (!sub) LV_FATAL("Null statistics group in group: {}", name());
  if (sub->data_ == data_) return;
  validate_child_name(sub->name());
  std::vector<const Data *> pending{sub->data_.get()};
  std::unordered_set<const Data *> visited;
  while (!pending.empty()) {
    const auto *current = pending.back();
    pending.pop_back();
    if (current == data_.get()) LV_FATAL("Cyclic statistics group: {}", name());
    if (visited.insert(current).second) {
      for (const auto &entry : current->groups)
        pending.push_back(entry.second.data_.get());
    }
  }
  data_->groups.emplace_back(sub->name(), *sub);
}
toml::table Group::contents() const {
  toml::table result;
  for (const auto &entry : data_->groups)
    result.insert(entry.first, entry.second.contents());
  for (const auto &child : data_->children)
    result.insert(child->name, child->tabularize());
  return result;
}
toml::table Group::tabularize() const {
  return toml::table{{name(), contents()}};
}
std::string Group::dump_toml() const {
  std::stringstream out;
  out << toml::toml_formatter{tabularize()};
  return out.str();
}
void Group::reset() {
  for (const auto &child : data_->children) child->reset();
  for (auto &entry : data_->groups) entry.second.reset();
}

LV_BINDING(stats, Expr)
    .set(sol::meta_function::addition, detail::binary_binding(detail::add))
    .set(sol::meta_function::subtraction, detail::binary_binding(detail::sub))
    .set(sol::meta_function::multiplication,
         detail::binary_binding(detail::mul))
    .set(sol::meta_function::division, detail::binary_binding(detail::div))
    .set(sol::meta_function::unary_minus,
         static_cast<Expr (*)(const Expr &)>(&operator-))
    .set(sol::meta_function::to_string, &Expr::expr);

LV_BINDING(stats, Group)
    .constructor<const char *>(
        lv::params("name"),
        lv::doc("Create a statistics group owning its registered data."))
    .method("get", &Group::get, lv::params("name"),
            lv::doc("Get a read-only live scalar expression. The expression "
                    "retains its data across reset and source destruction. "
                    "Missing names and non-scalar statistics are errors."))
    .method(
        "add_real_formula", &Group::add_formula<Real>,
        lv::params("name", "desc", "expression"),
        lv::doc("Register a formula whose dump uses floating-point arithmetic; "
                "return a live expression. References use their consumer's "
                "arithmetic. Expressions support +, -, *, / and unary minus."))
    .method(
        "add_integer_formula", &Group::add_formula<Integer>,
        lv::params("name", "desc", "expression"),
        lv::doc("Register a formula whose dump uses int64 arithmetic without "
                "converting counters through Lua numbers; return a live "
                "expression. References use their consumer's arithmetic."))
    .method("add_sub_group", &Group::add_sub_group, lv::params("sub_group"),
            lv::doc("Retain a child group's statistics data. Registration "
                    "names must be unique; cycles are rejected."))
    .method("dump_toml", &Group::dump_toml,
            lv::doc("Dump the group and children as TOML."))
    .method("reset", &Group::reset,
            lv::doc("Reset child counters and arrays without changing formula "
                    "definitions."));

}  // namespace lv::stats
