/**
 * @file JwtAuth.cpp
 * @brief Full implementation of JwtAuth — JWT token issuance and validation.
 *
 * issueToken() builds a signed HS256 JWT containing all required claims
 * and a UUID-style jti for revocation support.
 *
 * authenticate() delegates signature/claims verification to JwtValidator,
 * then checks the in-memory token denylist before returning an AuthContext.
 */

// jwt-cpp must be included before the PCH to ensure JWT_DISABLE_PICOJSON
// is defined before picojson.h could be pulled in transitively.
#define JWT_DISABLE_PICOJSON
#include <jwt-cpp/traits/nlohmann-json/defaults.h>

#include "../core/PrecompiledHeader.hpp"
#include "JwtAuth.hpp"
#include "internal/JwtValidator.hpp"
#include "internal/TokenStore.hpp"

#include <random>
#include <sstream>
#include <iomanip>

namespace hub32api::auth {

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------
namespace {
    constexpr const char* k_issuer   = "hub32api";
    constexpr const char* k_audience = "hub32api-clients";

    /**
     * @brief Generates a UUID v4 string using std::mt19937_64.
     *
     * The UUID is formatted as the standard 8-4-4-4-12 hex representation.
     * Using a thread_local generator ensures thread safety without a mutex.
     *
     * @return A UUID string, e.g. "550e8400-e29b-41d4-a716-446655440000".
     */
    std::string generateUuid()
    {
        static thread_local std::mt19937_64 rng{std::random_device{}()};
        std::uniform_int_distribution<uint64_t> dist;
        const uint64_t hi = dist(rng);
        const uint64_t lo = dist(rng);

        // Set version 4 bits and variant bits per RFC 4122
        const uint64_t hi4 = (hi & 0xFFFFFFFFFFFF0FFFULL) | 0x0000000000004000ULL;
        const uint64_t lo4 = (lo & 0x3FFFFFFFFFFFFFFFULL) | 0x8000000000000000ULL;

        std::ostringstream oss;
        oss << std::hex << std::setfill('0')
            << std::setw(8)  << ((hi4 >> 32) & 0xFFFFFFFFULL)
            << '-'
            << std::setw(4)  << ((hi4 >> 16) & 0xFFFFULL)
            << '-'
            << std::setw(4)  << (hi4 & 0xFFFFULL)
            << '-'
            << std::setw(4)  << ((lo4 >> 48) & 0xFFFFULL)
            << '-'
            << std::setw(12) << (lo4 & 0x0000FFFFFFFFFFFFULL);
        return oss.str();
    }
} // anonymous namespace

// ---------------------------------------------------------------------------
// Impl struct
// ---------------------------------------------------------------------------

/**
 * @brief Private implementation data for JwtAuth (Pimpl pattern).
 */
struct JwtAuth::Impl
{
    /** @brief Raw HMAC secret for signing/verifying HS256 tokens. */
    std::string secret;

    /** @brief Number of seconds until a newly issued token expires. */
    int expirySeconds;

    /** @brief Validates incoming JWT signatures and extracts claims. */
    std::unique_ptr<internal::JwtValidator> validator;

