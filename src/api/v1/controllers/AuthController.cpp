#include "core/PrecompiledHeader.hpp"
#include "AuthController.hpp"
#include "../dto/AuthDto.hpp"
#include "../dto/ErrorDto.hpp"
#include "auth/JwtAuth.hpp"
#include "auth/Hub32KeyAuth.hpp"
#include "auth/UserRoleStore.hpp"
#include "db/TeacherRepository.hpp"
#include "db/StudentRepository.hpp"
#include "core/internal/I18n.hpp"
#include "api/common/HttpErrorUtil.hpp"

// cpp-httplib
#include <httplib.h>

using hub32api::api::common::sendError;

namespace {

std::string getLocale(const httplib::Request& req) {
    auto* i = hub32api::core::internal::I18n::instance();
    if (!i) return "en";
    return i->negotiate(req.get_header_value("Accept-Language"));
}

hub32api::AuthMethod normalizeAuthMethod(const std::string& method)
{
    return hub32api::auth_method_from_string(method);
}

} // anonymous namespace

namespace hub32api::api::v1 {

/**
 * @brief Constructs the AuthController with the required auth services.
 *
 * @param jwtAuth      Service used to issue and revoke JWT tokens.
 * @param keyAuth      Service used for Hub32 public-key authentication.
 * @param roleStore    UserRoleStore for superadmin/admin credential verification.
 * @param teacherRepo  TeacherRepository for DB-backed teacher authentication.
 * @param studentRepo  StudentRepository for DB-backed student authentication.
 */
AuthController::AuthController(
    auth::JwtAuth&         jwtAuth,
    auth::Hub32KeyAuth&    keyAuth,
    auth::UserRoleStore&   roleStore,
    db::TeacherRepository& teacherRepo,
    db::StudentRepository& studentRepo)
    : m_jwtAuth(jwtAuth)
    , m_keyAuth(keyAuth)
    , m_roleStore(roleStore)
    , m_teacherRepo(teacherRepo)
    , m_studentRepo(studentRepo)
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

    const auto authMethod = normalizeAuthMethod(req_dto.method);

    // --- Read optional tenant header ---
    const std::string tenantId = req.get_header_value("X-Tenant-ID");

    // --- Authenticate and determine role ---
    // SECURITY: Role is determined by authenticated lookup in persistent store.
    // No code path exists where role is derived from username string comparison.
    //
    // Multi-tenant login order (Logon method):
    //   1. If X-Tenant-ID header present → try TeacherRepository (DB)
    //   2. If teacher not found           → try StudentRepository (DB)
    //   3. Fallback (no tenant or not found in DB) → UserRoleStore (superadmin/admin)
    std::string role;
    std::string subject;
    std::string tid;

    if (authMethod == AuthMethod::Logon) {
        // --- Try teacher lookup (scoped to tenant if X-Tenant-ID provided,
        //     otherwise search across all tenants by username) ---
        {
            auto teacherResult = m_teacherRepo.authenticate(
                req_dto.username, req_dto.password, tenantId);
            if (teacherResult.is_ok()) {
                const auto& rec = teacherResult.value();
                subject = rec.id;
                role    = rec.role;
                tid     = tenantId.empty() ? rec.tenantId : tenantId;
            }
        }

        if (subject.empty() && !tenantId.empty()) {
            // --- Try student (DB lookup, scoped to tenant) ---
            auto studentResult = m_studentRepo.authenticate(
                req_dto.username, req_dto.password, tenantId);
            if (studentResult.is_ok()) {
                subject = studentResult.value().username;
                role    = to_string(UserRole::Student);
                tid     = tenantId;
            }
        }

        // --- Fallback: UserRoleStore (superadmin / admin — no tenant) ---
        if (subject.empty()) {
            auto authResult = m_roleStore.authenticate(req_dto.username, req_dto.password);
            if (authResult.is_err()) {
                sendError(res, 401, tr(lang, "error.auth_failed"),
                          authResult.error().message);
                return;
            }
            role    = authResult.value();
            subject = req_dto.username;
            // tid stays empty — no tenant for superadmin/admin
        }
    }
    else if (authMethod == AuthMethod::Hub32Key) {
        // Hub32 key auth -- delegates to Hub32KeyAuth
        auto keyResult = m_keyAuth.authenticate({req_dto.keyName, req_dto.keyData});
        if (keyResult.is_err()) {
            sendError(res, 401, tr(lang, "error.auth_failed"),
                      keyResult.error().message);
            return;
        }
        // Hub32 key users get "teacher" role by default
        // PHASE-3 TODO: allow key-to-role mapping in the store
        role    = to_string(UserRole::Teacher);
        subject = req_dto.username;
        // tid stays empty — hub32-key auth is not tenant-scoped
    }
    else {
        sendError(res, 400, tr(lang, "error.unsupported_auth_method"),
                  tr(lang, "error.supported_methods"));
        return;
    }

    // --- Issue JWT (3-arg form: subject, role, tenant_id) ---
    // subject: teacher UUID or student username or admin username
    // tid:     tenant_id embedded as "tid" claim; empty for superadmin/admin
    const auto tokenResult = m_jwtAuth.issueToken(subject, role, tid);
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
    const dto::AuthResponse resp{tokenResult.value(), "Bearer", kDefaultTokenExpirySec};
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
    if (authHeader.empty() || authHeader.substr(0, kBearerPrefixLen) != kBearerPrefix) {
        sendError(res, 401, tr(lang, "error.unauthorized"),
                  tr(lang, "error.missing_auth_header"));
        return;
    }

    const std::string token = authHeader.substr(kBearerPrefixLen); // strip "Bearer "
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
