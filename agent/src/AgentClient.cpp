/**
 * @file AgentClient.cpp
 * @brief Implementation of the HTTP client for agent-server communication.
 *
 * Uses cpp-httplib to communicate with the hub32api server.
 * Handles registration, command polling, result reporting, heartbeat,
 * and unregistration.
 */

#include "hub32agent/AgentClient.hpp"

#include <httplib.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#ifdef _WIN32
#  include <windows.h>
#endif

namespace hub32agent {

// ---------------------------------------------------------------------------
// Construction / destruction / move
// ---------------------------------------------------------------------------

/**
 * @brief Constructs the AgentClient from configuration.
 *
 * Parses the serverUrl to extract host and port, creates the
 * httplib::Client, and configures connection/read timeouts.
 *
 * @param cfg Agent configuration.
 */
AgentClient::AgentClient(const AgentConfig& cfg)
    : m_cfg(cfg)
{
    // Parse host and port from serverUrl.
    // Expected formats: "http://host:port" or "http://host"
    std::string url = m_cfg.serverUrl;

    // Strip trailing slash
    if (!url.empty() && url.back() == '/') {
        url.pop_back();
    }

    m_client = std::make_unique<httplib::Client>(url);
    m_client->set_connection_timeout(10, 0);  // 10 seconds
    m_client->set_read_timeout(30, 0);        // 30 seconds
    m_client->set_write_timeout(10, 0);       // 10 seconds

    spdlog::debug("[AgentClient] created client for server: {}", url);
}

/**
 * @brief Destructor.
 */
AgentClient::~AgentClient() = default;

/**
 * @brief Move constructor.
 * @param other Source AgentClient to move from.
 */
AgentClient::AgentClient(AgentClient&&) noexcept = default;

/**
 * @brief Move assignment operator.
 * @param other Source AgentClient to move from.
 * @return Reference to this.
 */
AgentClient& AgentClient::operator=(AgentClient&&) noexcept = default;

// ---------------------------------------------------------------------------
// Registration
// ---------------------------------------------------------------------------

/**
 * @brief Registers this agent with the server.
 *
 * Sends a POST request to /api/v1/agents/register with the agent's
 * hostname, OS version, agent version, pre-shared key, and capabilities.
 * On a 200 response, stores the agentId and authToken for subsequent calls.
 *
 * @return true on successful registration, false on any failure.
 */
bool AgentClient::registerWithServer()
{
    nlohmann::json body;
    body["hostname"]     = getHostname();
    body["agentKey"]     = m_cfg.agentKey;
    body["osVersion"]    = getOsVersion();
    body["agentVersion"] = "1.0.0";
    body["capabilities"] = nlohmann::json::array({
        "screen-capture", "lock-screen", "input-lock",
        "message-display", "power-control"
    });

    const std::string payload = body.dump();
    spdlog::info("[AgentClient] registering with server (hostname={})", body["hostname"].get<std::string>());

    auto res = m_client->Post("/api/v1/agents/register",
                              payload, "application/json");

    if (!res) {
        spdlog::error("[AgentClient] registration request failed: {}",
                      httplib::to_string(res.error()));
        return false;
    }

    if (res->status != 200) {
        spdlog::error("[AgentClient] registration failed: HTTP {} -- {}",
                      res->status, res->body);
        return false;
    }

    try {
        auto j = nlohmann::json::parse(res->body);
        m_agentId   = j.value("agentId", "");
        m_authToken = j.value("authToken", "");

        if (m_agentId.empty() || m_authToken.empty()) {
            spdlog::error("[AgentClient] registration response missing agentId or authToken");
            m_agentId.clear();
            m_authToken.clear();
            return false;
        }

        const int pollMs = j.value("commandPollIntervalMs", m_cfg.pollIntervalMs);
        spdlog::info("[AgentClient] registered successfully (agentId={}, pollInterval={}ms)",
                     m_agentId, pollMs);
        return true;

    } catch (const nlohmann::json::exception& e) {
        spdlog::error("[AgentClient] failed to parse registration response: {}", e.what());
        return false;
    }
}

// ---------------------------------------------------------------------------
// Unregistration
// ---------------------------------------------------------------------------

/**
 * @brief Unregisters this agent from the server.
 *
 * Sends DELETE /api/v1/agents/{id} with the stored Bearer token.
 * Clears the internal agentId and authToken regardless of outcome.
 */
void AgentClient::unregister()
{
    if (m_agentId.empty()) {
        spdlog::warn("[AgentClient] unregister called but not registered");
        return;
    }

    const std::string path = "/api/v1/agents/" + m_agentId;
    httplib::Headers headers = {
        {"Authorization", "Bearer " + m_authToken}
    };

    spdlog::info("[AgentClient] unregistering agent {}", m_agentId);

    auto res = m_client->Delete(path, headers);

    if (!res) {
        spdlog::warn("[AgentClient] unregister request failed: {}",
                     httplib::to_string(res.error()));
    } else if (res->status != 200 && res->status != 204) {
        spdlog::warn("[AgentClient] unregister returned HTTP {}: {}",
                     res->status, res->body);
    } else {
        spdlog::info("[AgentClient] unregistered successfully");
    }

    m_agentId.clear();
    m_authToken.clear();
}

// ---------------------------------------------------------------------------
// Command polling
// ---------------------------------------------------------------------------

/**
 * @brief Polls the server for pending commands.
 *
 * Sends GET /api/v1/agents/{id}/commands with the stored Bearer token.
 * Parses the JSON array response and converts each element into a
 * PendingCommand struct.
 *
 * @return Vector of pending commands (empty if none or on error).
 */
std::vector<PendingCommand> AgentClient::pollCommands()
{
    std::vector<PendingCommand> commands;

    if (m_agentId.empty()) {
        spdlog::warn("[AgentClient] pollCommands called but not registered");
        return commands;
    }

    const std::string path = "/api/v1/agents/" + m_agentId + "/commands";
    httplib::Headers headers = {
        {"Authorization", "Bearer " + m_authToken}
    };

    auto res = m_client->Get(path, headers);

    if (!res) {
        spdlog::warn("[AgentClient] poll request failed: {}",
                     httplib::to_string(res.error()));
        return commands;
    }

    if (res->status != 200) {
        spdlog::warn("[AgentClient] poll returned HTTP {}: {}",
                     res->status, res->body);
        return commands;
    }

    try {
        auto j = nlohmann::json::parse(res->body);

        if (!j.is_array()) {
            spdlog::warn("[AgentClient] poll response is not a JSON array");
            return commands;
        }

        for (const auto& item : j) {
            PendingCommand cmd;
            cmd.commandId  = item.value("commandId", "");
            cmd.agentId    = item.value("agentId", "");
            cmd.featureUid = item.value("featureUid", "");
            cmd.operation  = item.value("operation", "");

            if (item.contains("arguments") && item["arguments"].is_object()) {
                for (auto& [key, val] : item["arguments"].items()) {
                    cmd.arguments[key] = val.is_string()
                        ? val.get<std::string>()
                        : val.dump();
                }
            }

            commands.push_back(std::move(cmd));
        }

        if (!commands.empty()) {
            spdlog::debug("[AgentClient] polled {} command(s)", commands.size());
        }

    } catch (const nlohmann::json::exception& e) {
        spdlog::warn("[AgentClient] failed to parse poll response: {}", e.what());
    }

    return commands;
}

// ---------------------------------------------------------------------------
// Result reporting
// ---------------------------------------------------------------------------

/**
 * @brief Reports the result of a command execution back to the server.
 *
 * Sends PUT /api/v1/agents/{id}/commands/{commandId} with a JSON body
 * containing the command ID, status, result, and execution duration.
 *
 * @param commandId  The ID of the command that was executed.
 * @param status     Result status: "success" or "failed".
 * @param result     Result data (JSON string or error message).
 * @param durationMs Execution duration in milliseconds.
 */
void AgentClient::reportResult(const std::string& commandId,
                               const std::string& status,
                               const std::string& result,
                               int durationMs)
{
    if (m_agentId.empty()) {
        spdlog::warn("[AgentClient] reportResult called but not registered");
        return;
    }

    const std::string path = "/api/v1/agents/" + m_agentId + "/commands/" + commandId;
    httplib::Headers headers = {
        {"Authorization", "Bearer " + m_authToken}
    };

    nlohmann::json body;
    body["commandId"]  = commandId;
    body["status"]     = status;
    body["result"]     = result;
    body["durationMs"] = durationMs;

    const std::string payload = body.dump();

    spdlog::debug("[AgentClient] reporting result for command {} (status={}, duration={}ms)",
                  commandId, status, durationMs);

    auto res = m_client->Put(path, headers, payload, "application/json");

    if (!res) {
        spdlog::warn("[AgentClient] report request failed: {}",
                     httplib::to_string(res.error()));
    } else if (res->status != 200 && res->status != 204) {
        spdlog::warn("[AgentClient] report returned HTTP {}: {}",
                     res->status, res->body);
    }
}

// ---------------------------------------------------------------------------
// Heartbeat
// ---------------------------------------------------------------------------

/**
 * @brief Sends a heartbeat to the server.
 *
 * Sends POST /api/v1/agents/{id}/heartbeat with an empty JSON body
 * and the stored Bearer token. Logs warnings on failure but does
 * not throw.
 */
void AgentClient::sendHeartbeat()
{
    if (m_agentId.empty()) {
        spdlog::warn("[AgentClient] sendHeartbeat called but not registered");
        return;
    }

    const std::string path = "/api/v1/agents/" + m_agentId + "/heartbeat";
    httplib::Headers headers = {
        {"Authorization", "Bearer " + m_authToken}
    };

    auto res = m_client->Post(path, headers, "{}", "application/json");

    if (!res) {
        spdlog::warn("[AgentClient] heartbeat request failed: {}",
                     httplib::to_string(res.error()));
    } else if (res->status != 200) {
        spdlog::warn("[AgentClient] heartbeat returned HTTP {}: {}",
                     res->status, res->body);
    } else {
        spdlog::debug("[AgentClient] heartbeat sent");
    }
}

// ---------------------------------------------------------------------------
// State queries
// ---------------------------------------------------------------------------

/**
 * @brief Returns whether the agent is currently registered.
 * @return true if agentId and authToken are both non-empty.
 */
bool AgentClient::isRegistered() const
{
    return !m_agentId.empty() && !m_authToken.empty();
}

/**
 * @brief Returns the assigned agent ID.
 * @return Agent ID string, or empty if not registered.
 */
const std::string& AgentClient::agentId() const
{
    return m_agentId;
}

// ---------------------------------------------------------------------------
// System information helpers
// ---------------------------------------------------------------------------

/**
 * @brief Gets the operating system version string.
 *
 * Dynamically loads RtlGetVersion from ntdll.dll to get accurate
 * Windows version information, since GetVersionEx is deprecated and
 * may return incorrect values on Windows 8.1+.
 *
 * Falls back to "Windows (unknown)" if the call fails.
 *
 * @return OS version string (e.g., "Windows 10.0.19045").
 */
std::string AgentClient::getOsVersion()
{
#ifdef _WIN32
    // Use RtlGetVersion for accurate version info (GetVersionEx is deprecated
    // and lies on Windows 8.1+ without a manifest).
    using RtlGetVersionFn = LONG(WINAPI*)(PRTL_OSVERSIONINFOW);

    HMODULE ntdll = GetModuleHandleA("ntdll.dll");
    if (ntdll) {
        auto rtlGetVersion = reinterpret_cast<RtlGetVersionFn>(
            GetProcAddress(ntdll, "RtlGetVersion"));

        if (rtlGetVersion) {
            RTL_OSVERSIONINFOW osvi{};
            osvi.dwOSVersionInfoSize = sizeof(osvi);

            if (rtlGetVersion(&osvi) == 0) { // STATUS_SUCCESS
                return "Windows " +
                       std::to_string(osvi.dwMajorVersion) + "." +
                       std::to_string(osvi.dwMinorVersion) + "." +
                       std::to_string(osvi.dwBuildNumber);
            }
        }
    }
    return "Windows (unknown)";
#else
    return "Unknown OS";
#endif
}

/**
 * @brief Gets the machine hostname.
 *
 * Uses the Win32 GetComputerNameA function to obtain the NetBIOS
 * computer name.
 *
 * @return Hostname string, or "unknown" on failure.
 */
std::string AgentClient::getHostname()
{
#ifdef _WIN32
    char buffer[MAX_COMPUTERNAME_LENGTH + 1] = {};
    DWORD size = sizeof(buffer);
    if (GetComputerNameA(buffer, &size)) {
        return std::string(buffer, size);
    }
    return "unknown";
#else
    char buffer[256] = {};
    if (gethostname(buffer, sizeof(buffer)) == 0) {
        return std::string(buffer);
    }
    return "unknown";
#endif
}

} // namespace hub32agent
