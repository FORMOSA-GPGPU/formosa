#ifndef DRAMUTILS_CONFIG_TOGGLING_RATE_H
#define DRAMUTILS_CONFIG_TOGGLING_RATE_H

#include <DRAMUtils/util/json_utils.h>

namespace DRAMUtils::Config
{

enum class TogglingRateIdlePattern
{
    L = 0,
    H = 1,
    Z = 2,
    Invalid = -1
};
NLOHMANN_JSON_SERIALIZE_ENUM(TogglingRateIdlePattern,
                             {{TogglingRateIdlePattern::Invalid, nullptr},
                              {TogglingRateIdlePattern::L, "L"},
                              {TogglingRateIdlePattern::H, "H"},
                              {TogglingRateIdlePattern::Z, "Z"}})

struct ToggleRateDefinition
{
    double togglingRateRead;
    double togglingRateWrite;
    double dutyCycleRead;
    double dutyCycleWrite;
    TogglingRateIdlePattern idlePatternRead;
    TogglingRateIdlePattern idlePatternWrite;
};
NLOHMANN_JSONIFY_ALL_THINGS(ToggleRateDefinition,
                            togglingRateRead,
                            togglingRateWrite,
                            dutyCycleRead,
                            dutyCycleWrite,
                            idlePatternRead,
                            idlePatternWrite)

} // namespace DRAMUtils::Config

#endif /* DRAMUTILS_CONFIG_TOGGLING_RATE_H */
