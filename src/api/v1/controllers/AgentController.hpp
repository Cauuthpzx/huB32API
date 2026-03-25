#pragma once

/**
 * @file AgentController.hpp
 * @brief REST controller for agent management endpoints.
 */

// Forward declarations
namespace httplib { class Request; class Response; }
namespace hub32api::agent { class AgentRegistry; }
namespace hub32api::auth { class JwtAuth; }

namespace hub32api::api::v1 {

/**
 * @brief REST controller for agent management endpoints.
 *
 * Handles 8 endpoints:
 * - POST   /api/v1/agents/register            — agent self-registration (public, validates agentKey via HUB32_AGENT_KEY env var)
 * - DELETE  /api/v1/agents/{id}                — unregister agent (protected)
 * - GET     /api/v1/agents                     — list all agents (protected, admin)
 * - GET     /api/v1/agents/{id}/status         — single agent status (protected)
 * - POST    /api/v1/agents/{id}/commands       — push command to agent (protected)
 * - GET     /api/v1/agents/{id}/commands       — agent polls pending commands (protected)
 * - PUT     /api/v1/agents/{id}/commands/{cid} — agent reports result (protected)
 * - POST    /api/v1/agents/{id}/heartbeat      — agent heartbeat (protected)
 */
class AgentController
{
public:
    /**
     * @brief Constructs the AgentController.
     * @param registry Reference to the agent registry.
     * @param jwtAuth  Reference to JWT auth service (for issuing agent tokens).
     */
    AgentController(agent::AgentRegistry& registry, auth::JwtAuth& jwtAuth);

    /**
     * @brief Handles POST /api/v1/agents/register — agent self-registration.
     * @param req The incoming HTTP request.
     * @param res The HTTP response to populate.
     */
    void handleRegister(const httplib::Request& req, httplib::Response& res);

    /**
     * @brief Handles DELETE /api/v1/agents/{id} — unregister an agent.
     * @param req The incoming HTTP request.
     * @param res The HTTP response to populate.
     */
    void handleUnregister(const httplib::Request& req, httplib::Response& res);

    /**
     * @brief Handles GET /api/v1/agents — list all agents.
     * @param req The incoming HTTP request.
     * @param res The HTTP response to populate.
     */
    void handleList(const httplib::Request& req, httplib::Response& res);

    /**
     * @brief Handles GET /api/v1/agents/{id}/status — single agent status.
     * @param req The incoming HTTP request.
     * @param res The HTTP response to populate.
     */
    void handleStatus(const httplib::Request& req, httplib::Response& res);

    /**
     * @brief Handles POST /api/v1/agents/{id}/commands — push command to agent.
     * @param req The incoming HTTP request.
     * @param res The HTTP response to populate.
     */
    void handlePushCommand(const httplib::Request& req, httplib::Response& res);

    /**
     * @brief Handles GET /api/v1/agents/{id}/commands — agent polls pending commands.
     * @param req The incoming HTTP request.
     * @param res The HTTP response to populate.
     */
    void handlePollCommands(const httplib::Request& req, httplib::Response& res);

    /**
     * @brief Handles PUT /api/v1/agents/{id}/commands/{cid} — agent reports result.
     * @param req The incoming HTTP request.
     * @param res The HTTP response to populate.
     */
    void handleReportResult(const httplib::Request& req, httplib::Response& res);

    /**
     * @brief Handles POST /api/v1/agents/{id}/heartbeat — agent heartbeat.
     * @param req The incoming HTTP request.
     * @param res The HTTP response to populate.
     */
    void handleHeartbeat(const httplib::Request& req, httplib::Response& res);

private:
    agent::AgentRegistry& m_registry; ///< Agent registry for managing agent state
    auth::JwtAuth& m_jwtAuth;         ///< JWT auth service for issuing tokens
};

} // namespace hub32api::api::v1
