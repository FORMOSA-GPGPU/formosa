// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

// Checks what the Lua trace API writes by decoding the trace file that
// lv::trace::Finish() completes.

#include <liblv/binding.h>
#include <liblv/log.h>
#include <liblv/trace.h>
#include <systemc.h>
#include <unistd.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#include "perfetto.h"

namespace {

using perfetto::protos::pbzero::DebugAnnotation;
using perfetto::protos::pbzero::Trace;
using perfetto::protos::pbzero::TracePacket;
using perfetto::protos::pbzero::TrackDescriptor;
using perfetto::protos::pbzero::TrackEvent;

void Check(bool condition, const std::string &message) {
  if (!condition) throw std::runtime_error(message);
}

// The scenario's steps, run 10 ns apart. liblv has no `sc` module, so the
// test advances simulated time between steps.
const std::vector<const char *> kSteps = {
    R"(
      track = trace.Track("events", false)
      trace.event_begin(track, "test", "plain")

      -- Parent names built at run time, so that Lua may collect them before
      -- the next track names the same parent.
      local function join(a, b) return a .. b end
      first = trace.Track("first", false, join("gro", "up"))
      collectgarbage("collect")
      for i = 1, 10000 do local _ = join("filler", tostring(i)) end
      second = trace.Track("second", false, join("gr", "oup"))
      joined = trace.Track("joined", false, "Cpp")
    )",
    R"(
      trace.event_end(track)
      trace.event_begin(track, "test", "with_args", {
        { "ratio", 0.5 }, { "count", 3 }, { "skipped", nil }, { "label", "cmd" },
        { "flag", true }, { "neg", -7 },
      })
    )",
    R"(
      trace.event_end(track)
      trace.event_instant(track, "test", "instant", { { "addr", 0x1000 } })

      local function rejects(args)
        return not pcall(trace.event_instant, track, "test", "bad", args)
      end
      assert(rejects({ { 2, 3 } }), "accepted a non-string key")
      assert(rejects({ { "bad", {} } }), "accepted a table value")
      assert(rejects({ bad = 1 }), "accepted a key = value field")
      assert(rejects({ "flat" }), "accepted an argument that is not a pair")
    )",
};

// Runs the scenario and completes its trace.
void RunScenario(const std::string &prefix) {
  auto &lua = lv::Runtime();
  lua["output_prefix"] = prefix;
  // Before the first track, which starts tracing.
  lua.safe_script("trace.settings.output_prefix = output_prefix");
  // The C++ macros share the Lua bindings' writer. Referring to them also
  // links trace.cc, and with it the Lua `trace` module, out of liblv.
  const auto cpp = lv::trace::CreateSubTrackWithName("Cpp", "cpp", false);
  LV_TRACE_INSTANT(cpp, "test", "cpp_instant",
                   LV_TRACE_ARG("addr", uint64_t{0x1000}),
                   LV_TRACE_ARG("label", "cmd"));
  for (size_t step = 0; step < kSteps.size(); ++step) {
    if (step > 0) sc_start(10, SC_NS);
    lua.safe_script(kSteps[step]);
  }
  lv::trace::Finish();
}

struct Event {
  uint64_t ts;
  int type;
  std::string name;
  // Annotations in trace order, as name and printed value.
  std::vector<std::pair<std::string, std::string>> args;
};

std::string Value(const DebugAnnotation::Decoder &arg) {
  if (arg.has_int_value()) return "int:" + std::to_string(arg.int_value());
  if (arg.has_uint_value()) return "uint:" + std::to_string(arg.uint_value());
  if (arg.has_double_value()) {
    return "double:" + std::to_string(arg.double_value());
  }
  if (arg.has_bool_value()) {
    return std::string("bool:") + (arg.bool_value() ? "true" : "false");
  }
  if (arg.has_string_value())
    return "string:" + arg.string_value().ToStdString();
  return "?";
}

std::string ReadFile(const std::string &path) {
  std::ifstream file(path, std::ios::binary);
  Check(file.good(), "no trace at " + path);
  return std::string((std::istreambuf_iterator<char>(file)),
                     std::istreambuf_iterator<char>());
}

struct TrackInfo {
  uint64_t uuid;
  uint64_t parent;
  std::string name;
};

std::vector<TrackInfo> ReadTracks(const std::string &path) {
  const std::string data = ReadFile(path);
  Trace::Decoder trace(data);
  std::vector<TrackInfo> tracks;
  for (auto it = trace.packet(); it; ++it) {
    TracePacket::Decoder packet(*it);
    if (!packet.has_track_descriptor()) continue;
    TrackDescriptor::Decoder track(packet.track_descriptor());
    tracks.push_back(
        {track.uuid(), track.parent_uuid(), track.name().ToStdString()});
  }
  return tracks;
}

