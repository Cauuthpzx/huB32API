/**
 * @file test_user_role_store.cpp
 * @brief Unit tests for hub32api::auth::UserRoleStore -- PBKDF2 hashing,
 *        password verification, and fail-closed behavior.
 *
 * These tests validate the security fix that removes hardcoded admin
 * role escalation. The key invariant: without a valid users.json file,
 * ALL authentication attempts must fail (fail closed).
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <fstream>
#include <string>
#include <nlohmann/json.hpp>

#include "auth/UserRoleStore.hpp"

using namespace hub32api;
using namespace hub32api::auth;

// ---------------------------------------------------------------------------
// Password hashing and verification
// ---------------------------------------------------------------------------

/**
 * @brief Verifies that hashPassword produces a well-formed Argon2id hash string
 *        and that verifyPassword accepts it.
 */
TEST(UserRoleStoreTest, HashAndVerifyPassword)
{
    auto hashResult = UserRoleStore::hashPassword("testpassword123");
    ASSERT_TRUE(hashResult.is_ok()) << "hashPassword must succeed";
    const auto hash = hashResult.take();

    // Must start with the Argon2id algorithm prefix
    EXPECT_EQ(hash.rfind("$argon2id$", 0), 0u)
        << "Hash must start with $argon2id$ prefix";

    // Must verify correctly
    EXPECT_TRUE(UserRoleStore::verifyPassword("testpassword123", hash))
        << "Correct password must verify against its own hash";

    // Wrong password must fail
    EXPECT_FALSE(UserRoleStore::verifyPassword("wrongpassword", hash))
        << "Wrong password must not verify";
}

/**
 * @brief Verifies that each call to hashPassword produces a different hash
 *        (different random salt each time).
 */
TEST(UserRoleStoreTest, HashPasswordProducesDifferentSalts)
{
    auto r1 = UserRoleStore::hashPassword("samepassword");
    auto r2 = UserRoleStore::hashPassword("samepassword");
    ASSERT_TRUE(r1.is_ok());
    ASSERT_TRUE(r2.is_ok());
    const auto hash1 = r1.take();
    const auto hash2 = r2.take();

    EXPECT_NE(hash1, hash2)
        << "Two hashes of the same password must differ (different salts)";

    // Both must still verify
    EXPECT_TRUE(UserRoleStore::verifyPassword("samepassword", hash1));
    EXPECT_TRUE(UserRoleStore::verifyPassword("samepassword", hash2));
}

/**
 * @brief Verifies that verifyPassword rejects malformed hash strings.
 */
TEST(UserRoleStoreTest, VerifyRejectsMalformedHash)
{
    EXPECT_FALSE(UserRoleStore::verifyPassword("any", "not-a-hash"));
    EXPECT_FALSE(UserRoleStore::verifyPassword("any", ""));
    EXPECT_FALSE(UserRoleStore::verifyPassword("any", "$pbkdf2-sha256$"));
    EXPECT_FALSE(UserRoleStore::verifyPassword("any", "$pbkdf2-sha256$bad$data"));
}

// ---------------------------------------------------------------------------
// Fail-closed behavior (empty store)
// ---------------------------------------------------------------------------

/**
 * @brief An empty store (no file) must reject all login attempts.
 *        This is the fail-closed invariant.
 */
TEST(UserRoleStoreTest, EmptyStoreRejectsAllLogins)
{
    UserRoleStore store; // no file path -- empty store
    EXPECT_FALSE(store.hasUsers());

    auto result = store.authenticate("admin", "admin");
    EXPECT_TRUE(result.is_err())
        << "Empty store must reject all authentication attempts (fail closed)";
}

/**
 * @brief A store constructed with a non-existent file must behave as empty
 *        (fail closed).
 */
TEST(UserRoleStoreTest, MissingFileRejectsAllLogins)
{
    UserRoleStore store("/nonexistent/path/users.json");
    EXPECT_FALSE(store.hasUsers());

    auto result = store.authenticate("admin", "password");
    EXPECT_TRUE(result.is_err())
        << "Missing users file must result in fail-closed behavior";
}

// ---------------------------------------------------------------------------
// File-based authentication
// ---------------------------------------------------------------------------

