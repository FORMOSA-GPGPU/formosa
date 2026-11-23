// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include <liblv/binding.h>
#include <liblv/log.h>
#include <liblv/schema.h>
#include <systemc.h>
#include <tlm.h>
#include <tlm_utils/simple_target_socket.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <sol/sol.hpp>
#include <string>
#include <vector>

namespace dbg {

namespace {

/**
 * @brief A byte memory whose DMI behaviour a test controls and observes. A
 * test-only binding, built into lv from lv/tests.
 *
 * Unlike a model memory, it can deny DMI or grant it read-only, gives reads
 * and writes their own latency, counts the transactions and DMI requests that
 * reach it, and can move to fresh backing storage after invalidating every
 * grant. The storage it moves from stays allocated with its old contents, so
 * an initiator that keeps using an invalidated pointer reads stale data
 * instead of crashing. b_transport adds the access latency without waiting,
 * and on_access reports each transaction from inside the call.
 */
class TestMemory : public sc_module {
 public:
  using Bytes = sol::as_table_t<std::vector<uint8_t>>;

  struct Param {
    uint64_t size = 0x1000;
    sc_time read_latency = SC_ZERO_TIME;
    sc_time write_latency = SC_ZERO_TIME;
    std::string dmi = "read_write";
    std::optional<sol::safe_function> on_access;
    // clang-format off
    LV_SCHEMA(TestMemory, Param,
              LV_FIELD(size, "Size in bytes (non-zero)"),
              LV_FIELD(read_latency,
                       "Latency of a read, annotated and granted"),
              LV_FIELD(write_latency,
                       "Latency of a write, annotated and granted"),
              LV_FIELD(dmi,
                       "DMI policy: read_write, read_only, or none to deny "
                       "requests and clear the DMI hint"),
              LV_FIELD(on_access,
                       "Called as on_access(write, addr, data, delta) for each "
                       "transaction after its data moved, with the delta "
                       "count at the call. It runs inside the transport call "
                       "and must not wait"))
    // clang-format on
  };

  TestMemory(const sc_module_name &name, const Param &param)
      : sc_module(name),
        socket_("port"),
        read_latency_(param.read_latency),
        write_latency_(param.write_latency),
        on_access_(param.on_access) {
    if (param.size == 0) LV_FATAL("{}: size must be non-zero", this->name());
    if (param.dmi == "read_write") {
      dmi_ = Dmi::kReadWrite;
    } else if (param.dmi == "read_only") {
      dmi_ = Dmi::kReadOnly;
    } else if (param.dmi == "none") {
      dmi_ = Dmi::kNone;
    } else {
      LV_FATAL("{}: unknown DMI policy {}", this->name(), param.dmi);
    }
    storages_.push_back(std::vector<unsigned char>(param.size, 0));
    socket_.register_b_transport(this, &TestMemory::Blocking);
    socket_.register_get_direct_mem_ptr(this, &TestMemory::Grant);
  }

  tlm_utils::simple_target_socket<TestMemory> *port() { return &socket_; }
  unsigned int transactions() const { return transactions_; }
  unsigned int dmi_requests() const { return dmi_requests_; }

  void write_bytes(uint64_t addr, const Bytes &data) {
    Bounds(addr, data.value().size(), "write_bytes");
    std::copy(data.value().begin(), data.value().end(),
              Current().begin() + addr);
  }

  /** @brief Invalidate every grant, then move to a copy of the storage. */
  void replace() {
    // Pointer validity ends over the whole allocation before the new one is
    // published.
    socket_->invalidate_direct_mem_ptr(0, Current().size() - 1);
    // Copy before push_back: growing the list may move the source vector.
    std::vector<unsigned char> copy = Current();
    storages_.push_back(std::move(copy));
  }

 private:
  enum class Dmi { kReadWrite, kReadOnly, kNone };

  // Each storage keeps its heap buffer when the list grows, so pointers into
  // a replaced one remain valid.
  std::vector<unsigned char> &Current() { return storages_.back(); }
  const std::vector<unsigned char> &Current() const { return storages_.back(); }

