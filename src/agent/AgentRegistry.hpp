#pragma once

#include <mutex>
#include <unordered_map>
#include <vector>
#include <deque>
#include "hub32api/agent/AgentInfo.hpp"
#include "hub32api/agent/AgentCommand.hpp"
#include "hub32api/core/Result.hpp"
#include "hub32api/export.h"

namespace hub32api::agent {

/**
 * @brief Thread-safe registry of connected agents and their command queues.
 *
 * The server-side AgentRegistry tracks all registered agents and maintains
 * a per-agent command queue. Teachers push commands via REST API, agents
 * poll and dequeue commands, then report results back.
 *
 * All public methods are thread-safe (guarded by internal mutex).
 */
class HUB32API_EXPORT AgentRegistry
{
public:
    /**
     * @brief Registers a new agent or updates an existing one.
     * @param info Agent information. agentId must be non-empty.
     * @return The agent ID on success, or InvalidRequest error if agentId is empty.
     */
    Result<Uid> registerAgent(const AgentInfo& info);

    /**
     * @brief Removes an agent and its command queue.
     * @param agentId The agent to unregister.
     */
    void unregisterAgent(const Uid& agentId);

    /**
     * @brief Finds an agent by ID.
     * @param agentId The agent ID to look up.
     * @return The AgentInfo on success, or NotFound error.
     */
    Result<AgentInfo> findAgent(const Uid& agentId) const;

    /**
     * @brief Lists all registered agents.
     * @return Vector of all AgentInfo entries.
     */
    std::vector<AgentInfo> listAgents() const;

    /**
     * @brief Lists only agents with state Online or Busy.
     * @return Vector of online/busy AgentInfo entries.
     */
    std::vector<AgentInfo> listOnlineAgents() const;

    /**
     * @brief Updates the lastHeartbeat timestamp and sets state to Online.
     * @param agentId The agent to heartbeat.
     */
    void heartbeat(const Uid& agentId);

    /**
     * @brief Updates the state of an agent.
     * @param agentId The agent to update.
     * @param state The new state.
     */
    void updateState(const Uid& agentId, AgentState state);

    /**
     * @brief Queues a command for an agent.
     * @param cmd The command to queue. cmd.agentId determines the target agent.
     */
    void queueCommand(const AgentCommand& cmd);

    /**
     * @brief Atomically dequeues all pending commands for an agent.
     * @param agentId The agent whose commands to dequeue.
     * @return Vector of pending commands (empty if none).
     */
    std::vector<AgentCommand> dequeuePendingCommands(const Uid& agentId);

    /**
     * @brief Reports the result of a command execution.
     * @param commandId The command that was executed.
     * @param status The final status (Success, Failed, Timeout).
     * @param result Result data (JSON string or error message).
     * @param durationMs Execution duration in milliseconds.
     */
    void reportCommandResult(const Uid& commandId,
                             CommandStatus status,
                             const std::string& result,
                             int durationMs);

    /**
     * @brief Finds a command by its ID in the history.
     * @param commandId The command ID to look up.
     * @return The AgentCommand on success, or NotFound error.
     */
    Result<AgentCommand> findCommand(const Uid& commandId) const;

    /**
     * @brief Returns the total number of registered agents.
     * @return Agent count.
     */
    size_t agentCount() const;

private:
    mutable std::mutex m_mutex;
    std::unordered_map<Uid, AgentInfo> m_agents;
    std::unordered_map<Uid, std::deque<AgentCommand>> m_commandQueues;
    std::unordered_map<Uid, AgentCommand> m_commandHistory; ///< commandId -> cmd
};

} // namespace hub32api::agent
