#pragma once

#include <string>
#include <nlohmann/json.hpp>

namespace veyon32api::api::v2::dto {

// Prometheus-compatible text format is generated in MetricsController.
// This DTO is for the JSON variant of GET /api/v2/metrics.
struct MetricsDto
{
    int activeConnections   = 0;
    int totalRequests       = 0;
    int failedRequests      = 0;
    int pluginCount         = 0;
    int uptimeSeconds       = 0;
    std::string serverVersion;
    std::string veyonVersion;
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(MetricsDto,
    activeConnections, totalRequests, failedRequests,
    pluginCount, uptimeSeconds, serverVersion, veyonVersion)

} // namespace veyon32api::api::v2::dto
