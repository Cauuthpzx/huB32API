#include "core/PrecompiledHeader.hpp"
#include "MetricsController.hpp"
#include "../dto/MetricsDto.hpp"
#include "core/internal/PluginRegistry.hpp"
#include "core/internal/ConnectionPool.hpp"
#include "core/internal/I18n.hpp"
#include "plugins/metrics/MetricsPlugin.hpp"
#include <httplib.h>

namespace hub32api::api::v2 {

namespace {

std::string getLocale(const httplib::Request& req) {
    auto* i = hub32api::core::internal::I18n::instance();
    if (!i) return "en";
    return i->negotiate(req.get_header_value("Accept-Language"));
}

/// @brief Returns the steady-clock time point when the process started.
/// Initialised once on first call and reused thereafter.
std::chrono::steady_clock::time_point processStartTime()
{
    static const auto s_start = std::chrono::steady_clock::now();
    return s_start;
}

} // anonymous namespace

/**
 * @brief Constructs a MetricsController.
 *
 * @param registry  Plugin registry used to count loaded plugins.
 * @param pool      Connection pool used to read the active-connection count.
 */
MetricsController::MetricsController(
    core::internal::PluginRegistry& registry,
    core::internal::ConnectionPool& pool)
    : m_registry(registry), m_pool(pool)
{
    // Touch the start-time sentinel at construction so that uptime counting
    // begins as early as possible (not deferred to the first metrics request).
    (void)processStartTime();
}

/**
 * @brief Handles GET /api/v2/health.
 *
 * Returns HTTP 200 with a JSON body `{"status":"ok","version":"1.0.0",
 * "plugins":<count>}` when at least one plugin is loaded, or HTTP 503 with
 * `{"status":"degraded","reason":"no plugins loaded"}` otherwise.
 *
 * @param req  Incoming HTTP request (unused).
 * @param res  Outgoing HTTP response.
 */
void MetricsController::handleHealth(const httplib::Request& req, httplib::Response& res)
{
    using hub32api::core::internal::tr;
    const auto lang = getLocale(req);

    const std::size_t pluginCount = m_registry.all().size();
    const bool healthy = (pluginCount > 0);

    if (healthy) {
        nlohmann::json body;
        body["status"]  = tr(lang, "health.status_ok");
        body["version"] = "1.0.0";
        body["plugins"] = static_cast<int>(pluginCount);
        res.status = 200;
        res.set_content(body.dump(), "application/json");
    } else {
        nlohmann::json body;
        body["status"] = tr(lang, "health.status_degraded");
        body["reason"] = tr(lang, "health.no_plugins");
        res.status = 503;
        res.set_content(body.dump(), "application/json");
    }
}

/**
 * @brief Handles GET /api/v2/metrics.
 *
 * Inspects the `Accept` request header to decide the output format:
 * - If the header contains `text/plain`, the response uses the Prometheus
 *   text exposition format (Content-Type: text/plain; version=0.0.4).
 * - Otherwise a JSON object (`dto::MetricsDto`) is returned.
 *
 * Metrics exposed: active connections, plugin count, uptime in seconds,
 * server version, and placeholder Hub32 version.
 *
 * @param req  Incoming HTTP request (inspected for `Accept` header).
 * @param res  Outgoing HTTP response.
 */
void MetricsController::handleMetrics(const httplib::Request& req, httplib::Response& res)
{
    using Clock = std::chrono::steady_clock;

    // Compute uptime
    const auto elapsed = Clock::now() - processStartTime();
    const int uptimeSec =
        static_cast<int>(
            std::chrono::duration_cast<std::chrono::seconds>(elapsed).count());

    // Populate the DTO
    dto::MetricsDto dto;
    dto.activeConnections = m_pool.activeCount();
    dto.pluginCount       = static_cast<int>(m_registry.all().size());
    dto.uptimeSeconds     = uptimeSec;
    dto.serverVersion     = "1.0.0";
    dto.hub32Version      = "4.x";  // populated from Hub32 core at runtime
    auto* metricsRaw = m_registry.find("a1b2c3d4-0004-0004-0004-000000000004");
    if (auto* mp = dynamic_cast<plugins::MetricsPlugin*>(metricsRaw)) {
        dto.totalRequests  = mp->totalRequests();
        dto.failedRequests = mp->failedRequests();
    } else {
        dto.totalRequests  = 0;
        dto.failedRequests = 0;
    }

    // Check whether the client prefers plain text (Prometheus scraper)
    const bool wantsPrometheus =
        req.has_header("Accept") &&
        req.get_header_value("Accept").find("text/plain") != std::string::npos;

    if (wantsPrometheus) {
        // -----------------------------------------------------------------------
        // Prometheus text exposition format (version 0.0.4)
        // -----------------------------------------------------------------------
        std::string body;
        body.reserve(512);

        body += "# HELP hub32api_active_connections Number of currently active VNC connections\n";
        body += "# TYPE hub32api_active_connections gauge\n";
        body += "hub32api_active_connections " +
                std::to_string(dto.activeConnections) + "\n";

        body += "# HELP hub32api_plugin_count Number of loaded plugins\n";
        body += "# TYPE hub32api_plugin_count gauge\n";
        body += "hub32api_plugin_count " +
                std::to_string(dto.pluginCount) + "\n";

        body += "# HELP hub32api_uptime_seconds Total process uptime in seconds\n";
        body += "# TYPE hub32api_uptime_seconds counter\n";
        body += "hub32api_uptime_seconds " +
                std::to_string(dto.uptimeSeconds) + "\n";

        body += "# HELP hub32api_total_requests Total number of HTTP requests handled\n";
        body += "# TYPE hub32api_total_requests counter\n";
        body += "hub32api_total_requests " +
                std::to_string(dto.totalRequests) + "\n";

        body += "# HELP hub32api_failed_requests Total number of failed HTTP requests\n";
        body += "# TYPE hub32api_failed_requests counter\n";
        body += "hub32api_failed_requests " +
                std::to_string(dto.failedRequests) + "\n";

        res.status = 200;
        res.set_content(body, "text/plain; version=0.0.4; charset=utf-8");
    } else {
        // -----------------------------------------------------------------------
        // JSON format
        // -----------------------------------------------------------------------
        nlohmann::json body = dto;
        res.status = 200;
        res.set_content(body.dump(), "application/json");
    }
}

} // namespace hub32api::api::v2
