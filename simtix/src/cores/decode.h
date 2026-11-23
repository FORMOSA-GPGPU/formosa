/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <array>
#include <tuple>
#include <type_traits>

#include "cores/instr_def.h"

namespace simtix {

// Check if T is included in the Tuple
template <typename T, typename Tuple>
struct HasType;

template <typename T, typename... Us>
struct HasType<T, std::tuple<Us...>>
    : std::disjunction<std::is_same<T, Us>...> {};

// If T is not in the Tuple, append T.
template <typename Tuple, typename T, bool contain>
struct AppendUniqueImpl;

template <typename... Us, typename T>
struct AppendUniqueImpl<std::tuple<Us...>, T, false> {
  using type = std::tuple<Us..., T>;
};

template <typename... Us, typename T>
struct AppendUniqueImpl<std::tuple<Us...>, T, true> {
  using type = std::tuple<Us...>;
};

// Deduplicate canonical operand layouts, independently of disassembly format.
template <typename Tuple>
struct ExtractUniqueFields;

template <>
struct ExtractUniqueFields<std::tuple<>> {
  using type = std::tuple<>;
};

template <typename Head, typename... Tail>
struct ExtractUniqueFields<std::tuple<Head, Tail...>> {
  using RestUnique = typename ExtractUniqueFields<std::tuple<Tail...>>::type;
  using type = typename AppendUniqueImpl<
      RestUnique, typename Head::OperandFields,
      HasType<typename Head::OperandFields, RestUnique>::value>::type;
};

template <typename T, typename Tuple>
struct TupleIndex;

template <typename T, typename... Types>
struct TupleIndex<T, std::tuple<T, Types...>> {
  static constexpr std::size_t value = 0;
};

template <typename T, typename U, typename... Types>
struct TupleIndex<T, std::tuple<U, Types...>> {
  static constexpr std::size_t value =
      1 + TupleIndex<T, std::tuple<Types...>>::value;
};

namespace detail {

// Stable union: duplicate types do not create duplicate bitmap entries, while
// first-match precedence between distinct definitions stays with the author.
template <typename Remaining, typename Seen = std::tuple<>>
struct UniqueInstructions;

template <typename Seen>
struct UniqueInstructions<std::tuple<>, Seen> {
  using type = Seen;
};

template <typename Head, typename... Tail, typename Seen>
struct UniqueInstructions<std::tuple<Head, Tail...>, Seen>
    : UniqueInstructions<std::tuple<Tail...>,
                         typename AppendUniqueImpl<
                             Seen, Head, HasType<Head, Seen>::value>::type> {};

// Only matching changes after dispatch verifies bits. Inherited execution,
// disassembly and operand declarations retain the original instruction
// identity.
template <class Definition, uint32_t KnownMask>
struct Residual : Definition {
  static constexpr uint32_t kMaskVal = Definition::kMaskVal & ~KnownMask;
  static constexpr uint32_t kMatchVal = Definition::kMatchVal & ~KnownMask;
  static constexpr bool Match(uint32_t word) {
    return (word & kMaskVal) == kMatchVal;
  }
};
template <class Definition, uint32_t KnownMask>
using Remaining = std::conditional_t<std::is_same_v<Definition, Illegal>,
                                     Illegal, Residual<Definition, KnownMask>>;

template <typename Instructions, uint32_t Opcode>
struct OpcodeCandidates;

template <typename... Definitions, uint32_t Opcode>
struct OpcodeCandidates<std::tuple<Definitions...>, Opcode> {
  // A wildcard in the opcode belongs to every compatible bucket. Retain the
  // non-opcode checks in the leaf; routing has verified the opcode bits.
  using type = decltype(std::tuple_cat(
      std::declval<std::conditional_t<
          (Opcode & Definitions::kMaskVal) ==
              (Definitions::kMatchVal & INSN_FIELD_OPCODE),
          std::tuple<Remaining<Definitions, INSN_FIELD_OPCODE>>,
          std::tuple<>>>()...,
      std::declval<std::tuple<Illegal>>()));

