// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include <liblv/binding.h>

#include "address_router.h"

namespace nic {

class XBar : public detail::AddressRouter {
 public:
  XBar(const sc_module_name &name, unsigned int num_masters,
       const std::vector<detail::AddressMapEntry> &addr_map)
      : AddressRouter(name, num_masters, addr_map) {}
};

LV_BINDING(nic, XBar)
    .constructor(
        [](const char *name, unsigned int num_masters,
           sol::as_table_t<std::vector<detail::AddressMapEntry>> addr_map) {
          return std::make_shared<XBar>(name, num_masters, addr_map.value());
        },
        lv::params("name", "num_masters", "addr_map"),
        lv::doc("Create an unclocked buffered crossbar; no clock is required"))
    .property("core_side", &XBar::from,
              lv::doc("Multi-target socket; bind each master directly"))
    .property("mem_side", &XBar::set_to,
              lv::doc("Bind the next target in address-map order"));

}  // namespace nic
