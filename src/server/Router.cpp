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
#include "../api/v1/controllers/ComputerController.hpp"
#include "../api/v1/controllers/FeatureController.hpp"
#include "../api/v1/controllers/FramebufferController.hpp"
#include "../api/v1/controllers/SessionController.hpp"

// Controllers — v2
#include "../api/v2/controllers/BatchController.hpp"
#include "../api/v2/controllers/LocationController.hpp"
#include "../api/v2/controllers/MetricsController.hpp"

// Middleware
#include "../api/v1/middleware/AuthMiddleware.hpp"
#include "../api/v1/middleware/CorsMiddleware.hpp"
#include "../api/v1/middleware/LoggingMiddleware.hpp"
#include "../api/v1/middleware/RateLimitMiddleware.hpp"

// Core
#include "../core/internal/ApiContext.hpp"
#include "../core/internal/PluginRegistry.hpp"
#include "../core/internal/ConnectionPool.hpp"

// Auth
#include "../auth/JwtAuth.hpp"
#include "../auth/Hub32KeyAuth.hpp"

#include <httplib.h>

#ifdef _WIN32
#  include <winsock2.h>
#  include <ws2tcpip.h>
#endif

namespace hub32api::server::internal {

// ─────────────────────────────────────────────────────────────────────────────
// Internal helpers
// ─────────────────────────────────────────────────────────────────────────────

namespace {

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
    WSADATA wsa{};
    WSAStartup(MAKEWORD(2, 2), &wsa);

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
    : m_server(server), m_svcs(svcs) {}

/**
 * @brief Registers all routes. Call once before Server::listen().
 */
void Router::registerAll()
{
    registerV1();
    registerV2();
    registerHealthAndMetrics();
    registerOpenApi();
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
    auto authCtrl     = std::make_shared<api::v1::AuthController>(m_svcs.jwtAuth, m_svcs.keyAuth);
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
    auto rl     = std::make_shared<api::v1::middleware::RateLimitMiddleware>(rlConfig);
    auto auth   = std::make_shared<api::v1::middleware::AuthMiddleware>(m_svcs.jwtAuth);

    // ── Route builders ────────────────────────────────────────────────────
    // Public: CORS + Logging + RateLimit (no JWT)
    auto publicRoute = [&](const std::string& method, const std::string& path, auto handler)
    {
        auto h = [cors, logger, rl, handler]
            (const httplib::Request& req, httplib::Response& res)
        {
            logger->logRequest(req);
            if (!cors->process(req, res)) { logger->logResponse(req, res); return; }
            if (!rl->process(req, res))   { logger->logResponse(req, res); return; }
            handler(req, res);
            logger->logResponse(req, res);
        };
        if (method == "GET")    m_server.Get(path.c_str(), h);
        else if (method == "POST")   m_server.Post(path.c_str(), h);
        else if (method == "DELETE") m_server.Delete(path.c_str(), h);
        else if (method == "OPTIONS")m_server.Options(path.c_str(), h);
    };

    // Protected: CORS + Logging + RateLimit + JWT
    auto protectedRoute = [&](const std::string& method, const std::string& path, auto handler)
    {
        auto h = [cors, logger, rl, auth, handler]
            (const httplib::Request& req, httplib::Response& res)
        {
            logger->logRequest(req);
            if (!cors->process(req, res)) { logger->logResponse(req, res); return; }
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
            j["methods"] = nlohmann::json::array({"hub32-key", "logon"});
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
    auto rl     = std::make_shared<api::v1::middleware::RateLimitMiddleware>(rlConfig);
    auto auth   = std::make_shared<api::v1::middleware::AuthMiddleware>(m_svcs.jwtAuth);

    auto protectedRoute = [&](const std::string& method, const std::string& path, auto handler)
    {
        auto h = [cors, logger, rl, auth, handler]
            (const httplib::Request& req, httplib::Response& res)
        {
            logger->logRequest(req);
            if (!cors->process(req, res)) { logger->logResponse(req, res); return; }
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

    // Computer TCP state check — exceeds original WebAPI capability
    protectedRoute("GET", R"(/api/v2/computers/([^/]+)/state)",
        [this](const httplib::Request& req, httplib::Response& res) {
            const std::string id = req.matches.size() > 1 ? req.matches[1].str() : "";
            if (id.empty()) { sendError(res, 400, "Missing computer id"); return; }

            // Try to resolve hostname via plugin; fall back to treating id as hostname
            std::string hostname = id;
            auto* plugin = m_svcs.registry.computerPlugin();
            if (plugin) {
                auto info = plugin->getComputer(id);
                if (info.is_ok()) hostname = info.value().hostname;
            }

            const auto t0  = std::chrono::steady_clock::now();
            const bool online = tcpPing(hostname);
            const auto t1  = std::chrono::steady_clock::now();
            const int latencyMs = static_cast<int>(
                std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count());

            nlohmann::json j;
            j["id"]        = id;
            j["hostname"]  = hostname;
            j["online"]    = online;
            j["latencyMs"] = online ? latencyMs : -1;
            res.status = 200;
            res.set_content(j.dump(), "application/json");
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

} // namespace hub32api::server::internal
