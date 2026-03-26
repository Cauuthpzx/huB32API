/**
 * @file AgentConfig.hpp
 * @brief Configuration structure for the hub32 agent service.
 *
 * Supports loading configuration from a JSON file, from the Windows
 * Registry, or from compiled-in defaults.
 */

#pragma once

#include <string>

namespace hub32agent {

/**
 * @brief Configuration for the hub32 agent service.
 *
 * Loaded from a JSON file (--config) or Windows Registry.
 * Controls how the agent connects to the server and behaves.
 */
struct AgentConfig
{
    std::string serverUrl = "http://127.0.0.1:11081";  ///< Hub32API server URL
    std::string agentKey;                               ///< Pre-shared key for registration
    int pollIntervalMs = 5000;                          ///< Command poll interval (ms)
    int heartbeatIntervalMs = 30000;                    ///< Heartbeat interval (ms)
    std::string logLevel = "info";                      ///< Log level (trace/debug/info/warn/error)
    std::string logFile;                                ///< Log file path (empty = stdout)
    std::string caCertPath;                             ///< CA certificate path for TLS verification

    /**
     * @brief Loads configuration from a JSON file.
     * @param path Path to the JSON config file.
     * @return Populated AgentConfig.
     */
    static AgentConfig from_file(const std::string& path);

    /**
     * @brief Loads configuration from Windows Registry.
     *
     * Reads from @c HKLM\\SOFTWARE\\hub32agent. Falls back to
     * defaults() if the registry key does not exist.
     *
     * @return Populated AgentConfig.
     */
    static AgentConfig from_registry();

    /**
     * @brief Returns default configuration.
     * @return Default AgentConfig with compiled-in values.
     */
    static AgentConfig defaults();
};

} // namespace hub32agent
