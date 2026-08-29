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

#ifndef DRAMUTILS_MEMSPEC_STANDARDS_MEMSPECLPDDR4_H
#define DRAMUTILS_MEMSPEC_STANDARDS_MEMSPECLPDDR4_H

#include "DRAMUtils/util/json_utils.h"
#include <optional>
#include <string>
#include <string_view>

namespace DRAMUtils::MemSpec
{

struct MemArchitectureSpecTypeLPDDR4
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
    std::optional<uint64_t> maxBurstLength;
};
NLOHMANN_JSONIFY_ALL_THINGS(MemArchitectureSpecTypeLPDDR4,
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
                            maxBurstLength)

struct MemImpedanceSpecTypeLPDDR4
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
NLOHMANN_JSONIFY_ALL_THINGS(MemImpedanceSpecTypeLPDDR4,
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

struct MemTimingSpecTypeLPDDR4
{
    double tCK;
    uint64_t CKE;
    uint64_t ESCKE;
    uint64_t CMDCKE;
    uint64_t RAS;
    uint64_t RCD;
    uint64_t RL;
    uint64_t REFI;
    uint64_t REFIpb;
    uint64_t RFCpb;
    uint64_t RFCab;
    uint64_t RPpb;
    uint64_t RPab;
    uint64_t RCpb;
    uint64_t RCab;
    uint64_t PPD;
    uint64_t FAW;
    uint64_t RRD;
    uint64_t CCD;
    uint64_t CCDMW;
    uint64_t RPST;
    uint64_t DQSCK;
    uint64_t RTP;
    uint64_t WL;
    uint64_t DQSS;
    uint64_t DQS2DQ;
    uint64_t WR;
    uint64_t WPRE;
    uint64_t WTR;
    uint64_t XP;
    uint64_t SR;
    uint64_t XSR;
    uint64_t RTRS;
};
NLOHMANN_JSONIFY_ALL_THINGS(MemTimingSpecTypeLPDDR4,
                            tCK,
                            CKE,
                            ESCKE,
                            CMDCKE,
                            RAS,
                            RCD,
                            RL,
                            REFI,
                            REFIpb,
                            RFCpb,
                            RFCab,
                            RPpb,
                            RPab,
                            RCpb,
                            RCab,
                            PPD,
                            FAW,
                            RRD,
                            CCD,
                            CCDMW,
                            RPST,
                            DQSCK,
                            RTP,
                            WL,
                            DQSS,
                            DQS2DQ,
                            WR,
                            WPRE,
                            WTR,
                            XP,
                            SR,
                            XSR,
                            RTRS)

enum class pasrModesType
{
    PASR_0,
    PASR_1,
    PASR_2,
    PASR_3,
    PASR_4,
    PASR_5,
    PASR_6,
    PASR_7,
    Invalid = -1,
};
NLOHMANN_JSON_SERIALIZE_ENUM(pasrModesType,
                             {
                                 {pasrModesType::Invalid, nullptr},
                                 {pasrModesType::PASR_0, 0},
                                 {pasrModesType::PASR_1, 1},
                                 {pasrModesType::PASR_2, 2},
                                 {pasrModesType::PASR_3, 3},
                                 {pasrModesType::PASR_4, 4},
                                 {pasrModesType::PASR_5, 5},
                                 {pasrModesType::PASR_6, 6},
                                 {pasrModesType::PASR_7, 7},
                             })

struct BankWiseSpecTypeLPDDR4
{
    std::optional<double> factRho;
    std::optional<double> factSigma;
    std::optional<pasrModesType> pasrMode;
    std::optional<bool> hasPASR;
};
NLOHMANN_JSONIFY_ALL_THINGS(BankWiseSpecTypeLPDDR4, factRho, factSigma, pasrMode, hasPASR)

struct MemPowerSpecTypeLPDDR4
{
    double vdd1;
    double idd01;
    double idd2n1;
    double idd3n1;
    double idd4r1;
    double idd4w1;
    double idd51;
    double idd5pb1;
    double idd61;
    double idd2p1;
    double idd3p1;

    double vdd2;
    double idd02;
    double idd2n2;
    double idd3n2;
    double idd4r2;
    double idd4w2;
    double idd52;
    double idd5pb2;
    double idd62;
    double idd2p2;
    double idd3p2;

    double vddq;

    std::optional<double> iBeta_vdd1;
    std::optional<double> iBeta_vdd2;
};
NLOHMANN_JSONIFY_ALL_THINGS(MemPowerSpecTypeLPDDR4,
                            vdd1,
                            idd01,
                            idd2n1,
                            idd3n1,
                            idd4r1,
                            idd4w1,
                            idd51,
                            idd5pb1,
                            idd61,
                            idd2p1,
                            idd3p1,
                            vdd2,
                            idd02,
                            idd2n2,
                            idd3n2,
                            idd4r2,
                            idd4w2,
                            idd52,
                            idd5pb2,
                            idd62,
                            idd2p2,
                            idd3p2,
                            vddq,
                            iBeta_vdd1,
                            iBeta_vdd2)

struct MemSpecLPDDR4
{
    static constexpr inline const std::string_view id = "LPDDR4";
    std::string memoryId;

    MemArchitectureSpecTypeLPDDR4 memarchitecturespec;
    MemPowerSpecTypeLPDDR4 mempowerspec;
    MemTimingSpecTypeLPDDR4 memtimingspec;
    MemImpedanceSpecTypeLPDDR4 memimpedancespec;
    std::optional<BankWiseSpecTypeLPDDR4> bankwisespec;
};
NLOHMANN_JSONIFY_ALL_THINGS(MemSpecLPDDR4,
                            memoryId,
                            memarchitecturespec,
                            mempowerspec,
                            memtimingspec,
                            memimpedancespec,
                            bankwisespec)

} // namespace DRAMUtils::MemSpec

#endif /* DRAMUTILS_MEMSPEC_STANDARDS_MEMSPECLPDDR4_H */
