/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <systemc.h>

// This shows C++ exception what()
#define SOL_EXCEPTIONS_SAFE_PROPAGATION 1
#define SOL_EXCEPTIONS_ALWAYS_UNSAFE 1

#include <liblv/schema.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <sol/sol.hpp>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace lv {

sol::state &Runtime();

class BindingLoadStats {
 public:
  BindingLoadStats() = delete;
  BindingLoadStats(const BindingLoadStats &) = delete;
  BindingLoadStats &operator=(const BindingLoadStats &) = delete;

  static std::uint32_t num_modules() { return num_modules_; }
  static double load_time_us() { return load_time_us_; }
  static void RecordLoadTime(double load_time_us) {
    load_time_us_ += load_time_us;
    num_modules_++;
  }

 private:
  inline static std::uint32_t num_modules_ = 0;
  inline static double load_time_us_ = 0;
};

template <typename T>
struct lua_type_alias {
  static std::string name() { return {}; }
};

}  // namespace lv

#include <liblv/binding/type_aliases.h>

namespace lv {

namespace docgen {

struct TypeRef {
  std::string name;
  std::function<std::string()> resolver;

  static TypeRef Exact(std::string name) {
    TypeRef ref;
    ref.name = std::move(name);
    return ref;
  }

  static TypeRef Deferred(std::function<std::string()> resolver) {
    TypeRef ref;
    ref.resolver = std::move(resolver);
    return ref;
  }

  std::string Resolve() const {
    if (!name.empty()) return name;
    if (resolver) return resolver();
    return {};
  }
};

struct ParamDoc {
  std::string name;
  std::string doc;
  TypeRef lua_type;
};

struct CallableDoc {
  std::string name;
  std::string doc;
  std::vector<ParamDoc> params;
  TypeRef return_type;
};

struct PropertyDoc {
  std::string name;
  std::string doc;
  TypeRef lua_type;
};

struct ValueDoc {
  std::string name;
  std::string doc;
  TypeRef lua_type;
};

struct SchemaFieldDoc {
  std::string name;
  std::string doc;
  TypeRef lua_type;
  bool optional = true;
};

struct SchemaDoc {
  std::type_index cpp_type{typeid(void)};
  std::string full_name;
  std::vector<SchemaFieldDoc> fields;
};

struct ClassDoc {
  std::string name;
  std::string doc;
};

struct EnumValueDoc {
  std::string name;
  std::string value;
};

struct EnumDoc {
  std::string name;
  std::vector<EnumValueDoc> values;
};

struct TypeDoc {
  std::type_index cpp_type{typeid(void)};
  std::string scope;
  std::string lua_name;
  std::string full_name;
  std::string doc;
  std::vector<TypeRef> bases;
  std::vector<CallableDoc> constructors;
  std::vector<CallableDoc> methods;
  std::vector<PropertyDoc> properties;
};

struct ModuleDoc {
  std::string scope;
  std::vector<ClassDoc> classes;
  std::vector<CallableDoc> functions;
  std::vector<ValueDoc> constants;
  std::vector<EnumDoc> enums;
};

class Registry {
 public:
  TypeDoc &RegisterType(std::type_index cpp_type, std::string scope,
                        std::string lua_name, std::string doc = {},
                        std::string cpp_name = {});
  void RegisterTypeName(std::type_index cpp_type, std::string scope,
                        std::string lua_name, std::string cpp_name = {});
  SchemaDoc &RegisterSchema(std::type_index cpp_type, std::string full_name,
                            std::vector<SchemaFieldDoc> fields);
  ModuleDoc &RegisterModule(std::string scope);
  TypeDoc *FindType(std::type_index cpp_type);
  const TypeDoc *FindType(std::type_index cpp_type) const;
  std::string FindTypeName(std::type_index cpp_type) const;
  std::string FindTypeName(std::string_view cpp_name) const;
  std::string FindSchemaName(std::type_index cpp_type) const;
  const std::vector<TypeDoc> &types() const { return types_; }
  const std::vector<ModuleDoc> &modules() const { return modules_; }
  const std::vector<SchemaDoc> &schemas() const { return schemas_; }

 private:
  struct TypeName {
    std::type_index cpp_type{typeid(void)};
    std::string cpp_name;
    std::string full_name;
  };

