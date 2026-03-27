#pragma once
#include <string>

#include "hub32api/service/EmailService.hpp"

namespace httplib { struct Request; struct Response; }
namespace hub32api::db { class TenantRepository; class TeacherRepository; class DatabaseManager; }

namespace hub32api::api::v1 {

/**
 * @brief Handles public registration endpoints.
 *
 * POST  /api/v1/register — creates a new tenant + owner account, issues a
 *                          registration token.  When EmailService is configured
 *                          the verification link is emailed; otherwise the token
 *                          is returned in the response body (dev/debug mode).
 * GET   /api/v1/verify   — verifies a registration token and activates the tenant.
 *
 * Both endpoints are public (no JWT required).
 * Thread safety: DatabaseManager mutex serialises all SQLite operations.
 */
class RegisterController
{
public:
    // emailService may be nullptr — in that case only the token is logged/returned.
    // appBaseUrl: base URL for verification links (e.g. "https://app.truong.edu.vn").
    //             Empty = falls back to "http://127.0.0.1:11081".
    RegisterController(db::TenantRepository& tenantRepo,
                       db::TeacherRepository& teacherRepo,
                       db::DatabaseManager&   dbManager,
                       service::EmailService* emailService = nullptr,
                       std::string            appBaseUrl   = "");

    void handleRegister(const httplib::Request& req, httplib::Response& res);
    void handleVerify(const httplib::Request& req, httplib::Response& res);
    void handleCaptcha(const httplib::Request& req, httplib::Response& res);

private:
    db::TenantRepository&  m_tenantRepo;
    db::TeacherRepository& m_teacherRepo;
    db::DatabaseManager&   m_dbManager;
    service::EmailService* m_emailService;  // non-owning; may be nullptr
    std::string            m_appBaseUrl;    // base URL for verification links in emails

    // Inserts a registration token with a given TTL into registration_tokens.
    // Returns true on success.
    bool insertToken(const std::string& token,
                     const std::string& tenantId,
                     int64_t            expiresAt);

    // Checks whether a token exists and is valid.
    // Returns:  0 = not found
    //           1 = found and valid
    //          -1 = found but expired
    //          -2 = found but already used
    int checkToken(const std::string& token, std::string& outTenantId);

    // Marks a registration token as used so it cannot be replayed.
    void markTokenUsed(const std::string& token);

    // Converts an organisation name to a URL-safe slug.
    static std::string makeSlug(const std::string& name);

    // Minimal RFC-5322 email format check.
    static bool isValidEmail(const std::string& email);

    // Returns {captchaId, digits} where captchaId is an HMAC-signed token.
    static std::pair<std::string, std::string> generateCaptcha();

    // Returns true if captchaId signature is valid, answer matches, and not expired.
    static bool verifyCaptcha(const std::string& captchaId, const std::string& answer);
};

} // namespace hub32api::api::v1
