/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <systemc.h>

#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#ifndef ENABLE_PERFETTO
#define ENABLE_PERFETTO false
#endif

namespace lv {
namespace trace {

class Track {
 public:
  Track(const Track &) = delete;
  Track &operator=(const Track &) = delete;

  Track(Track &&other)
      : enabled_(other.enabled_),
        is_counter_(other.is_counter_),
        uuid_(other.uuid_) {
    other.enabled_ = false;
    other.is_counter_ = false;
    other.uuid_ = 0;
  }

  Track &operator=(Track &&other) {
    this->enabled_ = other.enabled_;
    this->is_counter_ = other.is_counter_;
    this->uuid_ = other.uuid_;
    other.enabled_ = false;
    other.uuid_ = 0;
    return *this;
  }

  void enable() { enabled_ = true; }
  void disable() { enabled_ = false; }
  void set_enabled(bool enabled) { enabled_ = enabled; }

  bool enabled() const { return enabled_; }
  bool is_counter() const { return is_counter_; }
  uint64_t uuid() const { return uuid_; }

 private:
  explicit Track(bool enabled, bool is_counter, uint64_t uuid)
      : enabled_(enabled), is_counter_(is_counter), uuid_(uuid) {}

  bool enabled_;
  bool is_counter_;
  uint64_t uuid_;
  friend Track CreateSubTrackWithName(std::string_view, std::string_view, bool);
};

using TraceArgType = std::variant<std::string, uint64_t, int64_t, double, bool>;
using TraceArg = std::pair<std::string_view, TraceArgType>;

inline TraceArg KV(std::string_view key, std::string val) {
  return {key, TraceArgType{std::move(val)}};
}
inline TraceArg KV(std::string_view key, std::string_view val) {
  return {key, TraceArgType{std::string(val)}};
}
template <size_t N>
inline TraceArg KV(std::string_view key, const char (&val)[N]) {
  return {key, TraceArgType{std::string(val, N - 1)}};
}
inline TraceArg KV(std::string_view k, int64_t v) {
  return {k, TraceArgType{v}};
}
inline TraceArg KV(std::string_view k, uint64_t v) {
  return {k, TraceArgType{v}};
}
inline TraceArg KV(std::string_view k, double v) {
  return {k, TraceArgType{v}};
}
inline TraceArg KV(std::string_view k, bool v) { return {k, TraceArgType{v}}; }

// signed integral (except bool/int64_t) -> int64_t
template <class T, typename std::enable_if<std::is_integral<T>::value &&
                                               std::is_signed<T>::value &&
                                               !std::is_same<T, bool>::value &&
                                               !std::is_same<T, int64_t>::value,
                                           int>::type = 0>
inline TraceArg KV(std::string_view k, T v) {
  return KV(k, static_cast<int64_t>(v));
}

// unsigned integral (except bool/uint64_t) -> uint64_t
template <
    class T,
    typename std::enable_if<
        std::is_integral<T>::value && std::is_unsigned<T>::value &&
            !std::is_same<T, bool>::value && !std::is_same<T, uint64_t>::value,
        int>::type = 0>
inline TraceArg KV(std::string_view k, T v) {
  return KV(k, static_cast<uint64_t>(v));
}

Track CreateSubTrackWithName(std::string_view parent_name,
                             std::string_view name, bool counter);

Track NewModuleTrackImpl(sc_module *mod, std::string_view name, bool counter);

void TraceBeginImpl(const Track &track, std::string_view category,
                    std::string_view name, uint64_t timestamp,
                    std::initializer_list<TraceArg>);

void TraceEndImpl(const Track &track, uint64_t timestamp);

template <class T>
void TraceCounter(const Track &track, std::string_view category, T val,
                  uint64_t timestamp, std::initializer_list<TraceArg>);

void TraceInstant(const Track &track, std::string_view category,
                  std::string_view name, uint64_t timestamp,
                  std::initializer_list<TraceArg>);

}  // namespace trace
}  // namespace lv

#define SC_TIMESTAMP_NS (sc_time_stamp().value() / sc_time(1, SC_NS).value())

#define LV_NEW_MODULE_TRACK(name) \
  lv::trace::NewModuleTrackImpl(this, name, false)

#define LV_NEW_MODULE_COUNTER_TRACK(name) \
  lv::trace::NewModuleTrackImpl(this, name, true)

#if ENABLE_PERFETTO
#define LV_TRACE_BEGIN(track, category, name, ...)                 \
  do {                                                             \
    const auto &lv_trace_track = (track);                          \
    if (lv_trace_track.enabled()) {                                \
      ::lv::trace::TraceBeginImpl(lv_trace_track, category, name,  \
                                  SC_TIMESTAMP_NS, {__VA_ARGS__}); \
    }                                                              \
  } while (0)
#else
#define LV_TRACE_BEGIN(...) (void)sizeof((__VA_ARGS__), 0)
#endif

#if ENABLE_PERFETTO
#define LV_TRACE_END(track)                                       \
  do {                                                            \
    const auto &lv_trace_track = (track);                         \
    if (lv_trace_track.enabled()) {                               \
      ::lv::trace::TraceEndImpl(lv_trace_track, SC_TIMESTAMP_NS); \
    }                                                             \
  } while (0)
#else
#define LV_TRACE_END(...) (void)sizeof((__VA_ARGS__), 0)
#endif

#if ENABLE_PERFETTO
#define LV_TRACE_COUNTER(track, category, val, ...)              \
  do {                                                           \
    const auto &lv_trace_track = (track);                        \
    if (lv_trace_track.enabled()) {                              \
      ::lv::trace::TraceCounter(lv_trace_track, category, val,   \
                                SC_TIMESTAMP_NS, {__VA_ARGS__}); \
    }                                                            \
  } while (0)
#else
#define LV_TRACE_COUNTER(...) (void)sizeof((__VA_ARGS__), 0)
#endif

#if ENABLE_PERFETTO
#define LV_TRACE_INSTANT(track, category, name, ...)             \
  do {                                                           \
    const auto &lv_trace_track = (track);                        \
    if (lv_trace_track.enabled()) {                              \
      ::lv::trace::TraceInstant(lv_trace_track, category, name,  \
                                SC_TIMESTAMP_NS, {__VA_ARGS__}); \
    }                                                            \
  } while (0)
#else
#define LV_TRACE_INSTANT(...) (void)sizeof((__VA_ARGS__), 0)
#endif

#if ENABLE_PERFETTO
#define LV_TRACE_ARG(key, val) ::lv::trace::KV((key), (val))
#else
#define LV_TRACE_ARG(key, val) ((void)0)
#endif
