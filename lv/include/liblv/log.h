/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

// Unified lv output API, backed by Quill (asynchronous, front/back-end split).
//
// Two ways to log:
//   * Macros  LV_TRACE/LV_DEBUG/LV_INFO/LV_WARNING/LV_ERROR/LV_FATAL/LV_PRINTLN
//     forward the format string + args straight to Quill. They are the
//     canonical, performance API: arguments are copied to the queue and
//     formatted on the backend thread, and statements below
//     QUILL_COMPILE_ACTIVE_LOG_LEVEL are removed at compile time (no args
//     evaluated, no codegen).
//   * Functions lv::Info/Warning/Error/Fatal/Debug/Trace/Println exist for
//     source compatibility with existing callers. They eagerly fmt::format and
//     forward a string to Quill; prefer the macros in new and hot-path code.
//
// Time: the leveled output shows wall-clock time (Quill's %(time) column) plus
// the SystemC simulation time injected as a bracketed "[N ns]" prefix. Do NOT
// put a trailing '\n' in messages -- the sink appends exactly one newline.

#include <fmt/format.h>
#include <fmt/ranges.h>  // container/range formatting used by the function shims
#include <quill/Backend.h>
#include <quill/Frontend.h>
#include <quill/LogMacros.h>
#include <quill/Logger.h>

#include <cstdint>
#include <exception>
#include <optional>
#include <string>
#include <utility>

namespace lv {

// Thrown by Fatal()/LV_FATAL(). Caught at the top of main() to exit gracefully.
struct fatal_error : virtual std::exception {};

namespace log {

// Bump the per-thread SPSC queue from Quill's 128 KiB default to 256 KiB so a
// typical sim burst (e.g. konata trace) doesn't grow it at startup -- which
// otherwise prints "Quill INFO: Allocated a new SPSC queue ..." on stderr.
struct FrontendOptions : quill::FrontendOptions {
  static constexpr size_t initial_queue_capacity = 256u * 1024u;
};
using Frontend = quill::FrontendImpl<FrontendOptions>;
using Logger = quill::LoggerImpl<FrontendOptions>;

enum class Level { Trace, Debug, Info, Warning, Error, Fatal };

struct Config {
  // Messages below this level are dropped at runtime (does not affect Println).
  Level min_level = Level::Info;
  // Mirror output to the process stdout (console sink).
  bool to_stdout = true;
  // If set, also write (uncolored) output to this file.
  std::optional<std::string> file = std::nullopt;
  // Colorize console output. nullopt => Quill auto-detect (isatty).
  std::optional<bool> color = std::nullopt;
};

// Start the Quill backend, build sinks from Config, create the loggers, and
// install the SystemC report handler. Call early from main(). Idempotent for
// the backend; lazily run with defaults on first use.
void init(const Config& config = {});

// Route SystemC SC_REPORT_* through this logger. Called by init().
void install_systemc_handler();

// Block until pending async output is written (Quill flush_log). The SystemC
// handler calls this before any error/fatal abort so nothing is lost.
void flush();

// A self-contained async file writer backed by a private Quill logger+sink.
// Use this (instead of std::ofstream) for per-instance trace files such as
// Structured dumps: no per-record flushing needed; one newline per call;
// final flush happens in the destructor.
//
// The sink is cached by path in Quill's Frontend singleton. Re-creating a
// FileTracer for the same path appends to the existing file rather than
// truncating; use distinct paths per run to start fresh.
class FileTracer {
 public:
  explicit FileTracer(std::string path);
  ~FileTracer();
  FileTracer(const FileTracer&) = delete;
  FileTracer& operator=(const FileTracer&) = delete;

  // Returns the underlying Quill logger. Use via LV_FTRACER_PRINTLN for
  // hot-path calls that benefit from lazy backend-thread formatting.
  Logger* logger() const { return logger_; }

  // Convenience for cold-path calls: eagerly fmt::format, then log.
  template <typename... T>
  void println(fmt::format_string<T...> fmt, T&&... args) {
    LOG_CRITICAL(logger_, "{}", fmt::format(fmt, std::forward<T>(args)...));
  }

  // Block until queued writes have reached the sink. The destructor flushes
  // automatically; call this explicitly at checkpoints / sync points. Skipped
  // if Quill's backend has already been stopped (e.g. at static destruction):
  // flush_log() drives a frontend write into a torn-down queue and segfaults.
  void flush() {
    if (logger_ && quill::Backend::is_running()) logger_->flush_log();
  }

 private:
  Logger* logger_ = nullptr;
};

namespace detail {

// Quill loggers backing the API. logger() carries wall time + level;
// raw_logger() emits only the message (for Println / instruction traces). Both
// lazily init().
Logger* logger();
Logger* raw_logger();

// Current SystemC simulation time in nanoseconds (0 before sc_start). Defined
// in log.cc so this header stays free of <systemc.h>; injected into leveled
// logs.
uint64_t sim_now_ns();

}  // namespace detail

}  // namespace log

}  // namespace lv

// ---------------------------------------------------------------------------
// Canonical macro API (compile-time strippable via
// QUILL_COMPILE_ACTIVE_LOG_LEVEL). Each leveled message is prefixed with the
// simulation time. The format string must be a string literal (it is
// concatenated with the "[N ns]" prefix).
// ---------------------------------------------------------------------------

