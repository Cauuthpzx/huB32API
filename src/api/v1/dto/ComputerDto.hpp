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

/**
 * @brief Data Transfer Object representing a single computer resource.
 *
 * Flat JSON-serialisable view of a @ref ComputerInfo value object.
 */
struct ComputerDto
{
    std::string id;
    std::string name;
    std::string hostname;
    std::string location;
    std::string state;

    /**
     * @brief Constructs a ComputerDto from a domain ComputerInfo value.
     * @param info The plugin-layer computer descriptor.
     * @return A populated ComputerDto ready for JSON serialisation.
     */
    static ComputerDto from(const ComputerInfo& info)
    {
        const auto stateStr = [&]() -> std::string {
            switch (info.state) {
                case ComputerState::Online:  return "online";
                case ComputerState::Offline: return "offline";
                case ComputerState::Connected:    return "connected";
                case ComputerState::Connecting:   return "connecting";
                case ComputerState::Disconnecting: return "disconnecting";
                default:                     return "unknown";
            }
        }();
        return ComputerDto{info.uid, info.name, info.hostname, info.location, stateStr};
    }
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(ComputerDto, id, name, hostname, location, state)

struct ComputerListDto
{
    std::vector<ComputerDto> computers;
    int total = 0;
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(ComputerListDto, computers, total)

} // namespace veyon32api::api::v1::dto
