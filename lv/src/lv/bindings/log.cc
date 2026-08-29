// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include <liblv/binding.h>
#include <liblv/log.h>

#include <string_view>

// Expose the lv::log leveled API to Lua so script-side output goes through
// the same Quill seam as C++ sim output (same sink, same async queue, ordered).
// Each function takes a plain string -- Lua handles its own formatting:
//   lv.info(string.format("loaded %d tiles", n))
LV_MODULE(lv)
    .function(
        "trace",
        [](std::string_view msg) {
          LV_TRACE("{}", msg);
        },
        lv::params(lv::param("msg")),
        lv::doc("Log a TRACE-level message. Stripped at compile time above "
                "LV_LOG_LEVEL=trace."))
    .function(
        "debug",
        [](std::string_view msg) {
          LV_DEBUG("{}", msg);
        },
        lv::params(lv::param("msg")),
        lv::doc("Log a DEBUG-level message. Stripped at compile time above "
                "LV_LOG_LEVEL=debug."))
    .function(
        "info",
        [](std::string_view msg) {
          LV_INFO("{}", msg);
        },
        lv::params(lv::param("msg")), lv::doc("Log an INFO-level message."))
    .function(
        "warning",
        [](std::string_view msg) {
          LV_WARNING("{}", msg);
        },
        lv::params(lv::param("msg")), lv::doc("Log a WARNING-level message."))
    .function(
        "error",
        [](std::string_view msg) {
          LV_ERROR("{}", msg);
        },
        lv::params(lv::param("msg")), lv::doc("Log an ERROR-level message."))
    .function(
        "println",
        [](std::string_view msg) {
          LV_PRINTLN("{}", msg);
        },
        lv::params(lv::param("msg")),
        lv::doc(
            "Emit a raw, prefix-less line (no level badge, no sim-time "
            "prefix). Use for structured output that must stay byte-exact."));
