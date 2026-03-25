#include "core/PrecompiledHeader.hpp"
#include "RateLimitMiddleware.hpp"
#include "core/internal/I18n.hpp"
#include <httplib.h>
#include <cmath>
#include <ctime>

namespace {

std::string getLocale(const httplib::Request& req) {
    auto* i = hub32api::core::internal::I18n::instance();
    if (!i) return "en";
    return i->negotiate(req.get_header_value("Accept-Language"));
}

} // anonymous namespace

namespace hub32api::api::v1::middleware {

/**
 * @brief Constructs a RateLimitMiddleware with the given configuration.
 *
 * @param cfg  Rate-limit policy (requests-per-minute and burst size).
 */
RateLimitMiddleware::RateLimitMiddleware(RateLimitConfig cfg) : m_cfg(cfg) {}

/**
 * @brief Processes a request through the token-bucket rate limiter.
 *
 * Each unique remote address maintains its own token bucket.  On every
 * request the bucket is first refilled proportionally to the time elapsed
 * since the last visit (up to the configured burst ceiling), then one token
 * is consumed.  The standard @c X-RateLimit-* response headers are always
 * set so clients can observe their current quota.
 *
 * If no token is available the method writes an RFC 7807-compatible JSON
 * body, sets HTTP status 429, appends a @c Retry-After header, and returns
 * @c false to stop further handler processing.
 *
 * @param req  Incoming HTTP request (uses @c remote_addr as the bucket key).
 * @param res  Outgoing HTTP response (headers and body are mutated on 429).
 * @return @c true if the request is within quota; @c false if rate-limited.
 */
bool RateLimitMiddleware::process(const httplib::Request& req, httplib::Response& res)
{
    using Clock     = std::chrono::steady_clock;
    using SysClock  = std::chrono::system_clock;
    using Seconds   = std::chrono::duration<double>;

    // Requests-per-second derived from the per-minute configuration
    const double ratePerSecond =
        static_cast<double>(m_cfg.requestsPerMinute) / 60.0;
    const double burstSize = static_cast<double>(m_cfg.burstSize);

    const std::string key = req.remote_addr;
    const auto now        = Clock::now();

    std::lock_guard<std::mutex> lock(m_mutex);

    // -----------------------------------------------------------------
    // Periodically prune stale buckets to prevent unbounded map growth.
    // Every 1000 requests, remove buckets that have not been accessed
    // in the last 10 minutes.
    // -----------------------------------------------------------------
    static int callCount = 0;
    if (++callCount % 1000 == 0) {
        const auto cutoff = Clock::now() - std::chrono::minutes(10);
        for (auto bit = m_buckets.begin(); bit != m_buckets.end(); ) {
            if (bit->second.lastRefill < cutoff) bit = m_buckets.erase(bit);
            else ++bit;
        }
    }

    auto it = m_buckets.find(key);
    if (it == m_buckets.end()) {
        // First request from this address — start with a full bucket
        Bucket b;
        b.tokens     = m_cfg.burstSize;
        b.lastRefill = now;
        it = m_buckets.emplace(key, b).first;
    }

    Bucket& bucket = it->second;

    // -----------------------------------------------------------------------
    // Refill tokens proportional to elapsed time
    // -----------------------------------------------------------------------
    const double elapsed =
        std::chrono::duration_cast<Seconds>(now - bucket.lastRefill).count();

    const double refilled =
        static_cast<double>(bucket.tokens) + ratePerSecond * elapsed;

    // Cap at burst size; update timestamp
    bucket.tokens    = static_cast<int>(std::min(refilled, burstSize));
    bucket.lastRefill = now;

    // -----------------------------------------------------------------------
    // Compute the reset timestamp (when the bucket will next be full again)
    // -----------------------------------------------------------------------
    const double secondsUntilFull =
        (burstSize - static_cast<double>(bucket.tokens)) / ratePerSecond;
    const auto resetTimepoint =
        SysClock::now() +
        std::chrono::duration_cast<SysClock::duration>(
            std::chrono::duration<double>(secondsUntilFull));
    const auto resetUnix =
        std::chrono::duration_cast<std::chrono::seconds>(
            resetTimepoint.time_since_epoch()).count();

    // -----------------------------------------------------------------------
    // Standard rate-limit headers (always set)
    // -----------------------------------------------------------------------
    res.set_header("X-RateLimit-Limit",
                   std::to_string(m_cfg.burstSize));
    res.set_header("X-RateLimit-Remaining",
                   std::to_string(static_cast<int>(std::floor(
                       static_cast<double>(bucket.tokens)))));
    res.set_header("X-RateLimit-Reset",
                   std::to_string(static_cast<long long>(resetUnix)));

    // -----------------------------------------------------------------------
    // Consume one token (or reject if the bucket is empty)
    // -----------------------------------------------------------------------
    if (bucket.tokens < 1) {
        using hub32api::core::internal::tr;
        const auto lang = getLocale(req);

        // Seconds until at least one token is available
        const double retryAfter =
            std::ceil((1.0 - static_cast<double>(bucket.tokens)) / ratePerSecond);

        res.set_header("Retry-After",
                       std::to_string(static_cast<int>(retryAfter)));
        res.status = 429;
        nlohmann::json body;
        body["status"] = 429;
        body["title"]  = tr(lang, "error.too_many_requests");
        body["detail"] = tr(lang, "error.rate_limit_exceeded");
        res.set_content(body.dump(), "application/json");
        return false;
    }

    --bucket.tokens;

    // Update the Remaining header now that a token was consumed
    res.set_header("X-RateLimit-Remaining",
                   std::to_string(bucket.tokens));

    return true;
}

} // namespace hub32api::api::v1::middleware
