/**
 * @file Router.cpp
 * @brief Binds all hub32api REST routes to the cpp-httplib server instance.
 *
 * Full route table:
 *
 *  v1 — Auth (public)
 *   GET    /api/v1/auth/methods
 *   POST   /api/v1/auth
 *   DELETE /api/v1/auth
 *
 *  v1 — Computers (protected: JWT required)
 *   GET    /api/v1/computers          ?location=&state=&limit=&after=
 *   GET    /api/v1/computers/:id
 *   GET    /api/v1/computers/:id/info
 *   GET    /api/v1/computers/:id/session
 *   GET    /api/v1/computers/:id/user
 *   GET    /api/v1/computers/:id/screens
 *   GET    /api/v1/computers/:id/framebuffer
 *
 *  v1 — Features (protected)
 *   GET    /api/v1/computers/:id/features
 *   GET    /api/v1/computers/:id/features/:fid
 *   PUT    /api/v1/computers/:id/features/:fid
 *
 *  v1 — Agents
 *   POST   /api/v1/agents/register                 (public, validates agentKey)
 *   GET    /api/v1/agents                           (protected)
 *   DELETE /api/v1/agents/:id                       (protected)
 *   GET    /api/v1/agents/:id/status                (protected)
 *   POST   /api/v1/agents/:id/commands              (protected)
 *   GET    /api/v1/agents/:id/commands              (protected)
 *   PUT    /api/v1/agents/:id/commands/:cid         (protected)
 *   POST   /api/v1/agents/:id/heartbeat             (protected)
 *
 *  v2 — Locations (protected)
 *   GET    /api/v2/locations
 *   GET    /api/v2/locations/:id
 *
 *  v2 — Batch (protected)
 *   POST   /api/v2/batch/features
 *
 *  v2 — Computer TCP state (protected) — exceeds original WebAPI
 *   GET    /api/v2/computers/:id/state
 *
 *  v2 — Metrics / Health (public)
 *   GET    /api/v2/health
 *   GET    /health
 *   GET    /api/v2/metrics
 *
 *  OpenAPI (public)
 *   GET    /openapi.json
 */

#include "../core/PrecompiledHeader.hpp"
#include "internal/Router.hpp"

// Controllers — v1
#include "../api/v1/controllers/AuthController.hpp"
#include "../api/v1/controllers/AgentController.hpp"
#include "../api/v1/controllers/ComputerController.hpp"
#include "../api/v1/controllers/FeatureController.hpp"
#include "../api/v1/controllers/FramebufferController.hpp"
#include "../api/v1/controllers/SessionController.hpp"
#include "../api/v1/controllers/SchoolController.hpp"
#include "../api/v1/controllers/TeacherController.hpp"

// Controllers — v2
#include "../api/v2/controllers/BatchController.hpp"
#include "../api/v2/controllers/LocationController.hpp"
#include "../api/v2/controllers/MetricsController.hpp"

// Middleware
#include "../api/v1/middleware/AuthMiddleware.hpp"
#include "../api/v1/middleware/CorsMiddleware.hpp"
#include "../api/v1/middleware/InputValidationMiddleware.hpp"
#include "../api/v1/middleware/LoggingMiddleware.hpp"
#include "../api/v1/middleware/RateLimitMiddleware.hpp"

// Core
#include "../core/internal/I18n.hpp"
#include "../core/internal/ApiContext.hpp"
#include "../core/internal/PluginRegistry.hpp"
#include "../core/internal/ConnectionPool.hpp"

// Auth
#include "../auth/JwtAuth.hpp"
#include "../auth/Hub32KeyAuth.hpp"

// Agent
#include "../agent/AgentRegistry.hpp"

// Database repositories
#include "../db/SchoolRepository.hpp"
#include "../db/LocationRepository.hpp"
#include "../db/ComputerRepository.hpp"
#include "../db/TeacherRepository.hpp"
#include "../db/TeacherLocationRepository.hpp"

// SSE
#include "SseManager.hpp"

#include "../plugins/metrics/MetricsPlugin.hpp"

#include <httplib.h>
#include <sstream>
#include <cstdlib>

#ifdef _WIN32
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  include <iphlpapi.h>
#  include <icmpapi.h>
#endif

namespace hub32api::server::internal {

// ─────────────────────────────────────────────────────────────────────────────
// Internal helpers
// ─────────────────────────────────────────────────────────────────────────────

namespace {

/**
 * @brief Resolves the client's preferred locale from the Accept-Language header.
 * @param req The incoming HTTP request.
 * @return The best matching locale code.
 */
std::string resolveLocale(const httplib::Request& req)
{
    auto* i18n = hub32api::core::internal::I18n::instance();
    if (!i18n) return "en";
    const std::string acceptLang = req.get_header_value("Accept-Language");
    return i18n->negotiate(acceptLang);
}

/**
 * @brief Sends an RFC-7807 Problem Details JSON error response.
 */
void sendError(httplib::Response& res,
               int status,
               const std::string& title,
               const std::string& detail = {})
{
    nlohmann::json j;
    j["status"] = status;
    j["title"]  = title;
    j["detail"] = detail.empty() ? title : detail;
    res.status  = status;
    res.set_content(j.dump(), "application/problem+json");
}

/**
 * @brief Non-blocking TCP connect check. Returns true if @p host:@p port
 *        accepts a connection within @p timeoutMs milliseconds.
 *
 * Used for GET /api/v2/computers/:id/state — equivalent to the original
 * WebAPI hoststate endpoint but with actual latency measurement.
 */
bool tcpPing(const std::string& host, int port = 11100, int timeoutMs = 1500)
{
#ifdef _WIN32
    // WSAStartup is reference-counted; we init once and never cleanup
    // because httplib also calls it. A process-wide singleton is fine.
    static const bool wsaReady = [] {
        WSADATA wsa{};
        return WSAStartup(MAKEWORD(2, 2), &wsa) == 0;
    }();
    if (!wsaReady) return false;

    addrinfo hints{}, *result = nullptr;
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    const std::string portStr = std::to_string(port);
    if (::getaddrinfo(host.c_str(), portStr.c_str(), &hints, &result) != 0)
        return false;

    SOCKET sock = ::socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (sock == INVALID_SOCKET) { freeaddrinfo(result); return false; }

    u_long mode = 1;
    ::ioctlsocket(sock, FIONBIO, &mode);
    ::connect(sock, result->ai_addr, static_cast<int>(result->ai_addrlen));
    freeaddrinfo(result);

    fd_set wfds;
    FD_ZERO(&wfds);
    FD_SET(sock, &wfds);
    timeval tv{ timeoutMs / 1000, (timeoutMs % 1000) * 1000 };
    const bool ok = ::select(0, nullptr, &wfds, nullptr, &tv) > 0;
    ::closesocket(sock);
    return ok;
#else
    (void)host; (void)port; (void)timeoutMs;
    return false;
#endif
}

struct HostStateResult {
    std::string state;   // "online" | "up" | "down"
    bool reachable = false;
    int latencyMs = -1;
    int pingLatencyMs = -1;
};

HostStateResult checkHostState(const std::string& host, int vncPort = 11100, int timeoutMs = 1500)
{
    HostStateResult result;

    // 1. Try TCP connect (VNC port)
    if (tcpPing(host, vncPort, timeoutMs)) {
        result.state = "online";
        result.reachable = true;
        // measure latency
        auto start = std::chrono::steady_clock::now();
        tcpPing(host, vncPort, 500);
        auto end = std::chrono::steady_clock::now();
        result.latencyMs = static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count());
        return result;
    }

