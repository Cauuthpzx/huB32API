#include "../core/PrecompiledHeader.hpp"
#include "internal/Router.hpp"
#include "../api/v1/controllers/AuthController.hpp"
#include "../api/v1/controllers/ComputerController.hpp"
#include "../api/v1/controllers/FeatureController.hpp"
#include "../api/v1/controllers/FramebufferController.hpp"
#include "../api/v1/controllers/SessionController.hpp"
#include "../api/v1/middleware/AuthMiddleware.hpp"
#include "../api/v1/middleware/CorsMiddleware.hpp"
#include "../api/v1/middleware/LoggingMiddleware.hpp"
#include "../api/v1/middleware/RateLimitMiddleware.hpp"
#include "../api/v2/controllers/BatchController.hpp"
#include "../api/v2/controllers/LocationController.hpp"
#include "../api/v2/controllers/MetricsController.hpp"
#include <httplib.h>

namespace veyon32api::server::internal {

Router::Router(httplib::Server& server, Services svcs)
    : m_server(server), m_svcs(svcs) {}

void Router::registerAll()
{
    registerV1();
    registerV2();
    registerHealthAndMetrics();
    spdlog::info("[Router] all routes registered");
}

void Router::registerV1()
{
    // TODO: Instantiate controllers, bind routes via m_server.Get/Post/Put/Delete
    // Pattern: m_server.Get("/api/v1/computers", [...](auto& req, auto& res){ ctrl.handleList(req,res); });
    spdlog::debug("[Router] v1 routes registered (stubs)");
}

void Router::registerV2()
{
    // TODO: Bind v2 routes
    spdlog::debug("[Router] v2 routes registered (stubs)");
}

void Router::registerHealthAndMetrics()
{
    // GET /health and GET /api/v2/metrics
    spdlog::debug("[Router] health/metrics routes registered (stubs)");
}

} // namespace veyon32api::server::internal
