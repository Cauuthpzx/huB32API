#include "../core/PrecompiledHeader.hpp"
#include "HttpServer.hpp"
#include "internal/Router.hpp"
#include "internal/ThreadPool.hpp"
#include "../core/internal/VeyonCoreWrapper.hpp"
#include "../core/internal/PluginRegistry.hpp"
#include "../core/internal/ConnectionPool.hpp"
#include "../auth/JwtAuth.hpp"
#include "../auth/VeyonKeyAuth.hpp"
#include "../plugins/computer/ComputerPlugin.hpp"
#include "../plugins/feature/FeaturePlugin.hpp"
#include "../plugins/session/SessionPlugin.hpp"

#include <httplib.h>

namespace veyon32api {

struct HttpServer::Impl
{
    ServerConfig                                    cfg;
    std::unique_ptr<core::internal::VeyonCoreWrapper> veyonCore;
    std::unique_ptr<core::internal::PluginRegistry>   registry;
    std::unique_ptr<core::internal::ConnectionPool>   pool;
    std::unique_ptr<auth::JwtAuth>                    jwtAuth;
    std::unique_ptr<auth::VeyonKeyAuth>               keyAuth;
    std::unique_ptr<server::internal::ThreadPool>     threadPool;
    std::unique_ptr<httplib::Server>                  httpServer;
    std::unique_ptr<server::internal::Router>         router;
    bool running = false;
};

HttpServer::HttpServer(const ServerConfig& cfg)
    : m_impl(std::make_unique<Impl>())
{
    m_impl->cfg = cfg;

    // 1. Boot Veyon core
    m_impl->veyonCore = std::make_unique<core::internal::VeyonCoreWrapper>(cfg);

    // 2. Init plugin registry and register built-in plugins
    m_impl->registry = std::make_unique<core::internal::PluginRegistry>();
    m_impl->registry->registerPlugin(std::make_unique<plugins::ComputerPlugin>(*m_impl->veyonCore));
    m_impl->registry->registerPlugin(std::make_unique<plugins::FeaturePlugin>(*m_impl->veyonCore));
    m_impl->registry->registerPlugin(std::make_unique<plugins::SessionPlugin>(*m_impl->veyonCore));
    m_impl->registry->initializeAll();

    // 3. Connection pool
    core::internal::ConnectionPool::Limits limits;
    limits.perHost     = cfg.connectionLimitPerHost;
    limits.global      = cfg.globalConnectionLimit;
    limits.lifetimeSec = cfg.connectionLifetimeSec;
    limits.idleSec     = cfg.connectionIdleTimeoutSec;
    m_impl->pool = std::make_unique<core::internal::ConnectionPool>(limits);

    // 4. Auth
    m_impl->jwtAuth = std::make_unique<auth::JwtAuth>(cfg);
    m_impl->keyAuth = std::make_unique<auth::VeyonKeyAuth>();

    // 5. HTTP server + router
    m_impl->httpServer = std::make_unique<httplib::Server>();
    m_impl->httpServer->new_task_queue = [&cfg] {
        return new httplib::ThreadPool(cfg.workerThreads);
    };

    server::internal::Router::Services svcs{
        *m_impl->registry, *m_impl->pool, *m_impl->jwtAuth, *m_impl->keyAuth
    };
    m_impl->router = std::make_unique<server::internal::Router>(*m_impl->httpServer, svcs);
    m_impl->router->registerAll();

    spdlog::info("[HttpServer] initialized on {}:{}", cfg.bindAddress, cfg.httpPort);
}

HttpServer::~HttpServer()
{
    stop();
    if (m_impl->registry)
        m_impl->registry->shutdownAll();
}

bool HttpServer::start()
{
    m_impl->running = true;
    spdlog::info("[HttpServer] listening on {}:{}", m_impl->cfg.bindAddress, m_impl->cfg.httpPort);

    // Blocks until stop() or error
    bool ok = m_impl->httpServer->listen(
        m_impl->cfg.bindAddress.c_str(),
        m_impl->cfg.httpPort);

    m_impl->running = false;
    return ok;
}

void HttpServer::stop()
{
    if (m_impl->running && m_impl->httpServer) {
        m_impl->httpServer->stop();
    }
}

bool HttpServer::isRunning() const noexcept
{
    return m_impl->running;
}

} // namespace veyon32api
