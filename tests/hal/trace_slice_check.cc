// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

// Decodes a Perfetto trace and exits 0 when it holds a slice with a given name
// on a track with a given name, 1 when it does not, and 2 when the trace
// cannot be read.

#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

#include "perfetto.h"

namespace {
using perfetto::protos::pbzero::Trace;
using perfetto::protos::pbzero::TracePacket;
using perfetto::protos::pbzero::TrackDescriptor;
using perfetto::protos::pbzero::TrackEvent;
}  // namespace

int main(int argc, char **argv) {
  if (argc != 4) {
    std::cerr << "usage: " << argv[0] << " TRACE TRACK SLICE\n";
    return 2;
  }
  std::ifstream file(argv[1], std::ios::binary);
  if (!file) {
    std::cerr << "cannot read " << argv[1] << '\n';
    return 2;
  }
  const std::string data((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());
  const std::string track = argv[2];
  const std::string slice = argv[3];

  // A track's descriptor may follow its first event, so match at the end.
  std::map<uint64_t, std::string> track_names;
  std::vector<uint64_t> slice_tracks;
  Trace::Decoder trace(data);
  for (auto it = trace.packet(); it; ++it) {
    TracePacket::Decoder packet(*it);
    if (packet.has_track_descriptor()) {
      TrackDescriptor::Decoder descriptor(packet.track_descriptor());
      track_names[descriptor.uuid()] = descriptor.name().ToStdString();
    }
    if (packet.has_track_event()) {
      TrackEvent::Decoder event(packet.track_event());
      if (event.type() == TrackEvent::TYPE_SLICE_BEGIN &&
          event.name().ToStdString() == slice) {
        slice_tracks.push_back(event.track_uuid());
      }
    }
  }
  for (uint64_t uuid : slice_tracks) {
    if (track_names[uuid] == track) return 0;
  }
  std::cerr << "no slice named \"" << slice << "\" on a track named \"" << track
            << "\"\n";
  return 1;
}
