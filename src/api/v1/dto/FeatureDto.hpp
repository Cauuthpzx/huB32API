#pragma once

#include <string>
#include <map>
#include <vector>
#include <nlohmann/json.hpp>
#include "hub32api/plugins/FeaturePluginInterface.hpp"

namespace hub32api::api::v1::dto {

/**
 * @brief Data Transfer Object representing a single Hub32 feature.
 *
 * Carries metadata and runtime state for one feature exposed to API consumers.
 */
struct FeatureDto
{
    std::string uid;
    std::string name;
    std::string description;
    bool        isActive       = false;
    bool        isMasterSide   = false;
    bool        isServiceSide  = false;

    /**
     * @brief Constructs a FeatureDto from a domain FeatureDescriptor.
     * @param fd The descriptor returned by the feature plugin.
     * @return A populated FeatureDto ready for JSON serialisation.
     */
    static FeatureDto from(const FeatureDescriptor& fd)
    {
        return FeatureDto{fd.uid, fd.name, fd.description,
                          fd.isActive, fd.isMasterSide, fd.isServiceSide};
    }
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

} // namespace hub32api::api::v1::dto
