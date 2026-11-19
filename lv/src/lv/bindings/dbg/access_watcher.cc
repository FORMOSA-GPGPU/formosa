// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include <liblv/binding.h>
#include <liblv/common/annotation.h>
#include <liblv/log.h>
#include <liblv/schema.h>
#include <systemc.h>
#include <tlm.h>
#include <tlm_utils/simple_target_socket.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "pass_through.h"

namespace dbg {

namespace {

/**
 * @brief Watches the writes crossing a TLM link, like a logic-analyzer probe.
 *
 * A write that overlaps a watched address range is reported when the target's
 * OK response to it crosses the link, either as BEGIN_RESP or as TLM_COMPLETED
 * returned for BEGIN_REQ; by then the write has taken effect. Inserting the
 * watcher and reporting consume no simulated time.
 *
 * A streaming write, whose streaming width is below its data length,
 * writes its first streaming-width bytes again and again. The watcher
 * matches that window against the watched ranges and reports the write
 * once per beat, each report with the write's address and that beat's
 * bytes.
 *
 * Calls pass through dbg::PassThrough, which describes the cases it does not
 * fully support. on_write runs at the current simulation time, so it misses
 * the annotated delay of a write, whether the write came by b_transport or its
 * response carried a delay, and a DMI request through the watcher is
 * refused.
 */
class AccessWatcher : public sc_module {
 public:
  /** @brief Target socket type that the outgoing socket binds to. */
  using Target = PassThrough::Target;

  /** @brief Address range, as seen on the link. */
  struct Range {
    uint64_t addr = 0;
    uint64_t size = 0;
    // clang-format off
    LV_SCHEMA(AccessWatcher, Range,
              LV_FIELD(addr, "First address of the range, as seen on the link"),
              LV_FIELD(size, "Size of the range in bytes (non-zero)"))
    // clang-format on
  };

  /** @brief Watched ranges and the Lua callback that reports writes. */
  struct Param {
    std::vector<Range> watch;
    std::optional<sol::safe_function> on_write;
    // clang-format off
    LV_SCHEMA(AccessWatcher, Param,
              LV_FIELD(watch, "Address ranges whose writes are reported"),
              LV_FIELD(on_write,
                       "Called as on_write(addr, data, enables, annotation) "
                       "when the target responds OK to a write overlapping a "
                       "watched range. addr and data describe the whole "
                       "write; enables is nil unless the payload carries byte "
                       "enables; annotation is the text of the write's "
                       "lv::Annotation, or nil. It runs inside the transport "
                       "call and must not wait. A streaming write is reported "
                       "once per beat, each with the write's address"))
    // clang-format on
  };

  /**
   * @brief Create a watcher.
   *
   * @param name Module name.
   * @param param Watched ranges and the callback.
   */
  AccessWatcher(const sc_module_name &name, const Param &param);

  /** @brief Return the incoming socket that initiators bind to. */
  tlm_utils::simple_target_socket<PassThrough> *from() { return &link_.from; }

  /**
   * @brief Bind the outgoing socket to a target.
   *
   * @param target Target socket that receives the passed-on calls.
   */
  void set_to(Target *target) { link_.bind(target); }

  /**
   * @brief Return the target given to set_to().
   *
   * @return The bound target, or nullptr before set_to().
   */
  Target *to() const { return link_.target(); }

 private:
  void Observe(const tlm::tlm_generic_payload &trans,
               const tlm::tlm_phase &phase,
               std::optional<tlm::tlm_sync_enum> status);
  bool HitsWatchedRange(const tlm::tlm_generic_payload &trans) const;
  void ReportWrite(const tlm::tlm_generic_payload &trans);
  void ReportBeat(const tlm::tlm_generic_payload &trans, unsigned int offset,
                  unsigned int len);

