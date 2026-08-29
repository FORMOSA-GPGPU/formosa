/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <array>
#include <tuple>

#include "cores/instr.h"

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

// Recursively scan the OpcodeTuple and extract all unique types.
template <typename Tuple>
struct ExtractUniqueFmts;

template <>
struct ExtractUniqueFmts<std::tuple<>> {
  using type = std::tuple<>;
};

template <typename Head, typename... Tail>
struct ExtractUniqueFmts<std::tuple<Head, Tail...>> {
  using RestUnique = typename ExtractUniqueFmts<std::tuple<Tail...>>::type;
  using type = typename AppendUniqueImpl<
      RestUnique, typename Head::Fmt,
      HasType<typename Head::Fmt, RestUnique>::value>::type;
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

template <typename OpcodeTuple>
class Decoder {
 public:
  static Instr Decode(uint32_t iword) {
    return DecodeImpl(iword, std::make_index_sequence<kNumInstrs>{});
  }

 private:
  using UniqueFmtsTuple = typename ExtractUniqueFmts<OpcodeTuple>::type;

  static constexpr auto kNumInstrs = std::tuple_size_v<OpcodeTuple>;
  static constexpr auto kNumUniqueFmts = std::tuple_size_v<UniqueFmtsTuple>;

  struct DecodeEntry {
    ExecFunc exec;
    MnemonicFunc mnemonic;
    std::size_t fmt_idx;
  };

  template <typename InstrDef>
  static constexpr DecodeEntry MakeEntry() {
    return {&InstrDef::Execute, &InstrDef::Mnemonic,
            TupleIndex<typename InstrDef::Fmt, UniqueFmtsTuple>::value};
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

    std::array<Instr, kNumUniqueFmts> prefilled;
    FillAllUnique<UniqueFmtsTuple>(prefilled.data(), iword,
                                   std::make_index_sequence<kNumUniqueFmts>{});

    Instr instr = prefilled[entry.fmt_idx];
    instr.exec_ = entry.exec;
    instr.mnemonic_ = entry.mnemonic;
    return instr;
  }
};

Instr Decode(uint32_t iword);

}  // namespace simtix
