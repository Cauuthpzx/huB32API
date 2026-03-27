/**
 * @file test_tenant_repository.cpp
 * @brief Unit tests for hub32api::db::TenantRepository.
 *
 * Each test creates its own DatabaseManager in a unique temp directory and
 * cleans up in TearDown() to prevent cross-test interference.
 */

#include <gtest/gtest.h>
#include <filesystem>
#include <sqlite3.h>

#include "db/DatabaseManager.hpp"
#include "db/TenantRepository.hpp"

using hub32api::db::DatabaseManager;
using hub32api::db::TenantRepository;
using hub32api::db::TenantRecord;
using hub32api::ErrorCode;

class TenantRepositoryTest : public ::testing::Test {
protected:
    std::string dir;
    std::unique_ptr<DatabaseManager> dm;
    std::unique_ptr<TenantRepository> repo;

    void SetUp() override {
        dir = "test_data_tenant_" + std::to_string(reinterpret_cast<uintptr_t>(this));
        dm   = std::make_unique<DatabaseManager>(dir);
        ASSERT_TRUE(dm->isOpen()) << "DatabaseManager failed to open";
        repo = std::make_unique<TenantRepository>(*dm);
    }

    void TearDown() override {
        repo.reset();
        dm.reset();
        std::filesystem::remove_all(dir);
    }
};

// ---------------------------------------------------------------------------
// Test: CreateReturnsValidId
// ---------------------------------------------------------------------------

TEST_F(TenantRepositoryTest, CreateReturnsValidId)
{
    auto result = repo->create("Acme Corp", "acme-corp", "owner@acme.com");
    ASSERT_TRUE(result.is_ok()) << "create() must succeed";

    const std::string id = result.value();
    EXPECT_FALSE(id.empty()) << "Returned id must not be empty";
}

// ---------------------------------------------------------------------------
// Test: FindBySlugAfterCreate
// ---------------------------------------------------------------------------

TEST_F(TenantRepositoryTest, FindBySlugAfterCreate)
{
    auto createResult = repo->create("Beta School", "beta-school", "admin@beta.edu");
    ASSERT_TRUE(createResult.is_ok());

    auto findResult = repo->findBySlug("beta-school");
    ASSERT_TRUE(findResult.is_ok()) << "findBySlug() must succeed for a just-created tenant";

    const TenantRecord& rec = findResult.value();
    EXPECT_EQ(rec.id,         createResult.value());
    EXPECT_EQ(rec.slug,       "beta-school");
    EXPECT_EQ(rec.name,       "Beta School");
    EXPECT_EQ(rec.ownerEmail, "admin@beta.edu");
    EXPECT_EQ(rec.status,     "pending");
    EXPECT_EQ(rec.plan,       "trial");
    EXPECT_GT(rec.createdAt,  int64_t{0}) << "created_at must be a positive epoch timestamp";
    EXPECT_EQ(rec.activatedAt, int64_t{0}) << "activatedAt must be 0 before activation";
}

// ---------------------------------------------------------------------------
// Test: DuplicateSlugRejected
// ---------------------------------------------------------------------------

TEST_F(TenantRepositoryTest, DuplicateSlugRejected)
{
    auto first = repo->create("First Org", "shared-slug", "first@org.com");
    ASSERT_TRUE(first.is_ok()) << "First create() must succeed";

    auto second = repo->create("Second Org", "shared-slug", "second@org.com");
    ASSERT_TRUE(second.is_err()) << "Second create() with same slug must fail";
    EXPECT_EQ(second.error().code, ErrorCode::Conflict)
        << "Duplicate slug must return Conflict error";
}

// ---------------------------------------------------------------------------
// Test: ActivateSetsPendingToActive
// ---------------------------------------------------------------------------

TEST_F(TenantRepositoryTest, ActivateSetsPendingToActive)
{
    auto createResult = repo->create("Gamma Inc", "gamma-inc", "ceo@gamma.io");
    ASSERT_TRUE(createResult.is_ok());
    const std::string id = createResult.value();

    auto activateResult = repo->activate(id);
    ASSERT_TRUE(activateResult.is_ok()) << "activate() must succeed";

    auto findResult = repo->findById(id);
    ASSERT_TRUE(findResult.is_ok());

    const TenantRecord& rec = findResult.value();
    EXPECT_EQ(rec.status, "active") << "status must be 'active' after activation";
    EXPECT_GT(rec.activatedAt, int64_t{0}) << "activatedAt must be set after activation";
}

// ---------------------------------------------------------------------------
// Test: SuspendAndUnsuspend
// ---------------------------------------------------------------------------

TEST_F(TenantRepositoryTest, SuspendAndUnsuspend)
{
    auto createResult = repo->create("Delta Ltd", "delta-ltd", "ops@delta.co");
    ASSERT_TRUE(createResult.is_ok());
    const std::string id = createResult.value();

    // Activate first so we have a valid state to suspend from
    ASSERT_TRUE(repo->activate(id).is_ok());

    // Suspend
    auto suspendResult = repo->suspend(id);
    ASSERT_TRUE(suspendResult.is_ok()) << "suspend() must succeed";

    {
        auto findResult = repo->findById(id);
        ASSERT_TRUE(findResult.is_ok());
        EXPECT_EQ(findResult.value().status, "suspended")
            << "status must be 'suspended' after suspend()";
    }

    // Unsuspend
    auto unsuspendResult = repo->unsuspend(id);
    ASSERT_TRUE(unsuspendResult.is_ok()) << "unsuspend() must succeed";

    {
        auto findResult = repo->findById(id);
        ASSERT_TRUE(findResult.is_ok());
        EXPECT_EQ(findResult.value().status, "active")
            << "status must be 'active' after unsuspend()";
    }
}

// ---------------------------------------------------------------------------
// Test: DuplicateEmailRejected
// ---------------------------------------------------------------------------

TEST_F(TenantRepositoryTest, DuplicateEmailRejected)
{
    auto first = repo->create("School A", "school-a", "shared@example.com");
    ASSERT_TRUE(first.is_ok()) << "First create() must succeed";

    auto second = repo->create("School B", "school-b", "shared@example.com");
    ASSERT_TRUE(second.is_err()) << "Second create() with same owner_email must fail";
    EXPECT_EQ(second.error().code, ErrorCode::Conflict)
        << "Duplicate owner_email must return Conflict error";
}

// ---------------------------------------------------------------------------
// Test: FindByIdNotFound
// ---------------------------------------------------------------------------

TEST_F(TenantRepositoryTest, FindByIdNotFound)
{
    auto result = repo->findById("00000000-0000-0000-0000-000000000000");
    ASSERT_TRUE(result.is_err()) << "findById() must fail for a non-existent id";
    EXPECT_EQ(result.error().code, ErrorCode::NotFound);
}
