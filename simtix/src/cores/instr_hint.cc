// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include "cores/instr_def.h"

namespace simtix {

// This model does not act on locality hints. Each hint has its own identity,
// and no register operands or writes.
#define DEFINE_HINT(NAME, FORMATTER, FLAGS, ...)         \
  ExecFlag NAME::Execute(ExecContext *, const Instr &) { \
    return ExecFlag::NONE;                               \
  }
FOREACH_INSTR_NTL(DEFINE_HINT)
#undef DEFINE_HINT

}  // namespace simtix