    // 2. Fallback: ICMP ping (Windows only)
#ifdef _WIN32
    {
        HANDLE hIcmp = IcmpCreateFile();
        if (hIcmp != INVALID_HANDLE_VALUE) {
            // Resolve hostname
            addrinfo hints{}, *res = nullptr;
            hints.ai_family = AF_INET;
            if (::getaddrinfo(host.c_str(), nullptr, &hints, &res) == 0 && res) {
                auto* addr = reinterpret_cast<sockaddr_in*>(res->ai_addr);
                IPAddr destIp = addr->sin_addr.S_un.S_addr;
                freeaddrinfo(res);

                char replyBuf[sizeof(ICMP_ECHO_REPLY) + 32];
                DWORD ret = IcmpSendEcho(hIcmp, destIp, nullptr, 0,
                                         nullptr, replyBuf, sizeof(replyBuf),
                                         static_cast<DWORD>(timeoutMs));
                if (ret > 0) {
                    auto* reply = reinterpret_cast<ICMP_ECHO_REPLY*>(replyBuf);
                    result.state = "up";
                    result.reachable = true;
                    result.pingLatencyMs = static_cast<int>(reply->RoundTripTime);
                    IcmpCloseHandle(hIcmp);
                    return result;
                }
            }
            IcmpCloseHandle(hIcmp);
        }
    }
#endif

