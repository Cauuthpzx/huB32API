/**
 * @file AgentController.cpp
 * @brief Implementation of the AgentController REST endpoints.
 */

#include "core/PrecompiledHeader.hpp"
#include "AgentController.hpp"
#include "../dto/AgentDto.hpp"
#include "auth/JwtAuth.hpp"
#include "agent/AgentRegistry.hpp"
#include "hub32api/agent/AgentInfo.hpp"
#include "hub32api/agent/AgentCommand.hpp"
#include "core/internal/I18n.hpp"

// cpp-httplib
#include <httplib.h>

#include <random>
#include <cstdio>
#include <ctime>

namespace {

std::string getLocale(const httplib::Request& req) {
    auto* i = hub32api::core::internal::I18n::instance();
    if (!i) return "en";
    return i->negotiate(req.get_header_value("Accept-Language"));
}

/**
 * @brief Sends an RFC-7807-style JSON error response.
 * @param res    The httplib response to populate.
 * @param status HTTP status code to set.
 * @param title  Short human-readable problem title.
 * @param detail Longer explanation; defaults to @p title when empty.
 */
void sendError(httplib::Response& res,
               int                status,
               const std::string& title,
               const std::string& detail = {})
{
    nlohmann::json j;
    j["status"] = status;
    j["title"]  = title;
    j["detail"] = detail.empty() ? title : detail;
    res.status  = status;
    res.set_content(j.dump(), "application/json");
}

/**
 * @brief Generates a random UUID v4 string.
 *
 * Uses a thread-local mt19937_64 engine seeded from std::random_device
 * to produce two 64-bit random numbers, then formats them as a UUID v4
 * (variant bits 10xx, version bits 0100).
 *
 * @return A UUID v4 string (e.g., "a1b2c3d4-e5f6-4a7b-8c9d-0e1f2a3b4c5d").
 */
std::string generateUuid()
{
    static thread_local std::mt19937_64 gen(std::random_device{}());
    std::uniform_int_distribution<uint64_t> dist;
    auto r1 = dist(gen);
    auto r2 = dist(gen);
    char buf[40];
    std::snprintf(buf, sizeof(buf),
        "%08x-%04x-%04x-%04x-%012llx",
        static_cast<uint32_t>(r1 >> 32),
        static_cast<uint16_t>((r1 >> 16) & 0xFFFF),
        static_cast<uint16_t>(0x4000 | ((r1 & 0x0FFF))),
        static_cast<uint16_t>(0x8000 | ((r2 >> 48) & 0x3FFF)),
        static_cast<unsigned long long>(r2 & 0xFFFFFFFFFFFFULL));
    return buf;
}

/**
 * @brief Converts a system_clock time_point to an ISO 8601 UTC string.
 * @param ts The timestamp to convert.
 * @return ISO 8601 formatted string (e.g., "2026-03-26T12:34:56Z").
 */
std::string timestampToIso(hub32api::Timestamp ts)
{
    auto t = std::chrono::system_clock::to_time_t(ts);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&t));
    return buf;
}

/**
 * @brief Converts an AgentInfo to an AgentStatusDto.
 * @param info The agent info to convert.
 * @return A populated AgentStatusDto.
 */
hub32api::api::v1::dto::AgentStatusDto toStatusDto(const hub32api::AgentInfo& info)
{
    hub32api::api::v1::dto::AgentStatusDto dto;
    dto.agentId       = info.agentId;
    dto.hostname      = info.hostname;
    dto.ipAddress     = info.ipAddress;
    dto.state         = hub32api::to_string(info.state);
    dto.agentVersion  = info.agentVersion;
    dto.lastHeartbeat = timestampToIso(info.lastHeartbeat);
    dto.capabilities  = info.capabilities;
    return dto;
}

} // anonymous namespace

