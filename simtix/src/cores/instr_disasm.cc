// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include "cores/instr_disasm.h"

#include "cores/instr_def.h"

namespace simtix {

#define DEFINE_DISASSEMBLY(NAME, FORMATTER, FLAGS, ...)   \
  std::string NAME::Disassemble(const Instr &instr) {     \
    return FORMATTER(instr, std::string_view(kMnemonic)); \
  }
FOREACH_INSTR(DEFINE_DISASSEMBLY)
#undef DEFINE_DISASSEMBLY

}  // namespace simtix