    // 3. Both failed
    result.state = "down";
    result.reachable = false;
    return result;
}

/**
 * @brief Builds and returns the OpenAPI 3.1 specification as a JSON object.
 *
 * Self-contained — no external YAML/JSON file required.
 * Covers all implemented endpoints with schemas, security, and parameters.
 */
nlohmann::json buildOpenApiSpec()
{
    using json = nlohmann::json;

    json spec;
    spec["openapi"] = "3.1.0";
    spec["info"] = {
        {"title",       "hub32api"},
        {"description", "Modern REST API for Hub32 classroom management. "
                        "Surpasses the original WebAPI plugin with JWT auth, "
                        "batch operations, location management, and Prometheus metrics."},
        {"version",     "1.0.0"},
        {"license",     {{"name","GPL-2.0"}}}
    };
    spec["servers"] = json::array({ {{"url","/"}, {"description","Current server"}} });

    // Security
    spec["components"]["securitySchemes"]["bearerAuth"] = {
        {"type","http"}, {"scheme","bearer"}, {"bearerFormat","JWT"}
    };
    const json noAuth    = json::array();
    const json bearerSec = json::array({ json::object({{"bearerAuth", json::array()}}) });

    // Schemas
    auto& s = spec["components"]["schemas"];

    s["Error"] = { {"type","object"}, {"properties", {
        {"status",{{"type","integer"}}},
        {"title", {{"type","string"}}},
        {"detail",{{"type","string"}}}
    }}};

    s["Computer"] = { {"type","object"}, {"properties", {
        {"id",      {{"type","string"}}},
        {"name",    {{"type","string"}}},
        {"hostname",{{"type","string"}}},
        {"location",{{"type","string"}}},
        {"state",   {{"type","string"},{"enum",json::array({"online","offline","locked","unknown"})}}}
    }}};

    s["PageInfo"] = { {"type","object"}, {"properties", {
        {"total",      {{"type","integer"}}},
        {"limit",      {{"type","integer"}}},
        {"nextCursor", {{"type","string"}, {"nullable",true}}}
    }}};

    s["Feature"] = { {"type","object"}, {"properties", {
        {"uid",         {{"type","string"}}},
        {"name",        {{"type","string"}}},
        {"description", {{"type","string"}}},
        {"isActive",    {{"type","boolean"}}}
    }}};

    s["AuthRequest"] = {
        {"type","object"},
        {"required", json::array({"method","username"})},
        {"properties", {
            {"method",   {{"type","string"},{"enum",json::array({"hub32-key","logon"})}}},
            {"username", {{"type","string"}}},
            {"password", {{"type","string"}}},
            {"keyName",  {{"type","string"}}},
            {"keyData",  {{"type","string"}}}
        }}
    };

    s["AuthResponse"] = { {"type","object"}, {"properties", {
        {"token",    {{"type","string"}}},
        {"tokenType",{{"type","string"}}},
        {"expiresIn",{{"type","integer"}}}
    }}};

    s["BatchFeatureRequest"] = {
        {"type","object"},
        {"required", json::array({"computerIds","featureUid","operation"})},
        {"properties", {
            {"computerIds",{{"type","array"},{"items",{{"type","string"}}}}},
            {"featureUid", {{"type","string"}}},
            {"operation",  {{"type","string"},{"enum",json::array({"start","stop"})}}},
            {"arguments",  {{"type","object"}}}
        }}
    };

    s["HostState"] = { {"type","object"}, {"properties", {
        {"id",       {{"type","string"}}},
        {"hostname", {{"type","string"}}},
        {"online",   {{"type","boolean"}}},
        {"latencyMs",{{"type","integer"}}}
    }}};

    s["AgentRegisterRequest"] = {
        {"type","object"},
        {"required", json::array({"hostname","agentKey"})},
        {"properties", {
            {"hostname",    {{"type","string"}}},
            {"agentKey",    {{"type","string"}}},
            {"osVersion",   {{"type","string"}}},
            {"agentVersion",{{"type","string"}}},
            {"capabilities",{{"type","array"},{"items",{{"type","string"}}}}}
        }}
    };

    s["AgentRegisterResponse"] = { {"type","object"}, {"properties", {
        {"agentId",             {{"type","string"}}},
        {"authToken",           {{"type","string"}}},
        {"commandPollIntervalMs",{{"type","integer"}}}
    }}};

    s["AgentStatus"] = { {"type","object"}, {"properties", {
        {"agentId",      {{"type","string"}}},
        {"hostname",     {{"type","string"}}},
        {"ipAddress",    {{"type","string"}}},
        {"state",        {{"type","string"},{"enum",json::array({"offline","online","busy","error"})}}},
        {"agentVersion", {{"type","string"}}},
        {"lastHeartbeat",{{"type","string"},{"format","date-time"}}},
        {"capabilities", {{"type","array"},{"items",{{"type","string"}}}}}
    }}};

    s["AgentCommandRequest"] = {
        {"type","object"},
        {"required", json::array({"featureUid","operation"})},
        {"properties", {
            {"featureUid",{{"type","string"}}},
            {"operation", {{"type","string"},{"enum",json::array({"start","stop"})}}},
            {"arguments", {{"type","object"}}}
        }}
    };

    s["AgentCommandResponse"] = { {"type","object"}, {"properties", {
        {"commandId",{{"type","string"}}},
        {"status",   {{"type","string"}}}
    }}};

    // Helper lambda for $ref
    auto ref = [](const std::string& name) -> json {
        return { {"$ref", "#/components/schemas/" + name} };
    };

    // Reusable path parameter
    auto idParam = json::object({
        {"name","id"},{"in","path"},{"required",true},
        {"schema",{{"type","string"}}}
    });

    auto& paths = spec["paths"];

    // GET /api/v1/auth/methods
    {
        json op;
        op["summary"] = "List available authentication methods";
        op["tags"]    = json::array({"Auth"});
        op["security"] = noAuth;
        op["responses"]["200"]["description"] = "OK";
        op["responses"]["200"]["content"]["application/json"]["schema"]["type"] = "object";
        op["responses"]["200"]["content"]["application/json"]["schema"]["properties"]["methods"]["type"] = "array";
        op["responses"]["200"]["content"]["application/json"]["schema"]["properties"]["methods"]["items"]["type"] = "string";
        paths["/api/v1/auth/methods"]["get"] = op;
    }

    // POST /api/v1/auth
    {
        json op;
        op["summary"] = "Authenticate and obtain JWT Bearer token";
        op["tags"]    = json::array({"Auth"});
        op["security"] = noAuth;
        op["requestBody"]["required"] = true;
        op["requestBody"]["content"]["application/json"]["schema"] = ref("AuthRequest");
        op["responses"]["200"]["description"] = "Token issued";
        op["responses"]["200"]["content"]["application/json"]["schema"] = ref("AuthResponse");
        op["responses"]["401"]["description"] = "Bad credentials";
        paths["/api/v1/auth"]["post"] = op;
    }

    // DELETE /api/v1/auth
    {
        json op;
        op["summary"] = "Logout / revoke current token";
        op["tags"]    = json::array({"Auth"});
        op["security"] = bearerSec;
        op["responses"]["204"]["description"] = "Token revoked";
        paths["/api/v1/auth"]["delete"] = op;
    }

    // GET /api/v1/computers
    {
        json op;
        op["summary"]  = "List all computers with optional filtering and pagination";
        op["tags"]     = json::array({"Computers"});
        op["security"] = bearerSec;
        op["parameters"] = json::array({
            json{{"name","location"},{"in","query"},{"schema",json{{"type","string"}}}},
            json{{"name","state"},   {"in","query"},{"schema",json{{"type","string"}}}},
            json{{"name","limit"},   {"in","query"},{"schema",json{{"type","integer"},{"default",50},{"maximum",200}}}},
            json{{"name","after"},   {"in","query"},{"description","Opaque cursor"},{"schema",json{{"type","string"}}}}
        });
        op["responses"]["200"]["description"] = "OK";
        op["responses"]["200"]["content"]["application/json"]["schema"]["type"] = "object";
        op["responses"]["200"]["content"]["application/json"]["schema"]["properties"]["computers"]["type"] = "array";
        op["responses"]["200"]["content"]["application/json"]["schema"]["properties"]["computers"]["items"] = ref("Computer");
        op["responses"]["200"]["content"]["application/json"]["schema"]["properties"]["page"] = ref("PageInfo");
        paths["/api/v1/computers"]["get"] = op;
    }

    // GET /api/v1/computers/{id}
    {
        json op;
        op["summary"]  = "Get a single computer by UID";
        op["tags"]     = json::array({"Computers"});
        op["security"] = bearerSec;
        op["parameters"] = json::array({idParam});
        op["responses"]["200"]["description"] = "OK";
        op["responses"]["200"]["content"]["application/json"]["schema"] = ref("Computer");
        op["responses"]["404"]["description"] = "Not found";
        paths["/api/v1/computers/{id}"]["get"] = op;
    }

    // GET /api/v1/computers/{id}/features
    {
        json op;
        op["summary"]  = "List features available on a computer";
        op["tags"]     = json::array({"Features"});
        op["security"] = bearerSec;
        op["parameters"] = json::array({idParam});
        op["responses"]["200"]["description"] = "OK";
        paths["/api/v1/computers/{id}/features"]["get"] = op;
    }

    // PUT /api/v1/computers/{id}/features/{fid}
    {
        json op;
        op["summary"]  = "Start or stop a feature on a computer";
        op["tags"]     = json::array({"Features"});
        op["security"] = bearerSec;
        json fidParam{{"name","fid"},{"in","path"},{"required",true},{"schema",json{{"type","string"}}}};
        op["parameters"] = json::array({idParam, fidParam});
        op["requestBody"]["required"] = true;
        op["requestBody"]["content"]["application/json"]["schema"]["type"] = "object";
        op["requestBody"]["content"]["application/json"]["schema"]["properties"]["active"]["type"] = "boolean";
        op["requestBody"]["content"]["application/json"]["schema"]["properties"]["arguments"]["type"] = "object";
        op["responses"]["200"]["description"] = "Feature updated";
        op["responses"]["404"]["description"] = "Not found";
        paths["/api/v1/computers/{id}/features/{fid}"]["put"] = op;
    }

    // POST /api/v2/batch/features
    {
        json op;
        op["summary"]  = "Apply a feature operation to multiple computers at once";
        op["tags"]     = json::array({"Batch","v2"});
        op["security"] = bearerSec;
        op["requestBody"]["required"] = true;
        op["requestBody"]["content"]["application/json"]["schema"] = ref("BatchFeatureRequest");
        op["responses"]["200"]["description"] = "Batch result per computer";
        paths["/api/v2/batch/features"]["post"] = op;
    }

    // GET /api/v2/computers/{id}/state
    {
        json op;
        op["summary"]     = "Check host reachability via TCP ping";
        op["description"] = "Non-blocking TCP connect to the Hub32 service port. "
                            "Returns online status and round-trip latency in ms. "
                            "Unique to hub32api — not present in original WebAPI plugin.";
        op["tags"]        = json::array({"Computers","v2"});
        op["security"]    = bearerSec;
        op["parameters"]  = json::array({idParam});
        op["responses"]["200"]["description"] = "OK";
        op["responses"]["200"]["content"]["application/json"]["schema"] = ref("HostState");
        paths["/api/v2/computers/{id}/state"]["get"] = op;
    }

    // GET /api/v2/locations
    {
        json op;
        op["summary"]  = "List all locations (classroom groups)";
        op["tags"]     = json::array({"Locations","v2"});
        op["security"] = bearerSec;
        op["responses"]["200"]["description"] = "OK";
        paths["/api/v2/locations"]["get"] = op;
    }

    // GET /api/v2/health
    {
        json op;
        op["summary"]  = "Health check — 200 OK or 503 Degraded";
        op["tags"]     = json::array({"System"});
        op["security"] = noAuth;
        op["responses"]["200"]["description"] = "Healthy";
        op["responses"]["503"]["description"] = "Degraded";
        paths["/api/v2/health"]["get"] = op;
    }

    // GET /api/v2/metrics
    {
        json op;
        op["summary"]  = "Server metrics in JSON or Prometheus format";
        op["tags"]     = json::array({"System"});
        op["security"] = noAuth;
        json acceptParam{{"name","Accept"},{"in","header"},
            {"schema",json{{"type","string"},{"enum",json::array({"application/json","text/plain"})}}}};
        op["parameters"] = json::array({acceptParam});
        op["responses"]["200"]["description"] = "Metrics";
        paths["/api/v2/metrics"]["get"] = op;
    }

    // ── Agent endpoints ──────────────────────────────────────────────────

    // Reusable agent path parameter
    auto agentIdParam = json::object({
        {"name","id"},{"in","path"},{"required",true},
        {"description","Agent UUID"},
        {"schema",json{{"type","string"}}}
    });
    auto cmdIdParam = json::object({
        {"name","cid"},{"in","path"},{"required",true},
        {"description","Command UUID"},
        {"schema",json{{"type","string"}}}
    });

    // POST /api/v1/agents/register
    {
        json op;
        op["summary"]     = "Register a new agent";
        op["description"] = "Agents self-register by providing a pre-shared agentKey. "
                            "Returns a JWT for subsequent authenticated requests.";
        op["tags"]        = json::array({"Agents"});
        op["security"]    = noAuth;
        op["requestBody"]["required"] = true;
        op["requestBody"]["content"]["application/json"]["schema"] = ref("AgentRegisterRequest");
        op["responses"]["200"]["description"] = "Agent registered";
        op["responses"]["200"]["content"]["application/json"]["schema"] = ref("AgentRegisterResponse");
        op["responses"]["401"]["description"] = "Invalid agent key";
        paths["/api/v1/agents/register"]["post"] = op;
    }

    // GET /api/v1/agents
    {
        json op;
        op["summary"]  = "List all registered agents";
        op["tags"]     = json::array({"Agents"});
        op["security"] = bearerSec;
        op["responses"]["200"]["description"] = "OK";
        op["responses"]["200"]["content"]["application/json"]["schema"]["type"] = "array";
        op["responses"]["200"]["content"]["application/json"]["schema"]["items"] = ref("AgentStatus");
        paths["/api/v1/agents"]["get"] = op;
    }

    // DELETE /api/v1/agents/{id}
    {
        json op;
        op["summary"]    = "Unregister an agent";
        op["tags"]       = json::array({"Agents"});
        op["security"]   = bearerSec;
        op["parameters"] = json::array({agentIdParam});
        op["responses"]["204"]["description"] = "Agent unregistered";
        paths["/api/v1/agents/{id}"]["delete"] = op;
    }

    // GET /api/v1/agents/{id}/status
    {
        json op;
        op["summary"]    = "Get agent status";
        op["tags"]       = json::array({"Agents"});
        op["security"]   = bearerSec;
        op["parameters"] = json::array({agentIdParam});
        op["responses"]["200"]["description"] = "OK";
        op["responses"]["200"]["content"]["application/json"]["schema"] = ref("AgentStatus");
        op["responses"]["404"]["description"] = "Agent not found";
        paths["/api/v1/agents/{id}/status"]["get"] = op;
    }

    // POST /api/v1/agents/{id}/commands
    {
        json op;
        op["summary"]    = "Push a command to an agent";
        op["tags"]       = json::array({"Agents"});
        op["security"]   = bearerSec;
        op["parameters"] = json::array({agentIdParam});
        op["requestBody"]["required"] = true;
        op["requestBody"]["content"]["application/json"]["schema"] = ref("AgentCommandRequest");
        op["responses"]["200"]["description"] = "Command queued";
        op["responses"]["200"]["content"]["application/json"]["schema"] = ref("AgentCommandResponse");
        paths["/api/v1/agents/{id}/commands"]["post"] = op;
    }

    // GET /api/v1/agents/{id}/commands
    {
        json op;
        op["summary"]    = "Poll pending commands for an agent";
        op["tags"]       = json::array({"Agents"});
        op["security"]   = bearerSec;
        op["parameters"] = json::array({agentIdParam});
        op["responses"]["200"]["description"] = "OK";
        op["responses"]["200"]["content"]["application/json"]["schema"]["type"] = "array";
        paths["/api/v1/agents/{id}/commands"]["get"] = op;
    }

    // PUT /api/v1/agents/{id}/commands/{cid}
    {
        json op;
        op["summary"]    = "Report command execution result";
        op["tags"]       = json::array({"Agents"});
        op["security"]   = bearerSec;
        op["parameters"] = json::array({agentIdParam, cmdIdParam});
        op["requestBody"]["required"] = true;
        op["requestBody"]["content"]["application/json"]["schema"]["type"] = "object";
        op["requestBody"]["content"]["application/json"]["schema"]["properties"]["commandId"]["type"] = "string";
        op["requestBody"]["content"]["application/json"]["schema"]["properties"]["status"]["type"] = "string";
        op["requestBody"]["content"]["application/json"]["schema"]["properties"]["result"]["type"] = "string";
        op["requestBody"]["content"]["application/json"]["schema"]["properties"]["durationMs"]["type"] = "integer";
        op["responses"]["200"]["description"] = "Result recorded";
        paths["/api/v1/agents/{id}/commands/{cid}"]["put"] = op;
    }

    // POST /api/v1/agents/{id}/heartbeat
    {
        json op;
        op["summary"]    = "Agent heartbeat";
        op["tags"]       = json::array({"Agents"});
        op["security"]   = bearerSec;
        op["parameters"] = json::array({agentIdParam});
        op["responses"]["200"]["description"] = "OK";
        paths["/api/v1/agents/{id}/heartbeat"]["post"] = op;
    }

    return spec;
}

} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────
// Router public
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief Constructs the Router.
 * @param server  The httplib::Server to register routes on.
 * @param svcs    All service references needed by controllers.
 */