    /** @brief In-memory denylist for revoked tokens (logout support). */
    std::unique_ptr<internal::TokenStore> store;
};

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

/**
 * @brief Constructs JwtAuth from server configuration.
 *
 * Creates a JwtValidator seeded with @p cfg.jwtSecret, and an empty
 * in-memory TokenStore for the revocation denylist.
 *
 * @param cfg  Server configuration providing jwtSecret and jwtExpirySeconds.
 */
JwtAuth::JwtAuth(const ServerConfig& cfg)
    : m_impl(std::make_unique<Impl>())
{
    m_impl->secret        = cfg.jwtSecret;
    m_impl->expirySeconds = cfg.jwtExpirySeconds;
    m_impl->validator     = std::make_unique<internal::JwtValidator>(cfg.jwtSecret);
    m_impl->store         = std::make_unique<internal::TokenStore>();
}

/**
 * @brief Default destructor — Pimpl requires out-of-line definition.
 */
JwtAuth::~JwtAuth() = default;

// ---------------------------------------------------------------------------
// Public methods
// ---------------------------------------------------------------------------

/**
 * @brief Issues a signed HS256 JWT for the given subject and role.
 *
 * The generated token contains the following claims:
 *  - iss  "hub32api"
 *  - aud  "hub32api-clients"
 *  - sub  @p subject
 *  - role @p role  (custom claim)
 *  - jti  UUID v4 (for revocation)
 *  - iat  current time
 *  - exp  current time + jwtExpirySeconds
 *
 * @param subject  The authenticated username to embed as the "sub" claim.
 * @param role     The role string to embed as the "role" claim.
 * @return @c Result<std::string> containing the signed token on success,
 *         or an @c ApiError on signing failure.
 */
Result<std::string> JwtAuth::issueToken(
    const std::string& subject,
    const std::string& role) const
{
    try {
        const auto now    = std::chrono::system_clock::now();
        const auto expiry = now + std::chrono::seconds(m_impl->expirySeconds);
        const std::string jti = generateUuid();

        const std::string token =
            jwt::create()
                .set_type("JWT")
                .set_issuer(k_issuer)
                .set_audience(k_audience)
                .set_subject(subject)
                .set_issued_at(now)
                .set_expires_at(expiry)
                .set_id(jti)
                .set_payload_claim("role", jwt::claim(role))
                .sign(jwt::algorithm::hs256{m_impl->secret});

        spdlog::debug("[JwtAuth] issued token: sub={} role={} jti={}", subject, role, jti);
        return Result<std::string>::ok(std::move(token));
    }
    catch (const std::exception& ex) {
        spdlog::error("[JwtAuth] issueToken failed: {}", ex.what());
        return Result<std::string>::fail(ApiError{
            ErrorCode::InternalError,
            std::string("Token signing failed: ") + ex.what()
        });
    }
}

/**
 * @brief Validates a Bearer token and returns an authenticated AuthContext.
 *
 * Steps performed:
 *  1. Delegates to JwtValidator::validate() for signature and claims checks.
 *  2. Checks the in-memory denylist using token.jti.
 *  3. On success, constructs and returns an AuthContext with
 *     @c authenticated = true and the populated JwtToken.
 *
 * @param bearerToken  The Authorization header value or raw token string.
 *                     A "Bearer " prefix is accepted and stripped by the
 *                     validator.
 * @return @c Result<AuthContext> with an authenticated context on success,
 *         or an @c ApiError on any failure.
 */
Result<AuthContext> JwtAuth::authenticate(const std::string& bearerToken) const
{
    // 1. Validate signature, expiry, issuer, audience, and extract claims
    auto validationResult = m_impl->validator->validate(bearerToken);
    if (validationResult.is_err()) {
        spdlog::debug("[JwtAuth] authenticate: validation failed: {}",
                      validationResult.error().message);
        return Result<AuthContext>::fail(validationResult.error());
    }

    JwtToken token = validationResult.take();

    // 2. Check revocation denylist using jti
    if (m_impl->store->isRevoked(token.jti)) {
        spdlog::debug("[JwtAuth] authenticate: token jti={} is revoked", token.jti);
        return Result<AuthContext>::fail(ApiError{
            ErrorCode::Unauthorized, "Token has been revoked"
        });
    }

    // 3. Build authenticated context
    AuthContext ctx;
    ctx.authenticated = true;
    ctx.token         = std::move(token);

    spdlog::debug("[JwtAuth] authenticate: success sub={}", ctx.subject());
    return Result<AuthContext>::ok(std::move(ctx));
}

/**
 * @brief Adds a JWT to the in-memory revocation denylist.
 *
 * Subsequent authenticate() calls for tokens carrying this jti will
 * be rejected with ErrorCode::Unauthorized.
 *
 * @param jti  The JWT ID claim value to revoke.
 */
void JwtAuth::revokeToken(const std::string& jti)
{
    m_impl->store->revoke(jti);
    spdlog::debug("[JwtAuth] revokeToken: jti={}", jti);
}

} // namespace hub32api::auth
