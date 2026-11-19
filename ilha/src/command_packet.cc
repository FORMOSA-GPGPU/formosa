// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

// Exposes the Host ABI that the HAL and the CP firmware share to Lua decoders
// such as ilha.command_trace, so that they keep no copy of it: the C
// declarations of hal/src/hal/command_packet.h and formosa-hal/api.h for the
// LuaJIT FFI, and the macro constants that the FFI cannot see.

#include "command_packet.h"

#include <liblv/binding.h>

#include <cstddef>
#include <cstdint>

namespace ilha {
namespace {

// The declarations, preprocessed at build time without the system headers and
// the static assertions, which ffi.cdef cannot parse.
constexpr char kCdef[] =
#include "command_packet_cdef.inc"
    ;

struct CommandPacketBytes {
  const std::size_t offset;
  const std::size_t size;
};

// Whether a mask selects whole, contiguous bytes of a 64-bit word.
constexpr bool SelectsBytes(uint64_t mask) {
  const uint64_t low = mask >> __builtin_ctzll(mask);
  return __builtin_ctzll(mask) % 8 == 0 &&
         __builtin_popcountll(mask) % 8 == 0 && (low & (low + 1)) == 0;
}

// The bytes that a mask selects in a little-endian 64-bit word.
constexpr CommandPacketBytes MaskBytes(uint64_t mask) {
  return {static_cast<std::size_t>(__builtin_ctzll(mask) / 8),
          static_cast<std::size_t>(__builtin_popcountll(mask) / 8)};
}

static_assert(SelectsBytes(FSA_COMPLETION_TOKEN_SLOT_INDEX_MASK));
static_assert(SelectsBytes(FSA_COMPLETION_TOKEN_ALLOC_TAG_MASK));
static_assert(SelectsBytes(FSA_COMPLETION_RESULT_MASK));

// Plain C++ ABI metadata; const members are read-only through the binding.
struct CommandCompletionAbi {
  const CommandPacketBytes slot_index =
      MaskBytes(FSA_COMPLETION_TOKEN_SLOT_INDEX_MASK);
  const CommandPacketBytes alloc_tag =
      MaskBytes(FSA_COMPLETION_TOKEN_ALLOC_TAG_MASK);
  const CommandPacketBytes result = MaskBytes(FSA_COMPLETION_RESULT_MASK);
  const uint64_t pending = FSA_COMPLETION_RESULT_PENDING;
  const uint64_t success = FSA_COMPLETION_RESULT_SUCCESS;
};

struct CommandDispatchFlags {
  const uint32_t dim_mask = FSA_KERNEL_DISPATCH_DIM_MASK;
  const uint32_t printf_meta = FSA_KERNEL_DISPATCH_HAS_PRINTF_META;
  const uint32_t stack_remap = FSA_KERNEL_DISPATCH_STACK_REMAP;
};

struct CommandPacketAbi {
  const char *const cdef = kCdef;
  const CommandCompletionAbi completion;
  const CommandDispatchFlags dispatch_flags;
};

LV_BINDING(ilha, CommandPacketBytes)
    .property("offset", &CommandPacketBytes::offset)
    .property("size", &CommandPacketBytes::size);

LV_BINDING(ilha, CommandCompletionAbi)
    .property("slot_index", &CommandCompletionAbi::slot_index)
    .property("alloc_tag", &CommandCompletionAbi::alloc_tag)
    .property("result", &CommandCompletionAbi::result)
    .property("pending", &CommandCompletionAbi::pending)
    .property("success", &CommandCompletionAbi::success);

LV_BINDING(ilha, CommandDispatchFlags)
    .property("dim_mask", &CommandDispatchFlags::dim_mask)
    .property("printf_meta", &CommandDispatchFlags::printf_meta)
    .property("stack_remap", &CommandDispatchFlags::stack_remap);

LV_BINDING(ilha, CommandPacketAbi)
    .property("cdef", &CommandPacketAbi::cdef)
    .property("completion", &CommandPacketAbi::completion)
    .property("dispatch_flags", &CommandPacketAbi::dispatch_flags);

}  // namespace

LV_MODULE(ilha).value(
    "command_packet_abi", CommandPacketAbi{},
    lv::doc("Read-only Host ABI from hal/src/hal/command_packet.h for Lua "
            "decoders: { "
            "cdef = the C declarations of command_packet.h and "
            "formosa-hal/api.h, for ffi.cdef, completion = { slot_index, "
            "alloc_tag, result = { offset, size }, pending, success }, "
            "dispatch_flags = { dim_mask, printf_meta, stack_remap } }; "
            "completion fields are bytes of a completion token or slot, and "
            "dispatch_flags are bits of a kernel dispatch's dim_reserved0"));

}  // namespace ilha