Router::Router(httplib::Server& server, Services svcs)
    : m_server(server), m_svcs(svcs), m_sse(std::make_shared<server::SseManager>()) {}

/**
 * @brief Registers all routes. Call once before Server::listen().
 */
void Router::registerAll()
{
    registerV1();
    registerAgentRoutes();
    registerSchoolRoutes();
    registerTeacherRoutes();
    registerV2();
    registerHealthAndMetrics();
    registerOpenApi();
    registerSse();
    registerDebug();

    // Wire MetricsPlugin to record every request
    auto* metricsRaw = m_svcs.registry.find("a1b2c3d4-0004-0004-0004-000000000004");
    if (auto* mp = dynamic_cast<plugins::MetricsPlugin*>(metricsRaw)) {
        m_server.set_post_routing_handler(
            [mp](const httplib::Request& /*req*/, httplib::Response& res) {
                mp->recordRequest(res.status);
            });
    }

    spdlog::info("[Router] all routes registered");
}

// ─────────────────────────────────────────────────────────────────────────────
// v1
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief Registers all /api/v1/ routes.
 *
 * Each route is wrapped with: CORS → Logging → RateLimit → (Auth for protected)
 */
void Router::registerV1()
{
    // ── Controllers ───────────────────────────────────────────────────────
    auto authCtrl     = std::make_shared<api::v1::AuthController>(m_svcs.jwtAuth, m_svcs.keyAuth, m_svcs.roleStore);
    auto computerCtrl = std::make_shared<api::v1::ComputerController>(m_svcs.registry);
    auto featureCtrl  = std::make_shared<api::v1::FeatureController>(m_svcs.registry);
    auto fbCtrl       = std::make_shared<api::v1::FramebufferController>(m_svcs.registry);
    auto sessionCtrl  = std::make_shared<api::v1::SessionController>(m_svcs.registry);

    // ── Middleware ────────────────────────────────────────────────────────
    const api::v1::middleware::CorsConfig corsConfig{
        {"*"},
        {"GET","POST","PUT","DELETE","OPTIONS"},
        {"Authorization","Content-Type","X-Request-ID","Accept"},
        false,
        3600
    };
    const api::v1::middleware::RateLimitConfig rlConfig{ 120, 20 };

    auto cors   = std::make_shared<api::v1::middleware::CorsMiddleware>(corsConfig);
    auto logger = std::make_shared<api::v1::middleware::LoggingMiddleware>();
    auto iv     = std::make_shared<api::v1::middleware::InputValidationMiddleware>();
    auto rl     = std::make_shared<api::v1::middleware::RateLimitMiddleware>(rlConfig);
    auto auth   = std::make_shared<api::v1::middleware::AuthMiddleware>(m_svcs.jwtAuth);

    // ── Route builders ────────────────────────────────────────────────────
    // Public: CORS + Logging + InputValidation + RateLimit (no JWT)
    auto publicRoute = [&](const std::string& method, const std::string& path, auto handler)
    {
        auto h = [cors, logger, iv, rl, handler]
            (const httplib::Request& req, httplib::Response& res)
        {
            logger->logRequest(req);
            if (!cors->process(req, res)) { logger->logResponse(req, res); return; }
            if (!iv->process(req, res))   { logger->logResponse(req, res); return; }
            if (!rl->process(req, res))   { logger->logResponse(req, res); return; }
            handler(req, res);
            logger->logResponse(req, res);
        };
        if (method == "GET")    m_server.Get(path.c_str(), h);
        else if (method == "POST")   m_server.Post(path.c_str(), h);
        else if (method == "DELETE") m_server.Delete(path.c_str(), h);
        else if (method == "OPTIONS")m_server.Options(path.c_str(), h);
    };

    // Protected: CORS + Logging + InputValidation + RateLimit + JWT
    auto protectedRoute = [&](const std::string& method, const std::string& path, auto handler)
    {
        auto h = [cors, logger, iv, rl, auth, handler]
            (const httplib::Request& req, httplib::Response& res)
        {
            logger->logRequest(req);
            if (!cors->process(req, res)) { logger->logResponse(req, res); return; }
            if (!iv->process(req, res))   { logger->logResponse(req, res); return; }
            if (!rl->process(req, res))   { logger->logResponse(req, res); return; }
            core::internal::ApiContext ctx;
            ctx.requestId = req.has_header("X-Request-ID")
                ? req.get_header_value("X-Request-ID") : "";
            if (!auth->process(req, res, ctx)) { logger->logResponse(req, res); return; }
            handler(req, res);
            logger->logResponse(req, res);
        };
        if (method == "GET")    m_server.Get(path.c_str(), h);
        else if (method == "POST")   m_server.Post(path.c_str(), h);
        else if (method == "PUT")    m_server.Put(path.c_str(), h);
        else if (method == "DELETE") m_server.Delete(path.c_str(), h);
    };

    // ── Auth endpoints ────────────────────────────────────────────────────
    publicRoute("GET", "/api/v1/auth/methods",
        [](const httplib::Request&, httplib::Response& res) {
            nlohmann::json j;
            j["methods"] = nlohmann::json::array({
                nlohmann::json{{"name", "hub32-key"}, {"uuid", "0c69b301-81b4-42d6-8fae-128cdd113314"}},
                nlohmann::json{{"name", "logon"}, {"uuid", "63611f7c-b457-42c7-832e-67d0f9281085"}}
            });
            res.status = 200;
            res.set_content(j.dump(), "application/json");
        });

    publicRoute("POST", "/api/v1/auth",
        [authCtrl](const httplib::Request& req, httplib::Response& res) {
            authCtrl->handleLogin(req, res);
        });

    protectedRoute("DELETE", "/api/v1/auth",
        [authCtrl](const httplib::Request& req, httplib::Response& res) {
            authCtrl->handleLogout(req, res);
        });

    // OPTIONS preflight
    m_server.Options("/api/v1/auth", [cors](const httplib::Request& req, httplib::Response& res) {
        cors->process(req, res);
    });
    m_server.Options(R"(/api/v1/.*)", [cors](const httplib::Request& req, httplib::Response& res) {
        cors->process(req, res);
    });

    // ── Computers ─────────────────────────────────────────────────────────
    protectedRoute("GET", "/api/v1/computers",
        [computerCtrl](const httplib::Request& req, httplib::Response& res) {
            computerCtrl->handleList(req, res);
        });

    protectedRoute("GET", R"(/api/v1/computers/([^/]+)/info)",
        [computerCtrl](const httplib::Request& req, httplib::Response& res) {
            computerCtrl->handleInfo(req, res);
        });

    protectedRoute("GET", R"(/api/v1/computers/([^/]+)/session)",
        [sessionCtrl](const httplib::Request& req, httplib::Response& res) {
            sessionCtrl->handleGetSession(req, res);
        });

    protectedRoute("GET", R"(/api/v1/computers/([^/]+)/user)",
        [sessionCtrl](const httplib::Request& req, httplib::Response& res) {
            sessionCtrl->handleGetUser(req, res);
        });

    protectedRoute("GET", R"(/api/v1/computers/([^/]+)/screens)",
        [sessionCtrl](const httplib::Request& req, httplib::Response& res) {
            sessionCtrl->handleGetScreens(req, res);
        });

    protectedRoute("GET", R"(/api/v1/computers/([^/]+)/framebuffer)",
        [fbCtrl](const httplib::Request& req, httplib::Response& res) {
            fbCtrl->handleGetFramebuffer(req, res);
        });

    // Features — must come before the bare /:id route to avoid prefix conflict
    protectedRoute("GET", R"(/api/v1/computers/([^/]+)/features/([^/]+))",
        [featureCtrl](const httplib::Request& req, httplib::Response& res) {
            featureCtrl->handleGetOne(req, res);
        });

    protectedRoute("PUT", R"(/api/v1/computers/([^/]+)/features/([^/]+))",
        [featureCtrl](const httplib::Request& req, httplib::Response& res) {
            featureCtrl->handleControl(req, res);
        });

    protectedRoute("GET", R"(/api/v1/computers/([^/]+)/features)",
        [featureCtrl](const httplib::Request& req, httplib::Response& res) {
            featureCtrl->handleList(req, res);
        });

    // Bare /computers/:id — must be registered last among computer routes
    protectedRoute("GET", R"(/api/v1/computers/([^/]+))",
        [computerCtrl](const httplib::Request& req, httplib::Response& res) {
            computerCtrl->handleGetOne(req, res);
        });

    spdlog::debug("[Router] v1 routes registered");
}

