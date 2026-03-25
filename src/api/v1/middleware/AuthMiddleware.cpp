/**
 * @file AuthMiddleware.cpp
 * @brief Full implementation of AuthMiddleware.
 *
 * Extracts the Bearer token from the Authorization header, delegates
 * validation to JwtAuth::authenticate(), and populates ApiContext::auth
 * on success.  Returns HTTP 401 Unauthorized on any failure.
 */

#include "core/PrecompiledHeader.hpp"
#include "AuthMiddleware.hpp"
#include "auth/JwtAuth.hpp"
#include "core/internal/ApiContext.hpp"
#include "core/internal/I18n.hpp"

#include <httplib.h>

namespace hub32api::api::v1::middleware {

namespace {

std::string getLocale(const httplib::Request& req) {
    auto* i = hub32api::core::internal::I18n::instance();
    if (!i) return "en";
    return i->negotiate(req.get_header_value("Accept-Language"));
}

/**
 * @brief Sends an RFC-7807 Problem Details JSON error response.
 *
 * Sets the HTTP status code and writes a JSON body with @c status,
 * @c title, and @c detail fields matching the project-wide error format.
 *
 * @param res     The httplib response object to populate.
 * @param status  HTTP status code (e.g. 401).
 * @param title   Short human-readable problem title.
 * @param detail  Longer explanation; falls back to @p title when empty.
 */
void sendError(httplib::Response& res,
               int                status,
               const std::string& title,
               const std::string& detail = {})
{
    nlohmann::json j;
    j["status"] = status;
    j["title"]  = title;
    j["detail"] = detail.empty() ? title : detail;
    res.status  = status;
    res.set_content(j.dump(), "application/json");
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

/**
 * @brief Constructs the AuthMiddleware with a reference to the JWT auth service.
 * @param jwtAuth  The JwtAuth instance used to validate incoming Bearer tokens.
 *                 Must outlive this middleware object.
 */
AuthMiddleware::AuthMiddleware(auth::JwtAuth& jwtAuth)
    : m_jwtAuth(jwtAuth) {}

// ---------------------------------------------------------------------------
// Public methods
// ---------------------------------------------------------------------------

/**
 * @brief Validates the Bearer token on an incoming HTTP request.
 *
 * Processing steps:
 *  1. Read the "Authorization" request header.
 *  2. Reject with 401 if the header is absent or does not start with
 *     "Bearer " (case-sensitive, per RFC 6750 §2.1).
 *  3. Call JwtAuth::authenticate() with the raw header value (the
 *     validator strips the prefix internally).
 *  4. Reject with 401 if authentication fails.
 *  5. Populate @p ctx.auth with the returned AuthContext and return true
 *     to allow the request to proceed to the controller.
 *
 * @param req  The incoming HTTP request to inspect.
 * @param res  The HTTP response to populate on failure.
 * @param ctx  The per-request context whose @c auth field is populated on
 *             success.
 * @return @c true if the request is authenticated and should proceed;
 *         @c false if the request has been rejected (res already set).
 */
bool AuthMiddleware::process(
    const httplib::Request&        req,
    httplib::Response&             res,
    core::internal::ApiContext&    ctx)
{
    using hub32api::core::internal::tr;
    const auto lang = getLocale(req);

    // ------------------------------------------------------------------
    // 1. Extract the Authorization header
    // ------------------------------------------------------------------
    const std::string authHeader = req.get_header_value("Authorization");

    if (authHeader.empty()) {
        spdlog::debug("[AuthMiddleware] missing Authorization header");
        sendError(res, 401, tr(lang, "error.unauthorized"),
                  tr(lang, "error.missing_auth_header"));
        return false;
    }

    // ------------------------------------------------------------------
    // 2. Verify the "Bearer " scheme prefix
    // ------------------------------------------------------------------
    if (authHeader.rfind("Bearer ", 0) != 0) {
        spdlog::debug("[AuthMiddleware] Authorization header does not use Bearer scheme");
        sendError(res, 401, tr(lang, "error.unauthorized"),
                  tr(lang, "error.bearer_scheme_required"));
        return false;
    }

    // ------------------------------------------------------------------
    // 3. Authenticate — JwtAuth::authenticate() strips the prefix itself
    // ------------------------------------------------------------------
    auto result = m_jwtAuth.authenticate(authHeader);

    if (result.is_err()) {
        spdlog::debug("[AuthMiddleware] authentication failed: {}",
                      result.error().message);
        sendError(res, 401, tr(lang, "error.unauthorized"), result.error().message);
        return false;
    }

    // ------------------------------------------------------------------
    // 4. Populate the per-request ApiContext and allow the request through
    // ------------------------------------------------------------------
    ctx.auth = result.take();

    spdlog::debug("[AuthMiddleware] authenticated: sub={}", ctx.auth.subject());
    return true;
}

} // namespace hub32api::api::v1::middleware
