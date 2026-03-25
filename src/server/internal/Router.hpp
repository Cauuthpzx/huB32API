#pragma once

#include <memory>

// Forward declarations
namespace httplib { class Server; }
namespace veyon32api::core::internal { class PluginRegistry; class ConnectionPool; }
namespace veyon32api::auth { class JwtAuth; class VeyonKeyAuth; }

namespace veyon32api::server::internal {

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
        auth::VeyonKeyAuth&             keyAuth;
    };

    explicit Router(httplib::Server& server, Services svcs);

    void registerAll();

private:
    void registerV1();
    void registerV2();
    void registerHealthAndMetrics();
    void registerOpenApi();

    httplib::Server& m_server;
    Services         m_svcs;
};

} // namespace veyon32api::server::internal
