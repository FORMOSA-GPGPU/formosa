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

#ifndef DRAMUTILS_MEMSPEC_STANDARDS_MEMSPECDDR4_H
#define DRAMUTILS_MEMSPEC_STANDARDS_MEMSPECDDR4_H

#include "DRAMUtils/util/json_utils.h"
#include <optional>
#include <string>
#include <string_view>

namespace DRAMUtils::MemSpec
{

enum class RefModeTypeDDR4
{
    INVALID = -1,
    REF_MODE_1 = 1,
    REF_MODE_2 = 2,
    REF_MODE_4 = 4
};
NLOHMANN_JSON_SERIALIZE_ENUM(RefModeTypeDDR4,
                             {
                                 {RefModeTypeDDR4::INVALID, nullptr},
                                 {RefModeTypeDDR4::REF_MODE_1, 1},
                                 {RefModeTypeDDR4::REF_MODE_2, 2},
                                 {RefModeTypeDDR4::REF_MODE_4, 4},
                             })

struct MemArchitectureSpecTypeDDR4
{
    uint64_t nbrOfChannels;
    uint64_t nbrOfDevices;
    uint64_t nbrOfRanks;
    uint64_t nbrOfBanks;
    uint64_t nbrOfBankGroups;
    uint64_t nbrOfRows;
    uint64_t nbrOfColumns;
    uint64_t burstLength;
    uint64_t dataRate;
    uint64_t width;
    RefModeTypeDDR4 RefMode;
    std::optional<uint64_t> maxBurstLength;
};
NLOHMANN_JSONIFY_ALL_THINGS(MemArchitectureSpecTypeDDR4,
                            nbrOfChannels,
                            nbrOfDevices,
                            nbrOfRanks,
                            nbrOfBanks,
                            nbrOfBankGroups,
                            nbrOfRows,
                            nbrOfColumns,
                            burstLength,
                            dataRate,
                            width,
                            RefMode,
                            maxBurstLength)

struct MemTimingSpecTypeDDR4
{
    double tCK;
    uint64_t CKE;
    uint64_t CKESR;
    uint64_t RAS;
    uint64_t RC;
    uint64_t RCD;
    uint64_t RL;
    uint64_t RPRE;
    uint64_t RTP;
    uint64_t WL;
    uint64_t WPRE;
    uint64_t WR;
    uint64_t XP;
    uint64_t XS;
    uint64_t REFI;
    uint64_t RFC1; // RefModeTypeDDR4::REF_MODE_1 || RefModeTypeDDR4::INVALID
    uint64_t RFC2; // RefModeTypeDDR4::REF_MODE_2
    uint64_t RFC4; // RefModeTypeDDR4::REF_MODE_4
    uint64_t RP;
    uint64_t DQSCK;
    uint64_t CCD_S;
    uint64_t CCD_L;
    uint64_t FAW;
    uint64_t RRD_S;
    uint64_t RRD_L;
    uint64_t WTR_S;
    uint64_t WTR_L;
    uint64_t XPDLL;
    uint64_t XSDLL;
    uint64_t AL;
    uint64_t ACTPDEN;
    uint64_t PRPDEN;
    uint64_t REFPDEN;
    uint64_t RTRS;
};
NLOHMANN_JSONIFY_ALL_THINGS(MemTimingSpecTypeDDR4,
                            tCK,
                            CKE,
                            CKESR,
                            RAS,
                            RC,
                            RCD,
                            RL,
                            RPRE,
                            RTP,
                            WL,
                            WPRE,
                            WR,
                            XP,
                            XS,
                            REFI,
                            RFC1,
                            RFC2,
                            RFC4,
                            RP,
                            DQSCK,
                            CCD_S,
                            CCD_L,
                            FAW,
                            RRD_S,
                            RRD_L,
                            WTR_S,
                            WTR_L,
                            XPDLL,
                            XSDLL,
                            AL,
                            ACTPDEN,
                            PRPDEN,
                            REFPDEN,
                            RTRS)

struct MemPowerSpecTypeDDR4
{
    double vdd;
    double idd0;
    double idd2n;
    double idd3n;
    double idd4r;
    double idd4w;
    double idd6n;
    double idd2p;
    double idd3p;

    double vpp;
    double ipp0;
    double ipp2n;
    double ipp3n;
    double ipp4r;
    double ipp4w;
    double ipp6n;
    double ipp2p;
    double ipp3p;

    // RefModeTypeDDR4::REF_MODE_1 || RefModeTypeDDR4::INVALID
    double idd5B;
    double ipp5B;

    // RefModeTypeDDR4::REF_MODE_2
    double idd5F2;
    double ipp5F2;

    // RefModeTypeDDR4::REF_MODE_4
    double idd5F4;
    double ipp5F4;

    double vddq;

