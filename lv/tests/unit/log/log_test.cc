// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

// Native unit test for the lv::log seam.
//
// Verifies that every public logging entry point actually reaches a sink:
// the function shims (lv::Trace/Debug/Info/Warning/Error/Fatal/Println) AND
// the canonical macros (LV_TRACE/.../LV_FATAL/LV_PRINTLN). We log to a private
// FileSink (no stdout, no fork), flush, then read the file back and assert.
//
// Why a native test instead of a .lua script: these are C++ functions not yet
// bound into Lua, so they can only be driven from C++. Capturing to a file
// (rather than grepping async stdout) sidesteps the backend-thread/stdout race
// and gives a deterministic, ordered transcript to assert against.
//
// Level awareness: statements below QUILL_COMPILE_ACTIVE_LOG_LEVEL are removed
// at compile time, so each presence check is guarded by the same threshold --
// the test stays correct at any -DLV_LOG_LEVEL. Error/Critical/Println are
// always present.

#include <liblv/log.h>
#include <systemc.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

int g_failures = 0;

#define CHECK(cond, what)                                  \
  do {                                                     \
    if (!(cond)) {                                         \
      std::cerr << "FAIL: " << (what) << " (" #cond ")\n"; \
      ++g_failures;                                        \
    }                                                      \
  } while (0)

std::vector<std::string> read_lines(const std::string& path) {
  std::vector<std::string> lines;
  std::ifstream in(path);
  for (std::string line; std::getline(in, line);) lines.push_back(line);
  return lines;
}

// The first line containing `sub`, or "" if none.
std::string line_with(const std::vector<std::string>& lines,
                      const std::string& sub) {
  for (const auto& line : lines) {
    if (line.find(sub) != std::string::npos) return line;
  }
  return {};
}

// A leveled message must be present AND carry the injected "[N ns]" sim-time
// prefix (the "ns]" marker is unique to the prefix).
void expect_leveled(const std::vector<std::string>& lines,
                    const std::string& sentinel) {
  std::string line = line_with(lines, sentinel);
  CHECK(!line.empty(), "leveled message present: " + sentinel);
  if (!line.empty()) {
    CHECK(line.find("ns]") != std::string::npos,
          "leveled message carries [N ns] prefix: " + sentinel);
  }
}

// A raw (Println) line is emitted verbatim: exactly the message, no prefix,
// no level code, no "[N ns]".
void expect_raw(const std::vector<std::string>& lines,
                const std::string& sentinel) {
  bool exact = false;
  for (const auto& line : lines) {
    if (line == sentinel) exact = true;
  }
  CHECK(exact, "raw line emitted verbatim (no prefix): " + sentinel);
}

}  // namespace

// SystemC links a main() wrapper that calls sc_main; the lv binary defines its
// own main() and stubs sc_main. Mirror that so this test owns main().
int sc_main(int, char*[]) {
  assert(0);  // Should not be called.
  return 0;
}