namespace hub32api::api::v1 {

/**
 * @brief Constructs the AgentController with the required services.
 * @param registry Reference to the agent registry.
 * @param jwtAuth  Reference to the JWT auth service for issuing agent tokens.
 */
AgentController::AgentController(agent::AgentRegistry& registry, auth::JwtAuth& jwtAuth)
    : m_registry(registry)
    , m_jwtAuth(jwtAuth)
{}

/**
 * @brief Handles POST /api/v1/agents/register — agent self-registration.
 *
 * Validates the agentKey against the HUB32_AGENT_KEY environment variable.
 * On success, registers the agent in the AgentRegistry and issues a JWT
 * token with role "agent" for subsequent authenticated requests.
 *
 * @param req The incoming HTTP request (body: AgentRegisterRequest JSON).
 * @param res The HTTP response to populate (200 with AgentRegisterResponse, or error).
 */
void AgentController::handleRegister(const httplib::Request& req, httplib::Response& res)
{
    using hub32api::core::internal::tr;
    const auto lang = getLocale(req);

    // --- Parse request body ---
    dto::AgentRegisterRequest reqDto;
    try {
        const auto j = nlohmann::json::parse(req.body);
        reqDto = j.get<dto::AgentRegisterRequest>();
    }
    catch (const std::exception& ex) {
        sendError(res, 400, tr(lang, "error.invalid_request_body"), ex.what());
        return;
    }

    // --- Validate agentKey against HUB32_AGENT_KEY env var ---
    const char* expectedKey = std::getenv("HUB32_AGENT_KEY");
    if (!expectedKey || reqDto.agentKey != expectedKey) {
        sendError(res, 401, tr(lang, "error.unauthorized"), tr(lang, "error.invalid_agent_key"));
        return;
    }

    // --- Generate agent ID ---
    const std::string agentId = generateUuid();

    // --- Build AgentInfo ---
    AgentInfo info;
    info.agentId      = agentId;
    info.hostname     = reqDto.hostname;
    info.ipAddress    = req.remote_addr;
    info.osVersion    = reqDto.osVersion;
    info.agentVersion = reqDto.agentVersion;
    info.capabilities = reqDto.capabilities;

    // --- Register in AgentRegistry ---
    auto regResult = m_registry.registerAgent(info);
    if (regResult.is_err()) {
        sendError(res, 500, tr(lang, "error.registration_failed"), regResult.error().message);
        return;
    }

    // --- Issue JWT with subject=agentId, role="agent" ---
    auto tokenResult = m_jwtAuth.issueToken(agentId, "agent");
    if (tokenResult.is_err()) {
        sendError(res, 500, tr(lang, "error.token_generation_failed"), tokenResult.error().message);
        return;
    }

    // --- Build and return response ---
    const dto::AgentRegisterResponse resp{agentId, tokenResult.value(), 5000};
    const nlohmann::json j = resp;
    res.status = 200;
    res.set_content(j.dump(), "application/json");
}

/**
 * @brief Handles DELETE /api/v1/agents/{id} — unregisters an agent.
 *
 * Removes the agent and its command queue from the registry.
 * Returns 204 No Content on success.
 *
 * @param req The incoming HTTP request (agent ID in path param).
 * @param res The HTTP response to populate (204 on success).
 */
void AgentController::handleUnregister(const httplib::Request& req, httplib::Response& res)
{
    using hub32api::core::internal::tr;
    const auto lang = getLocale(req);
    const std::string id = req.matches.size() > 1 ? req.matches[1].str() : "";
    if (id.empty()) {
        sendError(res, 400, tr(lang, "error.missing_agent_id"));
        return;
    }

    m_registry.unregisterAgent(id);
    res.status = 204;
}

/**
 * @brief Handles GET /api/v1/agents — lists all registered agents.
 *
 * Returns a JSON array of AgentStatusDto objects for every agent
 * currently registered in the AgentRegistry.
 *
 * @param req The incoming HTTP request.
 * @param res The HTTP response to populate (200 with JSON array).
 */
void AgentController::handleList(const httplib::Request& /*req*/, httplib::Response& res)
{
    const auto agents = m_registry.listAgents();

    nlohmann::json arr = nlohmann::json::array();
    for (const auto& agent : agents) {
        const nlohmann::json j = toStatusDto(agent);
        arr.push_back(j);
    }

    res.status = 200;
    res.set_content(arr.dump(), "application/json");
}

/**
 * @brief Handles GET /api/v1/agents/{id}/status — single agent status.
 *
 * Looks up the agent by ID and returns its current status as an
 * AgentStatusDto. Returns 404 if the agent is not found.
 *
 * @param req The incoming HTTP request (agent ID in path param).
 * @param res The HTTP response to populate (200 with AgentStatusDto, or 404).
 */
void AgentController::handleStatus(const httplib::Request& req, httplib::Response& res)
{
    using hub32api::core::internal::tr;
    const auto lang = getLocale(req);
    const std::string id = req.matches.size() > 1 ? req.matches[1].str() : "";
    if (id.empty()) {
        sendError(res, 400, tr(lang, "error.missing_agent_id"));
        return;
    }

    auto result = m_registry.findAgent(id);
    if (result.is_err()) {
        sendError(res, 404, tr(lang, "error.agent_not_found"), result.error().message);
        return;
    }

    const nlohmann::json j = toStatusDto(result.value());
    res.status = 200;
    res.set_content(j.dump(), "application/json");
}

/**
 * @brief Handles POST /api/v1/agents/{id}/commands — push command to agent.
 *
 * Parses the request body as an AgentCommandRequest, generates a command ID,
 * queues it for the target agent, and returns the command ID with "pending" status.
 *
 * @param req The incoming HTTP request (agent ID in path, body: AgentCommandRequest).
 * @param res The HTTP response to populate (200 with AgentCommandResponse).
 */
void AgentController::handlePushCommand(const httplib::Request& req, httplib::Response& res)
{
    using hub32api::core::internal::tr;
    const auto lang = getLocale(req);
    const std::string agentId = req.matches.size() > 1 ? req.matches[1].str() : "";
    if (agentId.empty()) {
        sendError(res, 400, tr(lang, "error.missing_agent_id"));
        return;
    }

    // --- Parse request body ---
    dto::AgentCommandRequest reqDto;
    try {
        const auto j = nlohmann::json::parse(req.body);
        reqDto = j.get<dto::AgentCommandRequest>();
    }
    catch (const std::exception& ex) {
        sendError(res, 400, tr(lang, "error.invalid_request_body"), ex.what());
        return;
    }

    // --- Build AgentCommand ---
    const std::string commandId = generateUuid();
    AgentCommand cmd;
    cmd.commandId  = commandId;
    cmd.agentId    = agentId;
    cmd.featureUid = reqDto.featureUid;
    cmd.operation  = reqDto.operation;
    cmd.arguments  = reqDto.arguments;
    cmd.status     = CommandStatus::Pending;

    // --- Queue via registry ---
    m_registry.queueCommand(cmd);

    // --- Return response ---
    const dto::AgentCommandResponse resp{commandId, "pending"};
    const nlohmann::json j = resp;
    res.status = 200;
    res.set_content(j.dump(), "application/json");
}

/**
 * @brief Handles GET /api/v1/agents/{id}/commands — agent polls pending commands.
 *
 * Atomically dequeues all pending commands for the agent and returns them
 * as a JSON array. Each command includes commandId, featureUid, operation,
 * arguments, and status.
 *
 * @param req The incoming HTTP request (agent ID in path param).
 * @param res The HTTP response to populate (200 with JSON array of commands).
 */
void AgentController::handlePollCommands(const httplib::Request& req, httplib::Response& res)
{
    using hub32api::core::internal::tr;
    const auto lang = getLocale(req);
    const std::string agentId = req.matches.size() > 1 ? req.matches[1].str() : "";
    if (agentId.empty()) {
        sendError(res, 400, tr(lang, "error.missing_agent_id"));
        return;
    }

    const auto commands = m_registry.dequeuePendingCommands(agentId);

    nlohmann::json arr = nlohmann::json::array();
    for (const auto& cmd : commands) {
        nlohmann::json jcmd;
        jcmd["commandId"]  = cmd.commandId;
        jcmd["featureUid"] = cmd.featureUid;
        jcmd["operation"]  = cmd.operation;
        jcmd["arguments"]  = cmd.arguments;
        jcmd["status"]     = to_string(cmd.status);
        arr.push_back(jcmd);
    }

    res.status = 200;
    res.set_content(arr.dump(), "application/json");
}

/**
 * @brief Handles PUT /api/v1/agents/{id}/commands/{cid} — agent reports result.
 *
 * Parses the request body as an AgentCommandResultRequest and updates the
 * command's status, result string, and execution duration in the registry.
 *
 * @param req The incoming HTTP request (agent ID and command ID in path params).
 * @param res The HTTP response to populate (200 with {"ok": true}).
 */
void AgentController::handleReportResult(const httplib::Request& req, httplib::Response& res)
{
    using hub32api::core::internal::tr;
    const auto lang = getLocale(req);
    const std::string agentId   = req.matches.size() > 1 ? req.matches[1].str() : "";
    const std::string commandId = req.matches.size() > 2 ? req.matches[2].str() : "";
    if (agentId.empty() || commandId.empty()) {
        sendError(res, 400, tr(lang, "error.missing_agent_or_command_id"));
        return;
    }

    // --- Parse request body ---
    dto::AgentCommandResultRequest reqDto;
    try {
        const auto j = nlohmann::json::parse(req.body);
        reqDto = j.get<dto::AgentCommandResultRequest>();
    }
    catch (const std::exception& ex) {
        sendError(res, 400, tr(lang, "error.invalid_request_body"), ex.what());
        return;
    }

    // --- Report command result ---
    const CommandStatus status = command_status_from_string(reqDto.status);
    m_registry.reportCommandResult(commandId, status, reqDto.result, reqDto.durationMs);

    // --- Return success ---
    nlohmann::json j;
    j["ok"] = true;
    res.status = 200;
    res.set_content(j.dump(), "application/json");
}

/**
 * @brief Handles POST /api/v1/agents/{id}/heartbeat — agent heartbeat.
 *
 * Updates the agent's lastHeartbeat timestamp and transitions it to
 * Online state if it was previously Offline or in Error state.
 *
 * @param req The incoming HTTP request (agent ID in path param).
 * @param res The HTTP response to populate (200 with {"ok": true}).
 */
void AgentController::handleHeartbeat(const httplib::Request& req, httplib::Response& res)
{
    using hub32api::core::internal::tr;
    const auto lang = getLocale(req);
    const std::string id = req.matches.size() > 1 ? req.matches[1].str() : "";
    if (id.empty()) {
        sendError(res, 400, tr(lang, "error.missing_agent_id"));
        return;
    }

    m_registry.heartbeat(id);

    nlohmann::json j;
    j["ok"] = true;
    res.status = 200;
    res.set_content(j.dump(), "application/json");
}

} // namespace hub32api::api::v1