  std::vector<TypeDoc> types_;
  std::vector<ModuleDoc> modules_;
  std::vector<SchemaDoc> schemas_;
  std::vector<TypeName> type_names_;
};

Registry &registry();
std::string GenerateLuaLS();

}  // namespace docgen

struct Doc {
  std::string text;
};

struct Param {
  std::string name;
  std::string doc;
  std::string lua_type;
};

struct Params {
  std::vector<Param> values;
};

struct LuaType {
  std::string name;
};

inline Doc doc(std::string_view text) { return Doc{std::string(text)}; }

inline LuaType lua_type(std::string_view name) {
  return LuaType{std::string(name)};
}

inline Param param(std::string_view name, std::string_view doc = {}) {
  return Param{std::string(name), std::string(doc), {}};
}

inline Param param(std::string_view name, LuaType lua_type,
                   std::string_view doc = {}) {
  return Param{std::string(name), std::string(doc), std::move(lua_type.name)};
}

inline Param param(std::string_view name, std::string_view doc,
                   LuaType lua_type) {
  return Param{std::string(name), std::string(doc), std::move(lua_type.name)};
}

namespace binding_detail {

template <typename T>
struct is_metadata : std::false_type {};
template <>
struct is_metadata<Doc> : std::true_type {};
template <>
struct is_metadata<Param> : std::true_type {};
template <>
struct is_metadata<Params> : std::true_type {};
template <>
struct is_metadata<LuaType> : std::true_type {};

template <typename T>
constexpr bool is_metadata_v = is_metadata<std::decay_t<T>>::value;

template <typename... Ts>
Params make_params(Ts &&...values) {
  Params params;
  (params.values.push_back(
       Param{std::string(std::forward<Ts>(values)), {}, {}}),
   ...);
  return params;
}

inline void AppendParam(Params &params, Param value) {
  params.values.push_back(std::move(value));
}

template <typename... Ts>
Params make_rich_params(Ts &&...values) {
  Params params;
  (AppendParam(params, std::forward<Ts>(values)), ...);
  return params;
}

struct Metadata {
  std::string doc;
  std::optional<Params> params;
  std::optional<std::string> lua_type;
};

inline void ApplyMetadata(Metadata &metadata, const Doc &doc) {
  metadata.doc = doc.text;
}

inline void ApplyMetadata(Metadata &metadata, const Params &params) {
  metadata.params = params;
}

inline void ApplyMetadata(Metadata &metadata, const LuaType &lua_type) {
  metadata.lua_type = lua_type.name;
}

inline void ApplyMetadata(Metadata &metadata, const char *doc) {
  metadata.doc = doc == nullptr ? std::string{} : std::string(doc);
}

inline void ApplyMetadata(Metadata &metadata, std::string_view doc) {
  metadata.doc = std::string(doc);
}

template <typename... Meta>
Metadata ParseMetadata(Meta &&...meta) {
  Metadata metadata;
  (ApplyMetadata(metadata, std::forward<Meta>(meta)), ...);
  return metadata;
}

template <typename T, typename = void>
struct is_optional : std::false_type {};
template <typename T>
struct is_optional<T, std::enable_if_t<sol::meta::is_optional_v<T>>>
    : std::true_type {
  using value_type = typename T::value_type;
};

template <typename T>
struct is_shared_ptr : std::false_type {};
template <typename T>
struct is_shared_ptr<std::shared_ptr<T>> : std::true_type {
  using element_type = T;
};

template <typename T>
struct is_sol_as_table : std::false_type {};
template <typename T>
struct is_sol_as_table<sol::as_table_t<T>> : std::true_type {
  using value_type = T;
};

template <typename T>
struct member_object_pointer_traits;
template <typename Class, typename T>
struct member_object_pointer_traits<T Class::*> {
  using value_type = T;
};

template <typename T, typename = void>
struct function_traits;

template <typename R, typename... Args>
struct function_traits<R (*)(Args...), void> {
  using return_type = R;
  using args_tuple = std::tuple<Args...>;
};

template <typename R, typename... Args>
struct function_traits<R(Args...), void> {
  using return_type = R;
  using args_tuple = std::tuple<Args...>;
};

template <typename Class, typename R, typename... Args>
struct function_traits<R (Class::*)(Args...), void> {
  using return_type = R;
  using class_type = Class;
  using args_tuple = std::tuple<Args...>;
};

template <typename Class, typename R, typename... Args>
struct function_traits<R (Class::*)(Args...) const, void> {
  using return_type = R;
  using class_type = Class;
  using args_tuple = std::tuple<Args...>;
};

template <typename Class, typename R, typename... Args>
struct function_traits<R (Class::*)(Args...) volatile, void> {
  using return_type = R;
  using class_type = Class;
  using args_tuple = std::tuple<Args...>;
};

template <typename Class, typename R, typename... Args>
struct function_traits<R (Class::*)(Args...) const volatile, void> {
  using return_type = R;
  using class_type = Class;
  using args_tuple = std::tuple<Args...>;
};

template <typename T>
struct function_traits<T, std::void_t<decltype(&std::decay_t<T>::operator())>>
    : function_traits<decltype(&std::decay_t<T>::operator())> {};

template <typename T>
using stripped_t = std::remove_cv_t<std::remove_reference_t<T>>;

template <typename T>
using lua_arg_t =
    std::conditional_t<std::is_lvalue_reference_v<T> &&
                           (!std::is_const_v<std::remove_reference_t<T>> ||
                            !std::is_copy_constructible_v<stripped_t<T>>),
                       T, stripped_t<T>>;

template <typename T>
std::string LuaTypeName();

template <typename T>
std::string SchemaInlineType();

template <typename T>
std::string SchemaLuaTypeName();

template <typename T>
docgen::TypeRef LuaTypeRef();

template <typename T>
std::string AliasTypeName() {
  using U = stripped_t<T>;
  return lua_type_alias<U>::name();
}

template <typename T>
std::string RegisteredTypeName() {
  using U = stripped_t<T>;
  return docgen::registry().FindTypeName(std::type_index(typeid(U)));
}

template <typename T>
std::string EnumLuaType() {
  if constexpr (std::is_enum_v<stripped_t<T>>) {
    return "integer";
  } else {
    return LuaTypeName<T>();
  }
}

template <typename T, typename = void>
struct has_schema_owner : std::false_type {};
template <typename T>
struct has_schema_owner<T, std::void_t<typename T::_lv_schema_owner_type>>
    : std::true_type {};

template <typename T, typename = void>
struct has_schema_scope : std::false_type {};
template <typename T>
struct has_schema_scope<T, std::void_t<decltype(T::_lv_schema_scope)>>
    : std::true_type {};

template <typename T, typename = void>
struct is_complete_type : std::false_type {};
template <typename T>
struct is_complete_type<T, std::void_t<decltype(sizeof(T))>> : std::true_type {
};

template <typename Desc>
std::string SchemaFieldType(const Desc &desc) {
  using FieldType = stripped_t<decltype(desc.default_value)>;
  return LuaTypeName<FieldType>();
}

template <typename Class, typename T>
std::string SchemaFieldType(const EnumFieldDescriptor<Class, T> &desc) {
  if (desc.map.empty()) return EnumLuaType<T>();
  std::ostringstream os;
  bool first = true;
  for (const auto &[_, name] : desc.map) {
    if (!first) os << "|";
    first = false;
    os << "\"" << name << "\"";
  }
  return os.str();
}

template <typename Class, typename T>
std::string SchemaFieldType(const FieldDescriptor<Class, T> &) {
  return LuaTypeName<T>();
}

template <typename Desc>
docgen::TypeRef SchemaFieldLuaType(const Desc &desc) {
  using FieldType = stripped_t<decltype(desc.default_value)>;
  return LuaTypeRef<FieldType>();
}

template <typename Class, typename T>
docgen::TypeRef SchemaFieldLuaType(const EnumFieldDescriptor<Class, T> &desc) {
  if (desc.map.empty()) return LuaTypeRef<T>();
  std::ostringstream os;
  bool first = true;
  for (const auto &[_, name] : desc.map) {
    if (!first) os << "|";
    first = false;
    os << "\"" << name << "\"";
  }
  return docgen::TypeRef::Exact(os.str());
}

inline std::string QuoteLuaString(std::string_view value) {
  std::string out = "\"";
  for (char ch : value) {
    switch (ch) {
      case '\\':
        out += "\\\\";
        break;
      case '"':
        out += "\\\"";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        out.push_back(ch);
        break;
    }
  }
  out.push_back('"');
  return out;
}

template <typename T>
std::string SchemaDefaultLiteral(const T &value) {
  using U = stripped_t<T>;
  if constexpr (is_optional<U>::value) {
    if (!value) return "nil";
    return SchemaDefaultLiteral(*value);
  } else if constexpr (std::is_same_v<U, bool>) {
    return value ? "true" : "false";
  } else if constexpr (std::is_integral_v<U>) {
    return std::to_string(value);
  } else if constexpr (std::is_floating_point_v<U>) {
    std::ostringstream os;
    os << value;
    return os.str();
  } else if constexpr (std::is_enum_v<U>) {
    return std::to_string(static_cast<std::underlying_type_t<U>>(value));
  } else if constexpr (std::is_same_v<U, std::string>) {
    return QuoteLuaString(value);
  } else if constexpr (std::is_same_v<U, std::string_view>) {
    return QuoteLuaString(value);
  } else if constexpr (std::is_same_v<U, char *> ||
                       std::is_same_v<U, const char *>) {
    return value == nullptr ? "nil" : QuoteLuaString(value);
  } else if constexpr (std::is_same_v<U, sol::function> ||
                       std::is_same_v<U, sol::protected_function>) {
    return "nil";
  } else if constexpr (is_vector<U>::value || has_schema<U>::value) {
    return "{}";
  } else {
    return {};
  }
}

template <typename Desc>
std::string SchemaDefaultValue(const Desc &desc) {
  return SchemaDefaultLiteral(desc.default_value);
}

template <typename Class, typename T>
std::string SchemaDefaultValue(const EnumFieldDescriptor<Class, T> &desc) {
  for (const auto &[value, name] : desc.map) {
    if (value == desc.default_value) return QuoteLuaString(name);
  }
  return SchemaDefaultLiteral(desc.default_value);
}

template <typename Field>
std::string SchemaFieldDocString(const Field &field) {
  std::string doc =
      field.description == nullptr ? std::string{} : field.description;
  const auto default_value = SchemaDefaultValue(field);
  if (default_value.empty()) return doc;
  if (!doc.empty()) {
    doc += " (default: ";
    doc += default_value;
    doc += ")";
  } else {
    doc = "Default: " + default_value;
  }
  return doc;
}

template <typename Field>
docgen::SchemaFieldDoc BuildSchemaFieldDoc(const Field &field) {
  docgen::SchemaFieldDoc doc;
  doc.name = field.name;
  doc.doc = SchemaFieldDocString(field);
  doc.lua_type = SchemaFieldLuaType(field);
  return doc;
}

template <typename Tuple, std::size_t... I>
std::vector<docgen::SchemaFieldDoc> BuildSchemaFields(
    const Tuple &fields, std::index_sequence<I...>) {
  std::vector<docgen::SchemaFieldDoc> docs;
  docs.reserve(sizeof...(I));
  (docs.push_back(BuildSchemaFieldDoc(std::get<I>(fields))), ...);
  return docs;
}

template <typename T>
std::string SchemaFullName() {
  using U = stripped_t<T>;
  if constexpr (has_schema_owner<U>::value) {
    using Owner = typename U::_lv_schema_owner_type;
    auto owner_name =
        docgen::registry().FindTypeName(U::_lv_schema_owner_token);
    if constexpr (is_complete_type<Owner>::value) {
      if (owner_name.empty()) {
        owner_name =
            docgen::registry().FindTypeName(std::type_index(typeid(Owner)));
      }
    }
    if (owner_name.empty()) return U::_lv_schema_local_name;
    return owner_name + "." + std::string(U::_lv_schema_local_name);
  } else if constexpr (has_schema_scope<U>::value) {
    if (std::string_view(U::_lv_schema_scope).empty()) {
      return U::_lv_schema_local_name;
    }
    return std::string(U::_lv_schema_scope) + "." +
           std::string(U::_lv_schema_local_name);
  } else {
    return {};
  }
}

template <typename T>
void RegisterSchemaDoc(const std::string &full_name) {
  using U = stripped_t<T>;
  if (!docgen::registry().FindSchemaName(std::type_index(typeid(U))).empty()) {
    return;
  }

  const auto fields = U::schema();
  auto field_docs = BuildSchemaFields(
      fields, std::make_index_sequence<std::tuple_size_v<decltype(fields)>>{});
  docgen::registry().RegisterSchema(std::type_index(typeid(U)), full_name,
                                    std::move(field_docs));
}

template <typename T>
std::string SchemaLuaTypeName() {
  using U = stripped_t<T>;
  if constexpr (!has_schema_owner<U>::value && !has_schema_scope<U>::value) {
    return SchemaInlineType<U>();
  } else {
    const auto registered =
        docgen::registry().FindSchemaName(std::type_index(typeid(U)));
    if (!registered.empty()) return registered;

    const auto full_name = SchemaFullName<U>();
    if (full_name.empty()) return SchemaInlineType<U>();
    RegisterSchemaDoc<U>(full_name);
    return full_name;
  }
}

template <typename T>
std::string LuaTypeName() {
  using U = stripped_t<T>;
  if constexpr (std::is_same_v<U, void>) {
    return {};
  } else if constexpr (std::is_same_v<U, bool>) {
    return "boolean";
  } else if constexpr (std::is_same_v<U, std::string> ||
                       std::is_same_v<U, std::string_view> ||
                       std::is_same_v<U, char *> ||
                       std::is_same_v<U, const char *>) {
    return "string";
  } else if constexpr (std::is_integral_v<U>) {
    return "integer";
  } else if constexpr (std::is_floating_point_v<U>) {
    return "number";
  } else if constexpr (std::is_same_v<U, sol::table>) {
    return "table";
  } else if constexpr (std::is_same_v<U, sol::object>) {
    return "any";
  } else if constexpr (std::is_same_v<U, sol::function> ||
                       std::is_same_v<U, sol::protected_function>) {
    return "function";
  } else if constexpr (is_optional<U>::value) {
    return LuaTypeName<typename is_optional<U>::value_type>() + "|nil";
  } else if constexpr (is_sol_as_table<U>::value) {
    return LuaTypeName<typename is_sol_as_table<U>::value_type>();
  } else if constexpr (is_vector<U>::value) {
    return LuaTypeName<typename U::value_type>() + "[]";
  } else if constexpr (is_shared_ptr<U>::value) {
    using Element = typename is_shared_ptr<U>::element_type;
    if constexpr (std::is_void_v<Element>) {
      return "any";
    } else {
      return LuaTypeName<Element>();
    }
  } else if constexpr (std::is_pointer_v<U>) {
    using Pointee = std::remove_cv_t<std::remove_pointer_t<U>>;
    if constexpr (std::is_void_v<Pointee>) {
      return "any";
    } else {
      return LuaTypeName<Pointee>();
    }
  } else if constexpr (has_schema<U>::value) {
    return SchemaLuaTypeName<U>();
  } else {
    const auto alias = AliasTypeName<U>();
    if (!alias.empty()) return alias;
    const auto registered = RegisteredTypeName<U>();
    return registered.empty() ? "any" : registered;
  }
}

template <typename T>
docgen::TypeRef LuaTypeRef() {
  return docgen::TypeRef::Deferred([] {
    return LuaTypeName<T>();
  });
}

template <typename Tuple, std::size_t... I>
void AppendSchemaFields(std::ostringstream &os, const Tuple &fields,
                        std::index_sequence<I...>) {
  bool first = true;
  auto append = [&](const auto &field) {
    if (!first) os << ", ";
    first = false;
    os << field.name << "?: " << SchemaFieldType(field);
  };
  (append(std::get<I>(fields)), ...);
}

template <typename T>
std::string SchemaInlineType() {
  const auto fields = T::schema();
  std::ostringstream os;
  os << "{ ";
  AppendSchemaFields(
      os, fields,
      std::make_index_sequence<std::tuple_size_v<decltype(fields)>>{});
  os << " }";
  return os.str();
}

template <typename Class, typename Arg>
constexpr bool IsSelfArg() {
  using U = stripped_t<Arg>;
  if constexpr (std::is_pointer_v<U>) {
    return std::is_base_of_v<Class, std::remove_pointer_t<U>> ||
           std::is_base_of_v<std::remove_pointer_t<U>, Class>;
  } else {
    return std::is_base_of_v<Class, U> || std::is_base_of_v<U, Class>;
  }
}

template <typename Class, typename Tuple>
constexpr std::size_t CallableParamOffset() {
  if constexpr (std::tuple_size_v<Tuple> == 0) {
    return 0;
  } else {
    using First = std::tuple_element_t<0, Tuple>;
    return IsSelfArg<Class, First>() ? 1 : 0;
  }
}

template <typename Class, typename Func, typename Tuple>
constexpr std::size_t CallableParamOffsetFor() {
  if constexpr (std::is_member_pointer_v<std::decay_t<Func>>) {
    return 0;
  } else {
    return CallableParamOffset<Class, Tuple>();
  }
}

template <typename Tuple, std::size_t I>
void AppendCallableParam(std::vector<docgen::ParamDoc> &docs,
                         const Metadata &metadata, std::size_t metadata_index) {
  docgen::ParamDoc param;
  if (metadata.params && metadata_index < metadata.params->values.size()) {
    const auto &metadata_param = metadata.params->values[metadata_index];
    param.name = metadata_param.name;
    param.doc = metadata_param.doc;
    if (!metadata_param.lua_type.empty()) {
      param.lua_type = docgen::TypeRef::Exact(metadata_param.lua_type);
    }
  } else {
    param.name = "arg" + std::to_string(metadata_index);
  }
  if (!param.lua_type.resolver && param.lua_type.name.empty()) {
    param.lua_type = LuaTypeRef<std::tuple_element_t<I, Tuple>>();
  }
  docs.push_back(std::move(param));
}

template <typename Tuple, std::size_t Offset, std::size_t... I>
std::vector<docgen::ParamDoc> BuildParamDocsImpl(const Metadata &metadata,
                                                 std::index_sequence<I...>) {
  constexpr std::size_t count = sizeof...(I);
  if (metadata.params && metadata.params->values.size() != count) {
    throw std::runtime_error("lv binding metadata parameter count mismatch");
  }

  std::vector<docgen::ParamDoc> docs;
  docs.reserve(count);
  (AppendCallableParam<Tuple, I + Offset>(docs, metadata, I), ...);
  return docs;
}

template <typename Tuple, std::size_t Offset = 0>
std::vector<docgen::ParamDoc> BuildParamDocs(const Metadata &metadata) {
  constexpr std::size_t total = std::tuple_size_v<Tuple>;
  static_assert(Offset <= total);
  return BuildParamDocsImpl<Tuple, Offset>(
      metadata, std::make_index_sequence<total - Offset>{});
}

template <typename T>
bool StackArgMatches(const sol::variadic_args &args, std::size_t offset) {
  sol::stack::record tracking;
  return sol::stack::check<lua_arg_t<T>>(
      args.lua_state(), args.stack_index() + static_cast<int>(offset),
      &sol::no_panic, tracking);
}

template <typename Tuple, std::size_t Offset, std::size_t... I>
bool ArgsMatchImpl(const sol::variadic_args &args, std::index_sequence<I...>) {
  constexpr std::size_t count = sizeof...(I);
  if (args.size() != count) return false;
  return (StackArgMatches<std::tuple_element_t<I + Offset, Tuple>>(args, I) &&
          ...);
}

template <typename Tuple, std::size_t Offset = 0>
bool ArgsMatch(const sol::variadic_args &args) {
  constexpr std::size_t total = std::tuple_size_v<Tuple>;
  static_assert(Offset <= total);
  return ArgsMatchImpl<Tuple, Offset>(
      args, std::make_index_sequence<total - Offset>{});
}

template <typename Tuple, std::size_t Offset, std::size_t... I>
auto GetArgsImpl(const sol::variadic_args &args, std::index_sequence<I...>) {
  return std::tuple<lua_arg_t<std::tuple_element_t<I + Offset, Tuple>>...>(
      args.get<lua_arg_t<std::tuple_element_t<I + Offset, Tuple>>>(I)...);
}

template <typename Tuple, std::size_t Offset = 0>
auto GetArgs(const sol::variadic_args &args) {
  constexpr std::size_t total = std::tuple_size_v<Tuple>;
  static_assert(Offset <= total);
  return GetArgsImpl<Tuple, Offset>(args,
                                    std::make_index_sequence<total - Offset>{});
}

template <typename Func, typename ArgsTuple>
std::optional<sol::object> TryInvokeConstructor(Func func,
                                                const sol::variadic_args &args,
                                                sol::this_state state) {
  if (!ArgsMatch<ArgsTuple>(args)) return std::nullopt;
  auto typed_args = GetArgs<ArgsTuple>(args);
  if constexpr (std::is_void_v<typename function_traits<Func>::return_type>) {
    std::apply(func, typed_args);
    return sol::make_object(state.L, sol::lua_nil);
  } else {
    return std::apply(
        [&](auto &&...values) {
          return sol::make_object(
              state.L,
              std::invoke(func, std::forward<decltype(values)>(values)...));
        },
        typed_args);
  }
}

template <typename Class, typename Func, typename ArgsTuple>
std::optional<sol::object> TryInvokeMethod(Func func, Class &self,
                                           const sol::variadic_args &args,
                                           sol::this_state state) {
  constexpr std::size_t offset =
      CallableParamOffsetFor<Class, Func, ArgsTuple>();
  if (!ArgsMatch<ArgsTuple, offset>(args)) return std::nullopt;
  auto typed_args = GetArgs<ArgsTuple, offset>(args);

  auto invoke = [&](auto &&...values) -> sol::object {
    if constexpr (std::is_member_pointer_v<Func>) {
      if constexpr (std::is_void_v<
                        typename function_traits<Func>::return_type>) {
        std::invoke(func, self, std::forward<decltype(values)>(values)...);
        return sol::make_object(state.L, sol::lua_nil);
      } else {
        return sol::make_object(
            state.L,
            std::invoke(func, self, std::forward<decltype(values)>(values)...));
      }
    } else if constexpr (offset == 1) {
      if constexpr (std::is_void_v<
                        typename function_traits<Func>::return_type>) {
        std::invoke(func, self, std::forward<decltype(values)>(values)...);
        return sol::make_object(state.L, sol::lua_nil);
      } else {
        return sol::make_object(
            state.L,
            std::invoke(func, self, std::forward<decltype(values)>(values)...));
      }
    } else {
      if constexpr (std::is_void_v<
                        typename function_traits<Func>::return_type>) {
        std::invoke(func, std::forward<decltype(values)>(values)...);
        return sol::make_object(state.L, sol::lua_nil);
      } else {
        return sol::make_object(
            state.L,
            std::invoke(func, std::forward<decltype(values)>(values)...));
      }
    }
  };

  return std::apply(invoke, typed_args);
}

template <typename Class, typename Func>
std::vector<docgen::ParamDoc> BuildMethodParamDocs(const Metadata &metadata) {
  using Traits = function_traits<std::decay_t<Func>>;
  using Args = typename Traits::args_tuple;
  constexpr std::size_t offset = CallableParamOffsetFor<Class, Func, Args>();
  return BuildParamDocs<Args, offset>(metadata);
}

template <typename Accessor>
struct property_value_type {
 private:
  using AccessorType = std::decay_t<Accessor>;
  using Traits = function_traits<AccessorType>;
  using Return = typename Traits::return_type;
  using Args = typename Traits::args_tuple;