// Decodes the events on the track named `track_name`.
std::vector<Event> ReadEvents(const std::string &path,
                              const std::string &track_name) {
  const std::string data = ReadFile(path);
  Trace::Decoder trace(data);
  std::map<uint64_t, std::string> track_names;
  std::vector<std::pair<uint64_t, Event>> events;
  for (auto it = trace.packet(); it; ++it) {
    TracePacket::Decoder packet(*it);
    if (packet.has_track_descriptor()) {
      TrackDescriptor::Decoder track(packet.track_descriptor());
      track_names[track.uuid()] = track.name().ToStdString();
    }
    if (packet.has_track_event()) {
      TrackEvent::Decoder event(packet.track_event());
      Event out{
          packet.timestamp(), event.type(), event.name().ToStdString(), {}};
      for (auto a = event.debug_annotations(); a; ++a) {
        DebugAnnotation::Decoder arg(*a);
        out.args.emplace_back(arg.name().ToStdString(), Value(arg));
      }
      events.emplace_back(event.track_uuid(), std::move(out));
    }
  }
  std::vector<Event> found;
  for (auto &[uuid, event] : events) {
    if (track_names[uuid] == track_name) found.push_back(std::move(event));
  }
  return found;
}

void CheckArguments(const std::vector<Event> &events) {
  Check(events.size() == 5,
        "expected 5 events, got " + std::to_string(events.size()));
  const auto &plain = events[0];
  Check(plain.name == "plain" && plain.ts == 0 &&
            plain.type == TrackEvent::TYPE_SLICE_BEGIN && plain.args.empty(),
        "an event without arguments changed");

  const auto &with_args = events[2];
  const std::vector<std::pair<std::string, std::string>> expected{
      {"ratio", "double:0.500000"},
      {"count", "int:3"},
      {"label", "string:cmd"},
      {"flag", "bool:true"},
      {"neg", "int:-7"}};
  Check(with_args.name == "with_args" && with_args.ts == 10 &&
            with_args.args == expected,
        "slice arguments were not kept in order with their types, or a nil "
        "value was not skipped");

  const auto &instant = events[4];
  Check(instant.name == "instant" && instant.ts == 20 &&
            instant.type == TrackEvent::TYPE_INSTANT &&
            instant.args ==
                std::vector<std::pair<std::string, std::string>>{
                    {"addr", "int:4096"}},
        "instant arguments were lost");
}

void CheckParents(const std::vector<TrackInfo> &tracks) {
  std::map<std::string, std::vector<TrackInfo>> named;
  for (const auto &track : tracks) named[track.name].push_back(track);
  for (const char *name :
       {"group", "Cpp", "Lua", "first", "second", "joined", "cpp", "events"}) {
    Check(named[name].size() == 1, std::string("expected one track named ") +
                                       name + ", got " +
                                       std::to_string(named[name].size()));
  }
  const auto uuid = [&](const char *name) {
    return named[name][0].uuid;
  };
  const auto parent = [&](const char *name) {
    return named[name][0].parent;
  };
  Check(parent("first") == uuid("group") && parent("second") == uuid("group"),
        "tracks naming the same parent were split");
  Check(parent("joined") == uuid("Cpp") && parent("cpp") == uuid("Cpp"),
        "a Lua track did not join the parent of a C++ track");
  Check(parent("events") == uuid("Lua"),
        "a track without a parent left the Lua group");
}

void CheckCppArguments(const std::vector<Event> &events) {
  const std::vector<std::pair<std::string, std::string>> expected{
      {"addr", "uint:4096"}, {"label", "string:cmd"}};
  Check(events.size() == 1 && events[0].name == "cpp_instant" &&
            events[0].type == TrackEvent::TYPE_INSTANT &&
            events[0].args == expected,
        "C++ instant arguments changed");
}

}  // namespace

int sc_main(int, char *[]) {
#if !ENABLE_PERFETTO
  std::cout << "Pass! Perfetto is disabled, so there is no trace to check.\n";
  return 0;
#endif
  const std::string prefix = (std::filesystem::temp_directory_path() /
                              ("lv-trace-test-" + std::to_string(getpid())))
                                 .string();
  const std::string path = prefix + ".pftrace";
  try {
    RunScenario(prefix);
    CheckArguments(ReadEvents(path, "events"));
    CheckCppArguments(ReadEvents(path, "cpp"));
    CheckParents(ReadTracks(path));
    std::filesystem::remove(path);
    std::cout << "Pass!\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    std::filesystem::remove(path);
    return 1;
  }
}
