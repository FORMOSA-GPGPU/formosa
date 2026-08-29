#include <gtest/gtest.h>

#include <sstream>

#include "DRAMUtils/util/json.h"
#include "DRAMUtils/memspec/MemSpec.h"
#include "DRAMUtils/memspec/standards/MemSpecDDR3.h"

using namespace DRAMUtils;

const char* test_mem_spec = R"(
{
    "memarchitecturespec": {
        "burstLength": 0,
        "dataRate": 0,
        "nbrOfBanks": 0,
        "nbrOfChannels": 0,
        "nbrOfColumns": 0,
        "nbrOfDevices": 0,
        "nbrOfRows": 0,
        "nbrOfRanks": 0,
        "width": 0
    },
    "memoryId": "Test_DDR3",
    "memoryType": "DDR3",
    "memtimingspec": {
        "ACTPDEN": 0,
        "AL": 0,
        "CCD": 0,
        "CKE": 11,
        "CKESR": 0,
        "DQSCK": 0,
        "FAW": 0,
        "PRPDEN": 0,
        "RAS": 0,
        "RC": 0,
        "RCD": 0,
        "REFI": 0,
        "REFPDEN": 0,
        "RFC": 0,
        "RL": 0,
        "RP": 0,
        "RRD": 0,
        "RTP": 0,
        "RTRS": 0,
        "WL": 0,
        "WR": 0,
        "WTR": 0,
        "XP": 0,
        "XPDLL": 0,
        "XS": 0,
        "XSDLL": 0,
        "tCK": 0.0
    }
}
)";

class Memspec_Base_Test : public ::testing::Test 
{
protected:
    // Test data
    MemSpec::MemSpecContainer test_container;

    // Test util functions
    std::stringstream writeContainerToStream(const MemSpec::MemSpecContainer& container)
    {
        std::stringstream stream;
        json_t j = container;
        stream << j.dump(4);
        return stream;
    }

    MemSpec::MemSpecContainer readContainerFromStream(std::stringstream& stream)
    {
        json_t j;
        stream >> j;

        MemSpec::MemSpecContainer result = j;
        return result;
    }

    MemSpec::MemSpecVariant createDummyMemSpec()
    {
        MemSpec::MemSpecDDR3 memspec;
        memspec.memarchitecturespec = MemSpec::MemArchitectureSpecTypeDDR3{};
        memspec.memoryId = "Test_DDR3";
        memspec.memarchitecturespec.nbrOfRows = 10;
        memspec.memtimingspec = MemSpec::MemTimingSpecDDR3{};
        memspec.memtimingspec.CKE = 11;
        MemSpec::MemSpecVariant variant;
        variant.setVariant(memspec);
        return variant;
    }
    
    void compareMemSpec(MemSpec::MemSpecVariant& variant)
    {
        MemSpec::MemSpecDDR3 memspec = std::get<MemSpec::MemSpecDDR3>(variant.getVariant());
        ASSERT_EQ(memspec.memoryId, "Test_DDR3");
        ASSERT_EQ(memspec.memarchitecturespec.nbrOfRows, 10);
        ASSERT_EQ(memspec.memtimingspec.CKE, 11);
    }

    virtual void SetUp()
    {
        test_container.memspec = createDummyMemSpec();
    }

    virtual void TearDown()
    {
    }
};

TEST_F(Memspec_Base_Test, IDVariant)
{
    auto stream = writeContainerToStream(test_container);
    auto out_container = readContainerFromStream(stream);
    compareMemSpec(out_container.memspec);
};

TEST_F(Memspec_Base_Test, ParseMemSpec)
{
    // Test data
    json_t test_json = test_container;

    // Parse from buffer
    auto ok_spec = DRAMUtils::parse_Memspec_from_buffer(test_mem_spec);
    ASSERT_TRUE(ok_spec);

    // Parse from json
    auto ok_spec_1 = DRAMUtils::parse_memspec_from_json(test_json);
    auto ok_spec_2 = DRAMUtils::parse_memspec_from_json(test_json, "memspec");

    ASSERT_TRUE(ok_spec_1);
    compareMemSpec(*ok_spec_1);
    ASSERT_TRUE(ok_spec_2);
    compareMemSpec(*ok_spec_2);

    // Test with different key
    json_t test_diff_key_json;
    test_diff_key_json["other_key"] = test_json["memspec"];

    auto ok_spec_3 = DRAMUtils::parse_memspec_from_json(test_diff_key_json, "other_key");
    compareMemSpec(*ok_spec_3);

    // Parse memspec without container
    auto spec_json = test_json["memspec"];

    auto ok_spec_4 = DRAMUtils::parse_memspec_from_json(spec_json);
    compareMemSpec(*ok_spec_4);
};

TEST_F(Memspec_Base_Test, ParseMemSpec_Invalid)
{
    // Test data
    json_t test_json = test_container;

    // Parse from empty buffer
    ASSERT_FALSE(DRAMUtils::parse_Memspec_from_buffer(""));

    // Parse with wrong key
    auto fail_spec_1 = DRAMUtils::parse_memspec_from_json(test_json, "MemSpeck");
    ASSERT_FALSE(fail_spec_1);

    // Parse with empty key
    auto fail_spec_2 = DRAMUtils::parse_memspec_from_json(test_json, "");
    ASSERT_FALSE(fail_spec_2);

    // Test with invalid json
    json_t invalid = {"test", 0};
    ASSERT_FALSE(DRAMUtils::parse_memspec_from_json(invalid));

    // Test with invalid memspec
    json_t invalid_spec = test_container;
    invalid_spec["memspec"]["memarchitecturespec"].erase("nbrOfRows");
    ASSERT_FALSE(DRAMUtils::parse_memspec_from_json(invalid_spec));
}