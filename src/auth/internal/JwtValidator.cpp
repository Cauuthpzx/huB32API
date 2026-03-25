/**
 * @file JwtValidator.cpp
 * @brief Full implementation of JwtValidator using jwt-cpp with HS256 and RS256.
 *
 * Uses the nlohmann-json traits adapter so that jwt-cpp shares the same
 * JSON library as the rest of the project, avoiding a second JSON parser
 * (picojson) in the final binary.
 *
 * Supports both HS256 (symmetric HMAC) and RS256 (asymmetric RSA) algorithms.
 * When RS256 is configured, only the public key is needed for verification.
 */

// jwt-cpp must come before the PCH because it defines JWT_DISABLE_PICOJSON
// and pulls in OpenSSL headers that conflict with some Windows macros.
// The PCH already guards WIN32_LEAN_AND_MEAN / NOMINMAX, so this is safe.
#define JWT_DISABLE_PICOJSON
#include <jwt-cpp/traits/nlohmann-json/defaults.h>

#include "../../core/PrecompiledHeader.hpp"
#include "JwtValidator.hpp"

namespace hub32api::auth::internal {

// ---------------------------------------------------------------------------
// Internal type alias
// ---------------------------------------------------------------------------
using JwtDecoded = jwt::decoded_jwt<jwt::traits::nlohmann_json>;

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------
namespace {
    constexpr const char* k_issuer   = "hub32api";
    constexpr const char* k_audience = "hub32api-clients";
} // anonymous namespace

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

/**
 * @brief Constructs a JwtValidator with the specified algorithm and credentials.
 *
 * @param algorithm  "RS256" for asymmetric RSA verification, or "HS256" for
 *                   symmetric HMAC-SHA256 verification.
 * @param secret     Raw secret bytes used for HS256 verification. Ignored when
 *                   algorithm is "RS256".
 * @param publicKey  PEM-encoded RSA public key for RS256 verification. Ignored
 *                   when algorithm is "HS256".
 */
JwtValidator::JwtValidator(const std::string& algorithm,
                           const std::string& secret,
                           const std::string& publicKey)
    : m_algorithm(algorithm), m_secret(secret), m_publicKey(publicKey)
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
 *  3. Verifies the signature (HS256 or RS256), issuer, and audience with
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
    // 2b. SECURITY: Algorithm pinning — reject tokens claiming a different
    //     algorithm than what the server is configured for.
    //
    //     ATTACK PREVENTED: Algorithm confusion attack where an attacker
    //     crafts a token with "alg":"HS256" and signs it with the RS256
    //     public key (which may be publicly known). Without this check,
    //     the server would verify using HS256 with the public key as
    //     the secret, allowing token forgery.
    //     Also rejects "none" algorithm tokens unconditionally.
    //
    //     ATTACK SCENARIO (fixed):
    //       Server configured for RS256 (asymmetric). Key files missing/deleted
    //       at startup. Server silently falls back to HS256 with a secret
    //       generated by std::mt19937_64 (NOT crypto-secure). Attacker who
    //       knows approximate server start time can reconstruct the secret
    //       space and forge valid tokens for any user including admin.
    //       Blast radius: complete authentication bypass.
    // ------------------------------------------------------------------
    {
        const auto& header = decoded->get_header_json();
        std::string tokenAlg;
        if (header.count("alg")) {
            tokenAlg = header.at("alg").template get<std::string>();
        }

        if (tokenAlg == "none" || tokenAlg == "None" || tokenAlg == "NONE") {
            spdlog::warn("[JwtValidator] REJECTED: token uses 'none' algorithm — possible attack");
            return Result<JwtToken>::fail(ApiError{
                ErrorCode::Unauthorized,
                "Token algorithm 'none' is not permitted"
            });
        }

        if (tokenAlg != m_algorithm) {
            spdlog::warn("[JwtValidator] REJECTED: token algorithm '{}' does not match "
                         "configured algorithm '{}'", tokenAlg, m_algorithm);
            return Result<JwtToken>::fail(ApiError{
                ErrorCode::Unauthorized,
                "Token algorithm mismatch: expected " + m_algorithm + ", got " + tokenAlg
            });
        }
    }

    // ------------------------------------------------------------------
    // 3. Verify signature, issuer, audience, and expiry via jwt::verify()
    // ------------------------------------------------------------------
    try {
        auto verifier = jwt::verify()
            .with_issuer(k_issuer)
            .with_audience(k_audience);

        if (m_algorithm == "RS256") {
            verifier.allow_algorithm(jwt::algorithm::rs256{m_publicKey, "", "", ""});
        } else {
            verifier.allow_algorithm(jwt::algorithm::hs256{m_secret});
        }

        verifier.verify(*decoded);
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

} // namespace hub32api::auth::internal
