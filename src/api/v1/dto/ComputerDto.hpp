#pragma once

#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "veyon32api/core/Types.hpp"
#include "veyon32api/plugins/ComputerPluginInterface.hpp"

namespace veyon32api::api::v1::dto {

// -----------------------------------------------------------------------
// ComputerDto — JSON schema for computer resources in v1 API responses.
// All serialization lives here (no .cpp needed; header-only DTOs).
// -----------------------------------------------------------------------

struct ComputerDto
{
    std::string id;
    std::string name;
    std::string hostname;
    std::string location;
    std::string state;

    static ComputerDto from(const ComputerInfo& info);
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(ComputerDto, id, name, hostname, location, state)

struct ComputerListDto
{
    std::vector<ComputerDto> computers;
    int total = 0;
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(ComputerListDto, computers, total)

} // namespace veyon32api::api::v1::dto
