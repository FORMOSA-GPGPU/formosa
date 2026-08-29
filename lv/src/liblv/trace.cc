// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include <fmt/format.h>
#include <liblv/binding.h>
#include <liblv/trace.h>

#include <filesystem>
#include <functional>
#include <initializer_list>
#include <memory>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <variant>
#include <vector>

#include "liblv/output.h"
#include "perfetto.h"

namespace lv {
namespace trace {

class LVDataSource : public perfetto::DataSource<LVDataSource> {
 public:
  void OnSetup(const SetupArgs &) override {}
  void OnStart(const StartArgs &) override {}
  void OnStop(const StopArgs &) override {}
};

}  // namespace trace
}  // namespace lv

PERFETTO_DECLARE_DATA_SOURCE_STATIC_MEMBERS(lv::trace::LVDataSource);
PERFETTO_DEFINE_DATA_SOURCE_STATIC_MEMBERS(lv::trace::LVDataSource);

namespace {

class TraceManager final {
 public:
  static void Initialize() {
    // Instanciate a static instance, calling the constructor on the first
    // reference. Destructor is called when program terminates.
    static TraceManager t;
  }

  // Available settings.
  static struct Setting {
    bool streaming = true;
    uint32_t file_write_period_ms = 1000;
    uint32_t flush_period_ms = 250;
    sc_time flush_period_sc_time = SC_ZERO_TIME;  // No SystemC flush by default
    std::string_view output_prefix = "lv";
    uint32_t buffer_size_kb = 16384;
  } settings;

 private:
  SC_MODULE(Flusher) {
    SC_CTOR(Flusher, perfetto::TracingSession * session, sc_time period)
        : session_(session), period_(period) {
      SC_THREAD(Flush);
    }
    void Flush() {
      for (;;) {
        wait(period_);
        session_->FlushBlocking();
      }
    }
    perfetto::TracingSession *session_;
    sc_time period_;
  };

  TraceManager() {
    // Initialize Perfetto.
    perfetto::TracingInitArgs args;
    args.backends = perfetto::kInProcessBackend;
    perfetto::Tracing::Initialize(args);

    // Register our custom data source.
    perfetto::DataSourceDescriptor dsd;
    dsd.set_name("lv_source");
    lv::trace::LVDataSource::Register(dsd);

    // Configurations.
    perfetto::TraceConfig cfg;
    if (settings.streaming) {
      cfg.set_write_into_file(true);
      cfg.set_file_write_period_ms(settings.file_write_period_ms);
      cfg.set_flush_period_ms(settings.flush_period_ms);
      cfg.set_output_path(CreateOutputPath(settings.output_prefix));
    }

    auto *buffer_cfg = cfg.add_buffers();
    buffer_cfg->set_size_kb(settings.buffer_size_kb);
    buffer_cfg->set_fill_policy(
        settings.streaming ? perfetto::TraceConfig::BufferConfig::RING_BUFFER
                           : perfetto::TraceConfig::BufferConfig::DISCARD);

    auto *ds_cfg = cfg.add_data_sources()->mutable_config();
    ds_cfg->set_name("lv_source");

    // Start tracing.
    session_ = perfetto::Tracing::NewTrace();
    session_->Setup(cfg);
    session_->StartBlocking();

    if (settings.streaming && settings.flush_period_sc_time != SC_ZERO_TIME) {
      flusher_ = std::make_unique<Flusher>("flusher", session_.get(),
                                           settings.flush_period_sc_time);
    }
  }

  ~TraceManager() {
    lv::trace::LVDataSource::Trace(
        [](lv::trace::LVDataSource::TraceContext ctx) {
          ctx.Flush();
        });
    // Stop tracing and read the trace data.
    session_->StopBlocking();

    if (!settings.streaming) {
      std::vector<char> trace_data(session_->ReadTraceBlocking());

      // Write the result into a file.
      std::ofstream output;
      output.open(CreateOutputPath(settings.output_prefix),
                  std::ios::out | std::ios::binary);
      output.write(&trace_data[0],
                   static_cast<std::streamsize>(trace_data.size()));
      output.close();
    }
  }

  static std::string CreateOutputPath(std::string_view prefix) {
    std::string output_path = fmt::format("{}.pftrace", prefix);
    try {
      if (std::filesystem::exists(output_path)) {
        std::filesystem::remove(output_path);
      }
    } catch (const std::filesystem::filesystem_error &err) {
    }
    return output_path;
  }