  std::vector<Range> watch_;
  std::optional<sol::safe_function> on_write_;
  PassThrough link_;
};

// The bytes one beat of a write covers from its address. A streaming width
// below the data length makes the address repeat, as for a FIFO register.
unsigned int BeatLength(const tlm::tlm_generic_payload &trans) {
  const unsigned int len = trans.get_data_length();
  const unsigned int width = trans.get_streaming_width();
  return width != 0 && width < len ? width : len;
}

AccessWatcher::AccessWatcher(const sc_module_name &name, const Param &param)
    : sc_module(name),
      watch_(param.watch),
      on_write_(param.on_write),
      link_("link",
            [this](tlm::tlm_generic_payload &trans, const tlm::tlm_phase &phase,
                   std::optional<tlm::tlm_sync_enum> status, const sc_time &) {
              Observe(trans, phase, status);
            }) {
  for (const auto &range : watch_) {
    if (range.size == 0 || range.addr + (range.size - 1) < range.addr) {
      LV_FATAL("{}: watch range {:#x}+{:#x} must be non-empty and not wrap",
               this->name(), range.addr, range.size);
    }
  }
}

void AccessWatcher::Observe(const tlm::tlm_generic_payload &trans,
                            const tlm::tlm_phase &phase,
                            std::optional<tlm::tlm_sync_enum> status) {
  // The response first crosses the link as BEGIN_RESP, or as TLM_COMPLETED
  // returned for BEGIN_REQ when the target completes the write at once.
  // TLM_COMPLETED returned for a later phase follows a reported response.
  const bool response =
      (phase == tlm::BEGIN_RESP && status != tlm::TLM_COMPLETED) ||
      (phase == tlm::BEGIN_REQ && status == tlm::TLM_COMPLETED);
  if (!response || !on_write_ || !trans.is_write() || !trans.is_response_ok() ||
      !HitsWatchedRange(trans)) {
    return;
  }
  ReportWrite(trans);
}

bool AccessWatcher::HitsWatchedRange(
    const tlm::tlm_generic_payload &trans) const {
  const uint64_t begin = trans.get_address();
  const uint64_t len = BeatLength(trans);
  if (len == 0) return false;
  // Compare against last bytes so that no sum can overflow.
  for (const auto &range : watch_) {
    const uint64_t range_last = range.addr + (range.size - 1);
    if (begin <= range_last &&
        (range.addr <= begin || range.addr - begin < len)) {
      return true;
    }
  }
  return false;
}

void AccessWatcher::ReportWrite(const tlm::tlm_generic_payload &trans) {
  const unsigned int len = trans.get_data_length();
  const unsigned int beat = BeatLength(trans);
  for (unsigned int offset = 0; offset < len; offset += beat) {
    ReportBeat(trans, offset, std::min(beat, len - offset));
  }
}

void AccessWatcher::ReportBeat(const tlm::tlm_generic_payload &trans,
                               unsigned int offset, unsigned int len) {
  // Lua gets copies: the payload is reset and reused once released.
  const unsigned char *data = trans.get_data_ptr() + offset;
  sol::state_view lua(on_write_->lua_state());
  sol::table bytes = lua.create_table(static_cast<int>(len), 0);
  for (unsigned int i = 0; i < len; ++i) bytes[i + 1] = data[i];

  sol::object enables = sol::make_object(lua, sol::lua_nil);
  const unsigned char *be = trans.get_byte_enable_ptr();
  const unsigned int be_len = trans.get_byte_enable_length();
  if (be != nullptr && be_len != 0) {
    sol::table mask = lua.create_table(static_cast<int>(len), 0);
    for (unsigned int i = 0; i < len; ++i) {
      mask[i + 1] = be[(offset + i) % be_len] == TLM_BYTE_ENABLED;
    }
    enables = mask;
  }

  sol::object annotation = sol::make_object(lua, sol::lua_nil);
  if (const auto *ext = trans.get_extension<lv::Annotation>()) {
    annotation = sol::make_object(lua, ext->text);
  }

  sol::protected_function_result result =
      (*on_write_)(trans.get_address(), bytes, enables, annotation);
  if (!result.valid()) {
    sol::error err = result;
    LV_FATAL("{}: on_write failed: {}", name(), err.what());
  }
}

LV_BINDING(dbg, AccessWatcher)
    .constructor(
        [](const char *name, const AccessWatcher::Param &param) {
          return std::make_shared<AccessWatcher>(name, param);
        },
        lv::params("name", "param"),
        lv::doc("Create a transparent watcher that reports writes to watched "
                "address ranges"))
    .property("from", &AccessWatcher::from,
              lv::doc("Incoming TLM target socket"))
    .property("to", &AccessWatcher::to, &AccessWatcher::set_to,
              lv::doc("Outgoing TLM target"));

}  // namespace
}  // namespace dbg
