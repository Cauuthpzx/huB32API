/**
 * @file test_teacher_repository.cpp
 * @brief Unit tests for hub32api::db::TeacherRepository.
 *
 * Each test creates its own DatabaseManager in a unique temp directory and
 * cleans up in TearDown() to prevent cross-test interference.
 */

#include <gtest/gtest.h>
#include <filesystem>
#include <sqlite3.h>

#include "db/DatabaseManager.hpp"
#include "db/TeacherRepository.hpp"

using hub32api::db::DatabaseManager;
using hub32api::db::TeacherRepository;
using hub32api::ErrorCode;

class TeacherRepositoryTest : public ::testing::Test {
protected:
    std::string dir;
    std::unique_ptr<DatabaseManager>   dm;
    std::unique_ptr<TeacherRepository> repo;

    void SetUp() override {
        dir  = "test_data_teacher_" + std::to_string(reinterpret_cast<uintptr_t>(this));
        dm   = std::make_unique<DatabaseManager>(dir);
        ASSERT_TRUE(dm->isOpen()) << "DatabaseManager failed to open";
        repo = std::make_unique<TeacherRepository>(*dm);
    }

    void TearDown() override {
        repo.reset();
        dm.reset();
        std::filesystem::remove_all(dir);
    }
};

// ---------------------------------------------------------------------------
// Test: CreateAndFind
// ---------------------------------------------------------------------------

TEST_F(TeacherRepositoryTest, CreateAndFind)
{
    auto createResult = repo->create("jsmith", "secret123", "John Smith", "teacher");
    ASSERT_TRUE(createResult.is_ok()) << "create() must succeed";

    const std::string id = createResult.value();
    EXPECT_FALSE(id.empty()) << "Returned id must not be empty";

    auto findResult = repo->findById(id);
    ASSERT_TRUE(findResult.is_ok()) << "findById() must succeed for a just-created teacher";

    const auto& rec = findResult.value();
    EXPECT_EQ(rec.id,       id);
    EXPECT_EQ(rec.username, "jsmith");
    EXPECT_EQ(rec.fullName, "John Smith");
    EXPECT_EQ(rec.role,     "teacher");
    EXPECT_GT(rec.createdAt, int64_t{0}) << "createdAt must be a positive epoch timestamp";
}

// ---------------------------------------------------------------------------
// Test: FindByUsername
// ---------------------------------------------------------------------------

TEST_F(TeacherRepositoryTest, FindByUsername)
{
    auto createResult = repo->create("adoe", "password42", "Alice Doe", "admin");
    ASSERT_TRUE(createResult.is_ok());
    const std::string id = createResult.value();

    auto findResult = repo->findByUsername("adoe");
    ASSERT_TRUE(findResult.is_ok()) << "findByUsername() must succeed";

    const auto& rec = findResult.value();
    EXPECT_EQ(rec.id,       id);
    EXPECT_EQ(rec.username, "adoe");
    EXPECT_EQ(rec.fullName, "Alice Doe");
    EXPECT_EQ(rec.role,     "admin");
}

// ---------------------------------------------------------------------------
// Test: Authenticate
// ---------------------------------------------------------------------------

TEST_F(TeacherRepositoryTest, Authenticate)
{
    auto createResult = repo->create("tuser", "test123", "Test User", "teacher");
    ASSERT_TRUE(createResult.is_ok());

    auto authResult = repo->authenticate("tuser", "test123");
    ASSERT_TRUE(authResult.is_ok()) << "authenticate() must succeed with correct password";
    EXPECT_EQ(authResult.value().role, "teacher") << "authenticate() must return TeacherRecord with correct role";
}

// ---------------------------------------------------------------------------
// Test: AuthenticateFail
// ---------------------------------------------------------------------------

TEST_F(TeacherRepositoryTest, AuthenticateFail)
{
    auto createResult = repo->create("badpw", "correct_pass", "Bad Password User", "teacher");
    ASSERT_TRUE(createResult.is_ok());

    auto authResult = repo->authenticate("badpw", "wrong_pass");
    ASSERT_TRUE(authResult.is_err()) << "authenticate() must fail with wrong password";
    EXPECT_EQ(authResult.error().code, ErrorCode::AuthenticationFailed);

    // Verify the error message is the same for non-existent user (no enumeration)
    auto authResultNoUser = repo->authenticate("no_such_user", "whatever");
    ASSERT_TRUE(authResultNoUser.is_err());
    EXPECT_EQ(authResultNoUser.error().code, ErrorCode::AuthenticationFailed);
    EXPECT_EQ(authResultNoUser.error().message, authResult.error().message)
        << "Error message must be identical for wrong-password and user-not-found";
}

// ---------------------------------------------------------------------------
// Test: Update
// ---------------------------------------------------------------------------

TEST_F(TeacherRepositoryTest, Update)
{
    auto createResult = repo->create("updateme", "pw123", "Old Name", "teacher");
    ASSERT_TRUE(createResult.is_ok());
    const std::string id = createResult.value();

    auto updateResult = repo->update(id, "New Name", "admin");
    ASSERT_TRUE(updateResult.is_ok()) << "update() must succeed";

    auto findResult = repo->findById(id);
    ASSERT_TRUE(findResult.is_ok());
    EXPECT_EQ(findResult.value().fullName, "New Name");
    EXPECT_EQ(findResult.value().role,     "admin");
}

// ---------------------------------------------------------------------------
// Test: ChangePassword
// ---------------------------------------------------------------------------

TEST_F(TeacherRepositoryTest, ChangePassword)
{
    auto createResult = repo->create("changepw", "old_password", "Change PW User", "teacher");
    ASSERT_TRUE(createResult.is_ok());
    const std::string id = createResult.value();

    // Old password should work
    auto authOld = repo->authenticate("changepw", "old_password");
    ASSERT_TRUE(authOld.is_ok()) << "old password must work before change";

    auto changeResult = repo->changePassword(id, "new_password");
    ASSERT_TRUE(changeResult.is_ok()) << "changePassword() must succeed";

    // Old password must no longer work
    auto authOldAfter = repo->authenticate("changepw", "old_password");
    ASSERT_TRUE(authOldAfter.is_err()) << "old password must fail after change";

    // New password must work
    auto authNew = repo->authenticate("changepw", "new_password");
    ASSERT_TRUE(authNew.is_ok()) << "new password must work after change";
    EXPECT_EQ(authNew.value().role, "teacher");
}
