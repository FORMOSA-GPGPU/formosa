/* SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 * SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <tuple>
#include <type_traits>
#include <utility>

#include "cores/instr.h"

namespace simtix {

template <uint32_t Mask>
struct UnsignedBits {
  static_assert(Mask != 0);
  static constexpr uint32_t Get(uint32_t word) {
    return (word & Mask) >> __builtin_ctz(Mask);
  }
};

namespace detail {
template <unsigned Width>
constexpr int64_t SignExtend(uint32_t value) {
  constexpr uint32_t sign = 1u << (Width - 1);
  return int64_t(value ^ sign) - sign;
}
}  // namespace detail

struct ImmediateI {
  static constexpr int64_t Get(uint32_t w) {
    return detail::SignExtend<12>(UnsignedBits<INSN_FIELD_IMM12>::Get(w));
  }
};
struct ImmediateS {
  static constexpr int64_t Get(uint32_t w) {
    return detail::SignExtend<12>(((w >> 20) & 0xfe0) | ((w >> 7) & 31));
  }
};
struct ImmediateB {
  static constexpr int64_t Get(uint32_t w) {
    return detail::SignExtend<13>(((w >> 19) & 0x1000) | ((w << 4) & 0x800) |
                                  ((w >> 20) & 0x7e0) | ((w >> 7) & 0x1e));
  }
};
struct ImmediateU {
  static constexpr int64_t Get(uint32_t w) {
    return detail::SignExtend<32>(w & INSN_FIELD_IMM20);
  }
};
struct ImmediateJ {
  static constexpr int64_t Get(uint32_t w) {
    return detail::SignExtend<21>(((w >> 11) & 0x100000) | (w & 0xff000) |
                                  ((w >> 9) & 0x800) | ((w >> 20) & 0x7fe));
  }
};

// A field binds an extraction recipe to a destination once. The member pointer
// is a template argument, so filling a layout needs no runtime descriptor.
template <auto Member, class Extraction>
struct Field {
  using Value =
      std::remove_reference_t<decltype(std::declval<Instr>().*Member)>;
  static void Fill(Instr *instr, uint32_t word) {
    instr->*Member = Extraction::Get(word);
  }
};

#define FOREACH_INSTR_FIELD(V)                    \
  V(Rd, rd_, UnsignedBits<INSN_FIELD_RD>)         \
  V(Rs1, rs1_, UnsignedBits<INSN_FIELD_RS1>)      \
  V(Rs2, rs2_, UnsignedBits<INSN_FIELD_RS2>)      \
  V(Rs3, rs3_, UnsignedBits<INSN_FIELD_RS3>)      \
  V(Rs3FromRd, rs3_, UnsignedBits<INSN_FIELD_RD>) \
  V(Rm, rm_, UnsignedBits<INSN_FIELD_RM>)         \
  V(Csr, csr_, UnsignedBits<INSN_FIELD_CSR>)      \
  V(Uimm, uimm_, UnsignedBits<INSN_FIELD_ZIMM5>)  \
  V(Uimm12, imm_, UnsignedBits<INSN_FIELD_IMM12>) \
  V(Pri, pri_, UnsignedBits<INSN_FIELD_RS2>)      \
  V(ImmI, imm_, ImmediateI)                       \
  V(ImmS, imm_, ImmediateS)                       \
  V(ImmB, imm_, ImmediateB)                       \
  V(ImmU, imm_, ImmediateU)                       \
  V(ImmJ, imm_, ImmediateJ)

// Only this declaration needs access to Instr's private members. Offsets are
// used exclusively for compile-time validation of overlapping union storage.
struct InstrFields {
#define DECL_FIELD(NAME, MEMBER, EXTRACTION)                   \
  struct NAME : Field<&Instr::MEMBER, EXTRACTION> {            \
    static constexpr size_t kOffset = offsetof(Instr, MEMBER); \
  };
  FOREACH_INSTR_FIELD(DECL_FIELD)
#undef DECL_FIELD
};

#define EXPORT_FIELD(NAME, MEMBER, EXTRACTION) using NAME = InstrFields::NAME;
FOREACH_INSTR_FIELD(EXPORT_FIELD)
#undef EXPORT_FIELD

namespace detail {

template <class... F>
struct DisjointFields : std::true_type {};
template <class First, class... Rest>
struct DisjointFields<First, Rest...>
    : std::bool_constant<
          (((First::kOffset + sizeof(typename First::Value) <= Rest::kOffset ||
             Rest::kOffset + sizeof(typename Rest::Value) <= First::kOffset)) &&
           ...) &&
          DisjointFields<Rest...>::value> {};

template <class... F>
struct FieldList {
  static_assert(DisjointFields<F...>::value,
                "Operand fields overlap in Instr storage");
  static void Fill(Instr *instr, uint32_t word) { (F::Fill(instr, word), ...); }
};

#define FIELD_TUPLE(NAME, MEMBER, EXTRACTION) std::tuple<NAME>{},
using FieldCatalog =
    decltype(std::tuple_cat(FOREACH_INSTR_FIELD(FIELD_TUPLE) std::tuple<>{}));
#undef FIELD_TUPLE

template <class Requested, class Catalog, class Result = FieldList<>>
struct CanonicalFields;
template <class... Requested, class... Result>
struct CanonicalFields<std::tuple<Requested...>, std::tuple<>,
                       FieldList<Result...>> {
  static_assert(sizeof...(Requested) == sizeof...(Result),
                "Unknown or duplicate operand field");
  using type = FieldList<Result...>;
};
template <class... Requested, class Head, class... Tail, class... Result>
struct CanonicalFields<std::tuple<Requested...>, std::tuple<Head, Tail...>,
                       FieldList<Result...>>
    : CanonicalFields<std::tuple<Requested...>, std::tuple<Tail...>,
                      std::conditional_t<
                          (std::is_same_v<Head, Requested> || ...),
                          FieldList<Result..., Head>, FieldList<Result...>>> {};

}  // namespace detail

// Canonical order lets declarations share one layout regardless of field order.
template <class... F>
using Fields = typename detail::CanonicalFields<std::tuple<F...>,
                                                detail::FieldCatalog>::type;

#undef FOREACH_INSTR_FIELD

}  // namespace simtix
