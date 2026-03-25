#include "core/PrecompiledHeader.hpp"
#include "CorsMiddleware.hpp"
#include <httplib.h>

namespace veyon32api::api::v1::middleware {

/**
 * @brief Constructs a CorsMiddleware with the given configuration.
 *
 * @param cfg CORS policy configuration (allowed origins, methods, headers, etc.).
 */
CorsMiddleware::CorsMiddleware(CorsConfig cfg) : m_cfg(std::move(cfg)) {}

/**
 * @brief Processes a request through the CORS middleware.
 *
 * Sets the appropriate CORS response headers based on the configured policy.
 * If the incoming request is an HTTP OPTIONS preflight, the method sets
 * `Access-Control-Allow-Methods`, `Access-Control-Allow-Headers`, and
 * `Access-Control-Max-Age`, responds with HTTP 204, and returns @c false to
 * stop further handler processing.  For all other methods it applies the
 * origin header and, when configured, the `Access-Control-Allow-Credentials`
 * header, then returns @c true to continue the handler chain.
 *
 * @param req  Incoming HTTP request.
 * @param res  Outgoing HTTP response (headers are mutated in-place).
 * @return @c false for OPTIONS preflight requests (response is complete);
 *         @c true for all other requests (processing should continue).
 */
bool CorsMiddleware::process(const httplib::Request& req, httplib::Response& res)
{
    // -----------------------------------------------------------------------
    // Determine the value for Access-Control-Allow-Origin
    // -----------------------------------------------------------------------
    std::string allowOrigin;

    if (m_cfg.allowedOrigins.size() == 1 && m_cfg.allowedOrigins[0] == "*") {
        // Wildcard — allow any origin
        allowOrigin = "*";
    } else {
        // Exact-match the request Origin header against the allow-list
        const std::string requestOrigin =
            req.has_header("Origin") ? req.get_header_value("Origin") : std::string{};

        for (const auto& allowed : m_cfg.allowedOrigins) {
            if (allowed == requestOrigin) {
                allowOrigin = requestOrigin;
                break;
            }
        }
        // If no match, allowOrigin stays empty; no header will be set below
        // (browser will block the cross-origin request — correct behaviour).
    }

    if (!allowOrigin.empty()) {
        res.set_header("Access-Control-Allow-Origin", allowOrigin);
        // Vary header ensures proxy caches key on Origin when not using wildcard
        if (allowOrigin != "*") {
            res.set_header("Vary", "Origin");
        }
    }

    // -----------------------------------------------------------------------
    // credentials flag (applies to all requests)
    // -----------------------------------------------------------------------
    if (m_cfg.allowCredentials) {
        res.set_header("Access-Control-Allow-Credentials", "true");
    }

    // -----------------------------------------------------------------------
    // OPTIONS preflight — respond immediately with 204 No Content
    // -----------------------------------------------------------------------
    if (req.method == "OPTIONS") {
        // Build comma-separated method list
        std::string methods;
        for (std::size_t i = 0; i < m_cfg.allowedMethods.size(); ++i) {
            if (i > 0) methods += ", ";
            methods += m_cfg.allowedMethods[i];
        }

        // Build comma-separated header list
        std::string headers;
        for (std::size_t i = 0; i < m_cfg.allowedHeaders.size(); ++i) {
            if (i > 0) headers += ", ";
            headers += m_cfg.allowedHeaders[i];
        }

        res.set_header("Access-Control-Allow-Methods", methods);
        res.set_header("Access-Control-Allow-Headers", headers);
        res.set_header("Access-Control-Max-Age",
                       std::to_string(m_cfg.maxAgeSec));

        res.status = 204;
        return false; // Stop processing; preflight is fully handled
    }

    return true; // Continue to the actual request handler
}

} // namespace veyon32api::api::v1::middleware
