#pragma once

#include <memory>

// Forward declarations
namespace httplib { class Server; }
namespace hub32api::core::internal { class PluginRegistry; class ConnectionPool; }
namespace hub32api::auth { class JwtAuth; class Hub32KeyAuth; }
namespace hub32api::agent { class AgentRegistry; }
namespace hub32api::server { class SseManager; }

namespace hub32api::server::internal {

// -----------------------------------------------------------------------
// Router — registers all API routes on the httplib::Server instance.
// Single entry point: call registerAll() once at startup.
// -----------------------------------------------------------------------
class Router
{
public:
    struct Services
    {
        core::internal::PluginRegistry& registry;
        core::internal::ConnectionPool& pool;
        auth::JwtAuth&                  jwtAuth;
        auth::Hub32KeyAuth&             keyAuth;
        agent::AgentRegistry&           agentRegistry;
    };

    explicit Router(httplib::Server& server, Services svcs);

    void registerAll();

private:
    void registerV1();
    void registerV2();
    void registerHealthAndMetrics();
    void registerOpenApi();
    void registerAgentRoutes();
    void registerSse();
    void registerDebug();

    httplib::Server& m_server;
    Services         m_svcs;
    std::shared_ptr<server::SseManager> m_sse;
};

} // namespace hub32api::server::internal
