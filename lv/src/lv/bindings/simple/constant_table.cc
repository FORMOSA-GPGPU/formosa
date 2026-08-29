// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include <liblv/binding.h>
#include <liblv/common/tlm_sink.h>
#include <liblv/output.h>
#include <liblv/schema.h>
#include <systemc.h>
#include <tlm_core/tlm_2/tlm_generic_payload/tlm_gp.h>

#include <algorithm>
#include <cstdint>
#include <limits>

namespace simple {

class ConstantTable : public sc_module {
 public:
  struct Entry {
    uint64_t addr = 0;
    size_t size = 0;
    uint64_t value = 0;
    // clang-format off
    LV_SCHEMA(ConstantTable, Entry,
              LV_FIELD(addr, "Address of the constant in the table"),
              LV_FIELD(size, "Size of the constant in bytes"),
              LV_FIELD(value, "Value of the constant"))
    // clang-format on
  };

  struct Param {
    std::vector<Entry> entries;

    // clang-format off
    LV_SCHEMA(ConstantTable, Param,
              LV_FIELD(entries, "Entries in the constant table"))
    // clang-format on
  };

  explicit ConstantTable(const sc_module_name &name, const Param &param)
      : sc_module(name), sink_("sink", [this](tlm::tlm_generic_payload &trans) {
          return ProcessRequest(&trans);
        }) {
    ValidateEntries(param.entries);

    for (const auto &entry : param.entries) {
      offset_map_[entry.addr] = {entry.size, entry.value};
    }
    SC_THREAD(ProcessRequestThread);
  }

  void ProcessRequestThread() {
    while (true) {
      // blocking call to get a request
      tlm::tlm_generic_payload *trans = sink_.req_port->read();
      ProcessRequest(trans);
      sink_.resp_port->write(trans);
    }
  }

  unsigned int ProcessRequest(tlm::tlm_generic_payload *trans) {
    tlm::tlm_command cmd = trans->get_command();
    sc_dt::uint64 addr = trans->get_address();
    unsigned char *ptr = trans->get_data_ptr();
    unsigned int len = trans->get_data_length();

    if (cmd == tlm::TLM_READ_COMMAND) {
      auto it = offset_map_.find(addr);
      if (it == offset_map_.end() || it->second.first != len) {
        trans->set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
        SC_REPORT_ERROR("TLM-2",
                        "Illegal address or length received by constant table");
        return 0;
      } else {
        std::memcpy(ptr, &it->second.second, len);
      }
    } else {
      trans->set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
      SC_REPORT_ERROR("TLM-2",
                      "Illegal transaction command received by constant table");
      return 0;
    }

    trans->set_response_status(tlm::TLM_OK_RESPONSE);
    return len;
  }

  auto port() const { return &sink_.port; }

 private:
  lv::TlmSink sink_;

  std::unordered_map<uint64_t, std::pair<size_t, uint64_t>> offset_map_;

  static constexpr std::array<size_t, 4> valid_sizes_ = {1, 2, 4, 8};

  static void ValidateEntries(const std::vector<Entry> &entries) {
    for (const auto &entry : entries) {
      // Validate size
      if (std::find(valid_sizes_.begin(), valid_sizes_.end(), entry.size) ==
          valid_sizes_.end()) {
        // keep on shim: valid_sizes_ is a std::array, which has no Quill codec
        // for lazy queue encoding, so the LV_FATAL macro can't take it. Cold
        // validation path, so eager fmt::format via the function shim is fine.
        lv::Fatal(
            "Invalid size for ConstantTable entry at address=0x{:x}, expected "
            "one of {}",
            entry.addr, valid_sizes_);
      }

      // Validate address alignment
      if (entry.addr % entry.size != 0) {
        LV_FATAL(
            "ConstantTable entry address 0x{:x} must be aligned to its "
            "size {} bytes",
            entry.addr, entry.size);
      }

      // Validate address + size does not overflow
      if (entry.addr > std::numeric_limits<uint64_t>::max() - entry.size) {
        LV_FATAL(
            "ConstantTable entry at address=0x{:x} with size {} bytes "
            "causes address overflow",
            entry.addr, entry.size);
      }

      // Validate value fits in the specified size
      if (entry.size < 8) {
        const size_t bits = entry.size * 8;
        const uint64_t bit_mask = (uint64_t{1} << bits) - 1;
        if ((entry.value & ~bit_mask) != 0) {
          LV_FATAL(
              "Constant value 0x{:x} at address=0x{:x} does not fit in {} "
              "bytes",
              entry.value, entry.addr, entry.size);
        }
      }
    }

    std::vector<Entry> sorted_entries = entries;
    std::sort(sorted_entries.begin(), sorted_entries.end(),
              [](const Entry &a, const Entry &b) {
                return a.addr < b.addr;
              });

    for (size_t i = 1; i < sorted_entries.size(); ++i) {
      const auto &prev = sorted_entries[i - 1];
      const auto &curr = sorted_entries[i];
      const uint64_t prev_end = prev.addr + prev.size;
      if (curr.addr < prev_end) {
        LV_FATAL(
            "Overlapping ConstantTable entries: "
            "[0x{:x}, 0x{:x}) overlaps with [0x{:x}, 0x{:x})",
            prev.addr, prev_end, curr.addr, curr.addr + curr.size);
      }
    }
  }
};

LV_BINDING(simple, ConstantTable)
    .constructor(
        [](const char *name, ConstantTable::Param param) {
          return std::make_shared<ConstantTable>(name, param);
        },
        lv::params("name", "param"),
        lv::doc("Create a read-only constant table"))
    .property("port", &ConstantTable::port,
              lv::doc("Constant table request port"));

}  // namespace simple