  std::unique_ptr<perfetto::TracingSession> session_;
  std::unique_ptr<Flusher> flusher_;
};

TraceManager::Setting TraceManager::settings = TraceManager::Setting();

// Expose settings to the Lua interface.
using Setting = TraceManager::Setting;
LV_BINDING(trace, Setting)
    .property("streaming", &Setting::streaming,
              lv::doc("Enable streaming trace writes."))
    .property("file_write_period_ms", &Setting::file_write_period_ms,
              lv::doc("Streaming file write period in milliseconds."))
    .property("flush_period_ms", &Setting::flush_period_ms,
              lv::doc("Perfetto flush period in milliseconds."))
    .property("flush_period_sc_time", &Setting::flush_period_sc_time,
              lv::doc("Optional SystemC-time flush period."))
    .property("output_prefix", &Setting::output_prefix,
              lv::doc("Trace output path prefix."))
    .property("buffer_size_kb", &Setting::buffer_size_kb,
              lv::doc("Trace buffer size in KiB."));

LV_MODULE(trace).value("settings", sol::var(std::ref(TraceManager::settings)),
                       lv::lua_type("trace.Setting"),
                       lv::doc("Global trace settings."));

// For globally unique UUID allocation.
uint64_t global_uuid = 1;

// Maps a module name to the module's master UUID.
std::unordered_map<std::string_view, uint64_t> module_uuids;

uint64_t AllocateUuid() { return global_uuid++; }

uint64_t GetOrCreateMasterTrackUuid(std::string_view name) {
  auto it = module_uuids.find(name);
  if (it != module_uuids.end()) {
    return it->second;  // Track exists, return its uuid directly.
  }

  // Creata a track with a new uuid.
  uint64_t uuid = AllocateUuid();
  module_uuids[name] = uuid;
  lv::trace::LVDataSource::Trace(
      [&](lv::trace::LVDataSource::TraceContext ctx) {
        auto packet = ctx.NewTracePacket();
        packet->set_timestamp(0);
        auto *descriptor = packet->set_track_descriptor();
        descriptor->set_uuid(uuid);
        descriptor->set_name(name.data());
      });

  // Flush to ensure track is created properly.
  lv::trace::LVDataSource::Trace([](lv::trace::LVDataSource::TraceContext ctx) {
    ctx.Flush();
  });
  return uuid;
}

}  // namespace

