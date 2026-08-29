// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include <liblv/binding.h>
#include <liblv/statistics.h>

#include <memory>
#include <vector>

namespace lv {
namespace stats {
namespace detail {

/* Internal registry to manage the lifetime of intermediate AST nodes */
static std::vector<std::unique_ptr<Expr>> &get_expr_registry() {
  static std::vector<std::unique_ptr<Expr>> registry;
  return registry;
}

/* Helper to register an expression node already created with 'new' */
template <typename T>
const Expr &register_expr(T *ptr) {
  std::unique_ptr<T> expr(ptr);
  const Expr &ref = *expr;
  get_expr_registry().push_back(std::move(expr));
  return ref;
}

/* Helper to create a constant node */
const Expr &make_const(Integer val) {
  return register_expr(new Constant<Integer>(val));
}
const Expr &make_const(Real val) {
  return register_expr(new Constant<Real>(val));
}

/* Binary node creation functions */

const Expr &add(const Expr &lhs, const Expr &rhs) {
  return register_expr(new AddExpr(&lhs, &rhs));
}

const Expr &sub(const Expr &lhs, const Expr &rhs) {
  return register_expr(new SubExpr(&lhs, &rhs));
}

const Expr &mul(const Expr &lhs, const Expr &rhs) {
  return register_expr(new MulExpr(&lhs, &rhs));
}

const Expr &div(const Expr &lhs, const Expr &rhs) {
  return register_expr(new DivExpr(&lhs, &rhs));
}

const Expr &mod(const Expr &lhs, const Expr &rhs) {
  return register_expr(new ModExpr(&lhs, &rhs));
}

}  // namespace detail

/* Unary operators */

const Expr &operator-(const Expr &s) {
  return detail::register_expr(new detail::NegExpr(&s));
}

LV_BINDING(stats, Group)
    .constructor(
        [](const char *name) {
          return std::make_shared<Group>(name);
        },
        lv::params(lv::param("name")), lv::doc("Create a statistics group."))
    .method("add_sub_group", &Group::add_sub_group,
            lv::params(lv::param("sub_group")),
            lv::doc("Attach another statistics group as a child."))
    .method("dump_toml", &Group::dump_toml,
            lv::doc("Dump the group and children as TOML."))
    .method("reset", &Group::reset,
            lv::doc("Reset all child metrics and sub-groups."));

}  // namespace stats
}  // namespace lv
