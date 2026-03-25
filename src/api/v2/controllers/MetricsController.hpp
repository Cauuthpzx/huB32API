#pragma once

namespace httplib { class Request; class Response; }
namespace hub32api::core::internal { class PluginRegistry; class ConnectionPool; }

namespace hub32api::api::v2 {

// GET /api/v2/metrics          → JSON or Prometheus text format
// GET /api/v2/health           → 200 OK or 503
class MetricsController
{
public:
    explicit MetricsController(
        core::internal::PluginRegistry& registry,
        core::internal::ConnectionPool& pool);

    void handleMetrics(const httplib::Request& req, httplib::Response& res);
    void handleHealth (const httplib::Request& req, httplib::Response& res);

private:
    core::internal::PluginRegistry& m_registry;
    core::internal::ConnectionPool& m_pool;
};

} // namespace hub32api::api::v2
