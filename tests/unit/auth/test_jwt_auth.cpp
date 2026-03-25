/**
 * @file test_jwt_auth.cpp
 * @brief Unit tests for hub32api::auth::JwtAuth — token issuance, validation,
 *        revocation, and expiry detection.
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <thread>
#include <chrono>
#include <string>

#include "hub32api/config/ServerConfig.hpp"
#include "auth/JwtAuth.hpp"

using namespace hub32api;
using namespace hub32api::auth;

namespace {

/**
 * @brief Builds a ServerConfig with a known JWT secret for deterministic tests.
 * @return A ServerConfig suitable for unit-test token operations.
 */
ServerConfig makeTestConfig(int expirySeconds = 3600)
{
    ServerConfig cfg;
    cfg.jwtSecret        = "test-secret-key-for-unit-tests";
    cfg.jwtExpirySeconds = expirySeconds;
    return cfg;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// IssueToken — basic issuance checks
// ---------------------------------------------------------------------------

/**
 * @brief Verifies that issueToken returns a non-empty JWT string
 *        containing the expected three dot-separated segments.
 */
TEST(JwtAuthTest, IssueTokenReturnsValidTokenString)
{
    const auto cfg = makeTestConfig();
    JwtAuth auth(cfg);

    auto result = auth.issueToken("admin", "admin");
    ASSERT_TRUE(result.is_ok()) << "issueToken should succeed";

    const std::string& token = result.value();
    EXPECT_FALSE(token.empty()) << "Token must be non-empty";

    // A valid JWT has exactly two dots separating header.payload.signature.
    const auto dotCount = std::count(token.begin(), token.end(), '.');
    EXPECT_EQ(dotCount, 2) << "JWT must contain exactly two dots (three segments)";
}

// ---------------------------------------------------------------------------
// Roundtrip — issue then authenticate
// ---------------------------------------------------------------------------

/**
 * @brief Issues a token and immediately authenticates it, verifying the
 *        returned AuthContext carries the correct subject and role.
 */
TEST(JwtAuthTest, IssueAndAuthenticateRoundtrip)
{
    const auto cfg = makeTestConfig();
    JwtAuth auth(cfg);

    auto issueResult = auth.issueToken("testuser", "teacher");
    ASSERT_TRUE(issueResult.is_ok());

    auto authResult = auth.authenticate(issueResult.value());
    ASSERT_TRUE(authResult.is_ok()) << "authenticate should accept a freshly issued token";

    const AuthContext& ctx = authResult.value();
    EXPECT_TRUE(ctx.authenticated);
    ASSERT_TRUE(ctx.token.has_value());
    EXPECT_EQ(ctx.token->subject, "testuser");
    EXPECT_EQ(ctx.token->role, "teacher");
}

// ---------------------------------------------------------------------------
// Revocation — revokeToken causes authenticate to fail
// ---------------------------------------------------------------------------

/**
 * @brief Issues a token, extracts its jti, revokes it, then verifies
 *        that authenticate rejects the revoked token.
 */
TEST(JwtAuthTest, RevokeTokenCausesAuthenticateToFail)
{
    const auto cfg = makeTestConfig();
    JwtAuth auth(cfg);

    auto issueResult = auth.issueToken("revokeuser", "admin");
    ASSERT_TRUE(issueResult.is_ok());
    const std::string token = issueResult.value();

    // First, authenticate to extract the jti.
    auto firstAuth = auth.authenticate(token);
    ASSERT_TRUE(firstAuth.is_ok());
    ASSERT_TRUE(firstAuth.value().token.has_value());
    const std::string jti = firstAuth.value().token->jti;
    EXPECT_FALSE(jti.empty()) << "Token must have a non-empty jti";

    // Revoke by jti.
    auth.revokeToken(jti);

    // Authenticate again — should now fail.
    auto secondAuth = auth.authenticate(token);
    EXPECT_TRUE(secondAuth.is_err())
        << "authenticate must reject a revoked token";
}

// ---------------------------------------------------------------------------
// Expiry — a token issued with 0-second expiry should fail validation
// ---------------------------------------------------------------------------

/**
 * @brief Issues a token with a zero-second expiry, waits briefly,
 *        then verifies that authenticate rejects it as expired.
 */
TEST(JwtAuthTest, ExpiredTokenFailsAuthentication)
{
    // Issue with immediate expiry (0 seconds).
    auto cfg = makeTestConfig(0);
    JwtAuth auth(cfg);

    auto issueResult = auth.issueToken("expireduser", "readonly");
    ASSERT_TRUE(issueResult.is_ok());

    // Small sleep to ensure the token is past its expiry instant.
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    auto authResult = auth.authenticate(issueResult.value());
    EXPECT_TRUE(authResult.is_err())
        << "authenticate must reject an expired token";
}