// ─────────────────────────────────────────────────────────────────────────────
// Agent routes
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief Registers all /api/v1/agents/ routes.
 *
 * Each route is wrapped with: CORS -> Logging -> RateLimit -> (Auth for protected).
 * The agent registration endpoint is public (authenticates via agentKey in body).
 */
void Router::registerAgentRoutes()
{
    // ── Controller ───────────────────────────────────────────────────────
    auto agentCtrl = std::make_shared<api::v1::AgentController>(
        m_svcs.agentRegistry, m_svcs.jwtAuth, m_svcs.agentKeyHash);

    // ── Middleware ────────────────────────────────────────────────────────
    const api::v1::middleware::CorsConfig corsConfig{
        {"*"},
        {"GET","POST","PUT","DELETE","OPTIONS"},
        {"Authorization","Content-Type","X-Request-ID","Accept"},
        false,
        3600
    };
    const api::v1::middleware::RateLimitConfig rlConfig{ 120, 20 };

    auto cors   = std::make_shared<api::v1::middleware::CorsMiddleware>(corsConfig);
    auto logger = std::make_shared<api::v1::middleware::LoggingMiddleware>();
    auto iv     = std::make_shared<api::v1::middleware::InputValidationMiddleware>();
    auto rl     = std::make_shared<api::v1::middleware::RateLimitMiddleware>(rlConfig);
    auto auth   = std::make_shared<api::v1::middleware::AuthMiddleware>(m_svcs.jwtAuth);

    // ── Route builders ────────────────────────────────────────────────────
    // Public: CORS + Logging + InputValidation + RateLimit (no JWT)
    auto publicRoute = [&](const std::string& method, const std::string& path, auto handler)
    {
        auto h = [cors, logger, iv, rl, handler]
            (const httplib::Request& req, httplib::Response& res)
        {
            logger->logRequest(req);
            if (!cors->process(req, res)) { logger->logResponse(req, res); return; }
            if (!iv->process(req, res))   { logger->logResponse(req, res); return; }
            if (!rl->process(req, res))   { logger->logResponse(req, res); return; }
            handler(req, res);
            logger->logResponse(req, res);
        };
        if (method == "GET")    m_server.Get(path.c_str(), h);
        else if (method == "POST")   m_server.Post(path.c_str(), h);
        else if (method == "DELETE") m_server.Delete(path.c_str(), h);
    };

    // Protected: CORS + Logging + InputValidation + RateLimit + JWT
    auto protectedRoute = [&](const std::string& method, const std::string& path, auto handler)
    {
        auto h = [cors, logger, iv, rl, auth, handler]
            (const httplib::Request& req, httplib::Response& res)
        {
            logger->logRequest(req);
            if (!cors->process(req, res)) { logger->logResponse(req, res); return; }
            if (!iv->process(req, res))   { logger->logResponse(req, res); return; }
            if (!rl->process(req, res))   { logger->logResponse(req, res); return; }
            core::internal::ApiContext ctx;
            ctx.requestId = req.has_header("X-Request-ID")
                ? req.get_header_value("X-Request-ID") : "";
            if (!auth->process(req, res, ctx)) { logger->logResponse(req, res); return; }
            handler(req, res);
            logger->logResponse(req, res);
        };
        if (method == "GET")    m_server.Get(path.c_str(), h);
        else if (method == "POST")   m_server.Post(path.c_str(), h);
        else if (method == "PUT")    m_server.Put(path.c_str(), h);
        else if (method == "DELETE") m_server.Delete(path.c_str(), h);
    };

    // ── Public: agent registration ───────────────────────────────────────
    publicRoute("POST", "/api/v1/agents/register",
        [agentCtrl](const httplib::Request& req, httplib::Response& res) {
            agentCtrl->handleRegister(req, res);
        });

    // ── Protected: all other agent endpoints ─────────────────────────────
    protectedRoute("GET", "/api/v1/agents",
        [agentCtrl](const httplib::Request& req, httplib::Response& res) {
            agentCtrl->handleList(req, res);
        });

    protectedRoute("DELETE", R"(/api/v1/agents/([^/]+))",
        [agentCtrl](const httplib::Request& req, httplib::Response& res) {
            agentCtrl->handleUnregister(req, res);
        });

    protectedRoute("GET", R"(/api/v1/agents/([^/]+)/status)",
        [agentCtrl](const httplib::Request& req, httplib::Response& res) {
            agentCtrl->handleStatus(req, res);
        });

    protectedRoute("POST", R"(/api/v1/agents/([^/]+)/commands)",
        [agentCtrl](const httplib::Request& req, httplib::Response& res) {
            agentCtrl->handlePushCommand(req, res);
        });

    protectedRoute("GET", R"(/api/v1/agents/([^/]+)/commands)",
        [agentCtrl](const httplib::Request& req, httplib::Response& res) {
            agentCtrl->handlePollCommands(req, res);
        });

    // PUT with 2 captures: /agents/{id}/commands/{cid}
    protectedRoute("PUT", R"(/api/v1/agents/([^/]+)/commands/([^/]+))",
        [agentCtrl](const httplib::Request& req, httplib::Response& res) {
            agentCtrl->handleReportResult(req, res);
        });

    protectedRoute("POST", R"(/api/v1/agents/([^/]+)/heartbeat)",
        [agentCtrl](const httplib::Request& req, httplib::Response& res) {
            agentCtrl->handleHeartbeat(req, res);
        });

    spdlog::debug("[Router] agent routes registered");
}

