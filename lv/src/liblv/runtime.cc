// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include <liblv/binding.h>

#if defined(_WIN32)
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#elif defined(__linux__)
#include <unistd.h>
#endif

#include <array>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <string>
#include <vector>

#ifndef LV_DEFAULT_RUNTIMEPATH
#define LV_DEFAULT_RUNTIMEPATH ""
#endif

#ifndef LV_DEFAULT_RUNFILES_ROOT
#define LV_DEFAULT_RUNFILES_ROOT "."
#endif

namespace {

namespace fs = std::filesystem;

fs::path ExecutableDirectory() {
#if defined(_WIN32)
  std::array<char, MAX_PATH> buffer{};
  const DWORD length =
      GetModuleFileNameA(nullptr, buffer.data(), buffer.size());
  if (length > 0 && length < buffer.size()) {
    return fs::path(std::string(buffer.data(), length)).parent_path();
  }
#elif defined(__APPLE__)
  uint32_t size = 0;
  _NSGetExecutablePath(nullptr, &size);
  std::string executable(size, '\0');
  if (_NSGetExecutablePath(executable.data(), &size) == 0) {
    executable.resize(std::char_traits<char>::length(executable.c_str()));
    return fs::path(executable).parent_path();
  }
#elif defined(__linux__)
  std::array<char, 4096> buffer{};
  const ssize_t capacity = static_cast<ssize_t>(buffer.size() - 1);
  const ssize_t length =
      readlink("/proc/self/exe", buffer.data(), static_cast<size_t>(capacity));
  // readlink() does not append a NUL terminator.  A result equal to the
  // requested capacity means the executable path may have been truncated.
  if (length > 0 && length < capacity) {
    return fs::path(std::string(buffer.data(), static_cast<size_t>(length)))
        .parent_path();
  }
#endif
  return fs::current_path();
}

fs::path ResolveDefaultPath(const fs::path &entry,
                            const fs::path &executable_dir) {
  if (entry.empty() || entry.is_absolute()) return entry;
  return executable_dir / entry;
}

void AppendRuntimePathEntries(std::vector<fs::path> &entries,
                              const char *runtime_path,
                              const fs::path *resolve_relative_to = nullptr) {
  if (!runtime_path) return;

  std::stringstream stream(runtime_path);
  std::string entry;
  const char separator =
#if defined(_WIN32)
      ';';
#else
      ':';
#endif
  while (std::getline(stream, entry, separator)) {
    if (!entry.empty()) {
      const fs::path path(entry);
      entries.emplace_back(resolve_relative_to
                               ? ResolveDefaultPath(path, *resolve_relative_to)
                               : path);
    }
  }
}

std::string LuaPackagePath(const std::vector<fs::path> &roots) {
  std::string package_path;
  for (const auto &root : roots) {
    package_path += (root / "?.lua").generic_string();
    package_path += ';';
    package_path += (root / "?/init.lua").generic_string();
    package_path += ';';
  }
  return package_path;
}

std::string PathWithTrailingSlash(const fs::path &path) {
  std::string result = path.generic_string();
  if (result.empty()) return "./";
  if (result.back() != '/') result += '/';
  return result;
}

class Runtime {
 private:
  // The runtime constructor specifies what to run before the singleton
  // sol::state instance is first acquired.
  Runtime() {
    lua_.open_libraries();

    const fs::path executable_dir = ExecutableDirectory();
    std::vector<fs::path> lua_roots;
    AppendRuntimePathEntries(lua_roots, std::getenv("LV_RUNTIMEPATH"));
    AppendRuntimePathEntries(lua_roots, LV_DEFAULT_RUNTIMEPATH,
                             &executable_dir);

    sol::table package = lua_["package"];
    const std::string default_package_path = package["path"];
    package["path"] = LuaPackagePath(lua_roots) + default_package_path;

    const char *runfiles_root = std::getenv("LV_RUNFILES_ROOT");
    lua_["__RUNFILE_PATH__"] = PathWithTrailingSlash(
        runfiles_root && runfiles_root[0]
            ? fs::path(runfiles_root)
            : ResolveDefaultPath(fs::path(LV_DEFAULT_RUNFILES_ROOT),
                                 executable_dir));
  }
  sol::state lua_;

  friend sol::state &lv::Runtime();
};

}  // namespace

namespace lv {

sol::state &Runtime() {
  static ::Runtime runtime;
  return runtime.lua_;
}

}  // namespace lv
