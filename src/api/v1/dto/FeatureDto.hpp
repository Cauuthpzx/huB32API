#pragma once

#include <string>
#include <map>
#include <vector>
#include <nlohmann/json.hpp>
#include "veyon32api/plugins/FeaturePluginInterface.hpp"

namespace veyon32api::api::v1::dto {

struct FeatureDto
{
    std::string uid;
    std::string name;
    std::string description;
    bool        isActive       = false;
    bool        isMasterSide   = false;
    bool        isServiceSide  = false;

    static FeatureDto from(const FeatureDescriptor& fd);
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(FeatureDto,
    uid, name, description, isActive, isMasterSide, isServiceSide)

struct FeatureListDto
{
    std::vector<FeatureDto> features;
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(FeatureListDto, features)

struct FeatureControlRequest
{
    bool                        active = false;
    std::map<std::string,std::string> arguments;
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(FeatureControlRequest, active, arguments)

} // namespace veyon32api::api::v1::dto
