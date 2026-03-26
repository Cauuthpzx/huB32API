#include "AgentRegistry.hpp"
#include <spdlog/spdlog.h>

namespace hub32api::agent {

static constexpr size_t k_maxCommandHistory = 10000;
static constexpr size_t k_commandHistoryPruneCount = 1000;

/**
 * @brief Registers a new agent or updates an existing one.
 *
 * Validates that agentId is non-empty, sets registeredAt and lastHeartbeat
 * to the current time, forces state to Online, and upserts into the registry.
 */
Result<Uid> AgentRegistry::registerAgent(const AgentInfo& info)
{
    if (info.agentId.empty()) {
        return Result<Uid>::fail(
            ApiError{ErrorCode::InvalidRequest, "agentId must not be empty"});
    }

    std::lock_guard<std::mutex> lock(m_mutex);

    auto now = std::chrono::system_clock::now();

    if (m_agents.find(info.agentId) != m_agents.end()) {
        spdlog::warn("[AgentRegistry] overwriting existing agent: {}", info.agentId);
    }

    AgentInfo entry = info;
    entry.registeredAt  = now;
    entry.lastHeartbeat = now;
    entry.state         = AgentState::Online;

    m_agents[entry.agentId] = entry;

    return Result<Uid>::ok(entry.agentId);
}

/**
 * @brief Removes an agent and its command queue from the registry.
 */
void AgentRegistry::unregisterAgent(const Uid& agentId)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    m_agents.erase(agentId);
    m_commandQueues.erase(agentId);
}

/**
 * @brief Finds an agent by ID.
 * @return The AgentInfo on success, or NotFound error if the agent is not registered.
 */
Result<AgentInfo> AgentRegistry::findAgent(const Uid& agentId) const
{
    std::lock_guard<std::mutex> lock(m_mutex);

    auto it = m_agents.find(agentId);
    if (it == m_agents.end()) {
        return Result<AgentInfo>::fail(
            ApiError{ErrorCode::NotFound, "Agent not found: " + agentId});
    }

    return Result<AgentInfo>::ok(it->second);
}

/**
 * @brief Lists all registered agents.
 */
std::vector<AgentInfo> AgentRegistry::listAgents() const
{
    std::lock_guard<std::mutex> lock(m_mutex);

    std::vector<AgentInfo> result;
    result.reserve(m_agents.size());

    for (const auto& [id, agent] : m_agents) {
        result.push_back(agent);
    }

    return result;
}

/**
 * @brief Lists only agents whose state is Online or Busy.
 */
std::vector<AgentInfo> AgentRegistry::listOnlineAgents() const
{
    std::lock_guard<std::mutex> lock(m_mutex);

    std::vector<AgentInfo> result;

    for (const auto& [id, agent] : m_agents) {
        if (agent.state == AgentState::Online || agent.state == AgentState::Busy) {
            result.push_back(agent);
        }
    }

    return result;
}

/**
 * @brief Updates the lastHeartbeat timestamp and transitions the agent to Online
 *        if it was previously Offline or in Error state.
 */
void AgentRegistry::heartbeat(const Uid& agentId)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    auto it = m_agents.find(agentId);
    if (it == m_agents.end()) {
        return;
    }

    it->second.lastHeartbeat = std::chrono::system_clock::now();

    if (it->second.state == AgentState::Offline ||
        it->second.state == AgentState::Error) {
        it->second.state = AgentState::Online;
    }
}

/**
 * @brief Updates the state of a registered agent.
 */
void AgentRegistry::updateState(const Uid& agentId, AgentState state)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    auto it = m_agents.find(agentId);
    if (it == m_agents.end()) {
        return;
    }

    it->second.state = state;
}

/**
 * @brief Queues a command for a specific agent.
 *
 * Sets createdAt to the current time if it has not been set,
 * appends the command to the agent's queue, and stores it in the
 * command history for later lookup by commandId.
 */
void AgentRegistry::queueCommand(const AgentCommand& cmd)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    // Validate that the target agent exists
    if (m_agents.find(cmd.agentId) == m_agents.end()) {
        spdlog::warn("[AgentRegistry] queueCommand: agent not found: {}", cmd.agentId);
        return;
    }

    AgentCommand entry = cmd;

    // Set createdAt if it hasn't been set (epoch / zero timepoint)
    if (entry.createdAt == Timestamp{}) {
        entry.createdAt = std::chrono::system_clock::now();
    }

    m_commandQueues[entry.agentId].push_back(entry);
    m_commandHistory[entry.commandId] = entry;

    // Prune command history if it exceeds the maximum size
    if (m_commandHistory.size() > k_maxCommandHistory) {
        spdlog::info("[AgentRegistry] command history size {} exceeds limit {}, pruning {} oldest entries",
                     m_commandHistory.size(), k_maxCommandHistory, k_commandHistoryPruneCount);
        auto it = m_commandHistory.begin();
        for (size_t i = 0; i < k_commandHistoryPruneCount && it != m_commandHistory.end(); ++i) {
            it = m_commandHistory.erase(it);
        }
    }
}

/**
 * @brief Atomically dequeues all pending commands for a given agent.
 *
 * Swaps the agent's command deque into a local temporary, leaving
 * the queue empty for future commands.
 *
 * @return Vector of pending commands (empty if the agent has no queued commands).
 */
std::vector<AgentCommand> AgentRegistry::dequeuePendingCommands(const Uid& agentId)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    auto it = m_commandQueues.find(agentId);
    if (it == m_commandQueues.end()) {
        return {};
    }

    std::deque<AgentCommand> temp;
    temp.swap(it->second);

    return {std::make_move_iterator(temp.begin()),
            std::make_move_iterator(temp.end())};
}

/**
 * @brief Reports the result of a previously queued command.
 *
 * Looks up the command in the history by commandId and updates its
 * status, result string, duration, and completedAt timestamp.
 */
void AgentRegistry::reportCommandResult(const Uid& commandId,
                                        CommandStatus status,
                                        const std::string& result,
                                        int durationMs)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    auto it = m_commandHistory.find(commandId);
    if (it == m_commandHistory.end()) {
        return;
    }

    it->second.status      = status;
    it->second.result      = result;
    it->second.durationMs  = durationMs;
    it->second.completedAt = std::chrono::system_clock::now();
}

/**
 * @brief Finds a command by its ID in the command history.
 * @return The AgentCommand on success, or NotFound error if the command ID is unknown.
 */
Result<AgentCommand> AgentRegistry::findCommand(const Uid& commandId) const
{
    std::lock_guard<std::mutex> lock(m_mutex);

    auto it = m_commandHistory.find(commandId);
    if (it == m_commandHistory.end()) {
        return Result<AgentCommand>::fail(
            ApiError{ErrorCode::NotFound, "Command not found: " + commandId});
    }

    return Result<AgentCommand>::ok(it->second);
}

/**
 * @brief Returns the total number of registered agents.
 */
size_t AgentRegistry::agentCount() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_agents.size();
}

} // namespace hub32api::agent