int main() {
  const std::string path =
      (std::filesystem::temp_directory_path() /
       ("lv_log_test_" + std::to_string(::getpid()) + ".log"))
          .string();

  lv::log::Config cfg;
  cfg.min_level = lv::log::Level::Trace;  // allow every level at runtime
  cfg.to_stdout = false;                  // capture to file only
  cfg.color = false;
  cfg.file = path;
  lv::log::init(cfg);

  // --- leveled: function shims (eager fmt::format) ---
  lv::Trace("LOGT_TRACE_SHIM {}", 1);
  lv::Debug("LOGT_DEBUG_SHIM {}", 2);
  lv::Info("LOGT_INFO_SHIM {}", 3);
  lv::Warning("LOGT_WARN_SHIM {}", 4);
  lv::Error("LOGT_ERROR_SHIM {}", 5);

  // --- leveled: canonical macros (lazy, backend-formatted) ---
  LV_TRACE("LOGT_TRACE_MACRO {}", 1);
  LV_DEBUG("LOGT_DEBUG_MACRO {}", 2);
  LV_INFO("LOGT_INFO_MACRO {}", 3);
  LV_WARNING("LOGT_WARN_MACRO {}", 4);
  LV_ERROR("LOGT_ERROR_MACRO {}", 5);

  // --- raw / prefix-less line output ---
  lv::Println("LOGT_RAW_SHIM");
  LV_PRINTLN("LOGT_RAW_MACRO");

  // --- Fatal: must log (Critical, never stripped) then throw fatal_error ---
  bool shim_threw = false;
  try {
    lv::Fatal("LOGT_FATAL_SHIM");
  } catch (const lv::fatal_error&) {
    shim_threw = true;
  }
  CHECK(shim_threw, "lv::Fatal throws fatal_error");

  bool macro_threw = false;
  try {
    LV_FATAL("LOGT_FATAL_MACRO");
  } catch (const lv::fatal_error&) {
    macro_threw = true;
  }
  CHECK(macro_threw, "LV_FATAL throws fatal_error");

  // --- sim time injection: advance the kernel and confirm it shows up ---
  // A clock provides events so sc_start actually advances time (sc_start with
  // no registered processes returns immediately at t=0). Use Error so the check
  // is independent of the compile-time level (Error is kept unless built at
  // CRITICAL).
  sc_core::sc_clock clk("clk", sc_core::sc_time(10, sc_core::SC_NS));
  sc_core::sc_start(sc_core::sc_time(120, sc_core::SC_NS));
  lv::Error("LOGT_SIMTIME marker");

  lv::log::flush();

  // --- assertions ---
  std::vector<std::string> lines = read_lines(path);
  CHECK(!lines.empty(), "log file is non-empty");

#if QUILL_COMPILE_ACTIVE_LOG_LEVEL <= QUILL_COMPILE_ACTIVE_LOG_LEVEL_TRACE_L1
  expect_leveled(lines, "LOGT_TRACE_SHIM");
  expect_leveled(lines, "LOGT_TRACE_MACRO");
#endif
#if QUILL_COMPILE_ACTIVE_LOG_LEVEL <= QUILL_COMPILE_ACTIVE_LOG_LEVEL_DEBUG
  expect_leveled(lines, "LOGT_DEBUG_SHIM");
  expect_leveled(lines, "LOGT_DEBUG_MACRO");
#endif
#if QUILL_COMPILE_ACTIVE_LOG_LEVEL <= QUILL_COMPILE_ACTIVE_LOG_LEVEL_INFO
  expect_leveled(lines, "LOGT_INFO_SHIM");
  expect_leveled(lines, "LOGT_INFO_MACRO");
#endif
#if QUILL_COMPILE_ACTIVE_LOG_LEVEL <= QUILL_COMPILE_ACTIVE_LOG_LEVEL_WARNING
  expect_leveled(lines, "LOGT_WARN_SHIM");
  expect_leveled(lines, "LOGT_WARN_MACRO");
#endif
#if QUILL_COMPILE_ACTIVE_LOG_LEVEL <= QUILL_COMPILE_ACTIVE_LOG_LEVEL_ERROR
  expect_leveled(lines, "LOGT_ERROR_SHIM");
  expect_leveled(lines, "LOGT_ERROR_MACRO");
  expect_leveled(lines, "LOGT_SIMTIME");
#endif

  // Always present (Critical / raw).
  expect_leveled(lines, "LOGT_FATAL_SHIM");
  expect_leveled(lines, "LOGT_FATAL_MACRO");
  expect_raw(lines, "LOGT_RAW_SHIM");
  expect_raw(lines, "LOGT_RAW_MACRO");

  // Sim time advanced to 120 ns and was injected into the message.
  CHECK(line_with(lines, "LOGT_SIMTIME").find("120 ns]") != std::string::npos,
        "sim time 120 ns injected into leveled message");

  // --- FileTracer: per-instance file writer, exercised end-to-end ---
  // Construct, drive both the LV_FTRACER_PRINTLN macro and the println()
  // method, optionally flush, destroy, then read the file back. The tracer
  // is %(message)-only and never compile-stripped (uses LOG_CRITICAL).
  const std::string ft_path =
      (std::filesystem::temp_directory_path() /
       ("lv_ftracer_test_" + std::to_string(::getpid()) + ".log"))
          .string();
  {
    lv::log::FileTracer tracer(ft_path);
    LV_FTRACER_PRINTLN(tracer, "FTRACE_MACRO\t{}\t{}", 1, "alpha");
    tracer.println("FTRACE_METHOD\t{}\t{}", 2, "beta");
    tracer.flush();  // explicit flush: contents are durable before dtor.
    LV_FTRACER_PRINTLN(tracer, "FTRACE_AFTER_FLUSH");
  }  // destructor: flush + remove_logger

  std::vector<std::string> ft_lines = read_lines(ft_path);
  CHECK(!ft_lines.empty(), "FileTracer file is non-empty");
  expect_raw(ft_lines, "FTRACE_MACRO\t1\talpha");
  expect_raw(ft_lines, "FTRACE_METHOD\t2\tbeta");
  expect_raw(ft_lines, "FTRACE_AFTER_FLUSH");

  std::error_code ec;
  std::filesystem::remove(path, ec);
  std::filesystem::remove(ft_path, ec);

  if (g_failures == 0) {
    std::cout << "ALL_OK\n";
    return EXIT_SUCCESS;
  }
  std::cerr << g_failures << " check(s) failed\n";
  return EXIT_FAILURE;
}
