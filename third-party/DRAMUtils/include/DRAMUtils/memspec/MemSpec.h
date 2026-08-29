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

#ifndef DRAMUTILS_MEMSPEC_MEMSPEC_H
#define DRAMUTILS_MEMSPEC_MEMSPEC_H

#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

#include "DRAMUtils/util/id_variant.h"
#include "DRAMUtils/util/json_utils.h"
#include "DRAMUtils/util/types.h"

#include "standards/MemSpecDDR3.h"
#include "standards/MemSpecDDR4.h"
#include "standards/MemSpecDDR5.h"
#include "standards/MemSpecGDDR5.h"
#include "standards/MemSpecGDDR5X.h"
#include "standards/MemSpecGDDR6.h"
#include "standards/MemSpecHBM2.h"
#include "standards/MemSpecHBM3_4.h"
#include "standards/MemSpecLPDDR4.h"
#include "standards/MemSpecLPDDR5.h"
#include "standards/MemSpecLPDDR6.h"
#include "standards/MemSpecSTTMRAM.h"
#include "standards/MemSpecWideIO.h"
#include "standards/MemSpecWideIO2.h"

namespace DRAMUtils::MemSpec
{

// Variant types
using VariantTypes = util::type_sequence<MemSpecDDR3,
                                         MemSpecDDR4,
                                         MemSpecDDR5,
                                         MemSpecLPDDR4,
                                         MemSpecLPDDR5,
                                         MemSpecLPDDR6,
                                         MemSpecWideIO,
                                         MemSpecWideIO2,
                                         MemSpecGDDR5,
                                         MemSpecGDDR5X,
                                         MemSpecGDDR6,
                                         MemSpecHBM2,
                                         MemSpecHBM3,
                                         MemSpecHBM4,
                                         MemSpecSTTMRAM>;

DRAMUTILS_DECLARE_IDVARIANT(MemSpecVariant, "memoryType", VariantTypes)

// Simple MemSpecContainer
struct MemSpecContainer
{
    MemSpecVariant memspec;
};
NLOHMANN_JSONIFY_ALL_THINGS(MemSpecContainer, memspec)

} // namespace DRAMUtils::MemSpec

namespace DRAMUtils
{

namespace detail
{
struct keys
{
    static constexpr char memSpec[] = "memspec";
};

}; // namespace detail

/**
 * @brief Parses Memspec from JSON data into a MemSpecVariant object.
 *
 * This function attempts to parse a MemSpecVariant object from JSON data. It first
 * checks if the provided key exists in the JSON object; if found, it tries to extract
 * and parse the corresponding JSON value into the MemSpecVariant. If no key is provided
 * or the key is not found, it attempts to parse the entire JSON object directly.
 *
 * @param json The json object containing the MemSpec data
 * @param key Optional key to locate the MemSpec data in the json object.
 *            Defaults to "memspec" if not provided.
 *
 * @return An optional MemSpecVariant object if the JSON data was successfully parsed or
 * std::nullopt otherwise.
 */
inline std::optional<MemSpec::MemSpecVariant>
parse_memspec_from_json(const json_t& json, std::string_view key = detail::keys::memSpec)
{
    try
    {
        MemSpec::MemSpecVariant result;
        if (!key.empty() && json.contains(key) && result.from_json(json.at(key)))
            return result;
        else if (result.from_json(json))
            return result;
    }
    catch (std::exception&)
    {
        return std::nullopt;
    }

    return std::nullopt;
}

/**
 * @brief Parses Memspec from a string buffer into a MemSpecVariant object.
 *        This function is a wrapper around parse_memspec_from_json.
 *
 * @param buffer The string buffer containing the MemSpec data
 * @param key Optional key to locate the MemSpec data in the json object.
 *           Defaults to "memspec" if not provided.
 *
 * @return An optional MemSpecVariant object if the JSON data was successfully parsed or
 * std::nullopt otherwise.
 */
inline std::optional<MemSpec::MemSpecVariant>
parse_Memspec_from_buffer(std::string_view buffer, std::string_view key = detail::keys::memSpec)
{
    try
    {
        return parse_memspec_from_json(json_t::parse(buffer), key);
    }
    catch (std::exception&)
    {
        return std::nullopt;
    }
}

/**
 * @brief Parses Memspec from a file into a MemSpecVariant object.
 *       This function is a wrapper around parse_memspec_from_json.
 *
 * @param path The path to the file containing the MemSpec data
 * @param key Optional key to locate the MemSpec data in the json object.
 *          Defaults to "memspec" if not provided.
 *
 * @return An optional MemSpecVariant object if the JSON data was successfully parsed or
 * std::nullopt otherwise.
 */
inline std::optional<MemSpec::MemSpecVariant>
parse_memspec_from_file(const std::filesystem::path& path,
                        std::string_view key = detail::keys::memSpec)
{
    try
    {
        if (!std::filesystem::exists(path))
            return std::nullopt;

        std::ifstream file(path);
        if (!file.is_open())
            return std::nullopt;

        json_t json_obj = json_t::parse(file);
        return parse_memspec_from_json(json_obj, key);
    }
    catch (std::exception&)
    {
        return std::nullopt;
    }
}

} // namespace DRAMUtils

#endif /* DRAMUTILS_MEMSPEC_MEMSPEC_H */
