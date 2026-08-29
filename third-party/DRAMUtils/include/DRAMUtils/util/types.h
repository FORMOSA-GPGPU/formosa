/*
 * Copyright (c) 2024, RPTU Kaiserslautern-Landau
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER
 * OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Authors:
 *    Marco Mörz
 */

#ifndef DRAMUTILS_UTIL_TYPES_H
#define DRAMUTILS_UTIL_TYPES_H

#include <string_view>
#include <type_traits>
#include <variant>

namespace DRAMUtils::util
{

// Helper struct to create a type sequence
template <typename... Ts> struct type_sequence
{
};

// Helper struct for static assert
template <typename> struct always_false : std::false_type
{
};

// Helper struct to check if a type is in a type sequence
// forward declaration
template <typename T, typename Seq> struct is_one_of;
// specialization
template <typename T, typename... Ts>
struct is_one_of<T, type_sequence<Ts...>> : std::bool_constant<(std::is_same_v<T, Ts> || ...)>
{
};

// Helper struct to check if types in a sequence are unique
// forward declaration
template <typename... Ts> struct unique_types : std::true_type
{
}; // empty case
// specialization
template <typename First, typename... Ts>
struct unique_types<First, Ts...>
    : std::bool_constant<(sizeof...(Ts) == 0) ||                 // single type case
                         (!(std::is_same_v<First, Ts> || ...) && // check if First is not in Ts...
                          unique_types<Ts...>::value)            // recursive check
                         >
{
};
template <typename... Ts> struct unique_types<type_sequence<Ts...>> : unique_types<Ts...>
{
};

// Helper
// Default case
template <typename T, typename = void> struct has_noexcept_id_field : std::false_type
{
};
// specialization
template <typename T>
struct has_noexcept_id_field<T, std::void_t<decltype(T::id)>> // Check if T::id exists
{
    static constexpr bool value = noexcept(T::id);
};
// specialization
template <typename... Ts>
struct has_noexcept_id_field<type_sequence<Ts...>>
    : std::bool_constant<(has_noexcept_id_field<Ts>::value && ...)>
{
};

// Helper struct to check if all ids are unique
// forward declaration
template <typename Seq, typename = void> struct are_ids_unique : std::false_type
{
};
// specialization
template <typename T, typename... Ts>
struct are_ids_unique<type_sequence<T, Ts...>, std::void_t<decltype(T::id), decltype(Ts::id)...>>
{
    static constexpr bool value =
        ((T::id != Ts::id) && ...) && are_ids_unique<type_sequence<Ts...>>::value;
};
// specialization empty case
template <> struct are_ids_unique<type_sequence<>>
{
    static constexpr bool value = true;
};

// Helper is type const std::string_view
template <typename T, typename = void> struct is_id_const_string_view : std::false_type
{
};
// specialization
template <typename T>
struct is_id_const_string_view<T, std::void_t<decltype(T::id)>> // Check if T::id exists
{
    static constexpr bool value = std::is_same_v<decltype(T::id), const std::string_view>;
};
// specialization
template <typename... Ts>
struct is_id_const_string_view<type_sequence<Ts...>>
    : std::bool_constant<(is_id_const_string_view<Ts>::value && ...)>
{
};

// std::variant type_sequence variant helper
// constexpr std::string_view id field is required
// forward declaration
template <typename Seq, typename enable = void> struct type_sequence_id_variant
{
    static_assert(always_false<Seq>::value, "Invalid type sequence for variant");
};
// specialization
template <typename... Ts>
struct type_sequence_id_variant<
    type_sequence<Ts...>,
    std::void_t<std::enable_if_t<has_noexcept_id_field<type_sequence<Ts...>>::value>,
                std::enable_if_t<are_ids_unique<type_sequence<Ts...>>::value>,
                std::enable_if_t<is_id_const_string_view<type_sequence<Ts...>>::value>>>
{
    using type = std::variant<Ts...>;
};
// type_sequence_id_variant_t
template <typename Seq>
using type_sequence_id_variant_t = typename type_sequence_id_variant<Seq>::type;

} // namespace DRAMUtils::util

#endif /* DRAMUTILS_UTIL_TYPES_H */
