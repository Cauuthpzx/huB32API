#include "../core/PrecompiledHeader.hpp"
#include "HttpServer.hpp"
#include "internal/Router.hpp"
#include "internal/ThreadPool.hpp"
#include "../core/internal/Hub32CoreWrapper.hpp"
#include "../core/internal/PluginRegistry.hpp"
#include "../core/internal/ConnectionPool.hpp"
#include "../auth/JwtAuth.hpp"
#include "../auth/Hub32KeyAuth.hpp"
#include "../agent/AgentRegistry.hpp"
#include "../core/internal/I18n.hpp"
#include "../plugins/computer/ComputerPlugin.hpp"
#include "../plugins/feature/FeaturePlugin.hpp"
#include "../plugins/session/SessionPlugin.hpp"
#include "../plugins/metrics/MetricsPlugin.hpp"

#include <httplib.h>

namespace hub32api {

struct HttpServer::Impl
{
    ServerConfig                                    cfg;
    std::unique_ptr<core::internal::Hub32CoreWrapper> hub32Core;
    std::unique_ptr<core::internal::PluginRegistry>   registry;
    std::unique_ptr<core::internal::ConnectionPool>   pool;
    std::unique_ptr<auth::JwtAuth>                    jwtAuth;
    std::unique_ptr<auth::Hub32KeyAuth>               keyAuth;
    std::unique_ptr<agent::AgentRegistry>             agentRegistry;
    std::unique_ptr<server::internal::ThreadPool>     threadPool;
    std::unique_ptr<httplib::Server>                  httpServer;
    std::unique_ptr<server::internal::Router>         router;
    std::atomic<bool> running{false};
};

HttpServer::HttpServer(const ServerConfig& cfg)
    : m_impl(std::make_unique<Impl>())
{
    m_impl->cfg = cfg;

    // 1. Boot Hub32 core
    m_impl->hub32Core = std::make_unique<core::internal::Hub32CoreWrapper>(cfg);

    // 2. Init plugin registry and register built-in plugins
    m_impl->registry = std::make_unique<core::internal::PluginRegistry>();
    m_impl->registry->registerPlugin(std::make_unique<plugins::ComputerPlugin>(*m_impl->hub32Core));
    m_impl->registry->registerPlugin(std::make_unique<plugins::FeaturePlugin>(*m_impl->hub32Core));
    m_impl->registry->registerPlugin(std::make_unique<plugins::SessionPlugin>(*m_impl->hub32Core));
    m_impl->registry->registerPlugin(std::make_unique<plugins::MetricsPlugin>());
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
    m_impl->keyAuth = std::make_unique<auth::Hub32KeyAuth>();
    m_impl->agentRegistry = std::make_unique<agent::AgentRegistry>();

    // 4a. Wire AgentRegistry into plugins for live agent routing
    if (auto* compPlugin = m_impl->registry->computerPlugin()) {
        static_cast<plugins::ComputerPlugin*>(compPlugin)->setAgentRegistry(m_impl->agentRegistry.get());
    }
    if (auto* featPlugin = m_impl->registry->featurePlugin()) {
        static_cast<plugins::FeaturePlugin*>(featPlugin)->setAgentRegistry(m_impl->agentRegistry.get());
    }

    // 4b. Initialize i18n
    core::internal::I18n::init(cfg.localesDir, cfg.defaultLocale);

    // 5. HTTP/HTTPS server + router
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
    if (cfg.tlsEnabled && !cfg.tlsCertFile.empty() && !cfg.tlsKeyFile.empty()) {
        m_impl->httpServer = std::make_unique<httplib::SSLServer>(
            cfg.tlsCertFile.c_str(), cfg.tlsKeyFile.c_str());
        spdlog::info("[HttpServer] TLS enabled with cert={}", cfg.tlsCertFile);
    } else {
        m_impl->httpServer = std::make_unique<httplib::Server>();
    }
#else
    m_impl->httpServer = std::make_unique<httplib::Server>();
    if (cfg.tlsEnabled) {
        spdlog::warn("[HttpServer] TLS requested but OpenSSL support not compiled in");
    }
#endif
    m_impl->httpServer->new_task_queue = [&cfg] {
        return new httplib::ThreadPool(cfg.workerThreads);
    };

    server::internal::Router::Services svcs{
        *m_impl->registry, *m_impl->pool, *m_impl->jwtAuth, *m_impl->keyAuth,
        *m_impl->agentRegistry
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
    m_impl->running.store(true);
    spdlog::info("[HttpServer] listening on {}:{}", m_impl->cfg.bindAddress, m_impl->cfg.httpPort);

    // Blocks until stop() or error
    bool ok = m_impl->httpServer->listen(
        m_impl->cfg.bindAddress.c_str(),
        m_impl->cfg.httpPort);

    m_impl->running.store(false);
    return ok;
}

void HttpServer::stop()
{
    if (m_impl->running.load() && m_impl->httpServer) {
        m_impl->httpServer->stop();
    }
}

bool HttpServer::isRunning() const noexcept
{
    return m_impl->running.load();
}

} // namespace hub32api
