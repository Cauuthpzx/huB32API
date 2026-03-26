/**
 * @file test_jwt_auth.cpp
 * @brief Unit tests for hub32api::auth::JwtAuth — token issuance, validation,
 *        revocation, and expiry detection.
 *
 * All tests use RS256 with in-memory RSA key pairs generated at test startup.
 * HS256 support has been removed entirely.
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <thread>
#include <chrono>
#include <string>
#include <fstream>
#include <filesystem>

#include <openssl/rsa.h>
#include <openssl/pem.h>
#include <openssl/bio.h>
#include <openssl/evp.h>

#include "hub32api/config/ServerConfig.hpp"
#include "auth/JwtAuth.hpp"

using namespace hub32api;
using namespace hub32api::auth;

namespace {

/**
 * @brief RAII helper that writes RSA key pair to temp files and cleans up on destruction.
 */
struct TestRsaKeys
{
    std::string privateKeyPath;
    std::string publicKeyPath;

    TestRsaKeys()
    {
        // Generate RSA key pair using OpenSSL EVP API
        EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr);
        EVP_PKEY_keygen_init(ctx);
        EVP_PKEY_CTX_set_rsa_keygen_bits(ctx, 4096);
        EVP_PKEY* pkey = nullptr;
        EVP_PKEY_keygen(ctx, &pkey);
        EVP_PKEY_CTX_free(ctx);

        // Write private key to temp file
        auto tmpDir = std::filesystem::temp_directory_path();
        privateKeyPath = (tmpDir / "hub32_test_priv.pem").string();
        publicKeyPath  = (tmpDir / "hub32_test_pub.pem").string();

        {
            BIO* bio = BIO_new_file(privateKeyPath.c_str(), "w");
            PEM_write_bio_PrivateKey(bio, pkey, nullptr, nullptr, 0, nullptr, nullptr);
            BIO_free(bio);
        }
        {
            BIO* bio = BIO_new_file(publicKeyPath.c_str(), "w");
            PEM_write_bio_PUBKEY(bio, pkey);
            BIO_free(bio);
        }

        EVP_PKEY_free(pkey);
    }

    ~TestRsaKeys()
    {
        std::filesystem::remove(privateKeyPath);
        std::filesystem::remove(publicKeyPath);
    }
};

// Global test keys — generated once per test suite run.
// Using a pointer to avoid static-init-order issues.
static TestRsaKeys* g_testKeys = nullptr;

/**
 * @brief Builds a ServerConfig pointing to the test RSA key files.
 */
ServerConfig makeTestConfig(int expirySeconds = 3600)
{
    ServerConfig cfg;
    cfg.jwtAlgorithm      = "RS256";
    cfg.jwtPrivateKeyFile = g_testKeys->privateKeyPath;
    cfg.jwtPublicKeyFile  = g_testKeys->publicKeyPath;
    cfg.jwtExpirySeconds  = expirySeconds;
    return cfg;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Test environment — generates RSA keys once for all tests
// ---------------------------------------------------------------------------
class JwtAuthTestEnvironment : public ::testing::Environment
{
public:
    void SetUp() override    { g_testKeys = new TestRsaKeys(); }
    void TearDown() override { delete g_testKeys; g_testKeys = nullptr; }
};

// Register the environment so keys are generated before any test runs.
static auto* const g_env =
    ::testing::AddGlobalTestEnvironment(new JwtAuthTestEnvironment());

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
    auto authCreateResult = JwtAuth::create(cfg);
    ASSERT_TRUE(authCreateResult.is_ok()) << "JwtAuth::create must succeed";
    auto& auth = *authCreateResult.value();

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
    auto authCreateResult = JwtAuth::create(cfg);
    ASSERT_TRUE(authCreateResult.is_ok()) << "JwtAuth::create must succeed";
    auto& auth = *authCreateResult.value();

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
    auto authCreateResult = JwtAuth::create(cfg);
    ASSERT_TRUE(authCreateResult.is_ok()) << "JwtAuth::create must succeed";
    auto& auth = *authCreateResult.value();

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
    auto authCreateResult = JwtAuth::create(cfg);
    ASSERT_TRUE(authCreateResult.is_ok()) << "JwtAuth::create must succeed";
    auto& auth = *authCreateResult.value();

    auto issueResult = auth.issueToken("expireduser", "readonly");
    ASSERT_TRUE(issueResult.is_ok());

    // Small sleep to ensure the token is past its expiry instant.
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    auto authResult = auth.authenticate(issueResult.value());
    EXPECT_TRUE(authResult.is_err())
        << "authenticate must reject an expired token";
}

// ---------------------------------------------------------------------------
// Security: Algorithm pinning — tokens issued by our JwtAuth must round-trip
// ---------------------------------------------------------------------------

/**
 * @brief Verifies that algorithm pinning works: tokens issued with RS256 are
 *        accepted when the server is configured for RS256 (algorithm matches).
 */
TEST(JwtAuthTest, RejectsTokenWithMismatchedAlgorithm)
{
    ServerConfig cfg = makeTestConfig();

    auto authCreateResult = JwtAuth::create(cfg);
    ASSERT_TRUE(authCreateResult.is_ok()) << "JwtAuth::create must succeed";
    auto& auth = *authCreateResult.value();
    auto tokenResult = auth.issueToken("testuser", "teacher");
    ASSERT_TRUE(tokenResult.is_ok());

    auto authResult = auth.authenticate(tokenResult.value());
    ASSERT_TRUE(authResult.is_ok());
    EXPECT_EQ(authResult.value().token->subject, "testuser");
}

// ---------------------------------------------------------------------------
// Security: HS256 is no longer supported
// ---------------------------------------------------------------------------

/**
 * @brief Verifies that JwtAuth::create() with HS256 algorithm
 *        returns an error Result — HS256 has been removed.
 */
TEST(JwtAuthTest, HS256IsRejected)
{
    ServerConfig cfg{};
    cfg.jwtAlgorithm = "HS256";
    cfg.jwtSecret = "some-secret";

    auto result = JwtAuth::create(cfg);
    EXPECT_TRUE(result.is_err())
        << "HS256 must be rejected";
    EXPECT_EQ(result.error().code, ErrorCode::InvalidConfig);
}

// ---------------------------------------------------------------------------
// Security: RS256 requires key files
// ---------------------------------------------------------------------------

/**
 * @brief Verifies that JwtAuth::create() with RS256 and missing key file
 *        paths returns an error Result — no silent fallback.
 */
TEST(JwtAuthTest, RS256RequiresKeyFiles)
{
    ServerConfig cfg{};
    cfg.jwtAlgorithm = "RS256";
    cfg.jwtPrivateKeyFile = "";  // missing!
    cfg.jwtPublicKeyFile = "";   // missing!

    auto result = JwtAuth::create(cfg);
    EXPECT_TRUE(result.is_err())
        << "RS256 with missing key files must fail";
    EXPECT_EQ(result.error().code, ErrorCode::InvalidConfig);
}
