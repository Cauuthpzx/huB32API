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
 *  v1 — Students (teacher/owner protected, except activate)
 *   POST   /api/v1/students                          (teacher/owner)
 *   GET    /api/v1/students           ?classId=xxx   (teacher/owner)
 *   POST   /api/v1/students/activate                 (student JWT — no teacher role)
 *   GET    /api/v1/students/:id                      (teacher/owner)
 *   PUT    /api/v1/students/:id                      (teacher/owner)
 *   DELETE /api/v1/students/:id                      (teacher/owner)
 *   POST   /api/v1/students/:id/reset-machine        (teacher/owner)
 *
 *  v1 — Classes (owner/teacher protected)
 *   POST   /api/v1/classes                          (owner only)
 *   GET    /api/v1/classes                          (owner: all in tenant; teacher: own classes)
 *   GET    /api/v1/classes/:id                      (owner/teacher)
 *   PUT    /api/v1/classes/:id                      (owner only)
 *   DELETE /api/v1/classes/:id                      (owner only)
 *
 *  v1 — Requests/Inbox (ticket system for password-change approval)
 *   POST   /api/v1/requests/change-password         (student or teacher)
 *   GET    /api/v1/requests/inbox                   (teacher or owner)
 *   GET    /api/v1/requests/outbox                  (student or teacher)
 *   POST   /api/v1/requests/:id/accept              (teacher or owner)
 *   POST   /api/v1/requests/:id/reject              (teacher or owner)
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
#include "../api/v1/controllers/RegisterController.hpp"
#include "../api/v1/controllers/SchoolController.hpp"
#include "../api/v1/controllers/StreamController.hpp"
#include "../api/v1/controllers/TeacherController.hpp"
#include "../api/v1/controllers/StudentController.hpp"
#include "../api/v1/controllers/ClassController.hpp"

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

// Media
#include "../media/SfuBackend.hpp"
#include "../media/RoomManager.hpp"

// Database repositories
#include "../db/SchoolRepository.hpp"
#include "../db/LocationRepository.hpp"
#include "../db/ComputerRepository.hpp"
#include "../db/TeacherRepository.hpp"
#include "../db/TeacherLocationRepository.hpp"
#include "../db/TenantRepository.hpp"
#include "../db/DatabaseManager.hpp"
#include "../db/StudentRepository.hpp"
#include "../db/PendingRequestRepository.hpp"
#include "../api/v1/controllers/RequestController.hpp"

// SSE
#include "SseManager.hpp"

#include "../plugins/metrics/MetricsPlugin.hpp"

#include "api/common/HttpErrorUtil.hpp"

#include <httplib.h>
#include <sstream>
#include <cstdlib>

#ifdef _WIN32
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  include <iphlpapi.h>
#  include <icmpapi.h>
#endif

using hub32api::api::common::sendError;