  template <typename Tuple, std::size_t Size>
  struct arg_selector {
    using type = void;
  };
  template <typename Tuple>
  struct arg_selector<Tuple, 1> {
    using type = std::tuple_element_t<0, Tuple>;
  };
  template <typename Tuple>
  struct arg_selector<Tuple, 2> {
    using type = std::tuple_element_t<1, Tuple>;
  };

 public:
  using type = std::conditional_t<
      std::is_void_v<Return>,
      typename arg_selector<Args, std::tuple_size_v<Args>>::type, Return>;
};

template <typename Tuple, std::size_t Size>
struct property_arg_selector {
  using type = void;
};
template <typename Tuple>
struct property_arg_selector<Tuple, 1> {
  using type = std::tuple_element_t<0, Tuple>;
};
template <typename Tuple>
struct property_arg_selector<Tuple, 2> {
  using type = std::tuple_element_t<1, Tuple>;
};

template <typename R, typename... Args>
struct property_function_value_type {
  using ArgsTuple = std::tuple<Args...>;
  using type = std::conditional_t<
      std::is_void_v<R>,
      typename property_arg_selector<ArgsTuple, sizeof...(Args)>::type, R>;
};

template <typename Class, typename R, typename... Args>
struct property_value_type<R (Class::*)(Args...)>
    : property_function_value_type<R, Args...> {};

template <typename Class, typename R, typename... Args>
struct property_value_type<R (Class::*)(Args...) const>
    : property_function_value_type<R, Args...> {};

template <typename Class, typename R, typename... Args>
struct property_value_type<R (Class::*)(Args...) volatile>
    : property_function_value_type<R, Args...> {};

template <typename Class, typename R, typename... Args>
struct property_value_type<R (Class::*)(Args...) const volatile>
    : property_function_value_type<R, Args...> {};

template <typename Class, typename T>
struct property_value_type<T Class::*> {
  using type = T;
};

template <typename Accessor>
docgen::TypeRef PropertyLuaType(const Metadata &metadata) {
  if (metadata.lua_type) return docgen::TypeRef::Exact(*metadata.lua_type);
  using Value = typename property_value_type<Accessor>::type;
  return LuaTypeRef<Value>();
}

template <typename Value>
std::string LuaLiteral(Value &&value) {
  using U = stripped_t<Value>;
  if constexpr (std::is_same_v<U, bool>) {
    return value ? "true" : "false";
  } else if constexpr (std::is_integral_v<U>) {
    return std::to_string(value);
  } else if constexpr (std::is_enum_v<U>) {
    return std::to_string(static_cast<std::underlying_type_t<U>>(value));
  } else if constexpr (std::is_floating_point_v<U>) {
    std::ostringstream os;
    os << value;
    return os.str();
  } else {
    return "{}";
  }
}

inline void AppendEnumDocs(std::vector<docgen::EnumValueDoc> &) {}

template <typename Value, typename... Rest>
void AppendEnumDocs(std::vector<docgen::EnumValueDoc> &docs, const char *name,
                    Value &&value, Rest &&...rest) {
  docs.push_back(
      docgen::EnumValueDoc{name == nullptr ? std::string{} : std::string(name),
                           LuaLiteral(std::forward<Value>(value))});
  AppendEnumDocs(docs, std::forward<Rest>(rest)...);
}

}  // namespace binding_detail

template <typename... Ts,
          std::enable_if_t<(std::is_convertible_v<Ts, std::string_view> && ...),
                           int> = 0>
Params params(Ts &&...names) {
  return binding_detail::make_params(std::forward<Ts>(names)...);
}

template <
    typename... Ts,
    std::enable_if_t<(std::is_same_v<std::decay_t<Ts>, Param> && ...), int> = 0>
Params params(Ts &&...values) {
  return binding_detail::make_rich_params(std::forward<Ts>(values)...);
}

template <typename Class, typename... Bases>
class BindingBuilder {
 public:
  BindingBuilder(const char *scope_name, const char *lua_name,
                 const char *cpp_name = "")
      : state_(std::make_shared<State>()) {
    state_->load_start = std::chrono::high_resolution_clock::now();
    state_->scope_name = scope_name;
    state_->lua_name = lua_name;
    state_->scope =
        Runtime()[state_->scope_name].template get_or_create<sol::table>();
    if constexpr (sizeof...(Bases) == 0) {
      state_->usertype = state_->scope.template new_usertype<Class>(
          state_->lua_name, sol::no_constructor);
    } else {
      state_->usertype = state_->scope.template new_usertype<Class>(
          state_->lua_name, sol::no_constructor, sol::base_classes,
          sol::bases<Bases...>());
    }
    state_->doc = &docgen::registry().RegisterType(
        std::type_index(typeid(Class)), state_->scope_name, state_->lua_name,
        {}, cpp_name == nullptr ? std::string{} : std::string(cpp_name));
    if constexpr (sizeof...(Bases) > 0) {
      state_->doc->bases = {binding_detail::LuaTypeRef<Bases>()...};
    }
  }

