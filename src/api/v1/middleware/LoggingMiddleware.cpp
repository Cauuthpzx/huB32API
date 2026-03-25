#include "core/PrecompiledHeader.hpp"
#include "LoggingMiddleware.hpp"
#include <httplib.h>

namespace veyon32api::api::v1::middleware {

void LoggingMiddleware::logRequest(const httplib::Request& req)
{
    spdlog::info("[REQ] {} {} (X-Request-ID: {})",
        req.method, req.path,
        req.has_header("X-Request-ID") ? req.get_header_value("X-Request-ID") : "-");
}

void LoggingMiddleware::logResponse(const httplib::Request& req, const httplib::Response& res)
{
    spdlog::info("[RES] {} {} → {}", req.method, req.path, res.status);
}

} // namespace veyon32api::api::v1::middleware
