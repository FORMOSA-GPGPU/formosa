// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include "cores/decode.h"

#include "cores/encoding.h"
#include "cores/instr_def.h"
#include "liblv/binding.h"

namespace simtix {

Instr Decode(uint32_t iword) { return Decode<DefaultInstructionSet>(iword); }

LV_MODULE(simtix).function("decode", &Decode<DefaultInstructionSet>,
                           lv::params("iword"),
                           lv::doc("Decode a RISC-V instruction word"));

}  // namespace simtix
