/**
 * @file AgentConfig.cpp
 * @brief Implementation of AgentConfig loading from JSON file, Windows Registry,
 *        and default construction.
 *
 * Supports three configuration sources:
 *  - JSON file via from_file()
 *  - Windows Registry via from_registry()
 *  - Hardcoded defaults via defaults()
 */

#include "hub32agent/AgentConfig.hpp"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <fstream>

#ifdef _WIN32
#  include <windows.h>
#endif

namespace hub32agent {

namespace {

/**
 * @brief Helper to read a REG_DWORD value from an open registry key.
 *
 * @param hKey      Open registry key handle.
 * @param valueName Name of the registry value to read.
 * @param out       Output integer to populate on success.
 * @return true if the value was read successfully, false otherwise.
 */
bool readRegistryDword(HKEY hKey, const char* valueName, int& out)
{
    DWORD data = 0;
    DWORD dataSize = sizeof(data);
    DWORD type = 0;
    LONG result = RegQueryValueExA(hKey, valueName, nullptr, &type,
                                   reinterpret_cast<LPBYTE>(&data), &dataSize);
    if (result == ERROR_SUCCESS && type == REG_DWORD)
    {
        out = static_cast<int>(data);
        return true;
    }
    return false;
}

/**
 * @brief Helper to read a REG_SZ (string) value from an open registry key.
 *
 * @param hKey      Open registry key handle.
 * @param valueName Name of the registry value to read.
 * @param out       Output string to populate on success.
 * @return true if the value was read successfully, false otherwise.
 */
bool readRegistryString(HKEY hKey, const char* valueName, std::string& out)
{
    char buffer[1024] = {};
    DWORD bufferSize = sizeof(buffer);
    DWORD type = 0;
    LONG result = RegQueryValueExA(hKey, valueName, nullptr, &type,
                                   reinterpret_cast<LPBYTE>(buffer), &bufferSize);
    if (result == ERROR_SUCCESS && type == REG_SZ)
    {
        out = std::string(buffer);
        return true;
    }
    return false;
}

} // anonymous namespace

/**
 * @brief Returns an AgentConfig with all default values.
 *
 * @return A default-constructed AgentConfig.
 */
AgentConfig AgentConfig::defaults()
{
    spdlog::info("[AgentConfig] using default configuration");
    return AgentConfig{};
}

/**
 * @brief Loads configuration from a JSON file.
 *
 * Opens the file at the given path, reads its full content, and parses it
 * as JSON using nlohmann::json. Each recognized key is mapped to the
 * corresponding AgentConfig field using json::value() with a default fallback.
 *
 * On parse error or file-open failure, logs the error and returns defaults().
 *
 * @param path Filesystem path to the JSON configuration file.
 * @return A populated AgentConfig, or defaults() on error.
 */
AgentConfig AgentConfig::from_file(const std::string& path)
{
    spdlog::info("[AgentConfig] loading from file: {}", path);

    std::ifstream ifs(path);
    if (!ifs.is_open())
    {
        spdlog::error("[AgentConfig] failed to open config file: {}", path);
        return defaults();
    }

    std::string content((std::istreambuf_iterator<char>(ifs)),
                         std::istreambuf_iterator<char>());

    nlohmann::json j;
    try
    {
        j = nlohmann::json::parse(content);
    }
    catch (const nlohmann::json::parse_error& e)
    {
        spdlog::error("[AgentConfig] JSON parse error in {}: {}", path, e.what());
        return defaults();
    }

    AgentConfig cfg{};

    cfg.serverUrl         = j.value("serverUrl",         cfg.serverUrl);
    cfg.agentKey          = j.value("agentKey",          cfg.agentKey);
    cfg.pollIntervalMs    = j.value("pollIntervalMs",    cfg.pollIntervalMs);
    cfg.heartbeatIntervalMs = j.value("heartbeatIntervalMs", cfg.heartbeatIntervalMs);
    cfg.logLevel          = j.value("logLevel",          cfg.logLevel);
    cfg.logFile           = j.value("logFile",           cfg.logFile);
    cfg.caCertPath        = j.value("caCertPath",        cfg.caCertPath);
    cfg.locationId        = j.value("locationId",        cfg.locationId);
    cfg.streamWidth       = j.value("streamWidth",       cfg.streamWidth);
    cfg.streamHeight      = j.value("streamHeight",      cfg.streamHeight);
    cfg.streamFps         = j.value("streamFps",         cfg.streamFps);

    spdlog::info("[AgentConfig] loaded config from file: {} (serverUrl={}, poll={}ms)",
                 path, cfg.serverUrl, cfg.pollIntervalMs);
    return cfg;
}

/**
 * @brief Loads configuration from the Windows Registry.
 *
 * Reads values from @c HKEY_LOCAL_MACHINE\\SOFTWARE\\hub32agent. Integer fields
 * are read as REG_DWORD, string fields as REG_SZ.
 *
 * If the registry key does not exist, logs an informational message and
 * returns defaults().
 *
 * @return A populated AgentConfig from registry values, or defaults() on failure.
 */
AgentConfig AgentConfig::from_registry()
{
    spdlog::info("[AgentConfig] loading from Windows Registry");

    HKEY hKey = nullptr;
    LONG openResult = RegOpenKeyExA(HKEY_LOCAL_MACHINE,
                                    "SOFTWARE\\hub32agent",
                                    0,
                                    KEY_READ,
                                    &hKey);

    if (openResult != ERROR_SUCCESS)
    {
        spdlog::info("[AgentConfig] registry key HKLM\\SOFTWARE\\hub32agent not found "
                     "(error={}); using defaults", openResult);
        return defaults();
    }

    AgentConfig cfg{};

    // String fields (REG_SZ)
    readRegistryString(hKey, "serverUrl", cfg.serverUrl);
    readRegistryString(hKey, "agentKey",  cfg.agentKey);
    readRegistryString(hKey, "logLevel",  cfg.logLevel);
    readRegistryString(hKey, "logFile",   cfg.logFile);
    readRegistryString(hKey, "caCertPath",  cfg.caCertPath);
    readRegistryString(hKey, "locationId", cfg.locationId);

    // Integer fields (REG_DWORD)
    readRegistryDword(hKey, "pollIntervalMs",      cfg.pollIntervalMs);
    readRegistryDword(hKey, "heartbeatIntervalMs", cfg.heartbeatIntervalMs);
    readRegistryDword(hKey, "streamWidth",         cfg.streamWidth);
    readRegistryDword(hKey, "streamHeight",        cfg.streamHeight);
    readRegistryDword(hKey, "streamFps",           cfg.streamFps);

    RegCloseKey(hKey);

    spdlog::info("[AgentConfig] loaded config from registry (serverUrl={}, poll={}ms)",
                 cfg.serverUrl, cfg.pollIntervalMs);
    return cfg;
}

} // namespace hub32agent