// ─────────────────────────────────────────────────────────────────────────────
// School routes
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief Registers all /api/v1/schools and /api/v1/locations routes.
 */
void Router::registerSchoolRoutes()
{
    if (!m_svcs.schoolRepo || !m_svcs.locationRepo || !m_svcs.computerRepo) {
        spdlog::warn("[Router] school repos not available — skipping school routes");
        return;
    }

    auto schoolCtrl = std::make_shared<api::v1::SchoolController>(
        *m_svcs.schoolRepo, *m_svcs.locationRepo, *m_svcs.computerRepo, m_svcs.jwtAuth);

    // ── Middleware ────────────────────────────────────────────────────────
    const api::v1::middleware::CorsConfig corsConfig{
        {"*"},
        {"GET","POST","PUT","DELETE","OPTIONS"},
        {"Authorization","Content-Type","X-Request-ID","Accept"},
        false,
        3600
    };
    const api::v1::middleware::RateLimitConfig rlConfig{ 120, 20 };

    auto cors   = std::make_shared<api::v1::middleware::CorsMiddleware>(corsConfig);
    auto logger = std::make_shared<api::v1::middleware::LoggingMiddleware>();
    auto iv     = std::make_shared<api::v1::middleware::InputValidationMiddleware>();
    auto rl     = std::make_shared<api::v1::middleware::RateLimitMiddleware>(rlConfig);
    auto auth   = std::make_shared<api::v1::middleware::AuthMiddleware>(m_svcs.jwtAuth);

    // Protected: CORS + Logging + InputValidation + RateLimit + JWT
    auto protectedRoute = [&](const std::string& method, const std::string& path, auto handler)
    {
        auto h = [cors, logger, iv, rl, auth, handler]
            (const httplib::Request& req, httplib::Response& res)
        {
            logger->logRequest(req);
            if (!cors->process(req, res)) { logger->logResponse(req, res); return; }
            if (!iv->process(req, res))   { logger->logResponse(req, res); return; }
            if (!rl->process(req, res))   { logger->logResponse(req, res); return; }
            core::internal::ApiContext ctx;
            ctx.requestId = req.has_header("X-Request-ID")
                ? req.get_header_value("X-Request-ID") : "";
            if (!auth->process(req, res, ctx)) { logger->logResponse(req, res); return; }
            handler(req, res);
            logger->logResponse(req, res);
        };
        if (method == "GET")    m_server.Get(path.c_str(), h);
        else if (method == "POST")   m_server.Post(path.c_str(), h);
        else if (method == "PUT")    m_server.Put(path.c_str(), h);
        else if (method == "DELETE") m_server.Delete(path.c_str(), h);
    };

    // ── Schools ──────────────────────────────────────────────────────────
    protectedRoute("POST", "/api/v1/schools",
        [schoolCtrl](const httplib::Request& req, httplib::Response& res) {
            schoolCtrl->handleCreateSchool(req, res);
        });

    protectedRoute("GET", "/api/v1/schools",
        [schoolCtrl](const httplib::Request& req, httplib::Response& res) {
            schoolCtrl->handleListSchools(req, res);
        });

    protectedRoute("GET", R"(/api/v1/schools/([^/]+))",
        [schoolCtrl](const httplib::Request& req, httplib::Response& res) {
            schoolCtrl->handleGetSchool(req, res);
        });

    protectedRoute("PUT", R"(/api/v1/schools/([^/]+))",
        [schoolCtrl](const httplib::Request& req, httplib::Response& res) {
            schoolCtrl->handleUpdateSchool(req, res);
        });

    protectedRoute("DELETE", R"(/api/v1/schools/([^/]+))",
        [schoolCtrl](const httplib::Request& req, httplib::Response& res) {
            schoolCtrl->handleDeleteSchool(req, res);
        });

    // ── Locations ────────────────────────────────────────────────────────
    protectedRoute("POST", "/api/v1/locations",
        [schoolCtrl](const httplib::Request& req, httplib::Response& res) {
            schoolCtrl->handleCreateLocation(req, res);
        });

    protectedRoute("GET", "/api/v1/locations",
        [schoolCtrl](const httplib::Request& req, httplib::Response& res) {
            schoolCtrl->handleListLocations(req, res);
        });

    // Computers-by-location must be registered BEFORE bare /:id
    protectedRoute("GET", R"(/api/v1/locations/([^/]+)/computers)",
        [schoolCtrl](const httplib::Request& req, httplib::Response& res) {
            schoolCtrl->handleListLocationComputers(req, res);
        });

    protectedRoute("GET", R"(/api/v1/locations/([^/]+))",
        [schoolCtrl](const httplib::Request& req, httplib::Response& res) {
            schoolCtrl->handleGetLocation(req, res);
        });

    protectedRoute("PUT", R"(/api/v1/locations/([^/]+))",
        [schoolCtrl](const httplib::Request& req, httplib::Response& res) {
            schoolCtrl->handleUpdateLocation(req, res);
        });

    protectedRoute("DELETE", R"(/api/v1/locations/([^/]+))",
        [schoolCtrl](const httplib::Request& req, httplib::Response& res) {
            schoolCtrl->handleDeleteLocation(req, res);
        });

    spdlog::debug("[Router] school + location routes registered");
}

// ─────────────────────────────────────────────────────────────────────────────
// Teacher routes
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief Registers all /api/v1/teachers routes.
 */