  ~BindingBuilder() {
    if (!state_ || state_->load_recorded) return;
    state_->load_recorded = true;
    const auto end = std::chrono::high_resolution_clock::now();
    const auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
        end - state_->load_start);
    BindingLoadStats::RecordLoadTime(duration.count());
  }

  template <typename... Args, typename... Meta>
  std::enable_if_t<(sizeof...(Args) > 0), BindingBuilder &> constructor(
      Meta &&...meta) {
    auto factory = [](Args... args) {
      return std::make_shared<Class>(std::forward<Args>(args)...);
    };

    const auto metadata =
        binding_detail::ParseMetadata(std::forward<Meta>(meta)...);
    docgen::CallableDoc doc;
    doc.doc = metadata.doc;
    doc.params = binding_detail::BuildParamDocs<std::tuple<Args...>>(metadata);
    doc.return_type = binding_detail::LuaTypeRef<Class>();
    state_->doc->constructors.push_back(std::move(doc));
    AddConstructorInvoker<decltype(factory),
                          typename binding_detail::function_traits<
                              decltype(factory)>::args_tuple>(
        std::move(factory));
    RebindConstructors();
    return *this;
  }

  template <typename Func, typename... Meta,
            typename = std::enable_if_t<!binding_detail::is_metadata_v<Func>>>
  BindingBuilder &constructor(Func &&func, Meta &&...meta) {
    using Traits = binding_detail::function_traits<std::decay_t<Func>>;
    const auto metadata =
        binding_detail::ParseMetadata(std::forward<Meta>(meta)...);
    docgen::CallableDoc doc;
    doc.doc = metadata.doc;
    doc.params =
        binding_detail::BuildParamDocs<typename Traits::args_tuple>(metadata);
    doc.return_type = binding_detail::LuaTypeRef<Class>();
    state_->doc->constructors.push_back(std::move(doc));
    AddConstructorInvoker<std::decay_t<Func>, typename Traits::args_tuple>(
        std::forward<Func>(func));
    RebindConstructors();
    return *this;
  }