// The simulation-time prefix prepended to every leveled message: a bracketed,
// right-aligned "[N ns]". Defined once here (a string literal so it can be
// concatenated at compile time) and consumed by the macros, the function shims,
// and the SystemC report handler -- keep these in sync via this macro only.
// The matching argument is ::lv::log::detail::sim_now_ns().
#define LV_SIM_TIME_PREFIX "[{:>10} ns]  "

#define LV_TRACE(fmt_, ...)                                          \
  LOG_TRACE_L1(::lv::log::detail::logger(), LV_SIM_TIME_PREFIX fmt_, \
               ::lv::log::detail::sim_now_ns(), ##__VA_ARGS__)
#define LV_DEBUG(fmt_, ...)                                       \
  LOG_DEBUG(::lv::log::detail::logger(), LV_SIM_TIME_PREFIX fmt_, \
            ::lv::log::detail::sim_now_ns(), ##__VA_ARGS__)
#define LV_INFO(fmt_, ...)                                       \
  LOG_INFO(::lv::log::detail::logger(), LV_SIM_TIME_PREFIX fmt_, \
           ::lv::log::detail::sim_now_ns(), ##__VA_ARGS__)
#define LV_WARNING(fmt_, ...)                                       \
  LOG_WARNING(::lv::log::detail::logger(), LV_SIM_TIME_PREFIX fmt_, \
              ::lv::log::detail::sim_now_ns(), ##__VA_ARGS__)
#define LV_ERROR(fmt_, ...)                                       \
  LOG_ERROR(::lv::log::detail::logger(), LV_SIM_TIME_PREFIX fmt_, \
            ::lv::log::detail::sim_now_ns(), ##__VA_ARGS__)

// Fatal: emit, flush, then throw. The throw remains even if the log is
// stripped.
#define LV_FATAL(fmt_, ...)                                            \
  do {                                                                 \
    LOG_CRITICAL(::lv::log::detail::logger(), LV_SIM_TIME_PREFIX fmt_, \
                 ::lv::log::detail::sim_now_ns(), ##__VA_ARGS__);      \
    ::lv::log::flush();                                                \
    throw ::lv::fatal_error{};                                         \
  } while (0)

// Raw, prefix-less line for structured dumps (disassembly, register dumps,
// --docgen, instruction traces). Uses CRITICAL so it is never compile-stripped;
// the raw logger's %(message)-only formatter hides the level. Caller omits
// '\n'.
#define LV_PRINTLN(...) \
  LOG_CRITICAL(::lv::log::detail::raw_logger(), __VA_ARGS__)

// Per-instance file tracer (see lv::log::FileTracer). Hot-path macro: the
// format string stays a compile-time literal, args are lazy on the backend.
#define LV_FTRACER_PRINTLN(tracer_, fmt_, ...) \
  LOG_CRITICAL((tracer_).logger(), fmt_, ##__VA_ARGS__)

// ---------------------------------------------------------------------------
// Function compat shims (names kept for existing callers; non-strippable).
// Messages must not carry a trailing '\n' (the sink appends one).
// ---------------------------------------------------------------------------
namespace lv {

template <typename... T>
inline void Trace(fmt::format_string<T...> fmt, T&&... args) {
  LOG_TRACE_L1(log::detail::logger(), LV_SIM_TIME_PREFIX "{}",
               log::detail::sim_now_ns(),
               fmt::format(fmt, std::forward<T>(args)...));
}

template <typename... T>
inline void Debug(fmt::format_string<T...> fmt, T&&... args) {
  LOG_DEBUG(log::detail::logger(), LV_SIM_TIME_PREFIX "{}",
            log::detail::sim_now_ns(),
            fmt::format(fmt, std::forward<T>(args)...));
}

template <typename... T>
inline void Info(fmt::format_string<T...> fmt, T&&... args) {
  LOG_INFO(log::detail::logger(), LV_SIM_TIME_PREFIX "{}",
           log::detail::sim_now_ns(),
           fmt::format(fmt, std::forward<T>(args)...));
}

template <typename... T>
inline void Warning(fmt::format_string<T...> fmt, T&&... args) {
  LOG_WARNING(log::detail::logger(), LV_SIM_TIME_PREFIX "{}",
              log::detail::sim_now_ns(),
              fmt::format(fmt, std::forward<T>(args)...));
}

template <typename... T>
inline void Error(fmt::format_string<T...> fmt, T&&... args) {
  LOG_ERROR(log::detail::logger(), LV_SIM_TIME_PREFIX "{}",
            log::detail::sim_now_ns(),
            fmt::format(fmt, std::forward<T>(args)...));
}

template <typename... T>
inline void Fatal(fmt::format_string<T...> fmt, T&&... args) {
  LOG_CRITICAL(log::detail::logger(), LV_SIM_TIME_PREFIX "{}",
               log::detail::sim_now_ns(),
               fmt::format(fmt, std::forward<T>(args)...));
  log::flush();
  throw fatal_error{};
}

// Raw, prefix-less output for structured dumps. Routed through the raw logger.
template <typename... T>
inline void Println(fmt::format_string<T...> fmt, T&&... args) {
  LOG_CRITICAL(log::detail::raw_logger(), "{}",
               fmt::format(fmt, std::forward<T>(args)...));
}

}  // namespace lv
