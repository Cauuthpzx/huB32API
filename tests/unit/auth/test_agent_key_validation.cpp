/**
 * @file test_agent_key_validation.cpp
 * @brief Unit tests for agent key validation using PBKDF2 hash with
 *        constant-time comparison.
 *
 * SECURITY FIX VALIDATED:
 * These tests verify the fix for the timing side-channel attack in agent
 * key validation. The old code used std::getenv("HUB32_AGENT_KEY") with
 * string operator!= — which is NOT constant-time and leaks key bytes via
 * timing measurements. An attacker could brute-force the key one byte at
 * a time. Additionally, environment variables are visible in
 * /proc/self/environ, process listings, container inspection, and crash
 * dumps. Blast radius: arbitrary agent impersonation, commands sent to
 * student PCs.
 *
 * The fix loads a PBKDF2-SHA256 hash from a file and verifies submitted
 * keys using OpenSSL CRYPTO_memcmp for constant-time comparison.
 */

#include <gtest/gtest.h>
#include "auth/UserRoleStore.hpp"

using namespace hub32api;
using namespace hub32api::auth;

// ---------------------------------------------------------------------------
// Agent key PBKDF2 verification
// ---------------------------------------------------------------------------

/**
 * @brief Verifies that PBKDF2 hash/verify round-trips correctly for agent keys.
 *
 * This is the core mechanism that replaces the insecure env-var + operator!=
 * comparison.
 */
TEST(AgentKeyValidationTest, ConstantTimeVerification)
{
    auto hashResult = UserRoleStore::hashPassword("my-secret-agent-key");
    ASSERT_TRUE(hashResult.is_ok()) << "hashPassword must succeed";
    const auto hash = hashResult.take();

    // Correct key must verify
    EXPECT_TRUE(UserRoleStore::verifyPassword("my-secret-agent-key", hash))
        << "Correct agent key must verify against its PBKDF2 hash";

    // Wrong key must fail
    EXPECT_FALSE(UserRoleStore::verifyPassword("wrong-key", hash))
        << "Wrong agent key must not verify";

    // Empty key must fail
    EXPECT_FALSE(UserRoleStore::verifyPassword("", hash))
        << "Empty agent key must not verify";
}

/**
 * @brief Verifies that an empty hash (no key file configured) rejects all
 *        verification attempts — fail-closed behavior.
 */
TEST(AgentKeyValidationTest, EmptyHashRejectsAll)
{
    EXPECT_FALSE(UserRoleStore::verifyPassword("any-key", ""))
        << "Empty hash must reject all keys (fail closed)";

    EXPECT_FALSE(UserRoleStore::verifyPassword("", ""))
        << "Empty key + empty hash must also fail";
}

/**
 * @brief Verifies that different keys produce different hashes (salt uniqueness).
 */
TEST(AgentKeyValidationTest, DifferentKeysProduceDifferentHashes)
{
    auto r1 = UserRoleStore::hashPassword("agent-key-alpha");
    auto r2 = UserRoleStore::hashPassword("agent-key-beta");
    ASSERT_TRUE(r1.is_ok());
    ASSERT_TRUE(r2.is_ok());
    const auto hash1 = r1.take();
    const auto hash2 = r2.take();

    EXPECT_NE(hash1, hash2)
        << "Different keys must produce different hashes";

    EXPECT_TRUE(UserRoleStore::verifyPassword("agent-key-alpha", hash1));
    EXPECT_FALSE(UserRoleStore::verifyPassword("agent-key-alpha", hash2));
    EXPECT_TRUE(UserRoleStore::verifyPassword("agent-key-beta", hash2));
    EXPECT_FALSE(UserRoleStore::verifyPassword("agent-key-beta", hash1));
}

/**
 * @brief Verifies that re-hashing the same key produces a different hash
 *        each time (random salt), but both verify correctly.
 */
TEST(AgentKeyValidationTest, SameKeyDifferentSalts)
{
    auto r1 = UserRoleStore::hashPassword("same-agent-key");
    auto r2 = UserRoleStore::hashPassword("same-agent-key");
    ASSERT_TRUE(r1.is_ok());
    ASSERT_TRUE(r2.is_ok());
    const auto hash1 = r1.take();
    const auto hash2 = r2.take();

    EXPECT_NE(hash1, hash2)
        << "Same key hashed twice must produce different hashes (random salt)";

    EXPECT_TRUE(UserRoleStore::verifyPassword("same-agent-key", hash1));
    EXPECT_TRUE(UserRoleStore::verifyPassword("same-agent-key", hash2));
}

/**
 * @brief Verifies that malformed hash strings are rejected gracefully.
 */
TEST(AgentKeyValidationTest, MalformedHashRejected)
{
    EXPECT_FALSE(UserRoleStore::verifyPassword("key", "not-a-hash"));
    EXPECT_FALSE(UserRoleStore::verifyPassword("key", "$pbkdf2-sha256$"));
    EXPECT_FALSE(UserRoleStore::verifyPassword("key", "$pbkdf2-sha256$bad$data"));
    EXPECT_FALSE(UserRoleStore::verifyPassword("key", "random-garbage-string"));
}
