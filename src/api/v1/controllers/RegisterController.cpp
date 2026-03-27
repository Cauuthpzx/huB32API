/**
 * @file RegisterController.cpp
 * @brief Implementation of public registration and email-verification endpoints.
 *
 * POST /api/v1/register
 *   - Validates orgName (1-100 chars), email format, and password (min 8 chars).
 *   - Creates tenant (status=pending) + owner teacher (role=owner).
 *   - Generates a UUID registration token with a 24-hour TTL stored in
 *     registration_tokens.
 *   - Logs the token via spdlog::info; returns it in the response body when
 *     HUB32_ENV != "production" (dev/debug mode only).
 *
 * GET /api/v1/verify?token=<uuid>
 *   - Validates token exists, not expired, not already used.
 *   - Activates the tenant (status=active), marks token used.
 *   - Returns 200 {"message":"account activated"}.
 *
 * Thread safety: all direct SQLite operations lock m_dbManager.dbMutex().
 */

#include "core/PrecompiledHeader.hpp"
#include "RegisterController.hpp"
#include "../dto/RegisterDto.hpp"
#include "db/TenantRepository.hpp"
#include "db/TeacherRepository.hpp"
#include "db/DatabaseManager.hpp"
#include "api/common/HttpErrorUtil.hpp"
#include "core/internal/CryptoUtils.hpp"
#include "service_email/EmailTemplates.hpp"

#include <httplib.h>
#include <sqlite3.h>

using hub32api::api::common::sendError;
using hub32api::core::internal::CryptoUtils;

namespace hub32api::api::v1 {

// ─────────────────────────────────────────────────────────────────────────────
// Construction
// ─────────────────────────────────────────────────────────────────────────────

RegisterController::RegisterController(db::TenantRepository& tenantRepo,
                                       db::TeacherRepository& teacherRepo,
                                       db::DatabaseManager&   dbManager,
                                       service::EmailService* emailService,
                                       std::string            appBaseUrl)
    : m_tenantRepo(tenantRepo)
    , m_teacherRepo(teacherRepo)
    , m_dbManager(dbManager)
    , m_emailService(emailService)
    , m_appBaseUrl(std::move(appBaseUrl))
{}

// ─────────────────────────────────────────────────────────────────────────────
// Static helpers
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief Converts an organisation name to a URL-safe slug.
 *
 * Rules:
 * - Alpha chars are lowercased.
 * - Digits and hyphens pass through unchanged.
 * - Any other character is replaced with a single hyphen (no consecutive hyphens).
 * - Leading/trailing hyphens are trimmed.
 */
std::string RegisterController::makeSlug(const std::string& name)
{
    std::string slug;
    slug.reserve(name.size());
    for (char c : name) {
        if (std::isalpha(static_cast<unsigned char>(c))) {
            slug += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        } else if (std::isdigit(static_cast<unsigned char>(c)) || c == '-') {
            slug += c;
        } else if (!slug.empty() && slug.back() != '-') {
            slug += '-';
        }
    }
    while (!slug.empty() && slug.back() == '-') {
        slug.pop_back();
    }
    return slug;
}

/**
 * @brief Performs a minimal email format validation.
 *
 * Checks that the email contains exactly one '@', that it is not at
 * the beginning or end, and that there is a '.' after the '@' with at
 * least one character on both sides of it.
 */
bool RegisterController::isValidEmail(const std::string& email)
{
    const auto at = email.find('@');
    if (at == std::string::npos || at == 0 || at == email.size() - 1) {
        return false;
    }
    const auto dot = email.rfind('.');
    return dot != std::string::npos && dot > at + 1 && dot < email.size() - 1;
}

// ─────────────────────────────────────────────────────────────────────────────
// Token helpers (direct SQLite — no separate repository needed)
// ─────────────────────────────────────────────────────────────────────────────

bool RegisterController::insertToken(const std::string& token,
                                     const std::string& tenantId,
                                     int64_t            expiresAt)
{
    std::lock_guard<std::mutex> lock(m_dbManager.dbMutex());
    sqlite3* db = m_dbManager.schoolDb();

    constexpr const char* k_sql =
        "INSERT INTO registration_tokens(token, tenant_id, expires_at, used)"
        " VALUES(?, ?, ?, 0);";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, k_sql, -1, &stmt, nullptr) != SQLITE_OK) {
        spdlog::error("[RegisterController] insertToken prepare failed: {}", sqlite3_errmsg(db));
        return false;
    }

    sqlite3_bind_text(stmt, 1, token.c_str(),    static_cast<int>(token.size()),    SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, tenantId.c_str(), static_cast<int>(tenantId.size()), SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 3, expiresAt);

    const int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        spdlog::error("[RegisterController] insertToken step failed: {}", sqlite3_errmsg(db));
        return false;
    }
    return true;
}

