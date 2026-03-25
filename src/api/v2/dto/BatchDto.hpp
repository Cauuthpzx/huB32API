#pragma once

#include <string>
#include <vector>
#include <map>
#include <nlohmann/json.hpp>
#include "hub32api/core/Types.hpp"

namespace hub32api::api::v2::dto {

// -----------------------------------------------------------------------
// Batch operation request — v2 exclusive
// POST /api/v2/batch/features
// -----------------------------------------------------------------------
struct BatchFeatureRequest
{
    std::vector<std::string>         computerIds;
    std::string                      featureUid;
    std::string                      operation;  // "start" | "stop"
    std::map<std::string,std::string> arguments;
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(BatchFeatureRequest,
    computerIds, featureUid, operation, arguments)

struct BatchResultItem
{
    std::string computerId;
    bool        success = false;
    std::string error;
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(BatchResultItem, computerId, success, error)

struct BatchFeatureResponse
{
    int                        total     = 0;
    int                        succeeded = 0;
    int                        failed    = 0;
    std::vector<BatchResultItem> results;
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(BatchFeatureResponse, total, succeeded, failed, results)

} // namespace hub32api::api::v2::dto