  template <typename Func, typename... Meta>
  BindingBuilder &method(const char *name, Func &&func, Meta &&...meta) {
    using Traits = binding_detail::function_traits<std::decay_t<Func>>;
    const auto metadata =
        binding_detail::ParseMetadata(std::forward<Meta>(meta)...);
    docgen::CallableDoc doc;
    doc.name = name;
    doc.doc = metadata.doc;
    doc.params =
        binding_detail::BuildMethodParamDocs<Class, std::decay_t<Func>>(
            metadata);
    doc.return_type =
        metadata.lua_type
            ? docgen::TypeRef::Exact(*metadata.lua_type)
            : binding_detail::LuaTypeRef<typename Traits::return_type>();
    state_->doc->methods.push_back(std::move(doc));
    AddMethodInvoker<std::decay_t<Func>, typename Traits::args_tuple>(
        name, std::forward<Func>(func));
    RebindMethods(name);
    return *this;
  }

  template <typename Accessor, typename... Meta>
  BindingBuilder &property(const char *name, Accessor &&accessor,
                           Meta &&...meta) {
    if constexpr (std::is_member_object_pointer_v<std::decay_t<Accessor>>) {
      state_->usertype[name] = std::forward<Accessor>(accessor);
    } else {
      state_->usertype[name] = sol::property(std::forward<Accessor>(accessor));
    }
    const auto metadata =
        binding_detail::ParseMetadata(std::forward<Meta>(meta)...);
    docgen::PropertyDoc doc;
    doc.name = name;
    doc.doc = metadata.doc;
    doc.lua_type =
        binding_detail::PropertyLuaType<std::decay_t<Accessor>>(metadata);
    state_->doc->properties.push_back(std::move(doc));
    return *this;
  }

