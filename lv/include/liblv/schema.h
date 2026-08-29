/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <sol/sol.hpp>
#include <sol/stack_check_unqualified.hpp>
#include <string>
#include <tuple>
#include <vector>

namespace lv {

// SFINAE check
template <typename T>
struct is_vector : std::false_type {};
template <typename T, typename A>
struct is_vector<std::vector<T, A>> : std::true_type {};

template <typename T, typename = void>
struct has_schema : std::false_type {};
template <typename T>
struct has_schema<T, std::void_t<decltype(T::schema())>> : std::true_type {};

template <typename Class, typename T>
struct FieldDescriptor {
  const char *name;
  T Class::*member_ptr;
  T default_value;
  const char *description;
};

template <typename Class, typename T>
struct EnumFieldDescriptor : FieldDescriptor<Class, T> {
  std::vector<std::pair<T, std::string>> map;
};

// Helper function
template <typename Class, typename T>
constexpr auto field(const char *name, T Class::*ptr, T def, const char *desc) {
  return FieldDescriptor<Class, T>{name, ptr, def, desc};
}

template <typename Class, typename T>
auto field_enum(const char *name, T Class::*ptr, T def, const char *desc,
                std::initializer_list<std::pair<T, std::string>> map_init) {
  return EnumFieldDescriptor<Class, T>{{name, ptr, def, desc}, map_init};
}

template <typename Param>
Param generic_parser(const sol::table &tbl) {
  Param param;

  if (!tbl.valid()) return param;

  std::apply(
      [&](auto &&...fields) {
        (..., ([&]() {
           using DescType = std::decay_t<decltype(fields)>;
           using FieldType = std::decay_t<decltype(fields.default_value)>;

           sol::object obj = tbl[fields.name];
           if (!obj.valid()) return;

           if constexpr (std::is_same_v<
                             DescType, EnumFieldDescriptor<Param, FieldType>>) {
             // Enum field
             if (obj.is<std::string>()) {
               std::string val_str = obj.as<std::string>();
               for (const auto &[enum_val, str] : fields.map) {
                 if (str == val_str) {
                   param.*(fields.member_ptr) = enum_val;
                   return;
                 }
               }
             }
           } else if constexpr (is_vector<FieldType>::value) {
             if (obj.is<sol::table>()) {
               sol::table arr = obj.as<sol::table>();
               auto &vec = param.*(fields.member_ptr);
               vec.clear();

               using InnerType = typename FieldType::value_type;

               for (auto &kv : arr) {
                 sol::object val = kv.second;

                 if constexpr (has_schema<InnerType>::value) {
                   // Vector<Struct> (recursion)
                   if (val.is<sol::table>()) {
                     vec.push_back(
                         generic_parser<InnerType>(val.as<sol::table>()));
                   }
                 } else {
                   // Vector<Basic/Enum>
                   if (val.is<InnerType>()) {
                     vec.push_back(val.as<InnerType>());
                   }
                 }
               }
             }
           } else if constexpr (has_schema<FieldType>::value) {
             if (obj.is<sol::table>()) {
               param.*(fields.member_ptr) =
                   generic_parser<FieldType>(obj.as<sol::table>());
             }
           } else {
             // Normal field
             sol::optional<FieldType> val = tbl[fields.name];
             if (val) param.*(fields.member_ptr) = *val;
           }
         })());
      },
      Param::schema());

  return param;
}

}  // namespace lv

template <typename T, typename Handler>
inline typename std::enable_if_t<lv::has_schema<T>::value, bool> sol_lua_check(
    sol::types<T>, lua_State *L, int index, Handler &&handler,
    sol::stack::record &tracking) {
  return sol::stack::check<sol::table>(L, index, handler) ||
         sol::stack::check<sol::lua_nil_t>(L, index);
}

template <typename T>
inline typename std::enable_if_t<lv::has_schema<T>::value, T> sol_lua_get(
    sol::types<T>, lua_State *L, int index, sol::stack::record &tracking) {
  if (lua_isnil(L, index)) {
    tracking.use(1);
    return T{};
  }

  sol::table tbl(L, index);
  tracking.use(1);
  return lv::generic_parser<T>(tbl);
}

#define LV_FIELD(Name, Desc) lv::field(#Name, &Self::Name, d.Name, Desc)

#define LV_FIELD_ENUM(Name, Desc, ...) \
  lv::field_enum(#Name, &Self::Name, d.Name, Desc, __VA_ARGS__)

#define LV_SCHEMA(OwnerType, ClassName, ...)                        \
  using _lv_schema_owner_type = OwnerType;                          \
  static constexpr const char *_lv_schema_owner_token = #OwnerType; \
  static constexpr const char *_lv_schema_local_name = #ClassName;  \
  static inline auto schema() {                                     \
    using Self = ClassName;                                         \
    static const Self d;                                            \
    return std::make_tuple(__VA_ARGS__);                            \
  }

#define LV_SCHEMA_SHARED(scope_name, ClassName, ...)               \
  static constexpr const char *_lv_schema_scope = #scope_name;     \
  static constexpr const char *_lv_schema_local_name = #ClassName; \
  static inline auto schema() {                                    \
    using Self = ClassName;                                        \
    static const Self d;                                           \
    return std::make_tuple(__VA_ARGS__);                           \
  }