  // C++17 cannot omit switch cases with a pack expansion. Empty buckets get
  // distinct labels outside the 7-bit domain and disappear during optimization;
  // all unsupported opcodes take one shared default case.
  static constexpr uint32_t kCase =
      std::tuple_size_v<type> > 1 ? Opcode : Opcode + 128;
};

}  // namespace detail

// Compose flat instruction tuples. The order is significant for overlapping
// encodings: put a specific alias before its broader instruction definition.
template <typename... Tuples>
using InstructionSet =
    typename detail::UniqueInstructions<decltype(std::tuple_cat(
        std::declval<Tuples>()...))>::type;

template <typename OpcodeTuple>
class Decoder {
 public:
  static Instr Decode(uint32_t iword) {
    constexpr uint32_t bit =
        kNumInstrs > kLeafTarget
            ? SplitBit(std::make_index_sequence<kNumInstrs>{})
            : 0;
    if constexpr (bit == 0) {
      static_assert(kNumInstrs <= 64,
                    "Instruction group cannot be partitioned by encoding");
      return DecodeImpl(iword, std::make_index_sequence<kNumInstrs>{});
    } else {
      // Wildcard patterns remain in both children, in declaration order.
      using Zero =
          decltype(Subset<bit, false>(std::make_index_sequence<kNumInstrs>{}));
      using One =
          decltype(Subset<bit, true>(std::make_index_sequence<kNumInstrs>{}));
      if (iword & bit) return Decoder<One>::Decode(iword);
      return Decoder<Zero>::Decode(iword);
    }
  }

 private:
  // A soft target, including Illegal. Overlapping patterns may be inseparable;
  // those stay together as long as they fit the 64-bit bitmap. Keep this policy
  // private: instruction-set authors only declare instructions and operands.
  static constexpr std::size_t kLeafTarget = 4;

  using UniqueFieldsTuple = typename ExtractUniqueFields<OpcodeTuple>::type;

  static constexpr auto kNumInstrs = std::tuple_size_v<OpcodeTuple>;
  static constexpr auto kNumUniqueFields = std::tuple_size_v<UniqueFieldsTuple>;
  static_assert(kNumInstrs > 0, "An instruction group must not be empty");
  static_assert(std::is_same_v<
                    std::tuple_element_t<kNumInstrs - 1, OpcodeTuple>, Illegal>,
                "An opcode group must end with Illegal");

  template <std::size_t... I>
  static constexpr uint32_t SplitBit(std::index_sequence<I...>) {
    constexpr uint32_t masks[] = {
        std::tuple_element_t<I, OpcodeTuple>::kMaskVal...};
    constexpr uint32_t matches[] = {
        std::tuple_element_t<I, OpcodeTuple>::kMatchVal...};
    std::size_t best = kNumInstrs;
    uint32_t selected = 0;
    for (unsigned b = 0; b < 32; ++b) {
      const uint32_t bit = uint32_t{1} << b;
      std::size_t zero = 0, one = 0;
      for (std::size_t i = 0; i < kNumInstrs; ++i) {
        zero += !(masks[i] & bit) || !(matches[i] & bit);
        one += !(masks[i] & bit) || (matches[i] & bit);
      }
      const auto worst = zero > one ? zero : one;
      if (worst < best) {
        best = worst;
        selected = bit;
      }
    }
    return selected;
  }

  template <uint32_t Bit, bool Value, std::size_t... I>
  static auto Subset(std::index_sequence<I...>) -> decltype(std::tuple_cat(
      std::declval<std::conditional_t<
          !(std::tuple_element_t<I, OpcodeTuple>::kMaskVal & Bit) ||
              bool(std::tuple_element_t<I, OpcodeTuple>::kMatchVal & Bit) ==
                  Value,
          std::tuple<
              detail::Remaining<std::tuple_element_t<I, OpcodeTuple>, Bit>>,
          std::tuple<>>>()...));

  struct DecodeEntry {
    ExecFunc exec;
    DisassembleFunc disassemble;
    std::size_t fields_idx;
    InstrFlags::Bits flags;
  };

