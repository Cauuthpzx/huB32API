#include "core/PrecompiledHeader.hpp"
#include "RateLimitMiddleware.hpp"
#include <httplib.h>

namespace veyon32api::api::v1::middleware {

RateLimitMiddleware::RateLimitMiddleware(RateLimitConfig cfg) : m_cfg(cfg) {}

bool RateLimitMiddleware::process(const httplib::Request& req, httplib::Response& res)
{
    // TODO: implement token bucket algorithm per req.remote_addr
    // TODO: set X-RateLimit-Limit, X-RateLimit-Remaining, X-RateLimit-Reset headers
    // TODO: return false with 429 if bucket empty
    return true;
}

} // namespace veyon32api::api::v1::middleware
