// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include "cores/instr.h"

#include <liblv/binding.h>

#include "cores/instr_def.h"

namespace simtix {

bool Instr::is_call() const { return (is<JAL>() || is<JALR>()) && rd() == 1; }

bool Instr::is_ret() const {
  // RISC-V `ret` == jalr x0, 0(x1/ra).
  return is<JALR>() && rd() == 0 && rs1() == 1 && imm() == 0;
}

LV_BINDING(simtix, Instr)
    .property("rd", &Instr::rd, lv::doc("Destination register index"))
    .property("rs1", &Instr::rs1, lv::doc("First source register index"))
    .property("rs2", &Instr::rs2, lv::doc("Second source register index"))
    .property("rs3", &Instr::rs3, lv::doc("Third source register index"))
    .property("is_branch", &Instr::is_branch,
              lv::doc("Whether this is a conditional branch instruction"))
    .property("is_jump", &Instr::is_jump,
              lv::doc("Whether this is a jump instruction"))
    .property("is_cti", &Instr::is_cti,
              lv::doc("Whether this is a branch, jump, trap request or return"))
    .property("is_memory_access", &Instr::is_memory_access,
              lv::doc("Whether this is a load, store or atomic memory access"));

}  // namespace simtix