    std::optional<double> iBeta_vdd;
    std::optional<double> iBeta_vpp;
};
NLOHMANN_JSONIFY_ALL_THINGS(MemPowerSpecTypeDDR4,
                            vdd,
                            idd0,
                            idd2n,
                            idd3n,
                            idd4r,
                            idd4w,
                            idd6n,
                            idd2p,
                            idd3p,
                            vpp,
                            ipp0,
                            ipp2n,
                            ipp3n,
                            ipp4r,
                            ipp4w,
                            ipp6n,
                            ipp2p,
                            ipp3p,
                            idd5B,
                            ipp5B,
                            idd5F2,
                            ipp5F2,
                            idd5F4,
                            ipp5F4,
                            vddq,
                            iBeta_vdd,
                            iBeta_vpp)

struct BankWiseSpecTypeDDR4
{
    std::optional<double> factRho;
};
NLOHMANN_JSONIFY_ALL_THINGS(BankWiseSpecTypeDDR4, factRho)

// Pre and Postamble
// Total number of zero cycles relative to tCK (one cycle = tCK) for example, if tCK = 1ns, and
// read_zeroes = 2.5, then the total time is 2.5ns
struct PrePostambleTypeDDR4
{
    // Total number of zero/one cycles per DQs differential pair
    // relative to tCK (one cycle = tCK)
    // for example, if tCK = 1ns, and read_zeroes = 2.5, then the total time is 2.5ns
    double read_zeroes;
    double write_zeroes;
    double read_ones;
    double write_ones;

    // Total number of zero to one and one to zero transitions per DQs differential pair
    uint64_t read_zeroes_to_ones;
    uint64_t write_zeroes_to_ones;
    uint64_t write_ones_to_zeroes;
    uint64_t read_ones_to_zeroes;

    // Minimum time interval between two consecutive read/write commands to prevent merging or
    // seamless transition.
    uint64_t readMinTccd;
    uint64_t writeMinTccd;
};
NLOHMANN_JSONIFY_ALL_THINGS(PrePostambleTypeDDR4,
                            read_zeroes,
                            write_zeroes,
                            read_ones,
                            write_ones,
                            read_zeroes_to_ones,
                            write_zeroes_to_ones,
                            write_ones_to_zeroes,
                            read_ones_to_zeroes,
                            readMinTccd,
                            writeMinTccd)

struct MemImpedanceSpecTypeDDR4
{
    // Clock
    bool ck_termination;
    double ck_R_eq;
    double ck_dyn_E;

    // Commandbus
    bool ca_termination;
    double ca_R_eq;
    double ca_dyn_E;

    // Data bus read
    bool rdq_termination;
    double rdq_R_eq;
    double rdq_dyn_E;
    // Data bus write
    bool wdq_termination;
    double wdq_R_eq;
    double wdq_dyn_E;

    // DQS read
    bool rdqs_termination;
    double rdqs_R_eq;
    double rdqs_dyn_E;
    // DQS write
    bool wdqs_termination;
    double wdqs_R_eq;
    double wdqs_dyn_E;

    // DBI read
    bool rdbi_termination;
    double rdbi_R_eq;
    double rdbi_dyn_E;
    // DBI write
    bool wdbi_termination;
    double wdbi_R_eq;
    double wdbi_dyn_E;
};
NLOHMANN_JSONIFY_ALL_THINGS(MemImpedanceSpecTypeDDR4,
                            ck_termination,
                            ck_R_eq,
                            ck_dyn_E,
                            ca_termination,
                            ca_R_eq,
                            ca_dyn_E,
                            rdq_termination,
                            rdq_R_eq,
                            rdq_dyn_E,
                            wdq_termination,
                            wdq_R_eq,
                            wdq_dyn_E,
                            rdqs_termination,
                            rdqs_R_eq,
                            rdqs_dyn_E,
                            wdqs_termination,
                            wdqs_R_eq,
                            wdqs_dyn_E,
                            rdbi_termination,
                            rdbi_R_eq,
                            rdbi_dyn_E,
                            wdbi_termination,
                            wdbi_R_eq,
                            wdbi_dyn_E)

struct MemSpecDDR4
{
    static constexpr inline const std::string_view id = "DDR4";
    std::string memoryId;

    MemArchitectureSpecTypeDDR4 memarchitecturespec;
    MemPowerSpecTypeDDR4 mempowerspec;
    MemTimingSpecTypeDDR4 memtimingspec;
    std::optional<BankWiseSpecTypeDDR4> bankwisespec;
    MemImpedanceSpecTypeDDR4 memimpedancespec;
    PrePostambleTypeDDR4 prepostamble;
};
NLOHMANN_JSONIFY_ALL_THINGS(MemSpecDDR4,
                            memoryId,
                            memarchitecturespec,
                            mempowerspec,
                            memtimingspec,
                            bankwisespec,
                            memimpedancespec,
                            prepostamble)

} // namespace DRAMUtils::MemSpec

#endif /* DRAMUTILS_MEMSPEC_STANDARDS_MEMSPECDDR4_H */
