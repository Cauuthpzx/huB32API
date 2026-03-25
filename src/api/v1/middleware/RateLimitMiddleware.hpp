#pragma once

#include <string>
#include <mutex>
#include <unordered_map>
#include <chrono>

namespace httplib { class Request; class Response; }

namespace hub32api::api::v1::middleware {

struct RateLimitConfig {
    int requestsPerMinute = 120;
    int burstSize         = 20;
};

// Token-bucket rate limiter per client IP
class RateLimitMiddleware
{
public:
    explicit RateLimitMiddleware(RateLimitConfig cfg = RateLimitConfig{});

    // Returns false (and 429) if rate limit exceeded
    bool process(const httplib::Request& req, httplib::Response& res);

private:
    RateLimitConfig m_cfg;
    mutable std::mutex m_mutex;

    struct Bucket { int tokens = 0; std::chrono::steady_clock::time_point lastRefill; };
    std::unordered_map<std::string, Bucket> m_buckets;
};

} // namespace hub32api::api::v1::middleware