void Router::registerTeacherRoutes()
{
    if (!m_svcs.teacherRepo || !m_svcs.teacherLocationRepo || !m_svcs.locationRepo) {
        spdlog::warn("[Router] teacher repos not available — skipping teacher routes");
        return;
    }

    auto teacherCtrl = std::make_shared<api::v1::TeacherController>(
        *m_svcs.teacherRepo, *m_svcs.teacherLocationRepo, *m_svcs.locationRepo, m_svcs.jwtAuth);

    // ── Middleware ────────────────────────────────────────────────────────
    const api::v1::middleware::CorsConfig corsConfig{
        {"*"},
        {"GET","POST","PUT","DELETE","OPTIONS"},
        {"Authorization","Content-Type","X-Request-ID","Accept"},
        false,
        3600
    };
    const api::v1::middleware::RateLimitConfig rlConfig{ 120, 20 };

    auto cors   = std::make_shared<api::v1::middleware::CorsMiddleware>(corsConfig);
    auto logger = std::make_shared<api::v1::middleware::LoggingMiddleware>();
    auto iv     = std::make_shared<api::v1::middleware::InputValidationMiddleware>();
    auto rl     = std::make_shared<api::v1::middleware::RateLimitMiddleware>(rlConfig);
    auto auth   = std::make_shared<api::v1::middleware::AuthMiddleware>(m_svcs.jwtAuth);

    auto protectedRoute = [&](const std::string& method, const std::string& path, auto handler)
    {
        auto h = [cors, logger, iv, rl, auth, handler]
            (const httplib::Request& req, httplib::Response& res)
        {
            logger->logRequest(req);
            if (!cors->process(req, res)) { logger->logResponse(req, res); return; }
            if (!iv->process(req, res))   { logger->logResponse(req, res); return; }
            if (!rl->process(req, res))   { logger->logResponse(req, res); return; }
            core::internal::ApiContext ctx;
            ctx.requestId = req.has_header("X-Request-ID")
                ? req.get_header_value("X-Request-ID") : "";
            if (!auth->process(req, res, ctx)) { logger->logResponse(req, res); return; }
            handler(req, res);
            logger->logResponse(req, res);
        };
        if (method == "GET")    m_server.Get(path.c_str(), h);
        else if (method == "POST")   m_server.Post(path.c_str(), h);
        else if (method == "PUT")    m_server.Put(path.c_str(), h);
        else if (method == "DELETE") m_server.Delete(path.c_str(), h);
    };

    // ── CRUD ─────────────────────────────────────────────────────────────
    protectedRoute("POST", "/api/v1/teachers",
        [teacherCtrl](const httplib::Request& req, httplib::Response& res) {
            teacherCtrl->handleCreateTeacher(req, res);
        });

    protectedRoute("GET", "/api/v1/teachers",
        [teacherCtrl](const httplib::Request& req, httplib::Response& res) {
            teacherCtrl->handleListTeachers(req, res);
        });

    // Location assignment routes must be registered BEFORE bare /:id
    protectedRoute("POST", R"(/api/v1/teachers/([^/]+)/locations)",
        [teacherCtrl](const httplib::Request& req, httplib::Response& res) {
            teacherCtrl->handleAssignLocation(req, res);
        });

    protectedRoute("DELETE", R"(/api/v1/teachers/([^/]+)/locations/([^/]+))",
        [teacherCtrl](const httplib::Request& req, httplib::Response& res) {
            teacherCtrl->handleRevokeLocation(req, res);
        });

    protectedRoute("GET", R"(/api/v1/teachers/([^/]+))",
        [teacherCtrl](const httplib::Request& req, httplib::Response& res) {
            teacherCtrl->handleGetTeacher(req, res);
        });

    protectedRoute("PUT", R"(/api/v1/teachers/([^/]+))",
        [teacherCtrl](const httplib::Request& req, httplib::Response& res) {
            teacherCtrl->handleUpdateTeacher(req, res);
        });

    protectedRoute("DELETE", R"(/api/v1/teachers/([^/]+))",
        [teacherCtrl](const httplib::Request& req, httplib::Response& res) {
            teacherCtrl->handleDeleteTeacher(req, res);
        });

    spdlog::debug("[Router] teacher routes registered");
}

// ─────────────────────────────────────────────────────────────────────────────
// v2
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief Registers all /api/v2/ routes.
 */
void Router::registerV2()
{
    auto batchCtrl = std::make_shared<api::v2::BatchController>(m_svcs.registry);
    auto locCtrl   = std::make_shared<api::v2::LocationController>(m_svcs.registry);

    const api::v1::middleware::CorsConfig corsConfig{};
    const api::v1::middleware::RateLimitConfig rlConfig{};
    auto cors   = std::make_shared<api::v1::middleware::CorsMiddleware>(corsConfig);
    auto logger = std::make_shared<api::v1::middleware::LoggingMiddleware>();
    auto iv     = std::make_shared<api::v1::middleware::InputValidationMiddleware>();
    auto rl     = std::make_shared<api::v1::middleware::RateLimitMiddleware>(rlConfig);
    auto auth   = std::make_shared<api::v1::middleware::AuthMiddleware>(m_svcs.jwtAuth);

    auto protectedRoute = [&](const std::string& method, const std::string& path, auto handler)
    {
        auto h = [cors, logger, iv, rl, auth, handler]
            (const httplib::Request& req, httplib::Response& res)
        {
            logger->logRequest(req);
            if (!cors->process(req, res)) { logger->logResponse(req, res); return; }
            if (!iv->process(req, res))   { logger->logResponse(req, res); return; }
            if (!rl->process(req, res))   { logger->logResponse(req, res); return; }
            core::internal::ApiContext ctx;
            ctx.requestId = req.has_header("X-Request-ID")
                ? req.get_header_value("X-Request-ID") : "";
            if (!auth->process(req, res, ctx)) { logger->logResponse(req, res); return; }
            handler(req, res);
            logger->logResponse(req, res);
        };
        if (method == "GET")  m_server.Get(path.c_str(), h);
        else if (method == "POST") m_server.Post(path.c_str(), h);
    };

    // Locations
    protectedRoute("GET", "/api/v2/locations",
        [locCtrl](const httplib::Request& req, httplib::Response& res) {
            locCtrl->handleList(req, res);
        });

    protectedRoute("GET", R"(/api/v2/locations/([^/]+))",
        [locCtrl](const httplib::Request& req, httplib::Response& res) {
            locCtrl->handleGetOne(req, res);
        });

    // Batch feature control
    protectedRoute("POST", "/api/v2/batch/features",
        [batchCtrl](const httplib::Request& req, httplib::Response& res) {
            batchCtrl->handleBatchFeature(req, res);
        });

    // Computer host state check — exceeds original WebAPI capability
    protectedRoute("GET", R"(/api/v2/computers/([^/]+)/state)",
        [this](const httplib::Request& req, httplib::Response& res) {
            const std::string compId = req.matches.size() > 1 ? req.matches[1].str() : "";
            if (compId.empty()) { sendError(res, 400, "Missing computer id"); return; }

            // Try to resolve hostname via plugin; fall back to treating id as hostname
            std::string hostname = compId;
            auto* plugin = m_svcs.registry.computerPlugin();
            if (plugin) {
                auto info = plugin->getComputer(compId);
                if (info.is_ok()) hostname = info.value().hostname;
            }

            auto hostResult = checkHostState(hostname);
            nlohmann::json body;
            body["id"] = compId;
            body["hostname"] = hostname;
            body["state"] = hostResult.state;
            body["reachable"] = hostResult.reachable;
            body["latencyMs"] = hostResult.latencyMs;
            body["pingLatencyMs"] = hostResult.pingLatencyMs;
            res.status = 200;
            res.set_content(body.dump(), "application/json");
        });

    spdlog::debug("[Router] v2 routes registered");
}

// ─────────────────────────────────────────────────────────────────────────────
// Health + Metrics (unauthenticated)
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief Registers health and metrics endpoints. No authentication required.
 */
