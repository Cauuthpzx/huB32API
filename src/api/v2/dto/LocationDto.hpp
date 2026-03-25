#pragma once

#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace hub32api::api::v2::dto {

struct LocationDto
{
    std::string              id;
    std::string              name;
    int                      computerCount = 0;
    std::vector<std::string> computerIds;
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(LocationDto, id, name, computerCount, computerIds)

struct LocationListDto
{
    std::vector<LocationDto> locations;
    int total = 0;
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(LocationListDto, locations, total)

} // namespace hub32api::api::v2::dto
