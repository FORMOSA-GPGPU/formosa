// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include <liblv/binding.h>
#include <systemc.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

// A plain sc_module whose children are constructed from Lua. Bindings created
// by the callback become its children in the SystemC object hierarchy instead
// of landing in the global scope.
class Module final : public sc_module {
 public:
  Module(const sc_module_name &name, sol::function construct)
      : sc_module(name) {
    // sc_module's own constructor already pushed this object as the current
    // hierarchy scope, so children built here attach to it directly.
    construct();
  }

  std::string Name() const { return name(); }

  std::string Basename() const { return basename(); }

  sol::as_table_t<std::vector<std::string>> ChildNames() const {
    std::vector<std::string> names;
    names.reserve(get_child_objects().size());
    for (const auto *child : get_child_objects()) {
      names.emplace_back(child->name());
    }
    return sol::as_table(std::move(names));
  }
};

}  // namespace

LV_BINDING(sc, Module)
    .constructor(
        [](const char *name, sol::function construct) {
          return std::make_shared<Module>(name, std::move(construct));
        },
        lv::params(lv::param("name"),
                   lv::param("construct", lv::lua_type("fun()"))),
        lv::doc("Create a SystemC module and run its constructor callback "
                "inside this module's hierarchy scope."))
    .property("name", &Module::Name)
    .property("basename", &Module::Basename)
    .method("child_names", &Module::ChildNames,
            lv::doc("Return the full names of this module's direct child "
                    "objects."));
