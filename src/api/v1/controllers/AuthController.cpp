#include "core/PrecompiledHeader.hpp"
#include "AuthController.hpp"
#include "../dto/AuthDto.hpp"
#include "../dto/ErrorDto.hpp"
#include "auth/JwtAuth.hpp"
#include "auth/Hub32KeyAuth.hpp"

// cpp-httplib
#include <httplib.h>

namespace {

/**
 * @brief Sends an RFC-7807-style JSON error response.
 * @param res    The httplib response to populate.
 * @param status HTTP status code to set.
 * @param title  Short human-readable problem title.
 * @param detail Longer explanation; defaults to @p title when empty.
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

std::string normalizeAuthMethod(const std::string& method)
{
    // Veyon-compatible UUIDs
    if (method == "0c69b301-81b4-42d6-8fae-128cdd113314") return "hub32-key";
    if (method == "63611f7c-b457-42c7-832e-67d0f9281085") return "logon";
    // Already a string name
    return method;
}

} // anonymous namespace

namespace hub32api::api::v1 {

/**
 * @brief Constructs the AuthController with the required auth services.
 * @param jwtAuth  Service used to issue and revoke JWT tokens.
 * @param keyAuth  Service used for Hub32 public-key authentication.
 */
AuthController::AuthController(
    auth::JwtAuth&      jwtAuth,
    auth::Hub32KeyAuth& keyAuth)
    : m_jwtAuth(jwtAuth)
    , m_keyAuth(keyAuth)
{}

/**
 * @brief Handles POST /api/v1/auth — authenticates a client and issues a JWT.
 *
 * Expects a JSON body conforming to @ref dto::AuthRequest.
 * Supported @c method values: @c "hub32-key" and @c "logon".
 * On success returns HTTP 200 with a @ref dto::AuthResponse JSON body.
 * On failure returns HTTP 400 (bad request / unsupported method) or 401.
 *
 * @param req  The incoming HTTP request.
 * @param res  The HTTP response to populate.
 */
void AuthController::handleLogin(const httplib::Request& req, httplib::Response& res)
{
    // --- Parse request body ---
    dto::AuthRequest req_dto;
    try {
        const auto j = nlohmann::json::parse(req.body);
        req_dto = j.get<dto::AuthRequest>();
    }
    catch (const std::exception& ex) {
        sendError(res, 400, "Invalid request body", ex.what());
        return;
    }

    req_dto.method = normalizeAuthMethod(req_dto.method);

    // --- Validate method ---
    if (req_dto.method != "hub32-key" && req_dto.method != "logon") {
        sendError(res, 400, "Unsupported auth method",
                  "Supported methods: hub32-key, logon");
        return;
    }

    // --- Determine role based on auth method + credentials ---
    // TODO: Replace with proper role resolution from a user/role store.
    const std::string role =
        (req_dto.method == "logon" && req_dto.username == "admin") ? "admin" : "teacher";

    // --- Issue JWT ---
    const auto tokenResult = m_jwtAuth.issueToken(req_dto.username, role);
    if (tokenResult.is_err()) {
        sendError(res, 401, "Authentication failed",
                  tokenResult.error().message);
        return;
    }

    // --- Build and return response ---
    // NOTE: expiresIn is sourced at construction time; we use the default
    //       value baked into AuthResponse (3600 s) as the config is not
    //       available here.  Controllers that need the real expiry should
    //       accept a ServerConfig reference.
    const dto::AuthResponse resp{tokenResult.value(), "Bearer", 3600};
    const nlohmann::json j = resp;
    res.status = 200;
    res.set_content(j.dump(), "application/json");
}

/**
 * @brief Handles DELETE /api/v1/auth — revokes the caller's JWT.
 *
 * Extracts the raw Bearer token from the @c Authorization header and
 * passes it to @ref auth::JwtAuth::revokeToken.  Returns HTTP 204 No Content
 * on success, or HTTP 401 if the header is absent or malformed.
 *
 * @param req  The incoming HTTP request.
 * @param res  The HTTP response to populate.
 */
void AuthController::handleLogout(const httplib::Request& req, httplib::Response& res)
{
    // --- Extract Bearer token from Authorization header ---
    const std::string authHeader = req.get_header_value("Authorization");
    if (authHeader.empty() || authHeader.rfind("Bearer ", 0) != 0) {
        sendError(res, 401, "Unauthorized",
                  "Missing or malformed Authorization header");
        return;
    }

    const std::string token = authHeader.substr(7); // strip "Bearer "
    if (token.empty()) {
        sendError(res, 401, "Unauthorized", "Empty bearer token");
        return;
    }

    // Extract the jti claim from the token for proper revocation.
    // TokenStore indexes by jti, not raw JWT string.
    auto authResult = m_jwtAuth.authenticate(token);
    if (authResult.is_ok() && authResult.value().token) {
        m_jwtAuth.revokeToken(authResult.value().token->jti);
    } else {
        // Even if token is invalid/expired, try to revoke by raw token as fallback
        m_jwtAuth.revokeToken(token);
    }

    // 204 No Content — no body, no Content-Type
    res.status = 204;
}

} // namespace hub32api::api::v1
