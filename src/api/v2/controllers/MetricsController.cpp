#include "core/PrecompiledHeader.hpp"
#include "MetricsController.hpp"
#include "../dto/MetricsDto.hpp"
#include "core/internal/PluginRegistry.hpp"
#include "core/internal/ConnectionPool.hpp"
#include <httplib.h>

namespace veyon32api::api::v2 {

MetricsController::MetricsController(
    core::internal::PluginRegistry& registry,
    core::internal::ConnectionPool& pool)
    : m_registry(registry), m_pool(pool) {}

void MetricsController::handleMetrics(const httplib::Request& req, httplib::Response& res)
{
    // TODO: build dto::MetricsDto from pool.activeCount(), registry.all().size() etc.
    // TODO: if Accept: text/plain → Prometheus exposition format
    // TODO: else JSON
    res.status = 501;
    res.set_content(R"({"error":"not implemented"})", "application/json");
}

void MetricsController::handleHealth(const httplib::Request&, httplib::Response& res)
{
    // TODO: check core initialized, plugin count > 0
    // TODO: 200 {"status":"ok"} or 503 {"status":"degraded","reason":"..."}
    res.status = 200;
    res.set_content(R"({"status":"ok"})", "application/json");
}

} // namespace veyon32api::api::v2
