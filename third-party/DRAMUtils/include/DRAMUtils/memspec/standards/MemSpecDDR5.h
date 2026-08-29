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

#ifndef DRAMUTILS_MEMSPEC_STANDARDS_MEMSPECDDR5_H
#define DRAMUTILS_MEMSPEC_STANDARDS_MEMSPECDDR5_H

#include "DRAMUtils/util/json_utils.h"
#include <optional>
#include <string>
#include <string_view>

namespace DRAMUtils::MemSpec
{

enum class RefModeTypeDDR5
{
    INVALID = -1,
    REF_MODE_1 = 1,
    REF_MODE_2 = 2,
};
NLOHMANN_JSON_SERIALIZE_ENUM(RefModeTypeDDR5,
                             {
                                 {RefModeTypeDDR5::INVALID, nullptr},
                                 {RefModeTypeDDR5::REF_MODE_1, 1},
                                 {RefModeTypeDDR5::REF_MODE_2, 2},
                             })

struct MemArchitectureSpecTypeDDR5
{
    uint64_t nbrOfChannels;
    uint64_t nbrOfDevices;
    uint64_t nbrOfRanks;
    uint64_t nbrOfDIMMRanks;
    uint64_t nbrOfPhysicalRanks;
    uint64_t nbrOfLogicalRanks;
    uint64_t nbrOfBanks;
    uint64_t nbrOfBankGroups;
    uint64_t nbrOfRows;
    uint64_t nbrOfColumns;
    uint64_t burstLength;
    uint64_t dataRate;
    uint64_t width;
    RefModeTypeDDR5 RefMode;
    std::optional<uint64_t> maxBurstLength;

    uint64_t cmdMode;
    uint64_t RAAIMT;
    uint64_t RAAMMT;
    uint64_t RAADEC;
};
NLOHMANN_JSONIFY_ALL_THINGS(MemArchitectureSpecTypeDDR5,
                            nbrOfChannels,
                            nbrOfDevices,
                            nbrOfRanks,
                            nbrOfDIMMRanks,
                            nbrOfPhysicalRanks,
                            nbrOfLogicalRanks,
                            nbrOfBanks,
                            nbrOfBankGroups,
                            nbrOfRows,
                            nbrOfColumns,
                            burstLength,
                            dataRate,
                            width,
                            RefMode,
                            maxBurstLength,
                            cmdMode,
                            RAAIMT,
                            RAAMMT,
                            RAADEC)

struct MemTimingSpecTypeDDR5
{
    double tCK;
    uint64_t RAS;
    uint64_t RCD;
    uint64_t RTP;
    uint64_t WL;
    uint64_t WR;
    uint64_t RP;

    uint64_t PPD;
    uint64_t RL;
    uint64_t RPRE;
    uint64_t RPST;
    uint64_t RDDQS;
    uint64_t WPRE;
    uint64_t WPST;

    uint64_t CCD_L_slr;
    uint64_t CCD_L_WR_slr;
    uint64_t CCD_L_WR2_slr;
    uint64_t CCD_M_slr;
    uint64_t CCD_M_WR_slr;
    uint64_t CCD_S_slr;
    uint64_t CCD_S_WR_slr;
    uint64_t CCD_dlr;
    uint64_t CCD_WR_dlr;
    uint64_t CCD_WR_dpr;
    uint64_t RRD_L_slr;
    uint64_t RRD_S_slr;
    uint64_t RRD_dlr;
    uint64_t FAW_slr;
    uint64_t FAW_dlr;
    uint64_t WTR_L;
    uint64_t WTR_M;
    uint64_t WTR_S;
    uint64_t RFC1_slr; // RefModeTypeDDR5::REF_MODE_1
    uint64_t RFC2_slr; // RefModeTypeDDR5::REF_MODE_2
    uint64_t RFC1_dlr; // RefModeTypeDDR5::REF_MODE_1
    uint64_t RFC2_dlr; // RefModeTypeDDR5::REF_MODE_2
    uint64_t RFC1_dpr; // RefModeTypeDDR5::REF_MODE_1
    uint64_t RFC2_dpr; // RefModeTypeDDR5::REF_MODE_2
    uint64_t RFCsb_slr;
    uint64_t RFCsb_dlr;
    uint64_t REFI1;
    uint64_t REFI2;
    uint64_t REFISB;
    uint64_t REFSBRD_slr;
    uint64_t REFSBRD_dlr;
    uint64_t RTRS;
    uint64_t CPDED;
    uint64_t PD;
    uint64_t XP;
    uint64_t ACTPDEN;
    uint64_t PRPDEN;
    uint64_t REFPDEN;
};
NLOHMANN_JSONIFY_ALL_THINGS(MemTimingSpecTypeDDR5,
                            tCK,
                            RAS,
                            RCD,
                            RTP,
                            WL,
                            WR,
                            RP,
                            PPD,
                            RL,
                            RPRE,
                            RPST,
                            RDDQS,
                            WPRE,
                            WPST,
                            CCD_L_slr,
                            CCD_L_WR_slr,
                            CCD_L_WR2_slr,
                            CCD_M_slr,
                            CCD_M_WR_slr,
                            CCD_S_slr,
                            CCD_S_WR_slr,
                            CCD_dlr,
                            CCD_WR_dlr,
                            CCD_WR_dpr,
                            RRD_L_slr,
                            RRD_S_slr,
                            RRD_dlr,
                            FAW_slr,
                            FAW_dlr,
                            WTR_L,
                            WTR_M,
                            WTR_S,
                            RFC1_slr,
                            RFC2_slr,
                            RFC1_dlr,
                            RFC2_dlr,
                            RFC1_dpr,
                            RFC2_dpr,
                            RFCsb_slr,
                            RFCsb_dlr,
                            REFI1,
                            REFI2,
                            REFISB,
                            REFSBRD_slr,
                            REFSBRD_dlr,
                            RTRS,
                            CPDED,
                            PD,
                            XP,
                            ACTPDEN,
                            PRPDEN,
                            REFPDEN)

struct MemPowerSpecTypeDDR5
{
    double vdd;
    double idd0;
    double idd2n;
    double idd3n;
    double idd4r;
    double idd4w;
    double idd5c;
    double idd6n;
    double idd2p;
    double idd3p;