  void Bounds(uint64_t addr, uint64_t size, const char *what) const {
    if (addr > Current().size() || size > Current().size() - addr) {
      LV_FATAL("{}: {} {:#x}+{} is out of bounds", name(), what, addr, size);
    }
  }

  void Blocking(tlm::tlm_generic_payload &trans, sc_time &delay) {
    ++transactions_;
    const uint64_t addr = trans.get_address();
    const unsigned int length = trans.get_data_length();
    trans.set_dmi_allowed(false);
    if (addr > Current().size() || length > Current().size() - addr) {
      trans.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
      return;
    }
    if (!trans.is_read() && !trans.is_write()) {
      trans.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
      return;
    }
    unsigned char *memory = Current().data() + addr;
    if (trans.is_read()) {
      std::copy(memory, memory + length, trans.get_data_ptr());
    } else {
      std::copy(trans.get_data_ptr(), trans.get_data_ptr() + length, memory);
    }
    delay += trans.is_read() ? read_latency_ : write_latency_;
    trans.set_dmi_allowed(dmi_ != Dmi::kNone);
    trans.set_response_status(tlm::TLM_OK_RESPONSE);
    if (on_access_) Report(trans);
  }

  bool Grant(tlm::tlm_generic_payload &trans, tlm::tlm_dmi &dmi) {
    ++dmi_requests_;
    if (dmi_ == Dmi::kNone) {
      trans.set_dmi_allowed(false);
      return false;
    }
    dmi.set_dmi_ptr(Current().data());
    dmi.set_start_address(0);
    dmi.set_end_address(Current().size() - 1);
    if (dmi_ == Dmi::kReadOnly) {
      dmi.allow_read();
    } else {
      dmi.allow_read_write();
    }
    dmi.set_read_latency(read_latency_);
    dmi.set_write_latency(write_latency_);
    trans.set_dmi_allowed(true);
    return true;
  }

  void Report(const tlm::tlm_generic_payload &trans) {
    sol::state_view lua(on_access_->lua_state());
    const unsigned int length = trans.get_data_length();
    sol::table bytes = lua.create_table(static_cast<int>(length), 0);
    for (unsigned int i = 0; i < length; ++i) {
      bytes[i + 1] = trans.get_data_ptr()[i];
    }
    sol::protected_function_result result =
        (*on_access_)(trans.is_write(), trans.get_address(), bytes,
                      static_cast<uint64_t>(sc_delta_count()));
    if (!result.valid()) {
      sol::error err = result;
      LV_FATAL("{}: on_access failed: {}", name(), err.what());
    }
  }

  tlm_utils::simple_target_socket<TestMemory> socket_;
  const sc_time read_latency_;
  const sc_time write_latency_;
  Dmi dmi_ = Dmi::kReadWrite;
  std::optional<sol::safe_function> on_access_;
  std::vector<std::vector<unsigned char>> storages_;
  unsigned int transactions_ = 0;
  unsigned int dmi_requests_ = 0;
};

LV_BINDING(dbg, TestMemory)
    .constructor(
        [](const char *name, const TestMemory::Param &param) {
          return std::make_shared<TestMemory>(name, param);
        },
        lv::params("name", "param"),
        lv::doc("Create a byte memory whose DMI behaviour a test controls"))
    .property("port", &TestMemory::port, lv::doc("TLM target socket"))
    .property("transactions", &TestMemory::transactions,
              lv::doc("Number of b_transport calls received"))
    .property("dmi_requests", &TestMemory::dmi_requests,
              lv::doc("Number of DMI requests received"))
    .method("write_bytes", &TestMemory::write_bytes, lv::params("addr", "data"),
            lv::doc("Write the current storage without transport"))
    .method("replace", &TestMemory::replace,
            lv::doc("Invalidate every DMI grant, then move to a copy of the "
                    "storage; the old storage keeps its contents"));

}  // namespace

}  // namespace dbg
