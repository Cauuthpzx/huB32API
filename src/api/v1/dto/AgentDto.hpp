#pragma once

/**
 * @file AgentDto.hpp
 * @brief Data Transfer Objects for agent management REST endpoints.
 *
 * Defines request/response DTOs for:
 *  - Agent registration (POST /api/v1/agents/register)
 *  - Agent status queries (GET /api/v1/agents, GET /api/v1/agents/{id}/status)
 *  - Command push/poll/result (POST/GET/PUT /api/v1/agents/{id}/commands)
 */

#include <string>
#include <vector>
#include <map>
#include <nlohmann/json.hpp>

namespace hub32api::api::v1::dto {

/**
 * @brief Request body for POST /api/v1/agents/register.
 */
struct AgentRegisterRequest {
    std::string hostname;      ///< Machine hostname
    std::string macAddress;    ///< MAC address of the primary NIC
    std::string agentKey;      ///< Pre-shared key for agent authentication
    std::string osVersion;     ///< OS version string
    std::string agentVersion;  ///< Agent software version
    std::vector<std::string> capabilities; ///< Supported features
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(AgentRegisterRequest,
    hostname, macAddress, agentKey, osVersion, agentVersion, capabilities)

/**
 * @brief Response body for POST /api/v1/agents/register.
 */
struct AgentRegisterResponse {
    std::string agentId;              ///< Assigned agent ID
    std::string computerId;           ///< Database computer ID (may differ from agentId)
    std::string locationId;           ///< Assigned location ID (empty if unassigned)
    std::string authToken;            ///< JWT for subsequent requests
    int commandPollIntervalMs = 5000; ///< Suggested poll interval in ms
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(AgentRegisterResponse,
    agentId, computerId, locationId, authToken, commandPollIntervalMs)

/**
 * @brief Agent status DTO for GET responses.
 */
struct AgentStatusDto {
    std::string agentId;       ///< Agent ID
    std::string hostname;      ///< Hostname
    std::string ipAddress;     ///< IP address
    std::string state;         ///< State as string
    std::string agentVersion;  ///< Version
    std::string lastHeartbeat; ///< ISO 8601 timestamp
    std::vector<std::string> capabilities; ///< Capabilities list
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(AgentStatusDto,
    agentId, hostname, ipAddress, state, agentVersion, lastHeartbeat, capabilities)

/**
 * @brief Request body for POST /api/v1/agents/{id}/commands.
 */
struct AgentCommandRequest {
    std::string featureUid;  ///< Feature to invoke
    std::string operation;   ///< Operation (start/stop)
    std::map<std::string, std::string> arguments; ///< Command arguments
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(AgentCommandRequest,
    featureUid, operation, arguments)

/**
 * @brief Response body for POST /api/v1/agents/{id}/commands.
 */
struct AgentCommandResponse {
    std::string commandId;  ///< Assigned command ID
    std::string status;     ///< Initial status
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(AgentCommandResponse, commandId, status)

/**
 * @brief Request body for PUT /api/v1/agents/{id}/commands/{cid}.
 */
struct AgentCommandResultRequest {
    std::string commandId;  ///< Command being reported on
    std::string status;     ///< "success" or "failed"
    std::string result;     ///< Result data (JSON string)
    int durationMs = 0;     ///< Execution duration
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(AgentCommandResultRequest,
    commandId, status, result, durationMs)

} // namespace hub32api::api::v1::dto
