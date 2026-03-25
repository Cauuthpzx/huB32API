#include "core/PrecompiledHeader.hpp"
#include "AuthController.hpp"
#include "../dto/AuthDto.hpp"
#include "../dto/ErrorDto.hpp"
#include "auth/JwtAuth.hpp"
#include "auth/Hub32KeyAuth.hpp"
#include "auth/UserRoleStore.hpp"
#include "core/internal/I18n.hpp"

// cpp-httplib
#include <httplib.h>

namespace {

std::string getLocale(const httplib::Request& req) {
    auto* i = hub32api::core::internal::I18n::instance();
    if (!i) return "en";
    return i->negotiate(req.get_header_value("Accept-Language"));
}

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
    auth::JwtAuth&       jwtAuth,
    auth::Hub32KeyAuth&  keyAuth,
    auth::UserRoleStore& roleStore)
    : m_jwtAuth(jwtAuth)
    , m_keyAuth(keyAuth)
    , m_roleStore(roleStore)
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
    using hub32api::core::internal::tr;
    const auto lang = getLocale(req);

    // --- Parse request body ---
    dto::AuthRequest req_dto;
    try {
        const auto j = nlohmann::json::parse(req.body);
        req_dto = j.get<dto::AuthRequest>();
    }
    catch (const std::exception& ex) {
        sendError(res, 400, tr(lang, "error.invalid_request_body"), ex.what());
        return;
    }

    req_dto.method = normalizeAuthMethod(req_dto.method);

    // --- Authenticate and determine role ---
    // SECURITY: Role is determined by authenticated lookup in persistent store.
    // No code path exists where role is derived from username string comparison.
    //
    // ATTACK PREVENTED: Previously, sending username="admin" with method="logon"
    // granted admin role with zero credential verification. Now the password
    // must match the PBKDF2-SHA256 hash stored in users.json, and role comes
    // from the store entry, not from the username string.
    std::string role;

    if (req_dto.method == "logon") {
        // Authenticate against UserRoleStore (password verification + role lookup)
        auto authResult = m_roleStore.authenticate(req_dto.username, req_dto.password);
        if (authResult.is_err()) {
            sendError(res, 401, tr(lang, "error.auth_failed"),
                      authResult.error().message);
            return;
        }
        role = authResult.value();
    }
    else if (req_dto.method == "hub32-key") {
        // Hub32 key auth -- delegates to Hub32KeyAuth
        auto keyResult = m_keyAuth.authenticate({req_dto.keyName, req_dto.keyData});
        if (keyResult.is_err()) {
            sendError(res, 401, tr(lang, "error.auth_failed"),
                      keyResult.error().message);
            return;
        }
        // Hub32 key users get "teacher" role by default
        // PHASE-3 TODO: allow key-to-role mapping in the store
        role = "teacher";
    }
    else {
        sendError(res, 400, tr(lang, "error.unsupported_auth_method"),
                  tr(lang, "error.supported_methods"));
        return;
    }

    // --- Issue JWT ---
    const auto tokenResult = m_jwtAuth.issueToken(req_dto.username, role);
    if (tokenResult.is_err()) {
        sendError(res, 401, tr(lang, "error.auth_failed"),
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
    using hub32api::core::internal::tr;
    const auto lang = getLocale(req);

    // --- Extract Bearer token from Authorization header ---
    const std::string authHeader = req.get_header_value("Authorization");
    if (authHeader.empty() || authHeader.rfind("Bearer ", 0) != 0) {
        sendError(res, 401, tr(lang, "error.unauthorized"),
                  tr(lang, "error.missing_auth_header"));
        return;
    }

    const std::string token = authHeader.substr(7); // strip "Bearer "
    if (token.empty()) {
        sendError(res, 401, tr(lang, "error.unauthorized"),
                  tr(lang, "error.empty_bearer_token"));
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
