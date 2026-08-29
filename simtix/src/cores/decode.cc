// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include "cores/decode.h"

#include "cores/encoding.h"
#include "cores/instr_def.h"
#include "liblv/binding.h"

namespace simtix {

Instr Decode(uint32_t iword) {
  switch (iword & INSN_FIELD_OPCODE) {
    case kLoad:
      return Decoder<InstrLoad>::Decode(iword);
    case kAmo:
      return Decoder<InstrAmo>::Decode(iword);
    case kStore:
      return Decoder<InstrStore>::Decode(iword);
    case kBranch:
      return Decoder<InstrBranch>::Decode(iword);
    case kCustom1:
      return Decoder<InstrFormosa>::Decode(iword);
    case kOpImm:
      return Decoder<InstrOpImm>::Decode(iword);
    case kOp:
      return Decoder<InstrOp>::Decode(iword);
    case kSystem:
      return Decoder<InstrSystem>::Decode(iword);
    case kAuipc:
      return Decoder<InstrAuipc>::Decode(iword);
    case kLui:
      return Decoder<InstrLui>::Decode(iword);
    case kOpImm32:
      return Decoder<InstrOpImm32>::Decode(iword);
    case kOp32:
      return Decoder<InstrOp32>::Decode(iword);
    case kOpFp:
      return Decoder<InstrOpFp>::Decode(iword);
    case kFmadd:
    case kFmsub:
    case kFnmsub:
    case kFnmadd:
      return Decoder<InstrOpFused>::Decode(iword);
    case kJal:
      return Decoder<InstrJal>::Decode(iword);
    case kJalr:
      return Decoder<InstrJalr>::Decode(iword);
    default:
      return Decoder<InstrIllegal>::Decode(iword);
  }
}

LV_MODULE(simtix).function("decode", &Decode, lv::params("iword"),
                           lv::doc("Decode a RISC-V instruction word"));

}  // namespace simtix