namespace hub32api::server::internal {

// ─────────────────────────────────────────────────────────────────────────────
// Internal helpers
// ─────────────────────────────────────────────────────────────────────────────

namespace {


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
    : m_server(server), m_svcs(svcs), m_sse(std::make_shared<server::SseManager>()),
      m_rl(std::make_shared<api::v1::middleware::RateLimitMiddleware>(
               api::v1::middleware::RateLimitConfig{
                   hub32api::kDefaultRequestsPerMinute,
                   hub32api::kDefaultBurstSize,
                   {{ "/api/v1/auth", hub32api::kAuthRateRequestsPerMinute }}
               })) {}

/**
 * @brief Registers all routes. Call once before Server::listen().
 */
void Router::registerAll()
{
    registerV1();
    registerAgentRoutes();
    registerSchoolRoutes();
    registerTeacherRoutes();
    registerStudentRoutes();
    registerClassRoutes();
    registerRequestRoutes();
    registerStreamRoutes();
    registerRegisterRoutes();
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
    if (!m_svcs.teacherRepo || !m_svcs.studentRepo) {
        spdlog::critical("[Router] teacherRepo or studentRepo not available — cannot start auth routes");
        return;
    }
    auto authCtrl     = std::make_shared<api::v1::AuthController>(
        m_svcs.jwtAuth, m_svcs.keyAuth, m_svcs.roleStore,
        *m_svcs.teacherRepo, *m_svcs.studentRepo);
    if (!m_svcs.computerRepo) {
        spdlog::warn("[Router] computerRepo not available — computer routes disabled");
        return;
    }
    auto computerCtrl = std::make_shared<api::v1::ComputerController>(*m_svcs.computerRepo);
    auto featureCtrl  = std::make_shared<api::v1::FeatureController>(m_svcs.registry);
    auto fbCtrl       = std::make_shared<api::v1::FramebufferController>(m_svcs.registry);
    auto sessionCtrl  = std::make_shared<api::v1::SessionController>(m_svcs.registry);

    // ── Middleware ────────────────────────────────────────────────────────
    // X-Tenant-ID is required for multi-tenant login (POST /api/v1/auth with method=logon)
    const api::v1::middleware::CorsConfig corsConfig{
        {"*"},
        {"GET","POST","PUT","DELETE","OPTIONS"},
        {"Authorization","Content-Type","X-Request-ID","Accept","X-Tenant-ID"},
        false,
        3600
    };

    auto cors   = std::make_shared<api::v1::middleware::CorsMiddleware>(corsConfig);
    auto logger = std::make_shared<api::v1::middleware::LoggingMiddleware>();
    auto iv     = std::make_shared<api::v1::middleware::InputValidationMiddleware>();
    auto rl     = m_rl;
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

    protectedRoute("GET", "/api/v1/auth/me",
        [this](const httplib::Request& req, httplib::Response& res) {
            // Extract subject (username) from validated JWT (AuthMiddleware already ran)
            const std::string authHeader = req.get_header_value("Authorization");
            if (authHeader.size() <= 7) {
                api::common::sendError(res, 401, "Unauthorized");
                return;
            }
            const std::string token = authHeader.substr(7);
            auto authResult = m_svcs.jwtAuth.authenticate(token);
            if (authResult.is_err() || !authResult.value().token) {
                api::common::sendError(res, 401, "Invalid token");
                return;
            }

            const auto& subject = authResult.value().token->subject;

            if (!m_svcs.teacherRepo) {
                api::common::sendError(res, 503, "Teacher repository not available");
                return;
            }

            // subject is a UUID for tenant teachers/owners; try findById first,
            // fall back to findByUsername for superadmin/admin (UserRoleStore users).
            auto teacher = m_svcs.teacherRepo->findById(subject);
            if (teacher.is_err()) {
                teacher = m_svcs.teacherRepo->findByUsername(subject);
            }
            if (teacher.is_err()) {
                // subject is a non-tenant user (admin/superadmin from UserRoleStore)
                const auto& ctx = req.get_header_value("Authorization");
                auto authCtx = m_svcs.jwtAuth.authenticate(ctx.substr(7));
                nlohmann::json j;
                j["id"]        = subject;
                j["username"]  = subject;
                j["fullName"]  = subject;
                j["role"]      = authCtx.is_ok() && authCtx.value().token
                                    ? authCtx.value().token->role : "unknown";
                j["createdAt"] = 0;
                res.status = 200;
                res.set_content(j.dump(), "application/json");
                return;
            }

            const auto& t = teacher.value();
            nlohmann::json j;
            j["id"]        = t.id;
            j["username"]  = t.username;
            j["fullName"]  = t.fullName;
            j["role"]      = t.role;
            j["createdAt"] = t.createdAt;
            res.status = 200;
            res.set_content(j.dump(), "application/json");
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

    // PUT /api/v1/computers/:id/features/:fid — location-based access control
    // Non-admin users (teachers) must have access to the computer's location.
    m_server.Put(R"(/api/v1/computers/([^/]+)/features/([^/]+))",
        [this, cors, logger, iv, rl, auth, featureCtrl]
        (const httplib::Request& req, httplib::Response& res)
        {
            using hub32api::core::internal::tr;
            const auto lang = [&req] {
                auto* i18n = hub32api::core::internal::I18n::instance();
                if (!i18n) return std::string("en");
                return i18n->negotiate(req.get_header_value("Accept-Language"));
            }();

            logger->logRequest(req);
            if (!cors->process(req, res)) { logger->logResponse(req, res); return; }
            if (!iv->process(req, res))   { logger->logResponse(req, res); return; }
            if (!rl->process(req, res))   { logger->logResponse(req, res); return; }

            core::internal::ApiContext ctx;
            ctx.requestId = req.has_header("X-Request-ID")
                ? req.get_header_value("X-Request-ID") : "";
            if (!auth->process(req, res, ctx)) { logger->logResponse(req, res); return; }

            // Role-based location access check (admin bypasses)
            if (ctx.auth.token && user_role_from_string(ctx.auth.token->role) != UserRole::Admin) {
                if (m_svcs.computerRepo && m_svcs.teacherLocationRepo && m_svcs.teacherRepo) {
                    const std::string computerId = req.matches.size() > 1
                        ? req.matches[1].str() : "";
                    auto computer = m_svcs.computerRepo->findById(computerId);
                    if (computer.is_ok() && !computer.value().locationId.empty()) {
                        auto teacher = m_svcs.teacherRepo->findByUsername(ctx.auth.token->subject);
                        if (teacher.is_ok()) {
                            auto accessResult = m_svcs.teacherLocationRepo->hasAccess(
                                    teacher.value().id, computer.value().locationId);
                            if (accessResult.is_err() || !accessResult.value()) {
                                sendError(res, 403, tr(lang, "error.forbidden"));
                                logger->logResponse(req, res);
                                return;
                            }
                        }
                    }
                }
            }

            featureCtrl->handleControl(req, res);
            logger->logResponse(req, res);
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
    agentCtrl->setComputerRepository(m_svcs.computerRepo);

    // ── Middleware ────────────────────────────────────────────────────────
    const api::v1::middleware::CorsConfig corsConfig{
        {"*"},
        {"GET","POST","PUT","DELETE","OPTIONS"},
        {"Authorization","Content-Type","X-Request-ID","Accept"},
        false,
        3600
    };

    auto cors   = std::make_shared<api::v1::middleware::CorsMiddleware>(corsConfig);
    auto logger = std::make_shared<api::v1::middleware::LoggingMiddleware>();
    auto iv     = std::make_shared<api::v1::middleware::InputValidationMiddleware>();
    auto rl     = m_rl;
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

    auto cors   = std::make_shared<api::v1::middleware::CorsMiddleware>(corsConfig);
    auto logger = std::make_shared<api::v1::middleware::LoggingMiddleware>();
    auto iv     = std::make_shared<api::v1::middleware::InputValidationMiddleware>();
    auto rl     = m_rl;
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

    auto cors   = std::make_shared<api::v1::middleware::CorsMiddleware>(corsConfig);
    auto logger = std::make_shared<api::v1::middleware::LoggingMiddleware>();
    auto iv     = std::make_shared<api::v1::middleware::InputValidationMiddleware>();
    auto rl     = m_rl;
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
// Student routes
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief Registers all /api/v1/students routes.
 *
 * Route registration order is critical:
 *   1. POST /api/v1/students/activate  — must be registered before /:id patterns
 *      so that "activate" is not captured as an :id path segment.
 *   2. POST /api/v1/students/:id/reset-machine — before bare /:id
 *   3. Bare /:id routes (GET, PUT, DELETE)
 *
 * The activate endpoint accepts a student Bearer token (role="student"), not
 * teacher/owner. All other endpoints require teacher or owner role.
 */
void Router::registerStudentRoutes()
{
    if (!m_svcs.studentRepo) {
        spdlog::warn("[Router] studentRepo not available — skipping student routes");
        return;
    }

    auto studentCtrl = std::make_shared<api::v1::StudentController>(
        *m_svcs.studentRepo, m_svcs.jwtAuth);

    // ── Middleware ────────────────────────────────────────────────────────
    const api::v1::middleware::CorsConfig corsConfig{
        {"*"},
        {"GET","POST","PUT","DELETE","OPTIONS"},
        {"Authorization","Content-Type","X-Request-ID","Accept"},
        false,
        3600
    };

    auto cors   = std::make_shared<api::v1::middleware::CorsMiddleware>(corsConfig);
    auto logger = std::make_shared<api::v1::middleware::LoggingMiddleware>();
    auto iv     = std::make_shared<api::v1::middleware::InputValidationMiddleware>();
    auto rl     = m_rl;
    auto auth   = std::make_shared<api::v1::middleware::AuthMiddleware>(m_svcs.jwtAuth);

    // Protected route: runs full middleware stack (including JWT auth)
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
        if (method == "GET")         m_server.Get(path.c_str(), h);
        else if (method == "POST")   m_server.Post(path.c_str(), h);
        else if (method == "PUT")    m_server.Put(path.c_str(), h);
        else if (method == "DELETE") m_server.Delete(path.c_str(), h);
    };

    // Semi-protected route: CORS + logging + rate-limit but NO JWT auth middleware.
    // Role checking is performed inside the controller handler (student token).
    auto semiProtectedRoute = [&](const std::string& method, const std::string& path, auto handler)
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
        if (method == "POST") m_server.Post(path.c_str(), h);
    };

    // ── IMPORTANT: Register /activate BEFORE /:id to prevent route shadowing ──
    semiProtectedRoute("POST", "/api/v1/students/activate",
        [studentCtrl](const httplib::Request& req, httplib::Response& res) {
            studentCtrl->handleActivateMachine(req, res);
        });

    // ── Collection routes ─────────────────────────────────────────────────
    protectedRoute("POST", "/api/v1/students",
        [studentCtrl](const httplib::Request& req, httplib::Response& res) {
            studentCtrl->handleCreate(req, res);
        });

    protectedRoute("GET", "/api/v1/students",
        [studentCtrl](const httplib::Request& req, httplib::Response& res) {
            studentCtrl->handleList(req, res);
        });

    // ── Sub-resource routes — must be registered BEFORE bare /:id ────────
    protectedRoute("POST", R"(/api/v1/students/([^/]+)/reset-machine)",
        [studentCtrl](const httplib::Request& req, httplib::Response& res) {
            studentCtrl->handleResetMachine(req, res);
        });

    // ── Single-resource routes ────────────────────────────────────────────
    protectedRoute("GET", R"(/api/v1/students/([^/]+))",
        [studentCtrl](const httplib::Request& req, httplib::Response& res) {
            studentCtrl->handleGet(req, res);
        });

    protectedRoute("PUT", R"(/api/v1/students/([^/]+))",
        [studentCtrl](const httplib::Request& req, httplib::Response& res) {
            studentCtrl->handleUpdate(req, res);
        });

    protectedRoute("DELETE", R"(/api/v1/students/([^/]+))",
        [studentCtrl](const httplib::Request& req, httplib::Response& res) {
            studentCtrl->handleDelete(req, res);
        });

    spdlog::debug("[Router] student routes registered");
}

// ─────────────────────────────────────────────────────────────────────────────
// Stream (WebRTC signaling) routes
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief Registers all /api/v1/stream/ routes for WebRTC signaling.
 *
 * Routes:
 *   POST   /api/v1/stream/transport                 → handleCreateTransport
 *   POST   /api/v1/stream/transport/:id/connect     → handleConnectTransport
 *   POST   /api/v1/stream/produce                   → handleProduce
 *   POST   /api/v1/stream/consume                   → handleConsume
 *   DELETE /api/v1/stream/transport/:id              → handleCloseTransport
 *   GET    /api/v1/stream/ice-servers               → handleGetIceServers
 *   GET    /api/v1/stream/capabilities/:locationId  → handleGetCapabilities
 */
void Router::registerStreamRoutes()
{
    if (!m_svcs.roomManager || !m_svcs.sfuBackend) {
        spdlog::warn("[Router] SFU backend not available — skipping stream routes");
        return;
    }

    auto streamCtrl = std::make_shared<api::v1::StreamController>(
        *m_svcs.roomManager, *m_svcs.sfuBackend,
        m_svcs.turnSecret, m_svcs.turnServerUrl);

    // ── Middleware ────────────────────────────────────────────────────────
    const api::v1::middleware::CorsConfig corsConfig{
        {"*"},
        {"GET","POST","DELETE","OPTIONS"},
        {"Authorization","Content-Type","X-Request-ID","Accept"},
        false,
        3600
    };

    auto cors   = std::make_shared<api::v1::middleware::CorsMiddleware>(corsConfig);
    auto logger = std::make_shared<api::v1::middleware::LoggingMiddleware>();
    auto iv     = std::make_shared<api::v1::middleware::InputValidationMiddleware>();
    auto rl     = m_rl;
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
        else if (method == "DELETE") m_server.Delete(path.c_str(), h);
    };

    // ── Transport ───────────────────────────────────────────────────────
    protectedRoute("POST", "/api/v1/stream/transport",
        [streamCtrl](const httplib::Request& req, httplib::Response& res) {
            streamCtrl->handleCreateTransport(req, res);
        });

    // connect must be registered BEFORE bare transport/:id
    protectedRoute("POST", R"(/api/v1/stream/transport/([^/]+)/connect)",
        [streamCtrl](const httplib::Request& req, httplib::Response& res) {
            streamCtrl->handleConnectTransport(req, res);
        });

    protectedRoute("DELETE", R"(/api/v1/stream/transport/([^/]+))",
        [streamCtrl](const httplib::Request& req, httplib::Response& res) {
            streamCtrl->handleCloseTransport(req, res);
        });

    // ── Produce / Consume ───────────────────────────────────────────────
    protectedRoute("POST", "/api/v1/stream/produce",
        [streamCtrl](const httplib::Request& req, httplib::Response& res) {
            streamCtrl->handleProduce(req, res);
        });

    protectedRoute("POST", "/api/v1/stream/consume",
        [streamCtrl](const httplib::Request& req, httplib::Response& res) {
            streamCtrl->handleConsume(req, res);
        });

    // ── ICE + Capabilities ──────────────────────────────────────────────
    protectedRoute("GET", "/api/v1/stream/ice-servers",
        [streamCtrl](const httplib::Request& req, httplib::Response& res) {
            streamCtrl->handleGetIceServers(req, res);
        });

    protectedRoute("GET", R"(/api/v1/stream/capabilities/([^/]+))",
        [streamCtrl](const httplib::Request& req, httplib::Response& res) {
            streamCtrl->handleGetCapabilities(req, res);
        });

    spdlog::debug("[Router] stream (WebRTC signaling) routes registered");
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
    if (!m_svcs.locationRepo || !m_svcs.computerRepo) {
        spdlog::warn("[Router] locationRepo/computerRepo not available — v2 location routes disabled");
        return;
    }
    auto locCtrl   = std::make_shared<api::v2::LocationController>(*m_svcs.locationRepo, *m_svcs.computerRepo);

    const api::v1::middleware::CorsConfig corsConfig{};
    auto cors   = std::make_shared<api::v1::middleware::CorsMiddleware>(corsConfig);
    auto logger = std::make_shared<api::v1::middleware::LoggingMiddleware>();
    auto iv     = std::make_shared<api::v1::middleware::InputValidationMiddleware>();
    auto rl     = m_rl;
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
    // AUTH: Validates JWT before establishing the SSE connection.
    m_server.Get("/api/v1/events", [this](const httplib::Request& req, httplib::Response& res) {
        // --- Validate JWT before allowing SSE subscription ---
        const std::string authHeader = req.get_header_value("Authorization");
        if (authHeader.empty() || authHeader.substr(0, kBearerPrefixLen) != kBearerPrefix) {
            res.status = 401;
            res.set_content(R"({"status":401,"title":"Unauthorized","detail":"SSE requires Bearer token"})",
                            "application/json");
            return;
        }
        auto authResult = m_svcs.jwtAuth.authenticate(authHeader.substr(kBearerPrefixLen));
        if (authResult.is_err()) {
            res.status = 401;
            res.set_content(R"({"status":401,"title":"Unauthorized","detail":"Invalid token"})",
                            "application/json");
            return;
        }

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

                // Use shared alive flag to prevent dangling sink reference
                auto alive = std::make_shared<std::atomic<bool>>(true);
                auto sinkFn = [&sink, alive](const std::string& data) -> bool {
                    if (!alive->load(std::memory_order_acquire)) return false;
                    return sink.write(data.c_str(), data.size());
                };

                m_sse->subscribe("events", clientId, sinkFn);

                // Keep connection alive with heartbeat
                while (true) {
                    std::this_thread::sleep_for(std::chrono::seconds(15));
                    std::string heartbeat = ":heartbeat\n\n";
                    if (!sink.write(heartbeat.c_str(), heartbeat.size())) {
                        alive->store(false, std::memory_order_release);
                        break;  // Client disconnected
                    }
                }

                m_sse->unsubscribe("events", clientId);
                return false;  // Close
            });
    });

    // GET /api/v1/computers/:id/screen/stream — SSE stream for screen updates
    // AUTH: Validates JWT before establishing the SSE connection.
    m_server.Get(R"(/api/v1/computers/([^/]+)/screen/stream)",
        [this](const httplib::Request& req, httplib::Response& res) {
            // --- Validate JWT ---
            const std::string authHeader = req.get_header_value("Authorization");
            if (authHeader.empty() || authHeader.substr(0, kBearerPrefixLen) != kBearerPrefix) {
                res.status = 401;
                res.set_content(R"({"status":401,"title":"Unauthorized","detail":"SSE requires Bearer token"})",
                                "application/json");
                return;
            }
            auto authResult = m_svcs.jwtAuth.authenticate(authHeader.substr(kBearerPrefixLen));
            if (authResult.is_err()) {
                res.status = 401;
                res.set_content(R"({"status":401,"title":"Unauthorized","detail":"Invalid token"})",
                                "application/json");
                return;
            }

            if (req.matches.size() <= 1) {
                res.status = 400;
                res.set_content(R"({"status":400,"title":"Bad Request","detail":"Missing computer ID"})",
                                "application/json");
                return;
            }
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

                    // Use shared alive flag to prevent dangling sink reference
                    auto alive = std::make_shared<std::atomic<bool>>(true);
                    auto sinkFn = [&sink, alive](const std::string& data) -> bool {
                        if (!alive->load(std::memory_order_acquire)) return false;
                        return sink.write(data.c_str(), data.size());
                    };
                    m_sse->subscribe(channel, clientId, sinkFn);

                    while (true) {
                        std::this_thread::sleep_for(std::chrono::seconds(15));
                        std::string heartbeat = ":heartbeat\n\n";
                        if (!sink.write(heartbeat.c_str(), heartbeat.size())) {
                            alive->store(false, std::memory_order_release);
                            break;
                        }
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

// ─────────────────────────────────────────────────────────────────────────────
// Registration routes (public — no JWT required)
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief Registers POST /api/v1/register and GET /api/v1/verify.
 *
 * Both routes are public. CORS, Logging, and RateLimit middleware are applied;
 * AuthMiddleware is intentionally omitted.
 */
void Router::registerRegisterRoutes()
{
    if (!m_svcs.tenantRepo || !m_svcs.teacherRepo || !m_svcs.dbManager) {
        spdlog::warn("[Router] TenantRepository/TeacherRepository/DatabaseManager not available"
                     " — registration routes disabled");
        return;
    }

    auto regCtrl = std::make_shared<api::v1::RegisterController>(
        *m_svcs.tenantRepo, *m_svcs.teacherRepo, *m_svcs.dbManager,
        m_svcs.emailService, m_svcs.appBaseUrl);

    const api::v1::middleware::CorsConfig corsConfig{
        {"*"},
        {"GET", "POST", "OPTIONS"},
        {"Content-Type", "X-Request-ID", "Accept"},
        false,
        3600
    };

    auto cors   = std::make_shared<api::v1::middleware::CorsMiddleware>(corsConfig);
    auto logger = std::make_shared<api::v1::middleware::LoggingMiddleware>();
    auto rl     = m_rl;

    // Public route: CORS + Logging + RateLimit (no JWT auth)
    auto publicRoute = [&](const std::string& method, const std::string& path, auto handler) {
        auto h = [cors, logger, rl, handler]
            (const httplib::Request& req, httplib::Response& res)
        {
            logger->logRequest(req);
            if (!cors->process(req, res)) { logger->logResponse(req, res); return; }
            if (!rl->process(req, res))   { logger->logResponse(req, res); return; }
            handler(req, res);
            logger->logResponse(req, res);
        };
        if (method == "POST") m_server.Post(path.c_str(), h);
        else if (method == "GET") m_server.Get(path.c_str(), h);
    };

    // Serve registration HTML form at GET /register
    m_server.Get("/register", [](const httplib::Request&, httplib::Response& res) {
        res.status = 200;
        res.set_content(R"html(<!DOCTYPE html>
<html lang="vi">
<head>
<meta charset="UTF-8"/>
<meta name="viewport" content="width=device-width,initial-scale=1"/>
<title>Dang ky Hub32</title>
<style>
*,*::before,*::after{box-sizing:border-box;margin:0;padding:0}
body{font-family:Arial,sans-serif;background:#f0f2f5;min-height:100vh;display:flex;align-items:center;justify-content:center;padding:24px 16px}
.card{background:#fff;border-radius:12px;box-shadow:0 1px 3px rgba(0,0,0,.1),0 8px 24px rgba(0,0,0,.06);width:100%;max-width:480px;overflow:hidden}
.accent{height:4px;background:linear-gradient(135deg,#1d4ed8,#2563eb)}
.body{padding:36px 40px 40px}
.brand{display:flex;align-items:center;gap:12px;margin-bottom:28px}
.icon{width:40px;height:40px;background:#1d4ed8;border-radius:8px;display:flex;align-items:center;justify-content:center;color:#fff;font-size:20px;font-weight:800;flex-shrink:0}
.bname{font-size:20px;font-weight:700;color:#0f172a}
h1{font-size:20px;font-weight:700;color:#0f172a;margin-bottom:6px}
.sub{font-size:14px;color:#64748b;margin-bottom:28px}
.field{margin-bottom:18px}
label{display:block;font-size:13px;font-weight:600;color:#374151;margin-bottom:6px}
input[type=text],input[type=email],input[type=password]{width:100%;padding:10px 12px;border:1px solid #d1d5db;border-radius:8px;font-size:14px;color:#0f172a;outline:none;transition:border-color .15s,box-shadow .15s}
input:focus{border-color:#2563eb;box-shadow:0 0 0 3px rgba(37,99,235,.12)}
input.err{border-color:#ef4444}
.ferr{font-size:12px;color:#ef4444;margin-top:4px;display:none}
.crow{display:flex;gap:10px;align-items:flex-end}
.crow input{flex:1}
.cbox{flex-shrink:0;padding:10px 18px;background:#f1f5f9;border:1px solid #e2e8f0;border-radius:8px;font-family:'Courier New',monospace;font-size:22px;font-weight:700;letter-spacing:6px;color:#1d4ed8;cursor:pointer;user-select:none;white-space:nowrap;transition:opacity .2s;min-width:120px;text-align:center}
.cbox:hover{opacity:.7}
.chint{font-size:11px;color:#94a3b8;margin-top:4px}
.btn{width:100%;padding:12px;background:#1d4ed8;color:#fff;border:none;border-radius:8px;font-size:15px;font-weight:600;cursor:pointer;margin-top:8px;transition:background .15s}
.btn:hover{background:#1e40af}
.btn:disabled{opacity:.6;cursor:not-allowed}
.alert{border-radius:8px;padding:12px 16px;font-size:14px;margin-bottom:20px;display:none;line-height:1.5}
.aerr{background:#fef2f2;border:1px solid #fecaca;color:#b91c1c}
.spin{display:inline-block;width:14px;height:14px;border:2px solid rgba(255,255,255,.4);border-top-color:#fff;border-radius:50%;animation:sp .6s linear infinite;vertical-align:middle;margin-right:6px}
@keyframes sp{to{transform:rotate(360deg)}}
.ok{text-align:center;padding:40px;display:none}
.okicon{width:64px;height:64px;background:#dcfce7;border-radius:50%;display:flex;align-items:center;justify-content:center;margin:0 auto 20px;font-size:28px}
.ok h2{font-size:20px;color:#0f172a;margin-bottom:10px}
.ok p{font-size:14px;color:#64748b;line-height:1.6}
</style>
</head>
<body>
<div class="card">
<div class="accent"></div>
<div class="body" id="F">
  <div class="brand"><div class="icon">H</div><span class="bname">Hub32</span></div>
  <h1>Dang ky to chuc</h1>
  <p class="sub">Tao tai khoan quan ly phong may cho truong cua ban</p>
  <div class="alert aerr" id="AE"></div>
  <form id="form" novalidate>
    <div class="field">
      <label>Ten to chuc / truong</label>
      <input type="text" id="orgName" placeholder="VD: Truong THPT Nguyen Hue" maxlength="100"/>
      <div class="ferr" id="e1">Vui long nhap ten to chuc</div>
    </div>
    <div class="field">
      <label>Ten dang nhap (username)</label>
      <input type="text" id="username" placeholder="VD: admin.truong" maxlength="50" autocomplete="username"/>
      <div class="ferr" id="e6">Chi dung chu, so, dau cham, gach duoi, gach ngang</div>
    </div>
    <div class="field">
      <label>Email quan tri vien</label>
      <input type="email" id="email" placeholder="admin@truong.edu.vn"/>
      <div class="ferr" id="e2">Email khong hop le</div>
    </div>
    <div class="field">
      <label>Mat khau</label>
      <input type="password" id="pw" placeholder="Toi thieu 8 ky tu"/>
      <div class="ferr" id="e3">Mat khau toi thieu 8 ky tu</div>
    </div>
    <div class="field">
      <label>Nhap lai mat khau</label>
      <input type="password" id="pw2" placeholder="Nhap lai mat khau"/>
      <div class="ferr" id="e4">Mat khau khong khop</div>
    </div>
    <div class="field">
      <label>Ma xac nhan (nhap 6 so hien thi ben phai)</label>
      <div class="crow">
        <input type="text" id="ca" placeholder="Nhap 6 so" maxlength="6" inputmode="numeric" autocomplete="off"/>
        <div class="cbox" id="cbox" title="Nhan de lam moi" onclick="lc()">......</div>
      </div>
      <div class="chint">Nhan vao day so de lam moi captcha</div>
      <div class="ferr" id="e5">Ma xac nhan khong dung</div>
    </div>
    <button type="submit" class="btn" id="btn">Dang ky</button>
  </form>
</div>
<div class="ok" id="OK">
  <div class="okicon">&#x2709;</div>
  <h2>Kiem tra email cua ban!</h2>
  <p>Chung toi da gui link kich hoat den<br/><strong id="EM"></strong><br/><br/>
  Vui long nhan vao link trong email de kich hoat tai khoan.<br/>Link co hieu luc trong <strong>24 gio</strong>.</p>
</div>
</div>
<script>
var cid='';
function lc(){
  var b=document.getElementById('cbox');
  b.style.opacity='.3';
  document.getElementById('ca').value='';
  fetch('/api/v1/captcha').then(function(r){return r.json();}).then(function(d){
    cid=d.captchaId;b.textContent=d.text;b.style.opacity='1';
  }).catch(function(){b.textContent='ERR';b.style.opacity='1';});
}
function se(id,eid,show){
  var i=document.getElementById(id),e=document.getElementById(eid);
  if(show){i.classList.add('err');e.style.display='block';}
  else{i.classList.remove('err');e.style.display='none';}
  return !show;
}
function ve(e){return /^[^\s@]+@[^\s@]+\.[^\s@]+$/.test(e);}
document.getElementById('form').addEventListener('submit',function(ev){
  ev.preventDefault();
  var org=document.getElementById('orgName').value.trim();
  var uname=document.getElementById('username').value.trim();
  var em=document.getElementById('email').value.trim();
  var pw=document.getElementById('pw').value;
  var pw2=document.getElementById('pw2').value;
  var cap=document.getElementById('ca').value.trim();
  var ok=true;
  ok=se('orgName','e1',org.length===0)&&ok;
  ok=se('username','e6',uname.length===0||!/^[a-zA-Z0-9._-]+$/.test(uname))&&ok;
  ok=se('email','e2',!ve(em))&&ok;
  ok=se('pw','e3',pw.length<8)&&ok;
  ok=se('pw2','e4',pw!==pw2)&&ok;
  ok=se('ca','e5',!/^\d{6}$/.test(cap))&&ok;
  if(!ok)return;
  var ae=document.getElementById('AE');ae.style.display='none';
  var btn=document.getElementById('btn');btn.disabled=true;
  btn.innerHTML='<span class="spin"></span>Dang xu ly...';
  fetch('/api/v1/register',{method:'POST',headers:{'Content-Type':'application/json'},
    body:JSON.stringify({orgName:org,username:uname,email:em,password:pw,captchaId:cid,captchaAnswer:cap})
  }).then(function(r){return r.json().then(function(d){return{ok:r.ok,d:d};});})
  .then(function(x){
    if(x.ok){
      document.getElementById('F').style.display='none';
      document.getElementById('OK').style.display='block';
      document.getElementById('EM').textContent=em;
    } else {
      var msg=x.d.detail||x.d.title||'Dang ky that bai.';
      ae.textContent=msg;ae.style.display='block';
      lc();btn.disabled=false;btn.textContent='Dang ky';
    }
  }).catch(function(){
    ae.textContent='Khong the ket noi den may chu.';ae.style.display='block';
    lc();btn.disabled=false;btn.textContent='Dang ky';
  });
});
lc();
</script>
</body>
</html>)html", "text/html; charset=utf-8");
    });

    publicRoute("GET", "/api/v1/captcha",
        [regCtrl](const httplib::Request& req, httplib::Response& res) {
            regCtrl->handleCaptcha(req, res);
        });

    publicRoute("POST", "/api/v1/register",
        [regCtrl](const httplib::Request& req, httplib::Response& res) {
            regCtrl->handleRegister(req, res);
        });

    publicRoute("GET", "/api/v1/verify",
        [regCtrl](const httplib::Request& req, httplib::Response& res) {
            regCtrl->handleVerify(req, res);
        });

    spdlog::debug("[Router] registration routes registered");
}

// ─────────────────────────────────────────────────────────────────────────────
// Class routes
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief Registers all /api/v1/classes routes.
 *
 *  POST   /api/v1/classes          — owner only   — create class
 *  GET    /api/v1/classes          — owner/teacher — list classes
 *  GET    /api/v1/classes/:id      — owner/teacher — get single class
 *  PUT    /api/v1/classes/:id      — owner only    — update class
 *  DELETE /api/v1/classes/:id      — owner only    — delete class
 */
void Router::registerClassRoutes()
{
    if (!m_svcs.classRepo || !m_svcs.teacherRepo) {
        spdlog::warn("[Router] classRepo or teacherRepo not available — skipping class routes");
        return;
    }

    auto classCtrl = std::make_shared<api::v1::ClassController>(
        *m_svcs.classRepo, *m_svcs.teacherRepo, m_svcs.jwtAuth);

    // ── Middleware ────────────────────────────────────────────────────────
    const api::v1::middleware::CorsConfig corsConfig{
        {"*"},
        {"GET", "POST", "PUT", "DELETE", "OPTIONS"},
        {"Authorization", "Content-Type", "X-Request-ID", "Accept"},
        false,
        3600
    };

    auto cors   = std::make_shared<api::v1::middleware::CorsMiddleware>(corsConfig);
    auto logger = std::make_shared<api::v1::middleware::LoggingMiddleware>();
    auto iv     = std::make_shared<api::v1::middleware::InputValidationMiddleware>();
    auto rl     = m_rl;
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
        if (method == "GET")         m_server.Get(path.c_str(), h);
        else if (method == "POST")   m_server.Post(path.c_str(), h);
        else if (method == "PUT")    m_server.Put(path.c_str(), h);
        else if (method == "DELETE") m_server.Delete(path.c_str(), h);
    };

    // ── Collection routes ─────────────────────────────────────────────────
    protectedRoute("POST", "/api/v1/classes",
        [classCtrl](const httplib::Request& req, httplib::Response& res) {
            classCtrl->handleCreate(req, res);
        });

    protectedRoute("GET", "/api/v1/classes",
        [classCtrl](const httplib::Request& req, httplib::Response& res) {
            classCtrl->handleList(req, res);
        });

    // ── Single-resource routes ────────────────────────────────────────────
    protectedRoute("GET", R"(/api/v1/classes/([^/]+))",
        [classCtrl](const httplib::Request& req, httplib::Response& res) {
            classCtrl->handleGet(req, res);
        });

    protectedRoute("PUT", R"(/api/v1/classes/([^/]+))",
        [classCtrl](const httplib::Request& req, httplib::Response& res) {
            classCtrl->handleUpdate(req, res);
        });

    protectedRoute("DELETE", R"(/api/v1/classes/([^/]+))",
        [classCtrl](const httplib::Request& req, httplib::Response& res) {
            classCtrl->handleDelete(req, res);
        });

    spdlog::debug("[Router] class routes registered");
}

// ---------------------------------------------------------------------------
// registerRequestRoutes
// ---------------------------------------------------------------------------

void Router::registerRequestRoutes()
{
    if (!m_svcs.requestRepo || !m_svcs.classRepo ||
        !m_svcs.studentRepo || !m_svcs.teacherRepo) {
        spdlog::warn("[Router] requestRepo or dependencies not available — skipping request routes");
        return;
    }

    auto reqCtrl = std::make_shared<api::v1::RequestController>(
        *m_svcs.requestRepo,
        *m_svcs.classRepo,
        *m_svcs.studentRepo,
        *m_svcs.teacherRepo,
        m_svcs.jwtAuth);

    const api::v1::middleware::CorsConfig corsConfig{
        {"*"},
        {"GET", "POST", "OPTIONS"},
        {"Authorization", "Content-Type", "X-Request-ID", "Accept"},
        false,
        3600
    };

    auto cors   = std::make_shared<api::v1::middleware::CorsMiddleware>(corsConfig);
    auto logger = std::make_shared<api::v1::middleware::LoggingMiddleware>();
    auto iv     = std::make_shared<api::v1::middleware::InputValidationMiddleware>();
    auto rl     = m_rl;
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
        if (method == "GET")       m_server.Get(path.c_str(), h);
        else if (method == "POST") m_server.Post(path.c_str(), h);
    };

    // Fixed-path routes first (before /:id patterns).
    protectedRoute("POST", "/api/v1/requests/change-password",
        [reqCtrl](const httplib::Request& req, httplib::Response& res) {
            reqCtrl->handleSubmitChangePassword(req, res);
        });

    protectedRoute("GET", "/api/v1/requests/inbox",
        [reqCtrl](const httplib::Request& req, httplib::Response& res) {
            reqCtrl->handleListInbox(req, res);
        });

    protectedRoute("GET", "/api/v1/requests/outbox",
        [reqCtrl](const httplib::Request& req, httplib::Response& res) {
            reqCtrl->handleListOutbox(req, res);
        });

    // Parameterised routes after fixed paths.
    protectedRoute("POST", R"(/api/v1/requests/([^/]+)/accept)",
        [reqCtrl](const httplib::Request& req, httplib::Response& res) {
            reqCtrl->handleAccept(req, res);
        });

    protectedRoute("POST", R"(/api/v1/requests/([^/]+)/reject)",
        [reqCtrl](const httplib::Request& req, httplib::Response& res) {
            reqCtrl->handleReject(req, res);
        });

    spdlog::debug("[Router] request (inbox/ticket) routes registered");
}

} // namespace hub32api::server::internal
