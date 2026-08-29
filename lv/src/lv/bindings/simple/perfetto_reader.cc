// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include "perfetto_reader.h"

#include <liblv/binding.h>
#include <liblv/output.h>

#include <memory>
#include <string>
#include <systemc>
#include <utility>
#include <vector>

namespace simple {
PerfettoReader::PerfettoReader(const sc_module_name &name,
                               const std::vector<PfTrack> &tracks)
    : sc_module(name), slave_("slave", nullptr) {
  for (auto &t : tracks) {
    if ((t.pf_type == PfTrack::PR_SLICE || t.pf_type == PfTrack::PR_INSTANT) &&
        t.content_name.empty()) {
      LV_FATAL(
          "PerfettoReader {} track '{}' with type {} requires content_name",
          this->name(), t.track_name, static_cast<int>(t.pf_type));
    }
    if (t.pf_type == PfTrack::PR_COUNTER) {
      tracks_.emplace_back(
          std::make_pair(t, LV_NEW_MODULE_COUNTER_TRACK(t.track_name)));
    } else {
      tracks_.emplace_back(
          std::make_pair(t, LV_NEW_MODULE_TRACK(t.track_name)));
    }
  }
  SC_THREAD(monitor_proc);
}

void PerfettoReader::monitor_proc() {
  while (true) {
    auto *trans = slave_.req_port->read();
    auto cmd = trans->get_command();
    auto addr = trans->get_address() / 8;
    auto *data_ptr = reinterpret_cast<int *>(trans->get_data_ptr());

    if (cmd == tlm::TLM_READ_COMMAND) {
      *data_ptr = 0;
      trans->set_response_status(tlm::TLM_OK_RESPONSE);
      slave_.resp_port->write(trans);
      continue;
    }

    if (addr >= tracks_.size()) {
      SC_REPORT_ERROR("PerfettoReader", "Address exceeds number of tracks");
    }

    auto &track = tracks_[addr];
    switch (track.first.pf_type) {
      case PfTrack::PR_SLICE:
        if (*data_ptr >= 0) {
          if (static_cast<size_t>(*data_ptr) >=
              track.first.content_name.size()) {
            LV_FATAL(
                "PerfettoReader {} slice track '{}' content_name index {} out "
                "of bounds (size={})",
                this->name(), track.first.track_name, *data_ptr,
                track.first.content_name.size());
          }
          LV_TRACE_BEGIN(track.second, "PerfettoReader",
                         track.first.content_name[*data_ptr]);
        } else {
          LV_TRACE_END(track.second);
        }
        break;
      case PfTrack::PR_INSTANT:
        if (*data_ptr < 0 ||
            static_cast<size_t>(*data_ptr) >= track.first.content_name.size()) {
          LV_FATAL(
              "PerfettoReader {} instant track '{}' content_name index {} out "
              "of bounds (size={})",
              this->name(), track.first.track_name, *data_ptr,
              track.first.content_name.size());
        }
        LV_TRACE_INSTANT(track.second, "PerfettoReader",
                         track.first.content_name[*data_ptr]);
        break;
      case PfTrack::PR_COUNTER:
        LV_TRACE_COUNTER(track.second, "PerfettoReader", *data_ptr);
        break;
      default:
        SC_REPORT_ERROR("PerfettoReader", "Error track type");
    }

    trans->set_response_status(tlm::TLM_OK_RESPONSE);
    slave_.resp_port->write(trans);
  }
}

PerfettoReader::Source *PerfettoReader::slave_port() const {
  return &slave_.port;
}

LV_BINDING(simple, PerfettoReader)
    .constructor(
        [](const char *name,
           const sol::nested<std::vector<sol::table>> &param) {
          const auto &tracks = param.value();
          std::vector<PfTrack> tmp;
          for (auto &t : tracks) {
            std::string name = t.get<std::string>("track_name");
            int type = t.get<int>("type");
            std::vector<std::string> c =
                t.get_or<std::vector<std::string>>("content_name", {});
            tmp.emplace_back(PfTrack{(PfTrack::Type)type, name, c});
          }
          return std::make_shared<PerfettoReader>(name, tmp);
        },
        lv::params(
            lv::param("name"),
            lv::param("tracks",
                      lv::lua_type("{ track_name: string, type: integer, "
                                   "content_name?: string[] }[]"))),
        lv::doc("Create a Perfetto trace MMIO reader"))
    .property("slave_port", &PerfettoReader::slave_port,
              lv::doc("Perfetto trace MMIO slave port"));
}  // namespace simple
