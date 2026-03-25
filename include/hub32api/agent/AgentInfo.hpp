#pragma once

#include <string>
#include <vector>
#include <chrono>
#include "hub32api/core/Types.hpp"
#include "hub32api/export.h"

namespace hub32api {

/**
 * @brief Represents the operational state of an agent.
 */
enum class HUB32API_EXPORT AgentState
{
    Offline,    ///< Agent is not connected
    Online,     ///< Agent is connected and idle
    Busy,       ///< Agent is executing a command
    Error       ///< Agent encountered an error
};

/**
 * @brief Converts an AgentState enum value to its string representation.
 * @param s The agent state to convert.
 * @return String representation ("offline", "online", "busy", "error").
 */
HUB32API_EXPORT std::string to_string(AgentState s);

/**
 * @brief Parses a string to an AgentState enum value.
 * @param s The string to parse.
 * @return The corresponding AgentState (defaults to Offline if unknown).
 */
HUB32API_EXPORT AgentState agent_state_from_string(const std::string& s);

/**
 * @brief Information about a connected agent running on a student PC.
 *
 * This struct is stored in the AgentRegistry on the server side.
 * Agents register themselves via POST /api/v1/agents/register and
 * maintain their presence via periodic heartbeats.
 */
struct HUB32API_EXPORT AgentInfo
{
    Uid         agentId;                        ///< Unique agent identifier (UUID)
    std::string hostname;                       ///< Machine hostname (e.g., "PC-Lab-01")
    std::string ipAddress;                      ///< IP address of the agent
    uint16_t    agentPort  = 11082;             ///< Port the agent listens on
    std::string osVersion;                      ///< OS version string (e.g., "Windows 10 Pro 10.0.19045")
    std::string agentVersion;                   ///< Agent software version (e.g., "1.0.0")
    AgentState  state      = AgentState::Offline; ///< Current operational state
    Timestamp   registeredAt;                   ///< Timestamp of initial registration
    Timestamp   lastHeartbeat;                  ///< Timestamp of most recent heartbeat
    std::vector<std::string> capabilities;      ///< List of supported features (e.g., "screen-capture", "lock-screen")
};

} // namespace hub32api