  template <typename Getter, typename Setter, typename... Meta,
            typename = std::enable_if_t<!binding_detail::is_metadata_v<Setter>>>
  BindingBuilder &property(const char *name, Getter &&getter, Setter &&setter,
                           Meta &&...meta) {
    state_->usertype[name] = sol::property(std::forward<Getter>(getter),
                                           std::forward<Setter>(setter));
    const auto metadata =
        binding_detail::ParseMetadata(std::forward<Meta>(meta)...);
    docgen::PropertyDoc doc;
    doc.name = name;
    doc.doc = metadata.doc;
    doc.lua_type =
        binding_detail::PropertyLuaType<std::decay_t<Getter>>(metadata);
    state_->doc->properties.push_back(std::move(doc));
    return *this;
  }

  template <typename Key, typename Value>
  BindingBuilder &set(Key &&key, Value &&value) {
    state_->usertype[std::forward<Key>(key)] = std::forward<Value>(value);
    return *this;
  }

 private:
  using ConstructorInvoker = std::function<std::optional<sol::object>(
      sol::variadic_args, sol::this_state)>;
  using MethodInvoker = std::function<std::optional<sol::object>(
      Class &, sol::variadic_args, sol::this_state)>;

  template <typename Func, typename ArgsTuple>
  void AddConstructorInvoker(Func &&func) {
    state_->constructor_invokers.push_back(
        [func = std::decay_t<Func>(std::forward<Func>(func))](
            sol::variadic_args args,
            sol::this_state state) -> std::optional<sol::object> {
          return binding_detail::TryInvokeConstructor<std::decay_t<Func>,
                                                      ArgsTuple>(func, args,
                                                                 state);
        });
  }