    double vpp;
    double ipp0;
    double ipp2n;
    double ipp3n;
    double ipp4r;
    double ipp4w;
    double ipp5c;
    double ipp6n;
    double ipp2p;
    double ipp3p;

    double idd5b; // RefModeTypeDDR5::REF_MODE_1
    double idd5f; // RefModeTypeDDR5::REF_MODE_2

    double ipp5b; // RefModeTypeDDR5::REF_MODE_1
    double ipp5f; // RefModeTypeDDR5::REF_MODE_2

    double vddq;

    std::optional<double> iBeta_vdd;
    std::optional<double> iBeta_vpp;
};
NLOHMANN_JSONIFY_ALL_THINGS(MemPowerSpecTypeDDR5,
                            vdd,
                            idd0,
                            idd2n,
                            idd3n,
                            idd4r,
                            idd4w,
                            idd5c,
                            idd6n,
                            idd2p,
                            idd3p,
                            vpp,
                            ipp0,
                            ipp2n,
                            ipp3n,
                            ipp4r,
                            ipp4w,
                            ipp5c,
                            ipp6n,
                            ipp2p,
                            ipp3p,
                            idd5b,
                            idd5f,
                            ipp5b,
                            ipp5f,
                            vddq,
                            iBeta_vdd,
                            iBeta_vpp)

struct BankWiseSpecTypeDDR5
{
    std::optional<double> factRho;
};
NLOHMANN_JSONIFY_ALL_THINGS(BankWiseSpecTypeDDR5, factRho)

struct MemImpedanceSpecTypeDDR5
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
};
NLOHMANN_JSONIFY_ALL_THINGS(MemImpedanceSpecTypeDDR5,
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
                            wdqs_dyn_E)

struct DataRateSpecTypeDDR5
{
    uint64_t ca_bus_rate;
    uint64_t dq_bus_rate;
    uint64_t dqs_bus_rate;
};
NLOHMANN_JSONIFY_ALL_THINGS(DataRateSpecTypeDDR5, ca_bus_rate, dq_bus_rate, dqs_bus_rate)

struct MemSpecDDR5
{
    static constexpr inline const std::string_view id = "DDR5";
    std::string memoryId;

    MemArchitectureSpecTypeDDR5 memarchitecturespec;
    MemPowerSpecTypeDDR5 mempowerspec;
    MemTimingSpecTypeDDR5 memtimingspec;
    std::optional<BankWiseSpecTypeDDR5> bankwisespec;
    MemImpedanceSpecTypeDDR5 memimpedancespec;
    std::optional<DataRateSpecTypeDDR5> dataratespec;
};
NLOHMANN_JSONIFY_ALL_THINGS(MemSpecDDR5,
                            memoryId,
                            memarchitecturespec,
                            mempowerspec,
                            memtimingspec,
                            bankwisespec,
                            memimpedancespec,
                            dataratespec)

} // namespace DRAMUtils::MemSpec

#endif /* DRAMUTILS_MEMSPEC_STANDARDS_MEMSPECDDR5_H */
