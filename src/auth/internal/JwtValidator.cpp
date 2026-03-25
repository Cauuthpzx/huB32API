/**
 * @file JwtValidator.cpp
 * @brief Full implementation of JwtValidator using jwt-cpp with HS256.
 *
 * Uses the nlohmann-json traits adapter so that jwt-cpp shares the same
 * JSON library as the rest of the project, avoiding a second JSON parser
 * (picojson) in the final binary.
 */

// jwt-cpp must come before the PCH because it defines JWT_DISABLE_PICOJSON
// and pulls in OpenSSL headers that conflict with some Windows macros.
// The PCH already guards WIN32_LEAN_AND_MEAN / NOMINMAX, so this is safe.
#define JWT_DISABLE_PICOJSON
#include <jwt-cpp/traits/nlohmann-json/defaults.h>

#include "../../core/PrecompiledHeader.hpp"
#include "JwtValidator.hpp"

namespace veyon32api::auth::internal {

// ---------------------------------------------------------------------------
// Internal type alias
// ---------------------------------------------------------------------------
using JwtDecoded = jwt::decoded_jwt<jwt::traits::nlohmann_json>;

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------
namespace {
    constexpr const char* k_issuer   = "veyon32api";
    constexpr const char* k_audience = "veyon32api-clients";
} // anonymous namespace

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

/**
 * @brief Constructs a JwtValidator with the given HMAC-SHA256 secret.
 * @param secret  Raw secret bytes used for both signing verification and
 *                token creation.  Must not be empty in production.
 */
JwtValidator::JwtValidator(const std::string& secret)
    : m_secret(secret)
{}

// ---------------------------------------------------------------------------
// Public methods
// ---------------------------------------------------------------------------

/**
 * @brief Validates a raw JWT string and extracts its claims.
 *
 * The method performs the following steps in order:
 *  1. Strips the optional "Bearer " prefix.
 *  2. Decodes the token structure with jwt::decode().
 *  3. Verifies the HMAC-SHA256 signature, issuer, and audience with
 *     jwt::verify().
 *  4. Checks that the token has not expired.
 *  5. Extracts sub, role, jti, iat, and exp claims into a JwtToken.
 *
 * @param rawToken  The raw token string, optionally prefixed with "Bearer ".
 * @return @c Result<JwtToken> containing the parsed token on success, or an
 *         @c ApiError with ErrorCode::Unauthorized / TokenExpired on failure.
 */
Result<JwtToken> JwtValidator::validate(const std::string& rawToken) const
{
    // ------------------------------------------------------------------
    // 1. Strip optional "Bearer " prefix
    // ------------------------------------------------------------------
    std::string tokenStr = rawToken;
    constexpr std::string_view kBearer = "Bearer ";
    if (tokenStr.rfind("Bearer ", 0) == 0) {
        tokenStr = tokenStr.substr(kBearer.size());
    }

    if (tokenStr.empty()) {
        spdlog::debug("[JwtValidator] empty token after stripping prefix");
        return Result<JwtToken>::fail(ApiError{
            ErrorCode::Unauthorized, "Token is empty"
        });
    }

    // ------------------------------------------------------------------
    // 2. Decode the token (parse header + payload, no signature check yet)
    // ------------------------------------------------------------------
    std::optional<JwtDecoded> decoded;
    try {
        decoded.emplace(jwt::decode(tokenStr));
    }
    catch (const std::exception& ex) {
        spdlog::debug("[JwtValidator] decode failed: {}", ex.what());
        return Result<JwtToken>::fail(ApiError{
            ErrorCode::Unauthorized,
            std::string("Token decode failed: ") + ex.what()
        });
    }

    // ------------------------------------------------------------------
    // 3. Verify signature, issuer, audience, and expiry via jwt::verify()
    // ------------------------------------------------------------------
    try {
        jwt::verify()
            .allow_algorithm(jwt::algorithm::hs256{m_secret})
            .with_issuer(k_issuer)
            .with_audience(k_audience)
            .verify(*decoded);
    }
    catch (const jwt::error::token_verification_exception& ex) {
        spdlog::debug("[JwtValidator] verification failed: {}", ex.what());
        return Result<JwtToken>::fail(ApiError{
            ErrorCode::Unauthorized,
            std::string("Token verification failed: ") + ex.what()
        });
    }
    catch (const std::exception& ex) {
        spdlog::debug("[JwtValidator] unexpected verify error: {}", ex.what());
        return Result<JwtToken>::fail(ApiError{
            ErrorCode::Unauthorized,
            std::string("Token validation error: ") + ex.what()
        });
    }

    // ------------------------------------------------------------------
    // 4. Explicit expiry check so we can return TokenExpired specifically.
    // (jwt::verify also checks exp, but does not distinguish the error code.)
    // ------------------------------------------------------------------
    if (decoded->has_expires_at()) {
        const auto exp = decoded->get_expires_at();
        if (std::chrono::system_clock::now() > exp) {
            spdlog::debug("[JwtValidator] token has expired");
            return Result<JwtToken>::fail(ApiError{
                ErrorCode::TokenExpired, "Token has expired"
            });
        }
    }

    // ------------------------------------------------------------------
    // 5. Extract claims into JwtToken
    // ------------------------------------------------------------------
    JwtToken token;

    // Required: subject
    try {
        token.subject = decoded->get_subject();
    }
    catch (const std::exception& ex) {
        spdlog::debug("[JwtValidator] missing 'sub' claim: {}", ex.what());
        return Result<JwtToken>::fail(ApiError{
            ErrorCode::Unauthorized, "Token missing 'sub' claim"
        });
    }

    // Issuer (already verified above; set for callers that inspect it)
    try {
        token.issuer = decoded->get_issuer();
    }
    catch (const std::exception&) {
        token.issuer = k_issuer;
    }

    // Required custom claim: role
    try {
        token.role = decoded->get_payload_claim("role").as_string();
    }
    catch (const std::exception& ex) {
        spdlog::debug("[JwtValidator] missing 'role' claim: {}", ex.what());
        return Result<JwtToken>::fail(ApiError{
            ErrorCode::Unauthorized, "Token missing 'role' claim"
        });
    }

    // Required: JWT ID (jti) — needed for revocation
    try {
        token.jti = decoded->get_id();
    }
    catch (const std::exception& ex) {
        spdlog::debug("[JwtValidator] missing 'jti' claim: {}", ex.what());
        return Result<JwtToken>::fail(ApiError{
            ErrorCode::Unauthorized, "Token missing 'jti' claim"
        });
    }

    // Optional: issued-at (iat)
    if (decoded->has_issued_at()) {
        token.issuedAt = decoded->get_issued_at();
    }

    // Optional: expires-at (exp)
    if (decoded->has_expires_at()) {
        token.expiresAt = decoded->get_expires_at();
    }

    spdlog::debug("[JwtValidator] token valid: sub={} role={} jti={}",
                  token.subject, token.role, token.jti);

    return Result<JwtToken>::ok(std::move(token));
}

} // namespace veyon32api::auth::internal