namespace lv {
namespace trace {

Track CreateSubTrackWithName(std::string_view parent_name,
                             std::string_view name, bool is_counter) {
  if constexpr (!ENABLE_PERFETTO) return Track(false, false, 0);
  TraceManager::Initialize();
  uint64_t parent_uuid = GetOrCreateMasterTrackUuid(parent_name);
  uint64_t uuid = AllocateUuid();
  LVDataSource::Trace([&](LVDataSource::TraceContext ctx) {
    auto packet = ctx.NewTracePacket();
    packet->set_timestamp(0);
    auto *descriptor = packet->set_track_descriptor();
    descriptor->set_parent_uuid(parent_uuid);
    descriptor->set_uuid(uuid);
    descriptor->set_name(name.data(), name.size());
    if (is_counter) {
      descriptor->set_counter();
    }
  });
  return Track(true, is_counter, uuid);  // Enabled by default.
}

Track NewModuleTrackImpl(sc_module *mod, std::string_view name,
                         bool is_counter) {
  return CreateSubTrackWithName(mod->name(), name, is_counter);
}

void AddArgs(perfetto::protos::pbzero::TrackEvent *event,
             std::initializer_list<TraceArg> args) {
  for (const auto &[key, val] : args) {
    auto *arg = event->add_debug_annotations();
    arg->set_name(key.data(), key.size());

    std::visit(
        [&](auto &&v) {
          using T = std::decay_t<decltype(v)>;
          if constexpr (std::is_same_v<T, std::string>) {
            arg->set_string_value(v.data(), v.size());
          } else if constexpr (std::is_same_v<T, uint64_t>) {
            arg->set_uint_value(v);
          } else if constexpr (std::is_same_v<T, int64_t>) {
            arg->set_int_value(v);
          } else if constexpr (std::is_same_v<T, double>) {
            arg->set_double_value(v);
          } else if constexpr (std::is_same_v<T, bool>) {
            arg->set_bool_value(v);
          }
        },
        val);
  }
}

void TraceBeginImpl(const Track &track, std::string_view category,
                    std::string_view name, uint64_t timestamp,
                    std::initializer_list<TraceArg> args) {
  if (!track.enabled()) return;
  lv::trace::LVDataSource::Trace(
      [&](lv::trace::LVDataSource::TraceContext ctx) {
        auto packet = ctx.NewTracePacket();
        packet->set_timestamp(timestamp);
        auto *event = packet->set_track_event();
        event->set_type(perfetto::protos::pbzero::TrackEvent::TYPE_SLICE_BEGIN);
        event->set_track_uuid(track.uuid());
        event->set_name(name.data(), name.size());
        event->add_categories(category.data(), category.size());
        AddArgs(event, args);
      });
}

void TraceEndImpl(const Track &track, uint64_t timestamp) {
  if (!track.enabled()) return;
  lv::trace::LVDataSource::Trace(
      [&](lv::trace::LVDataSource::TraceContext ctx) {
        auto packet = ctx.NewTracePacket();
        packet->set_timestamp(timestamp);
        auto *event = packet->set_track_event();
        event->set_type(perfetto::protos::pbzero::TrackEvent::TYPE_SLICE_END);
        event->set_track_uuid(track.uuid());
      });
}

template <typename T>
void TraceCounter(const Track &track, std::string_view category, T val,
                  uint64_t timestamp, std::initializer_list<TraceArg> args) {
  static_assert(std::is_arithmetic_v<T>,
                "TraceCounter requires an arithmetic type");

  if (!track.enabled()) return;
  if (!track.is_counter()) {
    LV_WARNING("Track is not a counter track");
    return;
  }

  lv::trace::LVDataSource::Trace(
      [&](lv::trace::LVDataSource::TraceContext ctx) {
        auto packet = ctx.NewTracePacket();
        packet->set_timestamp(timestamp);
        auto *event = packet->set_track_event();
        event->set_type(perfetto::protos::pbzero::TrackEvent::TYPE_COUNTER);
        event->set_track_uuid(track.uuid());
        event->add_categories(category.data(), category.size());

        if constexpr (std::is_floating_point_v<T>) {
          event->set_double_counter_value(static_cast<double>(val));
        } else {
          event->set_counter_value(static_cast<int64_t>(val));
        }

        AddArgs(event, args);
      });
}

void TraceInstant(const Track &track, std::string_view category,
                  std::string_view name, uint64_t timestamp,
                  std::initializer_list<TraceArg> args) {
  if (!track.enabled()) return;
  lv::trace::LVDataSource::Trace(
      [&](lv::trace::LVDataSource::TraceContext ctx) {
        auto packet = ctx.NewTracePacket();
        packet->set_timestamp(timestamp);
        auto *event = packet->set_track_event();
        event->set_type(perfetto::protos::pbzero::TrackEvent::TYPE_INSTANT);
        event->set_track_uuid(track.uuid());
        event->add_categories(category.data(), category.size());
        event->set_name(name.data(), name.size());
        AddArgs(event, args);
      });
}

template void TraceCounter<int>(const Track &, std::string_view, int, uint64_t,
                                std::initializer_list<TraceArg>);
template void TraceCounter<uint32_t>(const Track &, std::string_view, uint32_t,
                                     uint64_t, std::initializer_list<TraceArg>);
template void TraceCounter<int64_t>(const Track &, std::string_view, int64_t,
                                    uint64_t, std::initializer_list<TraceArg>);
template void TraceCounter<uint64_t>(const Track &, std::string_view, uint64_t,
                                     uint64_t, std::initializer_list<TraceArg>);
template void TraceCounter<float>(const Track &, std::string_view, float,
                                  uint64_t, std::initializer_list<TraceArg>);
template void TraceCounter<double>(const Track &, std::string_view, double,
                                   uint64_t, std::initializer_list<TraceArg>);

LV_BINDING(trace, Track)
    .constructor(
        [](std::string_view name, bool is_counter) {
          return CreateSubTrackWithName("Lua", name, is_counter);
        },
        lv::params(lv::param("name"), lv::param("is_counter")),
        lv::doc("Create a Lua trace track."));

LV_MODULE(trace)
    .function(
        "event_begin",
        [](const Track &track, const char *category, const char *name) {
          LV_TRACE_BEGIN(track, category, name);
        },
        lv::params(lv::param("track"), lv::param("category"),
                   lv::param("name")),
        lv::doc("Begin a trace event on a track."))
    .function(
        "event_end",
        [](const Track &track) {
          LV_TRACE_END(track);
        },
        lv::params(lv::param("track")),
        lv::doc("End the current trace event on a track."))
    .function(
        "event_instant",
        [](const Track &track, const char *category, const char *name) {
          LV_TRACE_INSTANT(track, category, name);
        },
        lv::params(lv::param("track"), lv::param("category"),
                   lv::param("name")),
        lv::doc("Emit an instant trace event on a track."));

}  // namespace trace
}  // namespace lv