  void RebindConstructors() {
    auto state = state_;
    state_->usertype[sol::call_constructor] =
        [state](sol::variadic_args args,
                sol::this_state this_state) -> sol::object {
      for (const auto &invoker : state->constructor_invokers) {
        if (auto result = invoker(args, this_state)) return *result;
      }
      throw std::runtime_error("no matching constructor overload for " +
                               state->doc->full_name);
    };
  }

  template <typename Func, typename ArgsTuple>
  void AddMethodInvoker(const char *name, Func &&func) {
    state_->method_invokers[name].push_back(
        [func = std::decay_t<Func>(std::forward<Func>(func))](
            Class &self, sol::variadic_args args,
            sol::this_state state) -> std::optional<sol::object> {
          return binding_detail::TryInvokeMethod<Class, std::decay_t<Func>,
                                                 ArgsTuple>(func, self, args,
                                                            state);
        });
  }

  void RebindMethods(const char *name) {
    auto state = state_;
    std::string key = name;
    state_->usertype[name] = [state, key](
                                 Class &self, sol::variadic_args args,
                                 sol::this_state this_state) -> sol::object {
      for (const auto &invoker : state->method_invokers.at(key)) {
        if (auto result = invoker(self, args, this_state)) return *result;
      }
      throw std::runtime_error("no matching method overload for " +
                               state->doc->full_name + "." + key);
    };
  }

  struct State {
    std::string scope_name;
    std::string lua_name;
    sol::table scope;
    sol::usertype<Class> usertype;
    docgen::TypeDoc *doc = nullptr;
    std::vector<ConstructorInvoker> constructor_invokers;
    std::unordered_map<std::string, std::vector<MethodInvoker>> method_invokers;
    std::chrono::high_resolution_clock::time_point load_start;
    bool load_recorded = false;
  };

  std::shared_ptr<State> state_;
};

class ModuleBuilder {
 public:
  explicit ModuleBuilder(const char *scope_name)
      : state_(std::make_shared<State>()) {
    state_->load_start = std::chrono::high_resolution_clock::now();
    state_->scope_name = scope_name;
    state_->scope =
        Runtime()[state_->scope_name].template get_or_create<sol::table>();
    state_->doc = &docgen::registry().RegisterModule(state_->scope_name);
  }

