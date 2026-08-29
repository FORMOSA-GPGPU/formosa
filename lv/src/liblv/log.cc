// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include <liblv/log.h>
#include <pthread.h>
#include <quill/Backend.h>
#include <quill/Frontend.h>
#include <quill/core/Common.h>
#include <quill/core/PatternFormatterOptions.h>
#include <quill/sinks/ConsoleSink.h>
#include <quill/sinks/FileSink.h>
#include <systemc.h>

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace lv::log {
namespace {

Logger *g_logger = nullptr;
Logger *g_raw_logger = nullptr;
bool g_backend_started = false;
bool g_backend_running_at_fork = false;

// Quill's backend runs on a dedicated thread, which does NOT survive fork() --
// in the child the queue is never drained and flush_log() blocks forever. The
// lv simulator forks per run (e.g. run_opencl.lua), so make logging fork-safe:
// stop the backend in the parent's pre-fork handler (joins the real thread and
// flushes), then start a fresh backend in both parent and child afterwards.
// Loggers/sinks live in the Frontend singletons and persist across the restart.
void register_fork_handlers() {
  pthread_atfork(
      /*prepare=*/
      [] {
        g_backend_running_at_fork = quill::Backend::is_running();
        if (g_backend_running_at_fork) quill::Backend::stop();
      },
      /*parent=*/
      [] {
        if (g_backend_running_at_fork) quill::Backend::start();
      },
      /*child=*/
      [] {
        if (g_backend_running_at_fork) quill::Backend::start();
      });
}

quill::LogLevel to_quill(Level level) {
  switch (level) {
    case Level::Trace:
      return quill::LogLevel::TraceL1;
    case Level::Debug:
      return quill::LogLevel::Debug;
    case Level::Info:
      return quill::LogLevel::Info;
    case Level::Warning:
      return quill::LogLevel::Warning;
    case Level::Error:
      return quill::LogLevel::Error;
    case Level::Fatal:
      return quill::LogLevel::Critical;
  }
  return quill::LogLevel::Info;
}

// ---- SystemC report handler ----

void emit_report(sc_core::sc_severity sev, const std::string &body) {
  Logger *lg = detail::logger();
  const uint64_t t = detail::sim_now_ns();
  switch (sev) {
    case sc_core::SC_WARNING:
      LOG_WARNING(lg, LV_SIM_TIME_PREFIX "{}", t, body);
      break;
    case sc_core::SC_ERROR:
      LOG_ERROR(lg, LV_SIM_TIME_PREFIX "{}", t, body);
      break;
    case sc_core::SC_FATAL:
      LOG_CRITICAL(lg, LV_SIM_TIME_PREFIX "{}", t, body);
      break;
    case sc_core::SC_INFO:
    default:
      LOG_INFO(lg, LV_SIM_TIME_PREFIX "{}", t, body);
      break;
  }
}

void systemc_report_handler(const sc_core::sc_report &rep,
                            const sc_core::sc_actions &actions) {
  const char *type = rep.get_msg_type();
  const char *msg = rep.get_msg();
  std::string body = (msg != nullptr && msg[0] != '\0')
                         ? fmt::format("[{}] {}", type, msg)
                         : fmt::format("[{}]", type);
  emit_report(rep.get_severity(), body);

  // Make our output durable before SystemC potentially aborts/throws.
  if (rep.get_severity() == sc_core::SC_ERROR ||
      rep.get_severity() == sc_core::SC_FATAL) {
    flush();
  }

  // We have displayed/logged the report ourselves; strip those bits and let
  // SystemC perform the consequential actions (cache/throw/stop/abort) with its
  // exact default semantics -- no double printing.
  sc_core::sc_actions rest = actions & ~(sc_core::SC_DISPLAY | sc_core::SC_LOG);
  if (rest != sc_core::SC_UNSPECIFIED && rest != sc_core::SC_DO_NOTHING) {
    sc_core::sc_report_handler::default_handler(rep, rest);
  }
}

}  // namespace

void init(const Config &config) {
  static std::once_flag atfork_once;
  std::call_once(atfork_once, register_fork_handlers);

  if (!g_backend_started) {
    quill::Backend::start();
    g_backend_started = true;
  }

  // Console colour mode from Config (nullopt => Quill auto-detect).
  using CM = quill::ConsoleSinkConfig::ColourMode;
  CM colour = CM::Automatic;
  if (config.color.has_value()) colour = *config.color ? CM::Always : CM::Never;

  std::shared_ptr<quill::Sink> file_sink;
  if (config.file.has_value()) {
    quill::FileSinkConfig fcfg;
    fcfg.set_open_mode('w');
    file_sink =
        Frontend::create_or_get_sink<quill::FileSink>(*config.file, fcfg);
  }

  // Leveled logger: wall-clock time + level, coloured console.
  std::vector<std::shared_ptr<quill::Sink>> lv_sinks;
  if (config.to_stdout) {
    quill::ConsoleSinkConfig ccfg;
    ccfg.set_colour_mode(colour);
    lv_sinks.push_back(
        Frontend::create_or_get_sink<quill::ConsoleSink>("lv_console", ccfg));
  }
  if (file_sink) lv_sinks.push_back(file_sink);

  // Raw logger: message only, no colour (used by Println / traces).
  std::vector<std::shared_ptr<quill::Sink>> raw_sinks;
  if (config.to_stdout) {
    quill::ConsoleSinkConfig rcfg;
    rcfg.set_colour_mode(CM::Never);
    raw_sinks.push_back(Frontend::create_or_get_sink<quill::ConsoleSink>(
        "lv_raw_console", rcfg));
  }
  if (file_sink) raw_sinks.push_back(file_sink);

  // Wall-clock time column (bare, default Tsc clock) + short level code;
  // simulation time is injected into each message as a "[N ns]" prefix by the
  // LV_* macros / shims.
  quill::PatternFormatterOptions lv_opts{
      "%(time)  %(log_level_short_code:<2) %(message)", "%H:%M:%S.%Qms",
      quill::Timezone::LocalTime, /*add_metadata_to_multi_line_logs=*/false};
  quill::PatternFormatterOptions raw_opts{"%(message)"};

  g_logger = Frontend::create_or_get_logger("lv", lv_sinks, lv_opts);
  g_raw_logger = Frontend::create_or_get_logger("lv.raw", raw_sinks, raw_opts);

  g_logger->set_log_level(to_quill(config.min_level));
  g_raw_logger->set_log_level(
      quill::LogLevel::TraceL3);  // Println uses Critical

  install_systemc_handler();
}

void install_systemc_handler() {
  sc_core::sc_report_handler::set_handler(&systemc_report_handler);
}

void flush() {
  // Refuse to drive flush_log() into a stopped Quill backend: it tries to
  // queue a flush marker through the frontend and segfaults if the backend
  // thread has already torn down (e.g. via Quill's std::atexit handler).
  if (!quill::Backend::is_running()) return;
  if (g_logger) g_logger->flush_log();
  if (g_raw_logger) g_raw_logger->flush_log();
}

FileTracer::FileTracer(std::string path) {
  static std::atomic<uint32_t> seq{0};
  std::string name = "lv.ftrace." + std::to_string(seq.fetch_add(
                                        1, std::memory_order_relaxed));

  quill::FileSinkConfig fcfg;
  fcfg.set_open_mode('w');
  auto sink =
      Frontend::create_or_get_sink<quill::FileSink>(std::move(path), fcfg);

  quill::PatternFormatterOptions opts{"%(message)"};
  logger_ = Frontend::create_or_get_logger(name, {std::move(sink)}, opts);
  logger_->set_log_level(quill::LogLevel::TraceL3);
}

FileTracer::~FileTracer() {
  if (!logger_) return;
  // Quill registers an std::atexit that stops the backend thread; FileTracers
  // owned by static state (e.g. simtix modules held by the lv Lua Runtime
  // singleton) are destroyed AFTER that runs. Once the backend is stopped,
  // both flush_log() and remove_logger() drive frontend writes into a torn-
  // down queue and segfault. Skip them at that point -- process exit will
  // reclaim the logger memory.
  if (quill::Backend::is_running()) {
    logger_->flush_log();
    Frontend::remove_logger(logger_);
  }
  logger_ = nullptr;
}

namespace detail {

Logger *logger() {
  if (g_logger == nullptr) init(Config{});
  return g_logger;
}

Logger *raw_logger() {
  if (g_raw_logger == nullptr) init(Config{});
  return g_raw_logger;
}

uint64_t sim_now_ns() {
  // sc_time_stamp() is valid on the SC kernel thread and returns SC_ZERO_TIME
  // before sc_start. Captured at log time (frontend), formatted on the backend.
  // Integer math throughout: value() returns ticks in the kernel's base time
  // resolution (typically 1 ps); dividing by ticks-per-ns yields exact ns with
  // no float rounding and no 2^53-ns precision ceiling. The divisor is a
  // resolution-dependent constant the compiler can hoist.
  static const sc_dt::uint64 ticks_per_ns =
      sc_core::sc_time(1, sc_core::SC_NS).value();
  return sc_core::sc_time_stamp().value() / ticks_per_ns;
}

}  // namespace detail

}  // namespace lv::log
