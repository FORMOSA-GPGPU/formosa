// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include <liblv/binding.h>
#include <liblv/output.h>
#include <liblv/time.h>
#include <systemc.h>

#include <algorithm>
#include <argparse/argparse.hpp>
#include <sol/sol.hpp>
#include <string>
#include <string_view>
#include <vector>

int sc_main(int argc, char *argv[]) {
  assert(0);  // Should not be called.
  return 0;
}

using lv::Fatal;

int main(int argc, char *argv[]) {
  lv::initialize_elapsed_time();

  try {
    std::vector<std::string> script_options;
    argparse::ArgumentParser program(argv[0]);
    program.add_argument("--docgen")
        .help("dump LuaLS annotations to stdout")
        .default_value(false)
        .implicit_value(true)
        .nargs(0);
    program.add_argument("--log-level")
        .help("minimum log level (trace/debug/info/warning/error)")
        .default_value(std::string("info"))
        .choices("trace", "debug", "info", "warning", "error");
    program.add_argument("--log-file")
        .help("also write log output to this file")
        .default_value(std::string(""));
    program.add_argument("--log-color")
        .help("colorize console output (auto/always/never)")
        .default_value(std::string("auto"))
        .choices("auto", "always", "never");
    program.add_argument("script.lua").nargs(argparse::nargs_pattern::optional);
    program.add_argument("script options").remaining();

    try {
      auto argv_end = argv + argc;
      auto it = std::find_if(argv + 1, argv_end, [](char *arg) {
        return arg[0] != '-';
      });
      if (it != argv_end) {
        script_options.insert(script_options.end(), it + 1, argv_end);
        argc = it - argv + 1;
      }
      program.parse_args(argc, argv);
    } catch (const std::exception &err) {
      Fatal("{}\n{}", err.what(), program.help().str());
    }

    lv::log::Config log_cfg;
    {
      auto s = program.get<std::string>("--log-level");
      if (s == "trace")
        log_cfg.min_level = lv::log::Level::Trace;
      else if (s == "debug")
        log_cfg.min_level = lv::log::Level::Debug;
      else if (s == "warning")
        log_cfg.min_level = lv::log::Level::Warning;
      else if (s == "error")
        log_cfg.min_level = lv::log::Level::Error;
      // "info" is the default, no branch needed
    }
    {
      auto f = program.get<std::string>("--log-file");
      if (!f.empty()) log_cfg.file = f;
    }
    {
      auto c = program.get<std::string>("--log-color");
      if (c == "always")
        log_cfg.color = true;
      else if (c == "never")
        log_cfg.color = false;
      // "auto" → nullopt (Quill isatty detection)
    }
    lv::log::init(log_cfg);

    if (program.get<bool>("--docgen")) {
      lv::Println("{}", lv::docgen::GenerateLuaLS());
      lv::log::flush();  // async sink: ensure the dump lands before exit
      return EXIT_SUCCESS;
    }

    auto script = program.present<std::string>("script.lua");
    if (!script) Fatal("missing script.lua\n{}", program.help().str());

    auto &lua = lv::Runtime();
    sol::load_result fx = lua.load_file(*script);
    if (!fx.valid()) {
      sol::error err = fx;
      Fatal("{}", err.what());
    }

    LV_INFO("Loaded {} modules in {:.3f} ms",
            lv::BindingLoadStats::num_modules(),
            lv::BindingLoadStats::load_time_us() / 1000.0);

    sol::safe_function_result result = fx(sol::as_args(script_options));
    if (!result.valid()) {
      sol::error err = result;
      Fatal("{}", err.what());
    }
  } catch (const lv::fatal_error &) {
    lv::log::flush();
    return EXIT_FAILURE;
  }
  lv::log::flush();  // drain async logs before exit (Quill backend teardown)
  return EXIT_SUCCESS;
}
