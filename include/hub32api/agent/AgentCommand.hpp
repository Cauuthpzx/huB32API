#pragma once

#include <string>
#include <map>
#include <chrono>
#include "hub32api/core/Types.hpp"
#include "hub32api/export.h"

namespace hub32api {

/**
 * @brief Status of a command sent to an agent.
 */
enum class CommandStatus
{
    Pending,    ///< Command queued, waiting for agent to pick up
    Running,    ///< Agent is currently executing the command
    Success,    ///< Command completed successfully
    Failed,     ///< Command execution failed
    Timeout     ///< Command timed out before completion
};

/**
 * @brief Converts a CommandStatus enum value to its string representation.
 * @param s The command status to convert.
 * @return String representation ("pending", "running", "success", "failed", "timeout").
 */
HUB32API_EXPORT std::string to_string(CommandStatus s);

/**
 * @brief Parses a string to a CommandStatus enum value.
 * @param s The string to parse.
 * @return The corresponding CommandStatus (defaults to Pending if unknown).
 */
HUB32API_EXPORT CommandStatus command_status_from_string(const std::string& s);

/**
 * @brief Represents a command sent from the server to an agent.
 *
 * Commands are queued in the AgentRegistry and polled by agents via
 * GET /api/v1/agents/{id}/commands. After execution, the agent reports
 * the result back via PUT /api/v1/agents/{id}/commands/{cid}.
 */
struct HUB32API_EXPORT AgentCommand
{
    Uid         commandId;       ///< Unique command identifier (UUID)
    Uid         agentId;         ///< Target agent identifier
    std::string featureUid;      ///< Feature to invoke (e.g., "lock-screen", "screen-capture")
    std::string operation;       ///< Operation to perform (e.g., "start", "stop")
    std::map<std::string, std::string> arguments; ///< Additional arguments for the command
    CommandStatus status = CommandStatus::Pending; ///< Current command status
    Timestamp   createdAt;       ///< Timestamp when command was created
    Timestamp   completedAt;     ///< Timestamp when command completed (zero if not yet)
    std::string result;          ///< Result data (JSON string) or error message
    int         durationMs = 0;  ///< Execution duration in milliseconds
};

} // namespace hub32api
