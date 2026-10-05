// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include <liblv/binding.h>

#include "address_router.h"

namespace nic {

class Router : public detail::AddressRouter {
 public:
  Router(const sc_module_name &name,
         const std::vector<detail::AddressMapEntry> &addr_map)
      : AddressRouter(name, 1, addr_map) {}
};

LV_BINDING(nic, Router)
    .constructor(
        [](const char *name,
           sol::as_table_t<std::vector<detail::AddressMapEntry>> addr_map) {
          return std::make_shared<Router>(name, addr_map.value());
        },
        lv::params("name", "addr_map"),
        lv::doc("Create an unclocked one-to-many address router"))
    .property("from", &Router::from, lv::doc("Single upstream target binding"))
    .property("to", &Router::set_to,
              lv::doc("Bind the next target in address-map order"));

}  // namespace nic
