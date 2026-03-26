/**
 * @file JwtAuth.cpp
 * @brief Full implementation of JwtAuth — JWT token issuance and validation.
 *
 * issueToken() builds a signed JWT (RS256 or HS256) containing all required
 * claims and a UUID-style jti for revocation support.
 *
 * When RS256 is configured with valid PEM key files, tokens are signed with
 * the RSA private key and verified with the public key. If RS256 keys are
 * missing or unreadable, construction fails with a fatal error — there is
 * NO silent fallback to HS256.
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
#include "../core/internal/CryptoUtils.hpp"

#include <atomic>
#include <fstream>

namespace hub32api::auth {

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------
namespace {
    /**
     * @brief Reads the entire content of a file into a string.
     *
     * Used to load PEM-encoded RSA key files for RS256 JWT signing/verification.
     *
     * @param path  Filesystem path to the file.
     * @return The file content as a string, or empty string if the file cannot be opened.
     */
    std::string readFileContent(const std::string& path)
    {
        std::ifstream f(path);
        if (!f.is_open()) return {};
        return std::string(std::istreambuf_iterator<char>(f),
                           std::istreambuf_iterator<char>());
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
    /** @brief Algorithm in use: "RS256" or "HS256". */
    std::string algorithm;

    /** @brief Raw HMAC secret for signing/verifying HS256 tokens. */
    std::string secret;

    /** @brief PEM-encoded RSA private key content for RS256 signing. */
    std::string privateKey;

    /** @brief PEM-encoded RSA public key content for RS256 verification. */
    std::string publicKey;

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
 * @brief Private constructor — takes a fully-built Impl.
 *
 * Use the static factory method @c create() instead.
 */
JwtAuth::JwtAuth(std::unique_ptr<Impl> impl) : m_impl(std::move(impl)) {}

/**
 * @brief Factory method — creates a JwtAuth from server configuration.
 *
 * When jwtAlgorithm is "RS256", valid PEM key files MUST be provided.
 * Tokens will be signed with the RSA private key and verified with the
 * public key. If the key files are missing or unreadable, the factory
 * returns a Result error — there is NO silent fallback to HS256.
 *
 * When jwtAlgorithm is "HS256", cfg.jwtSecret must be non-empty.
 *
 * @param cfg   Server configuration providing JWT settings.
 * @return Result containing a unique_ptr<JwtAuth> on success,
 *         or an ApiError on configuration failure.
 */
Result<std::unique_ptr<JwtAuth>> JwtAuth::create(const ServerConfig& cfg)
{
    auto impl = std::make_unique<Impl>();
    impl->algorithm     = cfg.jwtAlgorithm;
    impl->secret        = cfg.jwtSecret;
    impl->expirySeconds = cfg.jwtExpirySeconds;

    // SECURITY: If RS256 is configured, key files are REQUIRED.
    // Silent fallback to HS256 creates an invisible security downgrade
    // where tokens signed with a potentially weak HS256 secret are
    // accepted system-wide. An attacker who can reconstruct the secret
    // (via mt19937_64 seed prediction or HS256 brute-force) gains
    // the ability to forge tokens for any user including admin.
    if (cfg.jwtAlgorithm == to_string(JwtAlgorithm::RS256)) {
        if (cfg.jwtPrivateKeyFile.empty() || cfg.jwtPublicKeyFile.empty()) {
            return Result<std::unique_ptr<JwtAuth>>::fail(ApiError{
                ErrorCode::InvalidConfig,
                "[JwtAuth] RS256 algorithm requires both private and public key files"
            });
        }

        impl->privateKey = readFileContent(cfg.jwtPrivateKeyFile);
        impl->publicKey  = readFileContent(cfg.jwtPublicKeyFile);

        if (impl->privateKey.empty()) {
            return Result<std::unique_ptr<JwtAuth>>::fail(ApiError{
                ErrorCode::FileReadError,
                "[JwtAuth] Cannot read RS256 private key file: " + cfg.jwtPrivateKeyFile
            });
        }
        if (impl->publicKey.empty()) {
            return Result<std::unique_ptr<JwtAuth>>::fail(ApiError{
                ErrorCode::FileReadError,
                "[JwtAuth] Cannot read RS256 public key file: " + cfg.jwtPublicKeyFile
            });
        }
    }

    // For HS256: verify secret is non-empty
    if (cfg.jwtAlgorithm == to_string(JwtAlgorithm::HS256) && cfg.jwtSecret.empty()) {
        return Result<std::unique_ptr<JwtAuth>>::fail(ApiError{
            ErrorCode::InvalidConfig,
            "[JwtAuth] HS256 algorithm requires a non-empty jwtSecret"
        });
    }

    impl->validator = std::make_unique<internal::JwtValidator>(
        impl->algorithm, impl->secret, impl->publicKey);
    impl->store = std::make_unique<internal::TokenStore>(cfg.tokenRevocationFile);

    spdlog::info("[JwtAuth] using {} algorithm", impl->algorithm);

    auto auth = std::unique_ptr<JwtAuth>(new JwtAuth(std::move(impl)));
    return Result<std::unique_ptr<JwtAuth>>::ok(std::move(auth));
}

/**
 * @brief Default destructor — Pimpl requires out-of-line definition.
 */
JwtAuth::~JwtAuth() = default;

// ---------------------------------------------------------------------------
// Public methods
// ---------------------------------------------------------------------------

/**
 * @brief Issues a signed JWT (RS256 or HS256) for the given subject and role.
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
 * When the active algorithm is RS256, the token is signed with the RSA
 * private key. Otherwise, HS256 signing with the shared secret is used.
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
        auto jtiResult = core::internal::CryptoUtils::generateUuid();
        if (jtiResult.is_err()) {
            return Result<std::string>::fail(ApiError{
                ErrorCode::InternalError,
                "JWT ID generation failed: " + jtiResult.error().message
            });
        }
        const std::string jti = jtiResult.take();

        auto builder = jwt::create()
            .set_type("JWT")
            .set_issuer(std::string(kJwtIssuer))
            .set_audience(std::string(kJwtAudience))
            .set_subject(subject)
            .set_issued_at(now)
            .set_expires_at(expiry)
            .set_id(jti)
            .set_payload_claim("role", jwt::claim(role));

        std::string token;
        if (m_impl->algorithm == to_string(JwtAlgorithm::RS256)) {
            token = builder.sign(jwt::algorithm::rs256{m_impl->publicKey, m_impl->privateKey, "", ""});
        } else {
            token = builder.sign(jwt::algorithm::hs256{m_impl->secret});
        }

        spdlog::debug("[JwtAuth] issued token: sub={} role={} jti={} alg={}",
                      subject, role, jti, m_impl->algorithm);
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
    // Periodically purge expired entries from the token denylist to prevent
    // unbounded memory growth.  Every 100 authenticate() calls the store is
    // swept for entries whose expiry time has passed.
    static std::atomic<int> authCount{0};
    if (++authCount % kTokenPurgeIntervalCalls == 0) {
        m_impl->store->purgeExpired();
    }

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
