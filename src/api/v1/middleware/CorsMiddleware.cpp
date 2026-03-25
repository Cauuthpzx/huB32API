#include "core/PrecompiledHeader.hpp"
#include "CorsMiddleware.hpp"
#include <httplib.h>

namespace veyon32api::api::v1::middleware {

CorsMiddleware::CorsMiddleware(CorsConfig cfg) : m_cfg(std::move(cfg)) {}

bool CorsMiddleware::process(const httplib::Request& req, httplib::Response& res)
{
    // TODO: set Access-Control-Allow-Origin from m_cfg.allowedOrigins
    // TODO: if OPTIONS preflight: set Access-Control-Allow-Methods/Headers, status 204, return false
    return true;
}

} // namespace veyon32api::api::v1::middleware
