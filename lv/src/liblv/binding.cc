// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include <fmt/format.h>
#include <liblv/binding.h>

#include <algorithm>
#include <iterator>

namespace lv::docgen {
namespace {

std::string FullName(std::string_view scope, std::string_view lua_name) {
  if (scope.empty()) return std::string(lua_name);
  return std::string(scope) + "." + std::string(lua_name);
}

void WriteDocComment(std::string &out, const std::string &doc) {
  if (doc.empty()) return;
  fmt::format_to(std::back_inserter(out), "---{}\n", doc);
}

void WriteNamespace(std::string &out, const std::string &scope) {
  if (scope.empty()) return;
  fmt::format_to(std::back_inserter(out), "{} = {} or {{}}\n\n", scope, scope);
}

void WriteNamespaceOnce(std::string &out, std::vector<std::string> &seen,
                        const std::string &scope) {
  if (scope.empty()) return;
  if (std::find(seen.begin(), seen.end(), scope) != seen.end()) return;
  WriteNamespace(out, scope);
  seen.push_back(scope);
}

std::string FullNameForMember(std::string_view scope, std::string_view name) {
  if (scope.empty()) return std::string(name);
  return std::string(scope) + "." + std::string(name);
}

std::string ResolveType(const TypeRef &type, std::string_view fallback) {
  const auto resolved = type.Resolve();
  return resolved.empty() ? std::string(fallback) : resolved;
}

std::string ResolvePrimaryBase(const std::vector<TypeRef> &bases) {
  for (const auto &base : bases) {
    const auto resolved = ResolveType(base, "");
    if (!resolved.empty() && resolved != "any") return resolved;
  }
  return {};
}

void WriteParams(std::string &out, const std::vector<ParamDoc> &params) {
  for (const auto &param : params) {
    fmt::format_to(std::back_inserter(out), "---@param {} {}", param.name,
                   ResolveType(param.lua_type, "any"));
    if (!param.doc.empty()) {
      fmt::format_to(std::back_inserter(out), " @{}", param.doc);
    }
    out.push_back('\n');
  }
}

void WriteReturn(std::string &out, const TypeRef &return_type) {
  const auto resolved = ResolveType(return_type, "");
  if (!resolved.empty()) {
    fmt::format_to(std::back_inserter(out), "---@return {}\n", resolved);
  }
}

void WriteCallable(std::string &out, const std::string &prefix,
                   const CallableDoc &callable) {
  WriteDocComment(out, callable.doc);
  WriteParams(out, callable.params);
  WriteReturn(out, callable.return_type);
  fmt::format_to(std::back_inserter(out), "function {}(", prefix);
  for (std::size_t i = 0; i < callable.params.size(); ++i) {
    if (i != 0) out += ", ";
    out += callable.params[i].name;
  }
  out += ") end\n\n";
}

void WriteOverload(std::string &out, const CallableDoc &callable) {
  WriteDocComment(out, callable.doc);
  out += "---@overload fun(";
  for (std::size_t i = 0; i < callable.params.size(); ++i) {
    const auto &param = callable.params[i];
    if (i != 0) out += ", ";
    fmt::format_to(std::back_inserter(out), "{}: {}", param.name,
                   ResolveType(param.lua_type, "any"));
  }
  out.push_back(')');
  const auto return_type = ResolveType(callable.return_type, "");
  if (!return_type.empty()) {
    fmt::format_to(std::back_inserter(out), ": {}", return_type);
  }
  out.push_back('\n');
}

void WriteConstant(std::string &out, const std::string &scope,
                   const ValueDoc &constant) {
  WriteDocComment(out, constant.doc);
  fmt::format_to(std::back_inserter(out), "---@type {}\n",
                 ResolveType(constant.lua_type, "any"));
  fmt::format_to(std::back_inserter(out), "{} = nil\n\n",
                 FullNameForMember(scope, constant.name));
}

std::string RootScope(std::string_view full_name) {
  const auto dot = full_name.find('.');
  if (dot == std::string_view::npos) return {};
  return std::string(full_name.substr(0, dot));
}

void WriteSchema(std::string &out, const SchemaDoc &schema) {
  fmt::format_to(std::back_inserter(out), "---@class {}\n", schema.full_name);
  for (const auto &field : schema.fields) {
    fmt::format_to(std::back_inserter(out), "---@field {}", field.name);
    if (field.optional) out.push_back('?');
    fmt::format_to(std::back_inserter(out), " {}",
                   ResolveType(field.lua_type, "any"));
    if (!field.doc.empty()) {
      fmt::format_to(std::back_inserter(out), " @{}", field.doc);
    }
    out.push_back('\n');
  }
  fmt::format_to(std::back_inserter(out), "{} = {{}}\n\n", schema.full_name);
}

void WriteAbstractClass(std::string &out, const std::string &scope,
                        const ClassDoc &class_doc) {
  WriteDocComment(out, class_doc.doc);
  const auto full_name = FullNameForMember(scope, class_doc.name);
  fmt::format_to(std::back_inserter(out), "---@class {}\n", full_name);
  fmt::format_to(std::back_inserter(out), "{} = {{}}\n\n", full_name);
}

void WriteEnum(std::string &out, const std::string &scope,
               const EnumDoc &enum_doc) {
  const auto full_name = FullNameForMember(scope, enum_doc.name);
  fmt::format_to(std::back_inserter(out), "---@enum {}\n", full_name);
  fmt::format_to(std::back_inserter(out), "{} = {{\n", full_name);
  for (const auto &value : enum_doc.values) {
    fmt::format_to(std::back_inserter(out), "  {} = {},\n", value.name,
                   value.value);
  }
  out += "}\n\n";
}

template <typename PrefixFn>
void WriteCallableGroups(std::string &out,
                         const std::vector<CallableDoc> &callables,
                         PrefixFn prefix_for_name) {
  std::vector<std::string> emitted_names;
  for (const auto &callable : callables) {
    if (std::find(emitted_names.begin(), emitted_names.end(), callable.name) !=
        emitted_names.end()) {
      continue;
    }

    std::vector<const CallableDoc *> group;
    for (const auto &candidate : callables) {
      if (candidate.name == callable.name) group.push_back(&candidate);
    }

    const auto prefix = prefix_for_name(callable.name);
    if (group.size() == 1) {
      WriteCallable(out, prefix, *group.front());
    } else {
      for (const auto *overload : group) {
        WriteOverload(out, *overload);
      }
      fmt::format_to(std::back_inserter(out), "function {}(...) end\n\n",
                     prefix);
    }

    emitted_names.push_back(callable.name);
  }
}

}  // namespace

TypeDoc &Registry::RegisterType(std::type_index cpp_type, std::string scope,
                                std::string lua_name, std::string doc,
                                std::string cpp_name) {
  if (auto *type = FindType(cpp_type)) {
    type->scope = std::move(scope);
    type->lua_name = std::move(lua_name);
    type->full_name = FullName(type->scope, type->lua_name);
    RegisterTypeName(cpp_type, type->scope, type->lua_name,
                     std::move(cpp_name));
    if (!doc.empty()) type->doc = std::move(doc);
    return *type;
  }

  TypeDoc type;
  type.cpp_type = cpp_type;
  type.scope = std::move(scope);
  type.lua_name = std::move(lua_name);
  type.full_name = FullName(type.scope, type.lua_name);
  type.doc = std::move(doc);
  RegisterTypeName(cpp_type, type.scope, type.lua_name, std::move(cpp_name));
  types_.push_back(std::move(type));
  return types_.back();
}

void Registry::RegisterTypeName(std::type_index cpp_type, std::string scope,
                                std::string lua_name, std::string cpp_name) {
  const auto full_name = FullName(scope, lua_name);
  auto it = std::find_if(type_names_.begin(), type_names_.end(),
                         [&](const TypeName &type) {
                           return type.cpp_type == cpp_type;
                         });
  if (it != type_names_.end()) {
    it->full_name = full_name;
    if (!cpp_name.empty()) it->cpp_name = std::move(cpp_name);
    return;
  }

  TypeName type_name;
  type_name.cpp_type = cpp_type;
  type_name.cpp_name = std::move(cpp_name);
  type_name.full_name = full_name;
  type_names_.push_back(std::move(type_name));
}

SchemaDoc &Registry::RegisterSchema(std::type_index cpp_type,
                                    std::string full_name,
                                    std::vector<SchemaFieldDoc> fields) {
  auto name_it = std::find_if(
      schemas_.begin(), schemas_.end(), [&](const SchemaDoc &schema) {
        return schema.full_name == full_name && schema.cpp_type != cpp_type;
      });
  if (name_it != schemas_.end()) {
    throw std::runtime_error("duplicate Lua schema name: " + full_name);
  }

  auto it = std::find_if(schemas_.begin(), schemas_.end(),
                         [&](const SchemaDoc &schema) {
                           return schema.cpp_type == cpp_type;
                         });
  if (it != schemas_.end()) {
    it->full_name = std::move(full_name);
    if (!fields.empty()) it->fields = std::move(fields);
    return *it;
  }

  SchemaDoc schema;
  schema.cpp_type = cpp_type;
  schema.full_name = std::move(full_name);
  schema.fields = std::move(fields);
  schemas_.push_back(std::move(schema));
  return schemas_.back();
}

ModuleDoc &Registry::RegisterModule(std::string scope) {
  auto it = std::find_if(modules_.begin(), modules_.end(),
                         [&](const ModuleDoc &module) {
                           return module.scope == scope;
                         });
  if (it != modules_.end()) return *it;

  ModuleDoc module;
  module.scope = std::move(scope);
  modules_.push_back(std::move(module));
  return modules_.back();
}

TypeDoc *Registry::FindType(std::type_index cpp_type) {
  auto it =
      std::find_if(types_.begin(), types_.end(), [&](const TypeDoc &type) {
        return type.cpp_type == cpp_type;
      });
  return it == types_.end() ? nullptr : &*it;
}

const TypeDoc *Registry::FindType(std::type_index cpp_type) const {
  auto it =
      std::find_if(types_.begin(), types_.end(), [&](const TypeDoc &type) {
        return type.cpp_type == cpp_type;
      });
  return it == types_.end() ? nullptr : &*it;
}

std::string Registry::FindTypeName(std::type_index cpp_type) const {
  auto it = std::find_if(type_names_.begin(), type_names_.end(),
                         [&](const TypeName &type) {
                           return type.cpp_type == cpp_type;
                         });
  return it == type_names_.end() ? std::string{} : it->full_name;
}

std::string Registry::FindTypeName(std::string_view cpp_name) const {
  auto it = std::find_if(type_names_.begin(), type_names_.end(),
                         [&](const TypeName &type) {
                           return type.cpp_name == cpp_name;
                         });
  return it == type_names_.end() ? std::string{} : it->full_name;
}

std::string Registry::FindSchemaName(std::type_index cpp_type) const {
  auto it = std::find_if(schemas_.begin(), schemas_.end(),
                         [&](const SchemaDoc &schema) {
                           return schema.cpp_type == cpp_type;
                         });
  return it == schemas_.end() ? std::string{} : it->full_name;
}

Registry &registry() {
  static Registry registry;
  return registry;
}

std::string GenerateLuaLS() {
  std::string out;
  out += "---@meta\n\n";

  std::vector<std::string> seen_scopes;
  for (const auto &module : registry().modules()) {
    WriteNamespaceOnce(out, seen_scopes, module.scope);
    for (const auto &class_doc : module.classes) {
      WriteAbstractClass(out, module.scope, class_doc);
    }
    for (const auto &enum_doc : module.enums) {
      WriteEnum(out, module.scope, enum_doc);
    }
    for (const auto &constant : module.constants) {
      WriteConstant(out, module.scope, constant);
    }
    WriteCallableGroups(out, module.functions, [&](const std::string &name) {
      return FullName(module.scope, name);
    });
  }

  for (const auto &type : registry().types()) {
    WriteNamespaceOnce(out, seen_scopes, type.scope);

    WriteDocComment(out, type.doc);
    const auto base = ResolvePrimaryBase(type.bases);
    if (base.empty()) {
      fmt::format_to(std::back_inserter(out), "---@class {}\n", type.full_name);
    } else {
      fmt::format_to(std::back_inserter(out), "---@class {} : {}\n",
                     type.full_name, base);
    }
    for (const auto &property : type.properties) {
      fmt::format_to(std::back_inserter(out), "---@field {} {}", property.name,
                     ResolveType(property.lua_type, "any"));
      if (!property.doc.empty()) {
        fmt::format_to(std::back_inserter(out), " @{}", property.doc);
      }
      out.push_back('\n');
    }
    for (const auto &ctor : type.constructors) {
      WriteOverload(out, ctor);
    }
    fmt::format_to(std::back_inserter(out), "{} = {{}}\n\n", type.full_name);

    WriteCallableGroups(out, type.methods, [&](const std::string &name) {
      return type.full_name + ":" + name;
    });
  }

  std::size_t schema_index = 0;
  while (schema_index < registry().schemas().size()) {
    const auto schema = registry().schemas()[schema_index++];
    WriteNamespaceOnce(out, seen_scopes, RootScope(schema.full_name));
    WriteSchema(out, schema);
  }

  return out;
}

}  // namespace lv::docgen