void Router::registerHealthAndMetrics()
{
    auto metricsCtrl = std::make_shared<api::v2::MetricsController>(
        m_svcs.registry, m_svcs.pool);
    auto logger = std::make_shared<api::v1::middleware::LoggingMiddleware>();

    auto wrap = [&](const char* path, auto handler) {
        m_server.Get(path, [logger, handler]
            (const httplib::Request& req, httplib::Response& res) {
            logger->logRequest(req);
            handler(req, res);
            logger->logResponse(req, res);
        });
    };

    wrap("/api/v2/health", [metricsCtrl](const httplib::Request& req, httplib::Response& res){
        metricsCtrl->handleHealth(req, res);
    });
    wrap("/health", [metricsCtrl](const httplib::Request& req, httplib::Response& res){
        metricsCtrl->handleHealth(req, res);
    });
    wrap("/api/v2/metrics", [metricsCtrl](const httplib::Request& req, httplib::Response& res){
        metricsCtrl->handleMetrics(req, res);
    });

    spdlog::debug("[Router] health/metrics routes registered");
}

// ─────────────────────────────────────────────────────────────────────────────
// OpenAPI spec (unauthenticated)
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief Registers GET /openapi.json — serves the OpenAPI 3.1 specification.
 *
 * The spec is built once at startup and cached as a static string.
 * Cache-Control: public, max-age=3600 allows proxies to cache it.
 */
void Router::registerOpenApi()
{
    // Built once; safe to store as static since this is called at startup
    static const std::string specJson = buildOpenApiSpec().dump(2);

    m_server.Get("/openapi.json", [](const httplib::Request&, httplib::Response& res) {
        res.status = 200;
        res.set_header("Cache-Control", "public, max-age=3600");
        res.set_content(specJson, "application/json");
    });

    spdlog::debug("[Router] OpenAPI 3.1 spec registered at /openapi.json");
}

// ─────────────────────────────────────────────────────────────────────────────
// SSE (Server-Sent Events) endpoints for real-time push
// ─────────────────────────────────────────────────────────────────────────────

void Router::registerSse()
{
    // GET /api/v1/events — SSE stream for real-time events
    m_server.Get("/api/v1/events", [this](const httplib::Request& /*req*/, httplib::Response& res) {
        // Generate unique client ID
        static std::atomic<int> clientCounter{0};
        const std::string clientId = "sse-" + std::to_string(++clientCounter);

        res.set_header("Cache-Control", "no-cache");
        res.set_header("Connection", "keep-alive");
        res.set_header("Access-Control-Allow-Origin", "*");

        // Use chunked content provider for SSE
        res.set_chunked_content_provider(
            "text/event-stream",
            [this, clientId](size_t /*offset*/, httplib::DataSink& sink) -> bool {
                // Send initial connected event
                std::string connectMsg = "event: connected\ndata: {\"clientId\":\"" + clientId + "\"}\n\n";
                sink.write(connectMsg.c_str(), connectMsg.size());

                // Register this client's sink function
                auto sinkFn = [&sink](const std::string& data) -> bool {
                    return sink.write(data.c_str(), data.size());
                };

                m_sse->subscribe("events", clientId, sinkFn);

                // Keep connection alive with heartbeat
                while (true) {
                    std::this_thread::sleep_for(std::chrono::seconds(15));
                    std::string heartbeat = ":heartbeat\n\n";
                    if (!sink.write(heartbeat.c_str(), heartbeat.size())) {
                        break;  // Client disconnected
                    }
                }

                m_sse->unsubscribe("events", clientId);
                return false;  // Close
            });
    });

    // GET /api/v1/computers/:id/screen/stream — SSE stream for screen updates
    m_server.Get(R"(/api/v1/computers/([^/]+)/screen/stream)",
        [this](const httplib::Request& req, httplib::Response& res) {
            const std::string compId = req.matches[1].str();
            static std::atomic<int> counter{0};
            const std::string clientId = "screen-" + std::to_string(++counter);
            const std::string channel = "screen:" + compId;

            res.set_header("Cache-Control", "no-cache");
            res.set_header("Connection", "keep-alive");
            res.set_header("Access-Control-Allow-Origin", "*");

            res.set_chunked_content_provider(
                "text/event-stream",
                [this, clientId, channel, compId](size_t /*offset*/, httplib::DataSink& sink) -> bool {
                    std::string connectMsg = "event: connected\ndata: {\"computer\":\"" + compId + "\"}\n\n";
                    sink.write(connectMsg.c_str(), connectMsg.size());

                    auto sinkFn = [&sink](const std::string& data) -> bool {
                        return sink.write(data.c_str(), data.size());
                    };
                    m_sse->subscribe(channel, clientId, sinkFn);

                    while (true) {
                        std::this_thread::sleep_for(std::chrono::seconds(15));
                        std::string heartbeat = ":heartbeat\n\n";
                        if (!sink.write(heartbeat.c_str(), heartbeat.size())) break;
                    }

                    m_sse->unsubscribe(channel, clientId);
                    return false;
                });
        });

    spdlog::debug("[Router] SSE streaming routes registered");
}

// ─────────────────────────────────────────────────────────────────────────────
// Debug endpoints (enabled via HUB32API_DEBUG env variable)
// ─────────────────────────────────────────────────────────────────────────────

void Router::registerDebug()
{
    // Only enable when HUB32API_DEBUG environment variable is set
    if (!std::getenv("HUB32API_DEBUG")) return;

    spdlog::info("[Router] debug endpoints enabled (HUB32API_DEBUG set)");

    m_server.Get("/api/v1/debug/info",
        [this](const httplib::Request& /*req*/, httplib::Response& res)
    {
        using Clock = std::chrono::steady_clock;
        static const auto startTime = Clock::now();
        const int uptimeSec = static_cast<int>(
            std::chrono::duration_cast<std::chrono::seconds>(
                Clock::now() - startTime).count());

        const auto plugins = m_svcs.registry.all();
        const int connCount = m_svcs.pool.activeCount();

        // Read MetricsPlugin counters
        int totalReqs = 0, failedReqs = 0;
        auto* metricsRaw = m_svcs.registry.find("a1b2c3d4-0004-0004-0004-000000000004");
        if (auto* mp = dynamic_cast<plugins::MetricsPlugin*>(metricsRaw)) {
            totalReqs = mp->totalRequests();
            failedReqs = mp->failedRequests();
        }

        std::ostringstream html;
        html << "<!DOCTYPE html>\n<html><head><title>Hub32 API Debug</title>"
             << "<style>body{font-family:monospace;margin:2em;}"
             << "table{border-collapse:collapse;} td,th{border:1px solid #ccc;padding:4px 8px;}"
             << "h1{color:#333;} h2{color:#666;margin-top:1.5em;}</style></head><body>\n"
             << "<h1>Hub32 API Debug Information</h1>\n"
             << "<h2>System</h2>\n"
             << "<ul>\n"
             << "<li><b>Server version:</b> 1.0.0</li>\n"
             << "<li><b>Uptime:</b> " << uptimeSec << " seconds</li>\n"
             << "<li><b>Bind address:</b> " << m_svcs.pool.activeCount() << " active connections</li>\n"
             << "</ul>\n"
             << "<h2>Statistics</h2>\n"
             << "<ul>\n"
             << "<li><b>Total requests:</b> " << totalReqs << "</li>\n"
             << "<li><b>Failed requests (5xx):</b> " << failedReqs << "</li>\n"
             << "<li><b>Active connections:</b> " << connCount << "</li>\n"
             << "</ul>\n"
             << "<h2>Loaded Plugins (" << plugins.size() << ")</h2>\n"
             << "<table><tr><th>UID</th><th>Name</th><th>Version</th><th>Description</th></tr>\n";
        for (auto* p : plugins) {
            html << "<tr><td>" << p->uid() << "</td><td>" << p->name()
                 << "</td><td>" << p->version() << "</td><td>" << p->description() << "</td></tr>\n";
        }
        html << "</table>\n</body></html>";

        res.status = 200;
        res.set_content(html.str(), "text/html");
    });
}

} // namespace hub32api::server::internal
