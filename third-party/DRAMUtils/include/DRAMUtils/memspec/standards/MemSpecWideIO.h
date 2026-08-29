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

#ifndef DRAMUTILS_MEMSPEC_STANDARDS_MEMSPECWIDEIO_H
#define DRAMUTILS_MEMSPEC_STANDARDS_MEMSPECWIDEIO_H

#include "DRAMUtils/util/json_utils.h"
#include <string>
#include <string_view>

namespace DRAMUtils::MemSpec
{

struct MemArchitectureSpecTypeWideIO
{
    uint64_t nbrOfRows;
    uint64_t nbrOfColumns;
    uint64_t burstLength;
    std::optional<uint64_t> maxBurstLength;
    uint64_t dataRate;
    uint64_t width;
    uint64_t nbrOfChannels;
    uint64_t nbrOfRanks;
    uint64_t nbrOfBanks;
    uint64_t nbrOfDevices;
};
NLOHMANN_JSONIFY_ALL_THINGS(MemArchitectureSpecTypeWideIO,
                            nbrOfRows,
                            nbrOfColumns,
                            burstLength,
                            maxBurstLength,
                            dataRate,
                            width,
                            nbrOfChannels,
                            nbrOfRanks,
                            nbrOfBanks,
                            nbrOfDevices)

struct MemTimingSpecTypeWideIO
{
    double tCK;
    uint64_t CKE;
    uint64_t CKESR;
    uint64_t RAS;
    uint64_t RC;
    uint64_t RCD;
    uint64_t RL;
    uint64_t WL;
    uint64_t WR;
    uint64_t XP;
    uint64_t XSR;
    uint64_t REFI;
    uint64_t RFC;
    uint64_t RP;
    uint64_t DQSCK;
    uint64_t AC;
    uint64_t CCD_R;
    uint64_t CCD_W;
    uint64_t RRD;
    uint64_t TAW;
    uint64_t WTR;
    uint64_t RTRS;
};
NLOHMANN_JSONIFY_ALL_THINGS(MemTimingSpecTypeWideIO,
                            tCK,
                            CKE,
                            CKESR,
                            RAS,
                            RC,
                            RCD,
                            RL,
                            WL,
                            WR,
                            XP,
                            XSR,
                            REFI,
                            RFC,
                            RP,
                            DQSCK,
                            AC,
                            CCD_R,
                            CCD_W,
                            RRD,
                            TAW,
                            WTR,
                            RTRS)

struct MemSpecWideIO
{
    static constexpr inline const std::string_view id = "WIDEIO_SDR";
    std::string memoryId;

    MemArchitectureSpecTypeWideIO memarchitecturespec;
    MemTimingSpecTypeWideIO memtimingspec;
};
NLOHMANN_JSONIFY_ALL_THINGS(MemSpecWideIO, memoryId, memarchitecturespec, memtimingspec)

} // namespace DRAMUtils::MemSpec

#endif /* DRAMUTILS_MEMSPEC_STANDARDS_MEMSPECWIDEIO_H */