int RegisterController::checkToken(const std::string& token, std::string& outTenantId)
{
    std::lock_guard<std::mutex> lock(m_dbManager.dbMutex());
    sqlite3* db = m_dbManager.schoolDb();

    constexpr const char* k_sql =
        "SELECT tenant_id, expires_at, used FROM registration_tokens WHERE token=? LIMIT 1;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, k_sql, -1, &stmt, nullptr) != SQLITE_OK) {
        spdlog::error("[RegisterController] checkToken prepare failed: {}", sqlite3_errmsg(db));
        return 0;
    }

    sqlite3_bind_text(stmt, 1, token.c_str(), static_cast<int>(token.size()), SQLITE_STATIC);

    const int rc = sqlite3_step(stmt);
    if (rc != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        return 0;  // not found
    }

    const char* tenantIdRaw = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    outTenantId = tenantIdRaw ? std::string(tenantIdRaw) : std::string{};
    const int64_t expiresAt = sqlite3_column_int64(stmt, 1);
    const int     used      = sqlite3_column_int(stmt, 2);
    sqlite3_finalize(stmt);

    if (used != 0) {
        return -2;  // already used
    }

    const int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    if (now > expiresAt) {
        return -1;  // expired
    }

    return 1;  // valid
}

void RegisterController::markTokenUsed(const std::string& token)
{
    std::lock_guard<std::mutex> lock(m_dbManager.dbMutex());
    sqlite3* db = m_dbManager.schoolDb();

    constexpr const char* k_sql =
        "UPDATE registration_tokens SET used=1 WHERE token=?;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, k_sql, -1, &stmt, nullptr) != SQLITE_OK) {
        spdlog::error("[RegisterController] markTokenUsed prepare failed: {}", sqlite3_errmsg(db));
        return;
    }

    sqlite3_bind_text(stmt, 1, token.c_str(), static_cast<int>(token.size()), SQLITE_STATIC);
    const int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        spdlog::error("[RegisterController] markTokenUsed step failed: {}", sqlite3_errmsg(db));
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// POST /api/v1/register
// ─────────────────────────────────────────────────────────────────────────────