  template <typename InstrDef>
  static constexpr DecodeEntry MakeEntry() {
    return {
        &InstrDef::Execute, &InstrDef::Disassemble,
        TupleIndex<typename InstrDef::OperandFields, UniqueFieldsTuple>::value,
        InstrDef::kFlags};
  }

  template <std::size_t... I>
  static constexpr auto CreateTable(std::index_sequence<I...>) {
    return std::array<DecodeEntry, kNumInstrs>{
        {MakeEntry<std::tuple_element_t<I, OpcodeTuple>>()...}};
  }

  static constexpr auto kMetaTable =
      CreateTable(std::make_index_sequence<kNumInstrs>{});

  template <typename UniqueTuple, std::size_t... J>
  static void FillAllUnique(Instr *arr, uint32_t iword,
                            std::index_sequence<J...>) {
    (std::tuple_element_t<J, UniqueTuple>::Fill(&arr[J], iword), ...);
  }

  template <std::size_t... I>
  static Instr DecodeImpl(uint32_t iword, std::index_sequence<I...>) {
    uint64_t bitmap = ((static_cast<uint64_t>(
                            std::tuple_element_t<I, OpcodeTuple>::Match(iword))
                        << I) |
                       ...);

    int index = __builtin_ctzll(bitmap);
    auto entry = kMetaTable[index];

    std::array<Instr, kNumUniqueFields> prefilled;
    FillAllUnique<UniqueFieldsTuple>(
        prefilled.data(), iword, std::make_index_sequence<kNumUniqueFields>{});

    Instr instr = prefilled[entry.fields_idx];
    instr.exec_ = entry.exec;
    instr.disassemble_ = entry.disassemble;
    instr.instr_flags_ = entry.flags;
    return instr;
  }
};

// C++17 cannot expand a parameter pack into switch cases. Enumerate the fixed
// 7-bit opcode domain mechanically; instruction membership comes exclusively
// from mask/match, including opcodes not used by the default instruction set.
template <typename Instructions>
Instr Decode(uint32_t iword) {
  using Set = InstructionSet<Instructions>;
  static_assert(INSN_FIELD_OPCODE == 0x7f);
#define DECODE_OPCODE(OPCODE)                        \
  case detail::OpcodeCandidates<Set, OPCODE>::kCase: \
    return Decoder<                                  \
        typename detail::OpcodeCandidates<Set, OPCODE>::type>::Decode(iword);
#define DECODE_OCTET(BASE) \
  DECODE_OPCODE(BASE)      \
  DECODE_OPCODE(BASE + 1)  \
  DECODE_OPCODE(BASE + 2)  \
  DECODE_OPCODE(BASE + 3)  \
  DECODE_OPCODE(BASE + 4)  \
  DECODE_OPCODE(BASE + 5)  \
  DECODE_OPCODE(BASE + 6)  \
  DECODE_OPCODE(BASE + 7)
// Keep each jump table within one half of the opcode domain. A single wide
// switch changes the compiler's dispatch layout as new opcodes are added.
#define DECODE_HALF(BASE)                                 \
  switch (iword & INSN_FIELD_OPCODE) {                    \
    DECODE_OCTET(BASE + 0x00)                             \
    DECODE_OCTET(BASE + 0x08)                             \
    DECODE_OCTET(BASE + 0x10)                             \
    DECODE_OCTET(BASE + 0x18)                             \
    DECODE_OCTET(BASE + 0x20)                             \
    DECODE_OCTET(BASE + 0x28)                             \
    DECODE_OCTET(BASE + 0x30)                             \
    DECODE_OCTET(BASE + 0x38)                             \
    default:                                              \
      return Decoder<std::tuple<Illegal>>::Decode(iword); \
  }
  if (iword & 0x40) {
    DECODE_HALF(0x40)
  } else {
    DECODE_HALF(0x00)
  }
#undef DECODE_HALF
#undef DECODE_OCTET
#undef DECODE_OPCODE
}

Instr Decode(uint32_t iword);

}  // namespace simtix
