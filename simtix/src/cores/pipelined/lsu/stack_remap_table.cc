/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "cores/pipelined/lsu/stack_remap_table.h"

#include <liblv/binding.h>
#include <liblv/output.h>

#include <cstring>

namespace simtix::pipelined {

namespace {

bool IsPowerOfTwo(uint64_t value) {
  return value != 0 && (value & (value - 1)) == 0;
}

}  // namespace

StackRemapTable::StackRemapTable(const sc_module_name &name, const Param &param)
    : sc_module(name),
      region_mask_(~(param.region_size - 1)),
      descriptors_(param.entries, 0),
      sink_("mmio_port") {
  if (param.entries == 0) {
    LV_FATAL("StackRemapTable must contain at least one entry");
  }
  if (!IsPowerOfTwo(param.region_size)) {
    LV_FATAL("StackRemapTable region_size must be a power of two, got {}",
             param.region_size);
  }

  SC_THREAD(HandleRequests);
}

bool StackRemapTable::IsDescriptorValid(uint64_t descriptor) const {
  return (descriptor & kValidBitMask) != 0;
}

uint64_t StackRemapTable::NormalizeDescriptor(uint64_t descriptor) const {
  return (descriptor & kAddressMask & region_mask_) |
         (descriptor & kValidBitMask);
}

bool StackRemapTable::Matches(uint64_t addr) const {
  const uint64_t region_base = addr & region_mask_;
  for (uint64_t descriptor : descriptors_) {
    if (IsDescriptorValid(descriptor) &&
        (descriptor & region_mask_) == region_base) {
      return true;
    }
  }
  return false;
}

void StackRemapTable::HandleRequests() {
  for (;;) {
    auto *trans = sink_.req_port->read();
    const uint64_t addr = trans->get_address();
    const unsigned int size = trans->get_data_length();

    if (size != sizeof(uint64_t) || addr % sizeof(uint64_t) != 0 ||
        addr + size > descriptors_.size() * sizeof(uint64_t)) {
      trans->set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
      sink_.resp_port->write(trans);
      continue;
    }

    uint64_t &descriptor = descriptors_[addr / sizeof(uint64_t)];
    if (trans->is_write()) {
      uint64_t value = 0;
      std::memcpy(&value, trans->get_data_ptr(), sizeof(value));
      descriptor = NormalizeDescriptor(value);
    } else if (trans->is_read()) {
      std::memcpy(trans->get_data_ptr(), &descriptor, sizeof(descriptor));
    } else {
      trans->set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
      sink_.resp_port->write(trans);
      continue;
    }

    trans->set_response_status(tlm::TLM_OK_RESPONSE);
    sink_.resp_port->write(trans);
  }
}

LV_BINDING(simtix, StackRemapTable)
    .constructor(
        [](const char *name, const StackRemapTable::Param &param) {
          return std::make_shared<StackRemapTable>(name, param);
        },
        lv::params("name", "param"),
        lv::doc("Create a firmware-programmable stack remapping table"))
    .property("mmio_port", &StackRemapTable::port,
              lv::doc("Stack remapping table MMIO target"))
    .method("matches", &StackRemapTable::Matches, lv::params("address"),
            lv::doc("Return whether an address matches a valid descriptor"));

}  // namespace simtix::pipelined