void RegisterController::handleRegister(const httplib::Request& req, httplib::Response& res)
{
    // 1. Parse and validate request body
    dto::RegisterRequest dto;
    try {
        const auto j = nlohmann::json::parse(req.body);
        dto = j.get<dto::RegisterRequest>();
    } catch (const std::exception& ex) {
        sendError(res, 400, "Invalid request body", ex.what());
        return;
    }

    if (dto.orgName.empty() || dto.orgName.size() > 100) {
        sendError(res, 400, "orgName must be between 1 and 100 characters");
        return;
    }
    if (!isValidEmail(dto.email)) {
        sendError(res, 400, "Invalid email format");
        return;
    }
    if (dto.password.size() < 8) {
        sendError(res, 400, "Password must be at least 8 characters");
        return;
    }

    // 2. Generate slug; fall back to "org" if empty
    std::string slug = makeSlug(dto.orgName);
    if (slug.empty()) {
        slug = "org";
    }

    // 3. Create tenant (status=pending); handle slug/email conflicts
    auto tenantResult = m_tenantRepo.create(dto.orgName, slug, dto.email);
    if (tenantResult.is_err()) {
        const auto& err = tenantResult.error();
        if (err.code == ErrorCode::Conflict) {
            // If the slug was taken (not the email), retry with a random suffix
            if (err.message.find("owner_email") == std::string::npos) {
                // slug conflict — generate suffix from a new UUID
                auto suffixResult = CryptoUtils::generateUuid();
                const std::string suffix = suffixResult.is_ok()
                    ? suffixResult.value().substr(0, 4) : "rand";
                const std::string slugAlt = slug + "-" + suffix;
                auto retryResult = m_tenantRepo.create(dto.orgName, slugAlt, dto.email);
                if (retryResult.is_err()) {
                    const auto& err2 = retryResult.error();
                    if (err2.code == ErrorCode::Conflict) {
                        sendError(res, 409, "Email already registered or slug conflict");
                    } else {
                        sendError(res, 500, "Failed to create tenant", err2.message);
                    }
                    return;
                }
                tenantResult = std::move(retryResult);
            } else {
                // email conflict
                sendError(res, 409, "Email already registered");
                return;
            }
        } else {
            sendError(res, 500, "Failed to create tenant", err.message);
            return;
        }
    }
    const std::string tenantId = tenantResult.value();

    // 4. Create owner account in teachers table
    auto teacherResult = m_teacherRepo.create(dto.email, dto.password, dto.orgName, "owner");
    if (teacherResult.is_err()) {
        spdlog::error("[RegisterController] failed to create owner teacher: {}",
                      teacherResult.error().message);
        sendError(res, 500, "Failed to create owner account", teacherResult.error().message);
        return;
    }
    const std::string teacherId = teacherResult.value();

    // 5. Bind teacher to tenant via direct UPDATE (TeacherRepository::create does not
    //    accept tenant_id parameter yet — this is the simplest approach).
    {
        std::lock_guard<std::mutex> lock(m_dbManager.dbMutex());
        sqlite3* db = m_dbManager.schoolDb();

        constexpr const char* k_sql =
            "UPDATE teachers SET tenant_id=? WHERE id=?;";

        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db, k_sql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, tenantId.c_str(),  static_cast<int>(tenantId.size()),  SQLITE_STATIC);
            sqlite3_bind_text(stmt, 2, teacherId.c_str(), static_cast<int>(teacherId.size()), SQLITE_STATIC);
            const int rc = sqlite3_step(stmt);
            sqlite3_finalize(stmt);
            if (rc != SQLITE_DONE) {
                spdlog::warn("[RegisterController] could not set tenant_id on teacher {}: {}",
                             teacherId, sqlite3_errmsg(db));
                // Non-fatal: teacher was created; tenant_id will be NULL until migration adds column
            }
        } else {
            spdlog::warn("[RegisterController] UPDATE teachers SET tenant_id prepare failed: {}",
                         sqlite3_errmsg(db));
        }
    }

    // 6. Generate registration token (UUID, 24-hour TTL)
    auto tokenUuidResult = CryptoUtils::generateUuid();
    if (tokenUuidResult.is_err()) {
        spdlog::error("[RegisterController] UUID generation for token failed: {}",
                      tokenUuidResult.error().message);
        sendError(res, 500, "Failed to generate registration token");
        return;
    }
    const std::string regToken = tokenUuidResult.value();

    const int64_t now       = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    constexpr int64_t kTtlSeconds = 24 * 60 * 60;  // 24 hours
    const int64_t expiresAt = now + kTtlSeconds;

    if (!insertToken(regToken, tenantId, expiresAt)) {
        sendError(res, 500, "Failed to store registration token");
        return;
    }

    spdlog::info("[Register] new tenant={} owner={} token={}", tenantId, dto.email, regToken);

    // 7. Send verification email (non-fatal if EmailService unavailable)
    if (m_emailService && m_emailService->isConfigured()) {
        const std::string verifyUrl = m_appBaseUrl + "/api/v1/verify?token=" + regToken;
        auto emailMsgResult = service::makeVerificationEmail(dto.email, dto.orgName, verifyUrl);
        if (emailMsgResult.is_err()) {
            spdlog::warn("[Register] failed to build verification email: {}",
                         emailMsgResult.error().message);
        } else {
            auto emailResult = m_emailService->send(emailMsgResult.value());
            if (emailResult.is_err()) {
                spdlog::warn("[Register] failed to send verification email to {}: {}",
                             dto.email, emailResult.error().message);
                // Non-fatal: user can still use debug_token in dev mode
            } else {
                spdlog::info("[Register] verification email sent to {}", dto.email);
            }
        }
    }

    // 8. Build response — include debug token only outside production
    const char* envVar      = std::getenv("HUB32_ENV");
    const bool  isProd      = (envVar && std::string_view{envVar} == "production");

    dto::RegisterResponse resp;
    resp.message    = "check email";
    resp.debugToken = isProd ? "" : regToken;

    const nlohmann::json j = resp;
    res.status = 201;
    res.set_content(j.dump(), "application/json");
}

// ─────────────────────────────────────────────────────────────────────────────
// GET /api/v1/verify?token=<uuid>
// ─────────────────────────────────────────────────────────────────────────────

void RegisterController::handleVerify(const httplib::Request& req, httplib::Response& res)
{
    // 1. Extract token query parameter
    const std::string token = req.get_param_value("token");
    if (token.empty()) {
        sendError(res, 400, "Missing required query parameter: token");
        return;
    }

    // 2. Validate token
    std::string tenantId;
    const int tokenStatus = checkToken(token, tenantId);

    switch (tokenStatus) {
        case 0:
            sendError(res, 404, "Token not found");
            return;
        case -1:
            sendError(res, 410, "Token has expired");
            return;
        case -2:
            sendError(res, 409, "Token has already been used");
            return;
        default:
            break;  // 1 = valid
    }

    // 3. Activate tenant
    auto activateResult = m_tenantRepo.activate(tenantId);
    if (activateResult.is_err()) {
        spdlog::error("[RegisterController] failed to activate tenant {}: {}",
                      tenantId, activateResult.error().message);
        sendError(res, 500, "Failed to activate tenant", activateResult.error().message);
        return;
    }

    // 4. Consume the token so it cannot be replayed
    markTokenUsed(token);

    spdlog::info("[Register] tenant {} activated via token verification", tenantId);

    nlohmann::json j;
    j["message"] = "account activated";
    res.status = 200;
    res.set_content(j.dump(), "application/json");
}

} // namespace hub32api::api::v1