/**
 * @brief Creates a temporary users.json, loads it, and verifies
 *        that authentication works with correct credentials and fails
 *        with incorrect ones.
 */
TEST(UserRoleStoreTest, AuthenticateFromFile)
{
    // Generate a hash for the test password
    auto adminResult = UserRoleStore::hashPassword("adminpass");
    auto teacherResult = UserRoleStore::hashPassword("teacherpass");
    ASSERT_TRUE(adminResult.is_ok());
    ASSERT_TRUE(teacherResult.is_ok());
    const auto adminHash = adminResult.take();
    const auto teacherHash = teacherResult.take();

    // Write a temporary users.json
    const std::string tmpFile = "test_users_temp.json";
    {
        nlohmann::json j;
        j["users"] = nlohmann::json::array({
            {{"username", "admin"}, {"passwordHash", adminHash}, {"role", "admin"}},
            {{"username", "teacher1"}, {"passwordHash", teacherHash}, {"role", "teacher"}}
        });
        std::ofstream ofs(tmpFile);
        ofs << j.dump(2);
    }

    UserRoleStore store(tmpFile);
    EXPECT_TRUE(store.hasUsers());

    // Correct admin credentials -> admin role
    {
        auto result = store.authenticate("admin", "adminpass");
        ASSERT_TRUE(result.is_ok()) << "Admin with correct password must authenticate";
        EXPECT_EQ(result.value(), "admin") << "Admin user must have admin role";
    }

    // Correct teacher credentials -> teacher role
    {
        auto result = store.authenticate("teacher1", "teacherpass");
        ASSERT_TRUE(result.is_ok()) << "Teacher with correct password must authenticate";
        EXPECT_EQ(result.value(), "teacher") << "Teacher user must have teacher role";
    }

    // Wrong password -> fail
    {
        auto result = store.authenticate("admin", "wrongpass");
        EXPECT_TRUE(result.is_err()) << "Wrong password must fail";
    }

    // Unknown user -> fail
    {
        auto result = store.authenticate("nonexistent", "anypass");
        EXPECT_TRUE(result.is_err()) << "Unknown user must fail";
    }

    // Clean up
    std::remove(tmpFile.c_str());
}

/**
 * @brief Verifies that a corrupt JSON file results in fail-closed behavior
 *        (no users loaded, all auth fails).
 */
TEST(UserRoleStoreTest, CorruptFileFailsClosed)
{
    const std::string tmpFile = "test_users_corrupt.json";
    {
        std::ofstream ofs(tmpFile);
        ofs << "THIS IS NOT VALID JSON {{{";
    }

    UserRoleStore store(tmpFile);
    EXPECT_FALSE(store.hasUsers());

    auto result = store.authenticate("admin", "admin");
    EXPECT_TRUE(result.is_err())
        << "Corrupt users file must result in fail-closed behavior";

    std::remove(tmpFile.c_str());
}

/**
 * @brief Verifies that a JSON file missing the "users" array results in
 *        fail-closed behavior.
 */
TEST(UserRoleStoreTest, MissingUsersArrayFailsClosed)
{
    const std::string tmpFile = "test_users_noarray.json";
    {
        std::ofstream ofs(tmpFile);
        ofs << R"({"notUsers": []})";
    }

    UserRoleStore store(tmpFile);
    EXPECT_FALSE(store.hasUsers());

    auto result = store.authenticate("admin", "admin");
    EXPECT_TRUE(result.is_err());

    std::remove(tmpFile.c_str());
}

/**
 * @brief SECURITY: Verifies that sending username="admin" alone (the old attack)
 *        no longer grants admin role. This is the exact attack scenario
 *        that the fix prevents.
 */
TEST(UserRoleStoreTest, UsernameAdminAloneDoesNotGrantAdminRole)
{
    // Empty store: the old code would have returned "admin" role here.
    // The new code must reject this.
    UserRoleStore store;

    auto result = store.authenticate("admin", "");
    EXPECT_TRUE(result.is_err())
        << "SECURITY: username='admin' with empty password must NOT grant admin role";

    auto result2 = store.authenticate("admin", "admin");
    EXPECT_TRUE(result2.is_err())
        << "SECURITY: username='admin' with guessed password must NOT grant admin role "
           "when no users.json exists";
}