  ~ModuleBuilder() {
    if (!state_ || state_->load_recorded) return;
    state_->load_recorded = true;
    const auto end = std::chrono::high_resolution_clock::now();
    const auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
        end - state_->load_start);
    BindingLoadStats::RecordLoadTime(duration.count());
  }

  template <typename Func>
  ModuleBuilder &init(Func &&func) {
    if constexpr (std::is_invocable_v<Func, sol::table>) {
      std::invoke(std::forward<Func>(func), state_->scope);
    } else {
      std::invoke(std::forward<Func>(func));
    }
    return *this;
  }

  template <typename... Meta>
  ModuleBuilder &abstract_class(const char *name, Meta &&...meta) {
    const auto metadata =
        binding_detail::ParseMetadata(std::forward<Meta>(meta)...);

    for (auto &class_doc : state_->doc->classes) {
      if (class_doc.name != name) continue;
      if (!metadata.doc.empty()) class_doc.doc = metadata.doc;
      return *this;
    }

    docgen::ClassDoc doc;
    doc.name = name;
    doc.doc = metadata.doc;
    state_->doc->classes.push_back(std::move(doc));
    return *this;
  }

  template <typename Func, typename... Meta>
  ModuleBuilder &function(const char *name, Func &&func, Meta &&...meta) {
    using Traits = binding_detail::function_traits<std::decay_t<Func>>;
    const auto metadata =
        binding_detail::ParseMetadata(std::forward<Meta>(meta)...);

    docgen::CallableDoc doc;
    doc.name = name;
    doc.doc = metadata.doc;
    doc.params =
        binding_detail::BuildParamDocs<typename Traits::args_tuple>(metadata);
    doc.return_type =
        metadata.lua_type
            ? docgen::TypeRef::Exact(*metadata.lua_type)
            : binding_detail::LuaTypeRef<typename Traits::return_type>();
    state_->doc->functions.push_back(std::move(doc));

    AddFunctionInvoker<std::decay_t<Func>, typename Traits::args_tuple>(
        name, std::forward<Func>(func));
    RebindFunction(name);
    return *this;
  }

  template <typename Value, typename... Meta>
  ModuleBuilder &constant(const char *name, Value &&value, Meta &&...meta) {
    return this->value(name, std::forward<Value>(value),
                       std::forward<Meta>(meta)...);
  }

  template <typename Value, typename... Meta>
  ModuleBuilder &value(const char *name, Value &&value, Meta &&...meta) {
    state_->scope.set(name, std::forward<Value>(value));

    const auto metadata =
        binding_detail::ParseMetadata(std::forward<Meta>(meta)...);
    docgen::ValueDoc doc;
    doc.name = name;
    doc.doc = metadata.doc;
    doc.lua_type = metadata.lua_type
                       ? docgen::TypeRef::Exact(*metadata.lua_type)
                       : binding_detail::LuaTypeRef<std::decay_t<Value>>();
    state_->doc->constants.push_back(std::move(doc));
    return *this;
  }

  template <typename... Values>
  ModuleBuilder &enumeration(const char *name, Values &&...values) {
    state_->scope.new_enum(name, std::forward<Values>(values)...);

    docgen::EnumDoc doc;
    doc.name = name;
    binding_detail::AppendEnumDocs(doc.values, std::forward<Values>(values)...);
    state_->doc->enums.push_back(std::move(doc));
    return *this;
  }

  template <typename Enum, typename... Values>
  ModuleBuilder &enumeration(const char *name, Values &&...values) {
    state_->scope.new_enum(name, std::forward<Values>(values)...);
    docgen::registry().RegisterTypeName(std::type_index(typeid(Enum)),
                                        state_->scope_name, name);

    docgen::EnumDoc doc;
    doc.name = name;
    binding_detail::AppendEnumDocs(doc.values, std::forward<Values>(values)...);
    state_->doc->enums.push_back(std::move(doc));
    return *this;
  }

 private:
  using FunctionInvoker = std::function<std::optional<sol::object>(
      sol::variadic_args, sol::this_state)>;

  template <typename Func, typename ArgsTuple>
  void AddFunctionInvoker(const char *name, Func &&func) {
    state_->function_invokers[name].push_back(
        [func = std::decay_t<Func>(std::forward<Func>(func))](
            sol::variadic_args args,
            sol::this_state state) -> std::optional<sol::object> {
          return binding_detail::TryInvokeConstructor<std::decay_t<Func>,
                                                      ArgsTuple>(func, args,
                                                                 state);
        });
  }

  void RebindFunction(const char *name) {
    auto state = state_;
    std::string key = name;
    state_->scope.set_function(
        name,
        [state, key](sol::variadic_args args,
                     sol::this_state this_state) -> sol::object {
          for (const auto &invoker : state->function_invokers.at(key)) {
            if (auto result = invoker(args, this_state)) return *result;
          }
          throw std::runtime_error("no matching function overload for " +
                                   state->scope_name + "." + key);
        });
  }

  struct State {
    std::string scope_name;
    sol::table scope;
    docgen::ModuleDoc *doc = nullptr;
    std::unordered_map<std::string, std::vector<FunctionInvoker>>
        function_invokers;
    std::chrono::high_resolution_clock::time_point load_start;
    bool load_recorded = false;
  };

  std::shared_ptr<State> state_;
};

}  // namespace lv

// This macro prevents the static registrar object from being optimized out.
#if defined(__GNUC__) || defined(__clang__)
#define LV_KEEP_SYMBOL __attribute__((used))
#else
#define LV_KEEP_SYMBOL
#endif

// Helper macros to generate unique names.
#define LV_CONCAT_IMPL(a, b) a##b
#define LV_CONCAT(a, b) LV_CONCAT_IMPL(a, b)
#define LV_UNIQUE_NAME(prefix) LV_CONCAT(prefix, __LINE__)

#define LV_BINDING(scope_name, ClassName) \
  LV_BINDING_WITH_NAME(scope_name, ClassName, #ClassName)

#define LV_BINDING_WITH_NAME(scope_name, ClassName, LuaName) \
  LV_KEEP_SYMBOL static auto LV_UNIQUE_NAME(lv_binding_) =   \
      ::lv::BindingBuilder<ClassName>(#scope_name, LuaName, #ClassName)

#define LV_BINDING_WITH_BASES(scope_name, ClassName, ...) \
  LV_BINDING_WITH_BASES_AND_NAME(scope_name, ClassName, #ClassName, __VA_ARGS__)

#define LV_BINDING_WITH_BASES_AND_NAME(scope_name, ClassName, LuaName, ...) \
  LV_KEEP_SYMBOL static auto LV_UNIQUE_NAME(lv_binding_) =                  \
      ::lv::BindingBuilder<ClassName, __VA_ARGS__>(#scope_name, LuaName,    \
                                                   #ClassName)

#define LV_MODULE(scope_name)                             \
  LV_KEEP_SYMBOL static auto LV_UNIQUE_NAME(lv_module_) = \
      ::lv::ModuleBuilder(#scope_name)

#define LV_DECLARE_LUA_TYPE_ALIAS(CppType, LuaFullName) \
  namespace lv {                                        \
  template <>                                           \
  struct lua_type_alias<CppType> {                      \
    static std::string name() { return LuaFullName; }   \
  };                                                    \
  }

#define LV_DECLARE_LUA_TYPE(CppType, LuaFullName) \
  LV_DECLARE_LUA_TYPE_ALIAS(CppType, LuaFullName)

#define LV_DECLARE_SCOPED_LUA_TYPE(scope_name, CppType, LuaName) \
  LV_DECLARE_LUA_TYPE(CppType,                                   \
                      std::string(#scope_name) + "." + std::string(LuaName))
