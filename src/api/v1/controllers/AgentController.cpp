/**
 * @file AgentController.cpp
 * @brief Implementation of the AgentController REST endpoints.
 */

#include "core/PrecompiledHeader.hpp"
#include "AgentController.hpp"
#include "../dto/AgentDto.hpp"
#include "auth/JwtAuth.hpp"
#include "auth/UserRoleStore.hpp"
#include "agent/AgentRegistry.hpp"
#include "hub32api/agent/AgentInfo.hpp"
#include "hub32api/agent/AgentCommand.hpp"
#include "core/internal/I18n.hpp"
#include "core/internal/CryptoUtils.hpp"
#include "db/ComputerRepository.hpp"
#include "api/common/HttpErrorUtil.hpp"
#include "utils/time_utils.hpp"

// cpp-httplib
#include <httplib.h>

using hub32api::api::common::sendError;

namespace {

std::string getLocale(const httplib::Request& req) {
    auto* i = hub32api::core::internal::I18n::instance();
    if (!i) return "en";
    return i->negotiate(req.get_header_value("Accept-Language"));
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
    dto.lastHeartbeat = hub32api::utils::format_iso8601(info.lastHeartbeat);
    dto.capabilities  = info.capabilities;
    return dto;
}

} // anonymous namespace

namespace hub32api::api::v1 {

/**
 * @brief Constructs the AgentController with the required services.
 * @param registry     Reference to the agent registry.
 * @param jwtAuth      Reference to the JWT auth service for issuing agent tokens.
 * @param agentKeyHash PBKDF2-SHA256 hash of the agent registration key (empty = disabled).
 */
AgentController::AgentController(agent::AgentRegistry& registry, auth::JwtAuth& jwtAuth,
                                  const std::string& agentKeyHash)
    : m_registry(registry)
    , m_jwtAuth(jwtAuth)
    , m_agentKeyHash(agentKeyHash)
{}

/**
 * @brief Wires in a ComputerRepository for agent↔computer DB synchronisation.
 * @param repo Pointer to the ComputerRepository, or nullptr to disable.
 */
void AgentController::setComputerRepository(db::ComputerRepository* repo)
{
    m_computerRepo = repo;
}

/**
 * @brief Handles POST /api/v1/agents/register — agent self-registration.
 *
 * Validates the agentKey against a PBKDF2-SHA256 hash loaded from a file
 * at startup, using constant-time comparison (CRYPTO_memcmp).
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

    // SECURITY: Agent key validation using PBKDF2 hash with constant-time comparison.
    //
    // ATTACK PREVENTED: Previously used std::getenv("HUB32_AGENT_KEY") with
    // string operator!= which is:
    //   (a) NOT constant-time — leaks key bytes via timing side-channel,
    //       allowing an attacker to brute-force the key one byte at a time
    //       by measuring response time differences
    //   (b) Stored in env var — visible in /proc/self/environ, process
    //       listings, container inspection, and crash dumps
    //   (c) No key rotation without server restart
    //
    // Blast radius: arbitrary agent impersonation, commands sent to student PCs.
    //
    // Now the key hash is loaded from a file at startup. The submitted key
    // is verified against the stored PBKDF2-SHA256 hash using OpenSSL
    // CRYPTO_memcmp for constant-time comparison.
    if (m_agentKeyHash.empty()) {
        sendError(res, 503, tr(lang, "error.service_unavailable"),
                  "Agent registration is not configured (no agent key file)");
        return;
    }

    if (!hub32api::auth::UserRoleStore::verifyPassword(reqDto.agentKey, m_agentKeyHash)) {
        sendError(res, 401, tr(lang, "error.unauthorized"), tr(lang, "error.invalid_agent_key"));
        return;
    }

    // --- Generate agent ID ---
    auto agentIdResult = hub32api::core::internal::CryptoUtils::generateUuid();
    if (agentIdResult.is_err()) {
        sendError(res, 500, tr(lang, "error.internal"), "UUID generation failed");
        return;
    }
    const std::string agentId = agentIdResult.take();

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

    // --- Upsert computer record in database (agent registration → computer sync) ---
    std::string computerId;
    std::string locationId;
    if (m_computerRepo) {
        auto existing = m_computerRepo->findByHostname(reqDto.hostname);
        if (existing.is_ok()) {
            computerId = existing.value().id;
            locationId = existing.value().locationId;
            m_computerRepo->updateHeartbeat(existing.value().id, req.remote_addr, reqDto.agentVersion);
            m_computerRepo->updateState(existing.value().id, "online");
        } else {
            auto createResult = m_computerRepo->createUnassigned(reqDto.hostname, reqDto.macAddress);
            if (createResult.is_ok()) {
                computerId = createResult.value();
            }
        }
    }

    // --- Issue JWT with subject=agentId, role="agent" ---
    auto tokenResult = m_jwtAuth.issueToken(agentId, "agent");
    if (tokenResult.is_err()) {
        sendError(res, 500, tr(lang, "error.token_generation_failed"), tokenResult.error().message);
        return;
    }

    // --- Build and return response ---
    dto::AgentRegisterResponse resp;
    resp.agentId = agentId;
    resp.computerId = computerId;
    resp.locationId = locationId;
    resp.authToken = tokenResult.value();
    resp.commandPollIntervalMs = 5000;
    const nlohmann::json j = resp;
    res.status = 201;  // 201 Created — new resource was created
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

    // --- Verify target agent exists before queuing ---
    auto targetAgent = m_registry.findAgent(agentId);
    if (targetAgent.is_err()) {
        sendError(res, 404, tr(lang, "error.agent_not_found"), targetAgent.error().message);
        return;
    }

    // --- Build AgentCommand ---
    auto commandIdResult = hub32api::core::internal::CryptoUtils::generateUuid();
    if (commandIdResult.is_err()) {
        sendError(res, 500, tr(lang, "error.internal"), "UUID generation failed");
        return;
    }
    const std::string commandId = commandIdResult.take();
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
    res.status = 201;  // 201 Created — new command resource was created
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

    // --- Update computer heartbeat in database ---
    if (m_computerRepo) {
        auto agent = m_registry.findAgent(id);
        if (agent.is_ok()) {
            auto computer = m_computerRepo->findByHostname(agent.value().hostname);
            if (computer.is_ok()) {
                m_computerRepo->updateHeartbeat(computer.value().id,
                                                agent.value().ipAddress,
                                                agent.value().agentVersion);
            }
        }
    }

    // --- Dequeue any pending commands for this agent ---
    auto commands = m_registry.dequeuePendingCommands(id);

    nlohmann::json resp;
    resp["ok"] = true;
    resp["pendingCommands"] = nlohmann::json::array();
    for (const auto& cmd : commands) {
        nlohmann::json c;
        c["commandId"]  = cmd.commandId;
        c["featureUid"] = cmd.featureUid;
        c["operation"]  = cmd.operation;
        c["arguments"]  = cmd.arguments;
        resp["pendingCommands"].push_back(std::move(c));
    }

    res.status = 200;
    res.set_content(resp.dump(), "application/json");
}

} // namespace hub32api::api::v1
