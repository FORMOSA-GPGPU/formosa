// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include "cores/pipelined/param.h"
#include "cores/pipelined/scoreboard.h"
#include "liblv/binding.h"

namespace simtix::pipelined {

class ScoreboardTester {
 public:
  ScoreboardTester(const ArchParam &param, const Param &pipe_param)
      : num_subcores_(pipe_param.num_subcores), pool_(param, 10) {
    scoreboards_.reserve(pipe_param.num_subcores);
    uint32_t num_warps_per_subcore = param.num_warps / pipe_param.num_subcores;
    for (uint32_t i = 0; i < num_subcores_; ++i) {
      scoreboards_.emplace_back(num_warps_per_subcore, pipe_param.num_subcores);
    }
  }

  // Lua bindings
  bool can_issue(uint32_t wid, const Instr &instr) {
    Packet *p = pool_.Acquire();
    p->wid = wid;
    p->instr = instr;
    bool ret = scoreboards_[get_subcore_id(wid)].CanIssue(p);
    pool_.Release(p);
    return ret;
  }

  void issue(uint32_t wid, const Instr &instr) {
    Packet *p = pool_.Acquire();
    p->wid = wid;
    p->instr = instr;
    scoreboards_[get_subcore_id(wid)].Issue(p);
    pool_.Release(p);
  }

  void commit(uint32_t wid, const Instr &instr) {
    Packet *p = pool_.Acquire();
    p->wid = wid;
    p->instr = instr;
    scoreboards_[get_subcore_id(wid)].Commit(p);
    pool_.Release(p);
  }

 private:
  uint32_t get_subcore_id(uint32_t wid) const { return wid % num_subcores_; }

  const uint32_t num_subcores_;
  PacketPool pool_;
  std::vector<Scoreboard> scoreboards_;
};

LV_BINDING(simtix, ScoreboardTester)
    .constructor(
        [](const ArchParam &param, const Param &pipe_param) {
          return std::make_shared<ScoreboardTester>(param, pipe_param);
        },
        lv::params("param", "pipe_param"),
        lv::doc("Create a scoreboard tester"))
    .method("can_issue", &ScoreboardTester::can_issue,
            lv::params("wid", "instr"),
            lv::doc("Check whether a warp can issue an instruction"))
    .method("issue", &ScoreboardTester::issue, lv::params("wid", "instr"),
            lv::doc("Mark an instruction as issued"))
    .method("commit", &ScoreboardTester::commit, lv::params("wid", "instr"),
            lv::doc("Mark an instruction as committed"));

}  // namespace simtix::pipelined
