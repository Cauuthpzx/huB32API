/**
 * @file AgentClient.hpp
 * @brief HTTP client for communicating with the hub32api server.
 *
 * Handles registration, command polling, result reporting, heartbeat,
 * and unregistration. Uses cpp-httplib for HTTP requests.
 */

#pragma once

#include <string>
#include <vector>
#include <memory>
#include <map>
#include "hub32agent/AgentConfig.hpp"

// Forward declare httplib
namespace httplib { class Client; }

namespace hub32agent {

/**
 * @brief Response from server registration.
 *
 * Contains the assigned agent ID, JWT auth token, and the
 * server-suggested command poll interval.
 */
struct RegisterResponse
{
    std::string agentId;              ///< Assigned agent ID (UUID)
    std::string authToken;            ///< JWT for subsequent requests
    int commandPollIntervalMs = 5000; ///< Suggested poll interval (ms)
};

/**
 * @brief Command received from server during polling.
 *
 * Represents a pending command that the agent should execute
 * via the CommandDispatcher.
 */
struct PendingCommand
{
    std::string commandId;    ///< Unique command identifier
    std::string agentId;      ///< Target agent identifier
    std::string featureUid;   ///< Feature to invoke (e.g., "screen-capture")
    std::string operation;    ///< Operation to perform ("start", "stop")
    std::map<std::string, std::string> arguments; ///< Additional arguments
};

/**
 * @brief HTTP client for communicating with the hub32api server.
 *
 * Manages the full agent-server communication lifecycle:
 *
 * Usage:
 * 1. Construct with AgentConfig
 * 2. Call registerWithServer() to obtain agentId + JWT
 * 3. Call pollCommands() periodically to fetch pending commands
 * 4. Call reportResult() after executing each command
 * 5. Call sendHeartbeat() periodically to maintain presence
 * 6. Call unregister() on shutdown
 */
class AgentClient
{
public:
    /**
     * @brief Constructs the AgentClient.
     *
     * Parses the server URL from the configuration to extract
     * host and port, then creates the underlying HTTP client
     * with appropriate timeouts.
     *
     * @param cfg Agent configuration with serverUrl, agentKey, etc.
     */
    explicit AgentClient(const AgentConfig& cfg);

    /**
     * @brief Destructor.
     */
    ~AgentClient();

    // Non-copyable
    AgentClient(const AgentClient&) = delete;
    /// @brief Deleted copy assignment operator.
    AgentClient& operator=(const AgentClient&) = delete;

    /**
     * @brief Move constructor.
     * @param other Source AgentClient to move from.
     */
    AgentClient(AgentClient&&) noexcept;

    /**
     * @brief Move assignment operator.
     * @param other Source AgentClient to move from.
     * @return Reference to this.
     */
    AgentClient& operator=(AgentClient&&) noexcept;

    /**
     * @brief Registers this agent with the server.
     *
     * Sends hostname, OS version, agent version, capabilities, and agentKey
     * to POST /api/v1/agents/register. On success, stores the agentId and
     * authToken internally for use in subsequent API calls.
     *
     * @return true on successful registration, false on any failure.
     */
    bool registerWithServer();

    /**
     * @brief Unregisters this agent from the server.
     *
     * Sends DELETE /api/v1/agents/{id} with the stored Bearer token.
     * Clears the internal agentId and authToken on completion.
     */
    void unregister();

    /**
     * @brief Polls the server for pending commands.
     *
     * Sends GET /api/v1/agents/{id}/commands with the stored Bearer token.
     * Parses the JSON array response into PendingCommand structs.
     *
     * @return Vector of pending commands (empty if none or on error).
     */
    std::vector<PendingCommand> pollCommands();

    /**
     * @brief Reports the result of a command execution back to the server.
     *
     * Sends PUT /api/v1/agents/{id}/commands/{commandId} with the result payload.
     *
     * @param commandId The ID of the command that was executed.
     * @param status    Result status: "success" or "failed".
     * @param result    Result data (JSON string or error message).
     * @param durationMs Execution duration in milliseconds.
     */
    void reportResult(const std::string& commandId,
                      const std::string& status,
                      const std::string& result,
                      int durationMs);

    /**
     * @brief Sends a heartbeat to the server.
     *
     * Sends POST /api/v1/agents/{id}/heartbeat with an empty JSON body
     * and the stored Bearer token, to maintain agent presence.
     */
    void sendHeartbeat();

    /**
     * @brief Returns whether the agent is currently registered.
     * @return true if registerWithServer() succeeded and unregister() has not been called.
     */
    bool isRegistered() const;

    /**
     * @brief Returns the assigned agent ID.
     * @return Agent ID string, or empty if not registered.
     */
    const std::string& agentId() const;

    /// @brief Returns the JWT auth token obtained during registration.
    const std::string& authToken() const { return m_authToken; }

private:
    AgentConfig m_cfg;                          ///< Agent configuration
    std::string m_agentId;                      ///< Assigned agent ID from server
    std::string m_authToken;                    ///< JWT auth token from server
    std::unique_ptr<httplib::Client> m_client;  ///< HTTP client instance

    /**
     * @brief Gets the operating system version string.
     *
     * Uses RtlGetVersion via dynamic loading of ntdll.dll to obtain
     * accurate Windows version information.
     *
     * @return OS version string (e.g., "Windows 10.0.19045").
     */
    static std::string getOsVersion();

    /**
     * @brief Gets the machine hostname.
     *
     * Uses the Win32 GetComputerNameA function.
     *
     * @return Hostname string.
     */
    static std::string getHostname();
};

} // namespace hub32agent
